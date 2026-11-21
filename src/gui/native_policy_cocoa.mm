// native_policy.h on macOS (wxOSX, Cocoa).

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <optional>
#include <utility>

#include "gui/native_policy.h"
#include "gui/native_policy_wx.h"

namespace rawreel::native_policy {

// macOS has no scroll zoom (design §5): there the wheel pans with Cmd too.
bool ctrlWheelZooms() noexcept {
    return false;
}

wxString revealFolderLabel() {
    return "Show in Finder";
}

wxString systemLogLabel() {
    return "Send warnings and errors to the system log (Console)";
}

void applySettingsPageStyle(wxWindow& page, dvv::MoveOnlyFunction<void(wxWindow& top)> bind_shown) {
    wx_generic::applySettingsPageStyle(page, std::move(bind_shown));
}

// Spec §2: macOS shows and hides a part of a window with a title that changes, not a check mark.
bool logItemHasChangingTitle() noexcept {
    return true;
}

std::optional<int> stockId(Command command) noexcept {
    if (command == Command::About) {
        return wxID_ABOUT;
    }
    if (command == Command::Quit) {
        return wxID_EXIT;
    }
    return std::nullopt;
}

std::optional<Command> commandOfStockId(int id) noexcept {
    if (id == wxID_ABOUT) {
        return Command::About;
    }
    if (id == wxID_EXIT) {
        return Command::Quit;
    }
    return std::nullopt;
}

bool settingsAtEndOfEdit() noexcept {
    return wx_generic::settingsAtEndOfEdit();
}

// Spec §5.1: the Settings window has no menu of its own.
bool closeCommandClosesSettingsFirst() noexcept {
    return true;
}

bool reappliesMinimumAfterMap() noexcept {
    return wx_generic::reappliesMinimumAfterMap();
}

bool hideMeansIconified() noexcept {
    return wx_generic::hideMeansIconified();
}

bool flushesClipboardOnExit() noexcept {
    return wx_generic::flushesClipboardOnExit();
}

bool spinButtonsTakeBestWidth() noexcept {
    return wx_generic::spinButtonsTakeBestWidth();
}

bool undoesForeignResize() noexcept {
    return wx_generic::undoesForeignResize();
}

// macOS without Full Keyboard Access (the system default): lists, check boxes and buttons never
// take the focus, so Return goes to the default button's key equivalent.
bool returnTakesDefaultButtonWithoutFocus() noexcept {
    return true;
}

bool infoCopiesOnCtrlC() noexcept {
    return false;
}

bool dpiChangeTakesSuggestedRect() noexcept {
    return wx_generic::dpiChangeTakesSuggestedRect();
}

bool hidesStaleMapShow() noexcept {
    return wx_generic::hidesStaleMapShow();
}

// The closed hand.
wxStockCursor panCursor() noexcept {
    return wxCURSOR_CLOSED_HAND;
}

}  // namespace rawreel::native_policy
