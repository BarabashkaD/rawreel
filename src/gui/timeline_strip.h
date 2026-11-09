#pragma once

// The timeline strip above the transport bar's slider (video control panel spec §5.2, §5.3, §8):
// ticks over the slider's range and the first, last, current and original frame numbers, drawn
// over the native thumb's travel. The geometry comes from core/timeline.h; the strip measures the
// text, reads the travel from the slider and paints.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <wx/settings.h>
#include <wx/slider.h>

#include "core/timeline.h"
#include "dvv/base/move_only_function.h"
#include "gui/native_controls.h"

namespace rawreel {

// What the strip shows; the bar builds it from its status and state.
struct TimelineView {
    std::optional<std::int64_t> lastFrame;  // min(length - 1, INT_MAX); nullopt: empty strip
    std::int64_t sliderMin = 0, sliderMax = 1;
    std::optional<std::int64_t> currentAnchor;  // frame the current label sits over
    std::optional<std::int64_t> currentText;    // number it shows; nullopt: none
    std::optional<std::int64_t> marker;         // original frame while Scrubbing
    bool operator==(const TimelineView&) const = default;
};

struct TimelineLabel {
    std::string role;  // "current", "original", "first", "last"
    std::string text;
    int x;       // left edge, strip px
    int anchor;  // the x it belongs to, strip px
    int width;
};

// The labels a paint draws, in the order current, original, first, last (hidden ones left out).
struct TimelineModel {
    std::vector<TimelineLabel> labels;
    std::optional<std::int64_t> marker;
};

// The slider's thumb travel in its wx client px (spec §5.3): sliderTrack, else a thumb of
// FALLBACK_THUMB_DIP with half of it (rounded) in from each end. The strip, the bar's frameAt and
// the tests use it.
[[nodiscard]] SliderTrack sliderTravel(const wxSlider& slider);

class TimelineStrip final : public wxWindow {
public:
    // `slider` is a sibling (same parent): the travel is the slider's track shifted by the
    // difference of their positions. `colour_source` gives the system colours (spec §5.2).
    TimelineStrip(wxWindow* parent, wxSlider& slider,
                  dvv::MoveOnlyFunction<wxColour(wxSystemColour)> colour_source);

    void setView(const TimelineView& view);  // Refresh() only when the view changed

    [[nodiscard]] TimelineModel modelForTest();               // the layout a paint computes
    [[nodiscard]] std::pair<double, double> travelForTest();  // strip px
    [[nodiscard]] wxColour textColourForTest() const { return text_colour_; }

    bool AcceptsFocus() const override { return false; }
    bool AcceptsFocusFromKeyboard() const override { return false; }
    bool SetFont(const wxFont& font) override;

private:
    struct Geometry {
        double travelStart = 0;
        double travelEnd = 0;
        TickInterval interval;
        std::vector<TimelineLabel> labels;
    };

    // The track-dependent layout; it stores the original label's side (spec §5.2 hysteresis).
    [[nodiscard]] Geometry computeLayout();
    [[nodiscard]] std::pair<double, double> travel() const;
    [[nodiscard]] double xOf(std::int64_t frame, const Geometry& geometry) const;
    void updateMinSize();
    void readColours();
    void onPaint(wxPaintEvent& event);
    void onSize(wxSizeEvent& event);
    void onSliderSize(wxSizeEvent& event);
    void onDpiChanged(wxDPIChangedEvent& event);
    void onSysColourChanged(wxSysColourChangedEvent& event);

    wxSlider& slider_;
    dvv::MoveOnlyFunction<wxColour(wxSystemColour)> colour_source_;
    TimelineView view_;
    std::optional<Side> previous_side_;
    wxColour text_colour_;
    wxColour marker_colour_;
};

}  // namespace rawreel
