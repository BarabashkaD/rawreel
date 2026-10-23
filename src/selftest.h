#pragma once

// The --selftest script of rawreel-gui (design M1b §5.6). A 50 ms timer drives five steps, each
// with a 5 s deadline: (1) first frames, (2) resize 800x600 and 333x177, (3) hide, (4) restore,
// (5) close. A missed deadline exits 6 naming the step; a presenter failure exits 4.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include <wx/timer.h>

#include "dvv/gpu/presenter.h"
#include "dvv/platform/native_window.h"

namespace rawreel {

class MainFrame;
class VulkanViewport;

class SelfTest final : private wxTimer {
public:
    // Starts the timer. The frame owns the selftest and deletes it before the viewport.
    SelfTest(MainFrame& frame, VulkanViewport& viewport);
    SelfTest(const SelfTest&) = delete;
    SelfTest& operator=(const SelfTest&) = delete;
    ~SelfTest() override;

private:
    enum class Step : std::uint8_t {
        FirstFrames,
        Resize800,
        Resize333,
        Hide,
        Restore,
        Close,
        Done
    };

    void Notify() override;  // one tick
    // Whether the current step's condition holds.
    [[nodiscard]] bool passed(const dvv::gpu::PresenterStats& stats) const;
    // Records the step's start values, then performs its action.
    void enter(Step step);
    // Stops the script and hands the result to App::finish().
    void finish(int code, const std::string& message);
    [[nodiscard]] std::string seen() const;  // what the current step saw, for its failure message

    static int number(Step step) noexcept;
    static std::string_view name(Step step) noexcept;

    MainFrame* frame_;
    VulkanViewport* viewport_;
    Step step_ = Step::FirstFrames;
    std::chrono::steady_clock::time_point step_start_;
    int ticks_ = 0;  // ticks since the step started
    std::uint64_t start_frames_ = 0;
    std::uint64_t start_swapchains_ = 0;
    dvv::platform::PixelSize previous_size_{};
};

}  // namespace rawreel
