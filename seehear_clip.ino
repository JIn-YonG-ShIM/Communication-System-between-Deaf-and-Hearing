/*
  SeeHear 클립 펌웨어 v1.3 (LED 2개: 왼쪽 / 오른쪽)
  ------------------------------------------------------------
  보드  : Seeed Studio XIAO ESP32-C3  (Arduino IDE: XIAO_ESP32C3, esp32 보드 패키지 3.x)
  LED   : WS2812 2개 (줄줄이 연결)
          XIAO D2 -> 1번 LED Din,  1번 LED Dout -> 2번 LED Din
          두 LED의 5V(+)와 GND(-)는 각각 같은 선에 함께 연결
  마이크: INMP441         SCK -> D3, WS -> D4, SD -> D5, L/R -> GND, VDD -> 3V3
  라이브러리: Adafruit NeoPixel (나머지 BLE, I2S 는 보드 패키지에 포함)

  v1.3 에서 더한 것
    - 큰 소리 감지 : 마이크 소리가 기준(th)보다 크게 약 0.1초 이상 이어지면
                     ① 이 클립이 스스로 빨강 빠르게(DANGER) 켜지고
                     ② 앱에 "SHOUT <크기>" 를 알린다 (BLE 알림 특성 EVT_UUID / 시리얼 "EVT SHOUT <크기>")
                     앱은 이걸 받아 모든 동료의 클립에 DANGER 를 보낸다. 5초 안에는 한 번만.
    - 기준선       : 켜진 뒤 2초 동안 평소 소음을 재고, 그 4배(ratio)를 넘어야 울림. 최소값은 th(기본 2500).
                     조용할 때 기준선은 아주 천천히 따라가므로 매장 소음이 변해도 맞춰짐. "mic on" 으로 숫자 확인.
    - loud on/off  : 큰 소리 감지 켜기/끄기 (기본 켜짐)
  v1.2 에서 더한 것
    - rgb r g b  : 정해진 색 하나를 계속 켠다 (앱의 '클립 색 모드 · 클립으로 확인하기'가 두 색을 번갈아 보낼 때 사용)
    - 안전 끄기  : 앱 연결이 끊기면 빛을 끈다. 빛이 켜진 뒤 60초 동안 아무 명령도 없으면 스스로 끈다.

  하는 일
    1) 앱(또는 시리얼 모니터)이 보낸 '신호 이름'을 받아서 코딩표대로 빛을 낸다.
    2) 마이크로 소리 크기를 잰다. (말소리를 글자로 바꾸는 건 v2에서 방식 결정 후 추가)

  명령 보내는 법
    - 시리얼 모니터 : 속도 115200, 줄 끝 "새 줄(Newline)"
    - 블루투스(BLE): 기기 이름 "SeeHear-Clip", 아래 CMD 특성(characteristic)에 글자로 쓰기
                      (테스트는 휴대폰 앱 nRF Connect 로 가능)

  명령어
    신호 이름 [L|R]  예) DANGER, PERSONAL_REPLY L, YES R  (list 로 전체 보기)
                    L = 왼쪽만, R = 오른쪽만, 안 쓰면 양쪽
    rgb r g b       정해진 색 계속 켜기 (0~255)   예) rgb 255 0 0
    test            왼쪽 -> 오른쪽 순서로 켜서 좌우 장착 확인
    ack             손님 호출 확인 → 빛 끄기 (다른 신호도 끔)
    off             끄기
    list            신호 목록 보기
    cvd on / off    색각이상자용 색 모드 켜기/끄기
    b 숫자          밝기(%)          예) b 30
    mic on / off    소리 크기를 시리얼 창에 계속 보여주기
    th 숫자         큰 소리 최소값   예) th 2500   (기준선×배율이 이보다 작아도 이 값은 넘어야 울림)
    ratio 숫자      큰 소리 배율     예) ratio 4   (평소 소음의 몇 배부터 울릴지, 기본 4)
    calib           평소 소음 기준선 다시 재기 (2초 조용히)
    loud on / off   큰 소리 감지 켜기/끄기
    ?               지금 상태 보기
*/

