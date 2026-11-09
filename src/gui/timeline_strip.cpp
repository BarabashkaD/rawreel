// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/timeline_strip.h"

#include <cmath>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include <wx/dcbuffer.h>

#include "gui/native_controls.h"

namespace rawreel {

namespace {

// Spec §5.2, global constraints: the initial constants, in DIP.
constexpr int MIN_MINOR_TICK_DIP = 4;
constexpr int LABEL_FLIP_HYSTERESIS_DIP = 8;
constexpr int FALLBACK_THUMB_DIP = 20;
constexpr int MIN_WIDTH_DIP = 400;
constexpr int MARKER_WIDTH_DIP = 2;
constexpr int ORIGINAL_GAP_DIP = 2;
// The tick row under the numbers: major ticks, minor ticks at most half as long.
constexpr int TICK_GAP_DIP = 1;
constexpr int MAJOR_TICK_DIP = 7;
constexpr int MINOR_TICK_DIP = 3;

int toInt(double value) {
    return static_cast<int>(std::lround(value));
}

wxColour blend(const wxColour& a, const wxColour& b) {
    auto half = [](unsigned char x, unsigned char y) {
        return static_cast<unsigned char>((static_cast<unsigned>(x) + y) / 2);
    };
    return {half(a.Red(), b.Red()), half(a.Green(), b.Green()), half(a.Blue(), b.Blue())};
}

}  // namespace

TimelineStrip::TimelineStrip(wxWindow* parent, wxSlider& slider,
                             dvv::MoveOnlyFunction<wxColour(wxSystemColour)> colour_source)
    : slider_(slider), colour_source_(std::move(colour_source)) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);  // before Create (wxGTK)
    Create(parent, wxID_ANY);
    SetCanFocus(false);
    readColours();
    updateMinSize();
    Bind(wxEVT_PAINT, &TimelineStrip::onPaint, this);
    Bind(wxEVT_SIZE, &TimelineStrip::onSize, this);
    Bind(wxEVT_DPI_CHANGED, &TimelineStrip::onDpiChanged, this);
    Bind(wxEVT_SYS_COLOUR_CHANGED, &TimelineStrip::onSysColourChanged, this);
    // wx drops the binding when this strip (the sink) is destroyed first.
    slider_.Bind(wxEVT_SIZE, &TimelineStrip::onSliderSize, this);
}

void TimelineStrip::setView(const TimelineView& view) {
    if (view == view_) {
        return;
    }
    if (!view.marker) {
        previous_side_.reset();
    }
    view_ = view;
    Refresh();
}

// Spec §5.1, §5.2: 400 DIP wide; as high as the numbers and the tick row. Only the font and the
// DPI change it, never a resize.
void TimelineStrip::updateMinSize() {
    const int text = GetTextExtent("0123456789").y;
    SetMinSize(wxSize(FromDIP(MIN_WIDTH_DIP), text + FromDIP(TICK_GAP_DIP + MAJOR_TICK_DIP)));
}

bool TimelineStrip::SetFont(const wxFont& font) {
    if (!wxWindow::SetFont(font)) {
        return false;
    }
    updateMinSize();
    Refresh();
    return true;
}

void TimelineStrip::readColours() {
    text_colour_ = colour_source_(wxSYS_COLOUR_BTNTEXT);
    marker_colour_ = colour_source_(wxSYS_COLOUR_HIGHLIGHT);
}

SliderTrack sliderTravel(const wxSlider& slider) {
    if (const std::optional<SliderTrack> track = sliderTrack(slider)) {
        return *track;
    }
    const int thumb = slider.FromDIP(FALLBACK_THUMB_DIP);
    const int half = toInt(thumb / 2.0);
    return SliderTrack{half, slider.GetClientSize().x - half, thumb};
}

// Spec §5.3: the slider's travel shifted into the strip's px.
std::pair<double, double> TimelineStrip::travel() const {
    const double offset = slider_.GetPosition().x - GetPosition().x;
    const SliderTrack track = sliderTravel(slider_);
    return {offset + track.travelStart, offset + track.travelEnd};
}

double TimelineStrip::xOf(std::int64_t frame, const Geometry& geometry) const {
    return thumbX(frame, view_.sliderMin, view_.sliderMax, geometry.travelStart,
                  geometry.travelEnd);
}

