#pragma once

// The --selftest runner of rawreel-gui (design M3 v2 §8.4; M1b §5.6): a 50 ms timer drives the
// steps of a scenario script, each with its own deadline, then the runner's close step. A missed
// deadline exits 6 naming the step; a presenter failure exits 4 (unless the script expects one).

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <wx/eventfilter.h>
#include <wx/timer.h>

#include "core/gui_arguments.h"
#include "core/player.h"
#include "core/session_types.h"
#include "dvv/base/move_only_function.h"

class wxDirDialog;
class wxFileDialog;
class wxGenericAboutDialog;  // NOLINT(readability-identifier-naming): wxWidgets' name
class wxTextEntryDialog;

namespace rawreel {

class App;
class FormatDialog;
class MainFrame;
class SettingsWindow;

// --selftest input isolation (an app-wide wx event filter, installed only while a SelfTest runs):
// real keyboard, mouse-button, wheel and menu events are dropped, counted and logged, so a key
// press or a click cannot change a step's result. The script's own key events go through
// injectInput().
class SelfTestInput final : public wxEventFilter {
public:
    int FilterEvent(wxEvent& event) override;
    [[nodiscard]] std::uint64_t ignored() const noexcept { return ignored_; }

private:
    std::uint64_t ignored_ = 0;
};
// The script's input: target.ProcessEvent(event), passed by SelfTestInput. GUI thread only.
bool injectInput(wxEvtHandler& target, wxEvent& event);

struct SelfTestStep {
    std::string name;
    dvv::MoveOnlyFunction<void()> start;  // once, when the step begins (may be empty)
    // Polled every tick; `seen` explains a miss.
    dvv::MoveOnlyFunction<bool(std::string& seen)> passed;
    std::chrono::milliseconds deadline{5000};
};
struct SelfTestScript {
    std::vector<SelfTestStep> steps;
    bool presenterFailureFails = true;  // lost: false
};

class SelfTest final : private wxTimer {
public:
    SelfTest(MainFrame& frame, App& app);
    SelfTest(const SelfTest&) = delete;
    SelfTest& operator=(const SelfTest&) = delete;
    SelfTest(SelfTest&&) = delete;
    SelfTest& operator=(SelfTest&&) = delete;
    // A frame closed before the script finished fails the selftest (code 6).
    ~SelfTest() override;

    // selfTestScript(scenario, *this); nullopt -> App::finish(6, "rawreel-gui --selftest: scenario
    // <name> is not available").
    void start(Scenario scenario);
    // Stops the timer; the object lives until ~MainFrame. Not wxTimer::Stop(): it also makes every
    // later tick a no-op.
    void stop() noexcept;  // NOLINT(bugprone-virtual-near-miss)
    [[nodiscard]] MainFrame& frame() const noexcept { return *frame_; }
    [[nodiscard]] App& app() const noexcept { return *app_; }
    // Real input events dropped so far (SelfTestInput).
    [[nodiscard]] std::uint64_t inputIgnored() const noexcept { return input_.ignored(); }

private:
    void Notify() override;  // one tick
    // Records the step's start time and runs its start; past the last step, the close step.
    void enter(std::size_t step);
    // Terminal: the runner's own close step (no member access after frame().Close()).
    void closeStep();
    // Terminal: stops the script and hands the result to App::finish().
    void fail(int code, const std::string& message);
    // The presenter's failure when presenter failures fail the script.
    [[nodiscard]] std::optional<std::string> presenterFailure() const;
    [[nodiscard]] std::string stepLabel() const;  // "<scenario> step <k> (<name>)"

