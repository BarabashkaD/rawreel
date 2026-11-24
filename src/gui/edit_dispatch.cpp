// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/edit_dispatch.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <typeinfo>

#include <wx/anybutton.h>
#include <wx/bookctrl.h>
#include <wx/calctrl.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/ctrlsub.h>
#include <wx/dataview.h>
#include <wx/datectrl.h>
#include <wx/gauge.h>
#include <wx/grid.h>
#include <wx/hyperlink.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/radiobox.h>
#include <wx/radiobut.h>
#include <wx/scrolbar.h>
#include <wx/slider.h>
#include <wx/spinbutt.h>
#include <wx/spinctrl.h>
#include <wx/statbmp.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textentry.h>
#include <wx/timectrl.h>
#include <wx/treectrl.h>

#include "core/gui_arguments.h"
#include "core/log_text.h"
#include "gui/app.h"
#include "gui/log_list.h"
#include "gui/log_panel.h"
#include "gui/native_controls.h"
#include "gui/native_policy.h"
#include "gui/selftest.h"

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

// `focus` is the inner window of a grid or a data view, or the control itself: these move their
// selection with every key and start editing on a typed character.
bool inGridOrDataView(const wxWindow& focus) {
    for (const wxWindow* window = &focus; window != nullptr && !window->IsTopLevel();
         window = window->GetParent()) {
        if (dynamic_cast<const wxGrid*>(window) != nullptr ||
            dynamic_cast<const wxDataViewCtrl*>(window) != nullptr) {
            return true;
        }
    }
    return false;
}

// A drop-down choice without a text field (not a combo box).
bool isPlainChoice(const wxWindow& focus) {
    return dynamic_cast<const wxChoice*>(&focus) != nullptr &&
           dynamic_cast<const wxComboBox*>(&focus) == nullptr;
}

// A control that takes every plain key: it edits text, typing selects an item, or it moves a date,
// a time or a cell. A plain choice takes Space only where the OS's choice opens on it.
bool takesEveryKey(const wxWindow& focus, const KeyChord& key) {
    if (isPlainChoice(focus) && key.key == static_cast<std::int32_t>(Key::Space)) {
        return native_policy::choiceTakesSpace();
    }
    const bool items = dynamic_cast<const wxItemContainer*>(&focus) != nullptr;
    if (const auto* entry = dynamic_cast<const wxTextEntry*>(&focus); entry != nullptr && !items) {
        return entry->IsEditable();  // read-only: the caret keys only (takesArrows)
    }
    return items || dynamic_cast<const wxSpinCtrl*>(&focus) != nullptr ||
           dynamic_cast<const wxSpinCtrlDouble*>(&focus) != nullptr ||
           dynamic_cast<const wxListCtrl*>(&focus) != nullptr ||
           dynamic_cast<const wxTreeCtrl*>(&focus) != nullptr ||
           dynamic_cast<const wxDatePickerCtrl*>(&focus) != nullptr ||
           dynamic_cast<const wxTimePickerCtrl*>(&focus) != nullptr ||
           dynamic_cast<const wxCalendarCtrl*>(&focus) != nullptr || inGridOrDataView(focus);
}

// A window that uses no plain key: a top-level window, a plain panel (of no derived class, so a
// scrolled window or a list built on a panel is not one), static text, a static box, bitmap or
// line, a gauge, a book without tabs.
bool keepsNoKey(const wxWindow& focus) {
    if (focus.IsTopLevel() || typeid(focus) == typeid(wxPanel)) {
        return true;
    }
    if (dynamic_cast<const wxControl*>(&focus) == nullptr) {
        return false;  // a window this rule does not know: the arrows and Space
    }
    if (const auto* book = dynamic_cast<const wxBookCtrlBase*>(&focus); book != nullptr) {
        return dynamic_cast<const wxNotebook*>(book) == nullptr;
    }
    return dynamic_cast<const wxStaticText*>(&focus) != nullptr ||
           dynamic_cast<const wxStaticBox*>(&focus) != nullptr ||
           dynamic_cast<const wxStaticBitmap*>(&focus) != nullptr ||
           dynamic_cast<const wxStaticLine*>(&focus) != nullptr ||
           dynamic_cast<const wxGauge*>(&focus) != nullptr;
}

// A control that moves a value, a caret or a selection with the arrows, Home and End.
bool takesArrows(const wxWindow& focus) {
    return dynamic_cast<const wxTextEntry*>(&focus) != nullptr ||
           dynamic_cast<const wxSlider*>(&focus) != nullptr ||
           dynamic_cast<const wxSpinButton*>(&focus) != nullptr ||
           dynamic_cast<const wxScrollBar*>(&focus) != nullptr ||
           dynamic_cast<const wxRadioButton*>(&focus) != nullptr ||
           dynamic_cast<const wxRadioBox*>(&focus) != nullptr ||
           dynamic_cast<const wxNotebook*>(&focus) != nullptr;
}

// A control that Space presses, toggles, follows or selects.
bool takesSpace(const wxWindow& focus) {
    return dynamic_cast<const wxAnyButton*>(&focus) != nullptr ||
           dynamic_cast<const wxCheckBox*>(&focus) != nullptr ||
           dynamic_cast<const wxHyperlinkCtrl*>(&focus) != nullptr ||
           dynamic_cast<const wxRadioButton*>(&focus) != nullptr ||
           dynamic_cast<const wxRadioBox*>(&focus) != nullptr;
}

bool isArrowKey(const KeyChord& key) {
    for (const Key arrow : {Key::Left, Key::Right, Key::Up, Key::Down, Key::Home, Key::End}) {
        if (key.key == static_cast<std::int32_t>(arrow)) {
            return true;
        }
    }
    return false;
}

// A selftest's copy goes to its clipboard hook (spec §7.1), so the developer's clipboard stays
// untouched; otherwise the control's own Copy() and Cut().
bool clipboardHooked() {
    return App::arguments().selftest != Scenario::None;
}

// Undo and Redo per toolkit (native_controls.h): wxOSX's CanUndo() is false for a single-line field
// (spec §4.2), so there they use the native undo manager.
bool canEditText(int id, wxWindow& window, const wxTextEntry& entry) {
    const bool editable = entry.IsEditable();
    switch (id) {
        case wxID_UNDO: return editable && canUndoText(window, entry, false);
        case wxID_REDO: return editable && canUndoText(window, entry, true);
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
        case wxID_REDO: (void)undoText(window, entry, id == wxID_REDO); return;
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

bool controlKeepsKey(const wxWindow* focus, const KeyChord& key) {
    if (focus == nullptr || key.ctrl || key.alt) {
        return false;
    }
    if (takesEveryKey(*focus, key)) {
        return true;
    }
    if (isPlainChoice(*focus)) {
        return false;  // Space where the choice does not open on it
    }
    if (keepsNoKey(*focus)) {
        return false;
    }
    const bool arrows = takesArrows(*focus);
    const bool space = takesSpace(*focus);
    const bool unknown = !arrows && !space;  // fails safe: the arrows and Space
    if (isArrowKey(key)) {
        return arrows || unknown;
    }
    return key.key == static_cast<std::int32_t>(Key::Space) && (space || unknown);
}

}  // namespace rawreel
