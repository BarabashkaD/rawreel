// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/transport_bar.h"

#include <algorithm>
#include <format>
#include <limits>
#include <string>
#include <utility>

namespace rawreel {

namespace {

// The slider keeps the value of a scroll event this long against the status timer.
constexpr std::chrono::milliseconds SCROLL_HOLD{250};

std::string labelText(const PlayerStatus& status) {
    const std::string position =
        status.position >= 0 ? std::to_string(status.position) : std::string("-");
    if (status.length) {
        return std::format("frame {} / {}", position, *status.length);
    }
    return "frame " + position;
}

}  // namespace

TransportBar::TransportBar(wxWindow* parent, std::move_only_function<void(Command)> execute,
                           std::move_only_function<void(std::int64_t)> seek)
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

void TransportBar::onScroll(wxScrollEvent& event) {
    last_scroll_ = std::chrono::steady_clock::now();
    seek_(event.GetPosition());
}

void TransportBar::update(const PlayerStatus& status) {
    // The slider's range is an int; longer streams are clamped (the slider then ends early).
    constexpr std::int64_t INT_LIMIT = std::numeric_limits<int>::max();
    const int max = status.length && *status.length > 0
                        ? static_cast<int>(std::min(*status.length - 1, INT_LIMIT))
                        : -1;
    if (max != max_) {
        max_ = max;
        slider_->SetRange(0, std::max(max, 1));
        slider_->Enable(max >= 0);
    }
    if (max_ >= 0 && std::chrono::steady_clock::now() - last_scroll_ >= SCROLL_HOLD) {
        const int value = static_cast<int>(std::clamp<std::int64_t>(status.position, 0, max_));
        if (slider_->GetValue() != value) {
            slider_->SetValue(value);
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
}

}  // namespace rawreel
