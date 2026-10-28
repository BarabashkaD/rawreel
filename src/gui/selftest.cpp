// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/selftest.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <wx/aui/framemanager.h>
// clang-format off
#include <wx/infobar.h>  // wxInfoBarBase, before the generic class
#include <wx/generic/infobar.h>
// clang-format on
// clang-format off
#include <wx/aboutdlg.h>  // wxAboutDialogInfo, before the generic dialog
#include <wx/generic/aboutdlgg.h>
// clang-format on
#include <wx/collpane.h>
#include <wx/hyperlink.h>
#include <wx/settings.h>
#include <wx/slider.h>
#include <wx/textdlg.h>

#include "core/commands.h"
#include "core/format_choice.h"
#include "core/layout.h"
#include "core/perf_stats.h"
#include "core/session.h"
#include "core/startup.h"
#include "core/version_text.h"
#include "core/view.h"
#include "dvv/base/error.h"
#include "dvv/base/json.h"
#include "dvv/base/log.h"
#include "dvv/base/uri.h"
#include "dvv/components/ffmpeg.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/gpu_info_json.h"
#include "dvv/gpu/presenter.h"
#include "dvv/gpu/testing/fault_injection.h"
#include "dvv/media/format_json.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/types.h"
#include "dvv/platform/native_window.h"
#include "gui/about.h"
#include "gui/app.h"
#include "gui/drop_target.h"
#include "gui/format_dialog.h"
#include "gui/info_panel.h"
#include "gui/log_panel.h"
#include "gui/main_frame.h"
#include "gui/player_panel.h"
#include "gui/transport_bar.h"
#include "gui/viewport.h"
#include "render/display_state.h"

