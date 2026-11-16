/* SeeHear iOS 앱 — 웹 블루투스 · 음성 인식 폴리필
   WKWebView 에는 navigator.bluetooth 와 webkitSpeechRecognition 이 없어서,
   seehear-*.html 이 쓰는 만큼만 흉내 내고 실제 일은 Swift(BLEBridge)에 넘겨요. */
(function () {
  if (!window.webkit || !webkit.messageHandlers || !webkit.messageHandlers.ble) return;
  let seq = 0;
  const waits = {};
  const call = (handler, cmd, extra) => new Promise((resolve, reject) => {
    const id = 'r' + (++seq);
    waits[id] = { resolve, reject };
    webkit.messageHandlers[handler].postMessage(Object.assign({ id, cmd }, extra || {}));
  });

  // ---- Swift → 웹 ----
  const listeners = {};                      // 이벤트 이름 → [함수]
  window.__webble = {
    resolve(id, v) { const w = waits[id]; if (w) { delete waits[id]; w.resolve(v); } },
    reject(id, why) { const w = waits[id]; if (w) { delete waits[id]; w.reject(new Error(why)); } },
    event(name, v) { (listeners[name] || []).forEach(f => { try { f(v); } catch (e) {} }); }
  };
  const on = (name, f) => { (listeners[name] = listeners[name] || []).push(f); };

  // ---- navigator.bluetooth ----
  class EvTarget {
    constructor() { this._l = {}; }
    addEventListener(t, f) { (this._l[t] = this._l[t] || []).push(f); }
    removeEventListener(t, f) { this._l[t] = (this._l[t] || []).filter(x => x !== f); }
    dispatch(t, ev) { (this._l[t] || []).forEach(f => f(ev)); }
  }
  class Characteristic extends EvTarget {
    constructor(service, uuid) { super(); this.service = service; this.uuid = uuid; this.value = null; }
    _bytes(v) { const u8 = v instanceof ArrayBuffer ? new Uint8Array(v) : new Uint8Array(v.buffer || v); return Array.from(u8); }
    writeValue(v) { return call('ble', 'write', { service: this.service.uuid, uuid: this.uuid, bytes: this._bytes(v), response: true }); }
    writeValueWithResponse(v) { return this.writeValue(v); }
    writeValueWithoutResponse(v) { return call('ble', 'write', { service: this.service.uuid, uuid: this.uuid, bytes: this._bytes(v), response: false }); }
    async startNotifications() { await call('ble', 'startNotifications', { service: this.service.uuid, uuid: this.uuid }); return this; }
    async stopNotifications() { return this; }
  }
  class Service {
    constructor(device, uuid) { this.device = device; this.uuid = uuid; this._chars = {}; }
    async getCharacteristic(uuid) {
      uuid = String(uuid).toLowerCase();
      if (!this._chars[uuid]) { await call('ble', 'getCharacteristic', { service: this.uuid, uuid }); this._chars[uuid] = new Characteristic(this, uuid); }
      return this._chars[uuid];
    }
  }
  class GattServer {
    constructor(device) { this.device = device; this.connected = false; this._svcs = {}; }
    async connect() { await call('ble', 'connect', { device: this.device.id }); this.connected = true; return this; }
    disconnect() { this.connected = false; call('ble', 'disconnect', {}); }
    async getPrimaryService(uuid) {
      uuid = String(uuid).toLowerCase();
      if (!this._svcs[uuid]) { await call('ble', 'getPrimaryService', { uuid }); this._svcs[uuid] = new Service(this.device, uuid); }
      return this._svcs[uuid];
    }
  }
  class Device extends EvTarget {
    constructor(id, name) { super(); this.id = id; this.name = name; this.gatt = new GattServer(this); }
  }
  let current = null;
  // 특성 값 알림 → characteristicvaluechanged
  on('value', v => {
    if (!current) return;
    const [su, cu] = v.key.split('|');
    const svc = current.gatt._svcs[su]; const ch = svc && svc._chars[cu]; if (!ch) return;
    const dv = new DataView(new Uint8Array(v.bytes).buffer);
    ch.value = dv;
    ch.dispatch('characteristicvaluechanged', { target: ch });
  });
  on('disconnected', () => { if (current) { current.gatt.connected = false; current.dispatch('gattserverdisconnected', { target: current }); } });

  navigator.bluetooth = {
    async requestDevice(opts) {
      const f = (opts && opts.filters && opts.filters[0]) || {};
      const namePrefix = f.namePrefix || f.name || 'SeeHear';
      const services = (opts && opts.optionalServices) || [];
      const r = await call('ble', 'requestDevice', { namePrefix, services });
      current = new Device(r.id, r.name);
      return current;
    },
    getAvailability: async () => true
  };

  // ---- webkitSpeechRecognition (SFSpeechRecognizer 로) ----
  if (webkit.messageHandlers.speech) {
    class Rec {
      constructor() { this.lang = 'ko-KR'; this.continuous = true; this.interimResults = true; this.onresult = null; this.onerror = null; this.onend = null; this.onstart = null; Rec._inst = this; }
      start() { call('speech', 'start', {}); }
      stop() { call('speech', 'stop', {}); }
      abort() { this.stop(); }
    }
    on('speech.result', v => {
      const r = Rec._inst; if (!r || !r.onresult) return;
      const res = [[{ transcript: v.transcript, confidence: 0.9 }]]; res[0].isFinal = !!v.final;
      r.onresult({ resultIndex: 0, results: res });
    });
    on('speech.error', why => { const r = Rec._inst; if (r && r.onerror) r.onerror({ error: why }); });
    on('speech.start', () => { const r = Rec._inst; if (r && r.onstart) r.onstart(); });
    on('speech.end', () => { const r = Rec._inst; if (r && r.onend) r.onend(); });
    window.webkitSpeechRecognition = Rec;
    window.SpeechRecognition = Rec;
  }

  // 앱 안이라는 표시 (페이지가 상태바 자리를 비워 두게)
  window.__nativeApp = true;
  document.addEventListener('DOMContentLoaded', () => { document.documentElement.classList.add('in-native-app'); });
})();
