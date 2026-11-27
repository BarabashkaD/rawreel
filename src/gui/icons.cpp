// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/icons.h"

#include <array>
#include <cmath>
#include <format>
#include <string>
#include <string_view>

#include <wx/image.h>
#include <wx/settings.h>

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

wxCursor pipetteCursor(const wxWindow& window) {
    int size = wxSystemSettings::GetMetric(wxSYS_CURSOR_X, &window);
    if (size <= 0) {
        size = 32;  // wxOSX reports -1
    }
    const wxSize pixels(size, size);
    const auto render = [&](const wxColour& colour) {
        wxImage image = iconBundle(Icon::Pipette, colour).GetBitmap(pixels).ConvertToImage();
        if (!image.HasAlpha()) {
            image.InitAlpha();
        }
        return image;
    };
    const wxImage black = render(*wxBLACK);
    const wxImage white = render(*wxWHITE);
    wxImage out(pixels);
    out.InitAlpha();
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            out.SetRGB(x, y, 0, 0, 0);
            out.SetAlpha(x, y, 0);
        }
    }
    // Source-over of one layer onto `out` at the pixel (x, y) from (sx, sy) of `layer`.
    const auto over = [&](const wxImage& layer, int x, int y, int sx, int sy) {
        if (sx < 0 || sy < 0 || sx >= size || sy >= size) {
            return;
        }
        const double a = layer.GetAlpha(sx, sy) / 255.0;
        const double b = out.GetAlpha(x, y) / 255.0;
        const double alpha = a + b * (1 - a);
        if (alpha <= 0) {
            return;
        }
        const auto mix = [&](unsigned char top, unsigned char under) {
            return static_cast<unsigned char>(std::lround((top * a + under * b * (1 - a)) / alpha));
        };
        out.SetRGB(x, y, mix(layer.GetRed(sx, sy), out.GetRed(x, y)),
                   mix(layer.GetGreen(sx, sy), out.GetGreen(x, y)),
                   mix(layer.GetBlue(sx, sy), out.GetBlue(x, y)));
        out.SetAlpha(x, y, static_cast<unsigned char>(std::lround(alpha * 255)));
    };
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    over(white, x, y, x + dx, y + dy);  // the outline: the white one pixel around
                }
            }
        }
    }
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            over(black, x, y, x, y);
        }
    }
    out.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_X, 2);
    out.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_Y, size - 3);
    return wxCursor(out);
}

}  // namespace rawreel
