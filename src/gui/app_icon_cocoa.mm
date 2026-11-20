// app_icon.h on macOS (wxOSX, Cocoa): the icons come from the bundle.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/app_icon.h"

#include <string>

#include <CoreFoundation/CoreFoundation.h>
#include <wx/osx/core/cfstring.h>

#include "gui/app_icon_detail.h"

namespace rawreel {

namespace {

// macOS uses the exact scales 1 and 2 only.
wxBitmapBundle exactScalesLogo(wxImage source) {
    return wxBitmapBundle::FromBitmaps(
        app_icon_detail::scaledLogo(source, wxSize(ABOUT_LOGO_DIP, ABOUT_LOGO_DIP)),
        app_icon_detail::scaledLogo(source, wxSize(2 * ABOUT_LOGO_DIP, 2 * ABOUT_LOGO_DIP)));
}

}  // namespace

// macOS: the bundle's rawreel.icns (CFBundleIconFile) is the Dock's and the title's icon (D3).
wxIconBundle windowIcons() {
    return {};
}

wxBitmapBundle aboutLogo() {
    return app_icon_detail::aboutLogo(exactScalesLogo);
}

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

}  // namespace rawreel
