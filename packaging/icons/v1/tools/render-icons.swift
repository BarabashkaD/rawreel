// Renders the committed rawreel icon files from the SVG masters (svg/), on macOS with AppKit:
//   rawreel.icns          macOS bundle icon (iconutil), plated; 16 and 32 px from the small icon
//   rawreel.ico           Windows icon: 16-32 small unplated, 40-64 mid, 96 and 256 unplated
//   png/linux/*.png       Linux window icons: 16-32 small unplated, 48-64 mid, 128-256 unplated
//   png/logo/*.png        About lockups: macOS at 1x and 2x, Windows and Linux at 1x, 2x and 3x
//   sources.sha256        SHA-256 of art/*, svg/* and both scripts (checked by the rawreel.icons test)
// Usage: swift render-icons.swift <icons dir> [<sheet.png>]
// With <sheet.png> (a path outside the repository) it also writes a review contact sheet: every
// variant at its use sizes on light and dark backgrounds, and the ICO and icns images read back.
import AppKit
import CryptoKit
import ImageIO

let args = CommandLine.arguments
guard args.count == 2 || args.count == 3 else {
    FileHandle.standardError.write("usage: swift render-icons.swift <icons dir> [<sheet.png>]\n".data(using: .utf8)!)
    exit(2)
}
let root = URL(fileURLWithPath: args[1])
let svgDir = root.appendingPathComponent("svg")
let fm = FileManager.default

// ---- rasterising ----
func bitmap(_ w: Int, _ h: Int) -> NSBitmapImageRep {
    NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: w, pixelsHigh: h, bitsPerSample: 8, samplesPerPixel: 4,
                     hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!.retagging(with: .sRGB)!
}
func draw(into rep: NSBitmapImageRep, _ body: () -> Void) {
    NSGraphicsContext.saveGraphicsState()
    let ctx = NSGraphicsContext(bitmapImageRep: rep)!
    NSGraphicsContext.current = ctx
    ctx.imageInterpolation = .high
    body()
    NSGraphicsContext.restoreGraphicsState()
}
var cache: [String: NSBitmapImageRep] = [:]
func raster(_ svg: String, _ w: Int, _ h: Int) -> NSBitmapImageRep {
    let key = "\(svg)@\(w)x\(h)"
    if let rep = cache[key] { return rep }
    let url = svgDir.appendingPathComponent(svg)
    guard let img = NSImage(contentsOf: url) else { fatalError("cannot load svg/\(svg)") }
    let rep = bitmap(w, h)
    draw(into: rep) { img.draw(in: NSRect(x: 0, y: 0, width: w, height: h)) }
    cache[key] = rep
    return rep
}
func png(_ rep: NSBitmapImageRep) -> Data { rep.representation(using: .png, properties: [:])! }
func save(_ data: Data, _ url: URL) {
    try! fm.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    try! data.write(to: url)
}

// ---- which master draws which size ----
let smallPlated = "icon-small-32.svg", plated = "icon-1024.svg"
let smallUnplated = "icon-small-unplated-32.svg", mid = "icon-mid-unplated-64.svg", unplated = "icon-unplated-1024.svg"
func unplatedSource(_ s: Int) -> String { s <= 32 ? smallUnplated : (s <= 64 ? mid : unplated) }
func macSource(_ s: Int) -> String { s <= 32 ? smallPlated : plated }

// ---- Linux window icons ----
let linuxSizes = [16, 24, 32, 48, 64, 128, 256]
for s in linuxSizes {
    save(png(raster(unplatedSource(s), s, s)), root.appendingPathComponent("png/linux/rawreel-\(s).png"))
}

// ---- About lockups (light: dark wordmark for light backgrounds; dark: the opposite) ----
let lockups: [(os: String, scales: [Int])] = [("macos", [1, 2]), ("unplated", [1, 2, 3])]
for (os, scales) in lockups {
    for theme in ["light", "dark"] {
        for k in scales {
            let w = 320 * k, h = 100 * k
            save(png(raster("logo-lockup-\(os)-\(theme).svg", w, h)),
                 root.appendingPathComponent("png/logo/logo-\(os)-\(theme)-\(w)x\(h).png"))
        }
    }
}

// ---- macOS icns through iconutil ----
let iconsetEntries: [(name: String, size: Int)] = [
    ("icon_16x16", 16), ("icon_16x16@2x", 32), ("icon_32x32", 32), ("icon_32x32@2x", 64),
    ("icon_128x128", 128), ("icon_128x128@2x", 256), ("icon_256x256", 256), ("icon_256x256@2x", 512),
    ("icon_512x512", 512), ("icon_512x512@2x", 1024)]
