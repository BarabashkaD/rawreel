// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/transport_bar.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/timeline.h"
#include "dvv/base/log.h"
#include "gui/native_controls.h"
#include "gui/skim_key.h"
#include "gui/timeline_strip.h"

namespace rawreel {

namespace {

// The slider's maximum for `status`; -1: no range (no stream or an unbounded length). The slider's
// range is an int; longer streams are clamped (the slider then ends early).
int sliderMaxFor(const PlayerStatus& status) {
    constexpr std::int64_t INT_LIMIT = std::numeric_limits<int>::max();
    return status.length && *status.length > 0
               ? static_cast<int>(std::min(*status.length - 1, INT_LIMIT))
               : -1;
}

const char* scrubName(ScrubState state) {
    switch (state) {
        case ScrubState::Scrubbing: return "Scrubbing";
        case ScrubState::Skimming: return "Skimming";
        case ScrubState::Idle: break;
    }
    return "Idle";
}

// Every run of TransportBar::forgetRelease, by any bar (GUI thread only).
int& forgetReleaseRuns() noexcept {
    static int runs = 0;
    return runs;
}

// The transport row's buttons (spec §5.1): at least 29 x 28 DIP, 2 DIP apart from everything.
constexpr int BUTTON_WIDTH_DIP = 29;
constexpr int BUTTON_HEIGHT_DIP = 28;
constexpr int BUTTON_BORDER_DIP = 2;

#if defined(__WXOSX__)
constexpr bool CMD_FOR_CTRL = true;  // the menus show an Accelerator's Ctrl as Cmd there
#else
constexpr bool CMD_FOR_CTRL = false;
#endif

const CommandInfo& commandInfo(Command command) {
    return commandTable()[static_cast<std::size_t>(command)];
}

wxString fromUtf8(std::string_view text) {
    return wxString::FromUTF8(text.data(), text.size());
}

}  // namespace

// Spec §5.1, window title and Info design §12: the slider row, then [Open][Format] stretch [group]
// stretch [spacer]["i"]. The spacer makes the right side as wide as the left (syncSpacer), so the
// group is centred on the whole bar.
TransportBar::TransportBar(wxWindow* parent, dvv::MoveOnlyFunction<void(Command)> execute,
                           dvv::MoveOnlyFunction<void(std::int64_t)> seek,
                           dvv::MoveOnlyFunction<BarStatus()> read_status)
    : wxPanel(parent),
      execute_(std::move(execute)),
      seek_(std::move(seek)),
      read_status_(std::move(read_status)) {
    tint_ = systemColour(wxSYS_COLOUR_BTNTEXT);
    for (std::size_t i = 0; i < ICON_COUNT; ++i) {
        bundles_[i] = iconBundle(static_cast<Icon>(i), tint_);
    }
    const int border = FromDIP(BUTTON_BORDER_DIP);
    slider_ = new wxSlider(this, wxID_ANY, 0, 0, 1);
    slider_->Disable();
    slider_->Bind(wxEVT_SCROLL_THUMBTRACK, &TransportBar::onScroll, this);
    slider_->Bind(wxEVT_SCROLL_CHANGED, &TransportBar::onScroll, this);
    slider_->Bind(wxEVT_SCROLL_THUMBRELEASE, &TransportBar::onScroll, this);
    slider_->Bind(wxEVT_LEFT_DOWN, &TransportBar::onSliderLeftDown, this);
    slider_->Bind(wxEVT_MOTION, &TransportBar::onSliderMotion, this);
    slider_->Bind(wxEVT_LEAVE_WINDOW, &TransportBar::onSliderLeave, this);
    // The strip reads the bar's colours: a MoveOnlyFunction cannot be shared.
    strip_ = new TimelineStrip(this, *slider_,
                               [this](wxSystemColour index) { return systemColour(index); });

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    open_ = addButton(*row, Command::Open, Icon::FolderOpen);
    open_item_ = row->GetItem(open_);
    format_ = addButton(*row, Command::Format, Icon::SlidersHorizontal);
    format_item_ = row->GetItem(format_);
    row->AddStretchSpacer();
    auto* group = new wxBoxSizer(wxHORIZONTAL);
    addButton(*group, Command::First, Icon::SkipBack);
    addButton(*group, Command::StepBack, Icon::StepBack);
    play_ = addButton(*group, Command::PlayPause, Icon::Play);
    addButton(*group, Command::Stop, Icon::Square);
    addButton(*group, Command::StepForward, Icon::StepForward);
    addButton(*group, Command::Last, Icon::SkipForward);
    loop_ =
        new wxBitmapToggleButton(this, wxID_ANY, bundles_[static_cast<std::size_t>(Icon::Repeat)]);
    loop_->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& /*event*/) { execute_(Command::Loop); });
    setUp(*loop_, Command::Loop, Icon::Repeat);
    group->Add(loop_, wxSizerFlags().Border(wxALL, border));
    row->Add(group);
    row->AddStretchSpacer();
    spacer_ = row->Add(0, 0);
    info_ =
        new wxBitmapToggleButton(this, wxID_ANY, bundles_[static_cast<std::size_t>(Icon::Info)]);
    info_->Bind(wxEVT_TOGGLEBUTTON,
                [this](wxCommandEvent& /*event*/) { execute_(Command::InfoWindow); });
    setUp(*info_, Command::InfoWindow, Icon::Info);
    info_item_ = row->Add(info_, wxSizerFlags().Border(wxALL, border));
    syncSpacer();

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(strip_, wxSizerFlags().Expand());
    sizer->Add(slider_, wxSizerFlags().Expand().Border(wxALL, border));
    sizer->Add(row, wxSizerFlags().Expand());
    SetSizer(sizer);
    Bind(wxEVT_DPI_CHANGED, &TransportBar::onDpiChanged, this);
    Bind(wxEVT_SYS_COLOUR_CHANGED, &TransportBar::onSysColourChanged, this);
}

