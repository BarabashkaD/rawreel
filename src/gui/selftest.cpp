// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/selftest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <wx/anybutton.h>
#include <wx/aui/framemanager.h>
// clang-format off
#include <wx/infobar.h>  // wxInfoBarBase, before the generic class
#include <wx/generic/infobar.h>
// clang-format on
// clang-format off
#include <wx/aboutdlg.h>  // wxAboutDialogInfo, before the generic dialog
#include <wx/generic/aboutdlgg.h>
// clang-format on
#include <wx/clipbrd.h>
#include <wx/collpane.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/hyperlink.h>
#include <wx/preferences.h>
#include <wx/settings.h>
#include <wx/slider.h>
#include <wx/textdlg.h>

#include "core/commands.h"
#include "core/config.h"
#include "core/format_choice.h"
#include "core/layout.h"
#include "core/log_file_switch.h"
#include "core/log_mirror.h"
#include "core/log_store.h"
#include "core/log_text.h"
#include "core/perf_stats.h"
#include "core/session.h"
#include "core/settings_claim.h"
#include "core/startup.h"
#include "core/timeline.h"
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
#include "gui/icons.h"
#include "gui/info_panel.h"
#include "gui/log_list.h"
#include "gui/log_panel.h"
#include "gui/main_frame.h"
#include "gui/native_controls.h"
#include "gui/native_window.h"
#include "gui/player_panel.h"
#include "gui/settings.h"
#include "gui/timeline_strip.h"
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