do {
    let iconset = fm.temporaryDirectory.appendingPathComponent("rawreel-\(UUID().uuidString).iconset")
    try! fm.createDirectory(at: iconset, withIntermediateDirectories: true)
    defer { try? fm.removeItem(at: iconset) }
    for (name, s) in iconsetEntries {
        save(png(raster(macSource(s), s, s)), iconset.appendingPathComponent("\(name).png"))
    }
    let tool = Process()
    tool.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
    tool.arguments = ["-c", "icns", "-o", root.appendingPathComponent("rawreel.icns").path, iconset.path]
    try! tool.run()
    tool.waitUntilExit()
    guard tool.terminationStatus == 0 else { fatalError("iconutil failed with \(tool.terminationStatus)") }
}

// ---- Windows ICO: 256 as PNG, the others as 32-bit BGRA DIBs with an AND mask ----
let icoSizes = [16, 20, 24, 32, 40, 48, 64, 96, 256]
func le16(_ v: Int) -> [UInt8] { [UInt8(v & 255), UInt8((v >> 8) & 255)] }
func le32(_ v: Int) -> [UInt8] { le16(v & 0xFFFF) + le16((v >> 16) & 0xFFFF) }
// Straight (un-premultiplied) RGBA rows, top row first.
func straightRGBA(_ rep: NSBitmapImageRep) -> [[UInt8]] {
    precondition(rep.bitsPerPixel == 32 && !rep.isPlanar && !rep.bitmapFormat.contains(.alphaFirst)
                 && !rep.bitmapFormat.contains(.alphaNonpremultiplied), "unexpected bitmap layout")
    let w = rep.pixelsWide, h = rep.pixelsHigh, stride = rep.bytesPerRow, data = rep.bitmapData!
    var rows: [[UInt8]] = []
    for y in 0..<h {
        var row = [UInt8](repeating: 0, count: 4 * w)
        for x in 0..<w {
            let p = data + y * stride + 4 * x
            let a = Int(p[3])
            if a > 0 {
                for c in 0..<3 { row[4 * x + c] = UInt8(min(255, (Int(p[c]) * 255 + a / 2) / a)) }
            }
            row[4 * x + 3] = UInt8(a)
        }
        rows.append(row)
    }
    return rows
}
func dib(_ rep: NSBitmapImageRep) -> [UInt8] {
    let w = rep.pixelsWide, h = rep.pixelsHigh
    let rows = straightRGBA(rep)
    let maskStride = (w + 31) / 32 * 4
    var xor: [UInt8] = [], and: [UInt8] = []
    for y in (0..<h).reversed() { // bottom-up
        let row = rows[y]
        var mask = [UInt8](repeating: 0, count: maskStride)
        for x in 0..<w {
            xor += [row[4 * x + 2], row[4 * x + 1], row[4 * x], row[4 * x + 3]] // BGRA
            if row[4 * x + 3] == 0 { mask[x / 8] |= UInt8(0x80 >> (x % 8)) }
        }
        and += mask
    }
    // BITMAPINFOHEADER: size, width, height (XOR + AND), planes, bit count, BI_RGB, image size,
    // resolution x and y, colours used, colours important.
    let header = le32(40) + le32(w) + le32(2 * h) + le16(1) + le16(32) + le32(0) + le32(xor.count + and.count)
        + le32(0) + le32(0) + le32(0) + le32(0)
    return header + xor + and
}
do {
    let images: [[UInt8]] = icoSizes.map { s in
        let rep = raster(unplatedSource(s), s, s)
        return s == 256 ? [UInt8](png(rep)) : dib(rep)
    }
    var ico = le16(0) + le16(1) + le16(icoSizes.count)
    var offset = 6 + 16 * icoSizes.count
    for (s, image) in zip(icoSizes, images) {
        let b = UInt8(s == 256 ? 0 : s)
        ico += [b, b, 0, 0] + le16(1) + le16(32) + le32(image.count) + le32(offset)
        offset += image.count
    }
    for image in images { ico += image }
    save(Data(ico), root.appendingPathComponent("rawreel.ico"))
}

// ---- sources.sha256: art/*, svg/* and both scripts, sorted by path ----
func files(_ dir: String) -> [String] {
    try! fm.contentsOfDirectory(atPath: root.appendingPathComponent(dir).path).filter { !$0.hasPrefix(".") }.map { "\(dir)/\($0)" }
}
do {
    let paths = (files("art") + files("svg") + ["tools/gen-svg.swift", "tools/render-icons.swift"]).sorted()
    var text = ""
    for path in paths {
        let digest = SHA256.hash(data: try! Data(contentsOf: root.appendingPathComponent(path)))
        text += digest.map { String(format: "%02x", $0) }.joined() + "  " + path + "\n"
    }
    save(text.data(using: .utf8)!, root.appendingPathComponent("sources.sha256"))
}

// ---- review contact sheet (optional) ----
guard args.count == 3 else { print("done"); exit(0) }
let sheetURL = URL(fileURLWithPath: args[2])