#include <Adafruit_NeoPixel.h>
#include <ESP_I2S.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ---------------- 핀 ----------------
// XIAO ESP32-C3 의 D2~D5 는 GPIO 4,5,6,7 이에요. 숫자로 적어 두면 보드를 'XIAO_ESP32C3' 가 아닌
// 'ESP32C3 Dev Module' 로 골라도 컴파일돼요. (보드는 XIAO_ESP32C3 를 고르는 걸 권장)
const int LED_PIN = 4;   // D2
const int MIC_SCK = 5;   // D3
const int MIC_WS  = 6;   // D4
const int MIC_SD  = 7;   // D5

// ---------------- LED 순서 ----------------
// 줄의 첫 번째(XIAO에 가까운) LED가 0번이에요.
// 모자에 달아 보고 좌우가 반대면 이 두 숫자만 바꾸세요.
const int LED_COUNT = 2;
const int LEFT  = 0;
const int RIGHT = 1;
const uint8_t SIDE_LEFT = 1, SIDE_RIGHT = 2, SIDE_BOTH = 3;

// ---------------- BLE 주소(UUID) ----------------
// 앱(웹 블루투스)에서도 이 값을 그대로 써야 연결돼요. 바꿔도 되지만 앱과 똑같이 맞추기.
// 클립이 2개 이상이면 이름을 다르게 (예: "SeeHear-BAR", "SeeHear-CS") — 앱은 "SeeHear"로 시작하는 이름을 모두 찾아요
#define DEVICE_NAME  "SeeHear-Clip"
#define SERVICE_UUID "6a4e0001-6c3b-4b1e-9d1a-5ee4ea12c001"
#define CMD_UUID     "6a4e0002-6c3b-4b1e-9d1a-5ee4ea12c001"
#define EVT_UUID     "6a4e0003-6c3b-4b1e-9d1a-5ee4ea12c001"   // 클립 → 앱 알림 (NOTIFY)

// ---------------- 깜박임 속도 ----------------
const float FAST = 2.6;   // 빠름: 답장 요망 / 위험
const float SLOW = 1.0;   // 느림: 정보 수신
const float ON   = 0.0;   // 켜진 빛(계속)

// ---------------- 안전 끄기 ----------------
const unsigned long AUTO_OFF_MS = 60000;   // 빛이 켜진 뒤 이 시간 동안 명령이 없으면 끔 (앱이 평소엔 더 일찍 off 를 보냄)

// ---------------- 코딩표 ----------------
// 정해 둔 클립 코딩표를 그대로 옮겼어요. 바꾸고 싶으면 이 표만 고치면 돼요.
struct Signal {
  const char* code;     // 앱이 보내는 이름
  const char* meaning;  // 사람이 읽는 설명
  uint8_t r, g, b;      // 기본 색
  uint8_t cr, cg, cb;   // 색각이상자 모드 색 (초록 대신 흰색)
  float hz;             // 깜박임 속도 (0 = 계속 켜짐)
};

Signal SIGNALS[] = {
  // code             meaning                    기본 R,G,B       색각이상 R,G,B    속도
  {"DANGER",          "빨강 빠름: 위험한 상황",     255,   0,   0,    255,   0,   0,   FAST},
  {"CALL",            "빨강 켜짐: 손님 호출",       255,   0,   0,    255,   0,   0,   ON  },
  {"GROUP_REPLY",     "노랑 빠름: 그룹 답장요망",   255, 180,   0,    255, 180,   0,   FAST},
  {"GROUP_INFO",      "노랑 느림: 그룹 정보수신",   255, 180,   0,    255, 180,   0,   SLOW},
  {"PERSONAL_REPLY",  "초록 빠름: 개인 답장요망",     0, 255,   0,    255, 255, 255,   FAST},
  {"PERSONAL_INFO",   "초록 느림: 개인 정보수신",     0, 255,   0,    255, 255, 255,   SLOW},
  {"YES",             "파랑 느림: 긍정 대답 수신",    0,   0, 255,      0,   0, 255,   SLOW},
  {"HOLD_REPLY",      "주황 빠름: 보류 답장요망",   255,  70,   0,    255,  70,   0,   FAST},
  {"HOLD_INFO",       "주황 느림: 보류 수신",       255,  70,   0,    255,  70,   0,   SLOW},
};
const int SIGNAL_COUNT = sizeof(SIGNALS) / sizeof(SIGNALS[0]);

