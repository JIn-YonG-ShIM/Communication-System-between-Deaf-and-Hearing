import Foundation
import Combine
import CoreBluetooth
import WebKit
import Speech
import AVFoundation
import UIKit

/// 웹(webble.js) ↔ iOS 사이의 다리.
/// 웹이 window.webkit.messageHandlers.ble.postMessage({...}) 로 부르면 여기서 CoreBluetooth 를 움직이고,
/// 결과는 window.__webble.resolve(id, 값) / __webble.reject(id, 이유) / __webble.event(이름, 값) 으로 돌려줘요.
final class BLEBridge: NSObject, ObservableObject, WKScriptMessageHandler {
    weak var webView: WKWebView?

    // ---- 기기 고르기 창 ----
    struct Found: Identifiable { let id: UUID; let name: String; let rssi: Int }
    @Published var showPicker = false
    @Published var found: [Found] = []
    private var pickRequestId: String?
    private var namePrefix = AppConfig.defaultNamePrefix

    // ---- 블루투스 ----
    private var central: CBCentralManager!
    private var peripherals: [UUID: CBPeripheral] = [:]
    private var connected: CBPeripheral?
    private var services: [String: CBService] = [:]          // uuid 문자열(소문자) → 서비스
    private var chars: [String: CBCharacteristic] = [:]      // "svc|chr" → 특성
    private var pending: [String: String] = [:]              // 작업 키 → 요청 id (연결, 서비스 찾기 등)
    private var wantedServices: [CBUUID] = []

