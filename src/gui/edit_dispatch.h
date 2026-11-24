#pragma once

// The Edit menu and the focus that keeps the player's keys. The main window handles wxEVT_MENU
// and wxEVT_UPDATE_UI for the Edit ids by calling the action on the focused control directly,
// never by sending the event again (it would climb back to the main window and recurse). On macOS
// these events reach the main window only when the native responder chain did not handle the item
// (cut:, copy:, paste:, delete:, selectAll:), and always for Undo and Redo, which wxOSX does not
// map.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "core/commands.h"

namespace rawreel {

class LogPanel;

// The Edit menu at index 1 (after File): Undo, Redo, Cut, Copy, Paste, Delete (wxID_CLEAR, no key),
// Select All, with the keys of each OS (spec §4.2).
void buildEditMenu(wxMenuBar& bar);
// Runs Edit item `id` on `focus`: an editable wxTextEntry gets its own action (enabled by its
// Can...()), a read-only one Copy and Select All, the Log list Copy (the selected records) and
// Select All; anything else nothing. Copied text goes through setClipboardText. False when nothing
// ran.
bool runEdit(int id, wxWindow* focus, LogPanel& log);
// Whether runEdit(id, focus, log) would do something now (the item's enabled state).
[[nodiscard]] bool canEdit(int id, wxWindow* focus, const LogPanel& log);
// The focus keeps every key (no playback key reaches the player): an editable wxTextEntry, or a
// shown window in the subtree of `log_list`, the Log list's control (LogList::control()). The
// Log pane's wrapper panels and a hidden pane keep no keys: they can hold the native focus with
// nothing visibly focused.
[[nodiscard]] bool focusKeepsKeys(wxWindow* focus, const wxWindow* log_list);
// A floating window's focus keeps the plain `key` (no Ctrl, no Alt) when the focused native control
// uses that key itself; every other plain key goes to the player. Every plain key: an editable text
// entry, a spin control, a choice, combo box or list, a tree, a date, time or calendar control, a
// grid or a data view (also their inner windows). On Windows a choice does not take Space
// (choiceTakesSpace). The arrows, Home and End: a read-only text entry, a slider, a spin button, a
// scroll bar, a radio button or box, a notebook's tabs. Space: a button, a toggle button, a
// checkbox, a hyperlink, a radio button or box. A top-level window, a plain panel (of no derived
// class), static text, static boxes, bitmaps and lines, a gauge, a book without tabs and no focus
// keep no key. Any other window keeps the arrows and Space (a scrolled window, a list built on a
// panel, a custom window too), so a window this rule does not know fails safe.
[[nodiscard]] bool controlKeepsKey(const wxWindow* focus, const KeyChord& key);
// `window` is `ancestor` or one of its descendants (the parent chain, up to the top-level window).
[[nodiscard]] bool windowWithin(const wxWindow* window, const wxWindow* ancestor);

}  // namespace rawreel
