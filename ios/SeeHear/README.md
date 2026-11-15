# SeeHear iOS 앱 — 설치 안내 (Mac + Xcode, 무료 Apple ID 가능)

Bluefy/WebBLE 대신 쓰는 SeeHear 전용 앱이에요. 하는 일:
- GitHub Pages 의 seehear-bar.html(또는 cs)을 **전체화면**으로 엶 (상단 회색 바 없음)
- 웹 블루투스(`navigator.bluetooth`)를 iOS CoreBluetooth 로 연결 → 클립 연결·명령·알림(큰 소리 감지) 모두 동작
- 음성 인식(`webkitSpeechRecognition`)을 iOS 음성 인식으로 연결 → '말로 위험 알리기' 동작
- 두 폰 연결(중계소 웹소켓)은 그대로 동작 (웹뷰가 지원)

## 1. 준비
- Mac 에 **Xcode** (App Store, 무료, 큰 파일)
- 아이폰: 설정 → 개인정보 보호 및 보안 → **개발자 모드** 켜기 (Xcode 연결 후 뜨기도 함)
- Apple ID (무료 계정이면 앱이 7일 뒤 만료 → 다시 실행(Run)하면 됨)

## 2. 프로젝트 만들기 (5분)
1. Xcode → File → New → Project → **iOS → App** → Next
   - Product Name: `SeeHear`  · Interface: **SwiftUI** · Language: **Swift** · 나머지 기본
   - Team: 본인 Apple ID (없으면 Xcode → Settings → Accounts 에서 추가)
2. 만들어진 프로젝트에서 `ContentView.swift` 를 **삭제**(Move to Trash) 하고,
   이 폴더의 파일 5개를 프로젝트 탐색기(왼쪽 트리)의 `SeeHear` 폴더로 **드래그**:
   `SeeHearApp.swift`(기존 것 덮어쓰기) · `ContentView.swift` · `BLEBridge.swift` · `AppConfig.swift` · `webble.js`
   - 드래그 창에서 "Copy items if needed" 체크, Target `SeeHear` 체크
3. 프로젝트 설정(파란 아이콘) → TARGETS SeeHear → **Info** 탭 → `+` 로 `Info-plist-추가항목.txt` 의 3개 권한 설명 추가
4. **Signing & Capabilities** 탭 → Team 선택, Bundle Identifier 를 겹치지 않게 (예: `com.shim.seehear`)

## 3. 아이폰에 설치
1. 아이폰을 케이블로 연결 → Xcode 상단 기기 목록에서 내 아이폰 선택
2. ▶ Run. 처음엔 아이폰에서 "신뢰하지 않는 개발자" 가 뜸 →
   아이폰 설정 → 일반 → VPN 및 기기 관리 → 내 Apple ID → **신뢰**
3. 앱 아이콘이 홈 화면에 생김. 이후엔 케이블 없이 실행.

## 4. CS 용 (아이패드)
`AppConfig.swift` 의 url 을 `seehear-cs.html` 로 바꾸고, 아이패드를 선택해 Run.
(한 프로젝트에서 번갈아 빌드해도 되고, Bundle Identifier 를 `…seehear-cs` 로 바꿔 두 앱으로 둬도 됨)

## 자주 생기는 문제
- "Untrusted Developer": 위 3-2.
- 앱이 7일 뒤 안 열림: 무료 계정 한계. Xcode 로 다시 Run.
- 클립이 목록에 안 뜸: 클립 전원·펌웨어 이름(`SeeHear…`)·다른 앱(Bluefy/nRF)이 연결을 잡고 있지 않은지.
- 음성 인식 권한 팝업이 두 번(마이크·음성 인식) 뜸 → 둘 다 허용.
- 웹 페이지를 고쳐 올린 뒤엔 앱을 완전히 종료했다 다시 열면 새 버전을 받음.
