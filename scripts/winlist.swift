import CoreGraphics
import Foundation

// Print: <windowNumber> <x> <y> <w> <h> <title>  for every on-screen window
// owned by the app named in argv[1]. Used to capture ONE window rather than
// the whole desktop, so a comparison shot cannot pick up whatever else is open.
let target = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "Puffin"
guard let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID) as? [[String: Any]] else {
    exit(1)
}
for w in list {
    guard let owner = w[kCGWindowOwnerName as String] as? String, owner == target else { continue }
    guard let b = w[kCGWindowBounds as String] as? [String: Any] else { continue }
    let num = w[kCGWindowNumber as String] as? Int ?? 0
    let x = Int(b["X"] as? Double ?? 0), y = Int(b["Y"] as? Double ?? 0)
    let ww = Int(b["Width"] as? Double ?? 0), hh = Int(b["Height"] as? Double ?? 0)
    let name = w[kCGWindowName as String] as? String ?? ""
    print("\(num) \(x) \(y) \(ww) \(hh) \(name)")
}