// Spec §5.2: the ticks over 0 ... lastFrame and the labels placed by placeLabels. A one-frame
// stream shows only the first label: its "0" is the frame on screen. When "0" and the last
// number do not both fit, the first wins (placeLabels does not test them against each other).
TimelineStrip::Geometry TimelineStrip::computeLayout() {
    Geometry geometry;
    std::tie(geometry.travelStart, geometry.travelEnd) = travel();
    if (!view_.lastFrame) {
        return geometry;
    }
    const std::int64_t last = *view_.lastFrame;
    geometry.interval = chooseTicks(0, last, geometry.travelEnd - geometry.travelStart,
                                    FromDIP(MIN_MINOR_TICK_DIP));

    struct Pending {
        const char* role;
        std::string text;
        double anchor;
        int width;
    };
    auto pending = [this, &geometry](const char* role, std::int64_t frame,
                                     std::optional<double> anchor = std::nullopt) {
        std::string text = std::to_string(frame);
        const int width = GetTextExtent(wxString::FromUTF8(text)).x;
        return Pending{role, std::move(text), anchor.value_or(xOf(frame, geometry)), width};
    };
    std::optional<Pending> current;
    std::optional<Pending> original;
    std::optional<Pending> last_label;
    if (last > 0 && view_.currentText && view_.currentAnchor) {
        current = pending("current", *view_.currentText, xOf(*view_.currentAnchor, geometry));
    }
    if (view_.marker) {
        original = pending("original", *view_.marker);
    }
    const Pending first = pending("first", 0);
    if (last > 0) {
        last_label = pending("last", last);
    }
    auto in = [](const std::optional<Pending>& label) -> std::optional<LabelIn> {
        if (!label) {
            return std::nullopt;
        }
        return LabelIn{.anchor = label->anchor, .width = static_cast<double>(label->width)};
    };
    TimelineLabelsIn labels_in{
        .stripWidth = static_cast<double>(GetClientSize().x),
        .gap = static_cast<double>(FromDIP(ORIGINAL_GAP_DIP)),
        .hysteresis = static_cast<double>(FromDIP(LABEL_FLIP_HYSTERESIS_DIP)),
        .current = in(current),
        .original = in(original),
        .first = in(first),
        .last = in(last_label),
        .previousSide = previous_side_,
    };
    TimelineLabelsOut out = placeLabels(labels_in);
    if (view_.marker) {
        previous_side_ = out.originalSide;
    }
    if (out.first && out.last && *out.first + first.width > *out.last) {
        out.last.reset();
    }
    auto add = [&geometry](const std::optional<Pending>& label, std::optional<double> x) {
        if (label && x) {
            geometry.labels.push_back({.role = label->role,
                                       .text = label->text,
                                       .x = toInt(*x),
                                       .anchor = toInt(label->anchor),
                                       .width = label->width});
        }
    };
    add(current, out.current);
    add(original, out.original);
    add(first, out.first);
    add(last_label, out.last);
    return geometry;
}

TimelineModel TimelineStrip::modelForTest() {
    return {.labels = computeLayout().labels, .marker = view_.marker};
}

std::pair<double, double> TimelineStrip::travelForTest() {
    return travel();
}

// Spec §5.2, §8: the parent's background, the ticks, the marker, then the numbers.
void TimelineStrip::onPaint(wxPaintEvent& /*event*/) {
    wxAutoBufferedPaintDC dc(this);
    const wxColour background = GetParent()->GetBackgroundColour();
    dc.SetBackground(wxBrush(background));
    dc.Clear();
    if (!view_.lastFrame) {
        return;
    }
    const Geometry geometry = computeLayout();
    const int height = GetClientSize().y;
    const wxColour minor = blend(text_colour_, background);
    for (const Tick& tick : ticks(0, *view_.lastFrame, geometry.interval)) {
        const int x = toInt(xOf(tick.frame, geometry));
        const int length = FromDIP(tick.major ? MAJOR_TICK_DIP : MINOR_TICK_DIP);
        dc.SetPen(wxPen(tick.major ? text_colour_ : minor, 1));
        dc.DrawLine(x, height - length, x, height);
    }
    if (view_.marker) {
        const int width = FromDIP(MARKER_WIDTH_DIP);
        const int x = toInt(xOf(*view_.marker, geometry));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(marker_colour_));
        dc.DrawRectangle(x - (width / 2), 0, width, height);
    }
    dc.SetFont(GetFont());
    dc.SetTextForeground(text_colour_);
    for (const TimelineLabel& label : geometry.labels) {
        dc.DrawText(wxString::FromUTF8(label.text), label.x, 0);
    }
}

void TimelineStrip::onSize(wxSizeEvent& event) {
    Refresh();
    event.Skip();
}

void TimelineStrip::onSliderSize(wxSizeEvent& event) {
    Refresh();
    event.Skip();
}

void TimelineStrip::onDpiChanged(wxDPIChangedEvent& event) {
    updateMinSize();
    Refresh();
    event.Skip();
}

void TimelineStrip::onSysColourChanged(wxSysColourChangedEvent& event) {
    readColours();
    Refresh();
    event.Skip();
}

}  // namespace rawreel
