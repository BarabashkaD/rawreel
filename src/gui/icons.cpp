// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/icons.h"

#include <array>
#include <format>
#include <string>
#include <string_view>

#include "core/svg_colour.h"
#include "gui/icons/lucide_icons.h"

namespace rawreel {

namespace {

// In Icon order.
constexpr std::array<std::string_view, ICON_COUNT> SVGS = lucide::ICONS;

}  // namespace

wxBitmapBundle iconBundle(Icon icon, const wxColour& colour) {
    // "#rrggbb": nanosvg reads no alpha in a colour.
    const std::string css =
        std::format("#{:02x}{:02x}{:02x}", colour.Red(), colour.Green(), colour.Blue());
    std::string svg = replaceCurrentColor(SVGS[static_cast<std::size_t>(icon)], css);
    // FromSVG(char*) parses the text in place; `svg` is ours and NUL-terminated.
    return wxBitmapBundle::FromSVG(svg.data(), wxSize(ICON_DIP, ICON_DIP));
}

}  // namespace rawreel
