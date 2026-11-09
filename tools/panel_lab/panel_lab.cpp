// rawreel-panel-lab (video control panel spec §10.1): the real TransportBar fed by a simulated
// player, for the manual checks, and a --report self-check of the native slider queries (spec
// §5.3). Not packaged, not linked into rawreel-gui. The simulated player runs on lab-only
// wxTimers; the no-timer rule binds the product, not this lab.
//
//   rawreel-panel-lab [--frames N] [--fps F] [--seek-delay MS] [--report] [--debug-transport]
//
// The bar's timeline strip draws the current number over the thumb x the travel formula computes,
// for the manual check against the drawn thumb. --report prints the native track, the native and
// computed thumb centres at the minimum, middle and maximum value, and the bar's best size, at the
// initial width and with a 400 DIP wide slider (the strip's minimum width); it exits 1 when a
// centre is more than 2 px off, a query is missing on Windows, GTK or macOS, or a stage times out.

// clang-format off
#include <wx/wx.h>  // first: wx's Windows headers before anything else includes <windows.h> (R11)
// clang-format on

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <wx/platinfo.h>
#include <wx/timer.h>

#include "core/commands.h"
#include "core/gui_arguments.h"
#include "core/player.h"
#include "core/timeline.h"
#include "dvv/base/log.h"
#include "gui/native_controls.h"
#include "gui/skim_key.h"
#include "gui/transport_bar.h"
#if !defined(_WIN32) && !defined(__APPLE__)
#include "gui/gui_platform.h"
#endif

namespace {

constexpr int FAILED = 1;
constexpr int BAD_ARGUMENTS = 2;
constexpr int INITIAL_CLIENT_WIDTH_DIP = 800;
constexpr int REPORT_SLIDER_WIDTH_DIP = 400;
constexpr int STAGE_TIMEOUT_MS = 5000;
constexpr int MAX_ERROR_PX = 2;
// The toolkits with a native slider query (native_controls_*.cpp): --report fails without one.
#if defined(__WXMSW__) || defined(__WXGTK__) || defined(__WXOSX_COCOA__)
constexpr bool NATIVE_QUERY = true;
#else
constexpr bool NATIVE_QUERY = false;
#endif
// Whether a stage waits for a paint before measuring. The Windows trackbar answers its geometry
// messages at once after TBM_SETPOS, and wx sends no wxEVT_PAINT for it after a SetValue (seen in
// CI: the report timed out waiting), so there the two CallAfter hops are enough.
#if defined(__WXMSW__)
constexpr bool WAIT_FOR_PAINT = false;
#else
constexpr bool WAIT_FOR_PAINT = NATIVE_QUERY;
#endif

constexpr char USAGE[] =
    "usage: rawreel-panel-lab [--frames N] [--fps F] [--seek-delay MS] [--report]\n"
    "                         [--debug-transport]\n"
    "  --frames N       the simulated stream's length (default 4567)\n"
    "  --fps F          its playback rate (default 25)\n"
    "  --seek-delay MS  a seek shows its frame after MS milliseconds (default 0)\n"
    "  --report         measure the native slider queries, print them and exit\n"
    "  --debug-transport  log the bar's scrub and skim steps to stderr\n";

struct LabOptions {
    std::int64_t frames = 4567;
    double fps = 25;
    int seekDelayMs = 0;
    bool report = false;
    bool debugTransport = false;
};

LabOptions& labOptions() {
    static LabOptions options;
    return options;
}

int& labExitCode() {
    static int code = 0;
    return code;
}

void print(const std::string& line) {
    std::fputs((line + "\n").c_str(), stdout);
    std::fflush(stdout);
}

template <class Int>
bool parseInt(std::string_view text, Int low, Int high, Int& out) {
    Int value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < low || value > high) {
        return false;
    }
    out = value;
    return true;
}