// ---------------- 상태 ----------------
Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
I2SClass i2s;

int  current = -1;          // 지금 켜진 신호 번호 (-1 = 꺼짐)
uint8_t sides = SIDE_BOTH;  // 어느 쪽 LED를 켤지
bool rawOn = false;         // rgb 명령으로 켠 색인지
uint8_t rawR = 0, rawG = 0, rawB = 0;
bool cvdMode = false;       // 색각이상자 모드
int  brightness = 10;       // 밝기 % (앱: 낮음 4 · 보통 10 · 높음 25)
bool lit = false;
bool needRedraw = true;
unsigned long cycleStart = 0;
unsigned long lastCmdAt = 0;

bool micReady = false;
bool micPrint = false;
long micLevel = 0;          // 최근 소리 크기
// 큰 소리 감지 — 켜진 뒤 2초 동안 잰 '평소 소음(기준선)'보다 몇 배 커야 울린다
bool loudOn = true;
long loudTh = 2500;                       // 최소 절대값: 기준선이 아주 낮아도 이보다 작은 소리는 무시 (th 로 변경)
float loudRatio = 4.0;                    // 기준선의 몇 배부터 '큰 소리'로 볼지 (ratio 로 변경)
long baseline = 0;                        // 평소 소음 (계속 천천히 따라감)
bool baselineReady = false;
unsigned long micStartAt = 0;
const unsigned long CALIB_MS = 2000;      // 켜진 뒤 기준선 잡는 시간
const int LOUD_HOLD = 6;                  // 이 횟수(약 0.1초) 이상 연속으로 넘어야 인정 (딸깍 소리 거르기)
const unsigned long LOUD_COOLDOWN = 5000; // 한 번 울린 뒤 다시 울리기까지
int loudCount = 0;
unsigned long lastLoudAt = 0;
BLECharacteristic* evtChr = nullptr;
void onLoud(long level);   // 아래에 정의
long loudLimit() { long byRatio = (long)(baseline * loudRatio); return byRatio > loudTh ? byRatio : loudTh; }

// BLE 로 들어온 명령은 블루투스 작업 흐름에서 도착하므로, 잠깐 담아 두었다가 loop() 에서 처리
volatile bool bleHasCmd = false;
char bleCmd[48];
bool bleConnected = false;

// ================= LED =================
void drawLed(bool on) {
  uint32_t color = 0;
  if (on && rawOn) {
    color = led.Color(rawR * brightness / 100, rawG * brightness / 100, rawB * brightness / 100);
  } else if (on && current >= 0) {
    const Signal& s = SIGNALS[current];
    uint8_t r = cvdMode ? s.cr : s.r;
    uint8_t g = cvdMode ? s.cg : s.g;
    uint8_t b = cvdMode ? s.cb : s.b;
    color = led.Color(r * brightness / 100, g * brightness / 100, b * brightness / 100);
  }
  led.setPixelColor(LEFT,  (sides & SIDE_LEFT)  ? color : 0);
  led.setPixelColor(RIGHT, (sides & SIDE_RIGHT) ? color : 0);
  led.show();
  lit = on;
}

void updateLed() {
  bool wantOn = false;
  if (rawOn) {
    wantOn = true;
  } else if (current >= 0) {
    float hz = SIGNALS[current].hz;
    if (hz <= 0) {
      wantOn = true;
    } else {
      unsigned long period = (unsigned long)(1000.0f / hz);
      wantOn = ((millis() - cycleStart) % period) < period / 2;   // 켜짐 50%
    }
  }
  if (wantOn != lit || needRedraw) {
    drawLed(wantOn);
    needRedraw = false;
  }
  // 안전 끄기: 켜진 채로 오래 명령이 없으면 끔
  if ((rawOn || current >= 0) && millis() - lastCmdAt > AUTO_OFF_MS) {
    Serial.println("[안전] 오래 명령이 없어 빛을 끕니다");
    allOff();
  }
}