// The slider outlives this body (wx destroys the children later): its pointer events must not
// reach a bar that is half gone.
TransportBar::~TransportBar() {
    slider_->Unbind(wxEVT_LEFT_DOWN, &TransportBar::onSliderLeftDown, this);
    slider_->Unbind(wxEVT_MOTION, &TransportBar::onSliderMotion, this);
    slider_->Unbind(wxEVT_LEAVE_WINDOW, &TransportBar::onSliderLeave, this);
}

wxBitmapButton* TransportBar::addButton(wxSizer& sizer, Command command, Icon icon) {
    auto* button = new wxBitmapButton(this, wxID_ANY, bundles_[static_cast<std::size_t>(icon)]);
    button->Bind(wxEVT_BUTTON, [this, command](wxCommandEvent& /*event*/) { execute_(command); });
    setUp(*button, command, icon);
    sizer.Add(button, wxSizerFlags().Border(wxALL, FromDIP(BUTTON_BORDER_DIP)));
    return button;
}

// No caption and no focus; the tooltip and the names come from the command table (spec §7). Every
// button but Open and "i" waits for a stream (CP-D4, window title and Info design §12).
void TransportBar::setUp(wxAnyButton& button, Command command, Icon icon) {
    const CommandInfo& info = commandInfo(command);
    button.SetCanFocus(false);
    button.SetToolTip(wxString::FromUTF8(tooltipText(info, CMD_FOR_CTRL)));
    std::string_view label = info.label;
    button.SetName(fromUtf8(label));
    if (label.ends_with("...")) {
        label.remove_suffix(std::string_view("...").size());
    }
    setAccessibleName(button, fromUtf8(label));
    if (command != Command::Open && command != Command::InfoWindow) {
        button.Disable();
    }
    applyMinSize(button);
    buttons_.push_back({&button, command, icon});
}

