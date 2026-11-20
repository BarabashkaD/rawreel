#pragma once

// The native_policy.h values that two toolkits share: wx's own behaviour, where the third toolkit
// has its own. The backend files forward to these (native_policy_msw.cpp, native_policy_gtk.cpp,
// native_policy_cocoa.mm). Include it only from those files.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <optional>

#include "core/commands.h"
#include "dvv/base/move_only_function.h"

namespace rawreel::wx_generic {

[[nodiscard]] constexpr bool ctrlWheelZooms() noexcept {  // MSW, GTK
    return true;
}

// Nothing to set up (MSW, macOS).
inline void applySettingsPageStyle(wxWindow& /*page*/,
                                   dvv::MoveOnlyFunction<void(wxWindow& top)> /*bind_shown*/) {}

[[nodiscard]] constexpr bool logItemHasChangingTitle() noexcept {  // MSW, GTK
    return false;
}

// No stock ids besides the Settings one that every toolkit has (MSW, GTK).
[[nodiscard]] constexpr std::optional<int> stockId(Command /*command*/) noexcept {
    return std::nullopt;
}

[[nodiscard]] constexpr std::optional<Command> commandOfStockId(int /*id*/) noexcept {
    return std::nullopt;
}

[[nodiscard]] constexpr bool settingsAtEndOfEdit() noexcept {  // MSW, macOS
    return false;
}

[[nodiscard]] constexpr bool closeCommandClosesSettingsFirst() noexcept {  // MSW, GTK
    return false;
}

[[nodiscard]] constexpr bool reappliesMinimumAfterMap() noexcept {  // MSW, macOS
    return false;
}

[[nodiscard]] constexpr bool hideMeansIconified() noexcept {  // MSW, macOS
    return false;
}

[[nodiscard]] constexpr bool flushesClipboardOnExit() noexcept {  // MSW, macOS
    return false;
}

[[nodiscard]] constexpr bool spinButtonsTakeBestWidth() noexcept {  // MSW, macOS
    return false;
}

[[nodiscard]] constexpr bool undoesForeignResize() noexcept {  // MSW, macOS
    return false;
}

[[nodiscard]] constexpr bool returnTakesDefaultButtonWithoutFocus() noexcept {  // MSW, GTK
    return false;
}

[[nodiscard]] constexpr bool infoCopiesOnCtrlC() noexcept {  // MSW, GTK
    return true;
}

[[nodiscard]] constexpr bool dpiChangeTakesSuggestedRect() noexcept {  // GTK, macOS
    return false;
}

[[nodiscard]] constexpr bool hidesStaleMapShow() noexcept {  // MSW, macOS
    return false;
}

}  // namespace rawreel::wx_generic