bool parseFps(std::string_view text, double& out) {
    const std::string copy(text);
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (copy.empty() || end != copy.c_str() + copy.size() || !(value > 0) || value > 1000) {
        return false;
    }
    out = value;
    return true;
}

// nullopt: the arguments are good; else the exit code (0 after --help).
std::optional<int> parseOptions(std::span<const std::string_view> args, LabOptions& options) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--help") {
            std::fputs(USAGE, stdout);
            return 0;
        }
        if (arg == "--debug-transport") {
            options.debugTransport = true;
            continue;
        }
        if (arg == "--report") {
            options.report = true;
            continue;
        }
        if (arg != "--frames" && arg != "--fps" && arg != "--seek-delay") {
            std::fprintf(stderr, "rawreel-panel-lab: unknown argument '%.*s'\n\n%s",
                         static_cast<int>(arg.size()), arg.data(), USAGE);
            return BAD_ARGUMENTS;
        }
        if (i + 1 == args.size()) {
            std::fprintf(stderr, "rawreel-panel-lab: option %.*s needs a value\n",
                         static_cast<int>(arg.size()), arg.data());
            return BAD_ARGUMENTS;
        }
        const std::string_view value = args[++i];
        const bool good =
            arg == "--frames"
                ? parseInt<std::int64_t>(value, 0, 1'000'000'000, options.frames)
                : (arg == "--fps" ? parseFps(value, options.fps)
                                  : parseInt<int>(value, 0, 60'000, options.seekDelayMs));
        if (!good) {
            std::fprintf(stderr, "rawreel-panel-lab: bad value '%.*s' for %.*s\n",
                         static_cast<int>(value.size()), value.data(), static_cast<int>(arg.size()),
                         arg.data());
            return BAD_ARGUMENTS;
        }
    }
    return std::nullopt;
}

// The lab's fake player: it answers the bar's commands and seeks like the real one in outline (a
// seek or step pauses; the target moves at once, the position after --seek-delay) and notifies
// after every change.
class SimulatedPlayer {
public:
    SimulatedPlayer(const LabOptions& options,
                    std::function<void(const rawreel::PlayerStatus&)> notify)
        : frames_(options.frames),
          seek_delay_ms_(options.seekDelayMs),
          tick_ms_(std::max(1, static_cast<int>(std::lround(1000.0 / options.fps)))),
          notify_(std::move(notify)) {
        status_.length = frames_;
        status_.position = frames_ > 0 ? 0 : -1;
        status_.target = status_.position;
        status_.targetFps = options.fps;
        play_timer_.Bind(wxEVT_TIMER, [this](wxTimerEvent& /*event*/) { onTick(); });
        seek_timer_.Bind(wxEVT_TIMER, [this](wxTimerEvent& /*event*/) {
            status_.position = status_.target;
            notify_(status_);
        });
    }

    [[nodiscard]] const rawreel::PlayerStatus& status() const noexcept { return status_; }

    void execute(rawreel::Command command) {
        if (frames_ <= 0 && command != rawreel::Command::Loop) {
            return;
        }
        switch (command) {
            case rawreel::Command::PlayPause:
                if (status_.playing) {
                    pause();
                } else {
                    status_.playing = true;
                    play_timer_.Start(tick_ms_);
                }
                notify_(status_);
                break;
            case rawreel::Command::Stop:  // pause and seek to the start
            case rawreel::Command::First: seek(0); break;
            case rawreel::Command::Last: seek(frames_ - 1); break;
            case rawreel::Command::StepBack: step(-1); break;
            case rawreel::Command::StepForward: step(1); break;
            case rawreel::Command::Loop:
                status_.loop = !status_.loop;
                notify_(status_);
                break;
            default: print("lab: command not simulated"); break;
        }
    }

    void seek(std::int64_t index) {
        if (frames_ <= 0) {
            return;
        }
        pause();
        status_.target = std::clamp<std::int64_t>(index, 0, frames_ - 1);
        if (seek_delay_ms_ == 0) {
            status_.position = status_.target;
        } else {
            seek_timer_.StartOnce(seek_delay_ms_);
        }
        notify_(status_);
    }

private:
    void pause() {
        status_.playing = false;
        play_timer_.Stop();
    }

    void step(std::int64_t delta) {
        std::int64_t next = status_.target + delta;
        if (status_.loop) {
            next = ((next % frames_) + frames_) % frames_;
        }
        seek(next);
    }

    void onTick() {
        if (status_.position != status_.target) {
            return;  // a delayed seek is still pending
        }
        std::int64_t next = status_.position + 1;
        if (next >= frames_) {
            if (!status_.loop) {
                pause();
                notify_(status_);
                return;
            }
            next = 0;
        }
        status_.position = next;
        status_.target = next;
        notify_(status_);
    }

    std::int64_t frames_;
    int seek_delay_ms_;
    int tick_ms_;
    rawreel::PlayerStatus status_;
    std::function<void(const rawreel::PlayerStatus&)> notify_;
    wxTimer play_timer_;
    wxTimer seek_timer_;
};

class LabFrame final : public wxFrame {
public:
    explicit LabFrame(const LabOptions& options) : wxFrame(nullptr, wxID_ANY, "rawreel panel lab") {
        auto* root = new wxPanel(this);
        bar_ = new rawreel::TransportBar(
            root, [this](rawreel::Command command) { player_->execute(command); },
            [this](std::int64_t index) { player_->seek(index); },
            [this] { return rawreel::BarStatus{.player = player_->status(), .hasStream = true}; });
        bar_->setTrace(options.debugTransport);
        player_ =
            std::make_unique<SimulatedPlayer>(options, [this](const rawreel::PlayerStatus& status) {
                bar_->update(rawreel::BarStatus{.player = status, .hasStream = true});
            });
        // --report waits for a paint of the slider (MSW, macOS: wx sends wxEVT_PAINT to native
        // controls) or of the bar (a size change that leaves the slider alone; on wxGTK, which
        // sends none to a GtkScale, every stage: GTK draws the bar after its layout pass has
        // placed the scale's thumb).
        slider().Bind(wxEVT_PAINT, &LabFrame::onSourcePaint, this);
        bar_->Bind(wxEVT_PAINT, &LabFrame::onSourcePaint, this);
        deadline_.Bind(wxEVT_TIMER, [this](wxTimerEvent& /*event*/) {
            if (finished_) {
                return;
            }
            print("timeout at " + waiting_for_);
            finish(FAILED);
        });
        // Wired as in MainFrame: the bar's activation and the skim key's release.
        Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& event) {
            if (!IsBeingDeleted()) {
                bar_->frameActivated(event.GetActive());
            }
            event.Skip();
        });
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(bar_, wxSizerFlags().Expand());
        root->SetSizer(sizer);
        bar_->update(rawreel::BarStatus{.player = player_->status(), .hasStream = true});
        SetClientSize(FromDIP(wxSize(INITIAL_CLIENT_WIDTH_DIP, 0)).x, root->GetBestSize().y);
    }

    void syncActivation() { bar_->frameActivated(IsActive()); }

    // Before Show(): the first stage waits for the first paint.
    void startReport() {
        print(std::format("port {} dpi scale {:.2f}",
                          wxPlatformInfo::Get().GetPortIdName().utf8_string(),
                          GetDPIScaleFactor()));
        waitThenRun("initial size", [this] {
            measureStage("initial size", [this] {
                // The frame grows or shrinks by what the slider lacks of 400 DIP: a 400 DIP frame
                // would squeeze the one-row bar's slider to nothing.
                const int change =
                    FromDIP(wxSize(REPORT_SLIDER_WIDTH_DIP, 0)).x - slider().GetClientSize().x;
                SetClientSize(GetClientSize().x + change, GetClientSize().y);
                actionDone();
                waitThenRun("slider 400 DIP", [this] {
                    measureStage("slider 400 DIP", [this] { finish(failed_ ? FAILED : 0); });
                });
            });
        });
    }