// Every image of a written icon file, decoded by ImageIO.
func readBack(_ name: String) -> [NSBitmapImageRep] {
    guard let src = CGImageSourceCreateWithURL(root.appendingPathComponent(name) as CFURL, nil) else { fatalError("cannot read \(name)") }
    return (0..<CGImageSourceGetCount(src)).map { NSBitmapImageRep(cgImage: CGImageSourceCreateImageAtIndex(src, $0, nil)!) }
}
struct Item { let rep: NSBitmapImageRep; let scale: Int; let label: String }
func item(_ svg: String, _ w: Int, _ h: Int, scale: Int = 1) -> Item {
    Item(rep: raster(svg, w, h), scale: scale, label: scale == 1 ? (w == h ? "\(w)" : "\(w)×\(h)") : "\(w) ×\(scale)")
}
func sizes(_ svg: String, _ list: [Int]) -> [Item] { list.map { item(svg, $0, $0) } }
let sections: [(title: String, items: [Item])] = [
    ("Plated icon, macOS (icon-1024.svg)", sizes(plated, [1024, 512, 256, 128, 64, 32, 16])),
    ("Plated small icon, macOS (icon-small-32.svg)", sizes(smallPlated, [32, 16]) + [item(smallPlated, 16, 16, scale: 8)]),
    ("Unplated icon, Windows and Linux (icon-unplated-1024.svg)", sizes(unplated, [256, 128, 64, 48, 40])),
    ("Unplated small icon, Windows and Linux (icon-small-unplated-32.svg)",
     sizes(smallUnplated, [32, 24, 20, 16]) + [item(smallUnplated, 16, 16, scale: 8)]),
    ("Unplated mid icon without keys, Windows and Linux (icon-mid-unplated-64.svg)", sizes(mid, [64, 48, 40])),
] + lockups.flatMap { (os, scales) in
    ["light", "dark"].map { theme in
        ("About lockup logo-lockup-\(os)-\(theme).svg", scales.map { item("logo-lockup-\(os)-\(theme).svg", 320 * $0, 100 * $0) })
    }
} + [
    ("rawreel.ico, read back (\(readBack("rawreel.ico").count) images)",
     readBack("rawreel.ico").map { Item(rep: $0, scale: 1, label: "\($0.pixelsWide)") }),
    ("rawreel.icns, read back (\(readBack("rawreel.icns").count) images)",
     readBack("rawreel.icns").map { Item(rep: $0, scale: 1, label: "\($0.pixelsWide)") }),
]
let pad = 40, gap = 30, titleH = 56, labelH = 34
func itemW(_ i: Item) -> Int { i.rep.pixelsWide * i.scale }
func itemH(_ i: Item) -> Int { i.rep.pixelsHigh * i.scale }
func stripH(_ items: [Item]) -> Int { items.map(itemH).max()! + labelH + 2 * pad / 2 + 10 }
let W = max(1600, sections.map { s in 2 * pad + s.items.map { max(itemW($0), 40) }.reduce(0, +) + gap * (s.items.count - 1) + 2 * pad }.max()!)
let H = 80 + sections.map { titleH + 2 * stripH($0.items) + pad }.reduce(0, +)
let lightBG = NSColor(calibratedRed: 0.95, green: 0.955, blue: 0.965, alpha: 1)
let darkBG = NSColor(calibratedRed: 0.11, green: 0.11, blue: 0.12, alpha: 1)
func text(_ s: String, _ x: Int, _ top: Int, _ color: NSColor, _ size: CGFloat) {
    (s as NSString).draw(at: NSPoint(x: CGFloat(x), y: CGFloat(H - top) - size * 1.25),
                         withAttributes: [.font: NSFont.systemFont(ofSize: size, weight: .medium), .foregroundColor: color])
}
let sheet = bitmap(W, H)
draw(into: sheet) {
    let ctx = NSGraphicsContext.current!
    lightBG.setFill(); NSRect(x: 0, y: 0, width: W, height: H).fill()
    text("rawreel icons · CHROMA_GAIN from gen-svg.swift · every variant on light and dark", pad, 20, .black, 34)
    var top = 80
    for (title, items) in sections {
        text(title, pad, top + 10, .black, 26)
        top += titleH
        for bg in [lightBG, darkBG] {
            let h = stripH(items)
            bg.setFill(); NSRect(x: pad, y: H - top - h, width: W - 2 * pad, height: h).fill()
            let fg: NSColor = bg == lightBG ? .darkGray : .lightGray
            let maxH = items.map(itemH).max()!
            var x = 2 * pad
            for i in items {
                let w = itemW(i), ih = itemH(i)
                let y = top + pad / 2 + (maxH - ih) / 2
                ctx.imageInterpolation = i.scale > 1 ? .none : .high
                i.rep.draw(in: NSRect(x: x, y: H - y - ih, width: w, height: ih), from: .zero, operation: .sourceOver,
                           fraction: 1, respectFlipped: false, hints: nil)
                text(i.label, x, top + pad / 2 + maxH + 6, fg, 18)
                x += max(w, 40) + gap
            }
            top += h
        }
        top += pad
    }
}
save(png(sheet), sheetURL)
print("done; sheet \(W)x\(H)")