// The larger of the native minimum and BUTTON_*_DIP.
void TransportBar::applyMinSize(wxAnyButton& button) {
    button.SetMinSize(wxDefaultSize);
    button.InvalidateBestSize();
    const wxSize native = button.GetBestSize();
    const wxSize least = FromDIP(wxSize(BUTTON_WIDTH_DIP, BUTTON_HEIGHT_DIP));
    button.SetMinSize(wxSize(std::max(native.x, least.x), std::max(native.y, least.y)));
}

// The right side (the spacer and "i") as wide as the left (Open and Format), with their borders.
void TransportBar::syncSpacer() {
    const auto side = [](const wxWindow& button, const wxSizerItem& item) {
        return button.GetEffectiveMinSize().x + (2 * item.GetBorder());
    };
    const int width =
        side(*open_, *open_item_) + side(*format_, *format_item_) - side(*info_, *info_item_);
    spacer_->AssignSpacer(std::max(width, 0), 0);
}

void TransportBar::setInfoShown(bool shown) {
    if (info_->GetValue() != shown) {
        info_->SetValue(shown);
    }
}

void TransportBar::setInfoHint(const std::string& hint) {
    std::string tip = tooltipText(commandInfo(Command::InfoWindow), CMD_FOR_CTRL);
    if (!hint.empty()) {
        tip += "\n" + hint;
    }
    info_->SetToolTip(fromUtf8(tip));
}

wxColour TransportBar::systemColour(wxSystemColour index) {
    return system_colour_ ? system_colour_(index) : wxSystemSettings::GetColour(index);
}

void TransportBar::rebuildIcons() {
    tint_ = systemColour(wxSYS_COLOUR_BTNTEXT);
    for (std::size_t i = 0; i < ICON_COUNT; ++i) {
        bundles_[i] = iconBundle(static_cast<Icon>(i), tint_);
    }
    for (const IconButton& entry : buttons_) {
        const Icon icon = entry.button == play_ ? play_icon_ : entry.icon;
        entry.button->SetBitmapLabel(bundles_[static_cast<std::size_t>(icon)]);
        applyMinSize(*entry.button);
    }
    ++icon_rebuilds_;
    syncSpacer();
    Layout();
}

void TransportBar::onDpiChanged(wxDPIChangedEvent& event) {
    for (const IconButton& entry : buttons_) {
        applyMinSize(*entry.button);
    }
    syncSpacer();
    Layout();
    event.Skip();
}

void TransportBar::onSysColourChanged(wxSysColourChangedEvent& event) {
    rebuildIcons();
    event.Skip();
}

void TransportBar::setSystemColourForTest(dvv::MoveOnlyFunction<wxColour(wxSystemColour)> source) {
    system_colour_ = std::move(source);
}

wxRect TransportBar::groupRectForTest() const {
    static constexpr std::array<Command, 7> GROUP{
        Command::First,       Command::StepBack, Command::PlayPause, Command::Stop,
        Command::StepForward, Command::Last,     Command::Loop};
    std::optional<wxRect> rect;
    for (const IconButton& entry : buttons_) {
        if (std::ranges::find(GROUP, entry.command) != GROUP.end()) {
            const wxRect button = entry.button->GetRect();
            rect = rect ? rect->Union(button) : button;
        }
    }
    return rect.value_or(wxRect());
}

wxAnyButton& TransportBar::buttonForTest(Command command) {
    const auto found = std::ranges::find(buttons_, command, &IconButton::command);
    if (found == buttons_.end()) {
        DVV_LOG_ERROR("transport: no button for command {}", commandInfo(command).id);
        return *open_;
    }
    return *found->button;
}

void TransportBar::setLeftButtonProbeForTest(dvv::MoveOnlyFunction<bool()> probe) {
    left_button_probe_ = std::move(probe);
}

void TransportBar::setUpdateObserverForTest(dvv::MoveOnlyFunction<void(int value)> observer) {
    update_observer_ = std::move(observer);
}

bool TransportBar::leftButtonDown() {
    return left_button_probe_ ? left_button_probe_() : wxGetMouseState().LeftIsDown();
}