void allOff() {
  current = -1; rawOn = false; sides = SIDE_BOTH; needRedraw = true;
}

void showSignal(int idx, uint8_t which = SIDE_BOTH) {
  rawOn = false;
  current = idx;
  sides = which;
  cycleStart = millis();
  needRedraw = true;
  const char* sideName = which == SIDE_LEFT ? "왼쪽" : which == SIDE_RIGHT ? "오른쪽" : "양쪽";
  if (idx >= 0) Serial.printf("[신호] %s (%s) - %s\n", SIGNALS[idx].code, sideName, SIGNALS[idx].meaning);
  else          Serial.println("[신호] 꺼짐");
}

void showRaw(int r, int g, int b) {
  rawOn = true; current = -1; sides = SIDE_BOTH;
  rawR = constrain(r, 0, 255); rawG = constrain(g, 0, 255); rawB = constrain(b, 0, 255);
  needRedraw = true;
  Serial.printf("[색] rgb %d %d %d\n", rawR, rawG, rawB);
}

// ================= 마이크 =================
void setupMic() {
  i2s.setPins(MIC_SCK, MIC_WS, -1, MIC_SD);
  micReady = i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_32BIT,
                       I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
  Serial.println(micReady ? "[마이크] 준비 완료" : "[마이크] 시작 실패 - 배선/보드 패키지 확인");
}

void updateMic() {
  if (!micReady) return;
  static int32_t buf[256];
  size_t got = i2s.readBytes((char*)buf, sizeof(buf));
  int n = got / sizeof(int32_t);
  if (n == 0) return;

  long mean = 0;
  for (int i = 0; i < n; i++) mean += (buf[i] >> 8) / n;
  long long sum = 0;
  for (int i = 0; i < n; i++) {
    long v = (buf[i] >> 8) - mean;
    sum += (v < 0) ? -v : v;
  }
  micLevel = (long)(sum / n) >> 4;

  // ---- 기준선(평소 소음) ----
  if (micStartAt == 0) micStartAt = millis();
  if (!baselineReady) {
    baseline = baseline == 0 ? micLevel : (baseline * 7 + micLevel) / 8;   // 빠르게 따라감
    if (millis() - micStartAt > CALIB_MS) { baselineReady = true; Serial.printf("[소리] 기준선 %ld (큰 소리 = %ld 이상)\n", baseline, loudLimit()); }
    return;                                                                // 기준선 잡는 동안은 감지 안 함
  }
  if (micLevel < loudLimit()) baseline = (baseline * 63 + micLevel) / 64;  // 조용할 때만 아주 천천히 따라감

  // ---- 큰 소리 감지 ----
  if (loudOn) {
    if (micLevel > loudLimit()) loudCount++; else loudCount = 0;
    if (loudCount >= LOUD_HOLD && millis() - lastLoudAt > LOUD_COOLDOWN) {
      lastLoudAt = millis(); loudCount = 0;
      onLoud(micLevel);
    }
  }

  if (micPrint) {
    static unsigned long last = 0;
    if (millis() - last > 100) {          // 0.1초마다 한 줄
      last = millis();
      int bars = constrain(micLevel / 200, 0, 40);
      Serial.printf("%6ld |", micLevel);
      for (int i = 0; i < bars; i++) Serial.print('#');
      Serial.println();
    }
  }
}

