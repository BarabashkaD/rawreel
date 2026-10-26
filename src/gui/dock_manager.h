#pragma once

// The main frame's wxAUI manager with docks that give way (M4 GUI D8). Plain wxAuiManager
// sizes a dock once, when it is created, and keeps that size on every later frame resize, so only
// the centre pane shrinks, down to nothing. DockManager fits the docks into the client size before
// every layout: each dock keeps the size the user gave it with the sash while the centre pane keeps
// its minimum (core/layout.h), gives way down to its own minimum when the window shrinks and grows
// back when the window grows.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <functional>
#include <vector>

#include <wx/aui/framemanager.h>

namespace rawreel {

class DockManager final : public wxAuiManager {
public:
    // `centre_minimum`: the centre pane's minimum size in physical pixels.
    explicit DockManager(std::function<wxSize()> centre_minimum);

    // SetManagedWindow(frame), with the frame's size and left-up events routed through the fit.
    void manage(wxWindow* frame);
    // The smallest client size that keeps the centre pane at its minimum with every dock at its
    // own; valid after Update().
    [[nodiscard]] wxSize minimumClientSize() const;
    // Fits the docks into the current client size; Update() and true if a dock size changed.
    bool fitDocks();

private:
    // A dock's identity across layouts and the sizes the fit saw: `user` is the last size the dock
    // had that the fit did not set (its first layout or a sash drag), `applied` the fit's last.
    struct Remembered {
        int direction = 0;
        int layer = 0;
        int row = 0;
        int user = 0;
        int applied = 0;
    };

    void onSize(wxSizeEvent& event);
    void onLeftUp(wxMouseEvent& event);
    Remembered& remembered(const wxAuiDockInfo& dock);

    std::function<wxSize()> centre_minimum_;
    std::vector<Remembered> remembered_;
};

}  // namespace rawreel