void TransportBar::setScrub(ScrubState state) {
    if (state == scrub_) {
        return;
    }
    if (trace_) {
        DVV_LOG_INFO("transport: scrub {} -> {}", scrubName(scrub_), scrubName(state));
    }
    if (scrub_ == ScrubState::Skimming) {  // every exit from a skim removes its ghost thumb
        ghost_x_.reset();
        showSliderGhost(*slider_, std::nullopt);
    }
    scrub_ = state;
}

void TransportBar::setSeekObserverForTest(dvv::MoveOnlyFunction<void(std::int64_t)> observer) {
    seek_observer_ = std::move(observer);
}

void TransportBar::seekTo(int value) {
    seek_(value);
    if (seek_observer_) {
        seek_observer_(value);
    }
}

void TransportBar::forgetRelease() {
    ++forgetReleaseRuns();
    release_value_.reset();
}

int TransportBar::forgetReleaseRunsForTest() noexcept {
    return forgetReleaseRuns();
}

void TransportBar::frameActivated(bool active) {
    active_ = active;
    if (!active && scrub_ == ScrubState::Skimming) {
        returnFromSkim();
    }
}

void TransportBar::skimKeyReleased() {
    if (scrub_ == ScrubState::Skimming) {
        returnFromSkim();
    }
}

// Spec §6.2: a click keeps the skimmed frame, then the Idle rule for a press.
void TransportBar::onSliderLeftDown(wxMouseEvent& event) {
    if (trace_) {
        DVV_LOG_INFO("transport: slider left-down at x {} in {}",
                     sliderEventX(*slider_, event.GetX()), scrubName(scrub_));
    }
    if (scrub_ == ScrubState::Skimming) {
        keepSkim();
    }
    if (scrub_ == ScrubState::Idle && tracksPage() && jumpToClick(event)) {
        return;
    }
    event.Skip();  // the native slider handles the click as usual
}

// Spec §6.2, §6.4: a skim starts on a motion with the skim key and the left button up, while the
// frame is active, paused, with a stream and an enabled slider; in a skim a motion with the key
// skims and one without it returns. A drag's motions change nothing.
void TransportBar::onSliderMotion(wxMouseEvent& event) {
    event.Skip();
    if (scrub_ == ScrubState::Idle) {
        if (skimKeyDown(event) && !event.LeftIsDown() && active_ && !last_status_.player.playing &&
            last_status_.hasStream && max_ >= 0 && slider_->IsEnabled()) {
            original_ = idle_value_;
            last_sent_ = idle_value_;  // the session already targets it
            setScrub(ScrubState::Skimming);
            skimTo(event.GetX());
        }
        return;
    }
    if (scrub_ == ScrubState::Skimming) {
        if (skimKeyDown(event)) {
            skimTo(event.GetX());
        } else {
            returnFromSkim();
        }
    }
}

void TransportBar::onSliderLeave(wxMouseEvent& event) {
    event.Skip();
    if (scrub_ == ScrubState::Skimming) {
        returnFromSkim();
    }
}

// Spec §6.2, §6.3: the real thumb stays at original_; the ghost thumb shows the skimmed frame and
// the picture previews it.
void TransportBar::skimTo(int event_x) {
    const int x = sliderEventX(*slider_, event_x);
    const SliderTrack travel = sliderTravel(*slider_);
    const int frame = static_cast<int>(std::min<std::int64_t>(
        frameAt(x, slider_->GetMin(), slider_->GetMax(), travel.travelStart, travel.travelEnd),
        max_));
    if (frame != last_sent_) {
        last_sent_ = frame;
        seekTo(frame);
    }
    ghost_x_ = static_cast<int>(std::lround(
        thumbX(frame, slider_->GetMin(), slider_->GetMax(), travel.travelStart, travel.travelEnd)));
    showSliderGhost(*slider_, ghost_x_);
    if (trace_) {
        const std::optional<int> centre = nativeThumbCentre(*slider_);
        DVV_LOG_INFO("transport: skim x {} -> frame {}, native thumb centre {}", x, frame,
                     centre ? std::to_string(*centre) : std::string("none"));
    }
    showStrip();
}

