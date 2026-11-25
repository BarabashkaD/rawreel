// native_policy.h on Windows (wxMSW).

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <optional>
#include <utility>

#include "gui/native_policy.h"
#include "gui/native_policy_wx.h"

namespace rawreel::native_policy {

bool ctrlWheelZooms() noexcept {
    return wx_generic::ctrlWheelZooms();
}

wxString revealFolderLabel() {
    return "Show in Explorer";
}

// SystemLogSink writes with OutputDebugStringW there, which is not the Event Log.
wxString systemLogLabel() {
    return "Send warnings and errors to the debugger output";
}

void applySettingsPageStyle(wxWindow& page, dvv::MoveOnlyFunction<void(wxWindow& top)> bind_shown) {
    wx_generic::applySettingsPageStyle(page, std::move(bind_shown));
}

bool shownItemHasChangingTitle() noexcept {
    return wx_generic::shownItemHasChangingTitle();
}

std::optional<int> stockId(Command command) noexcept {
    return wx_generic::stockId(command);
}

std::optional<Command> commandOfStockId(int id) noexcept {
    return wx_generic::commandOfStockId(id);
}

bool settingsAtEndOfEdit() noexcept {
    return wx_generic::settingsAtEndOfEdit();
}

bool closeCommandClosesSettingsFirst() noexcept {
    return wx_generic::closeCommandClosesSettingsFirst();
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

bool returnTakesDefaultButtonWithoutFocus() noexcept {
    return wx_generic::returnTakesDefaultButtonWithoutFocus();
}

bool infoCopiesOnCtrlC() noexcept {
    return wx_generic::infoCopiesOnCtrlC();
}

// Windows: the window is sized from its sizer around the rect the OS suggests; keeping the old
// top-left could move it back to the old display and start a stream of WM_DPICHANGED messages.
bool dpiChangeTakesSuggestedRect() noexcept {
    return true;
}

bool hidesStaleMapShow() noexcept {
    return wx_generic::hidesStaleMapShow();
}

long labelAlignment() noexcept {
    return wx_generic::labelAlignment();
}

bool floatingForwardsAccelerators() noexcept {
    return wx_generic::floatingForwardsAccelerators();
}

bool choiceTakesSpace() noexcept {
    return false;
}

// The move cursor.
wxStockCursor panCursor() noexcept {
    return wxCURSOR_SIZING;
}

double dipPerFontPoint() noexcept {
    return 96.0 / 72.0;
}

}  // namespace rawreel::native_policy
