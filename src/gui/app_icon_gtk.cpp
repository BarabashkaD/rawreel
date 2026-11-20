// app_icon.h on Linux (wxGTK): the window icons from the embedded hicolor PNGs.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>

#include "dvv/base/log.h"
#include "gui/app_icon.h"
#include "gui/app_icon_data.h"
#include "gui/app_icon_detail.h"

namespace rawreel {

namespace {

// The Linux window icons' keys: owl-on-film-reel-square-background-<N> (the build tree's copies of
// the hicolor PNGs).
constexpr std::string_view WINDOW_ICON_PREFIX = "owl-on-film-reel-square-background-";

bool isWindowIconKey(std::string_view key) {
    if (!key.starts_with(WINDOW_ICON_PREFIX) || key.size() == WINDOW_ICON_PREFIX.size()) {
        return false;
    }
    return std::ranges::all_of(key.substr(WINDOW_ICON_PREFIX.size()),
                               [](char c) { return c >= '0' && c <= '9'; });
}

}  // namespace

// D5, D7: the embedded hicolor PNGs (every size), decoded through wxImage
// (wxPNGHandler).
wxIconBundle windowIcons() {
    app_icon_detail::addPngHandler();
    wxIconBundle icons;
    std::size_t wanted = 0;
    {
        const wxLogNull no_log;  // a bad PNG is a warning in the log, not a wx dialog
        for (const EmbeddedPng& entry : embeddedPngs()) {
            if (!isWindowIconKey(entry.key)) {
                continue;
            }
            ++wanted;
            const wxBitmap bitmap = wxBitmap::NewFromPNGData(entry.png.data(), entry.png.size());
            if (bitmap.IsOk()) {
                wxIcon icon;
                icon.CopyFromBitmap(bitmap);
                icons.AddIcon(icon);
            }
        }
    }
    if (wanted == 0 || icons.GetIconCount() != wanted) {
        DVV_LOG_TEXT(dvv::LogLevel::Warn,
                     std::format("The window icons could not be loaded ({} of {} decoded)",
                                 icons.GetIconCount(), wanted));
        return {};
    }
    return icons;
}

wxBitmapBundle aboutLogo() {
    return app_icon_detail::aboutLogo(app_icon_detail::scalableLogo);
}

std::string bundleIconFile() {
    return app_icon_detail::bundleIconFile();
}

}  // namespace rawreel
