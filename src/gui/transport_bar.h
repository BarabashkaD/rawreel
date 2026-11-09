#pragma once

// The transport bar under the viewport (design M3 v2 §4.3, D17; video control panel spec §5, §7):
// the timeline strip with the frame numbers, the seek slider, then a row of Lucide icon buttons:
// Open on the left and First, StepBack, Play/Pause, Stop, StepForward, Last and the Loop toggle
// centred on the bar. Buttons never take the focus, so Space and the arrows always reach the
// frame's char hook.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <wx/bmpbndl.h>
#include <wx/bmpbuttn.h>
#include <wx/settings.h>
#include <wx/slider.h>
#include <wx/tglbtn.h>

#include "core/commands.h"
#include "core/player.h"
#include "dvv/base/move_only_function.h"
#include "gui/icons.h"
#include "gui/timeline_strip.h"

namespace rawreel {

// The slider's interaction (video control panel spec §6): Scrubbing while the user drags the
// thumb with the left button down; Skimming while, paused, the pointer moves over the slider with
// the skim key held (U-CP7, §6.4).
enum class ScrubState : std::uint8_t { Idle, Scrubbing, Skimming };

// What the bar shows. Without a stream: a default BarStatus (a default PlayerStatus, generation
// 0). `streamGeneration` is SessionStatus::streamGeneration (U-CP5).
struct BarStatus {
    PlayerStatus player;
    bool hasStream = false;
    std::uint64_t streamGeneration = 0;
};

class TransportBar final : public wxPanel {
public:
    // `read_status` reads the session's current status as `update()` would get it (spec §3); it
    // is called only when the bar leaves Scrubbing or Skimming, never on a timer.
    TransportBar(wxWindow* parent, dvv::MoveOnlyFunction<void(Command)> execute,
                 dvv::MoveOnlyFunction<void(std::int64_t)> seek,
                 dvv::MoveOnlyFunction<BarStatus()> read_status);
    TransportBar(const TransportBar&) = delete;
    TransportBar& operator=(const TransportBar&) = delete;
    TransportBar(TransportBar&&) = delete;
    TransportBar& operator=(TransportBar&&) = delete;
    ~TransportBar() override;

    // Called on every status notification (spec §6.2). A new stream generation, a range change or
    // no stream resets the bar to Idle without a seek back. While Scrubbing the slider is left
    // alone (and the bar returns to Idle once the left mouse button is up); while Skimming too
    // (playback started keeps the skimmed frame); while Idle it shows `target`. The slider is
    // disabled without a stream or with an unbounded length; the buttons follow
    // MainFrame::isAvailable (CP-D4).
    void update(const BarStatus& bar_status);
    // The frame's wxEVT_ACTIVATE: a skim starts only while active (initially true); deactivation
    // returns from a skim (spec §6.2).
    void frameActivated(bool active);
    // The skim key's wxEVT_KEY_UP anywhere (SkimKeyRelease): returns from a skim.
    void skimKeyReleased();
    [[nodiscard]] wxSlider& slider() noexcept { return *slider_; }
    [[nodiscard]] ScrubState scrubState() const noexcept { return scrub_; }
    // Logs each state change at Info ("transport: scrub Idle -> Scrubbing"), every left-down on the
    // slider and every skim step (spec §9a).
    void setTrace(bool on) noexcept { trace_ = on; }

    // Test seams. An empty probe reads wxGetMouseState().LeftIsDown(); the observer is called at
    // the end of every update() with the slider's value (empty: none).
    void setLeftButtonProbeForTest(dvv::MoveOnlyFunction<bool()> probe);
    void setUpdateObserverForTest(dvv::MoveOnlyFunction<void(int value)> observer);
    // Called with every value the bar passes to `seek`, after it does (empty: none).
    void setSeekObserverForTest(dvv::MoveOnlyFunction<void(std::int64_t)> observer);
    // Whether a press on the channel jumps (spec §6.5); empty: SLIDER_PAGES_ON_TRACK_CLICK.
    void setTrackClickPagesForTest(std::optional<bool> pages) noexcept {
        track_click_pages_ = pages;
    }
    // The stream generation the bar's view belongs to.
    [[nodiscard]] std::uint64_t streamGenerationForTest() const noexcept {
        return last_generation_;
    }
    // How often forgetRelease ran (every run of its body), for any bar.
    [[nodiscard]] static int forgetReleaseRunsForTest() noexcept;
    // The system colours the bar draws with (empty: wxSystemSettings::GetColour); the icons read
    // wxSYS_COLOUR_BTNTEXT from it on every wxEVT_SYS_COLOUR_CHANGED.
    void setSystemColourForTest(dvv::MoveOnlyFunction<wxColour(wxSystemColour)> source);
    [[nodiscard]] Icon playIconForTest() const noexcept { return play_icon_; }
    [[nodiscard]] wxColour iconTintForTest() const { return tint_; }
    [[nodiscard]] int iconRebuildsForTest() const noexcept { return icon_rebuilds_; }
    // The union of the centred group's buttons (First ... Loop), in the bar's client coordinates.
    [[nodiscard]] wxRect groupRectForTest() const;
    [[nodiscard]] wxWindow& openButtonForTest() noexcept { return *open_; }
    [[nodiscard]] TimelineStrip& timelineStripForTest() noexcept { return *strip_; }
    // The ghost thumb's wx client x while Skimming (spec §6.4); nullopt: none shown.
    [[nodiscard]] std::optional<int> ghostForTest() const noexcept { return ghost_x_; }

private:
    struct IconButton {
        wxAnyButton* button;
        Command command;
        Icon icon;
    };

