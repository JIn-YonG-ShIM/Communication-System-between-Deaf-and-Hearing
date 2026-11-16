import UIKit

/// 여기만 바꾸면 돼요.
enum AppConfig {
    static let base = "https://jin-yong-shim.github.io/Communication-System-between-Deaf-and-Hearing/"

    /// 기기에 따라 여는 화면이 달라요.
    ///  - 아이폰  → BAR 앱, 휴대폰 화면만 꽉 차게
    ///  - 아이패드 → CS 앱, 휴대폰 화면 + 조작판을 나란히 (한 화면)
    /// 역할을 서로 바꾸고 싶으면 아래 두 파일 이름(seehear-cs / seehear-bar)을 맞바꾸면 돼요.
    static var url: URL {
        let isPad = UIDevice.current.userInterfaceIdiom == .pad
        return URL(string: base + (isPad ? "seehear-cs.html#both" : "seehear-bar.html#phone"))!
    }

    /// 블루투스 기기 고르기 창에 보일 이름 앞부분 (앱이 namePrefix 로 요청하면 그 값을 우선 씀)
    static let defaultNamePrefix = "SeeHear"
}