// ================= 큰 소리 → 위험 =================
void notifyApp(const char* msg) {
  Serial.printf("EVT %s\n", msg);                       // USB 로 연결한 앱용
  if (evtChr && bleConnected) {                          // 블루투스로 연결한 앱용
    evtChr->setValue((uint8_t*)msg, strlen(msg));
    evtChr->notify();
  }
}
void onLoud(long level) {
  char msg[32];
  snprintf(msg, sizeof(msg), "SHOUT %ld", level);
  Serial.printf("[소리] 큰 소리 감지 (%ld > %ld, 기준선 %ld) → 위험 신호\n", level, loudLimit(), baseline);
  // 이 클립도 바로 빨강 빠르게 (앱이 없어도 동작)
  for (int i = 0; i < SIGNAL_COUNT; i++) if (strcmp(SIGNALS[i].code, "DANGER") == 0) { current = i; break; }
  rawOn = false; sides = SIDE_BOTH; cycleStart = millis(); lastCmdAt = millis(); needRedraw = true;
  notifyApp(msg);
}

// ================= 명령 처리 =================
void printList() {
  Serial.println("--- 신호 목록 ---");
  for (int i = 0; i < SIGNAL_COUNT; i++) Serial.printf("  %-15s %s\n", SIGNALS[i].code, SIGNALS[i].meaning);
  Serial.println("  신호 이름 뒤에 L 또는 R: 한쪽만 / rgb r g b / ack / off / test / list / cvd on|off / b 숫자 / th 숫자 / ratio 숫자 / calib / loud on|off / mic on|off / ?");
}

void printStatus() {
  Serial.printf("[상태] 신호=%s | 밝기=%d%% | 색각모드=%s | BLE=%s | 소리=%ld | 기준선=%ld | 큰소리=%ld 이상(%s)\n",
                rawOn ? "rgb" : current >= 0 ? SIGNALS[current].code : "꺼짐", brightness,
                cvdMode ? "켜짐" : "꺼짐", bleConnected ? "연결됨" : "대기", micLevel, baseline, loudLimit(), loudOn ? "켜짐" : "꺼짐");
}

void handleCommand(char* line) {
  // 앞뒤 공백 정리
  while (*line == ' ') line++;
  int len = strlen(line);
  while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\r' || line[len - 1] == '\n')) line[--len] = '\0';
  if (len == 0) return;
  lastCmdAt = millis();

  // 대문자로 바꿔서 비교 (앱에서 소문자로 보내도 되게)
  char up[48];
  int i = 0;
  for (; line[i] && i < 47; i++) up[i] = toupper((unsigned char)line[i]);
  up[i] = '\0';

  if (strncmp(up, "RGB ", 4) == 0) {
    int r = 0, g = 0, b = 0;
    if (sscanf(up + 4, "%d %d %d", &r, &g, &b) == 3) { showRaw(r, g, b); return; }
    Serial.println("rgb 는 숫자 세 개가 필요해요. 예) rgb 255 0 0");
    return;
  }

  uint8_t which = SIDE_BOTH;
  char name[48];
  strncpy(name, up, sizeof(name));
  name[sizeof(name) - 1] = '\0';
  char* sp = strchr(name, ' ');
  if (sp) {
    char side = sp[1];
    if (side == 'L' && sp[2] == '\0') which = SIDE_LEFT;
    else if (side == 'R' && sp[2] == '\0') which = SIDE_RIGHT;
    else if (side == 'B' && sp[2] == '\0') which = SIDE_BOTH;
    else sp = NULL;
    if (sp) *sp = '\0';
  }
  for (int s = 0; s < SIGNAL_COUNT; s++) {
    if (strcmp(name, SIGNALS[s].code) == 0) { showSignal(s, which); return; }
  }
  if (strcmp(up, "TEST") == 0) {
    led.clear(); led.setPixelColor(LEFT,  led.Color(60, 60, 60)); led.show(); delay(800);
    led.clear(); led.setPixelColor(RIGHT, led.Color(60, 60, 60)); led.show(); delay(800);
    led.clear(); led.show();
    Serial.println("[테스트] 왼쪽 -> 오른쪽 순서로 켜졌으면 정상");
    needRedraw = true;
    return;
  }
  if (strcmp(up, "ACK") == 0 || strcmp(up, "OFF") == 0) { showSignal(-1); return; }
  if (strcmp(up, "LIST") == 0)    { printList(); return; }
  if (strcmp(up, "?") == 0)       { printStatus(); return; }
  if (strcmp(up, "CVD ON") == 0)  { cvdMode = true;  needRedraw = true; printStatus(); return; }
  if (strcmp(up, "CVD OFF") == 0) { cvdMode = false; needRedraw = true; printStatus(); return; }
  if (strcmp(up, "MIC ON") == 0)  { micPrint = true;  return; }
  if (strcmp(up, "MIC OFF") == 0) { micPrint = false; return; }
  if (strcmp(up, "LOUD ON") == 0)  { loudOn = true;  Serial.println("큰 소리 감지 켜짐"); return; }
  if (strcmp(up, "LOUD OFF") == 0) { loudOn = false; Serial.println("큰 소리 감지 꺼짐"); return; }
  if (up[0] == 'T' && up[1] == 'H' && up[2] == ' ') {
    loudTh = constrain(atol(up + 3), 100L, 1000000L);
    Serial.printf("큰 소리 최소값 = %ld (지금 기준선 %ld → 울리는 값 %ld)\n", loudTh, baseline, loudLimit()); return;
  }
  if (strncmp(up, "RATIO ", 6) == 0) {
    loudRatio = constrain(atof(up + 6), 1.5, 50.0);
    Serial.printf("큰 소리 배율 = %.1f (울리는 값 %ld)\n", loudRatio, loudLimit()); return;
  }
  if (strcmp(up, "CALIB") == 0) { baselineReady = false; baseline = 0; micStartAt = millis(); Serial.println("기준선 다시 잼 (2초 조용히)"); return; }
  if (up[0] == 'B' && up[1] == ' ') {
    brightness = constrain(atoi(up + 2), 0, 100);
    needRedraw = true; printStatus(); return;
  }
  Serial.printf("모르는 명령: %s  (list 를 입력해 보세요)\n", line);
}

