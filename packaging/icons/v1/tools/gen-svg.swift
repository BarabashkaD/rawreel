// Writes the rawreel icon SVG masters (svg/) from the art inputs (art/).
// Usage: swift gen-svg.swift <icons dir>   (the folder that holds art/ and svg/)
//
// The owl is a full-colour RGB picture. Every fill is converted with BT.709 (non-linear R'G'B' in
// [0,1] -> Y in [0,1], U and V in [-0.5,0.5]) and rebuilt as RGB for each plane:
//   Y plane: (Y, 0, 0)                     greyscale luma
//   U plane: (0.5, CHROMA_GAIN * U, 0)     mid-grey neutral: +U blue, -U yellow
//   V plane: (0.5, 0, CHROMA_GAIN * V)     mid-grey neutral: +V red,  -V cyan
// U and V are clipped to +-0.5 after the gain. The planes sit in the three panels of a player.
//
// Outputs: the plated icon and mark (macOS), the unplated icon, its mid size without keys and its
// small size (Windows and Linux), the plated small size (macOS) and the four 320 x 100 About
// lockups (plated tile for macOS, unplated tile for Windows and Linux; light and dark).
// After a change here or in art/, run render-icons.swift and commit everything it writes.
import Foundation

// Chroma gain of every variant (1 = exact BT.709 chroma; the approved family uses 2).
let CHROMA_GAIN = 2.0

let root = URL(fileURLWithPath: CommandLine.arguments[1])
let artDir = root.appendingPathComponent("art")
let out = root.appendingPathComponent("svg")
try! FileManager.default.createDirectory(at: out, withIntermediateDirectories: true)

struct RGB { var r, g, b: Double }
func hex(_ s: String) -> RGB {
    let v = Int(s.dropFirst(), radix: 16)!
    return RGB(r: Double((v >> 16) & 255) / 255, g: Double((v >> 8) & 255) / 255, b: Double(v & 255) / 255)
}
func hexStr(_ c: RGB) -> String {
    func b(_ x: Double) -> Int { max(0, min(255, Int((x * 255).rounded()))) }
    return String(format: "#%02X%02X%02X", b(c.r), b(c.g), b(c.b))
}
// BT.709
let kr = 0.2126, kb = 0.0722, kg = 1 - kr - kb
func toYUV(_ c: RGB) -> (y: Double, u: Double, v: Double) {
    let y = kr * c.r + kg * c.g + kb * c.b
    return (y, (c.b - y) / (2 * (1 - kb)), (c.r - y) / (2 * (1 - kr)))
}
func toRGB(_ y: Double, _ u: Double, _ v: Double) -> RGB {
    let r = y + 2 * (1 - kr) * v
    let b = y + 2 * (1 - kb) * u
    let g = (y - kr * r - kb * b) / kg
    func c(_ x: Double) -> Double { min(1, max(0, x)) }
    return RGB(r: c(r), g: c(g), b: c(b))
}
func clipC(_ x: Double) -> Double { max(-0.5, min(0.5, x)) }
enum Plane: String { case y, u, v }
func convert(_ c: RGB, _ plane: Plane) -> RGB {
    let p = toYUV(c)
    switch plane {
    case .y: return toRGB(p.y, 0, 0)
    case .u: return toRGB(0.5, clipC(CHROMA_GAIN * p.u), 0)
    case .v: return toRGB(0.5, 0, clipC(CHROMA_GAIN * p.v))
    }
}

func read(_ name: String) -> String { try! String(contentsOf: artDir.appendingPathComponent(name), encoding: .utf8) }
struct Ink { let key: String; let ch: Character; let colour: RGB }
let inks: [Ink] = read("owl.palette").split(separator: "\n").compactMap { line in
    let f = line.split(separator: " ", omittingEmptySubsequences: true).map(String.init)
    guard f.count >= 3, !f[0].hasPrefix("#") else { return nil }
    return Ink(key: f[0], ch: Character(f[1]), colour: hex(f[2]))
}
// {key} -> the ink converted for the plane; {v} -> the plane name (keeps clip ids unique).
func fill(_ art: String, _ plane: Plane) -> String {
    var s = art
    for i in inks { s = s.replacingOccurrences(of: "{\(i.key)}", with: hexStr(convert(i.colour, plane))) }
    return s.replacingOccurrences(of: "{v}", with: plane.rawValue)
}
let art = read("owl.svgfrag")
let midArt = read("owl.mid.svgfrag")
func fillArt(_ plane: Plane) -> String { fill(art, plane) }
func fillMid(_ plane: Plane) -> String { fill(midArt, plane) }
func write(_ s: String, _ name: String) { try! s.write(to: out.appendingPathComponent(name), atomically: true, encoding: .utf8) }

