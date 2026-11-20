// MainFrame's members that only wxOSX has a body for (main_frame_cocoa.mm), on the other toolkits:
// no application menu, and no Settings window that ⌘W closes first.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/main_frame.h"

namespace rawreel {

void MainFrame::bindApplicationMenu() {}

wxWindow* MainFrame::settingsInFront() const {
    return nullptr;
}

}  // namespace rawreel
