// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/transport_bar.h"

#include <algorithm>
#include <format>
#include <limits>
#include <string>
#include <utility>

#include "dvv/base/log.h"

namespace rawreel {

namespace {

const char* scrubName(ScrubState state) {
    return state == ScrubState::Scrubbing ? "Scrubbing" : "Idle";
}

std::string labelText(const PlayerStatus& status) {
    const std::string position =
        status.position >= 0 ? std::to_string(status.position) : std::string("-");
    if (status.length) {
        return std::format("frame {} / {}", position, *status.length);
    }
    return "frame " + position;
}

}  // namespace

TransportBar::TransportBar(wxWindow* parent, dvv::MoveOnlyFunction<void(Command)> execute,
                           dvv::MoveOnlyFunction<void(std::int64_t)> seek)
    : wxPanel(parent), execute_(std::move(execute)), seek_(std::move(seek)) {
    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    addButton(*sizer, "|<", Command::First);
    addButton(*sizer, "<", Command::StepBack);
    play_ = new wxButton(this, wxID_ANY, "Play");
    play_->SetCanFocus(false);
    play_->Bind(wxEVT_BUTTON, [this](wxCommandEvent& /*event*/) { execute_(Command::PlayPause); });
    sizer->Add(play_, wxSizerFlags().Border(wxALL, FromDIP(2)));
    addButton(*sizer, ">", Command::StepForward);
    addButton(*sizer, ">|", Command::Last);
    loop_ = new wxToggleButton(this, wxID_ANY, "Loop");
    loop_->SetCanFocus(false);
    loop_->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& /*event*/) { execute_(Command::Loop); });
    sizer->Add(loop_, wxSizerFlags().Border(wxALL, FromDIP(2)));
    slider_ = new wxSlider(this, wxID_ANY, 0, 0, 1);
    slider_->Disable();
    slider_->Bind(wxEVT_SCROLL_THUMBTRACK, &TransportBar::onScroll, this);
    slider_->Bind(wxEVT_SCROLL_CHANGED, &TransportBar::onScroll, this);
    slider_->Bind(wxEVT_SCROLL_THUMBRELEASE, &TransportBar::onScroll, this);
    sizer->Add(slider_, wxSizerFlags(1).CenterVertical().Border(wxALL, FromDIP(2)));
    label_ = new wxStaticText(this, wxID_ANY, "frame -");
    // Wide enough for "frame 9999999 / 9999999" without relayout on every change.
    label_->SetMinSize(wxSize(label_->GetTextExtent("frame 9999999 / 9999999").x, -1));
    sizer->Add(label_, wxSizerFlags().CenterVertical().Border(wxALL, FromDIP(4)));
    SetSizer(sizer);
}

void TransportBar::addButton(wxSizer& sizer, const wxString& label, Command command) {
    auto* button =
        new wxButton(this, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    button->SetCanFocus(false);
    button->Bind(wxEVT_BUTTON, [this, command](wxCommandEvent& /*event*/) { execute_(command); });
    sizer.Add(button, wxSizerFlags().Border(wxALL, FromDIP(2)));
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
    scrub_ = state;
}

// Spec §6.6. THUMBTRACK starts a scrub; THUMBTRACK and CHANGED seek live (never the same value
// twice in one interaction); THUMBRELEASE and CHANGED end it. A disabled slider ignores all.
void TransportBar::onScroll(wxScrollEvent& event) {
    if (max_ < 0) {
        return;
    }
    const wxEventType type = event.GetEventType();
    if (type == wxEVT_SCROLL_THUMBTRACK) {
        setScrub(ScrubState::Scrubbing);
    }
    if (type == wxEVT_SCROLL_THUMBTRACK || type == wxEVT_SCROLL_CHANGED) {
        if (const int value = event.GetPosition(); value != last_sent_) {
            last_sent_ = value;
            seek_(value);
        }
    }
    if (type == wxEVT_SCROLL_THUMBRELEASE || type == wxEVT_SCROLL_CHANGED) {
        setScrub(ScrubState::Idle);
    }
}

void TransportBar::update(const PlayerStatus& status) {
    // The slider's range is an int; longer streams are clamped (the slider then ends early).
    constexpr std::int64_t INT_LIMIT = std::numeric_limits<int>::max();
    const int max = status.length && *status.length > 0
                        ? static_cast<int>(std::min(*status.length - 1, INT_LIMIT))
                        : -1;
    if (max != max_) {
        // Another stream (or none): whatever the user was dragging belongs to the old one.
        setScrub(ScrubState::Idle);
        last_sent_ = -1;
        max_ = max;
        slider_->SetRange(0, std::max(max, 1));
        slider_->Enable(max >= 0);
    }
    // Backstop for input that ends without a release (macOS sends no CHANGED; keys on a focused
    // macOS slider send THUMBTRACK only): it runs on a notification, never on a timer.
    if (scrub_ == ScrubState::Scrubbing && !leftButtonDown()) {
        setScrub(ScrubState::Idle);
    }
    if (scrub_ == ScrubState::Idle) {
        last_sent_ = -1;
        if (max_ >= 0) {
            // A queued seek may briefly name a frame beyond a new stream's end: clamp.
            const int value = static_cast<int>(std::clamp<std::int64_t>(status.target, 0, max_));
            if (slider_->GetValue() != value) {
                slider_->SetValue(value);
            }
        }
    }
    const wxString play = status.playing ? "Pause" : "Play";
    if (play_->GetLabel() != play) {
        play_->SetLabel(play);
    }
    if (loop_->GetValue() != status.loop) {
        loop_->SetValue(status.loop);
    }
    const wxString label = wxString::FromUTF8(labelText(status));
    if (label_->GetLabel() != label) {
        label_->SetLabel(label);
    }
    if (update_observer_) {
        update_observer_(slider_->GetValue());
    }
}

}  // namespace rawreel
