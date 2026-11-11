// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/app_icon.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dvv/base/log.h"
#include "gui/app_icon_data.h"

#if !wxUSE_LIBPNG
#error "rawreel-gui needs wxWidgets with wxUSE_LIBPNG (the embedded PNGs, app icon plan D7)"
#endif

#if defined(__WXMSW__)
#if !wxUSE_ICO_CUR
#error "rawreel-gui needs wxWidgets with wxUSE_ICO_CUR (the window icons from the ICO resource)"
#endif
#endif
#include <wx/image.h>
#include <wx/imagpng.h>
#include <wx/mstream.h>

#if defined(__WXOSX__)
#include <CoreFoundation/CoreFoundation.h>
#include <wx/osx/core/cfstring.h>
#endif

namespace rawreel {

namespace {

// About's logo, owl-on-film-reel/transparent-background/<key>.png (embed-png.cmake keys it by its
// stem), LOGO_SOURCE_PX square; scaled at run time.
constexpr std::string_view LOGO_KEY = "owl-on-film-reel-transparent-background-1024";
constexpr int LOGO_SOURCE_PX = 1024;

// The embedded PNGs are decoded through wxImage (D7).
void addPngHandler() {
    if (wxImage::FindHandler(wxBITMAP_TYPE_PNG) == nullptr) {
        wxImage::AddHandler(new wxPNGHandler);
    }
}

#if defined(__WXGTK__)
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
#endif

}  // namespace

#if defined(__WXMSW__)

// D4: every image of the exe's "rawreel" icon group (rawreel-gui.rc); wx picks the sizes for the
// window's DPI and picks again on a DPI change.
wxIconBundle windowIcons() {
    wxIconBundle icons;
    {
        const wxLogNull no_log;  // a missing group is a warning in the log, not a wx dialog
        icons = wxIconBundle(wxS("rawreel"), nullptr);
    }
    if (icons.IsEmpty()) {
        DVV_LOG_TEXT(dvv::LogLevel::Warn,
                     "The window icons could not be loaded from the rawreel icon resource");
    }
    return icons;
}

#elif defined(__WXGTK__)

// D5, D7: the embedded hicolor PNGs (every size), decoded through wxImage
// (wxPNGHandler).
wxIconBundle windowIcons() {
    addPngHandler();
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

#else

// macOS: the bundle's rawreel.icns (CFBundleIconFile) is the Dock's and the title's icon (D3).
wxIconBundle windowIcons() {
    return {};
}

#endif

namespace {

// The embedded PNG `key` decoded through wxImage; not ok if it is missing or bad (wxLogNull: a
// warning in the log, not a wx dialog).
wxImage embeddedImage(std::string_view key) {
    addPngHandler();
    const wxLogNull no_log;
    for (const EmbeddedPng& entry : embeddedPngs()) {
        if (entry.key == key) {
            wxMemoryInputStream stream(entry.png.data(), entry.png.size());
            return wxImage(stream, wxBITMAP_TYPE_PNG);
        }
    }
    return {};
}

// The logo at `size` pixels: the 1024 px source scaled with wxIMAGE_QUALITY_HIGH (an
// alpha-weighted box average when shrinking, bicubic when growing).
wxBitmap scaledLogo(const wxImage& source, const wxSize& size) {
    if (source.GetSize() == size) {
        return wxBitmap(source);
    }
    return wxBitmap(source.Scale(size.x, size.y, wxIMAGE_QUALITY_HIGH));
}

#if defined(__WXMSW__) || defined(__WXGTK__)

// D6: the logo at any scale. wxBitmapBundle::FromBitmaps would snap the preferred size at 125,
// 150 and 175 % to an existing entry, so the logo would have the wrong logical size there.
class LogoBundleImpl final : public wxBitmapBundleImpl {
public:
    // The source, LOGO_SOURCE_PX square.
    explicit LogoBundleImpl(wxImage source) : source_(std::move(source)) {}

    wxSize GetDefaultSize() const override { return {ABOUT_LOGO_DIP, ABOUT_LOGO_DIP}; }

    wxSize GetPreferredBitmapSizeAtScale(double scale) const override {
        return {wxRound(ABOUT_LOGO_DIP * scale), wxRound(ABOUT_LOGO_DIP * scale)};
    }

    // The source scaled to `size`, cached per size.
    wxBitmap GetBitmap(const wxSize& size) override {
        for (const auto& [cached_size, bitmap] : cache_) {
            if (cached_size == size) {
                return bitmap;
            }
        }
        wxBitmap result = scaledLogo(source_, size);
        cache_.emplace_back(size, result);
        return result;
    }

private:
    wxImage source_;
    std::vector<std::pair<wxSize, wxBitmap>> cache_;
};

#endif

}  // namespace

wxBitmapBundle aboutLogo() {
    wxImage source = embeddedImage(LOGO_KEY);
    if (!source.IsOk() || source.GetSize() != wxSize(LOGO_SOURCE_PX, LOGO_SOURCE_PX) ||
        !source.HasAlpha()) {
        DVV_LOG_TEXT(dvv::LogLevel::Warn,
                     std::format("About's logo could not be loaded ({})", LOGO_KEY));
        return {};
    }
#if defined(__WXOSX__)
    // macOS uses the exact scales 1 and 2 only.
    return wxBitmapBundle::FromBitmaps(
        scaledLogo(source, wxSize(ABOUT_LOGO_DIP, ABOUT_LOGO_DIP)),
        scaledLogo(source, wxSize(2 * ABOUT_LOGO_DIP, 2 * ABOUT_LOGO_DIP)));
#else
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the bundle takes the reference-counted impl
    return wxBitmapBundle::FromImpl(new LogoBundleImpl(std::move(source)));
#endif
}

#if defined(__WXOSX__)

std::string bundleIconFile() {
    CFBundleRef bundle = CFBundleGetMainBundle();
    if (bundle == nullptr) {
        return {};
    }
    CFTypeRef value = CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("CFBundleIconFile"));
    if (value == nullptr || CFGetTypeID(value) != CFStringGetTypeID()) {
        return {};
    }
    return wxCFStringRef::AsString(static_cast<CFStringRef>(value)).utf8_string();
}

#endif

}  // namespace rawreel
