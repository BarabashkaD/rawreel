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
#include <wx/settings.h>
#include <wx/slider.h>

#include "core/commands.h"
#include "core/format_choice.h"
#include "core/perf_stats.h"
#include "core/session.h"
#include "core/view.h"
#include "dvv/base/error.h"
#include "dvv/base/json.h"
#include "dvv/base/log.h"
#include "dvv/base/uri.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/gpu_info_json.h"
#include "dvv/gpu/presenter.h"
#include "dvv/gpu/testing/fault_injection.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/types.h"
#include "dvv/platform/native_window.h"
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

// Sets a flag for the lifetime of the guard (the busy guard of the runner, LOG 20).
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

}  // namespace

SelfTest::SelfTest(MainFrame& frame, App& app) : frame_(&frame), app_(&app) {}

SelfTest::~SelfTest() {
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
    std::optional<std::uint64_t> framesWhenShown;  // open stream: when the frame was first seen
    std::size_t logLength = 0;                     // menus: the Log pane's text at the step start
};

std::optional<dvv::gpu::PresenterStats> statsOf(const Viewport& viewport) {
    if (const dvv::gpu::Presenter* presenter = viewport.presenter(); presenter != nullptr) {
        return presenter->stats();
    }
    return std::nullopt;
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

// Logical pixels (no FromDIP): the checks compare with the physical size, so any DPI scale
// passes, and every size differs from the one before it at every scale. `size` is the viewport's
// size: the frame grows by what the panes, the infobar and the borders around the viewport take
// (the docked panes keep their size when the frame is resized).
SelfTestStep resizeStep(SelfTest* test, const SharedState& state, std::string name, wxSize size) {
    auto start = [test, state, size] {
        markStart(test->frame().viewport(), *state);
        MainFrame& frame = test->frame();
        const wxSize around = frame.GetClientSize() - frame.viewport().GetClientSize();
        frame.SetClientSize(size + around);
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

// 4. Hidden: no frames (one in flight when the window was hidden is tolerated).
SelfTestStep hideStep(SelfTest* test, const SharedState& state) {
    auto start = [test, state] {
        markStart(test->frame().viewport(), *state);
#if defined(_WIN32)
        test->frame().Iconize(true);
#else
        test->frame().Hide();  // compiled, not run in v1.0 (U-WIN)
#endif
    };
    auto passed = [test, state](std::string& seen) {
        const Viewport& viewport = test->frame().viewport();
        seen = describe(viewport, *state);
        const std::optional<dvv::gpu::PresenterStats> stats = statsOf(viewport);
        return ++state->ticks >= HIDE_TICKS && stats &&
               stats->framesPresented <= state->startFrames + 1 && stats->hidden && !stats->failed;
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
        test->frame().Show();
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
        const std::array<wxString, 3> texts = test->frame().statusTexts();
        seen = std::format("status bar '{}' '{}' '{}'", texts[0].utf8_string(),
                           texts[1].utf8_string(), texts[2].utf8_string());
        return texts[0].StartsWith("frame ") && texts[1].EndsWith("fps");
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

// Design §7: a Session error reaches the infobar; slot 1 is refused in v1.0.
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
        const bool refused = frame.lastMessage().Contains("one stream in v1.0");
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

}  // namespace

std::vector<SelfTestStep> smokeScenario(SelfTest& test) {
    SelfTest* const t = &test;
    const auto state = std::make_shared<SmokeState>();
    std::vector<SelfTestStep> steps;
    steps.push_back(firstFramesStep(t, state));
    steps.push_back(resizeStep(t, state, "resize 800x600", wxSize(800, 600)));
    steps.push_back(resizeStep(t, state, "resize 333x177", wxSize(333, 177)));
    steps.push_back(hideStep(t, state));
    steps.push_back(restoreStep(t, state));
    steps.push_back(openStreamStep(t, state));
    steps.push_back(resizeStep(t, state, "resize with a stream", wxSize(640, 360)));
    steps.push_back(panesStep(t));
    steps.push_back(menuTransportStep(t));
    steps.push_back(pauseStep(t));
    steps.push_back(statusBarStep(t));
    steps.push_back(diagnosticsStep(t));
    steps.push_back(menusStep(t, state));
    steps.push_back(infobarStep(t));
    steps.push_back(appearanceStep(t));
    return steps;
}

// --- lost (design v2 §8.4 lost, D7, D25, E3) -----------------------------------------------------

namespace {

std::uint64_t presented(Viewport& viewport) {
    const dvv::gpu::Presenter* presenter = viewport.presenter();
    return presenter != nullptr ? presenter->stats().framesPresented : 0;
}

}  // namespace

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

int showModal(FormatDialog& dialog) {
    std::move_only_function<void(FormatDialog&)>& slot = dialogHooks().format;
    if (slot) {
        // One shot; CallAfter needs a copyable functor, so the hook is shared.
        auto hook = std::make_shared<std::move_only_function<void(FormatDialog&)>>(std::move(slot));
        slot = nullptr;
        dialog.CallAfter([&dialog, hook] {
            (*hook)(dialog);
            dialog.EndModal(wxID_OK);
        });
    } else if (App::arguments().selftest != Scenario::None) {
        // A dialog the script did not expect: cancel it, so the run never waits for a user.
        dialog.CallAfter([&dialog] {
            DVV_LOG_WARN("rawreel-gui --selftest: a format dialog without a hook was cancelled");
            dialog.EndModal(wxID_CANCEL);
        });
    }
    return dialog.ShowModal();
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
    frame.GetEventHandler()->ProcessEvent(event);
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

// The Info line of a colour field without "(assumed)" right after it, e.g. "matrix bt601,".
bool infoHas(MainFrame& frame, std::string_view field) {
    const std::string text = frame.infoPanel().text().utf8_string();
    const std::size_t at = text.find(field);
    return at != std::string::npos && text.compare(at + field.size(), 10, " (assumed)") != 0;
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
        [&frame, &history, st] {
            frame.execute(Command::Loop);
            st->mark = history.size();
            frame.execute(Command::PlayPause);
        },
        [&history, st, status](std::string& seen) {
            const auto shows = history.since(st->mark);
            const bool wrapped =
                std::ranges::any_of(shows, [](const ShownSet& s) { return s.index == 0; });
            seen = std::format("loop {}, shows {}", status().loop, shows.size());
            return status().loop && wrapped && consecutive(shows, 1, 60, seen);
        }));
    steps.push_back(step(
        "pause",
        [&frame] {
            frame.execute(Command::PlayPause);
            frame.execute(Command::Loop);  // loop off again
        },
        [status](std::string& seen) {
            seen = std::format("playing {}, loop {}", status().playing, status().loop);
            return !status().playing && !status().loop;
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
            const double fit = ViewTransform::fit(extent, {64, 48}).zoom();
            seen = std::format("extent {}x{} (was {}x{}), recorded {}x{} zoom {} (fit {})",
                               extent.width, extent.height, st->extent0.width, st->extent0.height,
                               recorded ? recorded->extent.width : 0,
                               recorded ? recorded->extent.height : 0,
                               recorded ? recorded->zoom : 0.0, fit);
            return !(extent == st->extent0) && extent.width > 0 && extent.height > 0 && recorded &&
                   recorded->extent == extent && recorded->zoom == fit &&
                   presented(frame.viewport()) > st->presented0;
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
        "View > Format... sets BT.601 and full range",
        [&frame] {
            // The position stepped to above (> 0), so a reopen that loses it fails the check.
            dialogHooks().format = [](FormatDialog& dialog) {
                FormatChoice choice = dialog.choice();
                choice.colour.matrix = dvv::media::ColourMatrix::Bt601;
                choice.colour.range = dvv::media::ColourRange::Full;
                dialog.setChoice(choice);
            };
            frame.execute(Command::Format);  // ShowModal; the hook fills and closes the dialog
        },
        [&frame, st, status](std::string& seen) {
            const bool info = infoHas(frame, "matrix bt601,") && infoHas(frame, "range full,");
            seen = std::format("Info bt601/full {}, position {} (kept {}), playing {}", info,
                               status().position, st->position, status().playing);
            return info && st->position > 0 && status().position == st->position &&
                   !status().playing;
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
                frame.logPanel().text().Contains("1 files ignored: one stream in v1.0");
            seen = std::format("gray 32x16 {}, log line {}", stream_is(32, 16, "gray"), logged);
            return stream_is(32, 16, "gray") && logged;
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
        std::mt19937_64 random{1};
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
