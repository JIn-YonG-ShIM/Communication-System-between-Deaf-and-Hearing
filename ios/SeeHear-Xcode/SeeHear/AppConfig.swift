import Foundation

/// 여기만 바꾸면 돼요.
enum AppConfig {
    /// 열 앱 주소. BAR 폰이면 seehear-bar.html, CS 기기면 seehear-cs.html
    static let url = URL(string: "https://jin-yong-shim.github.io/Communication-System-between-Deaf-and-Hearing/seehear-bar.html")!
    /// 블루투스 기기 고르기 창에 보일 이름 앞부분 (앱이 namePrefix 로 요청하면 그 값을 우선 씀)
    static let defaultNamePrefix = "SeeHear"
}
