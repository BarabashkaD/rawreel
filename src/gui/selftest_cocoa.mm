// The selftest steps that only wxOSX (Cocoa) has: Edit -> Copy and ⌘W in the Info window, the
// backing scale change, the application menu, ⌘W with the Settings window in front, and the files
// that Finder opens.
// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/session.h"
#include "core/view.h"
#include "dvv/base/log.h"
#include "dvv/gpu/presenter.h"
#include "dvv/platform/pixel_size.h"
#include "gui/app.h"
#include "gui/main_frame.h"
#include "gui/native_window.h"
#include "gui/selftest.h"
#include "gui/selftest_detail.h"
#include "gui/settings.h"
#include "gui/viewport.h"

namespace rawreel {
namespace {

// §10 Cmd+W: with the focus in the Info window, File -> Close stream (⌘W) closes the window, not
// the stream; with no stream it is still available there and closes the window. Ends with the
// smoke stream open again and the window shown.
SelfTestStep infoWindowCloseKeyStep(SelfTest* test, const std::shared_ptr<InfoKeysState>& state) {
    // ⌘W with the focus in the (shown) Info window: whether Close stream was available.
    const auto close_key = [test] {
        MainFrame& frame = test->frame();
        frame.setFocusForTest(shownInfoContent(frame));
        const bool available = frame.isAvailable(Command::CloseStreams);
        frame.execute(Command::CloseStreams, CommandSource::Menu);
        frame.setFocusForTest(nullptr);
        return available;
    };
    auto start = [test, state, close_key] {
        state->phase = 0;
        state->availableAgain = false;
        state->hiddenAgain = false;
        state->streamsBefore = test->frame().session().status().streams;
        state->available = close_key();
    };
    auto passed = [test, state, close_key](std::string& seen) {
        MainFrame& frame = test->frame();
        const std::uint32_t streams = frame.session().status().streams;
        seen = std::format(
            "phase {}: available {} (no stream {}), closed by the second ⌘W {}, infoShown {}, {} "
            "session streams (was {}), {} frame streams",
            state->phase, state->available, state->availableAgain, state->hiddenAgain,
            frame.infoShown(), streams, state->streamsBefore, frame.streams().size());
        switch (state->phase) {
            case 0:
                if (state->available && !frame.infoShown() && state->streamsBefore == 1 &&
                    streams == 1 && frame.streams().size() == 1) {
                    DVV_LOG_INFO("selftest: info window close key: {}", seen);
                    state->phase = 1;
                    frame.execute(Command::CloseStreams);  // the stream itself (no window in front)
                }
                return false;
            case 1:
                if (streams == 0 && frame.streams().empty()) {
                    state->availableAgain = close_key();
                    state->hiddenAgain = !frame.infoShown();
                    DVV_LOG_INFO("selftest: info window close key: {}", seen);
                    state->phase = 2;
                    openSmokeStream(test);     // checked in a later turn
                    frame.setInfoShown(true);  // the scenario's close covers quitting with it open
                }
                return false;
            default:
                return state->availableAgain && state->hiddenAgain && frame.infoShown() &&
                       streams == 1 && frame.streams().size() == 1;
        }
    };
    return step("⌘W closes the Info window", std::move(start), std::move(passed));
}

// Design C5: About and Quit carry the standard ids, so wxOSX shows them in the application menu
// as "About rawreel" and "Quit rawreel" (and hides them in Help and File); the application menu's
// About opens Help -> About.
SelfTestStep appMenuStep(SelfTest* test, bool opens_about) {
    struct Seen {
        std::uint64_t about0 = 0;
        bool hookRan = false;
    };
    auto seen_state = std::make_shared<Seen>();
    auto start = [test, seen_state, opens_about] {
        if (!opens_about) {
            return;  // smoke: the menu only, no About window
        }
        MainFrame& frame = test->frame();
        seen_state->about0 = frame.aboutShown();
        dialogHooks().about = [seen_state](wxGenericAboutDialog& /*dialog*/) {
            seen_state->hookRan = true;
        };
        wxCommandEvent about(wxEVT_MENU, wxID_ABOUT);  // what the application menu's item sends
        about.SetEventObject(&frame);
        injectInput(*frame.GetEventHandler(), about);
    };
    auto passed = [test, seen_state, opens_about](std::string& seen) {
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
        seen = opens_about
                   ? std::format("application menu '{}' '{}' '{}', standard ids {}, About shown {}",
                                 about, settings, quit, ids, shown)
                   : std::format("application menu '{}' '{}' '{}', standard ids {}", about,
                                 settings, quit, ids);
        return about == "About rawreel" && settings == "Settings..." && quit == "Quit rawreel" &&
               ids && (shown || !opens_about);
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
// the frame (a hidden stand-in for a floating Log pane, a wxAuiFloatingFrame), the item
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

}  // namespace

void copyInInfoWindow(MainFrame& frame, const std::shared_ptr<InfoKeysState>& state) {
    wxWindow* const panel = shownInfoContent(frame);
    frame.setFocusForTest(panel);
    wxUpdateUIEvent update(wxID_COPY);
    update.SetEventObject(&frame);
    (void)frame.ProcessWindowEvent(update);
    state->copyEnabled = update.GetSetEnabled() && update.GetEnabled();
    state->consumed = panel != nullptr && sendMenu(frame, wxID_COPY);
    frame.setFocusForTest(nullptr);
}

void addInfoWindowCloseKeySteps(std::vector<SelfTestStep>& steps, SelfTest* test,
                                const std::shared_ptr<InfoKeysState>& state) {
    steps.push_back(infoWindowCloseKeyStep(test, state));
}

void addBackingScaleSteps(std::vector<SelfTestStep>& steps, SelfTest* test,
                          const std::shared_ptr<SmokeState>& state) {
    steps.push_back(backingScaleStep(test, state, false));
    steps.push_back(backingScaleStep(test, state, true));
}

void addApplicationMenuSteps(std::vector<SelfTestStep>& steps, SelfTest* test, bool opens_about) {
    steps.push_back(appMenuStep(test, opens_about));
}

void addSettingsCloseKeySteps(std::vector<SelfTestStep>& steps, SelfTest* test,
                              const std::shared_ptr<SettingsState>& state) {
    steps.push_back(settingsCloseKeyStep(test, state));
    steps.push_back(closeKeyOtherWindowStep(test, state));  // closes the stream (last)
}

void openFilesFromFinder(const wxArrayString& names) {
    wxGetApp().MacOpenFiles(names);
}

}  // namespace rawreel
