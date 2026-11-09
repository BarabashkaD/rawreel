#pragma once

// SVG text recoloured before wx renders it (video control panel spec §7): Lucide's icons draw in
// `currentColor`, which wx's SVG renderer (nanosvg) does not resolve. wx-free.

#include <string>
#include <string_view>

namespace rawreel {

// `svg` with every `currentColor` replaced by `colour` (for example "#1d1d1f").
[[nodiscard]] std::string replaceCurrentColor(std::string_view svg, std::string_view colour);

}  // namespace rawreel