private:
    wxSlider& slider() { return bar_->slider(); }

    std::optional<double> computedThumbX(int value) {
        const std::optional<rawreel::SliderTrack> track = rawreel::sliderTrack(slider());
        if (!track) {
            return std::nullopt;
        }
        return rawreel::thumbX(value, slider().GetMin(), slider().GetMax(), track->travelStart,
                               track->travelEnd);
    }

    // After the action that changed the size or the value. On wxGTK the bar must be drawn again:
    // a value change alone redraws the scale, which sends the lab nothing.
    void actionDone() {
#if defined(__WXGTK__)
        bar_->Refresh();
#endif
    }

    // Runs `next` after the next paint of the paint source and two CallAfter hops (the native
    // drawing is done by then), or after the two hops alone where no paint is known to arrive.
    // Fails the report after STAGE_TIMEOUT_MS.
    void waitThenRun(std::string stage, std::function<void()> next) {
        waiting_for_ = std::move(stage);
        next_ = std::move(next);
        deadline_.StartOnce(STAGE_TIMEOUT_MS);
        if constexpr (WAIT_FOR_PAINT) {
            armed_ = true;
        } else {
            settle();
        }
    }

    void onSourcePaint(wxPaintEvent& event) {
        event.Skip();  // the native control and the bar paint themselves
        if (armed_ && !finished_) {
            armed_ = false;
            settle();
        }
    }

    // Every stage callback runs from here; none after finish() (a late paint or queued hops
    // before the deferred Destroy()).
    void settle() {
        CallAfter([this] {
            CallAfter([this] {
                if (finished_ || !next_) {
                    return;
                }
                deadline_.Stop();
                std::function<void()> next = std::move(next_);
                next_ = nullptr;
                next();
            });
        });
    }

    void measureStage(const std::string& stage, std::function<void()> then) {
        const wxSize client = ToDIP(GetClientSize());
        print(std::format("stage {}: client {}x{} DIP, slider width {} px", stage, client.x,
                          client.y, slider().GetClientSize().x));
        const std::optional<rawreel::SliderTrack> track = rawreel::sliderTrack(slider());
        if (track) {
            print(std::format("track start {} end {} thumb {}", track->travelStart,
                              track->travelEnd, track->thumbWidth));
            if (track->travelStart < 0 || track->travelEnd > slider().GetClientSize().x ||
                track->travelEnd <= track->travelStart) {
                print("FAIL: the track is not inside the slider");
                failed_ = true;
            }
        } else {
            print("track none");
            failed_ = failed_ || NATIVE_QUERY;
        }
        const int min = slider().GetMin();
        const int max = slider().GetMax();
        measureValues(stage, {min, min + ((max - min) / 2), max}, 0, std::move(then));
    }

    void measureValues(const std::string& stage, std::vector<int> values, std::size_t index,
                       std::function<void()> then) {
        if (index == values.size()) {
            const wxSize best = bar_->ToDIP(bar_->GetBestSize());
            print(std::format("best {}x{} DIP", best.x, best.y));
            then();
            return;
        }
        const int value = values[index];
        auto measure = [this, stage, values, index, value, then = std::move(then)]() mutable {
            printValue(value);
            measureValues(stage, std::move(values), index + 1, std::move(then));
        };
        if (slider().GetValue() == value) {
            measure();  // nothing changes, so nothing would be drawn
            return;
        }
        slider().SetValue(value);
        actionDone();
        waitThenRun(std::format("{} value {}", stage, value), std::move(measure));
    }

    void printValue(int value) {
        const std::optional<int> native = rawreel::nativeThumbCentre(slider());
        const std::optional<double> computed = computedThumbX(value);
        if (!native || !computed) {
            print(std::format("value {} native none computed none", value));
            failed_ = failed_ || NATIVE_QUERY;
            return;
        }
        const int rounded = static_cast<int>(std::lround(*computed));
        const int error = *native - rounded;
        print(
            std::format("value {} native {} computed {} error {}", value, *native, rounded, error));
        if (std::abs(error) > MAX_ERROR_PX) {
            print(std::format("FAIL: |error| > {} px", MAX_ERROR_PX));
            failed_ = true;
        }
    }

    // One-shot: the first result (a timeout or the end of the report) is the exit code.
    void finish(int code) {
        if (finished_) {
            return;
        }
        finished_ = true;
        armed_ = false;
        next_ = nullptr;
        labExitCode() = code;
        deadline_.Stop();
        Destroy();
    }

    rawreel::TransportBar* bar_ = nullptr;
    rawreel::SkimKeyRelease skim_key_release_{[this] {
        if (bar_ != nullptr && !IsBeingDeleted()) {
            bar_->skimKeyReleased();
        }
    }};
    std::unique_ptr<SimulatedPlayer> player_;
    wxTimer deadline_;
    std::string waiting_for_;
    std::function<void()> next_;
    bool armed_ = false;
    bool failed_ = false;
    bool finished_ = false;
};

}  // namespace

