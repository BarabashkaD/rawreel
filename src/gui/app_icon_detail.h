#pragma once

// The parts of app_icon.h that the backends share: the embedded PNGs and About's logo source. The
// backend files use them (app_icon_msw.cpp, app_icon_gtk.cpp, app_icon_cocoa.mm). Include it only
// from those files.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <wx/bmpbndl.h>
#include <wx/image.h>
#include <wx/imagpng.h>
#include <wx/mstream.h>

#include "dvv/base/log.h"
#include "gui/app_icon.h"
#include "gui/app_icon_data.h"

#if !wxUSE_LIBPNG
#error "rawreel-gui needs wxWidgets with wxUSE_LIBPNG (the embedded PNGs, app icon plan D7)"
#endif

namespace rawreel::app_icon_detail {

// About's logo, owl-on-film-reel/transparent-background/<key>.png (embed-png.cmake keys it by its
// stem), LOGO_SOURCE_PX square; scaled at run time.
inline constexpr std::string_view LOGO_KEY = "owl-on-film-reel-transparent-background-1024";
inline constexpr int LOGO_SOURCE_PX = 1024;

// The embedded PNGs are decoded through wxImage (D7).
inline void addPngHandler() {
    if (wxImage::FindHandler(wxBITMAP_TYPE_PNG) == nullptr) {
        wxImage::AddHandler(new wxPNGHandler);
    }
}

// The embedded PNG `key` decoded through wxImage; not ok if it is missing or bad (wxLogNull: a
// warning in the log, not a wx dialog).
[[nodiscard]] inline wxImage embeddedImage(std::string_view key) {
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
[[nodiscard]] inline wxBitmap scaledLogo(const wxImage& source, const wxSize& size) {
    if (source.GetSize() == size) {
        return wxBitmap(source);
    }
    return wxBitmap(source.Scale(size.x, size.y, wxIMAGE_QUALITY_HIGH));
}

// About's logo: the embedded source, checked, then `bundle` (the backend's body) makes the bundle
// from it. Empty, with one warning, if the PNG does not load.
[[nodiscard]] inline wxBitmapBundle aboutLogo(wxBitmapBundle (*bundle)(wxImage source)) {
    wxImage source = embeddedImage(LOGO_KEY);
    if (!source.IsOk() || source.GetSize() != wxSize(LOGO_SOURCE_PX, LOGO_SOURCE_PX) ||
        !source.HasAlpha()) {
        DVV_LOG_TEXT(dvv::LogLevel::Warn,
                     std::format("About's logo could not be loaded ({})", LOGO_KEY));
        return {};
    }
    return bundle(std::move(source));
}

// D6: the logo at any scale (MSW, GTK). wxBitmapBundle::FromBitmaps would snap the preferred size
// at 125, 150 and 175 % to an existing entry, so the logo would have the wrong logical size there.
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

// About's logo at any scale, exactly ABOUT_LOGO_DIP DIP (MSW, GTK).
[[nodiscard]] inline wxBitmapBundle scalableLogo(wxImage source) {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the bundle takes the reference-counted impl
    return wxBitmapBundle::FromImpl(new LogoBundleImpl(std::move(source)));
}

// No bundle, so no icon file (MSW, GTK).
[[nodiscard]] inline std::string bundleIconFile() {
    return {};
}

}  // namespace rawreel::app_icon_detail