namespace rawreel {

namespace {

constexpr int TICK_MS = 50;

// Sets a flag for the lifetime of the guard (the busy guard of the runner).
class BusyGuard {
public:
    explicit BusyGuard(bool& flag) noexcept : flag_(&flag) { *flag_ = true; }
    BusyGuard(const BusyGuard&) = delete;
    BusyGuard& operator=(const BusyGuard&) = delete;
    BusyGuard(BusyGuard&&) = delete;
    BusyGuard& operator=(BusyGuard&&) = delete;
    ~BusyGuard() { *flag_ = false; }

private:
    bool* flag_;
};

SelfTestStep step(std::string name, std::move_only_function<void()> start,
                  std::move_only_function<bool(std::string&)> passed,
                  std::chrono::milliseconds deadline = std::chrono::seconds(5)) {
    return {.name = std::move(name),
            .start = std::move(start),
            .passed = std::move(passed),
            .deadline = deadline};
}

// Set while the script injects an event (injectInput).
bool& injecting() {
    static bool flag = false;
    return flag;
}

// Keyboard, mouse-button, wheel and menu events: what a person at the machine sends.
bool isRealInput(wxEventType type) {
    static const std::array<wxEventType, 20> REAL_INPUT_TYPES{
        wxEVT_KEY_DOWN,      wxEVT_KEY_UP,       wxEVT_CHAR,         wxEVT_CHAR_HOOK,
        wxEVT_LEFT_DOWN,     wxEVT_LEFT_UP,      wxEVT_LEFT_DCLICK,  wxEVT_RIGHT_DOWN,
        wxEVT_RIGHT_UP,      wxEVT_RIGHT_DCLICK, wxEVT_MIDDLE_DOWN,  wxEVT_MIDDLE_UP,
        wxEVT_MIDDLE_DCLICK, wxEVT_AUX1_DOWN,    wxEVT_AUX1_UP,      wxEVT_AUX2_DOWN,
        wxEVT_AUX2_UP,       wxEVT_MOUSEWHEEL,   wxEVT_CONTEXT_MENU, wxEVT_MENU};
    return std::ranges::find(REAL_INPUT_TYPES, type) != REAL_INPUT_TYPES.end();
}

// The About dialog's size from its creation until the hook clicks: a dialog that is resized,
// re-laid out or hidden and shown again after it appears flashes on screen.
struct AboutShowLog {
    int shows = 0;           // wxEVT_SHOW events that showed it
    wxSize atShow;           // its size when it was first shown
    int sizeChanges = 0;     // size events with another size after it was first shown
    wxSize last;             // its size at the last size event
    bool closed = false;     // it was hidden before the hook ran
    bool recording = false;  // set by showModal, cleared by the hook before its clicks
};

AboutShowLog& aboutShowLog() {
    static AboutShowLog log;
    return log;
}

}  // namespace

int SelfTestInput::FilterEvent(wxEvent& event) {
    if (injecting() || !isRealInput(event.GetEventType())) {
        return Event_Skip;
    }
    ++ignored_;
    DVV_LOG_INFO("rawreel-gui --selftest: real input ignored (event type {})",
                 event.GetEventType());
    return Event_Processed;  // handled: no handler and no native default processing sees it
}

bool injectInput(wxEvtHandler& target, wxEvent& event) {
    const BusyGuard mark(injecting());
    return target.ProcessEvent(event);
}

SelfTest::SelfTest(MainFrame& frame, App& app) : frame_(&frame), app_(&app) {}

SelfTest::~SelfTest() {
    if (filtering_) {
        wxEvtHandler::RemoveFilter(&input_);
    }
    Stop();
    // The window closed (X or WM_CLOSE) before the script finished: the selftest fails. Every
    // decided outcome (the close step or an earlier finish()) has already set finished_.
    if (!finished_ && !App::exitCode()) {
        try {
            App::record(exit_code::SELFTEST_FAILED,
                        std::format("rawreel-gui --selftest: {} failed: the window closed before "
                                    "the step finished",
                                    stepLabel()));
        } catch (...) {
            std::fputs("rawreel-gui --selftest: the window closed early\n", stderr);
        }
    }
}

void SelfTest::start(Scenario scenario) {
    scenario_ = scenario;
    std::optional<SelfTestScript> script = selfTestScript(scenario, *this);
    if (!script) {
        fail(exit_code::SELFTEST_FAILED,
             std::format("rawreel-gui --selftest: scenario {} is not available",
                         scenarioName(scenario)));
        return;
    }
    script_ = std::move(*script);
    // Real input must not change a result (a real L once inverted the player's loop step).
    wxEvtHandler::AddFilter(&input_);
    filtering_ = true;
    // The first tick enters step 1 inside the event loop.
    Start(TICK_MS);
}

void SelfTest::stop() noexcept {  // NOLINT(bugprone-virtual-near-miss)
    stopped_ = true;
    Stop();
}

void SelfTest::Notify() {
    if (busy_ || finished_ || stopped_) {
        return;
    }
    if (!entered_) {
        entered_ = true;
        enter(0);
        return;
    }
    frame_->viewport().requestFrame();  // also while hidden: the presenter must ignore it then
    if (const std::optional<std::string> failure = presenterFailure(); failure) {
        fail(exit_code::GPU_ERROR, "rawreel-gui --selftest: the presenter failed: " + *failure);
        return;
    }
    SelfTestStep& step = script_.steps[step_];
    std::string seen;
    bool ok = true;
    if (step.passed) {
        const BusyGuard busy(busy_);
        ok = step.passed(seen);
    }
    if (finished_ || stopped_) {
        return;
    }
    if (ok) {
        DVV_LOG_INFO("rawreel-gui --selftest: {} passed: {}", stepLabel(), seen);
        enter(step_ + 1);  // may be the terminal close step: return at once
        return;
    }
    if (std::chrono::steady_clock::now() - step_start_ >= step.deadline) {
        fail(exit_code::SELFTEST_FAILED,
             std::format("rawreel-gui --selftest: {} failed: {}", stepLabel(), seen));
    }
}

void SelfTest::enter(std::size_t step) {
    step_ = step;
    if (step_ >= script_.steps.size()) {
        closeStep();
        return;
    }
    step_start_ = std::chrono::steady_clock::now();
    if (SelfTestStep& current = script_.steps[step_]; current.start) {
        const BusyGuard busy(busy_);
        current.start();
    }
}

void SelfTest::closeStep() {
    finished_ = true;
    Stop();
    if (const std::optional<std::string> failure = presenterFailure(); failure) {
        app_->finish(exit_code::GPU_ERROR,
                     "rawreel-gui --selftest: the presenter failed: " + *failure);
        return;
    }
    // App::OnExit checks the validation messages, main() the leaks. Close() runs onClose now and
    // queues the frame's deletion, which deletes this runner: no member access afterwards.
    frame_->Close();
}

void SelfTest::fail(int code, const std::string& message) {
    finished_ = true;
    Stop();
    app_->finish(code, message);
}

std::optional<std::string> SelfTest::presenterFailure() const {
    if (!script_.presenterFailureFails) {
        return std::nullopt;
    }
    const Viewport& viewport = frame_->viewport();
    // Recorded by the viewport when the failure reached the GUI thread: by then the presenter is
    // usually gone (D25 drops it at once, a lost device at the next stackChanged).
    if (const std::optional<dvv::Error>& failed = viewport.lastFailure(); failed) {
        return failed->describe();
    }
    // A failure whose report is still queued for the GUI thread.
    if (const dvv::gpu::Presenter* presenter = viewport.presenter(); presenter != nullptr) {
        if (const std::optional<dvv::Error> failure = presenter->failure(); failure) {
            return failure->describe();
        }
    }
    return std::nullopt;
}

std::string SelfTest::stepLabel() const {
    if (step_ < script_.steps.size()) {
        return std::format("{} step {} ({})", scenarioName(scenario_), step_ + 1,
                           script_.steps[step_].name);
    }
    return std::format("{} (close)", scenarioName(scenario_));
}

std::optional<SelfTestScript> selfTestScript(Scenario scenario, SelfTest& test) {
    switch (scenario) {
        case Scenario::Smoke: return SelfTestScript{.steps = smokeScenario(test)};
        // The presenter fails with DeviceLost on purpose.
        case Scenario::Lost:
            return SelfTestScript{.steps = lostScenario(test), .presenterFailureFails = false};
        case Scenario::Player: return SelfTestScript{.steps = playerScenario(test)};
        case Scenario::Perf: return SelfTestScript{.steps = perfScenario(test)};
        case Scenario::None: return std::nullopt;
    }
    return std::nullopt;
}

// --- smoke (design §8.4; the M1b steps kept) ----------------------------------------------------

namespace {

constexpr int HIDE_TICKS = 6;  // 300 ms
constexpr std::string_view SMOKE_STREAM = "synthetic:gradient?w=64&h=48&fmt=yuv420p10le&frames=40";

// The values a step compares with, taken when it starts (before its action, which sends its
// events at once).
constexpr std::string_view REFUSED_STREAM = "synthetic:gradient?w=64&h=48&fmt=yuv420p&frames=4";

struct SmokeState {
    std::uint64_t startFrames = 0;
    std::uint64_t startSwapchains = 0;
    dvv::platform::PixelSize previousSize{};
    int ticks = 0;
    std::optional<std::uint64_t> framesWhenShown;   // open stream: when the frame was first seen
    std::optional<std::uint64_t> framesWhenHidden;  // hide: when the presenter was first hidden
    std::size_t logLength = 0;                      // menus: the Log pane's text at the step start
    std::size_t streamsBefore = 0;                  // D3: streams shown when the open started
    bool aboutHookRan = false;                      // about: the About hook ran
    bool aboutButtonsOk = false;                    // about: notices and copy buttons found
    bool aboutParentOk = false;                     // about: the dialog's parent is the frame
    int aboutWidth = 0;             // about: the dialog's width before the Components pane opens
    int aboutWidthExpanded = 0;     // about: its width (or wanted width) with the pane open
    int aboutHeightOpen = 0;        // about: its client height with the pane open the first time
    int aboutHeightClosed = 0;      // about: its client height with the pane closed again
    int aboutHeightReopen = 0;      // about: its client height with the pane open again
    wxSize aboutListReopen;         // about: the Components list size with the pane open again
    int aboutListMin = 0;           // about: the list's minimum height (all its lines)
    bool aboutListShown = false;    // about: the list is shown on screen with the pane open again
    std::uint64_t about0 = 0;       // about: aboutShown() at the step start
    int aboutShows = 0;             // about: times it was shown before the hook's clicks
    int aboutSizeChanges = 0;       // about: size changes after it was first shown
    bool aboutClosedEarly = false;  // about: it was hidden before the hook's clicks
    wxSize aboutAtShow;             // about: its size when it was first shown
    wxSize aboutAtHook;             // about: its size 1 s later
    std::vector<std::string> aboutLinks;  // about: the URLs of its links
    std::uint64_t presented0 = 0;         // lavfi: frames presented at the step start
    std::string streamUri;                // Format... on lavfi: the stream's URI
    std::uint64_t dialogs0 = 0;           // Format... on lavfi: format dialogs shown
};

std::optional<dvv::gpu::PresenterStats> statsOf(const Viewport& viewport) {
    if (const dvv::gpu::Presenter* presenter = viewport.presenter(); presenter != nullptr) {
        return presenter->stats();
    }
    return std::nullopt;
}

// Frames presented so far (0 without a presenter); smoke, lost and player use it.
std::uint64_t presented(Viewport& viewport) {
    const dvv::gpu::Presenter* presenter = viewport.presenter();
    return presenter != nullptr ? presenter->stats().framesPresented : 0;
}

// DVV_REQUIRE_FFMPEG=1: a missing FFmpeg fails the FFmpeg steps instead of skipping them.
bool ffmpegRequired() {
    wxString value;
    return wxGetEnv("DVV_REQUIRE_FFMPEG", &value) && value == "1";
}

// The step a scenario gets instead of its FFmpeg steps under DVV_REQUIRE_FFMPEG=1: it fails at its
// deadline with `reason`.
SelfTestStep ffmpegRequiredStep(std::string reason) {
    return step(
        "FFmpeg required", {},
        [reason = std::move(reason)](std::string& seen) {
            seen = reason;
            return false;
        },
        std::chrono::seconds(1));
}

std::string describe(const Viewport& viewport, const SmokeState& state) {
    const std::optional<dvv::gpu::PresenterStats> stats = statsOf(viewport);
    if (!stats) {
        return "no presenter";
    }
    const dvv::platform::PixelSize size = viewport.physicalClientSize();
    return std::format(
        "{} frames presented ({} at the step start), presented extent {}x{}, viewport {}x{} "
        "(previously {}x{}), {} swapchains created ({} at the step start), hidden {}, failed {}",
        stats->framesPresented, state.startFrames, stats->presentedExtent.width,
        stats->presentedExtent.height, size.width, size.height, state.previousSize.width,
        state.previousSize.height, stats->swapchainsCreated, state.startSwapchains, stats->hidden,
        stats->failed);
}

void markStart(const Viewport& viewport, SmokeState& state) {
    if (const std::optional<dvv::gpu::PresenterStats> stats = statsOf(viewport); stats) {
        state.startFrames = stats->framesPresented;
        state.startSwapchains = stats->swapchainsCreated;
    }
    state.previousSize = viewport.physicalClientSize();
    state.ticks = 0;
    state.framesWhenHidden.reset();
}

using SharedState = std::shared_ptr<SmokeState>;

// 1. The empty Session's background is presented at the client size.
SelfTestStep firstFramesStep(SelfTest* test, const SharedState& state) {
    auto passed = [test, state](std::string& seen) {
        const Viewport& viewport = test->frame().viewport();
        seen = describe(viewport, *state);
        const std::optional<dvv::gpu::PresenterStats> stats = statsOf(viewport);
        return stats && stats->framesPresented >= 2 &&
               stats->presentedExtent == viewport.physicalClientSize();
    };
    return {.name = "first frames", .start = {}, .passed = std::move(passed)};
}

// `size` is the viewport's size in logical pixels (no FromDIP): the frame grows by what the panes,
// the infobar and the borders around the viewport take (the docks keep their sash size while the
// viewport has room, D8). At a high scale (250 %) or on a small screen that client can be below the
// minimum the shrink step reached, so the client is at least the minimum plus more
// than the docks give way (Info 160 DIP, Log 100 DIP) and the viewport differs at every scale.
SelfTestStep resizeStep(SelfTest* test, const SharedState& state, std::string name, wxSize size) {
    auto start = [test, state, size] {
        markStart(test->frame().viewport(), *state);
        MainFrame& frame = test->frame();
        const wxSize around = frame.GetClientSize() - frame.viewport().GetClientSize();
        const wxSize least = frame.GetMinClientSize() + frame.FromDIP(wxSize(240, 160));
        const wxSize client = size + around;
        frame.SetClientSize(std::max(client.x, least.x), std::max(client.y, least.y));
    };
    auto passed = [test, state](std::string& seen) {
        const Viewport& viewport = test->frame().viewport();
        seen = describe(viewport, *state);
        const std::optional<dvv::gpu::PresenterStats> stats = statsOf(viewport);
        const dvv::platform::PixelSize now = viewport.physicalClientSize();
        return stats && stats->presentedExtent == now &&
               stats->swapchainsCreated > state->startSwapchains && now != state->previousSize;
    };
    return {.name = std::move(name), .start = std::move(start), .passed = std::move(passed)};
}

// The window shrinks below its minimum (M4 GUI D8): asked for a 100x80 viewport, the frame
// stops at its minimum client size, the docks give way and the viewport keeps 320x240 DIP; the
// smaller viewport gets a new swapchain of its size.
SelfTestStep shrinkStep(SelfTest* test, const SharedState& state) {
    auto start = [test, state] {
        markStart(test->frame().viewport(), *state);
        MainFrame& frame = test->frame();
        const wxSize around = frame.GetClientSize() - frame.viewport().GetClientSize();
        frame.SetClientSize(wxSize(100, 80) + around);
    };
    auto passed = [test, state](std::string& seen) {
        MainFrame& frame = test->frame();
        const Viewport& viewport = frame.viewport();
        const std::optional<dvv::gpu::PresenterStats> stats = statsOf(viewport);
        const dvv::platform::PixelSize now = viewport.physicalClientSize();
        const wxSize client = frame.GetClientSize();
        const wxSize minimum = frame.GetMinClientSize();
        const wxSize least = frame.FromDIP(wxSize(MIN_VIEWPORT_DIP.width, MIN_VIEWPORT_DIP.height));
        const bool minimal = std::cmp_greater_equal(now.width, least.x) &&
                             std::cmp_greater_equal(now.height, least.y);
        const wxSize panel = frame.playerPanel().GetClientSize();
        const wxSize centre = frame.playerPanel().minimumSize();
        wxWindow& bar = frame.playerPanel().transportBar();
        seen = std::format(
            "{}; client {}x{}, minimum client {}x{}, viewport minimum {}x{}, player panel {}x{} "
            "(minimum {}x{}), transport {}x{} (best {}x{}), frame {}x{} (minimum {}x{})",
            describe(viewport, *state), client.x, client.y, minimum.x, minimum.y, least.x, least.y,
            panel.x, panel.y, centre.x, centre.y, bar.GetSize().x, bar.GetSize().y,
            bar.GetBestSize().x, bar.GetBestSize().y, frame.GetSize().x, frame.GetSize().y,
            frame.GetMinSize().x, frame.GetMinSize().y);
        return stats && minimum.x > 0 && minimum.y > 0 && client.x >= minimum.x &&
               client.y >= minimum.y && minimal && stats->presentedExtent == now &&
               stats->swapchainsCreated > state->startSwapchains && now != state->previousSize;
    };
    return {.name = "shrink stops at the minimum",
            .start = std::move(start),
            .passed = std::move(passed)};
}

// A pane moved at the minimum: Info docked as a second bottom row raises the
// minimum client height, the frame grows to it and the viewport keeps 320x240 DIP. `back` docks
// Info on the right again, as the later steps expect.
SelfTestStep movedPaneStep(SelfTest* test, const SharedState& state, bool back) {
    auto start = [test, state, back] {
        markStart(test->frame().viewport(), *state);
        MainFrame& frame = test->frame();
        wxAuiPaneInfo& info = frame.aui().GetPane("info");
        if (back) {
            info.Right().Row(0);
        } else {
            info.Bottom().Row(1);
        }
        frame.aui().Update();
        const wxSize around = frame.GetClientSize() - frame.viewport().GetClientSize();
        frame.SetClientSize(wxSize(100, 80) + around);
    };
    auto passed = [test, state, back](std::string& seen) {
        MainFrame& frame = test->frame();
        const Viewport& viewport = frame.viewport();
        const std::optional<dvv::gpu::PresenterStats> stats = statsOf(viewport);
        const dvv::platform::PixelSize now = viewport.physicalClientSize();
        const wxSize least = frame.FromDIP(wxSize(MIN_VIEWPORT_DIP.width, MIN_VIEWPORT_DIP.height));
        const wxSize panel = frame.playerPanel().GetClientSize();
        const wxSize centre = frame.playerPanel().minimumSize();
        const int direction = frame.aui().GetPane("info").dock_direction;
        const bool docked = direction == (back ? wxAUI_DOCK_RIGHT : wxAUI_DOCK_BOTTOM);
        // The panel holds the infobar's reserve too, so the viewport alone would pass without the
        // fix while no message is shown. No tolerance: the frame's minimum carries a 2 DIP margin
        // for wxAUI's rounding.
        const bool minimal = std::cmp_greater_equal(now.width, least.x) &&
                             std::cmp_greater_equal(now.height, least.y) && panel.x >= centre.x &&
                             panel.y >= centre.y;
        seen = std::format("{}; info dock {}, player panel {}x{}, its minimum {}x{}",
                           describe(viewport, *state), direction, panel.x, panel.y, centre.x,
                           centre.y);
        return stats && docked && minimal && stats->presentedExtent == now;
    };
    return {.name = back ? "pane moved back" : "moved pane keeps the minimum",
            .start = std::move(start),
            .passed = std::move(passed)};
}

// 4. Hidden: no frames. The count starts when the presenter first reports hidden, because frames
// already requested or in flight when the window is hidden may still be presented; one more frame
// after that is tolerated.
SelfTestStep hideStep(SelfTest* test, const SharedState& state) {
    auto start = [test, state] {
        markStart(test->frame().viewport(), *state);
#if defined(_WIN32)
        test->frame().Iconize(true);
#else
        test->frame().Hide();  // X11: no WM-independent iconify (Xvfb has no WM); Hide/Show
#endif
    };
    auto passed = [test, state](std::string& seen) {
        const Viewport& viewport = test->frame().viewport();
        seen = describe(viewport, *state);
        const std::optional<dvv::gpu::PresenterStats> stats = statsOf(viewport);
        if (!stats || !stats->hidden) {
            return false;
        }
        if (!state->framesWhenHidden) {
            state->framesWhenHidden = stats->framesPresented;
            state->ticks = 0;
        }
        return ++state->ticks >= HIDE_TICKS &&
               stats->framesPresented <= *state->framesWhenHidden + 1 && !stats->failed;
    };
    return {.name = "hide", .start = std::move(start), .passed = std::move(passed)};
}

// 5. Presenting again.
SelfTestStep restoreStep(SelfTest* test, const SharedState& state) {
    auto start = [test, state] {
        markStart(test->frame().viewport(), *state);
#if defined(_WIN32)
        test->frame().Iconize(false);
#else
        test->frame().Show();  // X11: no WM-independent iconify; Hide/Show
#endif
    };
    auto passed = [test, state](std::string& seen) {
        const Viewport& viewport = test->frame().viewport();
        seen = describe(viewport, *state);
        const std::optional<dvv::gpu::PresenterStats> stats = statsOf(viewport);
        return stats && stats->framesPresented > state->startFrames && !stats->hidden;
    };
    return {.name = "restore", .start = std::move(start), .passed = std::move(passed)};
}

// 6. One stream: its frame 0 is shown for the current epoch, and a frame is presented after the
// snapshot first showed it.
SelfTestStep openStreamStep(SelfTest* test, const SharedState& state) {
    auto start = [test, state] {
        markStart(test->frame().viewport(), *state);
        state->framesWhenShown.reset();
        Session* session = test->app().session();
        dvv::Result<dvv::Uri> uri = dvv::parseUri(SMOKE_STREAM);
        if (session == nullptr || !uri) {
            DVV_LOG_ERROR("rawreel-gui --selftest: cannot open {}", SMOKE_STREAM);
            return;
        }
        session->open(0, dvv::media::SourceRequest{.uri = std::move(*uri), .hint = {}});
    };
    auto passed = [test, state](std::string& seen) {
        const Viewport& viewport = test->frame().viewport();
        const Session* session = test->app().session();
        if (session == nullptr) {
            seen = "no session";
            return false;
        }
        const SessionStatus status = session->status();
        const DisplaySnapshot snapshot = viewport.display()->snapshot();
        const std::optional<dvv::gpu::PresenterStats> stats = statsOf(viewport);
        seen = std::format(
            "{} streams, shown frames: count {}, index {}, epoch {} (viewport epoch {}), {} frames "
            "presented when first shown; {}",
            status.streams, snapshot.frames.count, snapshot.frames.index, snapshot.frames.epoch,
            viewport.epoch(),
            state->framesWhenShown ? std::to_string(*state->framesWhenShown) : "none",
            describe(viewport, *state));
        const bool shown = status.streams == 1 && snapshot.frames.count == 1 &&
                           snapshot.frames.index == 0 && snapshot.frames.epoch == viewport.epoch();
        if (!shown || !stats) {
            return false;
        }
        if (!state->framesWhenShown) {
            state->framesWhenShown = stats->framesPresented;
            return false;
        }
        return stats->framesPresented > *state->framesWhenShown;
    };
    return {.name = "open stream", .start = std::move(start), .passed = std::move(passed)};
}

// 8. The --appearance request took effect (Failure and CannotChange are not errors, §7).
SelfTestStep appearanceStep(SelfTest* test) {
    auto passed = [test](std::string& seen) {
        const Appearance requested = App::arguments().appearance;
        if (requested == Appearance::System) {
            seen = "the system appearance";
            return true;
        }
        const std::optional<wxApp::AppearanceResult> result = test->app().appearanceResult();
        if (!result) {
            seen = "no appearance result";
            return false;
        }
        const bool ok = *result == wxApp::AppearanceResult::Ok;
        const bool dark = wxSystemSettings::GetAppearance().IsDark();
        seen = std::format("requested dark {}, SetAppearance Ok {}, dark now {}",
                           requested == Appearance::Dark, ok, dark);
#if defined(_WIN32)
        if (ok) {
            return dark == (requested == Appearance::Dark);
        }
#endif
        // GTK: SetAppearance switches the GTK theme variant, but whether the theme has a dark
        // variant and what wxSystemSettings then reports depend on the installed theme, so only
        // the request is checked there (lenient by design).
        return true;
    };
    return {.name = "appearance", .start = {}, .passed = std::move(passed)};
}

// The shell (design D18): the Info and Log panes are shown, the player panel is the centre pane
// and holds the viewport.
SelfTestStep panesStep(SelfTest* test) {
    auto passed = [test](std::string& seen) {
        MainFrame& frame = test->frame();
        const wxAuiPaneInfo& info = frame.aui().GetPane("info");
        const wxAuiPaneInfo& log = frame.aui().GetPane("log");
        const wxAuiPaneInfo& player = frame.aui().GetPane("player");
        const bool centre = player.IsOk() && player.dock_direction == wxAUI_DOCK_CENTER;
        const bool child = frame.viewport().GetParent() == &frame.playerPanel();
        seen = std::format(
            "info shown {}, log shown {}, player pane {}, centre {}, viewport in the "
            "player panel {}",
            info.IsOk() && info.IsShown(), log.IsOk() && log.IsShown(), player.IsOk(), centre,
            child);
        return info.IsOk() && info.IsShown() && log.IsOk() && log.IsShown() && centre && child;
    };
    return step("panes", {}, std::move(passed));
}

// Play/Pause through MainFrame::execute (the menu path): playing reaches frame 3.
SelfTestStep menuTransportStep(SelfTest* test) {
    auto start = [test] { test->frame().execute(Command::PlayPause); };
    auto passed = [test](std::string& seen) {
        const SessionStatus status = test->frame().session().status();
        seen =
            std::format("playing {}, position {}", status.player.playing, status.player.position);
        return status.player.position >= 3;
    };
    return step("menu transport", std::move(start), std::move(passed));
}

SelfTestStep pauseStep(SelfTest* test) {
    auto start = [test] { test->frame().execute(Command::PlayPause); };
    auto passed = [test](std::string& seen) {
        const SessionStatus status = test->frame().session().status();
        seen =
            std::format("playing {}, position {}", status.player.playing, status.player.position);
        return !status.player.playing;
    };
    return step("pause", std::move(start), std::move(passed));
}

// D20: the position and rate fields.
SelfTestStep statusBarStep(SelfTest* test) {
    auto passed = [test](std::string& seen) {
        const std::array<wxString, 4> texts = test->frame().statusTexts();
        seen = std::format("status bar '{}' '{}' '{}' '{}'", texts[0].utf8_string(),
                           texts[1].utf8_string(), texts[2].utf8_string(), texts[3].utf8_string());
        // D5R §2.2: an SDR stream's mapping field.
        return texts[0].StartsWith("frame ") && texts[1].EndsWith("fps") &&
               texts[3] == "SDR, no tone mapping";
    };
    return step("status bar", {}, std::move(passed));
}

// Help -> Diagnostics writes the store counters to the log, which reaches the Log pane.
SelfTestStep diagnosticsStep(SelfTest* test) {
    auto start = [test] { test->frame().execute(Command::Diagnostics); };
    auto passed = [test](std::string& seen) {
        const bool found = test->frame().logPanel().text().Contains("store: frames");
        seen = std::format("'store: frames' in the Log pane {}", found);
        return found;
    };
    return step("diagnostics", std::move(start), std::move(passed));
}

// The second line of the GPU info JSON (its first key), trimmed; "" without a context.
std::string firstGpuInfoLine(const MainFrame& frame) {
    const std::shared_ptr<dvv::gpu::ExecutionContext> context = frame.currentContext().lock();
    if (!context) {
        return {};
    }
    const std::string text = dvv::json::dump(dvv::gpu::toJson(context->deviceInfo()), 2);
    const std::size_t first = text.find('\n');
    if (first == std::string::npos) {
        return {};
    }
    std::string line = text.substr(first + 1, text.find('\n', first + 1) - first - 1);
    const std::size_t begin = line.find_first_not_of(" \t\r");
    const std::size_t end = line.find_last_not_of(" \t\r");
    return begin == std::string::npos ? std::string() : line.substr(begin, end - begin + 1);
}

// Design §8.4 "menus" and "Help items": the menu bar from commandTable() and Help -> GPU info.
SelfTestStep menusStep(SelfTest* test, const SharedState& state) {
    auto start = [test, state] {
        state->logLength = test->frame().logPanel().text().length();
        test->frame().execute(Command::GpuInfo);  // selftest: to the log, no dialog
    };
    auto passed = [test, state](std::string& seen) {
        const MainFrame& frame = test->frame();
        const wxMenuBar* bar = frame.GetMenuBar();
        if (bar == nullptr) {
            seen = "no menu bar";
            return false;
        }
        const std::array<wxString, 4> names{"File", "Playback", "View", "Help"};
        bool menus = bar->GetMenuCount() == names.size();
        for (std::size_t i = 0; menus && i < names.size(); ++i) {
            menus = bar->GetMenuLabelText(i) == names[i];
        }
        const CommandInfo& open = commandTable()[static_cast<std::size_t>(Command::Open)];
        const wxMenuItem* open_item =
            bar->FindItem(COMMAND_ID_BASE + static_cast<int>(Command::Open));
        const bool open_label = open_item != nullptr &&
                                open_item->GetItemLabel() == wxString::FromUTF8(menuLabel(open));
        const bool reopen_in_view =
            menus && bar->GetMenu(2)->FindItem(COMMAND_ID_BASE +
                                               static_cast<int>(Command::Reopen)) != nullptr;
        const wxString log = frame.logPanel().text();
        const std::string line = firstGpuInfoLine(frame);
        const bool gpu_info = log.length() > state->logLength && !line.empty() &&
                              log.Contains(wxString::FromUTF8(line));
        seen = std::format(
            "{} menus in order {}, Open label '{}' ok {}, Reopen in View {}, GPU "
            "info line '{}' in the Log pane {}",
            bar->GetMenuCount(), menus,
            open_item != nullptr ? open_item->GetItemLabel().utf8_string() : "-", open_label,
            reopen_in_view, line, gpu_info);
        return menus && open_label && reopen_in_view && gpu_info;
    };
    return step("menus", std::move(start), std::move(passed));
}

// Design §7: a Session error reaches the infobar; slot 1 is refused.
SelfTestStep infobarStep(SelfTest* test) {
    auto start = [test] {
        dvv::Result<dvv::Uri> uri = dvv::parseUri(REFUSED_STREAM);
        if (!uri) {
            DVV_LOG_ERROR("rawreel-gui --selftest: cannot open {}", REFUSED_STREAM);
            return;
        }
        test->frame().session().open(1,
                                     dvv::media::SourceRequest{.uri = std::move(*uri), .hint = {}});
    };
    auto passed = [test](std::string& seen) {
        MainFrame& frame = test->frame();
        const bool shown = frame.playerPanel().infoBar().IsShown();
        const bool refused = frame.lastMessage().Contains("one stream at a time");
        seen =
            std::format("infobar shown {}, message '{}'", shown, frame.lastMessage().utf8_string());
        if (!shown || !refused) {
            return false;
        }
        frame.dismissMessage();
        const std::uint32_t streams = frame.session().status().streams;
        seen = std::format("dismissed: infobar shown {}, {} streams",
                           frame.playerPanel().infoBar().IsShown(), streams);
        return !frame.playerPanel().infoBar().IsShown() && streams == 1;
    };
    return step("infobar", std::move(start), std::move(passed));
}

// The first wxCollapsiblePane under `window` (depth first), or nullptr.
wxCollapsiblePane* findCollapsiblePane(wxWindow& window) {
    for (wxWindow* child : window.GetChildren()) {
        if (auto* pane = dynamic_cast<wxCollapsiblePane*>(child)) {
            return pane;
        }
        if (wxCollapsiblePane* pane = findCollapsiblePane(*child)) {
            return pane;
        }
    }
    return nullptr;
}

// The URLs of the wxHyperlinkCtrls under `window` (depth first).
void collectLinks(wxWindow& window, std::vector<std::string>& urls) {
    for (wxWindow* child : window.GetChildren()) {
        if (const auto* link = dynamic_cast<wxHyperlinkCtrl*>(child)) {
            urls.push_back(link->GetURL().utf8_string());
        }
        collectLinks(*child, urls);
    }
}

// D29: Help -> About through its hook (the hook closes the dialog).
SelfTestStep aboutStep(SelfTest* test, const std::shared_ptr<SmokeState>& state) {
    auto start = [test, state] {
        dialogHooks().about = [test, state](wxGenericAboutDialog& dialog) {
            state->aboutHookRan = true;
            state->aboutParentOk = dialog.GetParent() == &test->frame();
            // The About layout: the notices and copy buttons are in the dialog.
            state->aboutButtonsOk =
                wxWindow::FindWindowByLabel(
                    wxString::FromUTF8(ABOUT_NOTICES_LABEL.data(), ABOUT_NOTICES_LABEL.size()),
                    &dialog) != nullptr &&
                wxWindow::FindWindowByLabel(
                    wxString::FromUTF8(ABOUT_COPY_LABEL.data(), ABOUT_COPY_LABEL.size()),
                    &dialog) != nullptr;
            state->aboutLinks.clear();
            collectLinks(dialog, state->aboutLinks);
            // Opening the Components pane must not widen the dialog. Open, closed and open again,
            // the list must show at its full height (a window manager resizes asynchronously, so
            // each state gets 0.3 s to settle, as a user's clicks would).
            if (auto* pane = findCollapsiblePane(dialog)) {
                const auto wait = [] {
                    for (int i = 0; i < 30; ++i) {
                        wxTheApp->Yield(true);
                        wxMilliSleep(10);
                    }
                };
                // What a click does: the new state, then the changed event.
                const auto click = [pane, &wait](bool open) {
                    pane->Collapse(!open);
                    wxCollapsiblePaneEvent changed(pane, pane->GetId(), !open);
                    pane->GetEventHandler()->ProcessEvent(changed);
                    wait();
                };
                // On screen first (about 1 s); the size it was shown with must still be its size.
                for (int i = 0; i < 3; ++i) {
                    wait();
                }
                AboutShowLog& log = aboutShowLog();
                log.recording = false;
                state->aboutShows = log.shows;
                state->aboutSizeChanges = log.sizeChanges;
                state->aboutClosedEarly = log.closed;
                state->aboutAtShow = log.atShow;
                state->aboutAtHook = dialog.GetSize();
                state->aboutWidth = dialog.GetSize().x;
                click(true);
                state->aboutWidthExpanded = std::max(dialog.GetSize().x, dialog.GetBestSize().x);
                state->aboutHeightOpen = dialog.GetClientSize().y;
                click(false);
                state->aboutHeightClosed = dialog.GetClientSize().y;
                click(true);
                state->aboutHeightReopen = dialog.GetClientSize().y;
                const wxWindowList& inner = pane->GetPane()->GetChildren();
                if (!inner.empty()) {
                    state->aboutListReopen = inner.front()->GetSize();
                    state->aboutListMin = inner.front()->GetMinSize().y;
                    state->aboutListShown = inner.front()->IsShownOnScreen();
                }
            }
        };
        state->about0 = test->frame().aboutShown();
        test->frame().execute(Command::About);
    };
    auto passed = [test, state](std::string& seen) {
        const MainFrame& frame = test->frame();
        const std::string& text = frame.lastAboutText();
        const VulkanStatus vulkan = frame.vulkanStatus();
        const dvv::components::FfmpegStatus& ffmpeg = test->app().startup().ffmpeg;
        // The status only: no FFmpeg directory in About (users share its text).
        const bool no_path =
            !ffmpeg.core || !text.contains(dvv::media::utf8FromPath(ffmpeg.directory));
        // About: the name and version once, the licence, the FFmpeg version, the components, no
        // GPU (Help -> GPU info); the copied text is the full aboutText().
        const std::string heading = std::format("rawreel {}", packageVersions().front().second);
        const bool lines =
            text.find(heading) != std::string::npos && text.find(heading) == text.rfind(heading) &&
            text.contains("MIT licence") && vulkan.available && !text.contains(vulkan.device) &&
            !text.contains("Vulkan") && (!ffmpeg.core || text.contains(ffmpeg.version)) &&
            text.contains("dvv-base ") && no_path;
        // Every link of the About content is a link in the dialog.
        bool links = true;
        const AboutContent content =
            aboutContent({.packages = packageVersions(),
                          .vulkan = {},
                          .ffmpeg = ffmpegStatusLine(ffmpeg, StatusPaths::Hidden)});
        std::vector<TextLine> all = content.system;
        all.push_back(content.copyright);
        all.push_back({{"", content.website}});
        for (const TextLine& line : all) {
            for (const TextPart& part : line) {
                links = links &&
                        (part.url.empty() ||
                         std::ranges::find(state->aboutLinks, part.url) != state->aboutLinks.end());
            }
        }
        // Only the checked facts, not the text (it names the FFmpeg version and the device).
        seen = std::format(
            "hook ran {}, parent the frame {}, buttons {}, About shown {} ({} at the step start), "
            "lines {}, links {} ({} found), no FFmpeg path {}, width {} -> {} with Components "
            "open, height {} -> {} "
            "closed -> {} open again, list {}x{} (min height {}) shown {}; at open: shown {} "
            "time(s), "
            "hidden early {}, {}x{} when shown -> {}x{} after about 1 s, {} size change(s)",
            state->aboutHookRan, state->aboutParentOk, state->aboutButtonsOk, frame.aboutShown(),
            state->about0, lines, links, state->aboutLinks.size(), no_path, state->aboutWidth,
            state->aboutWidthExpanded, state->aboutHeightOpen, state->aboutHeightClosed,
            state->aboutHeightReopen, state->aboutListReopen.x, state->aboutListReopen.y,
            state->aboutListMin, state->aboutListShown, state->aboutShows, state->aboutClosedEarly,
            state->aboutAtShow.x, state->aboutAtShow.y, state->aboutAtHook.x, state->aboutAtHook.y,
            state->aboutSizeChanges);
        // No flash at open: shown once, at its final size, and not resized or hidden after.
        const bool steady_open = state->aboutShows == 1 && !state->aboutClosedEarly &&
                                 state->aboutSizeChanges == 0 &&
                                 state->aboutAtShow == state->aboutAtHook;
        const bool width_kept =
            state->aboutWidth > 0 && state->aboutWidthExpanded <= state->aboutWidth;
        const bool list_again =
            state->aboutHeightOpen > 0 && state->aboutHeightReopen >= state->aboutHeightOpen &&
            state->aboutListReopen.x > 0 && state->aboutListMin > 0 &&
            state->aboutListReopen.y >= state->aboutListMin && state->aboutListShown;
        return state->aboutHookRan && state->aboutParentOk && state->aboutButtonsOk &&
               frame.aboutShown() > state->about0 && lines && links && width_kept && list_again &&
               steady_open;
    };
    return step("about", std::move(start), std::move(passed));
}

// D29: a lavfi: URI through File -> Open URI... and its hook.
SelfTestStep lavfiUriStep(SelfTest* test, const std::shared_ptr<SmokeState>& state) {
    auto start = [test, state] {
        MainFrame& frame = test->frame();
        state->presented0 = presented(frame.viewport());
        dialogHooks().uri = [](wxTextEntryDialog& dialog) {
            dialog.SetValue("lavfi:testsrc2=s=64x48:r=25,format=yuv420p");
        };
        frame.execute(Command::OpenUri);
    };
    auto passed = [test, state](std::string& seen) {
        MainFrame& frame = test->frame();
        const auto& streams = frame.streams();
        const bool lavfi =
            streams.size() == 1 && streams[0].resolvedUri.starts_with("lavfi:testsrc2=s=64x48");
        const DisplaySnapshot snapshot = frame.viewport().display()->snapshot();
        const std::uint64_t now = presented(frame.viewport());
        seen = std::format("{} streams, URI '{}', shown count {}, presented {} ({} at the start)",
                           streams.size(), streams.empty() ? "-" : streams[0].resolvedUri,
                           snapshot.frames.count, now, state->presented0);
        return lavfi && snapshot.frames.count == 1 && now > state->presented0;
    };
    return step("lavfi through Open URI", std::move(start), std::move(passed));
}

// D28: View -> Format... on a lavfi: stream explains the graph instead of the dialog.
SelfTestStep lavfiFormatStep(SelfTest* test, const std::shared_ptr<SmokeState>& state) {
    auto start = [test, state] {
        MainFrame& frame = test->frame();
        state->streamUri = frame.streams().empty() ? std::string{} : frame.streams()[0].resolvedUri;
        state->dialogs0 = frame.formatDialogsShown();
        frame.execute(Command::Format);
    };
    auto passed = [test, state](std::string& seen) {
        const MainFrame& frame = test->frame();
        const auto& streams = frame.streams();
        const bool message = frame.lastMessage().Contains("setparams");
        const bool same = streams.size() == 1 && streams[0].resolvedUri == state->streamUri;
        seen = std::format("message '{}', same stream {}, format dialogs {} ({} at the start)",
                           frame.lastMessage().utf8_string(), same, frame.formatDialogsShown(),
                           state->dialogs0);
        return message && same && frame.formatDialogsShown() == state->dialogs0;
    };
    return step("Format... on a lavfi stream", std::move(start), std::move(passed));
}

}  // namespace

// M4 GUI D1, D2, D3 through MainFrame::openUri and the commands. A relative file
// name that does not exist (no machine path).
constexpr const char* MISSING_FILE = "rawreel-selftest-missing_64x64.yuv";

void addOpenUriSteps(std::vector<SelfTestStep>& steps, SelfTest* test,
                     const std::shared_ptr<SmokeState>& state) {
    // D1: a cancelled Open URI... leaves one Log line (no hook: the selftest cancels it).
    steps.push_back(step(
        "Open URI cancel logs it",
        [test, state] {
            state->logLength = test->frame().logPanel().text().length();
            test->frame().execute(Command::OpenUri);
        },
        [test, state](std::string& seen) {
            const wxString log = test->frame().logPanel().text();
            const bool logged = log.length() > state->logLength &&
                                log.Mid(state->logLength).Contains("open cancelled");
            seen = std::format("'open cancelled' logged {}", logged);
            return logged;
        }));
    // D3: the open closes the shown stream first; the failed open leaves none, with its error.
    steps.push_back(step(
        "a failed open leaves no stream",
        [test, state] {
            // Precondition: a stream is shown, so close-first has one to close.
            state->streamsBefore = test->frame().streams().size();
            test->frame().openUri(0, MISSING_FILE, std::nullopt);
        },
        [test, state](std::string& seen) {
            MainFrame& frame = test->frame();
            const bool shown = frame.playerPanel().infoBar().IsShown();
            const bool named = frame.lastMessage().Contains(MISSING_FILE);
            const std::uint32_t streams = frame.session().status().streams;
            seen = std::format(
                "{} streams before, {} frame streams, {} session streams, infobar shown {}, "
                "message '{}'",
                state->streamsBefore, frame.streams().size(), streams, shown,
                frame.lastMessage().utf8_string());
            return state->streamsBefore == 1 && frame.streams().empty() && streams == 0 && shown &&
                   named;
        }));
    // D2: the next good open ends the open error.
    steps.push_back(step(
        "a good open clears the error",
        [test] { test->frame().openUri(0, std::string(SMOKE_STREAM), {}); },
        [test](std::string& seen) {
            MainFrame& frame = test->frame();
            const bool shown = frame.playerPanel().infoBar().IsShown();
            seen = std::format("{} streams, infobar shown {}, message '{}'", frame.streams().size(),
                               shown, frame.lastMessage().utf8_string());
            return frame.streams().size() == 1 && !shown;
        }));
    // D2: Close stream ends a message shown with the stream (the slot 1 refusal).
    steps.push_back(step(
        "a refusal is shown with the stream",
        [test] {
            dvv::Result<dvv::Uri> uri = dvv::parseUri(REFUSED_STREAM);
            if (uri) {
                test->frame().session().open(
                    1, dvv::media::SourceRequest{.uri = std::move(*uri), .hint = {}});
            }
        },
        [test](std::string& seen) {
            MainFrame& frame = test->frame();
            const bool shown = frame.playerPanel().infoBar().IsShown();
            seen = std::format("{} streams, infobar shown {}, message '{}'", frame.streams().size(),
                               shown, frame.lastMessage().utf8_string());
            return frame.streams().size() == 1 && shown &&
                   frame.lastMessage().Contains("one stream at a time");
        }));
    steps.push_back(step(
        "Close stream clears the message", [test] { test->frame().execute(Command::CloseStreams); },
        [test](std::string& seen) {
            MainFrame& frame = test->frame();
            const bool shown = frame.playerPanel().infoBar().IsShown();
            seen = std::format("{} streams, infobar shown {}", frame.streams().size(), shown);
            return frame.streams().empty() && frame.session().status().streams == 0 && !shown;
        }));
    steps.push_back(step(
        "the smoke stream again",
        [test] { test->frame().openUri(0, std::string(SMOKE_STREAM), {}); },
        [test](std::string& seen) {
            seen = std::format("{} streams", test->frame().streams().size());
            return test->frame().streams().size() == 1;
        }));
}

std::vector<SelfTestStep> smokeScenario(SelfTest& test) {
    SelfTest* const t = &test;
    const auto state = std::make_shared<SmokeState>();
    std::vector<SelfTestStep> steps;
    steps.push_back(firstFramesStep(t, state));
    steps.push_back(resizeStep(t, state, "resize 800x600", wxSize(800, 600)));
    steps.push_back(shrinkStep(t, state));
    steps.push_back(movedPaneStep(t, state, false));
    steps.push_back(movedPaneStep(t, state, true));
    steps.push_back(hideStep(t, state));
    steps.push_back(restoreStep(t, state));
    steps.push_back(openStreamStep(t, state));
    steps.push_back(resizeStep(t, state, "resize with a stream", wxSize(1024, 640)));
    steps.push_back(panesStep(t));
    steps.push_back(menuTransportStep(t));
    steps.push_back(pauseStep(t));
    steps.push_back(statusBarStep(t));
    steps.push_back(diagnosticsStep(t));
    steps.push_back(menusStep(t, state));
    steps.push_back(infobarStep(t));
    steps.push_back(aboutStep(t, state));
    if (test.app().startup().ffmpeg.lavfi) {
        steps.push_back(lavfiUriStep(t, state));
        steps.push_back(lavfiFormatStep(t, state));
    } else if (ffmpegRequired()) {
        steps.push_back(ffmpegRequiredStep(ffmpegStatusLine(test.app().startup().ffmpeg)));
    } else {
        DVV_LOG_INFO("selftest: FFmpeg not available, lavfi steps skipped");
    }
    addOpenUriSteps(steps, t, state);
    steps.push_back(appearanceStep(t));
    return steps;
}

// --- lost (design v2 §8.4 lost, D7, D25, E3) -----------------------------------------------------

std::vector<SelfTestStep> lostScenario(SelfTest& test) {
    MainFrame& frame = test.frame();
    ShowHistory& history = *test.app().showHistory();
    struct State {
        std::size_t mark = 0;    // history.size() when the step started
        std::int64_t kept = -1;  // the position at the first loss
        std::optional<std::uint64_t> presentedAtShow;
        std::uint64_t presentedAtStart = 0;  // play continues: framesPresented at its start
        bool presenterAtInject = false;      // a presenter was running when the loss was injected
        Clock::time_point lastPoll{};
        std::int64_t longestGapMs = 0;  // between two polls since the injection
        std::uint32_t polls = 0;
    };
    auto st = std::make_shared<State>();
    auto status = [&frame] { return frame.session().status(); };

    // Injects a loss into the current stack's context. The strong reference lives only inside this
    // function, on the GUI thread (allowed by the §5.3 release rule).
    auto inject = [&frame, &history, st] {
        st->mark = history.size();
        st->lastPoll = Clock::now();
        st->longestGapMs = 0;
        st->polls = 0;
        st->presenterAtInject = frame.viewport().presenter() != nullptr;
        if (const std::shared_ptr<dvv::gpu::ExecutionContext> context =
                frame.currentContext().lock()) {
            dvv::gpu::testing::injectDeviceLost(*context);
        }
    };
    // The loss is reported and the GUI thread kept ticking (R10).
    auto reported = [&frame, status, st](std::string& seen) {
        const Clock::time_point now = Clock::now();
        st->longestGapMs = std::max<std::int64_t>(
            st->longestGapMs,
            std::chrono::duration_cast<std::chrono::milliseconds>(now - st->lastPoll).count());
        st->lastPoll = now;
        ++st->polls;
        const SessionStatus s = status();
        const bool presenter = frame.viewport().presenter() != nullptr;
        seen = std::format(
            "presenter at the injection {}, reopen offered {}, gpuAvailable {}, presenter {}, "
            "polls {}, longest poll gap {} ms",
            st->presenterAtInject, frame.reopenShown(), s.gpuAvailable, presenter, st->polls,
            st->longestGapMs);
        // The loss itself must remove the presenter: one must have run when it was injected.
        return st->presenterAtInject && frame.reopenShown() && !s.gpuAvailable && !presenter &&
               st->polls >= 10 && st->longestGapMs < 1000;
    };
    // Clicks the infobar's Reopen button (-> execute(Command::Reopen)).
    auto click_reopen = [&frame] {
        wxInfoBarGeneric& bar = frame.playerPanel().infoBar();
        wxCommandEvent click(wxEVT_BUTTON, INFOBAR_REOPEN_ID);
        click.SetEventObject(&bar);
        bar.GetEventHandler()->ProcessEvent(click);
    };
    std::vector<SelfTestStep> steps;

    steps.push_back(step(
        "play with loop",
        [&frame] {
            frame.execute(Command::Loop);
            frame.execute(Command::PlayPause);
        },
        [&frame, status](std::string& seen) {
            const SessionStatus s = status();
            seen = std::format("streams {}, epoch {}, playing {}, position {}, presented {}",
                               s.streams, s.epoch, s.player.playing, s.player.position,
                               presented(frame.viewport()));
            return s.streams == 1 && s.epoch == 1 && s.player.playing && s.player.position >= 3 &&
                   presented(frame.viewport()) >= 5;
        }));
    steps.push_back(step("loss offers Reopen", inject, [&history, reported, st](std::string& seen) {
        if (!reported(seen)) {
            return false;
        }
        const std::vector<ShownSet> shows = history.since(0);
        const auto last = std::ranges::find_if(shows.rbegin(), shows.rend(),
                                               [](const ShownSet& s) { return s.epoch == 1; });
        if (last == shows.rend()) {
            seen = "no show of epoch 1";
            return false;
        }
        st->kept = last->index;
        return true;
    }));
    steps.push_back(step(
        "Reopen presents the kept frame",
        [&history, click_reopen, st] {
            st->mark = history.size();
            st->presentedAtShow.reset();
            click_reopen();
        },
        [&frame, &history, status, st](std::string& seen) {
            const SessionStatus s = status();
            const std::vector<ShownSet> shows = history.since(st->mark);
            const bool shown = std::ranges::any_of(shows, [st](const ShownSet& set) {
                return set.epoch == 2 && set.index == st->kept && set.count == 1 && set.viewsMatch;
            });
            const std::optional<RecordedInfo> recorded = frame.viewport().lastRecorded();
            seen = std::format(
                "epoch {}, gpu {}, playing {}, position {} (kept {}), reopen shown {}, show of "
                "epoch 2 {}, recorded {}/{}",
                s.epoch, s.gpuAvailable, s.player.playing, s.player.position, st->kept,
                frame.reopenShown(), shown, recorded ? std::to_string(recorded->epoch) : "-",
                recorded ? std::to_string(recorded->index) : "-");
            if (s.epoch != 2 || !s.gpuAvailable || s.player.playing ||
                s.player.position != st->kept || frame.reopenShown() || !shown || !recorded ||
                recorded->epoch != 2 || recorded->index != st->kept) {
                return false;
            }
            const std::uint64_t now = presented(frame.viewport());
            if (!st->presentedAtShow) {
                st->presentedAtShow = now;
                seen = "waiting for a present after the recording";
                return false;
            }
            seen = std::format("framesPresented {} (at the show {})", now, *st->presentedAtShow);
            return now > *st->presentedAtShow;
        }));
    steps.push_back(step(
        "play continues",
        [&frame, &history, st] {
            st->mark = history.size();
            st->presentedAtStart = presented(frame.viewport());
            frame.execute(Command::PlayPause);
        },
        [&frame, &history, status, st](std::string& seen) {
            const SessionStatus s = status();
            const std::vector<ShownSet> shows = history.since(st->mark);
            const bool epoch2 = std::ranges::all_of(shows, [](const ShownSet& set) {
                return set.epoch == 2 && set.count == 1 && set.viewsMatch;
            });
            // The runner ignores presenter failures in this scenario, so check the epoch-2
            // presenter here: alive, not failed, and presenting the new shows.
            const Viewport& viewport = frame.viewport();
            const bool presenter = viewport.presenter() != nullptr;
            const bool failed = viewport.lastFailure().has_value();
            const std::uint64_t now = presented(frame.viewport());
            const std::uint64_t added = now > st->presentedAtStart ? now - st->presentedAtStart : 0;
            seen = std::format(
                "playing {}, position {} (kept {}), shows {}, all of epoch 2 {}, presenter {} "
                "(epoch {}, failed {}), new presents {}",
                s.player.playing, s.player.position, st->kept, shows.size(), epoch2, presenter,
                viewport.epoch(), failed, added);
            return s.player.playing && s.player.position != st->kept && shows.size() >= 3 &&
                   epoch2 && presenter && viewport.epoch() == 2 && !failed && added >= 3;
        }));
    steps.push_back(step("second loss offers Reopen again", inject, reported));
    // A Reopen whose presenter fails to start reports it during stackChanged (D25, synchronous);
    // the frame must keep that Reopen offer (it dismisses an old offer before setStack).
    steps.push_back(step(
        "a failed presenter start keeps Reopen",
        [&frame, click_reopen] {
            frame.viewport().failNextStartForTest();
            click_reopen();
        },
        [&frame, status](std::string& seen) {
            const SessionStatus s = status();
            const Viewport& viewport = frame.viewport();
            const bool presenter = viewport.presenter() != nullptr;
            const std::optional<dvv::Error>& failed = viewport.lastFailure();
            seen = std::format(
                "epoch {}, gpu {}, viewport epoch {}, presenter {}, failure {}, reopen shown {}",
                s.epoch, s.gpuAvailable, viewport.epoch(), presenter,
                failed ? failed->describe() : std::string("-"), frame.reopenShown());
            return s.epoch == 3 && s.gpuAvailable && viewport.epoch() == 3 && !presenter &&
                   failed && failed->code == dvv::Errc::Internal && frame.reopenShown();
        }));
    return steps;  // the runner's close step: validation 0, leak check (§8.4 lost)
}

// --- dialog hooks (design v2 §8.4) ---------------------------------------------------------------

DialogHooks& dialogHooks() {
    static DialogHooks hooks;  // GUI thread only
    return hooks;
}

namespace {

// One body for the three dialogs (format, Open URI, About).
template <class Dialog>
int showModalWith(Dialog& dialog, std::move_only_function<void(Dialog&)>& slot,
                  std::string_view kind) {
    if (slot) {
        // One shot; CallAfter needs a copyable functor, so the hook is shared.
        auto hook = std::make_shared<std::move_only_function<void(Dialog&)>>(std::move(slot));
        slot = nullptr;
        dialog.CallAfter([&dialog, hook] {
            (*hook)(dialog);
            dialog.EndModal(wxID_OK);
        });
    } else if (App::arguments().selftest != Scenario::None) {
        // A dialog the script did not expect: cancel it, so the run never waits for a user.
        dialog.CallAfter([&dialog, kind] {
            DVV_LOG_WARN("rawreel-gui --selftest: {} dialog without a hook was cancelled", kind);
            dialog.EndModal(wxID_CANCEL);
        });
    }
    return dialog.ShowModal();
}

}  // namespace

int showModal(FormatDialog& dialog) {
    return showModalWith(dialog, dialogHooks().format, "a format");
}
int showModal(wxTextEntryDialog& dialog) {
    return showModalWith(dialog, dialogHooks().uri, "an Open URI");
}
int showModal(wxGenericAboutDialog& dialog) {
    if (dialogHooks().about) {
        // The dialog is already at its final size here (the hook checks that it stays so).
        AboutShowLog& start = aboutShowLog();
        start = AboutShowLog{};
        start.last = dialog.GetSize();
        start.recording = true;
        dialog.Bind(wxEVT_SHOW, [&dialog](wxShowEvent& event) {
            event.Skip();
            AboutShowLog& log = aboutShowLog();
            if (!log.recording) {
                return;
            }
            if (!event.IsShown()) {
                log.closed = true;
            } else if (++log.shows == 1) {
                log.atShow = dialog.GetSize();
            }
        });
        dialog.Bind(wxEVT_SIZE, [](wxSizeEvent& event) {
            event.Skip();
            AboutShowLog& log = aboutShowLog();
            if (log.recording && log.shows > 0 && event.GetSize() != log.last) {
                ++log.sizeChanges;
            }
            if (log.recording) {
                log.last = event.GetSize();
            }
        });
    }
    return showModalWith(dialog, dialogHooks().about, "an About");
}

// --- player (design v2 §8.4 player) -------------------------------------------------------------

namespace {

// Through the frame's wxEVT_CHAR_HOOK handler, as a real key press reaches it first.
void sendKey(wxFrame& frame, int keycode, bool shift = false) {
    wxKeyEvent event(wxEVT_CHAR_HOOK);
    event.SetEventObject(&frame);
    event.m_keyCode = keycode;
    if (keycode > 0 && keycode < 128) {
        event.m_uniChar = static_cast<wxChar>(keycode);
    }
    event.SetShiftDown(shift);
    injectInput(*frame.GetEventHandler(), event);
}

void sendSliderTrack(wxSlider& slider, int value) {
    slider.SetValue(value);
    wxScrollEvent event(wxEVT_SCROLL_THUMBTRACK, slider.GetId(), value, wxHORIZONTAL);
    event.SetEventObject(&slider);
    slider.GetEventHandler()->ProcessEvent(event);
}

// Drops `files` on the viewport's DropTarget (the wx callback a real drop calls).
void dropFiles(Viewport& viewport, const std::vector<std::filesystem::path>& files) {
    auto* target = static_cast<DropTarget*>(viewport.GetDropTarget());
    const wxSize size = viewport.GetClientSize();
    wxArrayString names;
    for (const auto& file : files) {
        names.Add(wxString::FromUTF8(dvv::media::utf8FromPath(file)));
    }
    target->OnDropFiles(size.x / 2, size.y / 2, names);
}

// `bytes` bytes of `value`: a raw file for the drop steps.
void writeFile(const std::filesystem::path& file, std::size_t bytes, char value) {
    std::ofstream(file, std::ios::binary)
        .write(std::string(bytes, value).data(), static_cast<std::streamsize>(bytes));
}

// Every set holds `count` views of its own index, and each index follows the previous one
// (0 after length - 1 when looping).
bool consecutive(std::span<const ShownSet> shows, std::uint32_t count,
                 std::optional<std::int64_t> wrap_length, std::string& seen) {
    for (std::size_t i = 0; i < shows.size(); ++i) {
        if (shows[i].count != count || !shows[i].viewsMatch) {
            seen = std::format("set {} (index {}) has {} views, match {}", i, shows[i].index,
                               shows[i].count, shows[i].viewsMatch);
            return false;
        }
        if (i == 0) {
            continue;
        }
        const std::int64_t expected =
            wrap_length && shows[i - 1].index == *wrap_length - 1 ? 0 : shows[i - 1].index + 1;
        if (shows[i].index != expected) {
            seen = std::format("index {} followed {} (expected {})", shows[i].index,
                               shows[i - 1].index, expected);
            return false;
        }
    }
    return true;
}

// The Info line of a colour field with no origin tag right after it (D5R §4.5), e.g.
// "matrix bt601,": neither " (default)" nor " (from file)" follows.
bool infoHas(MainFrame& frame, std::string_view field) {
    const std::string text = frame.infoPanel().text().utf8_string();
    const std::size_t at = text.find(field);
    if (at == std::string::npos) {
        return false;
    }
    const std::string_view rest = std::string_view(text).substr(at + field.size());
    return !rest.starts_with(" (default)") && !rest.starts_with(" (from file)");
}

}  // namespace

std::vector<SelfTestStep> playerScenario(SelfTest& test) {
    MainFrame& frame = test.frame();
    ShowHistory& history = *test.app().showHistory();
    struct State {
        std::uint64_t presented0 = 0;
        std::size_t mark = 0;
        std::int64_t position = 0;
        dvv::platform::PixelSize extent0{};
        std::filesystem::path dir;
        std::uint64_t dialogs0 = 0;
        bool formatHookRan = false;
        dvv::media::ColourParams colourBefore{};  // the stream's colour before View > Format
        ColourChoice colourRead{};                // the colour a format hook read
        FormatChoice choiceRead{};                // the choice a format hook read (D5 T5b)
        bool lockedRead = false;                  // FormatDialog::fieldsLocked() in a hook
        bool flagRead = false;                    // another yes/no a format hook read
        std::vector<std::string> textsRead;       // dialog texts a format hook read
        bool loopBefore = false;                  // loop wraps: the GUI's loop flag before the step
        std::uint64_t ignored0 = 0;  // real input is ignored: SelfTestInput's count before
        std::chrono::steady_clock::time_point at;  // real input is ignored: the step start
        State() = default;
        State(const State&) = delete;
        State& operator=(const State&) = delete;
        State(State&&) = delete;
        State& operator=(State&&) = delete;
        // A failed step (or "player without a URI") ends the script early: the scratch folder is
        // removed when the steps are destroyed as well (no-op after the last step removed it).
        ~State() {
            if (!dir.empty()) {
                std::error_code error;
                std::filesystem::remove_all(dir, error);
            }
        }
    };
    auto st = std::make_shared<State>();
    auto status = [&frame] { return frame.session().status().player; };
    auto stream_is = [&frame](std::uint32_t w, std::uint32_t h, std::string_view format) {
        const auto& streams = frame.streams();
        return streams.size() == 1 && streams[0].info.desc.width == w &&
               streams[0].info.desc.height == h &&
               dvv::media::descriptor(streams[0].info.desc.format).name == format;
    };
    // The resolved URI of the one stream; "" while none is open (a reopen).
    auto stream_uri = [&frame] {
        const auto& streams = frame.streams();
        return streams.size() == 1 ? streams[0].resolvedUri : std::string();
    };
    std::vector<SelfTestStep> steps;

    steps.push_back(step(
        "open one stream",
        [st] {
            st->dir = std::filesystem::temp_directory_path() /
                      std::format("rawreel-selftest-{}", wxGetProcessId());
            std::filesystem::create_directories(st->dir);
        },
        [&frame, status](std::string& seen) {
            seen = std::format("streams {}, presented {}, position {}", frame.streams().size(),
                               presented(frame.viewport()), status().position);
            return frame.streams().size() == 1 && presented(frame.viewport()) >= 2 &&
                   status().position == 0;
        }));
    steps.push_back(step(
        "play 30 frames",
        [&frame, &history, st] {
            st->presented0 = presented(frame.viewport());
            st->mark = history.size();
            frame.execute(Command::PlayPause);
        },
        [&frame, &history, st, status](std::string& seen) {
            seen = std::format("position {}", status().position);
            if (status().position < 30) {
                return false;
            }
            if (presented(frame.viewport()) <= st->presented0) {
                seen = "framesPresented did not grow";
                return false;
            }
            return consecutive(history.since(st->mark), 1, std::nullopt, seen);
        }));
    steps.push_back(step(
        "Space pauses (CHAR_HOOK)",
        [&frame, status] {
            if (status().playing) {
                sendKey(frame, WXK_SPACE);
            }
        },
        [status](std::string& seen) {
            seen = std::format("playing {}, position {}", status().playing, status().position);
            return !status().playing && status().position < 59;  // paused by the key, not the end
        }));
    steps.push_back(step(
        "step back",
        [&frame, st, status] {
            st->position = status().position;
            frame.execute(Command::StepBack);
        },
        [st, status](std::string& seen) {
            seen = std::format("position {} (expected {})", status().position, st->position - 1);
            return status().position == st->position - 1;
        }));
    steps.push_back(step(
        "Right steps forward (CHAR_HOOK)", [&frame] { sendKey(frame, WXK_RIGHT); },
        [st, status](std::string& seen) {
            seen = std::format("position {} (expected {})", status().position, st->position);
            return status().position == st->position;
        }));
    steps.push_back(step(
        "slider to 5", [&frame] { sendSliderTrack(frame.transportBar().slider(), 5); },
        [&history, status](std::string& seen) {
            const auto shows = history.since(0);
            seen = std::format("position {}", status().position);
            return status().position == 5 && !shows.empty() && shows.back().index == 5;
        }));
    steps.push_back(step(
        "Home (CHAR_HOOK)", [&frame] { sendKey(frame, WXK_HOME); },
        [status](std::string& seen) {
            seen = std::format("position {}", status().position);
            return status().position == 0;
        }));
    steps.push_back(step(
        "End (CHAR_HOOK)", [&frame] { sendKey(frame, WXK_END); },
        [status](std::string& seen) {
            seen = std::format("position {}", status().position);
            return status().position == 59;
        }));
    steps.push_back(step(
        "loop wraps",
        [&frame, &history, st, status] {
            // Set, not toggled; the state before is logged (evidence if an outside toggle recurs).
            st->loopBefore = frame.loop();
            DVV_LOG_INFO("rawreel-gui --selftest: loop before the loop step: GUI {}, Session {}",
                         st->loopBefore, status().loop);
            if (!frame.loop()) {
                frame.execute(Command::Loop);
            }
            st->mark = history.size();
            frame.execute(Command::PlayPause);
        },
        [&history, st, status](std::string& seen) {
            const auto shows = history.since(st->mark);
            const bool wrapped =
                std::ranges::any_of(shows, [](const ShownSet& s) { return s.index == 0; });
            seen = std::format("loop {} (GUI before the step {}), shows {}", status().loop,
                               st->loopBefore, shows.size());
            return status().loop && wrapped && consecutive(shows, 1, 60, seen);
        }));
    steps.push_back(step(
        "pause",
        [&frame] {
            frame.execute(Command::PlayPause);
            if (frame.loop()) {
                frame.execute(Command::Loop);  // loop off again
            }
        },
        [status](std::string& seen) {
            seen = std::format("playing {}, loop {}", status().playing, status().loop);
            return !status().playing && !status().loop;
        }));
    // Input isolation (SelfTestInput): events sent without the script's mark stand in for a real
    // L key, a menu click (Play/Pause) and a mouse click; none may change the state. The native
    // mapping (a real WM_* message to these wx events) is not covered: it needs OS-level input
    // injection into the user's desktop.
    steps.push_back(step(
        "real input is ignored",
        [&frame, st, t = &test] {
            st->ignored0 = t->inputIgnored();
            st->at = std::chrono::steady_clock::now();
            wxKeyEvent key(wxEVT_CHAR_HOOK);
            key.SetEventObject(&frame);
            key.m_keyCode = 'L';
            key.m_uniChar = 'L';
            frame.GetEventHandler()->ProcessEvent(key);
            wxCommandEvent menu(wxEVT_MENU, COMMAND_ID_BASE + static_cast<int>(Command::PlayPause));
            menu.SetEventObject(&frame);
            frame.GetEventHandler()->ProcessEvent(menu);
            wxMouseEvent click(wxEVT_LEFT_DOWN);
            click.SetEventObject(&frame.transportBar());
            frame.transportBar().GetEventHandler()->ProcessEvent(click);
        },
        [&frame, st, status, t = &test](std::string& seen) {
            const std::uint64_t ignored = t->inputIgnored() - st->ignored0;
            const bool settled =
                std::chrono::steady_clock::now() - st->at >= std::chrono::milliseconds(300);
            // At least 3: real input during the step (e.g. typing while the window has the focus)
            // is ignored and counted too.
            seen = std::format("ignored {} (at least 3), loop {} (GUI {}), playing {}", ignored,
                               status().loop, frame.loop(), status().playing);
            return settled && ignored >= 3 && !status().loop && !frame.loop() && !status().playing;
        }));
    steps.push_back(step(
        "resize keeps the fit",
        [&frame, st] {
            st->extent0 = frame.viewport().physicalClientSize();
            st->presented0 = presented(frame.viewport());
            // DIP-scaled and large enough that the centre pane stays non-empty next to the Info
            // and Log panes at 150 % scaling (a 400x300 client could collapse the viewport).
            frame.SetClientSize(frame.FromDIP(wxSize(900, 640)));
        },
        [&frame, st](std::string& seen) {
            const dvv::platform::PixelSize extent = frame.viewport().physicalClientSize();
            const std::optional<RecordedInfo> recorded = frame.viewport().lastRecorded();
            // The fit of the opened stream's size, so the scenario works with any source.
            dvv::platform::PixelSize image{};
            if (!frame.streams().empty()) {
                const dvv::media::FrameDesc& desc = frame.streams()[0].info.desc;
                image = {desc.width, desc.height};
            }
            const double fit = ViewTransform::fit(extent, image).zoom();
            seen = std::format(
                "extent {}x{} (was {}x{}), image {}x{}, recorded {}x{} zoom {} (fit {})",
                extent.width, extent.height, st->extent0.width, st->extent0.height, image.width,
                image.height, recorded ? recorded->extent.width : 0,
                recorded ? recorded->extent.height : 0, recorded ? recorded->zoom : 0.0, fit);
            return !(extent == st->extent0) && extent.width > 0 && extent.height > 0 &&
                   image.width > 0 && image.height > 0 && recorded && recorded->extent == extent &&
                   recorded->zoom == fit && presented(frame.viewport()) > st->presented0;
        }));
    steps.push_back(step(
        "Shift+Right steps 10 (CHAR_HOOK)",
        [&frame, st, status] {
            st->position = std::min<std::int64_t>(status().position + 10, 59);
            sendKey(frame, WXK_RIGHT, true);
        },
        [st, status](std::string& seen) {
            seen = std::format("position {} (expected {})", status().position, st->position);
            return status().position == st->position;
        }));
    steps.push_back(step(
        "View > Format... BT.601 (PAL) with full range writes colour= and range=",
        [&frame, st] {
            // The position stepped to above (> 0), so a reopen that loses it fails the check.
            st->textsRead.clear();
            dialogHooks().format = [st](FormatDialog& dialog) {
                using Control = FormatDialog::Control;
                // The stream has no colour key: the default entry, no key written.
                st->textsRead.push_back(
                    (dialog.colourText() + "|" + dialog.colourOptionsText()).utf8_string());
                st->flagRead = !dialog.choice().colour.standard;
                dialog.userPick(Control::Colour, "BT.601 (PAL)");
                dialog.userPick(Control::Range, 1);
                st->textsRead.push_back(
                    (dialog.colourOptionsText() + "|" + dialog.colourValuesText()).utf8_string());
                st->colourRead = dialog.choice().colour;
            };
            frame.execute(Command::Format);  // ShowModal; the hook fills and closes the dialog
        },
        [&frame, st, status, stream_uri](std::string& seen) {
            const bool info = infoHas(frame, "matrix bt601,") && infoHas(frame, "range full,") &&
                              infoHas(frame, "colour: BT.601 (PAL)");
            const std::vector<std::string> expected{
                "BT.709 (default)|BT.1886|Limited|1",
                "BT.1886|Full (not standard)|1|Matrix BT.601, primaries BT.601-625 (not "
                "converted)"};
            const bool read = st->colourRead.standard == dvv::media::ColourPreset::Bt601Pal &&
                              st->colourRead.range == dvv::media::ColourRange::Full;
            const std::string uri = stream_uri();
            const bool keys = uri.contains("colour=bt601-pal") && uri.contains("range=full");
            std::string texts;
            for (const std::string& text : st->textsRead) {
                texts += std::format("'{}' ", text);
            }
            seen = std::format(
                "Info bt601/full {}, no key first {}, texts {}, read {}, URI keys {}, position {} "
                "(kept {}), playing {}",
                info, st->flagRead, texts, read, keys, status().position, st->position,
                status().playing);
            return info && st->flagRead && st->textsRead == expected && read && keys &&
                   st->position > 0 && status().position == st->position && !status().playing;
        }));
    steps.push_back(step(
        "BT.2100 PQ with limited range writes colour=bt2100-pq and Info shows pq",
        [&frame, st] {
            st->textsRead.clear();
            dialogHooks().format = [st](FormatDialog& dialog) {
                using Control = FormatDialog::Control;
                dialog.userPick(Control::Colour, "BT.2100");
                dialog.userPick(Control::Range, 0);  // limited: the base's range, no range key
                st->textsRead.push_back(dialog.colourOptionsText().utf8_string());
                st->colourRead = dialog.choice().colour;
            };
            frame.execute(Command::Format);
        },
        [&frame, st, status, stream_uri](std::string& seen) {
            const std::string uri = stream_uri();
            const bool read = st->colourRead.standard == dvv::media::ColourPreset::Bt2100Pq &&
                              st->colourRead.range == dvv::media::ColourRange::Unspecified;
            const bool keys = uri.contains("colour=bt2100-pq") && !uri.contains("matrix=") &&
                              !uri.contains("range=") && !uri.contains("transfer=");
            const bool info = infoHas(frame, "curve pq,") && infoHas(frame, "colour: BT.2100 PQ");
            const std::string options = st->textsRead.empty() ? "" : st->textsRead.front();
            // D5R §2.2: the 4th status field names the PQ tone mapping.
            const std::string mapping = frame.statusTexts()[3].utf8_string();
            seen = std::format(
                "options '{}', read {}, URI keys {}, Info pq {}, mapping '{}', position {} (kept "
                "{})",
                options, read, keys, info, mapping, status().position, st->position);
            return options == "PQ|Limited|1" && read && keys && info &&
                   mapping == "PQ → SDR tone mapping (1000 → 100 nits)" &&
                   status().position == st->position;
        }));
    steps.push_back(step(
        "open colour=bt709&range=full",
        [&frame, st] {
            std::string uri = frame.streams().front().resolvedUri;
            const std::string_view pq = "colour=bt2100-pq";
            if (const std::size_t at = uri.find(pq); at != std::string::npos) {
                uri.replace(at, pq.size(), "colour=bt709&range=full");
            }
            frame.openUri(0, uri, st->position);
        },
        [st, status, stream_uri](std::string& seen) {
            const std::string uri = stream_uri();
            const bool keys = uri.contains("colour=bt709") && uri.contains("range=full") &&
                              !uri.contains("bt2100-pq");
            seen = std::format("URI keys {}, position {} (kept {})", keys, status().position,
                               st->position);
            return keys && status().position == st->position;
        }));
    steps.push_back(step(
        "View > Format... reads colour=bt709&range=full back",
        [&frame, st] {
            st->colourRead = {};
            st->textsRead.clear();
            dialogHooks().format = [st](FormatDialog& dialog) {
                st->textsRead.push_back(
                    (dialog.colourText() + "|" + dialog.colourOptionsText()).utf8_string());
                st->colourRead = dialog.choice().colour;  // OK writes the same keys
#if defined(__WXGTK__)
                // A window manager that resizes the dialog anyway (as WSLg's does): the dialog
                // goes back to its own size, as it cannot be resized on Windows.
                const wxSize fixed = dialog.GetClientSize();
                dialog.SetClientSize(wxSize(fixed.x + 50, 1));
                wxSizeEvent resized(dialog.GetSize(), dialog.GetId());
                resized.SetEventObject(&dialog);
                dialog.HandleWindowEvent(resized);
                for (int i = 0; i < 50 && dialog.GetClientSize() != fixed; ++i) {
                    wxTheApp->Yield(true);
                    wxMilliSleep(10);
                }
                const wxSize now = dialog.GetClientSize();
                st->textsRead.push_back(now == fixed ? std::string("fixed size")
                                                     : std::format("client {}x{} (fixed {}x{})",
                                                                   now.x, now.y, fixed.x, fixed.y));
#else
                st->textsRead.push_back("fixed size");  // a dialog without a resize border
#endif
            };
            frame.execute(Command::Format);
        },
        [st, stream_uri](std::string& seen) {
            const bool read = st->colourRead.standard == dvv::media::ColourPreset::Bt709 &&
                              st->colourRead.range == dvv::media::ColourRange::Full;
            const std::string fixed = st->textsRead.size() >= 2 ? st->textsRead[1] : "";
            if (fixed != "fixed size") {
                seen = std::format("dialog size: '{}'", fixed);
                return false;
            }
            const std::string shown = st->textsRead.empty() ? "" : st->textsRead.front();
            const std::string uri = stream_uri();
            const bool keys = uri.contains("colour=bt709") && uri.contains("range=full") &&
                              !uri.contains("matrix=") && !uri.contains("transfer=");
            seen =
                std::format("shown '{}', read back bt709/full {}, URI keys {}", shown, read, keys);
            return shown == "BT.709 (default)|BT.1886|Full (not standard)|1" && read && keys;
        }));
    steps.push_back(step(
        "drop a named file",
        [&frame, st] {
            const auto file = st->dir / "drop_16x16_gray.yuv";
            writeFile(file, 768, '\x40');  // 3 frames of gray 16x16
            st->presented0 = presented(frame.viewport());
            st->dialogs0 = frame.formatDialogsShown();
            dropFiles(frame.viewport(), {file});
        },
        [&frame, st, stream_is](std::string& seen) {
            seen = std::format("gray 16x16 {}, dialogs {}", stream_is(16, 16, "gray"),
                               frame.formatDialogsShown() - st->dialogs0);
            return stream_is(16, 16, "gray") && frame.formatDialogsShown() == st->dialogs0 &&
                   presented(frame.viewport()) > st->presented0;
        }));
    steps.push_back(step(
        "drop clip.yuv without a size",
        [&frame, st] {
            const auto file = st->dir / "clip.yuv";
            writeFile(file, 768, '\x80');  // no size in the name: the format dialog asks
            st->dialogs0 = frame.formatDialogsShown();
            dialogHooks().format = [st](FormatDialog& dialog) {
                FormatChoice choice;
                choice.format = "gray";
                choice.width = 16;
                choice.height = 16;
                choice.fps = {25, 1};
                dialog.setChoice(choice);
                st->formatHookRan = true;
            };
            dropFiles(frame.viewport(), {file});
        },
        [&frame, st, stream_is](std::string& seen) {
            seen = std::format("dialogs {}, hook {}, gray 16x16 {}",
                               frame.formatDialogsShown() - st->dialogs0, st->formatHookRan,
                               stream_is(16, 16, "gray"));
            return st->formatHookRan && frame.formatDialogsShown() > st->dialogs0 &&
                   stream_is(16, 16, "gray") &&
                   frame.streams()[0].resolvedUri.find("clip.yuv") != std::string::npos;
        }));
    // The user's picks run the real handlers and the enabling rules hold.
    steps.push_back(step(
        "View > Format... picks drive the colour options and the values line",
        [&frame, st] {
            st->textsRead.clear();
            st->flagRead = true;
            st->colourRead = {};
            dialogHooks().format = [st](FormatDialog& dialog) {
                using Control = FormatDialog::Control;
                std::vector<std::string>& texts = st->textsRead;
                const auto read = [&dialog, &texts] {
                    texts.push_back((dialog.colourText() + "|" + dialog.colourOptionsText() + "|" +
                                     dialog.colourValuesText())
                                        .utf8_string());
                };
                const FormatChoice original = dialog.choice();  // gray 16x16, no colour key
                read();  // 0: the default entry, the read-only curve, gray's full range
                // A hidden control takes no pick: the curve is a choice for BT.2100 only.
                st->flagRead = !dialog.userPick(Control::Curve, 0);
                dialog.userPick(Control::Colour, "BT.2100");
                read();  // 1: BT.2100 PQ, the range kept
                dialog.userPick(Control::Curve, "HLG");
                dialog.userPick(Control::Format, "yuv420p");
                read();  // 2: HLG on YUV: the BT.2020 matrix, the range follows (limited)
                // The unpicked range followed gray -> yuv420p: no range= key.
                st->colourRead = dialog.choice().colour;
                dialog.userPick(Control::Format, "rgb24");
                dialog.userPick(Control::Colour, "BT.709 (default)");
                read();  // 3: RGB: no matrix
                dialog.userPick(Control::Format, "yuv420p");
                dialog.userPick(Control::Size, "720x576 (SD 625)");
                texts.push_back(dialog.colourHintText().utf8_string());  // 4: the size hint
                dialog.userPick(Control::Size, "1280x720 (720p)");
                texts.push_back(dialog.colourHintText().utf8_string());  // 5: none
                const FormatChoice sized = dialog.choice();
                texts.push_back(std::format("{}x{}", sized.width, sized.height));
                dialog.userPick(Control::Fps, "29.97");
                const dvv::media::Rational fps = dialog.choice().fps;  // the NTSC rule
                texts.push_back(std::format("{}/{}", fps.num, fps.den));
                // The dialog's size, logged once (not resizable, grows for its lines).
                DVV_LOG_INFO("selftest: format dialog size {}x{}", dialog.GetSize().x,
                             dialog.GetSize().y);
                // The spins drive the size choice (wxEVT_SPINCTRL).
                dialog.userSpin(false, 720);
                dialog.userSpin(true, 576);
                texts.push_back(dialog.sizeText().utf8_string());  // 8
                // Auto detect guesses BT.601 (PAL) for 576 lines; OK writes it.
                dialog.userAutoDetect(true);
                texts.push_back(
                    (dialog.colourText() + "|" + dialog.colourHintText()).utf8_string());
                const ColourChoice guessed = dialog.choice().colour;
                texts.push_back(std::format("{} {}",
                                            guessed.standard == dvv::media::ColourPreset::Bt601Pal,
                                            guessed.autoDetect));
                dialog.userSpin(true, 720);  // the guess follows the height
                texts.push_back(dialog.colourText().utf8_string());
                dialog.userAutoDetect(false);  // off again: later dialogs start without it
                texts.push_back(dialog.colourText().utf8_string());
                dialog.setChoice(original);  // OK keeps the stream as it was
            };
            frame.execute(Command::Format);
        },
        [st, stream_is](std::string& seen) {
            const std::vector<std::string>& t = st->textsRead;
            std::string texts;
            for (const std::string& text : t) {
                texts += std::format("'{}' ", text);
            }
            const bool hlg = st->colourRead.standard == dvv::media::ColourPreset::Bt2100Hlg &&
                             st->colourRead.range == dvv::media::ColourRange::Unspecified;
            seen = std::format("hidden pick refused {}, HLG without range= {}, texts {}",
                               st->flagRead, hlg, texts);
            // The options text and the values line of one pick, joined explicitly.
            const auto row = [](std::string_view options, std::string_view values) {
                return std::string(options) + std::string(values);
            };
            const std::vector<std::string> expected{
                row("BT.709 (default)|BT.1886|Full|1|",
                    "Matrix not used (gray format), primaries BT.709 (not converted)"),
                row("BT.2100|PQ|Full|1|",
                    "Matrix not used (gray format), primaries BT.2020 (not converted)"),
                "BT.2100|HLG|Limited|1|Matrix BT.2020 NCL, primaries BT.2020 (not converted)",
                row("BT.709 (default)|BT.1886|Full|1|",
                    "Matrix not used (RGB format), primaries BT.709 (not converted)"),
                "SD size: BT.601 is common for SD",
                "",
                "1280x720",
                "30000/1001",
                "720x576 (SD 625)",
                "BT.601 (PAL) (auto)|",
                "true true",
                "BT.709 (auto)",
                "BT.709 (default)"};
            return st->flagRead && hlg && t == expected && stream_is(16, 16, "gray");
        }));
    // D5 T5b: the size presets follow the spins; Load JSON of a clip description fills the groups.
    steps.push_back(step(
        "View > Format... size presets and Load JSON of a clip description",
        [&frame, st] {
            const auto file = st->dir / "desc.json";
            const std::string_view json =
                R"({"rawreelClip": 1, "format": "gray", "size": {"width": 32, "height": 8},)"
                R"( "frameRate": "30000/1001",)"
                R"( "colour": {"standard": "bt601-pal", "range": "limited"}})";
            std::ofstream(file, std::ios::binary)
                .write(json.data(), static_cast<std::streamsize>(json.size()));
            st->textsRead.clear();
            st->choiceRead = {};
            dialogHooks().format = [st,
                                    path = dvv::media::utf8FromPath(file)](FormatDialog& dialog) {
                FormatChoice qcif = dialog.choice();
                qcif.width = 176;
                qcif.height = 144;
                dialog.setChoice(qcif);
                st->textsRead.push_back(dialog.sizeText().utf8_string());
                dialog.loadJson(path);
                st->textsRead.push_back(dialog.sizeText().utf8_string());
                const dvv::media::Rational fps = dialog.choice().fps;
                st->textsRead.push_back(std::format("{}/{}", fps.num, fps.den));
                st->textsRead.push_back(dialog.errorText().utf8_string());
                st->choiceRead = dialog.choice();
            };
            frame.execute(Command::Format);
        },
        [st, stream_is, stream_uri](std::string& seen) {
            const std::vector<std::string> expected{"176x144 (QCIF)", "Custom", "30000/1001", ""};
            const FormatChoice& read = st->choiceRead;
            const bool filled = read.format == "gray" && read.width == 32 && read.height == 8 &&
                                read.fps == dvv::media::Rational{30000, 1001} &&
                                read.colour.standard == dvv::media::ColourPreset::Bt601Pal &&
                                read.colour.range == dvv::media::ColourRange::Limited;
            const std::string uri = stream_uri();
            // Gray's range is full without a key, so the limited range is written.
            const bool keys = uri.contains("colour=bt601-pal") && uri.contains("range=limited") &&
                              uri.contains("fps=30000/1001");
            seen = std::format("texts '{}', filled {}, gray 32x8 {}, URI keys {}",
                               st->textsRead.empty() ? "" : st->textsRead.front(), filled,
                               stream_is(32, 8, "gray"), keys);
            return st->textsRead == expected && filled && stream_is(32, 8, "gray") && keys;
        }));
    // D5R §4.4: the header of a size-only name, the Custom pixel-format entry and its width
    // rule (the 641 case), and that a candidate pick drops a stale Load JSON error.
    steps.push_back(step(
        "the format dialog header, the Custom (from json) entry and the stale-error drop",
        [&frame, st] {
            const auto file = st->dir / "size_16x16.yuv";
            writeFile(file, 768, '\x80');  // 2 frames of yuv420p 16x16
            // A copy of v210 (6-pixel groups: the width must be a multiple of its widthMultiple).
            const dvv::media::FormatId v210 = dvv::media::findFormat("v210").value();
            dvv::json::Value format = dvv::media::formatToJson(v210);
            format["name"] = "selftest_v210";
            const std::string text = dvv::json::dump(format);
            std::ofstream(st->dir / "custom.json", std::ios::binary)
                .write(text.data(), static_cast<std::streamsize>(text.size()));
            std::ofstream(st->dir / "bad-format.json", std::ios::binary).write("{\"name\": 1}", 11);
            st->textsRead.clear();
            st->flagRead = false;
            st->dialogs0 = frame.formatDialogsShown();
            dialogHooks().format = [st, dir = st->dir](FormatDialog& dialog) {
                using Control = FormatDialog::Control;
                std::vector<std::string>& texts = st->textsRead;
                texts.push_back(dialog.GetTitle().utf8_string());
                const wxString header = dialog.headerText();
                const wxString path = header.AfterFirst('|').BeforeFirst('|');
                texts.push_back((header.BeforeFirst('|') + "|" +
                                 (path.EndsWith("size_16x16.yuv") ? wxString("path") : path) + "|" +
                                 header.AfterFirst('|').AfterFirst('|').BeforeLast('|'))
                                    .utf8_string());
                texts.emplace_back(header.AfterLast('\n').utf8_string());  // the tooltip's end
                // A bad pixel-format JSON: the red line, no entry.
                dialog.loadJson(dvv::media::utf8FromPath(dir / "bad-format.json"));
                texts.emplace_back(dialog.errorText().empty() ? "no error" : "error");
                texts.push_back(dialog.formatLabel().BeforeFirst('|').utf8_string());
                dialog.loadJson(dvv::media::utf8FromPath(dir / "custom.json"));
                const wxString label = dialog.formatLabel();
                texts.push_back(
                    (label.BeforeFirst('|') + "|" +
                     (label.AfterFirst('|').EndsWith("custom.json") ? "path" : "no path"))
                        .utf8_string());
                // The width spin drives the geometry check as the user does.
                const bool spun641 = dialog.userSpin(false, 641);
                texts.emplace_back(!spun641                     ? "641 not spun"
                                   : dialog.errorText().empty() ? "641 accepted"
                                                                : "641 refused");
                const bool spun642 = dialog.userSpin(false, 642);
                texts.push_back((spun642 ? "642 " : "642 not spun ") +
                                dialog.errorText().utf8_string());
                st->flagRead = dialog.choice().format.starts_with('@');
                // A Load JSON error goes when the user picks a candidate.
                dialog.loadJson(dvv::media::utf8FromPath(dir / "bad-format.json"));
                const bool picked = dialog.userPick(Control::Candidate, 0);
                texts.push_back(
                    std::format("picked {} '{}'", picked, dialog.errorText().utf8_string()));
            };
            dropFiles(frame.viewport(), {file});
        },
        [&frame, st, stream_is](std::string& seen) {
            std::string texts;
            for (const std::string& text : st->textsRead) {
                texts += std::format("'{}' ", text);
            }
            const std::vector<std::string> expected{
                "Format of the stream - size_16x16.yuv",
                std::string("Size detected from the file name; format and frame rate not "
                            "detected|path|") +
                    "Name the file <name>_WxH[_fmt][_fps]|Example: clip_1920x1080_yuv420p_25.yuv",
                "Or put a clip description size_16x16.yuv.json next to the file.",
                "error",
                "yuv420p",
                "Custom (from custom.json)|path",
                "641 refused",
                "642 ",
                "picked true ''"};
            seen = std::format("dialogs {}, custom written {}, texts {}",
                               frame.formatDialogsShown() - st->dialogs0, st->flagRead, texts);
            return st->flagRead && st->textsRead == expected &&
                   frame.formatDialogsShown() > st->dialogs0 && stream_is(16, 16, "yuv420p");
        }));
    // A long name without a size (the title cut in the middle, "Format
    // not detected: <reason>", the Log line), a non-file title, and Auto detect on the open path.
    steps.push_back(step(
        "drop a long name without a size with Auto detect on",
        [&frame, st] {
            const auto file = st->dir / "a_clip_with_a_rather_long_name_and_no_size.yuv";
            writeFile(file, 720 * 576 * 3 / 2, '\x80');  // 1 frame of yuv420p 720x576
            st->textsRead.clear();
            st->flagRead = false;
            st->dialogs0 = frame.formatDialogsShown();
            dialogHooks().format = [st](FormatDialog& dialog) {
                using Control = FormatDialog::Control;
                std::vector<std::string>& texts = st->textsRead;
                texts.push_back(dialog.GetTitle().utf8_string());
                texts.push_back(dialog.headerText().BeforeFirst('|').utf8_string());
                texts.push_back(
                    formatDialogTitle("synthetic:gradient?fmt=gray&size=16x16").utf8_string());
                dialog.userPick(Control::Format, "yuv420p");
                dialog.userPick(Control::Size, "720x576 (SD 625)");
                dialog.userAutoDetect(true);  // kept for the session: the next dialog starts on
                st->flagRead = dialog.autoDetect();
            };
            dropFiles(frame.viewport(), {file});
        },
        [&frame, st, stream_is, stream_uri](std::string& seen) {
            const std::vector<std::string>& t = st->textsRead;
            const std::string detection = t.size() > 1 ? t[1] : std::string();
            const bool reason = detection.starts_with("Format not detected: ") &&
                                detection.size() > std::string_view("Format not detected: ").size();
            const wxString log = frame.logPanel().text();
            const bool logged = !detection.empty() && log.Contains("needs a format") &&
                                log.Contains(wxString::FromUTF8(detection));
            const std::string uri = stream_uri();
            const std::vector<std::string> titles{
                "Format of the stream - a_clip_with_a_rathe...me_and_no_size.yuv",
                "Format of the stream - synthetic:gradient"};
            const bool titled = t.size() == 3 && t[0] == titles[0] && t[2] == titles[1];
            seen = std::format("titles {} ('{}'), detection '{}', log line {}, auto {}, URI '{}'",
                               titled, t.empty() ? "" : t[0], detection, logged, st->flagRead, uri);
            return titled && reason && logged && st->flagRead && uri.contains("colour=bt601-pal") &&
                   stream_is(720, 576, "yuv420p");
        }));
    steps.push_back(step(
        "the next format dialog starts with Auto detect on",
        [&frame, st] {
            st->flagRead = false;
            st->textsRead.clear();
            dialogHooks().format = [st](FormatDialog& dialog) {
                st->flagRead = dialog.autoDetect();
                dialog.userAutoDetect(false);  // off again for the later steps
                st->textsRead.push_back(dialog.autoDetect() ? "on" : "off");
            };
            frame.execute(Command::Format);
        },
        [st, stream_is](std::string& seen) {
            const bool off = st->textsRead == std::vector<std::string>{"off"};
            seen = std::format("started on {}, turned off {}", st->flagRead, off);
            return st->flagRead && off && stream_is(720, 576, "yuv420p");
        }));
    // A colour-only sidecar does not claim the format the file name gave.
    steps.push_back(step(
        "drop a named file with a colour-only sidecar",
        [&frame, st] {
            const auto file = st->dir / "m3_16x16_yuv420p.yuv";
            writeFile(file, 768, '\x80');  // 2 frames of yuv420p 16x16
            const std::string_view json = R"({"rawreelClip": 1, "colour": {"standard": "bt2020"}})";
            std::ofstream(st->dir / "m3_16x16_yuv420p.yuv.json", std::ios::binary)
                .write(json.data(), static_cast<std::streamsize>(json.size()));
            st->dialogs0 = frame.formatDialogsShown();
            dropFiles(frame.viewport(), {file});
        },
        [&frame, st, stream_is](std::string& seen) {
            const bool bt2020 = !frame.streams().empty() && frame.streams()[0].colour.standard ==
                                                                dvv::media::ColourPreset::Bt2020;
            seen =
                std::format("yuv420p 16x16 {}, bt2020 {}, dialogs {}", stream_is(16, 16, "yuv420p"),
                            bt2020, frame.formatDialogsShown() - st->dialogs0);
            return stream_is(16, 16, "yuv420p") && bt2020 &&
                   frame.formatDialogsShown() == st->dialogs0;
        }));
    steps.push_back(step(
        "View > Format... credits the file name, not the colour-only sidecar",
        [&frame, st] {
            st->textsRead.clear();
            dialogHooks().format = [st](FormatDialog& dialog) {
                st->textsRead.push_back(dialog.headerText().BeforeFirst('|').utf8_string());
            };
            frame.execute(Command::Format);
        },
        [st, stream_is](std::string& seen) {
            const std::string line = st->textsRead.empty() ? "-" : st->textsRead.front();
            seen = std::format("detection '{}'", line);
            return line.contains("file name") && !line.contains(".json") &&
                   stream_is(16, 16, "yuv420p");
        }));
    steps.push_back(step(
        "drop a Y4M file",
        [&frame, st] {
            const auto file = st->dir / "clip.y4m";
            std::string y4m = "YUV4MPEG2 W16 H16 F25:1 Ip A1:1 C420jpeg\n";
            for (int i = 0; i < 2; ++i) {
                y4m += "FRAME\n" + std::string(384, '\x50');
            }
            std::ofstream(file, std::ios::binary)
                .write(y4m.data(), static_cast<std::streamsize>(y4m.size()));
            st->dialogs0 = frame.formatDialogsShown();
            dropFiles(frame.viewport(), {file});
        },
        [&frame, st, stream_is](std::string& seen) {
            const auto& streams = frame.streams();
            const bool y4m = streams.size() == 1 && streams[0].info.provider == "y4m";
            seen = std::format("yuv420p 16x16 {}, y4m {}, dialogs {}", stream_is(16, 16, "yuv420p"),
                               y4m, frame.formatDialogsShown() - st->dialogs0);
            return stream_is(16, 16, "yuv420p") && y4m &&
                   frame.formatDialogsShown() == st->dialogs0;
        }));
    steps.push_back(step(
        "View > Format... of a Y4M stream locks format, size and frame rate",
        [&frame, st] {
            const auto file = st->dir / "colour.json";
            const std::string_view json = R"({"rawreelClip": 1, "size": {"width": 8, "height": 8},)"
                                          R"( "colour": {"standard": "bt2020"}})";
            std::ofstream(file, std::ios::binary)
                .write(json.data(), static_cast<std::streamsize>(json.size()));
            const std::string_view size_only =
                R"({"rawreelClip": 1, "size": {"width": 8, "height": 8}})";
            std::ofstream(st->dir / "size-only.json", std::ios::binary)
                .write(size_only.data(), static_cast<std::streamsize>(size_only.size()));
            std::ofstream(st->dir / "broken.json", std::ios::binary).write("{", 1);
            st->textsRead.clear();
            st->choiceRead = {};
            st->lockedRead = false;
            st->flagRead = false;
            dialogHooks().format = [st,
                                    path = dvv::media::utf8FromPath(file)](FormatDialog& dialog) {
                using Control = FormatDialog::Control;
                std::vector<std::string>& texts = st->textsRead;
                st->lockedRead = dialog.fieldsLocked();
                st->flagRead = dialog.colourTakesInitialFocus();  // Enter presses OK
                texts.push_back(dialog.lockedText().utf8_string());
                // The Y4M header gives no colour: BT.709 by default, the range as the header says.
                texts.push_back(
                    (dialog.colourText() + "|" + dialog.colourOptionsText()).utf8_string());
                // Load JSON never shrinks a dialog the user enlarged.
                const wxSize enlarged = dialog.GetSize() + dialog.FromDIP(wxSize(120, 160));
                dialog.SetSize(enlarged);
                dialog.loadJson(
                    dvv::media::utf8FromPath(st->dir / "size-only.json"));  // no colour group
                texts.push_back(dialog.noteText().utf8_string());
                dialog.loadJson(path);
                texts.push_back(dialog.noteText().utf8_string());
                const wxSize after = dialog.GetSize();
                texts.push_back(after.x >= enlarged.x && after.y >= enlarged.y
                                    ? std::string("size kept")
                                    : std::format("shrunk {}x{} -> {}x{}", enlarged.x, enlarged.y,
                                                  after.x, after.y));
                st->choiceRead = dialog.choice();
                // A Load JSON error goes when the user changes a control.
                dialog.loadJson(dvv::media::utf8FromPath(st->dir / "broken.json"));
                texts.emplace_back(dialog.errorText().empty() ? "no error" : "error");
                dialog.userPick(Control::Colour, "BT.601 (NTSC)");
                texts.push_back((dialog.errorText() + "|" + dialog.colourText()).utf8_string());
                // D5R §4.4: the title and the detection line of a Y4M stream, no template lines.
                const wxString header = dialog.headerText();
                texts.push_back((dialog.GetTitle() + "|" + header.BeforeFirst('|') + "|" +
                                 header.AfterFirst('|').AfterFirst('|'))
                                    .utf8_string());
                // Back to the standard of the JSON, which OK writes.
                dialog.userPick(Control::Colour, "BT.2020");
            };
            frame.execute(Command::Format);
        },
        [st, stream_is, stream_uri](std::string& seen) {
            const std::string_view prefix = "Format, size and frame rate come from the file; ";
            const std::vector<std::string> expected{
                "yuv420p  (from file)|16x16 @ 25  (from file)",  // D5R §4.4: one Size text
                "BT.709 (default)|BT.1886|Limited|1",  // no XCOLORRANGE: limited by default
                std::format("{}nothing was loaded.", prefix),
                std::format("{}only the colour was loaded.", prefix),
                "size kept",
                "error",
                "|BT.601 (NTSC)",  // a pick drops the Load JSON error
                "Format of the stream - clip.y4m|Format detected from the Y4M header|||"};
            const FormatChoice& read = st->choiceRead;
            const bool colour_only = read.width == 16 && read.height == 16 &&
                                     read.colour.standard == dvv::media::ColourPreset::Bt2020;
            const std::string uri = stream_uri();
            std::string texts;
            for (const std::string& text : st->textsRead) {
                texts += std::format("'{}' ", text);
            }
            seen = std::format("locked {}, Colour first {}, texts {}, colour only {}, URI '{}'",
                               st->lockedRead, st->flagRead, texts, colour_only, uri);
            return st->lockedRead && st->flagRead && st->textsRead == expected && colour_only &&
                   uri.contains("colour=bt2020") && stream_is(16, 16, "yuv420p");
        }));
    // A gray Y4M with XCOLORRANGE=LIMITED keeps its range under BT.709 (the default
    // range of gray is full).
    steps.push_back(step(
        "drop a gray Y4M with XCOLORRANGE=LIMITED",
        [&frame, st] {
            const auto file = st->dir / "gray.y4m";
            std::string y4m = "YUV4MPEG2 W16 H16 F25:1 Ip A1:1 Cmono XCOLORRANGE=LIMITED\n";
            for (int i = 0; i < 2; ++i) {
                y4m += "FRAME\n" + std::string(256, '\x50');
            }
            std::ofstream(file, std::ios::binary)
                .write(y4m.data(), static_cast<std::streamsize>(y4m.size()));
            dropFiles(frame.viewport(), {file});
        },
        [&frame, stream_is](std::string& seen) {
            const auto& streams = frame.streams();
            const bool y4m = streams.size() == 1 && streams[0].info.provider == "y4m";
            seen = std::format("gray 16x16 {}, y4m {}", stream_is(16, 16, "gray"), y4m);
            return stream_is(16, 16, "gray") && y4m;
        }));
    steps.push_back(step(
        "gray Y4M LIMITED keeps its range under BT.709",
        [&frame, st] {
            st->textsRead.clear();
            dialogHooks().format = [st](FormatDialog& dialog) {
                using Control = FormatDialog::Control;
                std::vector<std::string>& texts = st->textsRead;
                texts.push_back(
                    (dialog.colourText() + "|" + dialog.colourOptionsText()).utf8_string());
                dialog.userPick(Control::Colour, "BT.2020");
                dialog.userPick(Control::Colour, "BT.709 (default)");
                texts.push_back(
                    (dialog.colourText() + "|" + dialog.colourOptionsText()).utf8_string());
                st->choiceRead = dialog.choice();
            };
            frame.execute(Command::Format);
        },
        [&frame, st, stream_is](std::string& seen) {
            const std::vector<std::string> expected{"BT.709 (default)|BT.1886|Limited|1",
                                                    "BT.709 (default)|BT.1886|Limited|1"};
            const std::string info = frame.infoPanel().text().utf8_string();
            const bool limited = info.contains("range limited");
            std::string texts;
            for (const std::string& text : st->textsRead) {
                texts += std::format("'{}' ", text);
            }
            seen = std::format("texts {}, Info range limited {}", texts, limited);
            return st->textsRead == expected && !st->choiceRead.colour.standard &&
                   st->choiceRead.colour.range == dvv::media::ColourRange::Unspecified && limited &&
                   stream_is(16, 16, "gray");
        }));
    steps.push_back(step(
        "drop two files",
        [&frame, st] {
            const auto first = st->dir / "first_32x16_gray.yuv";
            const auto second = st->dir / "second_16x16_gray.yuv";
            writeFile(first, 1536, '\x20');
            writeFile(second, 768, '\x60');
            dropFiles(frame.viewport(), {first, second});
        },
        [&frame, stream_is](std::string& seen) {
            const bool logged =
                frame.logPanel().text().Contains("1 files ignored: one stream at a time");
            seen = std::format("gray 32x16 {}, log line {}", stream_is(32, 16, "gray"), logged);
            return stream_is(32, 16, "gray") && logged;
        }));
    // §8.5: an FFV1/MKV drop opens through the ffmpeg provider without a format dialog.
    wxString mkv_text;
    const bool mkv_named = wxGetEnv("RAWREEL_TEST_MKV", &mkv_text) && !mkv_text.empty();
    const std::filesystem::path mkv =
        mkv_named ? dvv::media::pathFromUtf8(mkv_text.utf8_string()) : std::filesystem::path{};
    std::error_code mkv_error;
    const bool mkv_exists = mkv_named && std::filesystem::is_regular_file(mkv, mkv_error);
    if (test.app().startup().ffmpeg.core && mkv_exists) {
        steps.push_back(step(
            "drop the FFV1 MKV",
            [&frame, st, mkv] {
                st->dialogs0 = frame.formatDialogsShown();
                st->presented0 = presented(frame.viewport());
                dropFiles(frame.viewport(), {mkv});
            },
            [&frame, st, stream_is](std::string& seen) {
                const auto& streams = frame.streams();
                const bool ffmpeg =
                    streams.size() == 1 && streams[0].resolvedUri.contains("impl=ffmpeg");
                // The Session clears the display before streamsChanged, so a shown frame of the
                // current epoch is an MKV frame (presented alone grows with every tick).
                const DisplaySnapshot snapshot = frame.viewport().display()->snapshot();
                const bool shown =
                    snapshot.frames.count == 1 && snapshot.frames.epoch == frame.viewport().epoch();
                seen = std::format(
                    "yuv420p10le 64x48 {}, URI '{}', dialogs {} ({} at the start), shown {}",
                    stream_is(64, 48, "yuv420p10le"),
                    streams.empty() ? "-" : streams[0].resolvedUri, frame.formatDialogsShown(),
                    st->dialogs0, shown);
                return stream_is(64, 48, "yuv420p10le") && ffmpeg &&
                       frame.formatDialogsShown() == st->dialogs0 && shown &&
                       presented(frame.viewport()) > st->presented0;
            }));
        // View > Format on the locked MKV, OK without a change reopens it unchanged.
        steps.push_back(step(
            "View > Format... OK on the locked MKV",
            [&frame, st] {
                st->textsRead.clear();
                st->lockedRead = false;
                st->dialogs0 = frame.formatDialogsShown();
                dialogHooks().format = [st](FormatDialog& dialog) {
                    st->lockedRead = dialog.fieldsLocked();
                    st->textsRead.push_back(dialog.lockedText().utf8_string());
                    st->textsRead.push_back(dialog.colourText().utf8_string());
                    st->choiceRead = dialog.choice();
                };
                frame.execute(Command::Format);
            },
            [&frame, st, stream_is, stream_uri](std::string& seen) {
                const std::string uri = stream_uri();
                const bool no_key = !st->choiceRead.colour.standard && !uri.contains("colour=");
                std::string texts;
                for (const std::string& text : st->textsRead) {
                    texts += std::format("'{}' ", text);
                }
                seen = std::format("locked {}, texts {}, no key {}, URI '{}'", st->lockedRead,
                                   texts, no_key, uri);
                const std::vector<std::string> expected{
                    "yuv420p10le  (from file)|64x48 @ 25  (from file)", "BT.709 (from file)"};
                return st->lockedRead && st->textsRead == expected && no_key &&
                       frame.formatDialogsShown() > st->dialogs0 &&
                       stream_is(64, 48, "yuv420p10le") && uri.contains("impl=ffmpeg");
            }));
        steps.push_back(step(
            "play the MKV 5 frames", [&frame] { frame.execute(Command::PlayPause); },
            [status](std::string& seen) {
                seen = std::format("position {}", status().position);
                return status().position >= 5;
            }));
        steps.push_back(step(
            "pause the MKV",
            [&frame, status] {
                if (status().playing) {
                    frame.execute(Command::PlayPause);
                }
            },
            [status](std::string& seen) {
                seen = std::format("playing {}", status().playing);
                return !status().playing;
            }));
    } else if (ffmpegRequired()) {
        std::string reason;
        if (!test.app().startup().ffmpeg.core) {
            reason = ffmpegStatusLine(test.app().startup().ffmpeg);
        } else if (!mkv_named) {
            reason = "RAWREEL_TEST_MKV is not set";
        } else {
            reason = std::format("RAWREEL_TEST_MKV names no file: {}", mkv_text.utf8_string());
        }
        steps.push_back(ffmpegRequiredStep(std::move(reason)));
    } else {
        DVV_LOG_INFO("selftest: FFmpeg or RAWREEL_TEST_MKV not available, MKV steps skipped");
    }
    steps.push_back(step(
        "close stream and clean up", [&frame] { frame.execute(Command::CloseStreams); },
        [&frame, st](std::string& seen) {
            seen = std::format("streams {}", frame.streams().size());
            if (!frame.streams().empty()) {
                return false;
            }
            std::error_code error;
            std::filesystem::remove_all(st->dir, error);  // a failure only logs
            if (error) {
                DVV_LOG_WARN("selftest: could not remove the scratch folder: {}", error.message());
            }
            return true;
        }));
    return steps;  // the runner appends the close step (validation 0, leak check)
}

// --- perf (design v2 §8.4 perf, E7; manual) ------------------------------------------------------

std::vector<SelfTestStep> perfScenario(SelfTest& test) {
    MainFrame& frame = test.frame();
    ShowHistory& history = *test.app().showHistory();
    struct State {
        int quietTicks = 0;
        std::size_t mark = 0;
        std::uint64_t presented0 = 0, overwritten0 = 0, stalls0 = 0;
        std::uint64_t presented = 0, overwritten = 0, stalls = 0;
        std::string bottleneck = "none";
        ShowTiming timing;
        // Fixed seed on purpose: the perf seek pattern must be the same in every run.
        std::mt19937_64 random{1};  // NOLINT(bugprone-random-generator-seed)
        std::int64_t target = 0;
        Clock::time_point seekStart{};
        std::vector<double> seekMs;
    };
    auto st = std::make_shared<State>();
    auto status = [&frame] { return frame.session().status(); };
    std::vector<SelfTestStep> steps;
    using std::chrono::seconds;

    steps.push_back(step(
        "open and maximise", [&frame] { frame.Maximize(); },
        [&frame](std::string& seen) {
            seen = std::format("streams {}, presented {}", frame.streams().size(),
                               presented(frame.viewport()));
            return frame.streams().size() == 1 && presented(frame.viewport()) >= 2;
        },
        seconds(30)));
    steps.push_back(step(
        "warm-up", {},
        [st, status](std::string& seen) {
            const SessionStatus s = status();
            seen = std::format("inflight {}, readyAhead {}", s.inflight, s.readyAhead);
            st->quietTicks = s.inflight == 0 && s.readyAhead >= 2 ? st->quietTicks + 1 : 0;
            return st->quietTicks >= 3;  // W is full: nothing in flight, nothing more to fetch
        },
        seconds(60)));
    steps.push_back(step(
        "play 600 frames",
        [&frame, &history, st, status] {
            if (!status().player.loop) {
                frame.execute(Command::Loop);
            }
            st->stalls0 = status().player.stalls;
            st->presented0 = presented(frame.viewport());
            st->overwritten0 = frame.viewport().display()->stats().overwritten;
            st->mark = history.size();
            frame.execute(Command::PlayPause);
        },
        [&frame, &history, st, status](std::string& seen) {
            const SessionStatus s = status();
            if (!s.bottleneck.empty()) {
                st->bottleneck = s.bottleneck;
            }
            const std::size_t shown = history.size() - st->mark;
            seen = std::format("{} of 601 shows", shown);
            if (shown < 601) {
                return false;
            }
            frame.execute(Command::PlayPause);  // pause at once: the counters below end here
            st->stalls = status().player.stalls - st->stalls0;
            st->presented = presented(frame.viewport()) - st->presented0;
            st->overwritten = frame.viewport().display()->stats().overwritten - st->overwritten0;
            std::vector<ShowSample> samples;
            for (const ShownSet& set : history.since(st->mark)) {
                if (samples.size() == 601) {
                    break;
                }
                samples.push_back({.index = set.index, .at = set.at});
            }
            st->timing = showTiming(samples, frame.streams().at(0).info.fps);
            return true;
        },
        seconds(120)));
    for (int k = 1; k <= 20; ++k) {
        steps.push_back(step(
            std::format("seek {}", k),
            [&frame, &history, st, status] {
                const SessionStatus s = status();
                std::uniform_int_distribution<std::int64_t> pick(0,
                                                                 s.player.length.value_or(1) - 1);
                do {
                    st->target = pick(st->random);
                } while (st->target == s.player.position && s.player.length.value_or(1) > 1);
                st->mark = history.size();
                st->seekStart = Clock::now();
                frame.session().seek(st->target);
            },
            [&history, st](std::string& seen) {
                for (const ShownSet& set : history.since(st->mark)) {
                    if (set.index == st->target) {
                        st->seekMs.push_back(
                            std::chrono::duration<double, std::milli>(set.at - st->seekStart)
                                .count());
                        return true;
                    }
                }
                seen = std::format("target {} not shown", st->target);
                return false;
            },
            seconds(10)));
    }
    steps.push_back(step(
        "report",
        [st, status] {
            const std::string line = std::format(
                R"({{"targetFps":{:.3f},"actualFps":{:.3f},"stalls":{},"bottleneck":"{}","frames":600,"presented":{},"overwritten":{},"p99TickLatenessMs":{:.3f},"inflight":{},"seekMedianMs":{:.1f},"seekMaxMs":{:.1f}}})",
                status().player.targetFps, st->timing.actualFps, st->stalls, st->bottleneck,
                st->presented, st->overwritten, st->timing.p99LatenessMs, App::arguments().inflight,
                median(st->seekMs), st->seekMs.empty() ? 0.0 : std::ranges::max(st->seekMs));
            std::fputs((line + "\n").c_str(), stdout);
            std::fflush(stdout);
        },
        [](std::string&) { return true; }));
    return steps;  // the runner's close step follows; exit 0
}

}  // namespace rawreel
