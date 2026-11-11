#pragma once

// The window icons and About's logo (app icon plan D4 to D7), from packaging/icons/owl-on-film-reel
// (packaging/icons/README.md). GUI thread only.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#if defined(__WXOSX__)
#include <string>
#endif

#include <wx/bmpbndl.h>
#include <wx/iconbndl.h>

namespace rawreel {

// About's logo, the owl on a transparent background, a square of ABOUT_LOGO_DIP x ABOUT_LOGO_DIP.
inline constexpr int ABOUT_LOGO_DIP = 128;

// The frame's icons: Windows the "rawreel" ICO group of the exe (10 sizes); Linux (GTK) the
// embedded hicolor set (11 sizes); macOS none (the Dock and the title use the bundle's
// rawreel.icns). Empty on a load failure, which logs one warning.
[[nodiscard]] wxIconBundle windowIcons();

// About's logo, default size ABOUT_LOGO_DIP x ABOUT_LOGO_DIP, one image for light and dark (the
// panel colour shows through), from the embedded 1024 px transparent-background PNG scaled with
// wxIMAGE_QUALITY_HIGH: macOS FromBitmaps(1x, 2x); Windows and GTK at any scale, exactly
// ABOUT_LOGO_DIP DIP (cached per size). Empty, with one warning, if the PNG does not load.
[[nodiscard]] wxBitmapBundle aboutLogo();

#if defined(__WXOSX__)
// The main bundle's CFBundleIconFile ("" if absent), for the smoke check.
[[nodiscard]] std::string bundleIconFile();
#endif

}  // namespace rawreel