SelfTestStep step(std::string name, dvv::MoveOnlyFunction<void()> start,
                  dvv::MoveOnlyFunction<bool(std::string&)> passed,
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

// Keyboard, mouse-button, pointer-motion, wheel, menu and activation events: what a person at the
// machine sends.
bool isRealInput(wxEventType type) {
    static const std::array<wxEventType, 24> REAL_INPUT_TYPES{
        wxEVT_KEY_DOWN,      wxEVT_KEY_UP,       wxEVT_CHAR,         wxEVT_CHAR_HOOK,
        wxEVT_LEFT_DOWN,     wxEVT_LEFT_UP,      wxEVT_LEFT_DCLICK,  wxEVT_RIGHT_DOWN,
        wxEVT_RIGHT_UP,      wxEVT_RIGHT_DCLICK, wxEVT_MIDDLE_DOWN,  wxEVT_MIDDLE_UP,
        wxEVT_MIDDLE_DCLICK, wxEVT_AUX1_DOWN,    wxEVT_AUX1_UP,      wxEVT_AUX2_DOWN,
        wxEVT_AUX2_UP,       wxEVT_MOUSEWHEEL,   wxEVT_CONTEXT_MENU, wxEVT_MENU,
        wxEVT_ACTIVATE,      wxEVT_MOTION,       wxEVT_ENTER_WINDOW, wxEVT_LEAVE_WINDOW};
    return std::ranges::find(REAL_INPUT_TYPES, type) != REAL_INPUT_TYPES.end();
}

// Pointer motion over the windows: real input that is counted but not logged (every move sends
// some).
bool isPointerMotion(wxEventType type) {
    return type == wxEVT_MOTION || type == wxEVT_ENTER_WINDOW || type == wxEVT_LEAVE_WINDOW;
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
    if (!isPointerMotion(event.GetEventType())) {
        DVV_LOG_INFO("rawreel-gui --selftest: real input ignored (event type {})",
                     event.GetEventType());
    }
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
    std::uint64_t logNext = 0;                      // logNext() at the step start
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

// The store's next sequence: a step remembers it at its start (the store is capped, so steps
// compare sequence numbers, not text lengths; spec §7.1).
std::uint64_t logNext(const App& app) {
    return app.logStore()->next();
}

// The lines logged from sequence `since` on, as logRecordsText gives them.
std::string loggedSince(const App& app, std::uint64_t since) {
    const std::vector<LogEntry> entries = app.logStore()->entries();
    const auto first = std::ranges::find_if(
        entries, [since](const LogEntry& entry) { return entry.sequence >= since; });
    return logRecordsText(std::span<const LogEntry>(first, entries.end()));
}

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
#if defined(_WIN32) || defined(__WXOSX__)
        test->frame().Iconize(true);  // Windows and Cocoa minimise a window themselves
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
#if defined(_WIN32) || defined(__WXOSX__)
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
#if defined(_WIN32) || defined(__WXOSX__)
        // wxOSX sets the application's NSAppearance, which wxSystemSettings reads back.
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

// The shell (design D18): the Info pane is shown, the Log pane is not (a selftest starts with it
// hidden, Log pane and Settings design §2), the player panel is the centre pane and holds the
// viewport.
SelfTestStep panesStep(SelfTest* test) {
    auto passed = [test](std::string& seen) {
        MainFrame& frame = test->frame();
        const wxAuiPaneInfo& info = frame.aui().GetPane("info");
        const wxAuiPaneInfo& log = frame.aui().GetPane("log");
        const wxAuiPaneInfo& player = frame.aui().GetPane("player");
        const bool centre = player.IsOk() && player.dock_direction == wxAUI_DOCK_CENTER;
        const bool child = frame.viewport().GetParent() == &frame.playerPanel();
        seen = std::format(
            "info shown {}, log pane {} shown {} (logShown {}), player pane {}, centre {}, "
            "viewport in the player panel {}",
            info.IsOk() && info.IsShown(), log.IsOk(), log.IsShown(), frame.logShown(),
            player.IsOk(), centre, child);
        return info.IsOk() && info.IsShown() && log.IsOk() && !log.IsShown() && !frame.logShown() &&
               centre && child;
    };
    return step("panes", {}, std::move(passed));
}

// --- the Log pane: View -> Log, the close button and the notice (Log pane and Settings design §2)

// The View -> Log item after an update of the menus: checked (Windows, Linux) or titled "Hide Log"
// (macOS) when `shown`; unchecked or "Show Log" otherwise. `text` describes the item.
bool logItemFollows(MainFrame& frame, bool shown, std::string& text) {
    wxMenuBar* bar = frame.GetMenuBar();
    if (bar == nullptr) {
        text = "no menu bar";
        return false;
    }
    bar->UpdateMenus();
    const wxMenuItem* item = bar->FindItem(menuId(Command::LogPane));
    if (item == nullptr) {
        text = "no View -> Log item";
        return false;
    }
#if defined(__WXOSX__)
    const wxString title = item->GetItemLabelText();
    text = std::format("item '{}'", title.utf8_string());
    return title == (shown ? "Hide Log" : "Show Log");
#else
    text = std::format("item '{}' checkable {} checked {}", item->GetItemLabelText().utf8_string(),
                       item->IsCheckable(), item->IsChecked());
    return item->IsCheckable() && item->IsChecked() == shown;
#endif
}

// Spec §2: a warning logged while the pane is hidden shows in the status bar's notice, counted
// from the last time the pane was shown.
SelfTestStep logNoticeStep(SelfTest* test) {
    auto start = [test] {
        MainFrame& frame = test->frame();
        frame.setLogShown(true);
        frame.setLogShown(false);
        DVV_LOG_WARN("selftest notice warning");
    };
    auto passed = [test](std::string& seen) {
        const wxString notice = test->frame().logNotice();
        seen = std::format("notice '{}', status field '{}', pane shown {}", notice.utf8_string(),
                           test->frame().statusTexts()[4].utf8_string(), test->frame().logShown());
        // Other threads may log warnings too: only the form is checked.
        return !test->frame().logShown() && notice.Contains("warning") &&
               notice.EndsWith("in the log") && test->frame().statusTexts()[4] == notice;
    };
    return step("log notice", std::move(start), std::move(passed));
}

// View -> Log shows the pane: the item follows, the notice clears, the list is rebuilt from the
// mirror and the viewport keeps its minimum.
SelfTestStep logPaneShowsStep(SelfTest* test) {
    auto start = [test] { test->frame().execute(Command::LogPane); };
    auto passed = [test](std::string& seen) {
        MainFrame& frame = test->frame();
        const bool shown = frame.aui().GetPane("log").IsShown() && frame.logShown();
        std::string item;
        const bool follows = logItemFollows(frame, true, item);
        const LogPanel& panel = frame.logPanel();
        const std::size_t rows = panel.list().rowCount();
        const wxSize player = frame.playerPanel().GetClientSize();
        const wxSize minimum = frame.playerPanel().minimumSize();
        seen = std::format(
            "pane shown {}, {} ok {}, notice '{}', {} list rows ({} mirror records), player panel "
            "{}x{} (minimum {}x{})",
            shown, item, follows, frame.logNotice().utf8_string(), rows, panel.mirror().size(),
            player.x, player.y, minimum.x, minimum.y);
        return shown && follows && frame.logNotice().empty() && rows == panel.mirror().size() &&
               player.x >= minimum.x && player.y >= minimum.y;
    };
    return step("log pane shows", std::move(start), std::move(passed));
}

struct LogPaneState {
    int height = 0;  // the pane's height before its close button hid it
};

// The caption's close button (wxAUI's ClosePane): the pane hides and the item follows.
SelfTestStep logPaneCloseStep(SelfTest* test, const std::shared_ptr<LogPaneState>& state) {
    auto start = [test, state] {
        MainFrame& frame = test->frame();
        wxAuiPaneInfo& pane = frame.aui().GetPane("log");
        state->height = pane.rect.height;
        frame.aui().ClosePane(pane);
        frame.aui().Update();
    };
    auto passed = [test, state](std::string& seen) {
        MainFrame& frame = test->frame();
        const wxAuiPaneInfo& pane = frame.aui().GetPane("log");
        std::string item;
        const bool follows = logItemFollows(frame, false, item);
        seen = std::format("close button {}, pane shown {}, logShown {}, {} ok {}, height was {}",
                           pane.HasCloseButton(), pane.IsShown(), frame.logShown(), item, follows,
                           state->height);
        return pane.HasCloseButton() && !pane.IsShown() && !frame.logShown() && follows &&
               state->height > 0;
    };
    return step("log pane close button", std::move(start), std::move(passed));
}

// Spec §2: shown again, the pane has the height it had when it was hidden.
SelfTestStep logPaneHeightStep(SelfTest* test, const std::shared_ptr<LogPaneState>& state) {
    auto start = [test] { test->frame().execute(Command::LogPane); };
    auto passed = [test, state](std::string& seen) {
        const wxAuiPaneInfo& pane = test->frame().aui().GetPane("log");
        seen = std::format("pane shown {}, height {} (was {})", pane.IsShown(), pane.rect.height,
                           state->height);
        return pane.IsShown() && pane.rect.height == state->height;
    };
    return step("log pane height kept", std::move(start), std::move(passed));
}

constexpr std::size_t LOG_BURST = LOG_STORE_RECORDS + 10;

// Spec §3.2: more lines than the store keeps while the pane is hidden; the hidden refresh resets
// the mirror (the gap), and showing the pane rebuilds the list from it.
SelfTestStep logPaneBurstStep(SelfTest* test) {
    auto start = [test] {
        MainFrame& frame = test->frame();
        frame.setLogShown(false);
        for (std::size_t i = 0; i < LOG_BURST; ++i) {
            DVV_LOG_INFO("selftest burst {}", i);
        }
        // After the refresh the first line posted (FIFO), so the hidden pane's mirror takes the
        // burst first.
        frame.CallAfter([&frame] { frame.setLogShown(true); });
    };
    auto passed = [test](std::string& seen) {
        const MainFrame& frame = test->frame();
        const LogList& list = frame.logPanel().list();
        const std::size_t rows = list.rowCount();
        const wxString last = rows > 0 ? list.cellText(rows - 1, 2) : wxString();
        bool early = false;  // "selftest burst 9" was dropped with the other first ten
        for (std::size_t row = 0; row < rows && !early; ++row) {
            early = list.cellText(row, 2) == "selftest burst 9";
        }
        seen = std::format("pane shown {}, {} list rows, last '{}', 'selftest burst 9' listed {}",
                           frame.logShown(), rows, last.utf8_string(), early);
        return frame.logShown() && rows == LOG_STORE_RECORDS && last == "selftest burst 5009" &&
               !early;
    };
    return step("log pane after a burst", std::move(start), std::move(passed));
}

// The last smoke step: the pane is shown when the window closes, so the close saves
// gui.showLogPane = true into the selftest's own config (App::OnExit checks the file).
SelfTestStep logPaneAtCloseStep(SelfTest* test) {
    auto start = [test] { test->frame().setLogShown(true); };
    auto passed = [test](std::string& seen) {
        seen = std::format("pane shown {}, config gui.showLogPane {}", test->frame().logShown(),
                           test->app().config().gui.showLogPane);
        return test->frame().logShown();
    };
    return step("log pane shown at close", std::move(start), std::move(passed));
}

// --- the Log list (Log pane and Settings design §3.2, §4.1) ---

constexpr std::size_t LOG_LIST_FILL = LOG_STORE_RECORDS;
constexpr std::size_t LOG_LIST_MORE = 50;

std::string_view logListKindName(LogListKind kind) {
    return kind == LogListKind::ListCtrl ? "ListCtrl" : "DataView";
}

// The list shows the mirror without a timer: its rows are the mirror's, and row 0's Level cell
// comes from the mirror's first record. The pane is shown first (it starts hidden).
SelfTestStep logListFillsStep(SelfTest* test) {
    auto start = [test] { test->frame().setLogShown(true); };
    auto passed = [test](std::string& seen) {
        const LogPanel& panel = test->frame().logPanel();
        const LogMirror& mirror = panel.mirror();
        const std::size_t rows = panel.list().rowCount();
        const std::string level = rows > 0 ? panel.list().cellText(0, 1).utf8_string() : "";
        const std::string_view expected =
            mirror.size() > 0 ? dvv::logLevelName(mirror.at(0).level) : std::string_view{};
        seen = std::format("{} list rows, {} mirror records, row 0 level '{}' (mirror '{}')", rows,
                           mirror.size(), level, expected);
        return rows > 0 && rows == mirror.size() && level == expected;
    };
    return step("log list fills", std::move(start), std::move(passed));
}

struct LogListState {
    std::vector<std::uint64_t> selected;  // the sequences of the selected rows that stay
    std::uint64_t top = 0;                // the sequence of the top row
    std::uint64_t first = 0;              // the mirror's first sequence before the 50 more lines
};

// The list of `kind` replaces the pane's list (shown), then Info lines fill the mirror to 5000.
SelfTestStep logListFillStep(SelfTest* test, LogListKind kind) {
    auto start = [test, kind] {
        test->frame().setLogShown(true);
        test->frame().logPanel().recreateList(kind);
        for (std::size_t i = 0; i < LOG_LIST_FILL; ++i) {
            DVV_LOG_INFO("selftest fill {}", i);
        }
    };
    auto passed = [test](std::string& seen) {
        const LogPanel& panel = test->frame().logPanel();
        const std::uint64_t next = logNext(test->app());
        seen = std::format("{} list rows, {} mirror records, mirror next {} (store {})",
                           panel.list().rowCount(), panel.mirror().size(), panel.mirror().next(),
                           next);
        return panel.mirror().size() == LOG_LIST_FILL && panel.mirror().next() == next &&
               panel.list().rowCount() == LOG_LIST_FILL;
    };
    return step(std::format("log list fill ({})", logListKindName(kind)), std::move(start),
                std::move(passed));
}

// Spec §4.1: with the mirror full, 50 more lines drop 50 rows at the front; the selection and the
// top row stay on their records, and a selected row that was dropped leaves the selection. The
// rows are past the 50 dropped ones (rows 10, 20 and 5 of the plan would all be dropped).
SelfTestStep logListKeepsPlaceStep(SelfTest* test, LogListKind kind) {
    auto state = std::make_shared<LogListState>();
    auto start = [test, state] {
        LogPanel& panel = test->frame().logPanel();
        const LogMirror& mirror = panel.mirror();
        constexpr std::array<std::size_t, 3> SELECTED{10, 110, 120};
        panel.list().selectRows(SELECTED);
        panel.list().scrollToTop(105);
        state->selected = {mirror.at(110).sequence, mirror.at(120).sequence};
        state->top = mirror.at(105).sequence;
        state->first = mirror.at(0).sequence;
        for (std::size_t i = 0; i < LOG_LIST_MORE; ++i) {
            DVV_LOG_INFO("selftest more {}", i);
        }
    };
    auto passed = [test, state, kind](std::string& seen) {
        const LogPanel& panel = test->frame().logPanel();
        const LogMirror& mirror = panel.mirror();
        const std::uint64_t first = mirror.size() > 0 ? mirror.at(0).sequence : 0;
        const std::vector<std::uint64_t> selected = panel.list().selectedSequences();
        const ListPlace place = panel.list().place();
        const auto text = [](const std::vector<std::uint64_t>& values) {
            std::string out;
            for (const std::uint64_t value : values) {
                out += std::format("{}{}", out.empty() ? "" : " ", value);
            }
            return out;
        };
        seen = std::format(
            "{} list: first sequence {} (was {}), selected [{}] (want [{}]), top {} (want {}), "
            "tail {}",
            logListKindName(kind), first, state->first, text(selected), text(state->selected),
            place.top ? std::format("{}", *place.top) : "none", state->top, place.tail);
        return panel.list().kind() == kind && first >= state->first + LOG_LIST_MORE &&
               selected == state->selected && place.top == state->top;
    };
    return step(std::format("log list keeps its place ({})", logListKindName(kind)),
                std::move(start), std::move(passed));
}

constexpr std::size_t LOG_LIST_FOLLOW = 200;
constexpr std::size_t LOG_LIST_FOLLOW_TOP = 1000;

struct LogFollowState {
    std::uint64_t next = 0;            // the store's next sequence after the step's lines
    std::optional<std::uint64_t> top;  // the top row's sequence before them
    bool before = false;               // the last row was in view before them
};

// What the follow steps show of the list.
std::string logFollowText(const LogPanel& panel, const LogFollowState& state) {
    const LogList& list = panel.list();
    const LogMirror& mirror = panel.mirror();
    const ListPlace place = list.place();
    const std::size_t rows = list.rowCount();
    return std::format(
        "{} list: {} rows ({} mirror records), mirror next {} (want {}), last row in view {} "
        "(before {}), last row '{}', top {} (was {})",
        logListKindName(list.kind()), rows, mirror.size(), mirror.next(), state.next,
        list.lastRowVisible(), state.before,
        rows > 0 ? list.cellText(rows - 1, 2).utf8_string() : std::string(),
        place.top ? std::format("{}", *place.top) : "none",
        state.top ? std::format("{}", *state.top) : "none");
}

// Every line logged up to state.next is in the list, and the last row is the mirror's newest.
bool logFollowArrived(const LogPanel& panel, const LogFollowState& state) {
    return state.next > 0 && panel.mirror().next() >= state.next &&
           panel.list().rowCount() == panel.mirror().size();
}

// Spec §4.1: scrolled up, the list keeps its top record while lines arrive (and stops following).
SelfTestStep logListStaysStep(SelfTest* test, LogListKind kind) {
    auto state = std::make_shared<LogFollowState>();
    auto start = [test, state] {
        LogList& list = test->frame().logPanel().list();
        list.scrollToTop(LOG_LIST_FOLLOW_TOP);
        state->top = list.place().top;
        state->before = list.lastRowVisible();
        for (std::size_t i = 0; i < LOG_LIST_FOLLOW; ++i) {
            DVV_LOG_INFO("selftest stays {}", i);
        }
        state->next = logNext(test->app());
    };
    auto passed = [test, state, kind](std::string& seen) {
        const LogPanel& panel = test->frame().logPanel();
        seen = logFollowText(panel, *state);
        return panel.list().kind() == kind && logFollowArrived(panel, *state) && !state->before &&
               state->top && panel.list().place().top == state->top &&
               !panel.list().lastRowVisible();
    };
    return step(std::format("log list stays scrolled up ({})", logListKindName(kind)),
                std::move(start), std::move(passed));
}

// Spec §4.1: at the end, the list follows: after 200 more lines the last row, the newest record, is
// in view. With `scroll` the step scrolls back to the end first; without it the list is where the
// step before left it (just shown), read a timer tick later, so a scroll the control does on its
// next layout (wxGTK's scroll_to_cell after a model reset) has happened.
SelfTestStep logListFollowsStep(SelfTest* test, LogListKind kind, bool scroll) {
    auto state = std::make_shared<LogFollowState>();
    auto start = [test, state, scroll] {
        LogList& list = test->frame().logPanel().list();
        if (scroll && list.rowCount() > 0) {
            list.scrollToTop(list.rowCount() - 1);  // as near as the end allows: the end
        }
        state->before = list.lastRowVisible();
        for (std::size_t i = 0; i < LOG_LIST_FOLLOW; ++i) {
            DVV_LOG_INFO("selftest follows {}", i);
        }
        state->next = logNext(test->app());
    };
    auto passed = [test, state, kind](std::string& seen) {
        const LogPanel& panel = test->frame().logPanel();
        seen = logFollowText(panel, *state);
        return panel.list().kind() == kind && logFollowArrived(panel, *state) && state->before &&
               panel.list().lastRowVisible();
    };
    return step(std::format("log list follows {} ({})", scroll ? "at its end" : "once shown",
                            logListKindName(kind)),
                std::move(start), std::move(passed));
}

// Spec §3.2, §4.1: hidden while lines arrive, then shown: the list is rebuilt from the mirror with
// its last row in view (polled: a control may scroll on its next layout).
SelfTestStep logListShowsEndStep(SelfTest* test, LogListKind kind) {
    auto state = std::make_shared<LogFollowState>();
    auto start = [test, state] {
        MainFrame& frame = test->frame();
        state->before = frame.logPanel().list().lastRowVisible();  // shown, before the hide
        frame.setLogShown(false);
        for (std::size_t i = 0; i < LOG_LIST_FOLLOW; ++i) {
            DVV_LOG_INFO("selftest hidden {}", i);
        }
        state->next = logNext(test->app());
        // After the refresh these lines posted (FIFO): shown with them in the mirror.
        frame.CallAfter([&frame] { frame.setLogShown(true); });
    };
    auto passed = [test, state, kind](std::string& seen) {
        const LogPanel& panel = test->frame().logPanel();
        seen = std::format("pane shown {}, {}", test->frame().logShown(),
                           logFollowText(panel, *state));
        return test->frame().logShown() && panel.list().kind() == kind &&
               logFollowArrived(panel, *state) && panel.list().lastRowVisible();
    };
    return step(std::format("log list shown at its end ({})", logListKindName(kind)),
                std::move(start), std::move(passed));
}

// The keep-its-place and follow steps on the other adapter first, then on the native one, which
// the pane keeps for the steps after them.
void addLogListSteps(std::vector<SelfTestStep>& steps, SelfTest* test) {
    steps.push_back(logListFillsStep(test));
    const LogListKind native = nativeLogListKind();
    const LogListKind other =
        native == LogListKind::ListCtrl ? LogListKind::DataView : LogListKind::ListCtrl;
    for (const LogListKind kind : {other, native}) {
        steps.push_back(logListFillStep(test, kind));
        steps.push_back(logListKeepsPlaceStep(test, kind));
        steps.push_back(logListStaysStep(test, kind));
        steps.push_back(logListFollowsStep(test, kind, true));
        steps.push_back(logListShowsEndStep(test, kind));
        steps.push_back(logListFollowsStep(test, kind, false));
    }
}

// --- the Edit menu, copy and save (Log pane and Settings design §4.1 to §4.4) ---

// Through the frame's wxEVT_CHAR_HOOK handler, as a real key press reaches it first.
// Returns whether a handler took it (one that called Skip() did not).
bool sendKey(wxFrame& frame, int keycode, bool shift = false) {
    wxKeyEvent event(wxEVT_CHAR_HOOK);
    event.SetEventObject(&frame);
    event.m_keyCode = keycode;
    if (keycode > 0 && keycode < 128) {
        event.m_uniChar = static_cast<wxChar>(keycode);
    }
    event.SetShiftDown(shift);
    return injectInput(*frame.GetEventHandler(), event);
}

// A menu item of the frame (the Edit items on Windows and Linux, or on macOS when the responder
// chain did not handle it): the frame's wxEVT_MENU handler.
bool sendMenu(wxFrame& frame, int id) {
    wxCommandEvent event(wxEVT_MENU, id);
    event.SetEventObject(&frame);
    return injectInput(*frame.GetEventHandler(), event);
}

// A file's bytes; nullopt when it cannot be read.
std::optional<std::string> fileText(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The copy/save text of the mirror rows `rows` (each row's record by its sequence).
std::string mirrorRowsText(const LogMirror& mirror, std::span<const std::size_t> rows) {
    std::vector<LogEntry> entries;
    for (const std::size_t row : rows) {
        const std::uint64_t sequence = mirror.at(row).sequence;
        for (LogEntry& entry : mirror.range(sequence, sequence)) {
            entries.push_back(std::move(entry));
        }
    }
    return logRecordsText(entries);
}

// The copy/save text of every record the mirror holds now.
std::string mirrorText(const LogMirror& mirror) {
    if (mirror.size() == 0) {
        return {};
    }
    return logRecordsText(
        mirror.range(mirror.at(0).sequence, mirror.at(mirror.size() - 1).sequence));
}

struct EditState {
    bool consumed = false;                 // keeps keys: the frame's char hook took the Space
    bool playing = false;                  // keeps keys: playing before the Space
    std::int64_t position = -1;            // keeps keys: the position before the Space
    std::optional<std::string> copied;     // the clipboard hook's text
    std::string expected;                  // the text a copy or a save should give
    std::filesystem::path file;            // the file a save step chose
    std::optional<std::string> directory;  // the save dialog's start folder (remembers the folder)
    std::optional<std::string> message;    // the message hook's text and extended text
    std::uintmax_t logSize = 0;            // over the live log: rawreel.log's size before
    std::uint64_t logNext = 0;             // write failure: the store's next sequence before
};

// Spec §4.1: the focused Log list keeps its keys; Space does not reach playback.
SelfTestStep logListKeysStep(SelfTest* test, const std::shared_ptr<EditState>& state) {
    auto start = [test, state] {
        MainFrame& frame = test->frame();
        frame.setLogShown(true);
        const SessionStatus status = frame.session().status();
        state->playing = status.player.playing;
        state->position = status.player.position;
        frame.setFocusForTest(frame.logPanel().list().control());
        state->consumed = sendKey(frame, WXK_SPACE);
        frame.setFocusForTest(nullptr);
    };
    auto passed = [test, state](std::string& seen) {
        const SessionStatus status = test->frame().session().status();
        seen = std::format(
            "Space taken by the frame {}, playing {} (was {}), position {} (was {}), {} streams",
            state->consumed, status.player.playing, state->playing, status.player.position,
            state->position, status.streams);
        return status.streams > 0 && !state->consumed && !state->playing &&
               !status.player.playing && status.player.position == state->position;
    };
    return step("log list keeps keys", std::move(start), std::move(passed));
}

// Spec §4.1: only the list control keeps its keys. The shown pane's own panel (which can hold the
// native focus with nothing visibly focused) does not: Space reaches the frame and plays.
SelfTestStep logPanelKeysStep(SelfTest* test, const std::shared_ptr<EditState>& state) {
    auto start = [test, state] {
        MainFrame& frame = test->frame();
        frame.setLogShown(true);
        const SessionStatus status = frame.session().status();
        state->playing = status.player.playing;
        state->position = status.player.position;
        frame.setFocusForTest(&frame.logPanel());
        state->consumed = sendKey(frame, WXK_SPACE);
        frame.setFocusForTest(nullptr);
    };
    auto passed = [test, state](std::string& seen) {
        const SessionStatus status = test->frame().session().status();
        seen = std::format(
            "pane shown {}, Space taken by the frame {}, playing {} (was {}), position {} (was {})",
            test->frame().logShown(), state->consumed, status.player.playing, state->playing,
            status.player.position, state->position);
        // Played: playing now, or already past its start (a 40-frame stream may have ended).
        return test->frame().logShown() && state->consumed && !state->playing &&
               (status.player.playing || status.player.position != state->position);
    };
    return step("log pane panel keeps no keys", std::move(start), std::move(passed));
}

// Back to paused at frame 0 for the steps after.
SelfTestStep pauseAtFirstStep(SelfTest* test) {
    auto start = [test] {
        MainFrame& frame = test->frame();
        if (frame.session().status().player.playing) {
            frame.execute(Command::PlayPause);
        }
        frame.execute(Command::First);
    };
    auto passed = [test](std::string& seen) {
        const SessionStatus status = test->frame().session().status();
        seen =
            std::format("playing {}, position {}", status.player.playing, status.player.position);
        return !status.player.playing && status.player.position == 0;
    };
    return step("paused at the first frame", std::move(start), std::move(passed));
}

// Spec §4.2, §4.3: Select All, then Edit -> Copy with the Log list focused puts every selected
// record on the clipboard, as rawreel.log writes it.
SelfTestStep logCopyStep(SelfTest* test, const std::shared_ptr<EditState>& state) {
    auto start = [test, state] {
        MainFrame& frame = test->frame();
        LogPanel& panel = frame.logPanel();
        panel.list().selectAll();
        state->expected = mirrorText(panel.mirror());
        state->copied.reset();
        dialogHooks().clipboard = [state](std::string text) { state->copied = std::move(text); };
        frame.setFocusForTest(panel.list().control());
        (void)sendMenu(frame, wxID_COPY);
        frame.setFocusForTest(nullptr);
    };
    auto passed = [state](std::string& seen) {
        seen = std::format("copied {} bytes (want {})",
                           state->copied ? std::to_string(state->copied->size()) : "nothing",
                           state->expected.size());
        return !state->expected.empty() && state->copied == state->expected;
    };
    return step("log copy", std::move(start), std::move(passed));
}

// Spec §4.2: Edit -> Select All and Copy in the read-only Info pane copy its text.
SelfTestStep infoCopyStep(SelfTest* test, const std::shared_ptr<EditState>& state) {
    auto start = [test, state] {
        MainFrame& frame = test->frame();
        state->expected = frame.infoPanel().text().utf8_string();
        state->copied.reset();
        dialogHooks().clipboard = [state](std::string text) { state->copied = std::move(text); };
        frame.setFocusForTest(&frame.infoPanel().textControl());
        (void)sendMenu(frame, wxID_SELECTALL);
        (void)sendMenu(frame, wxID_COPY);
        frame.setFocusForTest(nullptr);
    };
    auto passed = [state](std::string& seen) {
        seen = std::format("copied '{}' (want '{}')", state->copied.value_or("nothing"),
                           state->expected);
        return !state->expected.empty() && state->copied == state->expected;
    };
    return step("info copy", std::move(start), std::move(passed));
}

// The selftest config folder (spec §7.1), where the save steps write.
std::filesystem::path saveFolder(const SelfTest* test) {
    return test->app().selftestConfigFolder();
}

// The saved file holds the expected text.
bool savedAsExpected(const EditState& state, std::string& seen) {
    const std::optional<std::string> text = fileText(state.file);
    seen = std::format("'{}': {} bytes (want {})", dvv::media::utf8FromPath(state.file),
                       text ? std::to_string(text->size()) : "not readable", state.expected.size());
    return !state.expected.empty() && text == state.expected;
}

// Spec §4.3, §4.4: File -> Save Log... writes every record of the mirror, as it was when the
// command ran, to the file the dialog returned.
SelfTestStep saveLogStep(SelfTest* test, const std::shared_ptr<EditState>& state) {
    auto start = [test, state] {
        state->file = saveFolder(test) / "saved.txt";
        state->expected = mirrorText(test->frame().logPanel().mirror());
        dialogHooks().saveLog = [state](wxFileDialog& dialog) {
            dialog.SetPath(wxString::FromUTF8(dvv::media::utf8FromPath(state->file)));
        };
        test->frame().execute(Command::SaveLog);
    };
    auto passed = [test, state](std::string& seen) {
        return !saveFolder(test).empty() && savedAsExpected(*state, seen);
    };
    return step("save log", std::move(start), std::move(passed));
}

// Spec §4.4: the next save dialog starts in the folder of the previous log save.
SelfTestStep saveLogFolderStep(SelfTest* test, const std::shared_ptr<EditState>& state) {
    auto start = [test, state] {
        state->file = saveFolder(test) / "saved2.txt";
        state->directory.reset();
        state->expected = mirrorText(test->frame().logPanel().mirror());
        dialogHooks().saveLog = [state](wxFileDialog& dialog) {
            // The folder the dialog was created with (wxGTK's GetDirectory() reads its chooser).
            state->directory = dialog.wxFileDialogBase::GetDirectory().utf8_string();
            dialog.SetPath(wxString::FromUTF8(dvv::media::utf8FromPath(state->file)));
        };
        test->frame().execute(Command::SaveLog);
    };
    auto passed = [test, state](std::string& seen) {
        const std::filesystem::path folder = saveFolder(test);
        std::error_code error;
        const bool same = state->directory && !folder.empty() &&
                          std::filesystem::equivalent(dvv::media::pathFromUtf8(*state->directory),
                                                      folder, error) &&
                          !error;
        std::string saved;
        const bool written = savedAsExpected(*state, saved);
        seen = std::format("dialog folder '{}' is '{}' {}; {}", state->directory.value_or("none"),
                           dvv::media::utf8FromPath(folder), same, saved);
        return same && written;
    };
    return step("save log remembers the folder", std::move(start), std::move(passed));
}

// Spec §4.3: File -> Save Selected Lines... writes exactly the selected records (rows 1 and 3, not
// the row between them).
SelfTestStep saveSelectedStep(SelfTest* test, const std::shared_ptr<EditState>& state) {
    auto start = [test, state] {
        LogPanel& panel = test->frame().logPanel();
        constexpr std::array<std::size_t, 2> ROWS{1, 3};
        panel.list().selectRows(ROWS);
        state->file = saveFolder(test) / "selected.txt";
        state->expected = mirrorRowsText(panel.mirror(), ROWS);
        dialogHooks().saveLog = [state](wxFileDialog& dialog) {
            dialog.SetPath(wxString::FromUTF8(dvv::media::utf8FromPath(state->file)));
        };
        test->frame().execute(Command::SaveSelectedLines);
    };
    auto passed = [test, state](std::string& seen) {
        return !saveFolder(test).empty() && savedAsExpected(*state, seen);
    };
    return step("save selected lines", std::move(start), std::move(passed));
}

// Spec §4.4: the live rawreel.log is refused with a message, and the file is not replaced.
SelfTestStep saveOverLiveLogStep(SelfTest* test, const std::shared_ptr<EditState>& state) {
    auto start = [test, state] {
        state->file = test->app().logFolderInUse() / std::string(LOG_FILE_NAME);
        std::error_code error;
        state->logSize = std::filesystem::file_size(state->file, error);
        state->message.reset();
        dialogHooks().saveLog = [state](wxFileDialog& dialog) {
            dialog.SetPath(wxString::FromUTF8(dvv::media::utf8FromPath(state->file)));
        };
        dialogHooks().message = [state](wxMessageDialog& dialog) {
            state->message =
                (dialog.GetMessage() + " " + dialog.GetExtendedMessage()).utf8_string();
        };
        test->frame().execute(Command::SaveLog);
    };
    auto passed = [test, state](std::string& seen) {
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(state->file, error);
        seen = std::format("log folder '{}', message '{}', rawreel.log {} bytes (was {})",
                           dvv::media::utf8FromPath(test->app().logFolderInUse()),
                           state->message.value_or("none"), error ? 0 : size, state->logSize);
        return !test->app().logFolderInUse().empty() && state->logSize > 0 && !error &&
               size >= state->logSize && state->message &&
               state->message->find(LOG_FILE_NAME) != std::string::npos;
    };
    return step("save over the live log", std::move(start), std::move(passed));
}

// Spec §4.4: a backup name that does not exist yet (rawreel.log.3; another case on Windows and
// macOS) is refused too, since a rotation would overwrite it; no file is created.
SelfTestStep saveOverLogBackupStep(SelfTest* test, const std::shared_ptr<EditState>& state) {
    auto start = [test, state] {
#if defined(_WIN32) || defined(__APPLE__)
        const std::string name = std::format("{}.{}", "RawReel.LOG", LOG_FILE_BACKUPS);
#else
        const std::string name = std::format("{}.{}", LOG_FILE_NAME, LOG_FILE_BACKUPS);
#endif
        state->file = test->app().logFolderInUse() / name;
        state->message.reset();
        dialogHooks().saveLog = [state](wxFileDialog& dialog) {
            dialog.SetPath(wxString::FromUTF8(dvv::media::utf8FromPath(state->file)));
        };
        dialogHooks().message = [state](wxMessageDialog& dialog) {
            state->message =
                (dialog.GetMessage() + " " + dialog.GetExtendedMessage()).utf8_string();
        };
        test->frame().execute(Command::SaveLog);
    };
    auto passed = [test, state](std::string& seen) {
        std::error_code error;
        const bool exists = std::filesystem::exists(state->file, error);
        seen = std::format("'{}' {}, message '{}'", dvv::media::utf8FromPath(state->file),
                           exists ? "exists" : "absent", state->message.value_or("none"));
        return !test->app().logFolderInUse().empty() && !error && !exists && state->message &&
               state->message->find(LOG_FILE_NAME) != std::string::npos;
    };
    return step("save over a log backup", std::move(start), std::move(passed));
}

// Spec §4.4: a write that fails shows a message naming the path and logs one warning.
SelfTestStep saveFailureStep(SelfTest* test, const std::shared_ptr<EditState>& state) {
    auto start = [test, state] {
        state->file = saveFolder(test) / "no-such-folder" / "saved.txt";
        state->message.reset();
        state->logNext = logNext(test->app());
        dialogHooks().saveLog = [state](wxFileDialog& dialog) {
            dialog.SetPath(wxString::FromUTF8(dvv::media::utf8FromPath(state->file)));
        };
        dialogHooks().message = [state](wxMessageDialog& dialog) {
            state->message =
                (dialog.GetMessage() + " " + dialog.GetExtendedMessage()).utf8_string();
        };
        test->frame().execute(Command::SaveLog);
    };
    auto passed = [test, state](std::string& seen) {
        const std::string path = dvv::media::utf8FromPath(state->file);
        bool warned = false;
        for (const LogEntry& entry : test->app().logStore()->entries()) {
            warned =
                warned || (entry.sequence >= state->logNext && entry.level == dvv::LogLevel::Warn &&
                           (entry.message.find("not saved") != std::string::npos ||
                            entry.message.find(path) != std::string::npos));
        }
        seen = std::format("message '{}', a warning logged {}", state->message.value_or("none"),
                           warned);
        return state->message && state->message->find(path) != std::string::npos && warned;
    };
    return step("save log write failure", std::move(start), std::move(passed));
}

// A hidden Log pane can keep the native focus (wxAUI hides the pane, not the focus): then the
// playback keys work (Home, which only goes to frame 0, reaches the frame's char hook).
SelfTestStep hiddenLogKeysStep(SelfTest* test, const std::shared_ptr<EditState>& state) {
    auto start = [test, state] {
        MainFrame& frame = test->frame();
        frame.setLogShown(false);
        frame.setFocusForTest(frame.logPanel().list().control());
        state->consumed = sendKey(frame, WXK_HOME);
        frame.setFocusForTest(nullptr);
    };
    auto passed = [test, state](std::string& seen) {
        seen = std::format("pane shown {}, Home taken by the frame {}", test->frame().logShown(),
                           state->consumed);
        return !test->frame().logShown() && state->consumed;
    };
    return step("hidden log pane keeps no keys", std::move(start), std::move(passed));
}

void addEditSteps(std::vector<SelfTestStep>& steps, SelfTest* test) {
    const auto state = std::make_shared<EditState>();
    steps.push_back(logListKeysStep(test, state));
    steps.push_back(logPanelKeysStep(test, state));
    steps.push_back(pauseAtFirstStep(test));
    steps.push_back(logCopyStep(test, state));
    steps.push_back(infoCopyStep(test, state));
    steps.push_back(saveLogStep(test, state));
    steps.push_back(saveLogFolderStep(test, state));
    steps.push_back(saveSelectedStep(test, state));
    steps.push_back(saveOverLiveLogStep(test, state));
    steps.push_back(saveOverLogBackupStep(test, state));
    steps.push_back(saveFailureStep(test, state));
    steps.push_back(hiddenLogKeysStep(test, state));
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
        const std::array<wxString, 5> texts = test->frame().statusTexts();
        seen = std::format("status bar '{}' '{}' '{}' '{}'", texts[0].utf8_string(),
                           texts[1].utf8_string(), texts[2].utf8_string(), texts[3].utf8_string());
        // D5R §2.2: an SDR stream's mapping field.
        return texts[0].StartsWith("frame ") && texts[1].EndsWith("fps") &&
               texts[3] == "SDR, no tone mapping";
    };
    return step("status bar", {}, std::move(passed));
}

// Spec §6.3: the first rawreel-gui owns the settings; a second claim of the same config, in this
// process too, fails while the App holds its own.
SelfTestStep settingsOwnerStep(SelfTest* test) {
    auto passed = [test](std::string& seen) {
        const App& app = test->app();
        const bool owner = app.ownsSettings();
        const bool second_refused = SettingsClaim::tryClaim(app.configPath()) == nullptr;
        seen = std::format("owns the settings {}, a second claim of '{}' refused {}", owner,
                           dvv::media::utf8FromPath(app.configPath()), second_refused);
        return owner && second_refused;
    };
    return step("settings owner", {}, std::move(passed));
}

// Spec §7.1: a --selftest run without --config uses <temp>/rawreel-selftest-config-<pid>-<random>/,
// whose rawreel.json sends the log file to its logs/ and turns the system log off.
SelfTestStep selftestConfigStep(SelfTest* test) {
    auto passed = [test](std::string& seen) {
        const App& app = test->app();
        const std::filesystem::path& folder = app.selftestConfigFolder();
        const std::string prefix = std::format("rawreel-selftest-config-{}-", wxGetProcessId());
        std::error_code error;
        const bool named =
            !folder.empty() &&
            std::filesystem::equivalent(folder.parent_path(),
                                        std::filesystem::temp_directory_path(), error) &&
            !error && dvv::media::utf8FromPath(folder.filename()).starts_with(prefix);
        const bool in_folder =
            named && !app.configPath().empty() &&
            std::filesystem::equivalent(app.configPath().parent_path(), folder, error) && !error;
        const std::filesystem::path logs = app.logFolderInUse();
        const bool logs_used =
            !logs.empty() && std::filesystem::equivalent(logs, folder / "logs", error) && !error;
        bool private_folders = true;  // 0700 on POSIX
#if !defined(_WIN32)
        for (const std::filesystem::path& made : {folder, folder / "logs"}) {
            const std::filesystem::file_status status =
                std::filesystem::symlink_status(made, error);
            private_folders = private_folders && !error &&
                              status.type() == std::filesystem::file_type::directory &&
                              status.permissions() == std::filesystem::perms::owner_all;
        }
#endif
        seen = std::format(
            "config '{}' in '{}' {}, log folder '{}' its logs/ {}, 0700 folders {}, system log {}",
            dvv::media::utf8FromPath(app.configPath()), dvv::media::utf8FromPath(folder), in_folder,
            dvv::media::utf8FromPath(logs), logs_used, private_folders, app.systemLogEnabled());
        return in_folder && logs_used && private_folders && !app.systemLogEnabled();
    };
    return step("selftest config", {}, std::move(passed));
}

// Help -> Diagnostics writes the store counters to the log, which reaches the Log pane, and shows
// the pane (hidden here first, spec §2).
SelfTestStep diagnosticsStep(SelfTest* test, const SharedState& state) {
    auto start = [test, state] {
        state->logNext = logNext(test->app());
        test->frame().setLogShown(false);
        test->frame().execute(Command::Diagnostics);
    };
    auto passed = [test, state](std::string& seen) {
        const bool found = loggedSince(test->app(), state->logNext).contains("store: frames");
        const bool shown = test->frame().logShown();
        seen = std::format("'store: frames' logged {}, Log pane shown {}", found, shown);
        return found && shown;
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
        state->logNext = logNext(test->app());
        test->frame().execute(Command::GpuInfo);  // selftest: to the log, no dialog
    };
    auto passed = [test, state](std::string& seen) {
        const MainFrame& frame = test->frame();
        const wxMenuBar* bar = frame.GetMenuBar();
        if (bar == nullptr) {
            seen = "no menu bar";
            return false;
        }
        // Log pane and Settings design §4.2: Edit between File and Playback.
        const std::array<wxString, 5> names{"File", "Edit", "Playback", "View", "Help"};
        bool menus = bar->GetMenuCount() == names.size();
        for (std::size_t i = 0; menus && i < names.size(); ++i) {
            menus = bar->GetMenuLabelText(i) == names[i];
        }
        // Every Edit item; Delete has no key (it stays with the focused control).
        bool edit = menus;
        for (const int id :
             {wxID_UNDO, wxID_REDO, wxID_CUT, wxID_COPY, wxID_PASTE, wxID_CLEAR, wxID_SELECTALL}) {
            const wxMenuItem* item = edit ? bar->GetMenu(1)->FindItem(id) : nullptr;
            // GetAccel() returns a new copy, or null without a key.
            const std::unique_ptr<wxAcceleratorEntry> key(item != nullptr ? item->GetAccel()
                                                                          : nullptr);
            edit = item != nullptr && (id == wxID_CLEAR) == (key == nullptr);
        }
        const bool save_in_file =
            menus && bar->GetMenu(0)->FindItem(menuId(Command::SaveLog)) != nullptr &&
            bar->GetMenu(0)->FindItem(menuId(Command::SaveSelectedLines)) != nullptr;
        // Log pane and Settings design §5.1: wxID_PREFERENCES in File (wxOSX shows it in the
        // application menu), in Edit on GTK.
#if defined(__WXGTK__)
        constexpr std::size_t SETTINGS_MENU = 1;
#else
        constexpr std::size_t SETTINGS_MENU = 0;
#endif
        const CommandInfo& settings = commandTable()[static_cast<std::size_t>(Command::Settings)];
        const wxMenuItem* settings_item =
            menus ? bar->GetMenu(SETTINGS_MENU)->FindItem(wxID_PREFERENCES) : nullptr;
        const bool settings_ok =
            settings_item != nullptr &&
            settings_item->GetItemLabel() == wxString::FromUTF8(menuLabel(settings));
        const CommandInfo& open = commandTable()[static_cast<std::size_t>(Command::Open)];
        const wxMenuItem* open_item =
            bar->FindItem(COMMAND_ID_BASE + static_cast<int>(Command::Open));
        const bool open_label = open_item != nullptr &&
                                open_item->GetItemLabel() == wxString::FromUTF8(menuLabel(open));
        const bool reopen_in_view =
            menus && bar->GetMenu(3)->FindItem(COMMAND_ID_BASE +
                                               static_cast<int>(Command::Reopen)) != nullptr;
        const std::string line = firstGpuInfoLine(frame);
        const bool gpu_info =
            !line.empty() && loggedSince(test->app(), state->logNext).contains(line);
        seen = std::format(
            "{} menus in order {}, Edit items {}, Save items in File {}, Settings item '{}' ok {}, "
            "Open label '{}' ok {}, Reopen in View {}, GPU info line '{}' logged {}",
            bar->GetMenuCount(), menus, edit, save_in_file,
            settings_item != nullptr ? settings_item->GetItemLabel().utf8_string() : "-",
            settings_ok, open_item != nullptr ? open_item->GetItemLabel().utf8_string() : "-",
            open_label, reopen_in_view, line, gpu_info);
        return menus && edit && save_in_file && settings_ok && open_label && reopen_in_view &&
               gpu_info;
    };
    return step("menus", std::move(start), std::move(passed));
}

#if defined(__WXOSX__)
// Design C5: About and Quit carry the standard ids, so wxOSX shows them in the application menu
// as "About rawreel" and "Quit rawreel" (and hides them in Help and File); the application menu's
// About opens Help -> About.
SelfTestStep appMenuStep(SelfTest* test) {
    struct Seen {
        std::uint64_t about0 = 0;
        bool hookRan = false;
    };
    auto seen_state = std::make_shared<Seen>();
    auto start = [test, seen_state] {
        MainFrame& frame = test->frame();
        seen_state->about0 = frame.aboutShown();
        dialogHooks().about = [seen_state](wxGenericAboutDialog& /*dialog*/) {
            seen_state->hookRan = true;
        };
        wxCommandEvent about(wxEVT_MENU, wxID_ABOUT);  // what the application menu's item sends
        about.SetEventObject(&frame);
        injectInput(*frame.GetEventHandler(), about);
    };
    auto passed = [test, seen_state](std::string& seen) {
        const MainFrame& frame = test->frame();
        const wxMenuBar* bar = frame.GetMenuBar();
        const wxMenu* apple = bar != nullptr ? bar->OSXGetAppleMenu() : nullptr;
        if (apple == nullptr) {
            seen = "no application menu";
            return false;
        }
        const auto label = [](const wxMenuItem* item) {
            return item != nullptr ? item->GetItemLabelText().utf8_string() : std::string("-");
        };
        const std::string about = label(apple->FindItem(wxID_ABOUT));
        const std::string quit = label(apple->FindItem(wxID_EXIT));
        const std::string settings = label(apple->FindItem(wxID_PREFERENCES));
        // The frame's own items carry the standard ids; none is left on the old ones.
        const bool ids =
            bar->FindItem(wxID_ABOUT) != nullptr && bar->FindItem(wxID_EXIT) != nullptr &&
            bar->FindItem(COMMAND_ID_BASE + static_cast<int>(Command::About)) == nullptr &&
            bar->FindItem(COMMAND_ID_BASE + static_cast<int>(Command::Quit)) == nullptr;
        const bool shown = frame.aboutShown() > seen_state->about0 && seen_state->hookRan;
        seen = std::format("application menu '{}' '{}' '{}', standard ids {}, About shown {}",
                           about, settings, quit, ids, shown);
        return about == "About rawreel" && settings == "Settings..." && quit == "Quit rawreel" &&
               ids && shown;
    };
    return step("application menu", std::move(start), std::move(passed));
}

// Review Focus 1 and 5: a window moved to a display of another backing scale keeps its size in
// points, so wx sends no size event. The test hook gives the viewport's child view another scale
// (Retina 2 <-> 1) and runs its viewDidChangeBackingProperties, Cocoa's own entry: the same
// presenter (the hook's scale lives on its child view) gets a swapchain of physicalPixelSize() of
// the unchanged point size. `restore`: the hook gives the window's own scale back.
SelfTestStep backingScaleStep(SelfTest* test, const SharedState& state, bool restore) {
    struct Probe {
        const dvv::gpu::Presenter* presenter = nullptr;
        wxSize points;
        double scale = 0;
        bool hooked = false;
    };
    auto probe = std::make_shared<Probe>();
    auto start = [test, state, probe, restore] {
        Viewport& viewport = test->frame().viewport();
        markStart(viewport, *state);
        probe->presenter = viewport.presenter();   // the hook needs it: it sets the child's scale
        probe->points = viewport.GetClientSize();  // points on wxOSX
        const double window_scale = viewport.GetContentScaleFactor();
        probe->scale = restore ? window_scale : (window_scale >= 2 ? 1.0 : 2.0);
        probe->hooked = setBackingScaleForTest(viewport, restore ? 0.0 : probe->scale);
    };
    auto passed = [test, state, probe](std::string& seen) {
        const Viewport& viewport = test->frame().viewport();
        const std::optional<dvv::gpu::PresenterStats> stats = statsOf(viewport);
        const dvv::platform::PixelSize expected =
            physicalPixelSize(probe->points.x, probe->points.y, probe->scale);
        const wxSize points = viewport.GetClientSize();
        const bool same_presenter =
            probe->presenter != nullptr && viewport.presenter() == probe->presenter;
        seen = std::format(
            "{}; hook {}, scale {}, points {}x{} (at the start {}x{}), expected extent {}x{}, "
            "same presenter {}",
            describe(viewport, *state), probe->hooked, probe->scale, points.x, points.y,
            probe->points.x, probe->points.y, expected.width, expected.height, same_presenter);
        return probe->hooked && stats && same_presenter && points == probe->points &&
               viewport.physicalClientSize() == expected && stats->presentedExtent == expected &&
               stats->swapchainsCreated > state->startSwapchains &&
               stats->framesPresented > state->startFrames;
    };
    return {.name = restore ? "backing scale restored" : "backing scale change",
            .start = std::move(start),
            .passed = std::move(passed)};
}
#endif

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
            state->logNext = logNext(test->app());
            test->frame().execute(Command::OpenUri);
        },
        [test, state](std::string& seen) {
            const bool logged = loggedSince(test->app(), state->logNext).contains("open cancelled");
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

// --- the Settings window (Log pane and Settings design §5, §7.3) --------------------------------

struct SettingsState {
    bool hookRan = false;
    bool pagesCreated = false;
    bool shown = false;
    wxString configPathText;
    wxString folderText;
    wxString folderBefore;  // folder failure: the field before Choose...
    bool useDefaultEnabled = false;
    bool revealEnabled = false;
    std::filesystem::path inUse;   // logFolderInUse() before the step's action
    std::filesystem::path target;  // the folder the chooseFolder hook returns
    std::optional<std::string> message;
    std::optional<std::filesystem::path> revealed;
    bool systemOn = false;              // system log: after "on"
    std::optional<bool> savedSystemOn;  // system log: the scratch file after "on"
    std::size_t streamsBefore = 0;      // close key
};

// The selftest's rawreel.json as it is now; nullopt when it cannot be read or parsed.
std::optional<Config> scratchConfig(const App& app) {
    dvv::Result<std::string> text = readConfigText(app.configPath());
    if (!text) {
        return std::nullopt;
    }
    dvv::Result<Config> parsed = parseConfig(*text);
    if (!parsed) {
        return std::nullopt;
    }
    return std::move(*parsed);
}

bool sameFolder(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code error;
    return !a.empty() && !b.empty() && std::filesystem::equivalent(a, b, error) && !error;
}

// Settings... through its menu item (wxID_PREFERENCES, as the File, Edit or application menu sends
// it); `action` runs through the settings hook, then OK (pressOk(); Windows only). On Windows a
// dialog still open after OK (a folder that failed Validate()) is cancelled.
void runSettings(SelfTest* test, const std::shared_ptr<SettingsState>& state,
                 dvv::MoveOnlyFunction<void(SettingsWindow&)> action) {
    state->hookRan = false;
    auto shared = std::make_shared<dvv::MoveOnlyFunction<void(SettingsWindow&)>>(std::move(action));
    dialogHooks().settings = [state, shared](SettingsWindow& settings) {
        state->hookRan = true;
        (*shared)(settings);
        settings.pressOk();
        if (wxPreferencesEditor::ShownModally() && settings.shown()) {
            settings.dismiss();
        }
    };
    (void)sendMenu(test->frame(), wxID_PREFERENCES);
}

// Spec §5.1, §5.2: the window opens with both pages; the Files page names the config file, the Log
// page shows the selftest's logs/ folder, which is the default one here (Use Default disabled).
SelfTestStep settingsOpensStep(SelfTest* test, const std::shared_ptr<SettingsState>& state) {
    auto start = [test, state] {
        runSettings(test, state, [state](SettingsWindow& settings) {
            state->pagesCreated = settings.pagesCreated();
            state->shown = settings.shown();
            state->configPathText = settings.filesPage().configPathText();
            state->folderText = settings.logPage().folderText();
            state->useDefaultEnabled = settings.logPage().useDefaultEnabled();
        });
    };
    auto passed = [test, state](std::string& seen) {
        const App& app = test->app();
        const std::filesystem::path logs = app.selftestConfigFolder() / "logs";
        const bool config =
            state->configPathText.utf8_string() == dvv::media::utf8FromPath(app.configPath());
        const bool folder =
            sameFolder(dvv::media::pathFromUtf8(state->folderText.utf8_string()), logs);
        seen = std::format(
            "hook ran {}, pages {}, shown {}, settings file '{}' is the config {}, folder '{}' is "
            "logs/ {}, Use Default enabled {}",
            state->hookRan, state->pagesCreated, state->shown, state->configPathText.utf8_string(),
            config, state->folderText.utf8_string(), folder, state->useDefaultEnabled);
        return state->hookRan && state->pagesCreated && state->shown && config && folder &&
               !state->useDefaultEnabled;
    };
    return step("settings opens", std::move(start), std::move(passed));
}

// Spec §5.3: Choose... a new folder: logs2/ is created, gets rawreel.log, and the old file's last
// line names the new one; the value is saved and Use Default is enabled.
SelfTestStep settingsLogFolderStep(SelfTest* test, const std::shared_ptr<SettingsState>& state) {
    auto start = [test, state] {
        state->inUse = test->app().logFolderInUse();
        state->target = test->app().selftestConfigFolder() / "logs2";
        dialogHooks().chooseFolder = [state](wxDirDialog& dialog) {
            dialog.wxDirDialogBase::SetPath(
                wxString::FromUTF8(dvv::media::utf8FromPath(state->target)));
        };
        runSettings(test, state, [state](SettingsWindow& settings) {
            settings.logPage().chooseFolder();
            state->useDefaultEnabled = settings.logPage().useDefaultEnabled();
            state->folderText = settings.logPage().folderText();
        });
    };
    auto passed = [test, state](std::string& seen) {
        const App& app = test->app();
        const bool moved = sameFolder(app.logFolderInUse(), state->target);
        std::error_code error;
        const bool file =
            std::filesystem::exists(state->target / std::string(LOG_FILE_NAME), error) && !error;
        const std::optional<std::string> old_text =
            fileText(state->inUse / std::string(LOG_FILE_NAME));
        bool named = false;
        if (old_text) {
            const std::size_t line = old_text->rfind("log file moves to");
            named =
                line != std::string::npos &&
                old_text->find(dvv::media::utf8FromPath(state->target), line) != std::string::npos;
        }
        const std::optional<Config> saved = scratchConfig(app);
        const bool stored = saved && sameFolder(saved->log.file.folder, state->target);
        seen = std::format(
            "hook ran {}, log folder '{}' is logs2 {}, logs2/rawreel.log {}, old file names it {}, "
            "saved log.file.folder '{}' {}, field '{}', Use Default enabled {}",
            state->hookRan, dvv::media::utf8FromPath(app.logFolderInUse()), moved, file, named,
            saved ? dvv::media::utf8FromPath(saved->log.file.folder) : "not readable", stored,
            state->folderText.utf8_string(), state->useDefaultEnabled);
        return state->hookRan && moved && file && named && stored && state->useDefaultEnabled;
    };
    return step("settings log folder", std::move(start), std::move(passed));
}

// Spec §5.3: a folder whose parent does not exist is refused with a message naming it; the log
// stays where it is, and on macOS and Linux the field keeps the old value (Windows keeps the
// user's value in its still open dialog, which the step cancels).
SelfTestStep settingsFolderFailureStep(SelfTest* test,
                                       const std::shared_ptr<SettingsState>& state) {
    auto start = [test, state] {
        state->inUse = test->app().logFolderInUse();
        state->target = test->app().selftestConfigFolder() / "no-such-parent" / "logs3";
        state->message.reset();
        dialogHooks().chooseFolder = [state](wxDirDialog& dialog) {
            dialog.wxDirDialogBase::SetPath(
                wxString::FromUTF8(dvv::media::utf8FromPath(state->target)));
        };
        dialogHooks().message = [state](wxMessageDialog& dialog) {
            state->message =
                (dialog.GetMessage() + " " + dialog.GetExtendedMessage()).utf8_string();
        };
        runSettings(test, state, [state](SettingsWindow& settings) {
            state->folderBefore = settings.logPage().folderText();
            settings.logPage().chooseFolder();
            state->folderText = settings.logPage().folderText();
        });
    };
    auto passed = [test, state](std::string& seen) {
        const App& app = test->app();
        const bool kept = app.logFolderInUse() == state->inUse;
#if defined(__WXMSW__)
        const bool field = true;  // the dialog kept the user's value until it was cancelled
#else
        const bool field = state->folderText == state->folderBefore;
#endif
        const bool named =
            state->message &&
            state->message->find(dvv::media::utf8FromPath(state->target)) != std::string::npos;
        seen = std::format(
            "hook ran {}, message '{}' names the folder {}, log folder '{}' kept {}, field '{}' "
            "(was '{}') {}",
            state->hookRan, state->message.value_or("none"), named,
            dvv::media::utf8FromPath(app.logFolderInUse()), kept, state->folderText.utf8_string(),
            state->folderBefore.utf8_string(), field);
        return state->hookRan && named && kept && field;
    };
    return step("settings folder failure", std::move(start), std::move(passed));
}

// Spec §5.2: Show in Finder/Explorer, Open Folder hands the live rawreel.log to the file manager.
SelfTestStep settingsRevealStep(SelfTest* test, const std::shared_ptr<SettingsState>& state) {
    auto start = [test, state] {
        state->revealed.reset();
        dialogHooks().reveal = [state](std::filesystem::path path) {
            state->revealed = std::move(path);
        };
        runSettings(test, state, [state](SettingsWindow& settings) {
            state->revealEnabled = settings.logPage().revealEnabled();
            settings.logPage().reveal();
        });
    };
    auto passed = [test, state](std::string& seen) {
        const std::filesystem::path want =
            test->app().logFolderInUse() / std::string(LOG_FILE_NAME);
        const bool same = state->revealed && *state->revealed == want;
        seen = std::format("hook ran {}, enabled {}, revealed '{}' (want '{}') {}", state->hookRan,
                           state->revealEnabled,
                           state->revealed ? dvv::media::utf8FromPath(*state->revealed) : "none",
                           dvv::media::utf8FromPath(want), same);
        return state->hookRan && state->revealEnabled && same;
    };
    return step("settings reveal", std::move(start), std::move(passed));
}

// Spec §5.3: the system log turned on, then off (two openings, so Windows' OK applies each); the
// sink's level follows, and the file holds each value.
SelfTestStep settingsSystemLogStep(SelfTest* test, const std::shared_ptr<SettingsState>& state) {
    auto start = [test, state] {
        runSettings(test, state,
                    [](SettingsWindow& settings) { settings.logPage().setSystemLog(true); });
        state->systemOn = state->hookRan && test->app().systemLogEnabled();
        const std::optional<Config> on = scratchConfig(test->app());
        state->savedSystemOn =
            on ? std::optional<bool>(on->log.system.enabled) : std::optional<bool>();
        runSettings(test, state,
                    [](SettingsWindow& settings) { settings.logPage().setSystemLog(false); });
    };
    auto passed = [test, state](std::string& seen) {
        const std::optional<Config> saved = scratchConfig(test->app());
        const bool off_saved = saved && !saved->log.system.enabled;
        seen = std::format(
            "on: enabled {}, saved {}; off: hook ran {}, enabled {}, saved false {}",
            state->systemOn,
            state->savedSystemOn ? (*state->savedSystemOn ? "true" : "false") : "not readable",
            state->hookRan, test->app().systemLogEnabled(), off_saved);
        return state->systemOn && state->savedSystemOn == true && state->hookRan &&
               !test->app().systemLogEnabled() && off_saved;
    };
    return step("settings system log", std::move(start), std::move(passed));
}

// Spec §5.3: File reads Buffered reaches the Session (every later open) and the file.
SelfTestStep settingsFileReadsStep(SelfTest* test, const std::shared_ptr<SettingsState>& state) {
    auto start = [test, state] {
        runSettings(test, state, [](SettingsWindow& settings) {
            settings.filesPage().setFileReads(dvv::platform::FileReadMode::Buffered);
        });
    };
    auto passed = [test, state](std::string& seen) {
        const dvv::platform::FileReadMode mode = test->frame().session().fileReads();
        const std::optional<Config> saved = scratchConfig(test->app());
        const bool stored = saved && saved->io.fileReads == dvv::platform::FileReadMode::Buffered;
        seen = std::format("hook ran {}, session reads '{}', saved io.fileReads '{}'",
                           state->hookRan, fileReadsName(mode),
                           saved ? fileReadsName(saved->io.fileReads) : "not readable");
        return state->hookRan && mode == dvv::platform::FileReadMode::Buffered && stored;
    };
    return step("settings file reads", std::move(start), std::move(passed));
}

#if defined(__WXOSX__)
// Spec §5.1: ⌘W (File -> Close stream) with the Settings window in front closes that window; the
// stream stays open.
SelfTestStep settingsCloseKeyStep(SelfTest* test, const std::shared_ptr<SettingsState>& state) {
    auto start = [test, state] {
        state->streamsBefore = test->frame().streams().size();
        runSettings(test, state, [test](SettingsWindow& settings) {
            MainFrame& frame = test->frame();
            wxWindowList& controls = settings.logPage().GetChildren();
            frame.setFocusForTest(controls.empty() ? static_cast<wxWindow*>(&settings.logPage())
                                                   : controls.front());
            frame.execute(Command::CloseStreams);
            frame.setFocusForTest(nullptr);
        });
    };
    auto passed = [test, state](std::string& seen) {
        MainFrame& frame = test->frame();
        const SettingsWindow* settings = frame.settingsWindow();
        const bool shown = settings == nullptr || settings->shown();
        const std::uint32_t streams = frame.session().status().streams;
        seen = std::format("hook ran {}, Settings shown {}, {} streams (was {}), session {}",
                           state->hookRan, shown, frame.streams().size(), state->streamsBefore,
                           streams);
        return state->hookRan && !shown && state->streamsBefore == 1 &&
               frame.streams().size() == 1 && streams == 1;
    };
    return step("settings close key", std::move(start), std::move(passed));
}

// Spec §5.1: only the Settings window takes ⌘W. With the focus in another top-level window of
// the frame (a hidden stand-in for a floating Info or Log pane, a wxAuiFloatingFrame), the item
// stays File -> Close stream: the stream closes and that window is not closed.
SelfTestStep closeKeyOtherWindowStep(SelfTest* test, const std::shared_ptr<SettingsState>& state) {
    struct Seen {
        bool closeAsked = false;  // the other window got wxEVT_CLOSE_WINDOW
        bool available = false;   // Close stream was enabled with the focus there
    };
    auto seen_state = std::make_shared<Seen>();
    auto start = [test, state, seen_state] {
        MainFrame& frame = test->frame();
        state->streamsBefore = frame.streams().size();
        auto* other = new wxFrame(&frame, wxID_ANY, "rawreel selftest stand-in");  // never shown
        other->Bind(wxEVT_CLOSE_WINDOW, [seen_state](wxCloseEvent& event) {
            seen_state->closeAsked = true;
            event.Veto();
        });
        auto* child = new wxPanel(other);
        frame.setFocusForTest(child);
        seen_state->available = frame.isAvailable(Command::CloseStreams);
        frame.execute(Command::CloseStreams);
        frame.setFocusForTest(nullptr);
        other->Destroy();
    };
    auto passed = [test, state, seen_state](std::string& seen) {
        const std::uint32_t streams = test->frame().session().status().streams;
        seen = std::format(
            "Close stream enabled {}, the other window asked to close {}, {} session streams (was "
            "{})",
            seen_state->available, seen_state->closeAsked, streams, state->streamsBefore);
        return seen_state->available && !seen_state->closeAsked && state->streamsBefore == 1 &&
               streams == 0;
    };
    return step("close key with another window", std::move(start), std::move(passed));
}
#endif

void addSettingsSteps(std::vector<SelfTestStep>& steps, SelfTest* test) {
    const auto state = std::make_shared<SettingsState>();
    steps.push_back(settingsOpensStep(test, state));
    steps.push_back(settingsLogFolderStep(test, state));
    steps.push_back(settingsFolderFailureStep(test, state));
    steps.push_back(settingsRevealStep(test, state));
    steps.push_back(settingsSystemLogStep(test, state));
    steps.push_back(settingsFileReadsStep(test, state));
#if defined(__WXOSX__)
    steps.push_back(settingsCloseKeyStep(test, state));
    steps.push_back(closeKeyOtherWindowStep(test, state));  // closes the stream (last)
#endif
}

std::vector<SelfTestStep> smokeScenario(SelfTest& test) {
    SelfTest* const t = &test;
    const auto state = std::make_shared<SmokeState>();
    std::vector<SelfTestStep> steps;
    steps.push_back(firstFramesStep(t, state));
    steps.push_back(settingsOwnerStep(t));
    steps.push_back(selftestConfigStep(t));
    steps.push_back(resizeStep(t, state, "resize 800x600", wxSize(800, 600)));
    steps.push_back(shrinkStep(t, state));
    steps.push_back(movedPaneStep(t, state, false));
    steps.push_back(movedPaneStep(t, state, true));
    steps.push_back(hideStep(t, state));
    steps.push_back(restoreStep(t, state));
    steps.push_back(openStreamStep(t, state));
    steps.push_back(resizeStep(t, state, "resize with a stream", wxSize(1024, 640)));
#if defined(__WXOSX__)
    steps.push_back(backingScaleStep(t, state, false));
    steps.push_back(backingScaleStep(t, state, true));
#endif
    steps.push_back(panesStep(t));
    steps.push_back(logNoticeStep(t));
    steps.push_back(logPaneShowsStep(t));
    const auto log_pane = std::make_shared<LogPaneState>();
    steps.push_back(logPaneCloseStep(t, log_pane));
    steps.push_back(logPaneHeightStep(t, log_pane));
    steps.push_back(logPaneBurstStep(t));
    addLogListSteps(steps, t);
    addEditSteps(steps, t);
    steps.push_back(menuTransportStep(t));
    steps.push_back(pauseStep(t));
    steps.push_back(statusBarStep(t));
    steps.push_back(diagnosticsStep(t, state));
    steps.push_back(menusStep(t, state));
    steps.push_back(infobarStep(t));
    steps.push_back(aboutStep(t, state));
#if defined(__WXOSX__)
    steps.push_back(appMenuStep(t));
#endif
    if (test.app().startup().ffmpeg.lavfi) {
        steps.push_back(lavfiUriStep(t, state));
        steps.push_back(lavfiFormatStep(t, state));
    } else if (ffmpegRequired()) {
        steps.push_back(ffmpegRequiredStep(ffmpegStatusLine(test.app().startup().ffmpeg)));
    } else {
        DVV_LOG_INFO("selftest: FFmpeg not available, lavfi steps skipped");
    }
    addOpenUriSteps(steps, t, state);
    addSettingsSteps(steps, t);  // after the smoke stream is open again (the close key)
    steps.push_back(appearanceStep(t));
    steps.push_back(logPaneAtCloseStep(t));  // last: the close saves gui.showLogPane = true
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
int showModalWith(Dialog& dialog, dvv::MoveOnlyFunction<void(Dialog&)>& slot,
                  std::string_view kind) {
    if (slot) {
        // One shot; CallAfter needs a copyable functor, so the hook is shared.
        auto hook = std::make_shared<dvv::MoveOnlyFunction<void(Dialog&)>>(std::move(slot));
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
int showModal(wxMessageDialog& dialog) {
    if (App::arguments().selftest == Scenario::None) {
        return dialog.ShowModal();
    }
    // A native message box cannot be ended from a CallAfter on every OS: in a selftest it is
    // never shown.
    dvv::MoveOnlyFunction<void(wxMessageDialog&)>& slot = dialogHooks().message;
    if (!slot) {
        DVV_LOG_WARN("rawreel-gui --selftest: a message dialog without a hook was cancelled");
        return wxID_CANCEL;
    }
    auto hook = std::move(slot);  // one shot
    slot = nullptr;
    hook(dialog);
    return wxID_OK;
}
int showModal(wxFileDialog& dialog) {
    if (App::arguments().selftest == Scenario::None) {
        return dialog.ShowModal();
    }
    // Native, like the message box: in a selftest it is never shown.
    dvv::MoveOnlyFunction<void(wxFileDialog&)>& slot = dialogHooks().saveLog;
    if (!slot) {
        DVV_LOG_WARN("rawreel-gui --selftest: a file dialog without a hook was cancelled");
        return wxID_CANCEL;
    }
    auto hook = std::move(slot);  // one shot
    slot = nullptr;
    hook(dialog);
    return wxID_OK;
}

int showModal(wxDirDialog& dialog) {
    if (App::arguments().selftest == Scenario::None) {
        return dialog.ShowModal();
    }
    // Native, like the file dialog: in a selftest it is never shown.
    dvv::MoveOnlyFunction<void(wxDirDialog&)>& slot = dialogHooks().chooseFolder;
    if (!slot) {
        DVV_LOG_WARN("rawreel-gui --selftest: a folder dialog without a hook was cancelled");
        return wxID_CANCEL;
    }
    auto hook = std::move(slot);  // one shot
    slot = nullptr;
    hook(dialog);
    return wxID_OK;
}

std::string chosenFolder(const wxDirDialog& dialog) {
    if (App::arguments().selftest != Scenario::None) {
        return dialog.wxDirDialogBase::GetPath().utf8_string();  // what the hook set
    }
    return dialog.GetPath().utf8_string();
}

void revealFile(const std::filesystem::path& file_or_folder) {
    if (App::arguments().selftest == Scenario::None) {
        revealInFileManager(file_or_folder);
        return;
    }
    dvv::MoveOnlyFunction<void(std::filesystem::path)>& slot = dialogHooks().reveal;
    if (!slot) {
        DVV_LOG_WARN("rawreel-gui --selftest: a file manager hand-off without a hook was dropped");
        return;
    }
    auto hook = std::move(slot);  // one shot
    slot = nullptr;
    hook(file_or_folder);
}

void showSettingsWindow(SettingsWindow& settings, wxWindow& parent) {
    if (App::arguments().selftest == Scenario::None) {
        settings.show(&parent);
        return;
    }
    dvv::MoveOnlyFunction<void(SettingsWindow&)>& slot = dialogHooks().settings;
    if (!wxPreferencesEditor::ShownModally()) {
        settings.show(&parent);  // returns at once
        if (slot) {
            auto hook = std::move(slot);  // one shot
            slot = nullptr;
            hook(settings);
        }
        return;
    }
    // Windows: the hook runs inside the modal loop and ends the dialog (pressOk()); without one
    // the dialog is cancelled.
    auto hook = std::make_shared<dvv::MoveOnlyFunction<void(SettingsWindow&)>>(std::move(slot));
    slot = nullptr;
    parent.CallAfter([&settings, hook] {
        if (*hook) {
            (*hook)(settings);
        } else {
            DVV_LOG_WARN(
                "rawreel-gui --selftest: the Settings window without a hook was cancelled");
            settings.dismiss();
        }
    });
    settings.show(&parent);
}

std::string chosenPath(const wxFileDialog& dialog) {
    if (App::arguments().selftest != Scenario::None) {
        return dialog.wxFileDialogBase::GetPath().utf8_string();  // what the hook's SetPath set
    }
    return dialog.GetPath().utf8_string();
}

void setClipboardText(std::string text) {
    if (App::arguments().selftest != Scenario::None) {
        dvv::MoveOnlyFunction<void(std::string)>& slot = dialogHooks().clipboard;
        if (!slot) {
            DVV_LOG_WARN("rawreel-gui --selftest: a copy without a clipboard hook was dropped");
            return;
        }
        auto hook = std::move(slot);  // one shot
        slot = nullptr;
        hook(std::move(text));
        return;
    }
    if (const wxClipboardLocker lock; !!lock) {
        // wx converts the line ends for the Windows clipboard.
        wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(text)));
    }
}

// --- player (design v2 §8.4 player) -------------------------------------------------------------

namespace {

void sendSliderTrack(wxSlider& slider, int value) {
    slider.SetValue(value);
    wxScrollEvent event(wxEVT_SCROLL_THUMBTRACK, slider.GetId(), value, wxHORIZONTAL);
    event.SetEventObject(&slider);
    slider.GetEventHandler()->ProcessEvent(event);
}

// A CHANGED without a drag (a Windows track click, the wheel or a key on Windows and GTK).
void sendSliderChanged(wxSlider& slider, int value) {
    slider.SetValue(value);
    wxScrollEvent event(wxEVT_SCROLL_CHANGED, slider.GetId(), value, wxHORIZONTAL);
    event.SetEventObject(&slider);
    slider.GetEventHandler()->ProcessEvent(event);
}

// The end of a drag (or of a GTK track click), at the slider's current value.
void sendSliderRelease(wxSlider& slider) {
    wxScrollEvent event(wxEVT_SCROLL_THUMBRELEASE, slider.GetId(), slider.GetValue(), wxHORIZONTAL);
    event.SetEventObject(&slider);
    slider.GetEventHandler()->ProcessEvent(event);
}

// The end of a GTK drag (spec §4): the value is read once, then THUMBRELEASE and CHANGED carry it.
void sendSliderReleasePair(wxSlider& slider) {
    const int value = slider.GetValue();
    for (const wxEventType type : {wxEVT_SCROLL_THUMBRELEASE, wxEVT_SCROLL_CHANGED}) {
        wxScrollEvent event(type, slider.GetId(), value, wxHORIZONTAL);
        event.SetEventObject(&slider);
        slider.GetEventHandler()->ProcessEvent(event);
    }
}

// The platform's skim key (spec §3): ⌥ Option on macOS, Ctrl elsewhere.
#if defined(__WXOSX__)
constexpr int SKIM_KEY_CODE = WXK_ALT;
#else
constexpr int SKIM_KEY_CODE = WXK_CONTROL;
#endif

void setSkimModifier(wxKeyboardState& state, bool down) {
#if defined(__WXOSX__)
    state.SetAltDown(down);
#else
    state.SetControlDown(down);
#endif
}

// The thumb centre of `frame` on the bar's slider, in its wx client px (spec §5.3).
int xOfFrame(TransportBar& bar, std::int64_t frame) {
    const wxSlider& slider = bar.slider();
    const SliderTrack travel = sliderTravel(slider);
    return static_cast<int>(std::lround(
        thumbX(frame, slider.GetMin(), slider.GetMax(), travel.travelStart, travel.travelEnd)));
}

// A mouse event on the bar's slider at the wx client x `x`, half way down, with the skim key's
// modifier as given. Its x is in the event's native shape: the inverse of sliderEventX. Returns
// what ProcessEvent did: true when no handler skipped the event.
bool sendSliderMouse(TransportBar& bar, wxEventType type, int x, bool skim_key,
                     bool left_down = false) {
    wxSlider& slider = bar.slider();
    int native_x = x - sliderEventX(slider, 0);
    if (const int back = sliderEventX(slider, native_x); back != x) {
        native_x += back < x ? 1 : -1;  // a fractional offset rounded the other way
    }
    wxMouseEvent event(type);
    event.SetEventObject(&slider);
    event.SetId(slider.GetId());
    event.m_x = native_x;
    event.m_y = slider.GetClientSize().y / 2;
    event.m_leftDown = left_down;
    setSkimModifier(event, skim_key);
    return injectInput(*slider.GetEventHandler(), event);
}

// The skim key's release, sent to `target` (the focused window gets it). Its modifier flag is set:
// GTK reports the state from before the event (spec §6.4).
void sendSkimKeyUp(wxWindow& target) {
    wxKeyEvent event(wxEVT_KEY_UP);
    event.SetEventObject(&target);
    event.m_keyCode = SKIM_KEY_CODE;
    setSkimModifier(event, true);
    injectInput(*target.GetEventHandler(), event);
}

void sendActivate(wxFrame& frame, bool active) {
    wxActivateEvent event(wxEVT_ACTIVATE, active, frame.GetId());
    event.SetEventObject(&frame);
    injectInput(*frame.GetEventHandler(), event);
}

// The first label of `model` with `role`; nullptr: none.
const TimelineLabel* labelWithRole(const TimelineModel& model, std::string_view role) {
    const auto found = std::ranges::find(model.labels, role, &TimelineLabel::role);
    return found == model.labels.end() ? nullptr : &*found;
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
        std::uint64_t logNext = 0;                // logNext() at the step start
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
    // The clip's URI, stored when "open one stream" passes (later steps replace or close it).
    auto clip_uri = std::make_shared<std::string>();
    ProduceDelay* const delay = test.app().produceDelay();  // non-null in --selftest runs
    const App& app = test.app();
    std::vector<SelfTestStep> steps;

    steps.push_back(step(
        "open one stream",
        [st] {
            st->dir = std::filesystem::temp_directory_path() /
                      std::format("rawreel-selftest-{}", wxGetProcessId());
            std::filesystem::create_directories(st->dir);
        },
        [&frame, status, stream_uri, clip_uri](std::string& seen) {
            seen = std::format("streams {}, presented {}, position {}", frame.streams().size(),
                               presented(frame.viewport()), status().position);
            if (frame.streams().size() != 1 || presented(frame.viewport()) < 2 ||
                status().position != 0) {
                return false;
            }
            *clip_uri = stream_uri();
            return true;
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
    {
        struct Quiet {
            std::uint64_t n0 = 0;
            bool grew = false;
            bool started = false;
            std::chrono::steady_clock::time_point since;
        };
        auto quiet = std::make_shared<Quiet>();
        steps.push_back(step(
            "paused: no status work",
            [&frame, status] {
                if (status().playing) {
                    frame.execute(Command::PlayPause);
                }
            },
            [&frame, quiet](std::string& seen) {
                const SessionStatus full = frame.session().status();
                if (!quiet->started) {
                    seen = std::format("inflight {} revision {} handled {}", full.inflight,
                                       full.revision, frame.lastRevisionForTest());
                    if (full.player.playing || full.inflight != 0 ||
                        frame.lastRevisionForTest() != full.revision) {
                        return false;
                    }
                    quiet->started = true;
                    quiet->n0 = frame.statusNotificationsForTest();
                    quiet->since = std::chrono::steady_clock::now();
                }
                if (frame.statusNotificationsForTest() != quiet->n0) {
                    quiet->grew = true;
                }
                seen = quiet->grew ? "a notification was handled while paused" : "quiet";
                return !quiet->grew &&
                       std::chrono::steady_clock::now() - quiet->since >= std::chrono::seconds(1);
            }));
        struct Playing {
            std::uint64_t n0 = 0;
            wxString text0;
        };
        auto playing = std::make_shared<Playing>();
        steps.push_back(step(
            "play updates the bar",
            [&frame, playing] {
                playing->n0 = frame.statusNotificationsForTest();
                playing->text0 = frame.statusTexts()[0];
                frame.execute(Command::PlayPause);
            },
            [&frame, playing](std::string& seen) {
                seen = std::format("notifications {} (from {})", frame.statusNotificationsForTest(),
                                   playing->n0);
                return frame.statusTexts()[0] != playing->text0 &&
                       frame.statusNotificationsForTest() > playing->n0;
            },
            std::chrono::seconds(2)));
        steps.push_back(step(
            "pause after the bar update",
            [&frame, status] {
                if (status().playing) {
                    frame.execute(Command::PlayPause);
                }
            },
            [status](std::string& seen) {
                seen = std::format("playing {}", status().playing);
                return !status().playing;
            }));
    }
    {
        // Spec §6.6: while Scrubbing, update() leaves the slider alone; the release ends it.
        struct Drag {
            std::vector<int> values;  // the slider's value after each update() since the 55
            bool broken = false;      // left Scrubbing or moved the slider during the hold
            bool released = false;
            std::chrono::steady_clock::time_point since;
        };
        auto drag = std::make_shared<Drag>();
        steps.push_back(step(
            "held drag on a slow source",
            [&frame, delay, drag] {
                delay->set(500);
                TransportBar& bar = frame.transportBar();
                bar.setLeftButtonProbeForTest([] { return true; });
                for (const int value : {25, 40, 55}) {
                    sendSliderTrack(bar.slider(), value);
                }
                // Installed after the 55 was sent: every value it records comes after it.
                bar.setUpdateObserverForTest([drag](int value) { drag->values.push_back(value); });
                drag->since = std::chrono::steady_clock::now();
            },
            [&frame, delay, drag, status](std::string& seen) {
                TransportBar& bar = frame.transportBar();
                const bool idle = bar.scrubState() == ScrubState::Idle;
                const bool at55 =
                    std::ranges::all_of(drag->values, [](int value) { return value == 55; });
                if (!drag->released) {
                    if (idle || !at55) {
                        drag->broken = true;
                    }
                    seen = std::format("hold: idle {}, updates {}, all at 55 {}, broken {}", idle,
                                       drag->values.size(), at55, drag->broken);
                    if (drag->broken || drag->values.empty() ||
                        std::chrono::steady_clock::now() - drag->since < std::chrono::seconds(2)) {
                        return false;
                    }
                    // A status that names another frame must not move the thumb under the drag
                    // (read-your-writes keeps `target` at 55 otherwise, which hides it).
                    PlayerStatus elsewhere = status();
                    elsewhere.target = 10;
                    elsewhere.position = 10;
                    bar.update(BarStatus{.player = elsewhere,
                                         .hasStream = true,
                                         .streamGeneration = bar.streamGenerationForTest()});
                    if (bar.slider().GetValue() != 55 ||
                        bar.scrubState() != ScrubState::Scrubbing) {
                        drag->broken = true;
                        seen = std::format("update() during the drag: slider {}, scrubbing {}",
                                           bar.slider().GetValue(),
                                           bar.scrubState() == ScrubState::Scrubbing);
                        return false;
                    }
                    bar.setLeftButtonProbeForTest([] { return false; });
                    sendSliderRelease(bar.slider());
                    drag->released = true;
                }
                const bool released_idle = bar.scrubState() == ScrubState::Idle;
                const int slider = bar.slider().GetValue();
                seen = std::format(
                    "released: position {}, idle {}, slider {}, updates {}, all at 55 {}",
                    status().position, released_idle, slider, drag->values.size(), at55);
                if (status().position != 55 || !released_idle || slider != 55 || !at55) {
                    return false;
                }
                delay->set(0);
                bar.setLeftButtonProbeForTest(nullptr);
                bar.setUpdateObserverForTest(nullptr);
                return true;
            },
            std::chrono::seconds(15)));
    }
    steps.push_back(step(
        "changed alone seeks and stays idle",
        [&frame] { sendSliderChanged(frame.transportBar().slider(), 20); },
        [&frame, status](std::string& seen) {
            const bool idle = frame.transportBar().scrubState() == ScrubState::Idle;
            seen = std::format("position {}, idle {}", status().position, idle);
            return status().position == 20 && idle;
        }));
    {
        struct Lone {
            std::int64_t position = -1;
            int ticks = 0;
            bool moved = false;
        };
        auto lone = std::make_shared<Lone>();
        steps.push_back(step(
            "a lone release does nothing",
            [&frame, lone, status] {
                lone->position = status().position;
                sendSliderRelease(frame.transportBar().slider());
            },
            [&frame, lone, status](std::string& seen) {
                const bool idle = frame.transportBar().scrubState() == ScrubState::Idle;
                lone->moved = lone->moved || status().position != lone->position;
                ++lone->ticks;
                seen = std::format("idle {}, position {} (was {}), ticks {}", idle,
                                   status().position, lone->position, lone->ticks);
                return idle && !lone->moved && lone->ticks >= 3;
            }));
    }
    // Video control panel spec §6.2, §6.3: the scrub states, the Idle view and the release pair.
    // Each check that must not yield is made in the action that sent the events.
    auto idle_at = [&frame, status](std::string name, int value) {
        return step(
            std::move(name),
            [&frame, value] { sendSliderChanged(frame.transportBar().slider(), value); },
            [&frame, status, value](std::string& seen) {
                const PlayerStatus now = status();
                TransportBar& bar = frame.transportBar();
                const bool idle = bar.scrubState() == ScrubState::Idle;
                seen = std::format("position {}, target {}, playing {}, idle {}, slider {}",
                                   now.position, now.target, now.playing, idle,
                                   bar.slider().GetValue());
                return idle && !now.playing && now.position == value && now.target == value &&
                       bar.slider().GetValue() == value;
            });
    };
    // The bar settled on the session's status: nothing in flight and the last revision handled.
    auto settled = [&frame](std::int64_t position) {
        const SessionStatus full = frame.session().status();
        return full.inflight == 0 && frame.lastRevisionForTest() == full.revision &&
               full.player.position == position && full.player.target == position;
    };
    struct Scrub {
        std::vector<std::int64_t> seeks;  // every value the bar passed to seek
        std::vector<int> updates;         // the slider's value after each update()
        bool ok = false;                  // the checks made in the action
        std::string check;                // what they saw
        int ticks = 0;
        bool released = false;             // the release-tail step sent its release pair
        bool sent = false;                 // a waiting step sent its events
        std::int64_t releasedTarget = -1;  // the session's target just after it
    };
    auto scrub = std::make_shared<Scrub>();
    // Starts recording the bar's seeks into `scrub`.
    auto record_seeks = [&frame, scrub] {
        *scrub = Scrub{};
        frame.transportBar().setSeekObserverForTest(
            [scrub](std::int64_t value) { scrub->seeks.push_back(value); });
    };
    auto stop_recording = [&frame] {
        TransportBar& bar = frame.transportBar();
        bar.setSeekObserverForTest(nullptr);
        bar.setUpdateObserverForTest(nullptr);
        bar.setLeftButtonProbeForTest(nullptr);
    };
    auto seeks_text = [scrub] {
        std::string text;
        for (const std::int64_t value : scrub->seeks) {
            text += std::format("{}{}", text.empty() ? "" : " ", value);
        }
        return text;
    };
    steps.push_back(idle_at("idle at 10 for the marker", 10));
    steps.push_back(step(
        "marker during a held drag",
        [&frame, scrub] {
            TransportBar& bar = frame.transportBar();
            TimelineStrip& strip = bar.timelineStripForTest();
            bar.setLeftButtonProbeForTest([] { return true; });
            sendSliderTrack(bar.slider(), 20);
            sendSliderTrack(bar.slider(), 35);
            const TimelineModel held = strip.modelForTest();
            const TimelineLabel* current = labelWithRole(held, "current");
            const TimelineLabel* original = labelWithRole(held, "original");
            const bool scrubbing = bar.scrubState() == ScrubState::Scrubbing;
            bar.setLeftButtonProbeForTest([] { return false; });
            sendSliderReleasePair(bar.slider());
            const TimelineModel released = strip.modelForTest();
            const bool idle = bar.scrubState() == ScrubState::Idle;
            bar.setLeftButtonProbeForTest(nullptr);
            scrub->check = std::format(
                "held: scrubbing {}, marker {}, current \"{}\", original \"{}\"; released: idle "
                "{}, marker {}, original label {}",
                scrubbing, held.marker.value_or(-1), current != nullptr ? current->text : "-",
                original != nullptr ? original->text : "-", idle, released.marker.value_or(-1),
                labelWithRole(released, "original") != nullptr);
            scrub->ok = scrubbing && held.marker == 10 && current != nullptr &&
                        current->text == "35" && original != nullptr && original->text == "10" &&
                        idle && !released.marker && labelWithRole(released, "original") == nullptr;
        },
        [scrub](std::string& seen) {
            seen = scrub->check;
            return scrub->ok;
        }));
    steps.push_back(idle_at("idle at 10 for a quick release", 10));
    steps.push_back(step(
        "a quick release",
        [&frame, scrub, record_seeks] {
            TransportBar& bar = frame.transportBar();
            record_seeks();
            bar.setLeftButtonProbeForTest([] { return true; });
            sendSliderTrack(bar.slider(), 20);
            sendSliderTrack(bar.slider(), 35);
            sendSliderReleasePair(bar.slider());
            bar.setLeftButtonProbeForTest([] { return false; });
            const bool idle = bar.scrubState() == ScrubState::Idle;
            scrub->check =
                std::format("at the release: idle {}, slider {}", idle, bar.slider().GetValue());
            scrub->ok = idle && bar.slider().GetValue() == 35;
            // Installed after the release: every value it records comes after it.
            bar.setUpdateObserverForTest([scrub](int value) { scrub->updates.push_back(value); });
        },
        [scrub, settled, seeks_text, stop_recording](std::string& seen) {
            const bool at35 =
                std::ranges::all_of(scrub->updates, [](int value) { return value == 35; });
            seen = std::format("{}; seeks [{}], updates {}, all at 35 {}", scrub->check,
                               seeks_text(), scrub->updates.size(), at35);
            if (!settled(35) || !scrub->ok || !at35 ||
                scrub->seeks != std::vector<std::int64_t>{20, 35}) {
                return false;
            }
            stop_recording();
            return true;
        }));
    steps.push_back(step(
        "a buttonless track stays idle",
        [&frame, scrub, record_seeks] {
            TransportBar& bar = frame.transportBar();
            record_seeks();
            bar.setLeftButtonProbeForTest([] { return false; });
            sendSliderTrack(bar.slider(), 12);
            const bool idle = bar.scrubState() == ScrubState::Idle;
            const std::optional<std::int64_t> marker =
                bar.timelineStripForTest().modelForTest().marker;
            scrub->check = std::format("idle {}, marker {}", idle, marker.value_or(-1));
            scrub->ok = idle && !marker;
        },
        [scrub, settled, seeks_text, stop_recording](std::string& seen) {
            seen = std::format("{}; seeks [{}]", scrub->check, seeks_text());
            if (!settled(12) || !scrub->ok || scrub->seeks != std::vector<std::int64_t>{12}) {
                return false;
            }
            stop_recording();
            return true;
        }));
    steps.push_back(step(
        "a new generation resets",
        [&frame, scrub, record_seeks] {
            TransportBar& bar = frame.transportBar();
            record_seeks();
            bar.setLeftButtonProbeForTest([] { return true; });
            sendSliderTrack(bar.slider(), 30);
            const bool scrubbing = bar.scrubState() == ScrubState::Scrubbing;
            const SessionStatus real = frame.session().status();
            const std::uint64_t generation = bar.streamGenerationForTest();
            BarStatus same{.player = real.player,
                           .hasStream = real.streams > 0,
                           .streamGeneration = generation};
            bar.update(same);
            const bool kept = bar.scrubState() == ScrubState::Scrubbing;
            same.streamGeneration = generation + 1;
            bar.update(same);
            const bool idle = bar.scrubState() == ScrubState::Idle;
            const std::optional<std::int64_t> marker =
                bar.timelineStripForTest().modelForTest().marker;
            bar.update(BarStatus{.player = real.player,
                                 .hasStream = real.streams > 0,
                                 .streamGeneration = real.streamGeneration});
            scrub->check = std::format(
                "scrubbing {}, generation {} (session {}), kept {}, idle {}, marker {}", scrubbing,
                generation, real.streamGeneration, kept, idle, marker.value_or(-1));
            scrub->ok = scrubbing && generation == real.streamGeneration && kept && idle && !marker;
        },
        [scrub, settled, seeks_text, stop_recording](std::string& seen) {
            ++scrub->ticks;
            seen =
                std::format("{}; seeks [{}], ticks {}", scrub->check, seeks_text(), scrub->ticks);
            if (!settled(30) || !scrub->ok || scrub->seeks != std::vector<std::int64_t>{30} ||
                scrub->ticks < 3) {
                return false;
            }
            stop_recording();
            return true;
        }));
    steps.push_back(idle_at("idle at 10 for play during a drag", 10));
    steps.push_back(step(
        "play during a drag",
        [&frame, scrub, record_seeks, status] {
            TransportBar& bar = frame.transportBar();
            record_seeks();
            bar.setLeftButtonProbeForTest([] { return true; });
            sendSliderTrack(bar.slider(), 25);
            sendKey(frame, WXK_SPACE);
            // Playback moves `target` on: the bar's fresh read lies between these two.
            const std::int64_t before = status().target;
            sendSliderReleasePair(bar.slider());
            const std::int64_t after = status().target;
            bar.setLeftButtonProbeForTest([] { return false; });
            const bool idle = bar.scrubState() == ScrubState::Idle;
            const int slider = bar.slider().GetValue();
            scrub->check = std::format("at the release: idle {}, slider {} (target {}..{})", idle,
                                       slider, before, after);
            scrub->ok = idle && before <= slider && slider <= after;
        },
        [scrub, seeks_text, stop_recording, status](std::string& seen) {
            const PlayerStatus now = status();
            seen = std::format("{}; seeks [{}], playing {}, position {}", scrub->check,
                               seeks_text(), now.playing, now.position);
            if (!scrub->ok || scrub->seeks != std::vector<std::int64_t>{25} || !now.playing ||
                now.position < 30) {
                return false;
            }
            stop_recording();
            return true;
        }));
    steps.push_back(step(
        "pause after play during a drag",
        [&frame, status] {
            if (status().playing) {
                frame.execute(Command::PlayPause);
            }
        },
        [status](std::string& seen) {
            seen = std::format("playing {}", status().playing);
            return !status().playing;
        }));
    // The GTK release tail after playback has moved `target` past the drag's value: only
    // release_value_ keeps its CHANGED from seeking (and pausing). Waits for the condition.
    steps.push_back(idle_at("idle at 10 for a release tail during playback", 10));
    steps.push_back(step(
        "a release tail during playback",
        [&frame, record_seeks] {
            TransportBar& bar = frame.transportBar();
            record_seeks();
            bar.setLeftButtonProbeForTest([] { return true; });
            sendSliderTrack(bar.slider(), 25);
            sendKey(frame, WXK_SPACE);
        },
        [&frame, scrub, seeks_text, stop_recording, status](std::string& seen) {
            TransportBar& bar = frame.transportBar();
            if (!scrub->released) {
                const PlayerStatus before = status();
                const bool scrubbing = bar.scrubState() == ScrubState::Scrubbing;
                seen =
                    std::format("held: scrubbing {}, playing {}, target {}, slider {}", scrubbing,
                                before.playing, before.target, bar.slider().GetValue());
                if (!scrubbing || !before.playing || before.target < 28) {
                    return false;
                }
                sendSliderReleasePair(bar.slider());  // at 25
                const std::int64_t after = status().target;
                bar.setLeftButtonProbeForTest([] { return false; });
                const int slider = bar.slider().GetValue();
                const bool idle = bar.scrubState() == ScrubState::Idle;
                scrub->check = std::format("at the release: idle {}, slider {} (target {}..{})",
                                           idle, slider, before.target, after);
                scrub->ok = idle && before.target <= slider && slider <= after;
                scrub->released = true;
                scrub->releasedTarget = after;
            }
            const PlayerStatus now = status();
            seen = std::format("{}; seeks [{}], playing {}, position {}", scrub->check,
                               seeks_text(), now.playing, now.position);
            if (!scrub->ok || scrub->seeks != std::vector<std::int64_t>{25} || !now.playing ||
                now.position < scrub->releasedTarget + 2) {
                return false;
            }
            stop_recording();
            return true;
        }));
    steps.push_back(step(
        "pause after the release tail",
        [&frame, status] {
            if (status().playing) {
                frame.execute(Command::PlayPause);
            }
        },
        [status](std::string& seen) {
            seen = std::format("playing {}", status().playing);
            return !status().playing;
        }));
    // The tail of a Windows release pair (spec §6.2 notes): Windows reads TBM_GETPOS again for
    // the CHANGED, so it carries the value the Idle view has just set. Paused, that value is the
    // release's; during playback only the idle_value_ rule keeps it from seeking (and pausing).
    steps.push_back(idle_at("idle at 10 for a windows release tail", 10));
    steps.push_back(step(
        "a windows release tail",
        [&frame, scrub, record_seeks] {
            TransportBar& bar = frame.transportBar();
            record_seeks();
            bar.setLeftButtonProbeForTest([] { return true; });
            sendSliderTrack(bar.slider(), 20);
            sendSliderTrack(bar.slider(), 35);
            sendSliderRelease(bar.slider());
            const int reread = bar.slider().GetValue();  // after the THUMBRELEASE handler ran
            sendSliderChanged(bar.slider(), reread);
            bar.setLeftButtonProbeForTest([] { return false; });
            const bool idle = bar.scrubState() == ScrubState::Idle;
            scrub->check = std::format("at the release: idle {}, re-read {}", idle, reread);
            scrub->ok = idle && reread == 35;
        },
        [scrub, settled, seeks_text, stop_recording](std::string& seen) {
            seen = std::format("{}; seeks [{}]", scrub->check, seeks_text());
            if (!settled(35) || !scrub->ok || scrub->seeks != std::vector<std::int64_t>{20, 35}) {
                return false;
            }
            stop_recording();
            return true;
        }));
    steps.push_back(idle_at("idle at 10 for a windows release tail during playback", 10));
    steps.push_back(step(
        "a windows release tail during playback",
        [&frame, record_seeks] {
            TransportBar& bar = frame.transportBar();
            record_seeks();
            bar.setLeftButtonProbeForTest([] { return true; });
            sendSliderTrack(bar.slider(), 25);
            sendKey(frame, WXK_SPACE);
        },
        [&frame, scrub, seeks_text, stop_recording, status](std::string& seen) {
            TransportBar& bar = frame.transportBar();
            if (!scrub->released) {
                const PlayerStatus before = status();
                const bool scrubbing = bar.scrubState() == ScrubState::Scrubbing;
                seen = std::format("held: scrubbing {}, playing {}, target {}", scrubbing,
                                   before.playing, before.target);
                if (!scrubbing || !before.playing || before.target < 28) {
                    return false;
                }
                sendSliderRelease(bar.slider());  // at 25
                const int reread = bar.slider().GetValue();
                sendSliderChanged(bar.slider(), reread);
                bar.setLeftButtonProbeForTest([] { return false; });
                const bool idle = bar.scrubState() == ScrubState::Idle;
                scrub->check = std::format("at the release: idle {}, re-read {} (target {})", idle,
                                           reread, before.target);
                scrub->ok = idle && reread >= before.target;
                scrub->released = true;
                scrub->releasedTarget = reread;
            }
            const PlayerStatus now = status();
            seen = std::format("{}; seeks [{}], playing {}, position {}", scrub->check,
                               seeks_text(), now.playing, now.position);
            if (!scrub->ok || scrub->seeks != std::vector<std::int64_t>{25} || !now.playing ||
                now.position < scrub->releasedTarget + 2) {
                return false;
            }
            stop_recording();
            return true;
        }));
    steps.push_back(step(
        "pause after the windows release tail",
        [&frame, status] {
            if (status().playing) {
                frame.execute(Command::PlayPause);
            }
        },
        [status](std::string& seen) {
            seen = std::format("playing {}", status().playing);
            return !status().playing;
        }));
    // Spec §6.2, §6.3: no drag cancel. Keys and the release pair go through injectInput.
    auto current_label = [&frame]() -> std::optional<TimelineLabel> {
        const TimelineModel model = frame.transportBar().timelineStripForTest().modelForTest();
        const TimelineLabel* current = labelWithRole(model, "current");
        if (current == nullptr) {
            return std::nullopt;
        }
        return *current;
    };
    auto has_marker = [&frame] {
        return frame.transportBar().timelineStripForTest().modelForTest().marker.has_value();
    };
    // The bar is Idle at `value`, settled there, paused, and seeked `seeks` only.
    auto rests_at = [&frame, scrub, settled, seeks_text, stop_recording, status](
                        std::string& seen, int value, const std::vector<std::int64_t>& seeks) {
        TransportBar& bar = frame.transportBar();
        const bool idle = bar.scrubState() == ScrubState::Idle;
        const PlayerStatus now = status();
        seen = std::format("{}; idle {}, slider {}, position {}, target {}, playing {}, seeks [{}]",
                           scrub->check, idle, bar.slider().GetValue(), now.position, now.target,
                           now.playing, seeks_text());
        if (!scrub->ok || !idle || bar.slider().GetValue() != value || !settled(value) ||
            now.playing || scrub->seeks != seeks) {
            return false;
        }
        stop_recording();
        return true;
    };
    auto back_at_10 = [rests_at](std::string& seen, const std::vector<std::int64_t>& seeks) {
        return rests_at(seen, 10, seeks);
    };
    steps.push_back(idle_at("idle at 10 for escape", 10));
    steps.push_back(step(
        "escape does nothing",
        [&frame, scrub, record_seeks] {
            TransportBar& bar = frame.transportBar();
            record_seeks();
            const bool idle_consumed = sendKey(frame, WXK_ESCAPE);
            const bool idle = bar.scrubState() == ScrubState::Idle;
            const std::size_t idle_seeks = scrub->seeks.size();
            bar.setLeftButtonProbeForTest([] { return true; });
            sendSliderTrack(bar.slider(), 30);
            const bool drag_consumed = sendKey(frame, WXK_ESCAPE);
            const bool scrubbing = bar.scrubState() == ScrubState::Scrubbing;
            sendSliderReleasePair(bar.slider());
            bar.setLeftButtonProbeForTest([] { return false; });
            scrub->check = std::format(
                "idle consumed {}, idle {}, seeks in idle {}, drag consumed {}, scrubbing {}",
                idle_consumed, idle, idle_seeks, drag_consumed, scrubbing);
            scrub->ok = !idle_consumed && idle && idle_seeks == 0 && !drag_consumed && scrubbing;
        },
        [&frame, scrub, settled, seeks_text, stop_recording, status](std::string& seen) {
            TransportBar& bar = frame.transportBar();
            const bool idle = bar.scrubState() == ScrubState::Idle;
            const PlayerStatus now = status();
            seen = std::format("{}; idle {}, slider {}, position {}, target {}, seeks [{}]",
                               scrub->check, idle, bar.slider().GetValue(), now.position,
                               now.target, seeks_text());
            if (!scrub->ok || !idle || bar.slider().GetValue() != 30 || !settled(30) ||
                now.playing || scrub->seeks != std::vector<std::int64_t>{30}) {
                return false;
            }
            stop_recording();
            return true;
        }));
    // Spec §6.2, §6.4: skimming on the slider with the platform's key, from synthetic slider
    // motion, leave, left-down, key-up and activation events. Each sequence starts paused at 10
    // and records the bar's seeks from its first step.
    auto skimming = [&frame] { return frame.transportBar().scrubState() == ScrubState::Skimming; };
    auto is_idle = [&frame] { return frame.transportBar().scrubState() == ScrubState::Idle; };
    auto skim_motion = [&frame](std::int64_t at, bool skim_key, bool left_down = false) {
        TransportBar& bar = frame.transportBar();
        sendSliderMouse(bar, wxEVT_MOTION, xOfFrame(bar, at), skim_key, left_down);
    };
    steps.push_back(idle_at("idle at 10 for skimming", 10));
    steps.push_back(step(
        "skim enters and previews",
        [&frame, record_seeks, skim_motion] {
            sendActivate(frame, true);  // a known activation state: real ones are filtered out
            record_seeks();
            skim_motion(30, true);
        },
        [&frame, scrub, settled, seeks_text, skimming, current_label](std::string& seen) {
            TransportBar& bar = frame.transportBar();
            const std::optional<std::int64_t> marker =
                bar.timelineStripForTest().modelForTest().marker;
            const std::optional<TimelineLabel> current = current_label();
            const std::optional<int> ghost = bar.ghostForTest();
            seen = std::format(
                "skimming {}, marker {}, slider {}, ghost x {} (frame 30 at x {}), current \"{}\", "
                "seeks [{}]",
                skimming(), marker.value_or(-1), bar.slider().GetValue(), ghost.value_or(-1),
                xOfFrame(bar, 30), current ? current->text : "-", seeks_text());
            // Settled first: the notifications of the skim's seek must not end it. The real thumb
            // stays at the start frame; the ghost shows the skimmed one (spec §6.3).
            return settled(30) && skimming() && marker == 10 &&
                   scrub->seeks == std::vector<std::int64_t>{30} && bar.slider().GetValue() == 10 &&
                   ghost == xOfFrame(bar, 30) && current && current->text == "30";
        }));
    steps.push_back(step(
        "skim moves without duplicates",
        [scrub, skim_motion] {
            skim_motion(30, true);
            const std::size_t again = scrub->seeks.size();
            skim_motion(40, true);
            scrub->check = std::format("seeks after frame 30 again {}", again);
            scrub->ok = again == 1;
        },
        [&frame, scrub, settled, seeks_text, skimming](std::string& seen) {
            TransportBar& bar = frame.transportBar();
            const int slider = bar.slider().GetValue();
            const std::optional<int> ghost = bar.ghostForTest();
            seen =
                std::format("{}; skimming {}, slider {}, ghost x {} (frame 40 at x {}), seeks [{}]",
                            scrub->check, skimming(), slider, ghost.value_or(-1), xOfFrame(bar, 40),
                            seeks_text());
            return scrub->ok && settled(40) && skimming() && slider == 10 &&
                   ghost == xOfFrame(bar, 40) && scrub->seeks == std::vector<std::int64_t>{30, 40};
        }));
    steps.push_back(step(
        "the key's release returns",
        [&frame, scrub, is_idle, has_marker] {
            sendSkimKeyUp(frame.viewport());
            const bool idle = is_idle();
            const bool marker = has_marker();
            const bool ghost = frame.transportBar().ghostForTest().has_value();
            scrub->check =
                std::format("idle after the release {}, marker {}, ghost {}", idle, marker, ghost);
            scrub->ok = scrub->ok && idle && !marker && !ghost;
        },
        [back_at_10](std::string& seen) { return back_at_10(seen, {30, 40, 10}); }));
    steps.push_back(idle_at("idle at 10 for a skim at the current frame", 10));
    steps.push_back(step(
        "a motion at the current frame seeks nothing",
        [&frame, scrub, record_seeks, skim_motion, skimming, is_idle] {
            record_seeks();
            skim_motion(10, true);
            const bool skimmed = skimming();
            const std::size_t skim_seeks = scrub->seeks.size();
            sendSkimKeyUp(frame.viewport());
            const bool idle = is_idle();
            scrub->check =
                std::format("skimming {}, seeks while skimming {}, idle after the release {}",
                            skimmed, skim_seeks, idle);
            scrub->ok = skimmed && skim_seeks == 0 && idle;
        },
        [back_at_10](std::string& seen) { return back_at_10(seen, {}); }));
    // Skims to 40, then `end` ends the skim: Idle right after it, then back at 10 after one seek.
    auto return_step = [scrub, record_seeks, skim_motion, skimming, is_idle, back_at_10](
                           std::string name, dvv::MoveOnlyFunction<void()> end) {
        return step(
            std::move(name),
            [scrub, record_seeks, skim_motion, skimming, is_idle, end = std::move(end)]() mutable {
                record_seeks();
                skim_motion(40, true);
                const bool skimmed = skimming();
                end();
                const bool idle = is_idle();
                scrub->check = std::format("skimming {}, idle after the end {}", skimmed, idle);
                scrub->ok = skimmed && idle;
            },
            [back_at_10](std::string& seen) { return back_at_10(seen, {40, 10}); });
    };
    steps.push_back(idle_at("idle at 10 for a motion without the key", 10));
    steps.push_back(
        return_step("a motion without the key returns", [skim_motion] { skim_motion(40, false); }));
    steps.push_back(idle_at("idle at 10 for leaving the slider", 10));
    steps.push_back(return_step("leaving the slider returns", [&frame] {
        TransportBar& bar = frame.transportBar();
        sendSliderMouse(bar, wxEVT_LEAVE_WINDOW, xOfFrame(bar, 40), true);
    }));
    steps.push_back(idle_at("idle at 10 for deactivation", 10));
    steps.push_back(return_step("deactivation returns", [&frame] {
        sendActivate(frame, false);
        sendActivate(frame, true);  // restore
    }));
    steps.push_back(idle_at("idle at 10 for a click while skimming", 10));
    steps.push_back(step(
        "a click keeps the skimmed frame",
        [&frame, scrub, record_seeks, skim_motion, skimming, is_idle, has_marker] {
            TransportBar& bar = frame.transportBar();
            record_seeks();
            skim_motion(40, true);
            const bool skimmed = skimming();
            sendSliderMouse(bar, wxEVT_LEFT_DOWN, xOfFrame(bar, 40), true, true);
            const bool idle = is_idle();
            const int slider = bar.slider().GetValue();
            const bool marker = has_marker();
            sendSkimKeyUp(frame.viewport());
            const bool still_idle = is_idle();
            scrub->check = std::format(
                "skimming {}; after the click: idle {}, slider {}, marker {}; idle after the "
                "release {}",
                skimmed, idle, slider, marker, still_idle);
            scrub->ok = skimmed && idle && slider == 40 && !marker && still_idle;
        },
        [rests_at](std::string& seen) { return rests_at(seen, 40, {40}); }));
    // Spec §6.5: a press on the channel jumps where the native control pages (Windows). The seam
    // makes the jump rule run on every platform.
    steps.push_back(idle_at("idle at 10 for a channel click", 10));
    steps.push_back(step(
        "a channel click jumps",
        [&frame, scrub, record_seeks] {
            TransportBar& bar = frame.transportBar();
            record_seeks();
            bar.setTrackClickPagesForTest(true);
            const bool consumed = sendSliderMouse(bar, wxEVT_LEFT_DOWN, xOfFrame(bar, 50), false);
            bar.setTrackClickPagesForTest(std::nullopt);
            scrub->check = std::format("consumed {}, slider {}", consumed, bar.slider().GetValue());
            scrub->ok = consumed && bar.slider().GetValue() == 50;
        },
        [rests_at](std::string& seen) { return rests_at(seen, 50, {50}); }));
    steps.push_back(idle_at("idle at 10 for a thumb click", 10));
    steps.push_back(step(
        "a thumb click does not jump",
        [&frame, scrub, record_seeks] {
            TransportBar& bar = frame.transportBar();
            record_seeks();
            bar.setTrackClickPagesForTest(true);
            const bool consumed = sendSliderMouse(bar, wxEVT_LEFT_DOWN, xOfFrame(bar, 10), false);
            bar.setTrackClickPagesForTest(std::nullopt);
            scrub->check = std::format("consumed {}, slider {}", consumed, bar.slider().GetValue());
            scrub->ok = !consumed && bar.slider().GetValue() == 10;
        },
        [back_at_10](std::string& seen) { return back_at_10(seen, {}); }));
    steps.push_back(idle_at("idle at 10 for a click while skimming", 10));
    steps.push_back(step(
        "a click while skimming keeps and jumps",
        [&frame, scrub, record_seeks, skim_motion, skimming, is_idle, has_marker] {
            TransportBar& bar = frame.transportBar();
            record_seeks();
            skim_motion(40, true);
            const bool skimmed = skimming();
            bar.setTrackClickPagesForTest(true);
            const bool consumed =
                sendSliderMouse(bar, wxEVT_LEFT_DOWN, xOfFrame(bar, 50), true, true);
            bar.setTrackClickPagesForTest(std::nullopt);
            const bool idle = is_idle();
            const int slider = bar.slider().GetValue();
            const bool marker = has_marker();
            sendSkimKeyUp(frame.viewport());
            scrub->check = std::format(
                "skimming {}; after the click: consumed {}, idle {}, slider {}, marker {}", skimmed,
                consumed, idle, slider, marker);
            scrub->ok = skimmed && consumed && idle && slider == 50 && !marker && is_idle();
        },
        [rests_at](std::string& seen) { return rests_at(seen, 50, {40, 50}); }));
    if constexpr (!SLIDER_PAGES_ON_TRACK_CLICK) {
        steps.push_back(idle_at("idle at 10 for a native channel click", 10));
        steps.push_back(step(
            "a native channel click is left to the control",
            [&frame, scrub, record_seeks] {
                TransportBar& bar = frame.transportBar();
                record_seeks();
                const bool consumed =
                    sendSliderMouse(bar, wxEVT_LEFT_DOWN, xOfFrame(bar, 50), false);
                scrub->check =
                    std::format("consumed {}, slider {}", consumed, bar.slider().GetValue());
                scrub->ok = !consumed && bar.slider().GetValue() == 10;
            },
            [back_at_10](std::string& seen) { return back_at_10(seen, {}); }));
    }
    steps.push_back(idle_at("idle at 10 for a motion with the button down", 10));
    steps.push_back(step(
        "a motion with the button down does not enter",
        [scrub, record_seeks, skim_motion, is_idle] {
            record_seeks();
            skim_motion(40, true, true);
            const bool idle = is_idle();
            scrub->check = std::format("idle after the motion {}", idle);
            scrub->ok = idle;
        },
        [back_at_10](std::string& seen) { return back_at_10(seen, {}); }));
    steps.push_back(idle_at("idle at 10 for a scroll event while skimming", 10));
    steps.push_back(step(
        "a scroll event while skimming keeps",
        [&frame, scrub, record_seeks, skim_motion, skimming, is_idle, seeks_text] {
            TransportBar& bar = frame.transportBar();
            record_seeks();
            skim_motion(40, true);
            const bool skimmed = skimming();
            bar.setLeftButtonProbeForTest([] { return false; });
            sendSliderTrack(bar.slider(), 45);
            const bool idle = is_idle();
            const std::string after_track = seeks_text();
            sendSkimKeyUp(frame.viewport());
            scrub->check = std::format("skimming {}, idle after the track {}, seeks then [{}]",
                                       skimmed, idle, after_track);
            scrub->ok = skimmed && idle && after_track == "40 45";
        },
        // The keep's Idle view shows 40 until the notification of the seek to 45.
        [rests_at](std::string& seen) { return rests_at(seen, 45, {40, 45}); }));
    steps.push_back(idle_at("idle at 10 while inactive", 10));
    steps.push_back(step(
        "no skim while inactive",
        [&frame, scrub, record_seeks, skim_motion, is_idle] {
            record_seeks();
            sendActivate(frame, false);
            skim_motion(40, true);
            const bool idle = is_idle();
            sendActivate(frame, true);  // restore
            scrub->check = std::format("idle after the motion {}", idle);
            scrub->ok = idle;
        },
        [back_at_10](std::string& seen) { return back_at_10(seen, {}); }));
    steps.push_back(idle_at("idle at 10 for a skim while playing", 10));
    steps.push_back(step(
        "skim while playing does nothing",
        [&frame, record_seeks] {
            record_seeks();
            sendKey(frame, WXK_SPACE);
        },
        [&frame, scrub, skim_motion, is_idle, seeks_text](std::string& seen) {
            if (!scrub->sent) {
                if (frame.transportBar().playIconForTest() != Icon::Pause) {
                    seen = "waiting for the pause icon";
                    return false;
                }
                skim_motion(40, true);
                scrub->sent = true;
                scrub->ok = is_idle() && scrub->seeks.empty();
                scrub->check =
                    std::format("idle after the motion {}, seeks [{}]", is_idle(), seeks_text());
            }
            seen = scrub->check;
            return scrub->ok;
        }));
    steps.push_back(step(
        "pause after the skim while playing",
        [&frame, status] {
            if (status().playing) {
                frame.execute(Command::PlayPause);
            }
        },
        [status, stop_recording](std::string& seen) {
            seen = std::format("playing {}", status().playing);
            if (status().playing) {
                return false;
            }
            stop_recording();
            return true;
        }));
    steps.push_back(idle_at("idle at 10 for the skim key during a drag", 10));
    steps.push_back(step(
        "the skim key during a drag",
        [&frame, scrub, record_seeks, skim_motion, is_idle] {
            TransportBar& bar = frame.transportBar();
            record_seeks();
            bar.setLeftButtonProbeForTest([] { return true; });
            sendSliderTrack(bar.slider(), 30);
            skim_motion(40, true, true);
            const bool scrubbing = bar.scrubState() == ScrubState::Scrubbing;
            bar.setLeftButtonProbeForTest([] { return false; });
            sendSliderReleasePair(bar.slider());
            const bool idle = is_idle();
            scrub->check = std::format("scrubbing after the motion {}, idle after the release {}",
                                       scrubbing, idle);
            scrub->ok = scrubbing && idle;
        },
        [rests_at](std::string& seen) { return rests_at(seen, 30, {30}); }));
    steps.push_back(idle_at("idle at 10 for a stream change while skimming", 10));
    steps.push_back(step(
        "a stream change while skimming",
        [&frame, scrub, record_seeks, skim_motion, skimming, is_idle, has_marker] {
            TransportBar& bar = frame.transportBar();
            record_seeks();
            skim_motion(40, true);
            const bool skimmed = skimming();
            const SessionStatus s = frame.session().status();
            bar.update(BarStatus{.player = s.player,
                                 .hasStream = s.streams > 0,
                                 .streamGeneration = s.streamGeneration + 1});
            const bool idle = is_idle();
            const bool marker = has_marker();
            bar.update(BarStatus{.player = s.player,
                                 .hasStream = s.streams > 0,
                                 .streamGeneration = s.streamGeneration});
            scrub->check = std::format("skimming {}; after the change: idle {}, marker {}", skimmed,
                                       idle, marker);
            scrub->ok =
                skimmed && idle && !marker && bar.streamGenerationForTest() == s.streamGeneration;
        },
        [rests_at](std::string& seen) { return rests_at(seen, 40, {40}); }));
    steps.push_back(idle_at("idle at 10 for a range change while skimming", 10));
    steps.push_back(step(
        "a range change while skimming",
        [&frame, scrub, record_seeks, skim_motion, skimming, is_idle, has_marker] {
            TransportBar& bar = frame.transportBar();
            record_seeks();
            skim_motion(40, true);
            const bool skimmed = skimming();
            const SessionStatus s = frame.session().status();
            PlayerStatus shorter = s.player;
            shorter.length = *s.player.length - 1;
            bar.update(BarStatus{.player = shorter,
                                 .hasStream = s.streams > 0,
                                 .streamGeneration = s.streamGeneration});
            const bool idle = is_idle();
            const bool marker = has_marker();
            bar.update(BarStatus{.player = s.player,
                                 .hasStream = s.streams > 0,
                                 .streamGeneration = s.streamGeneration});
            scrub->check = std::format("skimming {}; after the range change: idle {}, marker {}",
                                       skimmed, idle, marker);
            scrub->ok = skimmed && idle && !marker;
        },
        [rests_at](std::string& seen) { return rests_at(seen, 40, {40}); }));
    // forgetRelease is the bar's own CallAfter, which its destructor drops; only a drag's release
    // posts it. A live bar first shows that the counter moves, then a destroyed one does not.
    struct Gone {
        wxFrame* frame = nullptr;
        int runs = 0;
        int ticks = 0;
        bool ok = false;
        std::string check;
    };
    auto gone = std::make_shared<Gone>();
    // A temporary bar in a temporary frame: a drag to 20 and its release pair.
    auto drag_on_temporary_bar = [gone](bool destroy) {
        *gone = Gone{};
        gone->frame = new wxFrame(nullptr, wxID_ANY, "rawreel selftest bar");
        PlayerStatus player;
        player.length = 60;
        player.position = 10;
        player.target = 10;
        const BarStatus shown{.player = player, .hasStream = true, .streamGeneration = 1};
        auto* bar = new TransportBar(
            gone->frame, [](Command /*command*/) {}, [](std::int64_t /*index*/) {},
            [shown] { return shown; });
        bar->update(shown);
        bar->setLeftButtonProbeForTest([] { return true; });
        sendSliderTrack(bar->slider(), 20);
        const bool scrubbing = bar->scrubState() == ScrubState::Scrubbing;
        gone->runs = TransportBar::forgetReleaseRunsForTest();
        sendSliderReleasePair(bar->slider());  // posts forgetRelease
        const bool idle = bar->scrubState() == ScrubState::Idle;
        if (destroy) {
            bar->Destroy();  // a child: deleted at once
        }
        gone->check = std::format("scrubbing {}, idle after the release {}", scrubbing, idle);
        gone->ok = scrubbing && idle;
    };
    // After two yields: the frame is destroyed; returns how often forgetRelease ran meanwhile.
    auto wait_two_yields = [gone](std::string& seen, int& grew) {
        ++gone->ticks;
        grew = TransportBar::forgetReleaseRunsForTest() - gone->runs;
        seen = std::format("{}; forgetRelease runs {}, ticks {}", gone->check, grew, gone->ticks);
        if (gone->ticks < 3) {
            return false;
        }
        if (gone->frame != nullptr) {
            gone->frame->Destroy();
            gone->frame = nullptr;
        }
        return true;
    };
    steps.push_back(step(
        "a pending call on a live bar", [drag_on_temporary_bar] { drag_on_temporary_bar(false); },
        [gone, wait_two_yields](std::string& seen) {
            int grew = 0;
            return wait_two_yields(seen, grew) && gone->ok && grew == 1;
        }));
    steps.push_back(step(
        "a pending call after the bar is gone",
        [drag_on_temporary_bar] { drag_on_temporary_bar(true); },
        [gone, wait_two_yields](std::string& seen) {
            int grew = 0;
            return wait_two_yields(seen, grew) && gone->ok && grew == 0;
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
                // MSW and macOS: a dialog without a resize border.
                st->textsRead.push_back("fixed size");
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
        [&frame, &app, st] {
            const auto file = st->dir / "a_clip_with_a_rather_long_name_and_no_size.yuv";
            writeFile(file, 720 * 576 * 3 / 2, '\x80');  // 1 frame of yuv420p 720x576
            st->logNext = logNext(app);
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
        [&app, st, stream_is, stream_uri](std::string& seen) {
            const std::vector<std::string>& t = st->textsRead;
            const std::string detection = t.size() > 1 ? t[1] : std::string();
            const bool reason = detection.starts_with("Format not detected: ") &&
                                detection.size() > std::string_view("Format not detected: ").size();
            const std::string log = loggedSince(app, st->logNext);
            const bool logged =
                !detection.empty() && log.contains("needs a format") && log.contains(detection);
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
#if defined(__WXOSX__)
    // Design C5: a file opened from Finder or the Dock (wxApp::MacOpenFiles) takes the route of a
    // drop and gives the drop step's state: a new Y4M file opens without a format dialog.
    steps.push_back(step(
        "open a Y4M file from Finder (MacOpenFiles)",
        [&frame, st] {
            const auto file = st->dir / "finder_clip.y4m";
            std::string y4m = "YUV4MPEG2 W32 H16 F25:1 Ip A1:1 C420jpeg\n";
            for (int i = 0; i < 2; ++i) {
                y4m += "FRAME\n" + std::string(768, '\x50');
            }
            std::ofstream(file, std::ios::binary)
                .write(y4m.data(), static_cast<std::streamsize>(y4m.size()));
            st->dialogs0 = frame.formatDialogsShown();
            wxArrayString names;
            names.Add(wxString::FromUTF8(dvv::media::utf8FromPath(file)));
            wxGetApp().MacOpenFiles(names);
        },
        [&frame, st, stream_is, stream_uri](std::string& seen) {
            const auto& streams = frame.streams();
            const bool y4m = streams.size() == 1 && streams[0].info.provider == "y4m";
            const std::string uri = stream_uri();
            seen = std::format("yuv420p 32x16 {}, y4m {}, dialogs {}, URI '{}'",
                               stream_is(32, 16, "yuv420p"), y4m,
                               frame.formatDialogsShown() - st->dialogs0, uri);
            return stream_is(32, 16, "yuv420p") && y4m && uri.contains("finder_clip.y4m") &&
                   frame.formatDialogsShown() == st->dialogs0;
        }));
#endif
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
        [&frame, &app, st] {
            const auto first = st->dir / "first_32x16_gray.yuv";
            const auto second = st->dir / "second_16x16_gray.yuv";
            writeFile(first, 1536, '\x20');
            writeFile(second, 768, '\x60');
            st->logNext = logNext(app);
            dropFiles(frame.viewport(), {first, second});
        },
        [&app, st, stream_is](std::string& seen) {
            const bool logged =
                loggedSince(app, st->logNext).contains("1 files ignored: one stream at a time");
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
    // Opens the clip again (frame 0) and waits until it is shown.
    auto reopen_clip = [&frame, status, clip_uri](std::string name) {
        return step(
            std::move(name), [&frame, clip_uri] { frame.openUri(0, *clip_uri, {}); },
            [&frame, status](std::string& seen) {
                seen = std::format("streams {}, length {}, position {}", frame.streams().size(),
                                   status().length.value_or(-1), status().position);
                return frame.streams().size() == 1 && status().length == 60 &&
                       status().position == 0;
            });
    };
    steps.push_back(reopen_clip("reopen the clip"));
    // Spec §6.2: a new stream generation during a drag resets the bar to Idle, with no seek back.
    {
        struct Reset {
            std::vector<std::int64_t> seeks;
            std::uint64_t generation = 0;  // before the stream change
            bool scrubbing = false;        // after the track, before the change
        };
        auto reset = std::make_shared<Reset>();
        // Probe down, a track to 30, then `change` (another stream set).
        auto drag_then = [&frame, reset](dvv::MoveOnlyFunction<void()> change) {
            return [&frame, reset, change = std::move(change)]() mutable {
                TransportBar& bar = frame.transportBar();
                *reset =
                    Reset{.seeks = {}, .generation = frame.session().status().streamGeneration};
                bar.setSeekObserverForTest(
                    [reset](std::int64_t value) { reset->seeks.push_back(value); });
                bar.setLeftButtonProbeForTest([] { return true; });
                sendSliderTrack(bar.slider(), 30);
                reset->scrubbing = bar.scrubState() == ScrubState::Scrubbing;
                change();
            };
        };
        // The bar is Idle in the new generation, without a marker, at the new stream's target,
        // and it seeked only to the 30.
        auto reset_done = [&frame, reset](std::string& seen, bool stream_ok) {
            TransportBar& bar = frame.transportBar();
            const SessionStatus full = frame.session().status();
            const bool idle = bar.scrubState() == ScrubState::Idle;
            const std::optional<std::int64_t> marker =
                bar.timelineStripForTest().modelForTest().marker;
            std::string seeks;
            for (const std::int64_t value : reset->seeks) {
                seeks += std::format("{}{}", seeks.empty() ? "" : " ", value);
            }
            seen = std::format(
                "stream {}, scrubbing before {}, generation {} -> {} (bar {}), idle {}, marker {}, "
                "slider {}, "
                "target {}, inflight {}, seeks [{}]",
                stream_ok, reset->scrubbing, reset->generation, full.streamGeneration,
                bar.streamGenerationForTest(), idle, marker.value_or(-1), bar.slider().GetValue(),
                full.player.target, full.inflight, seeks);
            if (!stream_ok || !reset->scrubbing || full.streamGeneration == reset->generation ||
                bar.streamGenerationForTest() != full.streamGeneration || !idle || marker ||
                full.inflight != 0 || frame.lastRevisionForTest() != full.revision ||
                bar.slider().GetValue() != full.player.target ||
                reset->seeks != std::vector<std::int64_t>{30}) {
                return false;
            }
            bar.setSeekObserverForTest(nullptr);
            bar.setLeftButtonProbeForTest(nullptr);
            return true;
        };
        steps.push_back(step("a reopen during a drag resets",
                             drag_then([&frame, clip_uri] { frame.openUri(0, *clip_uri, {}); }),
                             [&frame, reset_done, status](std::string& seen) {
                                 return reset_done(seen, frame.streams().size() == 1 &&
                                                             status().length == 60 &&
                                                             status().position == 0);
                             }));
        if (test.app().startup().ffmpeg.core && mkv_exists) {
            steps.push_back(step("another clip during a drag resets",
                                 drag_then([&frame, mkv] { dropFiles(frame.viewport(), {mkv}); }),
                                 [&frame, reset_done, status](std::string& seen) {
                                     const auto& streams = frame.streams();
                                     return reset_done(
                                         seen, streams.size() == 1 &&
                                                   streams[0].resolvedUri.contains("impl=ffmpeg") &&
                                                   status().length != 60);
                                 }));
            steps.push_back(reopen_clip("reopen the clip after the MKV drag"));
        }
    }
    {
        auto closed = std::make_shared<bool>(false);
        steps.push_back(step(
            "stream change resets the scrub",
            [&frame] {
                TransportBar& bar = frame.transportBar();
                bar.setLeftButtonProbeForTest([] { return true; });
                sendSliderTrack(bar.slider(), 30);
            },
            [&frame, closed](std::string& seen) {
                TransportBar& bar = frame.transportBar();
                if (!*closed) {
                    const bool scrubbing = bar.scrubState() == ScrubState::Scrubbing;
                    seen = std::format("before the close: scrubbing {}", scrubbing);
                    if (!scrubbing) {
                        return false;
                    }
                    frame.execute(Command::CloseStreams);
                    *closed = true;
                }
                const bool idle = bar.scrubState() == ScrubState::Idle;
                const bool enabled = bar.slider().IsEnabled();
                seen = std::format("after the close: idle {}, slider enabled {}", idle, enabled);
                if (!idle || enabled) {
                    return false;
                }
                bar.setLeftButtonProbeForTest(nullptr);
                return true;
            }));
    }
    // The transport buttons (video control panel spec §5.1, §7), by their SetName names.
    static constexpr std::array<std::string_view, 8> BUTTON_NAMES{
        "Open...", "First frame",  "Step back",  "Play/Pause",
        "Stop",    "Step forward", "Last frame", "Loop"};
    auto bar_button = [&frame](std::string_view name) -> wxAnyButton* {
        return dynamic_cast<wxAnyButton*>(
            frame.transportBar().FindWindow(wxString::FromUTF8(name.data(), name.size())));
    };
    {
        auto reopened = std::make_shared<bool>(false);
        steps.push_back(step(
            "buttons wait for a stream", [] {},
            [&frame, status, clip_uri, bar_button, reopened](std::string& seen) {
                std::string states;
                bool all_found = true;
                bool others_enabled = true;
                bool others_disabled = true;
                for (const std::string_view name : BUTTON_NAMES) {
                    const wxAnyButton* const button = bar_button(name);
                    all_found = all_found && button != nullptr;
                    const bool enabled = button != nullptr && button->IsEnabled();
                    states += std::format("{} {}; ", name, enabled);
                    if (name != "Open...") {
                        others_enabled = others_enabled && enabled;
                        others_disabled = others_disabled && !enabled;
                    }
                }
                const wxAnyButton* const open = bar_button("Open...");
                const bool open_enabled = open != nullptr && open->IsEnabled();
                if (!*reopened) {
                    seen = "without a stream: " + states;
                    if (!all_found || !open_enabled || !others_disabled) {
                        return false;
                    }
                    frame.openUri(0, *clip_uri, {});
                    *reopened = true;
                }
                seen = std::format("after the reopen: streams {}, length {}; {}",
                                   frame.streams().size(), status().length.value_or(-1), states);
                return frame.streams().size() == 1 && status().length == 60 && open_enabled &&
                       others_enabled;
            }));
    }
    steps.push_back(step(
        "an unbounded stream keeps the buttons", [] {},
        [&frame, bar_button](std::string& seen) {
            TransportBar& bar = frame.transportBar();
            PlayerStatus unbounded = frame.session().status().player;
            unbounded.length = std::nullopt;
            bar.update(BarStatus{.player = unbounded,
                                 .hasStream = true,
                                 .streamGeneration = frame.session().status().streamGeneration});
            std::string states;
            bool expected = true;
            for (const std::string_view name : BUTTON_NAMES) {
                const wxAnyButton* const button = bar_button(name);
                const bool enabled = button != nullptr && button->IsEnabled();
                states += std::format("{} {}; ", name, enabled);
                expected = expected && enabled == (name != "Last frame");
            }
            const bool slider = bar.slider().IsEnabled();
            const SessionStatus real = frame.session().status();
            bar.update(BarStatus{.player = real.player,
                                 .hasStream = real.streams > 0,
                                 .streamGeneration = real.streamGeneration});
            const wxAnyButton* const last = bar_button("Last frame");
            const bool restored = last != nullptr && last->IsEnabled() && bar.slider().IsEnabled();
            seen = std::format("unbounded: {}slider {}; restored {}", states, slider, restored);
            return expected && !slider && restored;
        }));
    {
        auto paused = std::make_shared<bool>(false);
        steps.push_back(step(
            "the icons follow playback", [&frame] { frame.execute(Command::PlayPause); },
            [&frame, status, paused](std::string& seen) {
                const TransportBar& bar = frame.transportBar();
                const bool pause_icon = bar.playIconForTest() == Icon::Pause;
                if (!*paused) {
                    seen = std::format("playing {}, pause icon {}", status().playing, pause_icon);
                    if (!status().playing || !pause_icon) {
                        return false;
                    }
                    frame.execute(Command::PlayPause);
                    *paused = true;
                }
                const bool play_icon = bar.playIconForTest() == Icon::Play;
                seen = std::format("after the pause: playing {}, play icon {}", status().playing,
                                   play_icon);
                return !status().playing && play_icon;
            }));
    }
    steps.push_back(step(
        "the group is centred", [] {},
        [&frame](std::string& seen) {
            seen.clear();
            TransportBar& bar = frame.transportBar();
            bool centred = true;
            for (const int width_dip : {400, 1000}) {
                bar.SetSize(bar.FromDIP(width_dip), bar.GetSize().y);
                bar.Layout();
                const wxRect group = bar.groupRectForTest();
                const int width = bar.GetClientSize().x;
                const int centre = group.x + (group.width / 2);
                // The buttons' sizer border is 2 DIP (transport_bar.cpp).
                const int open_x = bar.openButtonForTest().GetPosition().x;
                const bool ok = std::abs((2 * centre) - width) <= 2 && open_x == bar.FromDIP(2);
                seen += std::format("{} DIP: width {}, group {}+{}, Open x {}, centred {}; ",
                                    width_dip, width, group.x, group.width, open_x, ok);
                centred = centred && ok;
            }
            frame.playerPanel().Layout();
            return centred;
        }));
    steps.push_back(step(
        "the icons follow a colour change", [] {},
        [&frame, bar_button](std::string& seen) {
            TransportBar& bar = frame.transportBar();
            const wxColour magenta(0xff, 0x00, 0xff);
            const int rebuilds0 = bar.iconRebuildsForTest();
            bar.setSystemColourForTest([magenta](wxSystemColour index) {
                return index == wxSYS_COLOUR_BTNTEXT ? magenta : wxSystemSettings::GetColour(index);
            });
            bar.SendSysColourChangedEvents();
            const wxColour tint = bar.iconTintForTest();
            const int rebuilds = bar.iconRebuildsForTest();
            bool bitmaps = true;
            for (const std::string_view name : BUTTON_NAMES) {
                const wxAnyButton* const button = bar_button(name);
                bitmaps = bitmaps && button != nullptr && button->GetBitmap().IsOk();
            }
            bar.setSystemColourForTest(nullptr);
            bar.SendSysColourChangedEvents();
            const wxColour system = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
            const bool restored = bar.iconTintForTest() == system;
            seen = std::format("tint {}, rebuilds {} -> {}, bitmaps {}, restored {}",
                               tint.GetAsString(wxC2S_HTML_SYNTAX).utf8_string(), rebuilds0,
                               rebuilds, bitmaps, restored);
            frame.playerPanel().Layout();
            return tint == magenta && rebuilds > rebuilds0 && bitmaps && restored;
        }));
    {
        // Spec §5.2, §6.3 Idle: the current label shows `position` over thumbX(target).
        struct OnScreen {
            int phase = 0;  // 0: seek to 10; 1: seek to 30 pending; 2: checked; 3: failed
            std::string failure;
        };
        auto on_screen = std::make_shared<OnScreen>();
        steps.push_back(step(
            "the strip shows the frame on screen",
            [&frame, on_screen] {
                *on_screen = OnScreen{};
                sendSliderChanged(frame.transportBar().slider(), 10);
            },
            [&frame, delay, status, on_screen](std::string& seen) {
                TransportBar& bar = frame.transportBar();
                const PlayerStatus now = status();
                if (on_screen->phase == 0) {
                    seen = std::format("waiting for 10: position {}, target {}", now.position,
                                       now.target);
                    if (now.position != 10 || now.target != 10) {
                        return false;
                    }
                    delay->set(500);
                    sendSliderChanged(bar.slider(), 30);
                    on_screen->phase = 1;
                    return false;
                }
                if (on_screen->phase == 1) {
                    if (now.position != 10) {
                        on_screen->phase = 3;
                        delay->set(0);
                    } else if (now.target == 30) {
                        TimelineStrip& strip = bar.timelineStripForTest();
                        const TimelineModel model = strip.modelForTest();
                        const auto [start, end] = strip.travelForTest();
                        const auto expected = std::lround(
                            thumbX(30, bar.slider().GetMin(), bar.slider().GetMax(), start, end));
                        const auto current = std::ranges::find(model.labels, std::string("current"),
                                                               &TimelineLabel::role);
                        on_screen->failure =
                            current == model.labels.end()
                                ? std::string("no current label")
                                : std::format("current \"{}\" anchor {} (thumb {})", current->text,
                                              current->anchor, expected);
                        if (current != model.labels.end() && current->text == "10" &&
                            std::abs(current->anchor - expected) <= 1) {
                            on_screen->phase = 2;
                            delay->set(0);
                        }
                    }
                    seen = std::format("position {}, target {}: {}", now.position, now.target,
                                       on_screen->failure);
                    return false;
                }
                seen = std::format("{}; position {}", on_screen->failure, now.position);
                return on_screen->phase == 2 && now.position == 30 && now.target == 30;
            },
            std::chrono::seconds(10)));
    }
    // The bar is resized to 400 and then 1000 DIP. GTK lays the native scale out asynchronously,
    // so each size is measured on later polls, once the native track is the same on two polls in
    // a row (and, for the second size, differs from the first).
    struct ResizeCheck {
        std::size_t size = 0;  // index into WIDTHS_DIP
        bool resized = false;
        std::optional<std::pair<int, int>> previous;  // the native track of the previous poll
        std::optional<std::pair<int, int>> firstSize;
        bool follow = true;
        std::vector<std::pair<long, long>> anchors;  // first, last per size
        std::string seen;
    };
    auto resize_check = std::make_shared<ResizeCheck>();
    steps.push_back(step(
        "labels follow a resize", [] {},
        [&frame, resize_check](std::string& seen) {
            static constexpr std::array<int, 2> WIDTHS_DIP{400, 1000};
            ResizeCheck& check = *resize_check;
            TransportBar& bar = frame.transportBar();
            TimelineStrip& strip = bar.timelineStripForTest();
            while (check.size < WIDTHS_DIP.size()) {
                const int width_dip = WIDTHS_DIP.at(check.size);
                if (!check.resized) {
                    bar.SetSize(bar.FromDIP(width_dip), bar.GetSize().y);
                    bar.Layout();
                    check.resized = true;
                    check.previous.reset();
                    seen = check.seen + std::format("{} DIP: resized, waiting", width_dip);
                    return false;
                }
                const std::optional<SliderTrack> now = sliderTrack(bar.slider());
                const std::optional<std::pair<int, int>> native =
                    now ? std::optional(std::pair(now->travelStart, now->travelEnd)) : std::nullopt;
                const bool stable = !now || (check.previous && *check.previous == *native);
                const bool moved = check.size == 0 || !now || native != check.firstSize;
                check.previous = native;
                if (!stable || !moved) {
                    seen = check.seen +
                           std::format("{} DIP: waiting for the native layout", width_dip);
                    return false;
                }
                if (check.size == 0) {
                    check.firstSize = native;
                }
                const TimelineModel model = strip.modelForTest();
                const auto [start, end] = strip.travelForTest();
                const auto first =
                    std::ranges::find(model.labels, std::string("first"), &TimelineLabel::role);
                const auto last =
                    std::ranges::find(model.labels, std::string("last"), &TimelineLabel::role);
                if (first == model.labels.end() || last == model.labels.end()) {
                    check.seen +=
                        std::format("{} DIP: first {}, last {}; ", width_dip,
                                    first != model.labels.end(), last != model.labels.end());
                    check.follow = false;
                    ++check.size;
                    check.resized = false;
                    continue;
                }
                bool ok = std::abs(first->anchor - std::lround(start)) <= 1 &&
                          std::abs(last->anchor - std::lround(end)) <= 1;
                // The strip's shift, checked on its own: the native track start in strip px.
                std::string native_text;
                if (now) {
                    const int start_x = strip
                                            .ScreenToClient(bar.slider().ClientToScreen(
                                                wxPoint(now->travelStart, 0)))
                                            .x;
                    ok = ok && std::abs(std::lround(start) - start_x) <= 1;
                    native_text = std::format(", native start {}", start_x);
                }
                check.seen += std::format(
                    "{} DIP: travel {:.1f}..{:.1f}{}, first {}, last {}, ok {}; ", width_dip, start,
                    end, native_text, first->anchor, last->anchor, ok);
                check.follow = check.follow && ok;
                check.anchors.emplace_back(first->anchor, last->anchor);
                ++check.size;
                check.resized = false;
            }
            seen = check.seen;
            frame.playerPanel().Layout();
            // The bar grows to the right: the travel start (and "0") stays, the end moves by the
            // width change.
            const long grown = bar.FromDIP(1000) - bar.FromDIP(400);
            return check.follow && check.anchors.size() == 2 &&
                   check.anchors[0].first == check.anchors[1].first &&
                   std::abs(check.anchors[1].second - check.anchors[0].second - grown) <= 2;
        }));
    steps.push_back(step(
        "degenerate statuses", [] {},
        [&frame](std::string& seen) {
            TransportBar& bar = frame.transportBar();
            TimelineStrip& strip = bar.timelineStripForTest();
            auto roles = [&strip] {
                std::string text;
                for (const TimelineLabel& label : strip.modelForTest().labels) {
                    text +=
                        std::format("{}{}=\"{}\"", text.empty() ? "" : " ", label.role, label.text);
                }
                return text;
            };
            const PlayerStatus real = frame.session().status().player;
            const std::uint64_t generation = frame.session().status().streamGeneration;
            bar.update(BarStatus{});
            const std::string none = roles();
            PlayerStatus unbounded = real;
            unbounded.length = std::nullopt;
            unbounded.position = 5;
            unbounded.target = 5;
            bar.update(
                BarStatus{.player = unbounded, .hasStream = true, .streamGeneration = generation});
            const std::string open_ended = roles();
            PlayerStatus single = real;
            single.length = 1;
            single.position = 0;
            single.target = 0;
            bar.update(
                BarStatus{.player = single, .hasStream = true, .streamGeneration = generation});
            const std::string one = roles();
            PlayerStatus unshown_status = real;
            unshown_status.position = -1;
            bar.update(BarStatus{
                .player = unshown_status, .hasStream = true, .streamGeneration = generation});
            const std::string unshown = roles();
            PlayerStatus huge = real;
            huge.length = 3'000'000'000;
            bar.update(
                BarStatus{.player = huge, .hasStream = true, .streamGeneration = generation});
            const std::string clamped = roles();
            const SessionStatus restored = frame.session().status();
            bar.update(BarStatus{.player = restored.player,
                                 .hasStream = restored.streams > 0,
                                 .streamGeneration = restored.streamGeneration});
            seen = std::format(
                "default [{}]; unbounded [{}]; length 1 [{}]; position -1 [{}]; length 3e9 [{}]",
                none, open_ended, one, unshown, clamped);
            return none.empty() && open_ended.empty() && one == "first=\"0\"" && !unshown.empty() &&
                   !unshown.contains("current") && clamped.contains("last=\"2147483647\"");
        }));
    steps.push_back(step(
        "the strip follows a colour change", [] {},
        [&frame](std::string& seen) {
            TransportBar& bar = frame.transportBar();
            const TimelineStrip& strip = bar.timelineStripForTest();
            const wxColour magenta(0xff, 0x00, 0xff);
            bar.setSystemColourForTest([magenta](wxSystemColour index) {
                return index == wxSYS_COLOUR_BTNTEXT ? magenta : wxSystemSettings::GetColour(index);
            });
            bar.SendSysColourChangedEvents();
            const wxColour text = strip.textColourForTest();
            bar.setSystemColourForTest(nullptr);
            bar.SendSysColourChangedEvents();
            const wxColour system = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
            const bool restored = strip.textColourForTest() == system;
            seen = std::format("text {}, restored {}",
                               text.GetAsString(wxC2S_HTML_SYNTAX).utf8_string(), restored);
            frame.playerPanel().Layout();
            return text == magenta && restored;
        }));
    steps.push_back(step(
        "close the clip after the buttons", [&frame] { frame.execute(Command::CloseStreams); },
        [&frame](std::string& seen) {
            seen = std::format("streams {}", frame.streams().size());
            return frame.streams().empty();
        }));
    steps.push_back(step(
        "no scrub on a disabled slider",
        [&frame] { sendSliderTrack(frame.transportBar().slider(), 10); },
        [&frame](std::string& seen) {
            const bool idle = frame.transportBar().scrubState() == ScrubState::Idle;
            seen = std::format("idle {}, streams {}", idle, frame.streams().size());
            return idle && frame.streams().empty();
        }));
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
    // Last: the app closes with the seek still pending; App::shutdownSession() stops the delay.
    steps.push_back(reopen_clip("reopen the clip for the close"));
    steps.push_back(step(
        "close during a slow seek",
        [&frame, delay] {
            // Above the parser's 60000: a close that did not wake it would outlast the CTest
            // TIMEOUT.
            delay->set(600000);
            sendSliderTrack(frame.transportBar().slider(), 45);
        },
        [](std::string& seen) {
            seen = "the seek is pending";
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