// Outside the anonymous namespace: wxIMPLEMENT_APP_NO_MAIN defines wxGetApp() for it.
class LabApp final : public wxApp {
public:
    // Not wxApp::OnInit(): the arguments were parsed before wxEntry().
    bool OnInit() override {
        auto* frame = new LabFrame(labOptions());
        if (labOptions().report) {
            frame->startReport();
        }
        frame->Show();
        // The frame may never become active: no skim until it does.
        frame->syncActivation();
        return true;
    }
};

wxIMPLEMENT_APP_NO_MAIN(LabApp);

namespace {

// Parses, then runs wx through `start_wx`; returns the process exit code.
template <class StartWx>
int run(std::span<const std::string_view> args, StartWx start_wx) {
    if (const std::optional<int> code = parseOptions(args, labOptions()); code) {
        return *code;
    }
#if !defined(_WIN32) && !defined(__APPLE__)
    // As rawreel-gui: XInitThreads(), GDK limited to X11, then the DISPLAY check.
    if (const std::optional<std::string> problem = rawreel::prepareX11StartUp(); problem) {
        std::fprintf(stderr, "rawreel-panel-lab: %s\n", problem->c_str());
        return rawreel::skipExitCode();
    }
#endif
    if (labOptions().debugTransport) {
        // The trace lines are DVV_LOG_INFO: they reach stderr through a sink, as in rawreel-gui.
        dvv::Logger::instance().addSink(std::make_shared<dvv::StderrSink>());
    }
    if (start_wx() != 0) {
        std::fputs("rawreel-panel-lab: the GUI could not start\n", stderr);
        return FAILED;
    }
    return labExitCode();
}

}  // namespace

#if defined(_WIN32)
// A console program with wide arguments: the console entry point calls wmain.
int wmain(int argc, wchar_t** argv) {
    try {
        std::vector<std::string> utf8;
        for (int i = 1; i < argc; ++i) {
            utf8.push_back(wxString(argv[i]).utf8_string());
        }
        const std::vector<std::string_view> args(utf8.begin(), utf8.end());
        return run(args, [&argc, argv] { return wxEntry(argc, argv); });
    } catch (const std::exception& error) {
        std::fprintf(stderr, "rawreel-panel-lab: %s\n", error.what());
        return FAILED;
    }
}
#else
int main(int argc, char** argv) {
    try {
        const std::vector<std::string_view> args(argv + 1, argv + argc);
        return run(args, [&argc, argv] { return wxEntry(argc, argv); });
    } catch (const std::exception& error) {
        std::fprintf(stderr, "rawreel-panel-lab: %s\n", error.what());
        return FAILED;
    }
}
#endif
