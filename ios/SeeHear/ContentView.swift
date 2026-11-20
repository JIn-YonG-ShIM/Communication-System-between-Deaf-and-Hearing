import SwiftUI
import WebKit

struct ContentView: View {
    @StateObject private var bridge = BLEBridge()
    var body: some View {
        ZStack(alignment: .top) {
            Color(red: 0.66, green: 0.77, blue: 0.65).ignoresSafeArea()   // 세이지 (로딩 중 배경)
            WebView(bridge: bridge).ignoresSafeArea()
        }
        .sheet(isPresented: $bridge.showPicker) {
            DevicePicker(bridge: bridge)
        }
    }
}

/// 블루투스 기기 고르기 창 (웹의 requestDevice 가 띄움)
struct DevicePicker: View {
    @ObservedObject var bridge: BLEBridge
    var body: some View {
        NavigationView {
            List {
                if bridge.found.isEmpty {
                    HStack { ProgressView(); Text("클립을 찾는 중… 클립 전원을 확인해 주세요").foregroundColor(.secondary) }
                }
                ForEach(bridge.found, id: \.id) { d in
                    Button {
                        bridge.pick(d.id)
                    } label: {
                        HStack {
                            Image(systemName: "dot.radiowaves.left.and.right")
                            VStack(alignment: .leading) {
                                Text(d.name).font(.headline)
                                Text("신호 \(d.rssi) dBm").font(.caption).foregroundColor(.secondary)
                            }
                        }
                    }
                }
            }
            .navigationTitle("클립 고르기")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("취소") { bridge.cancelPick() }
                }
            }
        }
    }
}

/// 전체화면 웹뷰. 페이지가 뜨기 전에 webble.js(웹 블루투스·음성 인식 폴리필)를 끼워 넣어요.
struct WebView: UIViewRepresentable {
    let bridge: BLEBridge

    func makeUIView(context: Context) -> WKWebView {
        let cfg = WKWebViewConfiguration()
        cfg.allowsInlineMediaPlayback = true
        cfg.mediaTypesRequiringUserActionForPlayback = []
        cfg.defaultWebpagePreferences.preferredContentMode = .mobile   // 아이패드에서도 '데스크톱 사이트'로 바뀌지 않게
        let ucc = WKUserContentController()
        if let p = Bundle.main.url(forResource: "webble", withExtension: "js"),
           let js = try? String(contentsOf: p) {
            ucc.addUserScript(WKUserScript(source: js, injectionTime: .atDocumentStart, forMainFrameOnly: true))
        }
        ucc.add(bridge, name: "ble")
        ucc.add(bridge, name: "speech")
        cfg.userContentController = ucc

        let wv = WKWebView(frame: .zero, configuration: cfg)
        wv.scrollView.contentInsetAdjustmentBehavior = .never   // 상태바 밑까지 꽉 채움
        wv.scrollView.bounces = false
        wv.isOpaque = false
        wv.backgroundColor = .clear
        wv.allowsBackForwardNavigationGestures = false
        wv.navigationDelegate = context.coordinator
        bridge.webView = wv
        wv.load(URLRequest(url: AppConfig.url, cachePolicy: .reloadRevalidatingCacheData))
        return wv
    }
    func updateUIView(_ uiView: WKWebView, context: Context) {}

    func makeCoordinator() -> Coordinator { Coordinator(bridge: bridge) }
    final class Coordinator: NSObject, WKNavigationDelegate {
        let bridge: BLEBridge
        init(bridge: BLEBridge) { self.bridge = bridge }
        // 화면(웹 페이지)이 새로 시작되면 예전 클립 연결을 놓아요. 화면은 '연결 안 됨'인데 속으로는 붙잡고 있는 일이 없게.
        func webView(_ webView: WKWebView, didStartProvisionalNavigation navigation: WKNavigation!) { bridge.releaseClip() }
        // iOS 가 뒤에 있던 화면을 메모리에서 내렸을 때: 클립을 놓고 화면을 다시 불러와요.
        func webViewWebContentProcessDidTerminate(_ webView: WKWebView) { bridge.releaseClip(); webView.reload() }
    }
}
