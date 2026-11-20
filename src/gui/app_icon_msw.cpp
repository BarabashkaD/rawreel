// app_icon.h on Windows (wxMSW): the window icons from the exe's resource.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/app_icon.h"

#include <string>

#include "dvv/base/log.h"
#include "gui/app_icon_detail.h"

#if !wxUSE_ICO_CUR
#error "rawreel-gui needs wxWidgets with wxUSE_ICO_CUR (the window icons from the ICO resource)"
#endif

namespace rawreel {

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

wxBitmapBundle aboutLogo() {
    return app_icon_detail::aboutLogo(app_icon_detail::scalableLogo);
}

std::string bundleIconFile() {
    return app_icon_detail::bundleIconFile();
}

}  // namespace rawreel
