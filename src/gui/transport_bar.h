#pragma once

// The transport bar under the viewport (design M3 v2 §4.3, D17): First, StepBack, Play/Pause,
// StepForward, Last, the Loop toggle, the seek slider and the frame label. Buttons never take the
// focus, so Space and the arrows always reach the frame's char hook.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <cstdint>

#include <wx/slider.h>
#include <wx/tglbtn.h>

#include "core/commands.h"
#include "core/player.h"
#include "dvv/base/move_only_function.h"

namespace rawreel {

// The slider's interaction (spec §6.6): Scrubbing while the user drags the thumb.
enum class ScrubState : std::uint8_t { Idle, Scrubbing };

class TransportBar final : public wxPanel {
public:
    TransportBar(wxWindow* parent, dvv::MoveOnlyFunction<void(Command)> execute,
                 dvv::MoveOnlyFunction<void(std::int64_t)> seek);

    // Called on every status notification; a default PlayerStatus means "no stream". A range
    // change first resets the bar to Idle. While Scrubbing the slider's value is left alone (and
    // the bar returns to Idle once the left mouse button is up); while Idle it shows `target`.
    void update(const PlayerStatus& status);
    [[nodiscard]] wxSlider& slider() noexcept { return *slider_; }
    [[nodiscard]] ScrubState scrubState() const noexcept { return scrub_; }
    // Logs each state change at Info ("transport: scrub Idle -> Scrubbing").
    void setTrace(bool on) noexcept { trace_ = on; }

    // Test seams. An empty probe reads wxGetMouseState().LeftIsDown(); the observer is called at
    // the end of every update() with the slider's value (empty: none).
    void setLeftButtonProbeForTest(dvv::MoveOnlyFunction<bool()> probe);
    void setUpdateObserverForTest(dvv::MoveOnlyFunction<void(int value)> observer);

private:
    void addButton(wxSizer& sizer, const wxString& label, Command command);
    void onScroll(wxScrollEvent& event);
    void setScrub(ScrubState state);
    [[nodiscard]] bool leftButtonDown();

    dvv::MoveOnlyFunction<void(Command)> execute_;
    dvv::MoveOnlyFunction<void(std::int64_t)> seek_;
    dvv::MoveOnlyFunction<bool()> left_button_probe_;
    dvv::MoveOnlyFunction<void(int)> update_observer_;
    // Child windows: wx owns them.
    wxButton* play_ = nullptr;
    wxToggleButton* loop_ = nullptr;
    wxSlider* slider_ = nullptr;
    wxStaticText* label_ = nullptr;
    int max_ = -1;  // the slider's maximum; -1: disabled (no stream or unbounded)
    ScrubState scrub_ = ScrubState::Idle;
    int last_sent_ = -1;  // the last value this bar seeked to in the current interaction; -1: none
    bool trace_ = false;
};

}  // namespace rawreel
