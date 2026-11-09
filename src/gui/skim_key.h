#pragma once

// The skim key (video control panel spec §3, §6.4, U-CP7): ⌥ Option on macOS, Ctrl on Windows and
// Linux. It is read from an event's own modifier state, never by polling; its release is the key's
// wxEVT_KEY_UP, which goes to the focused window only, so a wxEventFilter forwards it.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <wx/event.h>
#include <wx/kbdstate.h>

#include "dvv/base/move_only_function.h"

namespace rawreel {

// wxOSX: AltDown() (there ControlDown() is ⌘ Command); elsewhere ControlDown().
[[nodiscard]] bool skimKeyDown(const wxKeyboardState& state) noexcept;
// wxOSX: WXK_ALT; elsewhere WXK_CONTROL.
[[nodiscard]] bool isSkimKey(int key_code) noexcept;

// Calls `released` on every wxEVT_KEY_UP of the skim key, in any window of the application. The
// event's own modifier flags are not read: GTK reports them from before the event. Every event is
// passed on unchanged (Event_Skip). Registered while it lives; GUI thread only.
class SkimKeyRelease final : public wxEventFilter {
public:
    explicit SkimKeyRelease(dvv::MoveOnlyFunction<void()> released);
    SkimKeyRelease(const SkimKeyRelease&) = delete;
    SkimKeyRelease& operator=(const SkimKeyRelease&) = delete;
    SkimKeyRelease(SkimKeyRelease&&) = delete;
    SkimKeyRelease& operator=(SkimKeyRelease&&) = delete;
    ~SkimKeyRelease() override;

    int FilterEvent(wxEvent& event) override;

private:
    dvv::MoveOnlyFunction<void()> released_;
};

}  // namespace rawreel
