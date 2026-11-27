#pragma once
// The selftest's shared state and helpers that the per-backend step files use (selftest_msw.cpp,
// selftest_gtk.cpp, selftest_cocoa.mm). Private to rawreel-gui; include it only from selftest.cpp,
// those files and selftest_wx.h. GUI thread only.
// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "dvv/base/move_only_function.h"
#include "dvv/gpu/presenter.h"
#include "dvv/platform/pixel_size.h"
#include "gui/selftest.h"

namespace rawreel {

class MainFrame;
class SettingsWindow;
class Viewport;

struct SmokeState {
    std::uint64_t startFrames = 0;
    wxSize logBestSize;  // moved pane: the Log pane's BestSize before it docked on the right
    std::uint64_t startSwapchains = 0;
    std::uint64_t startShows = 0;  // Viewport::presenterShows() at the step start
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
    int aboutBitmaps = 0;                 // about: its wxStaticBitmaps (1: the logo, no stock icon)
    bool aboutLogoFound = false;          // about: a wxStaticBitmap named ABOUT_LOGO_NAME
    wxSize aboutLogoSize;                 // about: the logo bitmap's logical size
    wxSize aboutLogoWant;                 // about: FromDIP(ABOUT_LOGO_DIP x ABOUT_LOGO_DIP)
    wxSize aboutLogoPixels;               // about: the logo bitmap's size
    wxSize aboutBundlePixels;      // about: aboutLogo()'s bitmap for the logo's scale, its size
    wxSize aboutBundleSize;        // about: aboutLogo()'s default size
    int aboutCornerAlpha = -1;     // about: the shown bitmap's largest corner alpha (-1: none)
    int aboutEdgeAlpha = -1;       // about: its largest alpha at the 4 edge middles
    int aboutCentreAlpha = -1;     // about: the alpha at its centre
    std::uint64_t presented0 = 0;  // lavfi: frames presented at the step start
    std::string streamUri;         // Format... on lavfi: the stream's URI
    std::uint64_t dialogs0 = 0;    // Format... on lavfi: format dialogs shown
};

using SharedState = std::shared_ptr<SmokeState>;

struct InfoKeysState {
    int phase = 0;
    bool consumed = false;       // the Info window's char hook took the key
    bool consumedAgain = false;  // ... and the second key of a two-part step
    bool available = false;      // macOS ⌘W: Close stream enabled with the focus in the window
    bool availableAgain = false;
    bool hiddenAgain = false;  // macOS ⌘W: the second ⌘W (no stream) closed the window
    bool copyEnabled = false;  // macOS: Edit -> Copy enabled with the focus in the window
    bool onPage = false;       // the window has a current book page (the first Space's focus)
    // The second Space's focus: the real one where the OS gives the window the focus (focusReal),
    // else shownInfoFocus(); focusKeepsNoKey: it is in the content, at no native control.
    wxWindow* focusAgain = nullptr;
    bool focusReal = false;
    bool focusKeepsNoKey = false;
    std::uint32_t streamsBefore = 0;
};

struct SettingsState {
    bool hookRan = false;
    bool pagesCreated = false;
    bool shown = false;
    wxString configPathText;
    wxString folderText;
    wxString folderBefore;  // folder failure: the field before Choose...
    bool useDefaultEnabled = false;
    bool revealEnabled = false;
    std::filesystem::path inUse;    // logFolderInUse() before the step's action
    std::filesystem::path target;   // the folder the chooseFolder hook returns
    std::filesystem::path oldFile;  // currentLogFile() before the step's action
    std::optional<std::string> message;
    std::optional<std::filesystem::path> revealed;
    bool systemOn = false;              // system log: after "on"
    std::optional<bool> savedSystemOn;  // system log: the scratch file after "on"
    std::size_t streamsBefore = 0;      // close key
};

// A step of `name`; `start` may be empty.
SelfTestStep step(std::string name, dvv::MoveOnlyFunction<void()> start,
                  dvv::MoveOnlyFunction<bool(std::string&)> passed,
                  std::chrono::milliseconds deadline = std::chrono::seconds(5));
// The viewport's presenter statistics; nullopt without a presenter.
std::optional<dvv::gpu::PresenterStats> statsOf(const Viewport& viewport);
// The viewport's presenter, extent and counters against the step's start values.
std::string describe(const Viewport& viewport, const SmokeState& state);
// Records the step's start values (frames, swapchains, shows, size).
void markStart(const Viewport& viewport, SmokeState& state);
// Opens SMOKE_STREAM in slot 0 through the Session (no dialog).
void openSmokeStream(SelfTest* test);
// The rulers shown or hidden, unsaved; the other ruler settings (numbers, cursor colour) kept.
void showRulers(MainFrame& frame, bool show);
// The frame's wxEVT_MENU handler for the menu item `id`.
bool sendMenu(wxFrame& frame, int id);
// A char-hook key event, injected (injectInput) as a real key press reaches the frame's
// wxEVT_CHAR_HOOK handler first: whether a handler took it (one that called Skip() did not).
// `target` is the window the event starts at (the focused window), from where it propagates up to
// its top-level window. `ctrl`: ControlDown(), i.e. Cmd on macOS. `alt`: AltDown().
bool sendKey(wxWindow& target, int keycode, bool shift = false, bool ctrl = false,
             bool alt = false);
// sendKey() at `focus`, with the focus there; false without a window.
bool sendKeyWithFocus(MainFrame& frame, wxWindow* focus, int keycode, bool ctrl = false);
// A mouse event of `type` on the viewport at the client point `at` (window units), injected, with
// the button states given.
void sendViewportMouse(Viewport& viewport, wxEventType type, wxPoint at, bool left_down,
                       bool middle_down);
// The Info window, shown first: the window in it that wx gives the focus when the user activates
// it (focusLanding: the content panel, as the static text takes no focus from the keyboard);
// null without a window.
wxWindow* shownInfoFocus(MainFrame& frame);
// A char-hook key event at shownInfoFocus(), with the focus there: whether a handler took it.
bool sendInfoKey(MainFrame& frame, int keycode, bool ctrl = false);
// Runs `action` in the Settings window through the settings hook (state->hookRan).
void runSettings(SelfTest* test, const std::shared_ptr<SettingsState>& state,
                 dvv::MoveOnlyFunction<void(SettingsWindow&)> action);

}  // namespace rawreel
