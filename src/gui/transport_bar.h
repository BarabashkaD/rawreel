#pragma once

// The transport bar under the viewport (design M3 v2 §4.3, D17): First, StepBack, Play/Pause,
// StepForward, Last, the Loop toggle, the seek slider and the frame label. Buttons never take the
// focus, so Space and the arrows always reach the frame's char hook.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

#include <wx/slider.h>
#include <wx/tglbtn.h>

#include "core/commands.h"
#include "core/player.h"

namespace rawreel {

class TransportBar final : public wxPanel {
public:
    TransportBar(wxWindow* parent, std::move_only_function<void(Command)> execute,
                 std::move_only_function<void(std::int64_t)> seek);

    // Called by the 50 ms status timer; a default PlayerStatus means "no stream". The slider's
    // value is left alone for 250 ms after a scroll event (the user is dragging it).
    void update(const PlayerStatus& status);
    [[nodiscard]] wxSlider& slider() noexcept { return *slider_; }

private:
    void addButton(wxSizer& sizer, const wxString& label, Command command);
    void onScroll(wxScrollEvent& event);

    std::move_only_function<void(Command)> execute_;
    std::move_only_function<void(std::int64_t)> seek_;
    // Child windows: wx owns them.
    wxButton* play_ = nullptr;
    wxToggleButton* loop_ = nullptr;
    wxSlider* slider_ = nullptr;
    wxStaticText* label_ = nullptr;
    int max_ = -1;  // the slider's maximum; -1: disabled (no stream or unbounded)
    std::chrono::steady_clock::time_point last_scroll_{};
};

}  // namespace rawreel
