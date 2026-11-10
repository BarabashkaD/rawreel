// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/edit_dispatch.h"

#include <algorithm>
#include <array>

#include "core/gui_arguments.h"
#include "core/log_text.h"
#include "gui/app.h"
#include "gui/log_list.h"
#include "gui/log_panel.h"
#include "gui/selftest.h"
#if defined(__WXOSX__)
#include "gui/native_controls.h"
#endif

namespace rawreel {

namespace {

constexpr std::array<int, 7> EDIT_IDS{wxID_UNDO,  wxID_REDO,  wxID_CUT,      wxID_COPY,
                                      wxID_PASTE, wxID_CLEAR, wxID_SELECTALL};

// `focus` is in `list`'s subtree (the list control itself or a child of it, such as the header) and
// shown. Only the list control counts: a hidden pane, or the pane's own wrapper panels, can hold
// the native focus with nothing visibly focused, and then they keep no keys and get no Edit item.
bool inList(const wxWindow* focus, const wxWindow* list) {
    return list != nullptr && focus != nullptr && focus->IsShownOnScreen() &&
           windowWithin(focus, list);
}

// A selftest's copy goes to its clipboard hook (spec §7.1), so the developer's clipboard stays
// untouched; otherwise the control's own Copy() and Cut().
bool clipboardHooked() {
    return App::arguments().selftest != Scenario::None;
}

bool canUndo(wxWindow& window, const wxTextEntry& entry, bool redo) {
#if defined(__WXOSX__)
    // wxOSX's CanUndo() is false for a single-line field (spec §4.2): the native undo manager.
    (void)entry;
    return canUndoText(window, redo);
#else
    (void)window;
    return redo ? entry.CanRedo() : entry.CanUndo();
#endif
}

bool canEditText(int id, wxWindow& window, const wxTextEntry& entry) {
    const bool editable = entry.IsEditable();
    switch (id) {
        case wxID_UNDO: return editable && canUndo(window, entry, false);
        case wxID_REDO: return editable && canUndo(window, entry, true);
        case wxID_CUT: return editable && entry.CanCut();
        case wxID_COPY: return entry.CanCopy();
        case wxID_PASTE: return editable && entry.CanPaste();
        case wxID_CLEAR: return editable && entry.HasSelection();
        case wxID_SELECTALL: return !entry.IsEmpty();
        default: return false;
    }
}

void runEditText(int id, wxWindow& window, wxTextEntry& entry) {
    switch (id) {
        case wxID_UNDO:
        case wxID_REDO:
#if defined(__WXOSX__)
            (void)entry;
            (void)undoText(window, id == wxID_REDO);
#else
            (void)window;
            if (id == wxID_REDO) {
                entry.Redo();
            } else {
                entry.Undo();
            }
#endif
            return;
        case wxID_CUT:
            if (clipboardHooked()) {
                setClipboardText(entry.GetStringSelection().utf8_string());
                entry.RemoveSelection();
            } else {
                entry.Cut();
            }
            return;
        case wxID_COPY:
            if (clipboardHooked()) {
                setClipboardText(entry.GetStringSelection().utf8_string());
            } else {
                entry.Copy();
            }
            return;
        case wxID_PASTE: entry.Paste(); return;
        case wxID_CLEAR: entry.RemoveSelection(); return;
        case wxID_SELECTALL: entry.SelectAll(); return;
        default: return;
    }
}

bool canEditLog(int id, const LogPanel& log) {
    switch (id) {
        case wxID_COPY: return log.hasSelection();
        case wxID_SELECTALL: return log.list().rowCount() > 0;
        default: return false;
    }
}

// The Edit ids the main window binds.
bool isEditId(int id) noexcept {
    return std::ranges::find(EDIT_IDS, id) != EDIT_IDS.end();
}

}  // namespace

bool windowWithin(const wxWindow* window, const wxWindow* ancestor) {
    for (const wxWindow* parent = window; parent != nullptr; parent = parent->GetParent()) {
        if (parent == ancestor) {
            return true;
        }
        if (parent->IsTopLevel()) {
            return false;
        }
    }
    return false;
}

void buildEditMenu(wxMenuBar& bar) {
    auto* menu = new wxMenu();
    // "Ctrl" is ⌘ on macOS (spec §4.2).
    menu->Append(wxID_UNDO, "Undo\tCtrl+Z");
#if defined(__WXMSW__)
    menu->Append(wxID_REDO, "Redo\tCtrl+Y");
#else
    menu->Append(wxID_REDO, "Redo\tShift+Ctrl+Z");
#endif
    menu->AppendSeparator();
    menu->Append(wxID_CUT, "Cut\tCtrl+X");
    menu->Append(wxID_COPY, "Copy\tCtrl+C");
    menu->Append(wxID_PASTE, "Paste\tCtrl+V");
    // No key: the Delete key stays with the focused control on every OS (wxOSX maps the id to the
    // native delete:).
    menu->Append(wxID_CLEAR, "Delete");
    menu->AppendSeparator();
    menu->Append(wxID_SELECTALL, "Select All\tCtrl+A");
    bar.Insert(1, menu, "Edit");
}

bool runEdit(int id, wxWindow* focus, LogPanel& log) {
    if (focus == nullptr || !canEdit(id, focus, log)) {
        return false;
    }
    if (inList(focus, log.list().control())) {
        if (id == wxID_COPY) {
            setClipboardText(logRecordsText(log.selectedRecords()));  // a snapshot of the rows
        } else {
            log.list().selectAll();
        }
        return true;
    }
    if (auto* entry = dynamic_cast<wxTextEntry*>(focus); entry != nullptr) {
        runEditText(id, *focus, *entry);
        return true;
    }
    return false;
}

bool canEdit(int id, wxWindow* focus, const LogPanel& log) {
    if (focus == nullptr || !isEditId(id)) {
        return false;
    }
    if (inList(focus, log.list().control())) {
        return canEditLog(id, log);
    }
    if (const auto* entry = dynamic_cast<const wxTextEntry*>(focus); entry != nullptr) {
        return canEditText(id, *focus, *entry);
    }
    return false;
}

bool focusKeepsKeys(wxWindow* focus, const wxWindow* log_list) {
    if (focus == nullptr) {
        return false;
    }
    if (const auto* entry = dynamic_cast<const wxTextEntry*>(focus);
        entry != nullptr && entry->IsEditable()) {
        return true;
    }
    return inList(focus, log_list);
}

}  // namespace rawreel
