#pragma once

// The wx-only bodies of native_controls.h functions that some backends share: the backend files
// forward to these where their toolkit has no native call (native_controls_msw.cpp,
// native_controls_gtk.cpp, native_controls_cocoa.mm). Include it only from those files.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <cstddef>
#include <optional>

#include <wx/dataview.h>

namespace rawreel::wx_generic {

// wx's own undo of the entry (MSW, GTK).
[[nodiscard]] inline bool canUndoText(const wxTextEntry& entry, bool redo) {
    return redo ? entry.CanRedo() : entry.CanUndo();
}

inline bool undoText(wxTextEntry& entry, bool redo) {
    if (redo) {
        entry.Redo();
    } else {
        entry.Undo();
    }
    return true;
}

// wx has no represented file (MSW, GTK).
[[nodiscard]] inline wxString representedFilename() {
    return {};
}

// The row of GetTopItem() (MSW, macOS). An item of a wxDataViewVirtualListModel names its row;
// nullopt for any other model.
[[nodiscard]] inline std::optional<std::size_t> dataViewTopRow(wxDataViewCtrl& view) {
    const auto* const model = dynamic_cast<const wxDataViewVirtualListModel*>(view.GetModel());
    const wxDataViewItem top = view.GetTopItem();
    if (model == nullptr || !top.IsOk()) {
        return std::nullopt;
    }
    return model->GetRow(top);
}

// EnsureVisible scrolls as little as it can, so a row below the view first goes to the end and
// then back up to the top edge (MSW, macOS). Nothing for a model that is not a
// wxDataViewVirtualListModel.
inline void scrollDataViewRowToTop(wxDataViewCtrl& view, std::size_t row) {
    const auto* const model = dynamic_cast<const wxDataViewVirtualListModel*>(view.GetModel());
    if (model == nullptr) {
        return;
    }
    if (dataViewTopRow(view).value_or(0) < row) {
        view.EnsureVisible(model->GetItem(model->GetCount() - 1));
    }
    view.EnsureVisible(model->GetItem(static_cast<unsigned>(row)));
}

}  // namespace rawreel::wx_generic