    MainFrame* frame_;
    App* app_;
    Scenario scenario_ = Scenario::None;
    SelfTestScript script_;
    std::size_t step_ = 0;
    std::chrono::steady_clock::time_point step_start_;
    bool entered_ = false;    // step 1 was entered (on the first tick)
    bool busy_ = false;       // a step's start or passed is running (no re-entrant tick)
    bool finished_ = false;   // the outcome is decided
    bool stopped_ = false;    // stop() ran: no further tick does anything
    SelfTestInput input_;     // registered from start() until the destructor
    bool filtering_ = false;  // input_ is registered
};

struct ShownSet {
    std::int64_t index;
    std::uint32_t count;
    bool viewsMatch;  // every view's key().index == index
    std::uint64_t epoch;
    Clock::time_point at;
};

// Selftest only: records every FrameSet the Session shows (no leases kept) and forwards it.
class ShowHistory final : public IDisplaySink {
public:
    explicit ShowHistory(std::shared_ptr<IDisplaySink> inner) : inner_(std::move(inner)) {}
    void show(FrameSet frames) override {
        ShownSet record{.index = frames.index,
                        .count = frames.count,
                        .viewsMatch = true,
                        .epoch = frames.epoch,
                        .at = Clock::now()};
        for (std::uint32_t k = 0; k < frames.count; ++k) {
            const auto& view = frames.views[k];
            record.viewsMatch = record.viewsMatch && view && view->key().index == frames.index;
        }
        {
            const std::scoped_lock lock(mutex_);
            shown_.push_back(record);
        }
        inner_->show(std::move(frames));
    }
    void clear() override { inner_->clear(); }
    [[nodiscard]] std::size_t size() const {
        const std::scoped_lock lock(mutex_);
        return shown_.size();
    }
    [[nodiscard]] std::vector<ShownSet> since(std::size_t first) const {
        const std::scoped_lock lock(mutex_);
        return {shown_.begin() + static_cast<std::ptrdiff_t>(std::min(first, shown_.size())),
                shown_.end()};
    }

private:
    std::shared_ptr<IDisplaySink> inner_;
    mutable std::mutex mutex_;
    std::vector<ShownSet> shown_;
};

// Dialog hooks of the selftest (design v2 §8.4). GUI thread only; empty outside --selftest; a hook
// fires once.
struct DialogHooks {
    dvv::MoveOnlyFunction<void(FormatDialog&)> format;
    dvv::MoveOnlyFunction<void(wxTextEntryDialog&)> uri;       // Open URI... (D29)
    dvv::MoveOnlyFunction<void(wxGenericAboutDialog&)> about;  // Help -> About (D29)
    // A message: App::saveSettings' error, a refused or failed log save.
    dvv::MoveOnlyFunction<void(wxMessageDialog&)> message;
    // File -> Save Log... / Save Selected Lines...: sets the chosen file (SetPath).
    dvv::MoveOnlyFunction<void(wxFileDialog&)> saveLog;
    // The text a copy puts on the clipboard (the developer's clipboard stays untouched).
    dvv::MoveOnlyFunction<void(std::string)> clipboard;
    // Settings -> Log -> Choose...: sets the chosen folder (wxDirDialogBase::SetPath).
    dvv::MoveOnlyFunction<void(wxDirDialog&)> chooseFolder;
    // Show in Finder, Show in Explorer, Open Folder: the file or folder (nothing is launched).
    dvv::MoveOnlyFunction<void(std::filesystem::path)> reveal;
    // The Settings window (Log pane and Settings design §7.1), through showSettingsWindow: on macOS
    // and GTK it runs once the window is shown; on Windows it is queued (CallAfter) before the
    // modal Show() and runs inside its loop, so it must end the dialog (SettingsWindow::pressOk()).
    dvv::MoveOnlyFunction<void(SettingsWindow&)> settings;
    // The Log list's context menu (log level design §2.2), built but never shown: its items and
    // which Level item is checked.
    dvv::MoveOnlyFunction<void(const wxMenu&)> logMenu;
};
[[nodiscard]] DialogHooks& dialogHooks();
// Every ShowModal of these dialogs goes here. With a hook set, a CallAfter queued before ShowModal
// runs inside the modal loop, calls the hook (e.g. choice()/setChoice(), SetValue()) and ends the
// dialog with wxID_OK. A --selftest dialog without a hook is cancelled the same way (logged), so a
// script never hangs on a dialog.
[[nodiscard]] int showModal(FormatDialog& dialog);
[[nodiscard]] int showModal(wxTextEntryDialog& dialog);
[[nodiscard]] int showModal(wxGenericAboutDialog& dialog);
// A native message dialog is never shown in a --selftest run: with a hook it calls the hook and
// returns wxID_OK; without one it logs one warning and returns wxID_CANCEL. Outside a selftest it
// is ShowModal().
[[nodiscard]] int showModal(wxMessageDialog& dialog);
// The same for the native file dialog (the saveLog hook).
[[nodiscard]] int showModal(wxFileDialog& dialog);
// The same for the native folder dialog (the chooseFolder hook).
[[nodiscard]] int showModal(wxDirDialog& dialog);
// A popup menu (the Log list's context menu) is never shown in a --selftest run: the menu goes to
// the logMenu hook (without one: one warning) and wxID_NONE is returned. Otherwise
// window.GetPopupMenuSelectionFromUser(menu).
[[nodiscard]] int showLogMenu(wxWindow& window, wxMenu& menu);
// The file a save dialog returned: in a --selftest run the path its hook set (wxGTK's GetPath()
// reads its file chooser, which a dialog that was never shown may not have updated), else
// GetPath(). UTF-8.
[[nodiscard]] std::string chosenPath(const wxFileDialog& dialog);
// Copied text (UTF-8): to the clipboard hook in a --selftest run (without one: one warning), else
// to wxTheClipboard as plain text.
void setClipboardText(std::string text);
// The folder a folder dialog returned (UTF-8): in a --selftest run the path its hook set (wxGTK's
// SetPath() and GetPath() go to its file chooser), else GetPath().
[[nodiscard]] std::string chosenFolder(const wxDirDialog& dialog);
// Show in Finder/Explorer, Open Folder: to the reveal hook in a --selftest run (without one: one
// warning), else revealInFileManager().
void revealFile(const std::filesystem::path& file_or_folder);
// Settings... (Log pane and Settings design §5.1): settings.show(&parent), with the settings hook
// in a --selftest run. Without a hook a selftest on Windows ends the modal dialog with Cancel
// (logged), so a script never waits for a user.
void showSettingsWindow(SettingsWindow& settings, wxWindow& parent);

// The dispatcher: Smoke -> {smokeScenario(test)}, Lost -> {lostScenario(test), false}, Player ->
// {playerScenario(test)}, Perf -> {perfScenario(test)}, About -> {aboutScenario(test)}; every other
// scenario -> nullopt.
[[nodiscard]] std::optional<SelfTestScript> selfTestScript(Scenario scenario, SelfTest& test);
[[nodiscard]] std::vector<SelfTestStep> smokeScenario(SelfTest& test);
// Device loss (design v2 §8.4 lost, E3): an injected loss offers Reopen within 5 s without
// blocking the GUI thread, and Reopen presents the kept frame again on the new stack.
[[nodiscard]] std::vector<SelfTestStep> lostScenario(SelfTest& test);
// Transport, keys, slider, loop, the resize fit check, View -> Format..., drops and the format
// dialog (design v2 §8.4 player); one stream from the command line.
[[nodiscard]] std::vector<SelfTestStep> playerScenario(SelfTest& test);
// The performance run (design v2 §8.4 perf, E7; manual, not a CTest entry): maximise, wait for a
// full prefetch window, play 600 frames with Loop on, do 20 random seeks and print one JSON line.
[[nodiscard]] std::vector<SelfTestStep> perfScenario(SelfTest& test);
// Help -> About (opt-in, not in CTest): a shown frame, then the About step.
[[nodiscard]] std::vector<SelfTestStep> aboutScenario(SelfTest& test);

}  // namespace rawreel