    wxBitmapButton* addButton(wxSizer& sizer, Command command, Icon icon);
    void setUp(wxAnyButton& button, Command command, Icon icon);
    void applyMinSize(wxAnyButton& button);
    void syncSpacer();
    void rebuildIcons();
    [[nodiscard]] wxColour systemColour(wxSystemColour index);
    void onDpiChanged(wxDPIChangedEvent& event);
    void onSysColourChanged(wxSysColourChangedEvent& event);
    void onScroll(wxScrollEvent& event);
    void seekTo(int value);
    void setScrub(ScrubState state);
    [[nodiscard]] bool leftButtonDown();
    // Spec §6.1: Idle; the status (`fresh`: read_status(), else last_status_) sets the range and
    // the slider to its clamped `target`; forgets last_sent_; repaints the strip and the buttons.
    void applyIdleView(bool fresh);
    void showStatus();  // the buttons, the play icon and Loop from last_status_
    void showStrip();   // the strip's view for the state (spec §6.3)
    void forgetRelease();
    void onSliderLeftDown(wxMouseEvent& event);
    void onSliderMotion(wxMouseEvent& event);
    void onSliderLeave(wxMouseEvent& event);
    // Skimming: the slider and the seek follow the pointer's x (a mouse event's GetX()).
    void skimTo(int event_x);
    // Spec §6.2: leaving a skim. Keep: the skimmed frame stays (a fresh Idle view). Return: one
    // seek back to original_ unless a fresh read already targets it, then the Idle view.
    void keepSkim();
    bool tracksPage() const noexcept {
        return track_click_pages_.value_or(SLIDER_PAGES_ON_TRACK_CLICK);
    }
    bool jumpToClick(const wxMouseEvent& event);
    void returnFromSkim();

    dvv::MoveOnlyFunction<void(Command)> execute_;
    dvv::MoveOnlyFunction<void(std::int64_t)> seek_;
    dvv::MoveOnlyFunction<BarStatus()> read_status_;
    dvv::MoveOnlyFunction<bool()> left_button_probe_;
    dvv::MoveOnlyFunction<void(int)> update_observer_;
    dvv::MoveOnlyFunction<void(std::int64_t)> seek_observer_;
    dvv::MoveOnlyFunction<wxColour(wxSystemColour)> system_colour_;
    // Child windows: wx owns them. `buttons_` in row order, Open first; the rest form the group.
    std::vector<IconButton> buttons_;
    wxBitmapButton* open_ = nullptr;
    wxBitmapButton* play_ = nullptr;
    wxBitmapToggleButton* loop_ = nullptr;
    wxSizerItem* open_item_ = nullptr;                // Open in the transport row (its border)
    wxSizerItem* spacer_ = nullptr;                   // Open's width at the row's right end
    std::array<wxBitmapBundle, ICON_COUNT> bundles_;  // in Icon order, tinted with `tint_`
    wxColour tint_;
    int icon_rebuilds_ = 0;
    Icon play_icon_ = Icon::Play;
    wxSlider* slider_ = nullptr;
    TimelineStrip* strip_ = nullptr;
    int max_ = -1;  // the slider's maximum; -1: disabled (no stream or unbounded)
    ScrubState scrub_ = ScrubState::Idle;
    BarStatus last_status_;              // the last one given to update() or read on leaving a drag
    std::uint64_t last_generation_ = 0;  // its streamGeneration: a new one resets the bar
    int idle_value_ = -1;  // the read-your-writes target: the last Idle view or Idle seek; -1: none
    int original_ = -1;    // the original frame of the current drag or skim
    int last_sent_ = -1;   // the last value this bar seeked to in the current interaction; -1: none
    // The value of the release that ended the last drag, until a posted forgetRelease runs.
    std::optional<int> release_value_;
    bool active_ = true;          // the last wxEVT_ACTIVATE of the frame said so
    std::optional<int> ghost_x_;  // the ghost thumb's wx client x while Skimming
    bool trace_ = false;
    std::optional<bool> track_click_pages_;  // test seam
};

}  // namespace rawreel
