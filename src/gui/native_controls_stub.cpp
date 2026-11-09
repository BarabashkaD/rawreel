// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <optional>

#include "gui/native_controls.h"

namespace rawreel {

// No native slider query for this toolkit (MSW, wxGTK and wxOSX have theirs): callers use their
// fallback.
std::optional<SliderTrack> sliderTrack(const wxSlider& /*slider*/) {
    return std::nullopt;
}

std::optional<int> nativeThumbCentre(const wxSlider& /*slider*/) {
    return std::nullopt;
}

// Mouse events on the slider are in its wx client coordinates already.
int sliderEventX(const wxSlider& /*slider*/, int event_x) {
    return event_x;
}

std::optional<wxRect> sliderThumbRect(const wxSlider& /*slider*/) {
    return std::nullopt;
}

// Not drawn yet on this toolkit (spec §6.4): skimming still previews and moves the number.
void showSliderGhost(wxSlider& /*slider*/, std::optional<int> /*x*/) {}

// No accessible name for this toolkit: SetName only.
void setAccessibleName(wxWindow& /*window*/, const wxString& /*name*/) {}

}  // namespace rawreel
