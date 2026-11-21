// native_policy.h on Linux (wxGTK).

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <optional>

#include "gui/native_policy.h"
#include "gui/native_policy_wx.h"

namespace rawreel::native_policy {

bool ctrlWheelZooms() noexcept {
    return wx_generic::ctrlWheelZooms();
}

wxString revealFolderLabel() {
    return "Open Folder";
}

wxString systemLogLabel() {
    return "Send warnings and errors to the system log (journal/syslog)";
}

// Spec §5.1: the window is only hidden between shows.
void applySettingsPageStyle(wxWindow& page, dvv::MoveOnlyFunction<void(wxWindow& top)> bind_shown) {
    if (wxWindow* top = wxGetTopLevelParent(&page); top != nullptr) {
        bind_shown(*top);
    }
}

bool logItemHasChangingTitle() noexcept {
    return wx_generic::logItemHasChangingTitle();
}

std::optional<int> stockId(Command command) noexcept {
    return wx_generic::stockId(command);
}

std::optional<Command> commandOfStockId(int id) noexcept {
    return wx_generic::commandOfStockId(id);
}

// Spec §5.1: GTK puts Preferences... at the end of Edit, after a separator.
bool settingsAtEndOfEdit() noexcept {
    return true;
}

bool closeCommandClosesSettingsFirst() noexcept {
    return wx_generic::closeCommandClosesSettingsFirst();
}

// wxGTK keeps a minimum as a window size, converted with the decoration size it knew at
// SetMinClientSize(); the decorations are known only after the map.
bool reappliesMinimumAfterMap() noexcept {
    return true;
}

// X11: Hide stands for iconify.
bool hideMeansIconified() noexcept {
    return true;
}

// GTK hands the copied text to the clipboard manager.
bool flushesClipboardOnExit() noexcept {
    return true;
}

// A GTK 3 spin button draws its - and + buttons beside the entry; 72 DIP is less than its
// minimum (Gtk-CRITICAL "size >= 0" in GtkSpinButton), so GTK gets its best width.
bool spinButtonsTakeBestWidth() noexcept {
    return true;
}

bool undoesForeignResize() noexcept {
    return true;
}

bool returnTakesDefaultButtonWithoutFocus() noexcept {
    return wx_generic::returnTakesDefaultButtonWithoutFocus();
}

bool infoCopiesOnCtrlC() noexcept {
    return wx_generic::infoCopiesOnCtrlC();
}

bool dpiChangeTakesSuggestedRect() noexcept {
    return wx_generic::dpiChangeTakesSuggestedRect();
}

// X11 reports a map asynchronously: a map that arrives after a Hide() shows the window again.
bool hidesStaleMapShow() noexcept {
    return true;
}

// Fleur: wxGTK maps wxCURSOR_SIZENWSE to it.
wxStockCursor panCursor() noexcept {
    return wxCURSOR_SIZENWSE;
}

}  // namespace rawreel::native_policy
