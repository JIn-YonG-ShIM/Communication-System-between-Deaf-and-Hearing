import SwiftUI

@main
struct SeeHearApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
                .ignoresSafeArea()          // 상태바 뒤까지 앱 화면이 채움
                .statusBarHidden(true)               // 시간·요일·배터리 줄 숨김 (전체 화면)
                .persistentSystemOverlays(.hidden)   // 아래 홈 막대도 흐리게
        }
    }
}
