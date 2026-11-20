// MainFrame's members that only wxOSX (Cocoa) has a body for: the application menu, the Settings
// window in front, and the minimum on every resize.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/main_frame.h"

#include <algorithm>

#include "gui/settings.h"

namespace rawreel {

void MainFrame::bindApplicationMenu() {
    // The application menu's About and Quit (wxOSX hides the items in Help and File). Quit closes
    // the frame through onClose; unhandled, wxOSX would end the main loop without the frame's close
    // sequence.
    for (const int id : {wxID_ABOUT, wxID_EXIT}) {
        Bind(wxEVT_MENU, &MainFrame::onMenu, this, id);
        Bind(wxEVT_UPDATE_UI, &MainFrame::onUpdateUi, this, id);
    }
}

wxWindow* MainFrame::settingsInFront() const {
    wxWindow* const settings = settings_ ? settings_->window() : nullptr;
    if (settings == nullptr) {
        return nullptr;
    }
    // wxOSX FindFocus() is null when the key window's first responder is the NSWindow itself (no
    // control has the focus), so the Settings window as the active window counts too.
    auto* const window = wxDynamicCast(settings, wxTopLevelWindow);
    wxWindow* const focus = focusedWindow();
    const bool focused = focus != nullptr && wxGetTopLevelParent(focus) == settings;
    return focused || (window != nullptr && window->IsShown() && window->IsActive()) ? settings
                                                                                     : nullptr;
}

void MainFrame::DoSetSize(int x, int y, int width, int height, int flags) {
    // The minimum is a window size (SetMinClientSize converted it); -1 keeps a dimension.
    const wxSize minimum = GetMinSize();
    if (width != wxDefaultCoord && minimum.x != wxDefaultCoord) {
        width = std::max(width, minimum.x);
    }
    if (height != wxDefaultCoord && minimum.y != wxDefaultCoord) {
        height = std::max(height, minimum.y);
    }
    wxFrame::DoSetSize(x, y, width, height, flags);
}

}  // namespace rawreel
