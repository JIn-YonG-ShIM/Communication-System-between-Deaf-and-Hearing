import SwiftUI

@main
struct SeeHearApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
                .ignoresSafeArea()          // 상태바 뒤까지 앱 화면이 채움
                .statusBarHidden(false)
        }
    }
}