void readSerial() {
  static char buf[48];
  static int n = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (n > 0) { buf[n] = '\0'; handleCommand(buf); n = 0; }
    } else if (n < (int)sizeof(buf) - 1) {
      buf[n++] = ch;
    }
  }
}

// ================= BLE =================
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override    { bleConnected = true; }
  void onDisconnect(BLEServer*) override {
    bleConnected = false;
    allOff();                              // 앱이 끊기면 빛을 끔
    BLEDevice::startAdvertising();         // 끊기면 다시 찾을 수 있게
  }
};

class CmdCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue();
    if (v.length() == 0 || bleHasCmd) return;
    strncpy(bleCmd, v.c_str(), sizeof(bleCmd) - 1);
    bleCmd[sizeof(bleCmd) - 1] = '\0';
    bleHasCmd = true;
  }
};

void setupBle() {
  BLEDevice::init(DEVICE_NAME);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService* service = server->createService(SERVICE_UUID);
  BLECharacteristic* cmd = service->createCharacteristic(
      CMD_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  cmd->setCallbacks(new CmdCallbacks());
  evtChr = service->createCharacteristic(EVT_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  evtChr->addDescriptor(new BLE2902());
  service->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("[BLE] '" DEVICE_NAME "' 이름으로 연결 대기 중");
}

// ================= 시작 =================
void startupCheck() {
  // 켜질 때 빨강-초록-파랑을 한 번씩 보여줘서 LED와 배선을 확인
  uint32_t colors[] = { led.Color(60, 0, 0), led.Color(0, 60, 0), led.Color(0, 0, 60) };
  for (uint32_t c : colors) {
    led.setPixelColor(LEFT, c); led.setPixelColor(RIGHT, c); led.show(); delay(250);
  }
  led.clear(); led.show();
}

void setup() {
  Serial.begin(115200);
  led.begin();
  led.clear();
  led.show();
  delay(1500);
  Serial.println("SeeHear 클립 v1.3 시작");
  startupCheck();
  setupMic();
  setupBle();
  printList();
}

void loop() {
  readSerial();
  if (bleHasCmd) {
    char tmp[48];
    strncpy(tmp, bleCmd, sizeof(tmp));
    bleHasCmd = false;
    handleCommand(tmp);
  }
  updateMic();
  updateLed();
}
