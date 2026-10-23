// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "selftest.h"

#include <cstdio>
#include <format>
#include <optional>

#include "app.h"
#include "arguments.h"
#include "dvv/base/error.h"
#include "dvv/base/log.h"
#include "main_frame.h"
#include "vulkan_viewport.h"

namespace rawreel {

namespace {

constexpr int TICK_MS = 50;
constexpr std::chrono::seconds STEP_DEADLINE{5};
constexpr int HIDE_TICKS = 6;  // 300 ms

}  // namespace

SelfTest::SelfTest(MainFrame& frame, VulkanViewport& viewport)
    : frame_(&frame), viewport_(&viewport), step_start_(std::chrono::steady_clock::now()) {
    Start(TICK_MS);
}

SelfTest::~SelfTest() {
    Stop();
    // The window closed (X or WM_CLOSE) before the script finished: the selftest fails. Every
    // decided outcome (the step 5 pass or an earlier finish()) has already set Step::Done.
    if (step_ != Step::Done && !App::exitCode()) {
        try {
            App::record(exit_code::SELFTEST_FAILED,
                        std::format("rawreel-gui --selftest: step {} ({}) failed: the window "
                                    "closed before the step finished",
                                    number(step_), name(step_)));
        } catch (...) {
            std::fputs("rawreel-gui --selftest: the window closed early\n", stderr);
        }
    }
}

void SelfTest::Notify() {
    if (step_ == Step::Done) {
        return;
    }
    ++ticks_;
    if (dvv::gpu::Presenter* presenter = viewport_->presenter(); presenter != nullptr) {
        // Also while hidden: the presenter must ignore requests then (step 3).
        presenter->requestFrame();
        if (const std::optional<dvv::Error> failure = presenter->failure(); failure) {
            finish(exit_code::GPU_ERROR,
                   "rawreel-gui --selftest: the presenter failed: " + failure->describe());
            return;
        }
        if (passed(presenter->stats())) {
            DVV_LOG_INFO("rawreel-gui --selftest: step {} ({}) passed: {}", number(step_),
                         name(step_), seen());
            enter(static_cast<Step>(static_cast<std::uint8_t>(step_) + 1));
            return;
        }
    }
    if (std::chrono::steady_clock::now() - step_start_ >= STEP_DEADLINE) {
        finish(exit_code::SELFTEST_FAILED,
               std::format("rawreel-gui --selftest: step {} ({}) failed: {}", number(step_),
                           name(step_), seen()));
    }
}

bool SelfTest::passed(const dvv::gpu::PresenterStats& stats) const {
    const dvv::platform::PixelSize size = viewport_->physicalClientSize();
    switch (step_) {
        case Step::FirstFrames: return stats.framesPresented >= 2 && stats.presentedExtent == size;
        case Step::Resize800:
        case Step::Resize333:
            return stats.presentedExtent == size && stats.swapchainsCreated > start_swapchains_ &&
                   size != previous_size_;
        case Step::Hide:
            // One frame that was in flight when the window was hidden is tolerated.
            return ticks_ >= HIDE_TICKS && stats.framesPresented <= start_frames_ + 1 &&
                   stats.hidden && !stats.failed;
        case Step::Restore: return stats.framesPresented > start_frames_ && !stats.hidden;
        case Step::Close:
        case Step::Done: return false;
    }
    return false;
}

void SelfTest::enter(Step step) {
    step_ = step;
    step_start_ = std::chrono::steady_clock::now();
    ticks_ = 0;
    // The start values are taken before the step's action, which sends its events at once.
    dvv::gpu::Presenter* presenter = viewport_->presenter();
    if (presenter != nullptr) {
        const dvv::gpu::PresenterStats stats = presenter->stats();
        start_frames_ = stats.framesPresented;
        start_swapchains_ = stats.swapchainsCreated;
    }
    previous_size_ = viewport_->physicalClientSize();
    switch (step) {
        case Step::FirstFrames:
        case Step::Done: break;
        // Logical pixels (no FromDIP): the checks compare with the physical size, so any DPI
        // scale passes, and 800x600 differs from the initial 960x540 (DIP) at every scale.
        case Step::Resize800: frame_->SetClientSize(wxSize(800, 600)); break;
        case Step::Resize333: frame_->SetClientSize(wxSize(333, 177)); break;
        case Step::Hide:
#if defined(_WIN32)
            frame_->Iconize(true);
#else
            frame_->Hide();  // compiled, not run in v1.0 (U-WIN)
#endif
            break;
        case Step::Restore:
#if defined(_WIN32)
            frame_->Iconize(false);
#else
            frame_->Show();
#endif
            break;
        case Step::Close: {
            if (presenter == nullptr || presenter->failure() || presenter->stats().failed) {
                finish(exit_code::GPU_ERROR,
                       std::format("rawreel-gui --selftest: step 5 (close) failed: {}", seen()));
                return;
            }
            step_ = Step::Done;
            Stop();
            // Queued, so that this tick returns before the frame deletes the selftest. App::OnExit
            // checks the validation messages, main() the leaks.
            MainFrame* frame = frame_;
            frame->CallAfter([frame] { frame->Close(); });
            break;
        }
    }
}

void SelfTest::finish(int code, const std::string& message) {
    step_ = Step::Done;
    Stop();
    wxGetApp().finish(code, message);
}

std::string SelfTest::seen() const {
    const dvv::gpu::Presenter* presenter = viewport_->presenter();
    if (presenter == nullptr) {
        return "no presenter";
    }
    const dvv::gpu::PresenterStats stats = presenter->stats();
    const dvv::platform::PixelSize size = viewport_->physicalClientSize();
    std::string text = std::format(
        "{} frames presented ({} at the step start), presented extent {}x{}, viewport {}x{} "
        "(previously {}x{}), {} swapchains created ({} at the step start), hidden {}, failed {}",
        stats.framesPresented, start_frames_, stats.presentedExtent.width,
        stats.presentedExtent.height, size.width, size.height, previous_size_.width,
        previous_size_.height, stats.swapchainsCreated, start_swapchains_, stats.hidden,
        stats.failed);
    if (const std::optional<dvv::Error> failure = presenter->failure(); failure) {
        text += ", failure: " + failure->describe();
    }
    return text;
}

int SelfTest::number(Step step) noexcept {
    switch (step) {
        case Step::FirstFrames: return 1;
        case Step::Resize800:
        case Step::Resize333: return 2;
        case Step::Hide: return 3;
        case Step::Restore: return 4;
        case Step::Close:
        case Step::Done: return 5;
    }
    return 5;
}

std::string_view SelfTest::name(Step step) noexcept {
    switch (step) {
        case Step::FirstFrames: return "first frames";
        case Step::Resize800: return "resize 800x600";
        case Step::Resize333: return "resize 333x177";
        case Step::Hide: return "hide";
        case Step::Restore: return "restore";
        case Step::Close:
        case Step::Done: return "close";
    }
    return "close";
}

}  // namespace rawreel