    // ---- 음성 인식 ----
    private let recognizer = SFSpeechRecognizer(locale: Locale(identifier: "ko-KR"))
    private var audioEngine: AVAudioEngine?
    private var recRequest: SFSpeechAudioBufferRecognitionRequest?
    private var recTask: SFSpeechRecognitionTask?
    private var speechOn = false

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
        // 앱을 닫거나(홈으로 나가기·앱 끄기) 화면이 잠기면 클립을 놓아줘요.
        // 붙잡은 채로 남아 있으면 클립이 '나 여기 있어요' 신호를 멈춰서, 다시 켰을 때 목록에 안 보여요.
        let nc = NotificationCenter.default
        nc.addObserver(self, selector: #selector(appClosing), name: UIApplication.didEnterBackgroundNotification, object: nil)
        nc.addObserver(self, selector: #selector(appClosing), name: UIApplication.willTerminateNotification, object: nil)
        UIApplication.shared.isIdleTimerDisabled = true   // 전시 중 화면이 저절로 잠기지 않게
    }
    @objc private func appClosing() { releaseClip() }

    /// 붙잡고 있는 클립을 전부 놓아요 (연결 끊기). 끊기면 클립은 빛을 끄고 다시 찾을 수 있는 상태가 돼요.
    func releaseClip() {
        if let p = connected { central.cancelPeripheralConnection(p) }
        for p in peripherals.values where p.state == .connected || p.state == .connecting { central.cancelPeripheralConnection(p) }
        if !wantedServices.isEmpty {
            for p in central.retrieveConnectedPeripherals(withServices: wantedServices) { central.cancelPeripheralConnection(p) }
        }
        connected = nil; services = [:]; chars = [:]; pending = [:]
    }

    // ================= 웹 → iOS =================
    func userContentController(_ ucc: WKUserContentController, didReceive message: WKScriptMessage) {
        guard let body = message.body as? [String: Any] else { return }
        let id = body["id"] as? String ?? ""
        let cmd = body["cmd"] as? String ?? ""
        if message.name == "speech" { handleSpeech(cmd: cmd, id: id, body: body); return }

        switch cmd {
        case "requestDevice":
            namePrefix = body["namePrefix"] as? String ?? AppConfig.defaultNamePrefix
            wantedServices = (body["services"] as? [String] ?? []).map { CBUUID(string: $0) }
            startPick(requestId: id)
        case "connect":
            guard let p = connected ?? peripherals.values.first(where: { $0.identifier.uuidString == body["device"] as? String }) else { reject(id, "no device"); return }
            pending["connect"] = id
            central.connect(p, options: nil)
        case "disconnect":
            if let p = connected { central.cancelPeripheralConnection(p) }
            resolve(id, true)
        case "getPrimaryService":
            guard let p = connected else { reject(id, "not connected"); return }
            let u = (body["uuid"] as? String ?? "").lowercased()
            if services[u] != nil { resolve(id, u); return }
            pending["svc:" + u] = id
            p.discoverServices([CBUUID(string: u)])
        case "getCharacteristic":
            guard let p = connected else { reject(id, "not connected"); return }
            let su = (body["service"] as? String ?? "").lowercased(), cu = (body["uuid"] as? String ?? "").lowercased()
            if chars[su + "|" + cu] != nil { resolve(id, cu); return }
            guard let svc = services[su] else { reject(id, "service not found"); return }
            pending["chr:" + su + "|" + cu] = id
            p.discoverCharacteristics([CBUUID(string: cu)], for: svc)
        case "write":
            guard let p = connected else { reject(id, "not connected"); return }
            let key = (body["service"] as? String ?? "").lowercased() + "|" + (body["uuid"] as? String ?? "").lowercased()
            guard let c = chars[key], let bytes = body["bytes"] as? [Int] else { reject(id, "characteristic not found"); return }
            let data = Data(bytes.map { UInt8(truncatingIfNeeded: $0) })
            let withResponse = (body["response"] as? Bool) ?? false
            if withResponse && c.properties.contains(.write) { pending["write"] = id; p.writeValue(data, for: c, type: .withResponse) }
            else { p.writeValue(data, for: c, type: .withoutResponse); resolve(id, true) }
        case "startNotifications":
            guard let p = connected else { reject(id, "not connected"); return }
            let key = (body["service"] as? String ?? "").lowercased() + "|" + (body["uuid"] as? String ?? "").lowercased()
            guard let c = chars[key] else { reject(id, "characteristic not found"); return }
            pending["notify:" + key] = id
            p.setNotifyValue(true, for: c)
        default:
            reject(id, "unknown command " + cmd)
        }
    }

    // ================= 기기 고르기 =================
    private func startPick(requestId: String) {
        pickRequestId = requestId
        found = []
        showPicker = true
        if central.state == .poweredOn {
            releaseClip()   // 예전 연결이 남아 있으면 먼저 놓아야 클립이 목록에 다시 나타나요
            central.scanForPeripherals(withServices: nil, options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
        }
    }
    func pick(_ id: UUID) {
        central.stopScan()
        guard let p = peripherals[id], let rid = pickRequestId else { return }
        connected = p; p.delegate = self
        showPicker = false; pickRequestId = nil
        resolve(rid, ["id": p.identifier.uuidString, "name": p.name ?? namePrefix])
    }
    func cancelPick() {
        central.stopScan(); showPicker = false
        if let rid = pickRequestId { reject(rid, "User cancelled") }
        pickRequestId = nil
    }

    // ================= iOS → 웹 =================
    private func js(_ s: String) { DispatchQueue.main.async { self.webView?.evaluateJavaScript(s, completionHandler: nil) } }
    private func json(_ v: Any) -> String {
        if let d = try? JSONSerialization.data(withJSONObject: v, options: [.fragmentsAllowed]), let s = String(data: d, encoding: .utf8) { return s }
        return "null"
    }
    private func resolve(_ id: String, _ v: Any) { js("window.__webble && __webble.resolve(\(json(id)), \(json(v)))") }
    private func reject(_ id: String, _ why: String) { js("window.__webble && __webble.reject(\(json(id)), \(json(why)))") }
    private func event(_ name: String, _ v: Any) { js("window.__webble && __webble.event(\(json(name)), \(json(v)))") }

    // ================= 음성 인식 (webkitSpeechRecognition 대용) =================
    private func handleSpeech(cmd: String, id: String, body: [String: Any]) {
        switch cmd {
        case "start":
            SFSpeechRecognizer.requestAuthorization { auth in
                DispatchQueue.main.async {
                    guard auth == .authorized else { self.event("speech.error", "not-allowed"); return }
                    AVAudioSession.sharedInstance().requestRecordPermission { ok in
                        DispatchQueue.main.async { ok ? self.startSpeech() : self.event("speech.error", "not-allowed") }
                    }
                }
            }
            resolve(id, true)
        case "stop":
            stopSpeech(); resolve(id, true)
        default: reject(id, "unknown")
        }
    }
    private func startSpeech() {
        stopSpeech()
        guard let recognizer = recognizer, recognizer.isAvailable else { event("speech.error", "service-not-allowed"); return }
        let session = AVAudioSession.sharedInstance()
        try? session.setCategory(.playAndRecord, mode: .measurement, options: [.duckOthers, .defaultToSpeaker, .allowBluetooth])
        try? session.setActive(true, options: .notifyOthersOnDeactivation)
        let engine = AVAudioEngine()
        let req = SFSpeechAudioBufferRecognitionRequest()
        req.shouldReportPartialResults = true
        let input = engine.inputNode
        let fmt = input.outputFormat(forBus: 0)
        input.installTap(onBus: 0, bufferSize: 1024, format: fmt) { buf, _ in req.append(buf) }
        engine.prepare()
        do { try engine.start() } catch { event("speech.error", "audio-capture"); return }
        audioEngine = engine; recRequest = req; speechOn = true
        event("speech.start", true)
        recTask = recognizer.recognitionTask(with: req) { [weak self] result, err in
            guard let self = self else { return }
            if let r = result {
                self.event("speech.result", ["transcript": r.bestTranscription.formattedString, "final": r.isFinal])
            }
            if err != nil || (result?.isFinal ?? false) {
                // iOS 는 한 번에 ~1분까지만 들어요. 켜 두는 동안엔 끝나면 다시 시작.
                let again = self.speechOn
                self.teardownSpeech()
                if again { DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { self.startSpeech() } }
                else { self.event("speech.end", true) }
            }
        }
    }
    private func teardownSpeech() {
        recTask?.cancel(); recTask = nil
        recRequest?.endAudio(); recRequest = nil
        audioEngine?.stop(); audioEngine?.inputNode.removeTap(onBus: 0); audioEngine = nil
    }
    private func stopSpeech() { speechOn = false; teardownSpeech() }
}

// ================= CoreBluetooth =================
extension BLEBridge: CBCentralManagerDelegate, CBPeripheralDelegate {
    func centralManagerDidUpdateState(_ c: CBCentralManager) {
        if c.state == .poweredOn, showPicker {
            releaseClip()
            c.scanForPeripherals(withServices: nil, options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
        }
        if c.state != .poweredOn, let rid = pickRequestId { reject(rid, "Bluetooth is off"); showPicker = false; pickRequestId = nil }
    }
    func centralManager(_ c: CBCentralManager, didDiscover p: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = p.name ?? (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? ""
        guard name.hasPrefix(namePrefix) else { return }
        peripherals[p.identifier] = p
        if let i = found.firstIndex(where: { $0.id == p.identifier }) { found[i] = Found(id: p.identifier, name: name, rssi: RSSI.intValue) }
        else { found.append(Found(id: p.identifier, name: name, rssi: RSSI.intValue)) }
    }
    func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        services = [:]; chars = [:]
        if let id = pending.removeValue(forKey: "connect") { resolve(id, true) }
    }
    func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        if let id = pending.removeValue(forKey: "connect") { reject(id, error?.localizedDescription ?? "connect failed") }
    }
    func centralManager(_ c: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        services = [:]; chars = [:]
        event("disconnected", p.identifier.uuidString)
    }
    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        for s in p.services ?? [] { services[s.uuid.uuidString.lowercased()] = s }
        for (k, id) in pending where k.hasPrefix("svc:") {
            let u = String(k.dropFirst(4))
            if services[u] != nil { resolve(id, u) } else { reject(id, "service not found") }
            pending.removeValue(forKey: k)
        }
    }
    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        let su = s.uuid.uuidString.lowercased()
        for c in s.characteristics ?? [] { chars[su + "|" + c.uuid.uuidString.lowercased()] = c }
        for (k, id) in pending where k.hasPrefix("chr:" + su) {
            let key = String(k.dropFirst(4))
            if chars[key] != nil { resolve(id, String(key.split(separator: "|")[1])) } else { reject(id, "characteristic not found") }
            pending.removeValue(forKey: k)
        }
    }
    func peripheral(_ p: CBPeripheral, didWriteValueFor c: CBCharacteristic, error: Error?) {
        if let id = pending.removeValue(forKey: "write") { error == nil ? resolve(id, true) : reject(id, error!.localizedDescription) }
    }
    func peripheral(_ p: CBPeripheral, didUpdateNotificationStateFor c: CBCharacteristic, error: Error?) {
        let key = (c.service?.uuid.uuidString.lowercased() ?? "") + "|" + c.uuid.uuidString.lowercased()
        if let id = pending.removeValue(forKey: "notify:" + key) { error == nil ? resolve(id, true) : reject(id, error!.localizedDescription) }
    }
    func peripheral(_ p: CBPeripheral, didUpdateValueFor c: CBCharacteristic, error: Error?) {
        guard let d = c.value else { return }
        let key = (c.service?.uuid.uuidString.lowercased() ?? "") + "|" + c.uuid.uuidString.lowercased()
        event("value", ["key": key, "bytes": [UInt8](d).map { Int($0) }])
    }
}