// ---- full-size parts (1024 grid; plate on the macOS rounded-square grid) ----
let plateDefs = """
    <linearGradient id="plate" x1="512" y1="100" x2="512" y2="924" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="#2A3248"/>
      <stop offset="1" stop-color="#0F1219"/>
    </linearGradient>
    <linearGradient id="rim" x1="512" y1="100" x2="512" y2="924" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="#FFFFFF" stop-opacity="0.22"/>
      <stop offset="1" stop-color="#FFFFFF" stop-opacity="0.06"/>
    </linearGradient>
    <linearGradient id="key" x1="0" y1="722" x2="0" y2="842" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="#36405A"/>
      <stop offset="1" stop-color="#262D3F"/>
    </linearGradient>
"""
let plate = """
  <rect x="100" y="100" width="824" height="824" rx="185.4" fill="url(#plate)"/>
  <rect x="103" y="103" width="818" height="818" rx="182.4" fill="none" stroke="url(#rim)" stroke-width="6"/>
"""
let Y = (x: 212.0, y: 172.0, w: 600.0, h: 332.0, r: 24.0)
let U = (x: 212.0, y: 516.0, w: 294.0, h: 166.0, r: 18.0)
let V = (x: 518.0, y: 516.0, w: 294.0, h: 166.0, r: 18.0)
let clips = """
    <clipPath id="clipY"><rect x="\(Y.x)" y="\(Y.y)" width="\(Y.w)" height="\(Y.h)" rx="\(Y.r)"/></clipPath>
    <clipPath id="clipU"><rect x="\(U.x)" y="\(U.y)" width="\(U.w)" height="\(U.h)" rx="\(U.r)"/></clipPath>
    <clipPath id="clipV"><rect x="\(V.x)" y="\(V.y)" width="\(V.w)" height="\(V.h)" rx="\(V.r)"/></clipPath>
"""
// Two player-control keys: play and a wrench (tools / settings).
func buttons(keyFill: String, glyph: String, rim: String) -> String {
    """
      <g>
        <rect x="212" y="722" width="294" height="120" rx="36" fill="\(keyFill)"/>
        <rect x="518" y="722" width="294" height="120" rx="36" fill="\(keyFill)"/>
        <rect x="213.5" y="723.5" width="291" height="117" rx="34.5" fill="none" stroke="\(rim)" stroke-width="3"/>
        <rect x="519.5" y="723.5" width="291" height="117" rx="34.5" fill="none" stroke="\(rim)" stroke-width="3"/>
        <path d="M341 750 L389 782 L341 814 Z" fill="\(glyph)" stroke="\(glyph)" stroke-width="14" stroke-linejoin="round"/>
        <g fill="none" stroke="\(glyph)">
          <path d="M697.9 768.5 A18 18 0 1 1 678.4 749.1" stroke-width="15"/>
          <path d="M670 777 L642 805" stroke-width="17" stroke-linecap="round"/>
        </g>
      </g>
    """
}
let wordmark = """
    <g fill="none" stroke="WMCOLOR" stroke-width="12" stroke-linecap="round" stroke-linejoin="round">
      <path d="M6 46V94M6 70C6 54 16 46 32 46"/>
      <circle cx="78" cy="70" r="24"/><path d="M102 46V94"/>
      <path d="M128 46L141 94L154 58L167 94L180 46"/>
      <path d="M206 46V94M206 70C206 54 216 46 232 46"/>
      <path d="M256 70H304A24 24 0 1 0 298.4 85.4"/>
      <path d="M328 70H376A24 24 0 1 0 370.4 85.4"/>
      <path d="M400 14V94"/>
    </g>
"""
// The three planes: U and V are the same vector picture at exactly half width and half height
// (centred crop of 3 px per side).
let planes = """
  <g clip-path="url(#clipY)"><g transform="translate(\(Int(Y.x)) \(Int(Y.y)))">
\(fillArt(.y))  </g></g>
  <g clip-path="url(#clipU)"><g transform="translate(\(U.x - 3) \(U.y)) scale(0.5)">
\(fillArt(.u))  </g></g>
  <g clip-path="url(#clipV)"><g transform="translate(\(V.x - 3) \(V.y)) scale(0.5)">
\(fillArt(.v))  </g></g>
"""
func motif(unplated: Bool) -> String {
    """
    \(planes)
    \(unplated ? buttons(keyFill: "#1A2030", glyph: "#EEF1F6", rim: "#FFFFFF33") : buttons(keyFill: "url(#key)", glyph: "#E9EDF3", rim: "#FFFFFF14"))
    """
}
let defs = "  <defs>\n\(plateDefs)\n\(clips)  </defs>\n"
let plated = plate + motif(unplated: false)

