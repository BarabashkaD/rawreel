#pragma once

// Queries on native controls that wx does not expose (video control panel spec §5.3), and their
// accessible names (§7). One source file per toolkit: native_controls_msw.cpp,
// native_controls_gtk.cpp, native_controls_cocoa.mm, and native_controls_stub.cpp for any other.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <optional>

#include <wx/slider.h>

namespace rawreel {

struct SliderTrack {
    int travelStart;  // wx client px
    int travelEnd;
    int thumbWidth;
};

// The thumb centre's travel limits from the native control (spec §5.3), in the slider's wx client
// coordinates: Windows TBM_GETCHANNELRECT + TBM_GETTHUMBRECT; GTK gtk_range_get_range_rect +
// gtk_range_get_slider_range on wxSlider::m_scale, converted from the GtkScale to the wx widget;
// macOS the cell's barRectFlipped: + knobRectFlipped:, minus the NSView's alignment-rect inset that
// wxOSX applies. nullopt: no query on this platform. A horizontal slider without label styles.
[[nodiscard]] std::optional<SliderTrack> sliderTrack(const wxSlider& slider);
// The native thumb's centre at the slider's current value, same coordinates. Lab and tests only.
[[nodiscard]] std::optional<int> nativeThumbCentre(const wxSlider& slider);

// The x of a mouse event on `slider` (its GetX()) in the wx client coordinates sliderTrack uses:
// macOS event_x plus the bounds-to-wx offset of sliderTrack (wxOSX gives a mouse x in the NSView's
// bounds, which differ by the alignment-rect inset), rounded; elsewhere event_x.
[[nodiscard]] int sliderEventX(const wxSlider& slider, int event_x);

// Whether the native slider pages (moves a page step) on a press in its channel instead of jumping
// (spec §6.5): Windows. macOS and GTK jump natively.
inline constexpr bool SLIDER_PAGES_ON_TRACK_CLICK =
#if defined(__WXMSW__)
    true;
#else
    false;
#endif

// The native thumb's rectangle in the slider's wx client coordinates (Windows TBM_GETTHUMBRECT);
// nullopt: no query on this platform.
[[nodiscard]] std::optional<wxRect> sliderThumbRect(const wxSlider& slider);

// The ghost thumb of a skim (spec §6.4): the slider's knob drawn grey at 50 % opacity on the
// native slider, centred at the wx client x `x`; nullopt removes it. macOS: a CALayer on the
// NSSlider's layer, sized from the cell's knob rect. GTK, Windows and other toolkits: not drawn
// yet.
void showSliderGhost(wxSlider& slider, std::optional<int> x);

// The name screen readers announce for `window` (spec §7): macOS setAccessibilityLabel: on its
// NSView; GTK atk_object_set_name on its widget's AtkObject; Windows a wxAccessible returning the
// name (wxUSE_ACCESSIBILITY; without it, nothing). Other toolkits: nothing.
void setAccessibleName(wxWindow& window, const wxString& name);

}  // namespace rawreel