// Spec §6.5: where the native slider pages on a press in its channel, jump to the clicked frame.
// False (the press is on the thumb, or no stream): the native handling goes on.
bool TransportBar::jumpToClick(const wxMouseEvent& event) {
    if (max_ < 0 || !slider_->IsEnabled()) {
        return false;
    }
    const int x = sliderEventX(*slider_, event.GetX());
    const SliderTrack travel = sliderTravel(*slider_);
    const int min = slider_->GetMin();
    const int max = slider_->GetMax();
    const std::optional<wxRect> native = sliderThumbRect(*slider_);
    const wxRect thumb =
        native ? *native
               : wxRect(static_cast<int>(std::lround(thumbX(
                            slider_->GetValue(), min, max, travel.travelStart, travel.travelEnd))) -
                            travel.thumbWidth / 2,
                        0, travel.thumbWidth, slider_->GetClientSize().y);
    if (x >= thumb.GetLeft() && x <= thumb.GetRight()) {
        return false;
    }
    const int frame = static_cast<int>(
        std::min<std::int64_t>(frameAt(x, min, max, travel.travelStart, travel.travelEnd), max_));
    slider_->SetValue(frame);
    if (frame != idle_value_) {
        seekTo(frame);
    }
    idle_value_ = frame;
    showStrip();
    return true;
}

void TransportBar::keepSkim() {
    applyIdleView(true);
}

void TransportBar::returnFromSkim() {
    const BarStatus now = read_status_ ? read_status_() : last_status_;
    // A read that names another stream (its notification still queued) gets no seek back.
    if (now.hasStream && now.streamGeneration == last_generation_ &&
        now.player.target != original_) {
        seekTo(original_);
    }
    if (slider_->GetValue() != original_) {
        slider_->SetValue(original_);
    }
    applyIdleView(true);
}

// Spec §6.2. Idle seeks compare with idle_value_, a drag's seeks with last_sent_: never the same
// value twice in one interaction. A disabled slider ignores all. Any scroll event while Skimming
// keeps the skimmed frame first, then takes the Idle row.
void TransportBar::onScroll(wxScrollEvent& event) {
    if (max_ < 0) {
        return;
    }
    if (scrub_ == ScrubState::Skimming) {
        keepSkim();
    }
    if (max_ < 0) {
        return;  // keepSkim() read a status without a stream
    }
    const wxEventType type = event.GetEventType();
    const int value = event.GetPosition();
    if (scrub_ == ScrubState::Idle) {
        if (type == wxEVT_SCROLL_THUMBTRACK && leftButtonDown()) {
            original_ = idle_value_;
            last_sent_ = idle_value_;  // the session already targets it
            setScrub(ScrubState::Scrubbing);
            if (value != last_sent_) {
                last_sent_ = value;
                seekTo(value);
            }
            showStrip();
            return;
        }
        // A buttonless THUMBTRACK (a key or the wheel on macOS) or a CHANGED. The tail of a GTK
        // release pair carries the release's value: it must not seek (it would pause playback
        // started during the drag).
        const bool release_tail = type == wxEVT_SCROLL_CHANGED && release_value_ == value;
        if ((type == wxEVT_SCROLL_THUMBTRACK || type == wxEVT_SCROLL_CHANGED) && !release_tail &&
            value != idle_value_) {
            idle_value_ = value;
            seekTo(value);
            showStrip();
        }
        return;  // THUMBRELEASE in Idle: nothing
    }
    if (type == wxEVT_SCROLL_THUMBTRACK) {
        if (value != last_sent_) {
            last_sent_ = value;
            seekTo(value);
        }
        showStrip();
        return;
    }
    // THUMBRELEASE or CHANGED ends the drag.
    if (type == wxEVT_SCROLL_CHANGED && value != last_sent_) {
        last_sent_ = value;
        seekTo(value);
    }
    release_value_ = value;
    CallAfter(&TransportBar::forgetRelease);
    applyIdleView(true);
}

