// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/dock_manager.h"

#include <cstddef>
#include <utility>
#include <vector>

#include <wx/aui/dockart.h>

#include "core/layout.h"

// wx-internal dependency (checked against wxWidgets 3.3.4, src/aui/framemanager.cpp): the fit
// writes the protected `m_docks` sizes that LayoutAll() keeps on every Update() (`dock.size`, the
// dock's width or height with caption and borders, the sash not included; `dock.min_size` from the
// pane MinSize), and it takes over the handlers SetManagedWindow() binds on the frame
// (`OnSize`, `OnLeftUp`, both protected), so the fit runs before the layout of a size event and
// after the end of a sash drag. Re-check both after a wxWidgets update.

namespace rawreel {

namespace {

// A dock the fit sizes: a docked pane row or column around the centre pane.
bool fitted(const wxAuiDockInfo& dock) {
    return dock.IsOk() && dock.dock_direction != wxAUI_DOCK_CENTER && !dock.fixed &&
           !dock.toolbar && !dock.panes.IsEmpty();
}

}  // namespace

DockManager::DockManager(std::function<wxSize()> centre_minimum)
    : centre_minimum_(std::move(centre_minimum)) {}

void DockManager::manage(wxWindow* frame) {
    SetManagedWindow(frame);
    frame->Unbind(wxEVT_SIZE, &DockManager::OnSize, static_cast<wxAuiManager*>(this));
    frame->Unbind(wxEVT_LEFT_UP, &DockManager::OnLeftUp, static_cast<wxAuiManager*>(this));
    frame->Bind(wxEVT_SIZE, &DockManager::onSize, this);
    frame->Bind(wxEVT_LEFT_UP, &DockManager::onLeftUp, this);
}

wxSize DockManager::minimumClientSize() const {
    const wxSize centre = centre_minimum_();
    wxWindow* frame = GetManagedWindow();
    if (frame == nullptr) {
        return centre;
    }
    const int sash = GetArtProvider()->GetMetricForWindow(wxAUI_DOCKART_SASH_SIZE, frame);
    std::vector<DockLimit> limits;
    for (const wxAuiDockInfo& dock : m_docks) {
        if (dock.IsOk() && dock.dock_direction != wxAUI_DOCK_CENTER && !dock.panes.IsEmpty()) {
            limits.push_back({.acrossWidth = dock.IsVertical(),
                              .minimum = fitted(dock) ? dock.min_size : dock.size,
                              .sash = dock.resizable ? sash : 0});
        }
    }
    const LayoutSize client = minimumClient({.width = centre.x, .height = centre.y}, limits);
    return {client.width, client.height};
}

DockManager::Remembered& DockManager::remembered(const wxAuiDockInfo& dock) {
    for (Remembered& entry : remembered_) {
        if (entry.direction == dock.dock_direction && entry.layer == dock.dock_layer &&
            entry.row == dock.dock_row) {
            return entry;
        }
    }
    return remembered_.emplace_back(Remembered{.direction = dock.dock_direction,
                                               .layer = dock.dock_layer,
                                               .row = dock.dock_row,
                                               .user = dock.size,
                                               .applied = dock.size});
}

bool DockManager::fitDocks() {
    wxWindow* frame = GetManagedWindow();
    if (frame == nullptr) {
        return false;
    }
    const wxSize client = frame->GetClientSize();
    if (client.x <= 0 || client.y <= 0) {
        return false;  // minimised: keep the sizes for the restore
    }
    const wxSize centre = centre_minimum_();
    const int sash = GetArtProvider()->GetMetricForWindow(wxAUI_DOCKART_SASH_SIZE, frame);
    bool changed = false;
    for (const bool across_width : {true, false}) {
        std::vector<wxAuiDockInfo*> docks;
        std::vector<DockFit> fits;
        int space = across_width ? client.x - centre.x : client.y - centre.y;
        for (wxAuiDockInfo& dock : m_docks) {
            if (!fitted(dock) || dock.IsVertical() != across_width) {
                continue;
            }
            Remembered& entry = remembered(dock);
            if (dock.size != entry.applied) {
                entry.user = dock.size;  // a sash drag (or wx's own layout) set it
            }
            docks.push_back(&dock);
            fits.push_back({.user = entry.user, .minimum = dock.min_size});
            space -= dock.resizable ? sash : 0;
        }
        const std::vector<int> sizes = rawreel::fitDocks(fits, space);
        for (std::size_t i = 0; i < docks.size(); ++i) {
            if (docks[i]->size != sizes[i]) {
                docks[i]->size = sizes[i];
                changed = true;
            }
            remembered(*docks[i]).applied = sizes[i];
        }
    }
    if (changed) {
        Update();
    }
    return changed;
}

void DockManager::onSize(wxSizeEvent& event) {
    if (fitDocks()) {
        event.Skip();  // Update() did the layout
        return;
    }
    OnSize(event);  // the plain layout (and the update postponed while minimised)
}

void DockManager::onLeftUp(wxMouseEvent& event) {
    OnLeftUp(event);  // ends a sash drag with the dock at the user's new size
    fitDocks();
}

}  // namespace rawreel