write("""
<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
  <title>rawreel app icon, 1024 master on the macOS rounded-square grid</title>
\(defs)\(plated)</svg>

""", "icon-1024.svg")
write("""
<svg xmlns="http://www.w3.org/2000/svg" width="824" height="824" viewBox="100 100 824 824">
  <title>rawreel mark</title>
\(defs)\(plated)</svg>

""", "mark.svg")
// Unplated: no plate; each panel and key on a dark keyline, scaled up to fill the 1024 grid.
let keyline = """
    <g fill="#161C29" stroke="#161C29" stroke-width="16" stroke-linejoin="round">
      <rect x="\(Y.x)" y="\(Y.y)" width="\(Y.w)" height="\(Y.h)" rx="\(Y.r)"/>
      <rect x="\(U.x)" y="\(U.y)" width="\(U.w)" height="\(U.h)" rx="\(U.r)"/>
      <rect x="\(V.x)" y="\(V.y)" width="\(V.w)" height="\(V.h)" rx="\(V.r)"/>
      <rect x="212" y="722" width="294" height="120" rx="36"/>
      <rect x="518" y="722" width="294" height="120" rx="36"/>
    </g>
"""
write("""
<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
  <title>rawreel app icon, unplated variant for Windows and Linux</title>
\(defs)  <g transform="translate(512 512) scale(1.33) translate(-512 -507)">
\(keyline)
\(motif(unplated: true))  </g>
</svg>

""", "icon-unplated-1024.svg")

// ---- small (16 to 32 px): hand-made 12 x 7 RGB pixel art on the 16 px grid ----
// Y is converted per pixel; U and V are 4:2:0-style area averages of U and V over 5 x 4 blocks,
// multiplied by CHROMA_GAIN and clipped.
let byCh = Dictionary(uniqueKeysWithValues: inks.map { ($0.ch, $0) })
let map = read("owl.small16.txt").split(separator: "\n").filter { !$0.hasPrefix("#") }.map { Array($0) }
let mc = map[0].count, mr = map.count
func yuvAt(_ i: Int, _ j: Int) -> (y: Double, u: Double, v: Double) {
    guard let ink = byCh[map[j][i]] else { fatalError("unknown char \(map[j][i])") }
    return toYUV(ink.colour)
}
var yRects = ""
for j in 0..<mr {
    var i = 0
    while i < mc {
        var k = i; while k < mc && map[j][k] == map[j][i] { k += 1 }
        yRects += "      <rect x=\"\(i * 2)\" y=\"\(j * 2)\" width=\"\((k - i) * 2)\" height=\"2.02\" fill=\"\(hexStr(toRGB(yuvAt(i, j).y, 0, 0)))\"/>\n"
        i = k
    }
}
let cc = 5, cr = 4
var uRects = "", vRects = ""
for bj in 0..<cr { for bi in 0..<cc {
    // area of the Y map covered by this chroma block
    let x0 = Double(bi) * Double(mc) / Double(cc), x1 = Double(bi + 1) * Double(mc) / Double(cc)
    let y0 = Double(bj) * Double(mr) / Double(cr), y1 = Double(bj + 1) * Double(mr) / Double(cr)
    var su = 0.0, sv = 0.0, sw = 0.0
    for j in 0..<mr { for i in 0..<mc {
        let w = max(0, min(x1, Double(i + 1)) - max(x0, Double(i))) * max(0, min(y1, Double(j + 1)) - max(y0, Double(j)))
        if w > 0 { let p = yuvAt(i, j); su += p.u * w; sv += p.v * w; sw += w }
    } }
    let geo = "x=\"\(bi * 2)\" y=\"\(bj * 2)\" width=\"2.05\" height=\"2.05\""
    uRects += "      <rect \(geo) fill=\"\(hexStr(toRGB(0.5, clipC(CHROMA_GAIN * su / sw), 0)))\"/>\n"
    vRects += "      <rect \(geo) fill=\"\(hexStr(toRGB(0.5, 0, clipC(CHROMA_GAIN * sv / sw))))\"/>\n"
} }
let smallPlanes = """
  <g clip-path="url(#sY)"><g transform="translate(4 4)">
\(yRects)  </g></g>
  <g clip-path="url(#sU)"><g transform="translate(4 20)">
\(uRects)  </g></g>
  <g clip-path="url(#sV)"><g transform="translate(18 20)">
\(vRects)  </g></g>
"""
let smallClips = """
  <defs>
    <clipPath id="sY"><rect x="4" y="4" width="24" height="14" rx="1"/></clipPath>
    <clipPath id="sU"><rect x="4" y="20" width="10" height="8" rx="1"/></clipPath>
    <clipPath id="sV"><rect x="18" y="20" width="10" height="8" rx="1"/></clipPath>
  </defs>
"""
write("""
<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">
  <title>rawreel app icon, small plated version for macOS 16 to 32 px, drawn on the 16 px grid</title>
\(smallClips)  <rect x="1" y="1" width="30" height="30" rx="7" fill="#1A2030"/>
  <rect x="1.5" y="1.5" width="29" height="29" rx="6.5" fill="none" stroke="#FFFFFF" stroke-opacity="0.18"/>
\(smallPlanes)</svg>

""", "icon-small-32.svg")
// Unplated small for Windows and Linux: transparent background, each plane on a 1-unit dark keyline
// (separates the light Y plane from light taskbars; the plane colours carry it on dark taskbars).
write("""
<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">
  <title>rawreel app icon, small unplated version for Windows and Linux 16 to 32 px, drawn on the 16 px grid</title>
\(smallClips)  <g fill="#161C29">
    <rect x="3" y="3" width="26" height="16" rx="2"/>
    <rect x="3" y="19" width="12" height="10" rx="2"/>
    <rect x="17" y="19" width="12" height="10" rx="2"/>
  </g>
\(smallPlanes)</svg>

""", "icon-small-unplated-32.svg")