void TransportBar::update(const BarStatus& bar_status) {
    // Another stream (or none): whatever the user was dragging belongs to the old one.
    const bool reset = bar_status.streamGeneration != last_generation_ || !bar_status.hasStream ||
                       sliderMaxFor(bar_status.player) != max_;
    last_status_ = bar_status;
    last_generation_ = bar_status.streamGeneration;
    if (reset) {
        release_value_.reset();
    }
    // Backstop for a drag that ends without a release (the release lost): it runs on a
    // notification, never on a timer. A skim has no button to wait for.
    const bool release_lost = scrub_ == ScrubState::Scrubbing && !leftButtonDown();
    if (reset || scrub_ == ScrubState::Idle || release_lost) {
        applyIdleView(false);
    } else if (scrub_ == ScrubState::Skimming && bar_status.player.playing) {
        keepSkim();  // playback started: it plays on from the skimmed frame
    } else {
        // Scrubbing or Skimming: the slider stays where the user put it; the strip and the buttons
        // follow.
        showStatus();
        showStrip();
    }
    if (update_observer_) {
        update_observer_(slider_->GetValue());
    }
}

void TransportBar::applyIdleView(bool fresh) {
    if (fresh && read_status_) {
        last_status_ = read_status_();
        last_generation_ = last_status_.streamGeneration;
    }
    const PlayerStatus& status = last_status_.player;
    if (const int max = sliderMaxFor(status); max != max_) {
        max_ = max;
        slider_->SetRange(0, std::max(max, 1));
        slider_->Enable(max >= 0);
    }
    setScrub(ScrubState::Idle);
    last_sent_ = -1;
    idle_value_ = -1;
    if (max_ >= 0) {
        // A queued seek may briefly name a frame beyond a new stream's end: clamp.
        idle_value_ = static_cast<int>(std::clamp<std::int64_t>(status.target, 0, max_));
        if (slider_->GetValue() != idle_value_) {
            slider_->SetValue(idle_value_);
        }
    }
    showStatus();
    showStrip();
}

// CP-D4, as MainFrame::isAvailable: every button but Open and "i" waits for a stream, Last also for
// a known length. Enable states, bitmaps and values change only when they differ.
void TransportBar::showStatus() {
    const PlayerStatus& status = last_status_.player;
    for (const IconButton& entry : buttons_) {
        bool enabled = true;
        if (entry.command == Command::Last) {
            enabled = last_status_.hasStream && status.length.has_value();
        } else if (entry.command != Command::Open && entry.command != Command::InfoWindow) {
            enabled = last_status_.hasStream;
        }
        if (entry.button->IsEnabled() != enabled) {
            entry.button->Enable(enabled);
        }
    }
    if (const Icon play = status.playing ? Icon::Pause : Icon::Play; play != play_icon_) {
        play_icon_ = play;
        play_->SetBitmapLabel(bundles_[static_cast<std::size_t>(play)]);
    }
    if (loop_->GetValue() != status.loop) {
        loop_->SetValue(status.loop);
    }
}

// Spec §5.2, §6.3: Idle shows `position` over the thumb at the read-your-writes target; Scrubbing
// and Skimming the thumb's value at the thumb and the marker at the original frame; nothing
// without a range.
void TransportBar::showStrip() {
    TimelineView view;
    if (max_ >= 0) {
        view.lastFrame = max_;
        view.sliderMin = slider_->GetMin();
        view.sliderMax = slider_->GetMax();
        if (scrub_ == ScrubState::Skimming) {  // the number follows the ghost thumb
            view.currentAnchor = last_sent_;
            view.currentText = last_sent_;
            view.marker = original_;
        } else if (scrub_ != ScrubState::Idle) {
            view.currentAnchor = slider_->GetValue();
            view.currentText = slider_->GetValue();
            view.marker = original_;
        } else {
            view.currentAnchor = idle_value_;
            if (last_status_.player.position >= 0) {
                view.currentText = last_status_.player.position;
            }
        }
    }
    strip_->setView(view);
}

}  // namespace rawreel