// ---- mid size (Windows and Linux 40 to 64 px): unplated, no keys, simplified owl, 64 px grid ----
// Y 56 x 31 at (4, 8); U and V 27 x 15 at (4, 41) and (33, 41); 1 px dark keyline around each plane.
write("""
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
  <title>rawreel app icon, unplated mid-size version for Windows and Linux 40 to 64 px, no keys</title>
  <defs>
    <clipPath id="mY"><rect x="4" y="8" width="56" height="31" rx="2"/></clipPath>
    <clipPath id="mU"><rect x="4" y="41" width="27" height="15" rx="2"/></clipPath>
    <clipPath id="mV"><rect x="33" y="41" width="27" height="15" rx="2"/></clipPath>
  </defs>
  <g fill="#161C29">
    <rect x="3" y="7" width="58" height="33" rx="3"/>
    <rect x="3" y="40" width="58" height="17" rx="3"/>
  </g>
  <g clip-path="url(#mY)"><g transform="translate(4 8) scale(0.093333)">
\(fillMid(.y))  </g></g>
  <g clip-path="url(#mU)"><g transform="translate(4 41) scale(0.045)">
\(fillMid(.u))  </g></g>
  <g clip-path="url(#mV)"><g transform="translate(33 41) scale(0.045)">
\(fillMid(.v))  </g></g>
</svg>

""", "icon-mid-unplated-64.svg")

// ---- About lockups: 320 x 100 banner; the tile is the three planes (no keys), 92 px tall ----
// plated tile: dark plate 688 x 598 around the planes (content 212..812 x 172..682)
let platedTile = """
  <g transform="translate(4 4) scale(0.153846) translate(-168 -128)">
  <rect x="168" y="128" width="688" height="598" rx="132" fill="url(#plate)"/>
  <rect x="171" y="131" width="682" height="592" rx="129" fill="none" stroke="url(#rim)" stroke-width="6"/>
\(planes)  </g>
"""
let unplatedTile = """
  <g transform="translate(4 4) scale(0.174905) translate(-204 -164)">
  <g fill="#161C29" stroke="#161C29" stroke-width="16" stroke-linejoin="round">
    <rect x="\(Y.x)" y="\(Y.y)" width="\(Y.w)" height="\(Y.h)" rx="\(Y.r)"/>
    <rect x="\(U.x)" y="\(U.y)" width="\(U.w)" height="\(U.h)" rx="\(U.r)"/>
    <rect x="\(V.x)" y="\(V.y)" width="\(V.w)" height="\(V.h)" rx="\(V.r)"/>
  </g>
\(planes)  </g>
"""
for (os, tile, osName) in [("macos", platedTile, "macOS, plated tile"), ("unplated", unplatedTile, "Windows and Linux, unplated tile")] {
    for theme in ["light", "dark"] {
        // light backgrounds: dark wordmark; dark backgrounds: light wordmark
        let col = theme == "light" ? "#141821" : "#EEF1F6"
        write("""
        <svg xmlns="http://www.w3.org/2000/svg" width="320" height="100" viewBox="0 0 320 100">
          <title>rawreel logo, About banner, \(osName), for \(theme) backgrounds</title>
        \(defs)\(tile)  <g transform="translate(126 17.8) scale(0.46)">
        \(wordmark.replacingOccurrences(of: "WMCOLOR", with: col))  </g>
        </svg>

        """, "logo-lockup-\(os)-\(theme).svg")
    }
}
