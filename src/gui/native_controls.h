#pragma once

// Queries on native controls that wx does not expose (video control panel spec §5.3), and their
// accessible names (§7). One source file per toolkit: native_controls_msw.cpp,
// native_controls_gtk.cpp, native_controls_cocoa.mm, and native_controls_stub.cpp for any other.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>

#include <wx/bmpbndl.h>
#include <wx/slider.h>

class wxDataViewCtrl;

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

// A label in the OS's secondary text colour (window title and Info design §11): macOS
// NSColor.secondaryLabelColor resolved for the window's effective appearance (call it again on
// wxEVT_SYS_COLOUR_CHANGED); GTK the "dim-label" style class; Windows and other toolkits: nothing
// (the normal window text).
void setSecondaryText(wxWindow& label);

// A Settings page icon (Log pane and Settings design §5.1): on macOS the SF Symbol `sf_symbol` (a
// template image, so it follows the light and dark appearance), as wxOSX requires an icon for
// every page; elsewhere an empty bundle (the tabs show no icon).
[[nodiscard]] wxBitmapBundle settingsPageIcon(std::string_view sf_symbol);

// Show in Finder, Show in Explorer, Open Folder (§5.2): the OS file manager with `file_or_folder`
// selected where the OS can (macOS activateFileViewerSelectingURLs:, Windows explorer /select,),
// else the folder (GTK and other toolkits: wxLaunchDefaultApplication on the folder). Selftests
// never call it (selftest.h revealFile).
void revealInFileManager(const std::filesystem::path& file_or_folder);

#if defined(__WXOSX__)
// Edit -> Undo and Redo in a text field (Log pane and Settings design §4.2). wxOSX maps neither
// item to the native undo:/redo: (its menu event goes to the main window), and its
// wxTextEntry::CanUndo() is false for every single-line field. These act on the undo manager of the
// window's first responder when that is an editable NSTextView of `window` (a multi-line control,
// or the field editor that is editing a single-line field or combo box): the manager the text view
// registers its typing with. The call goes to that manager only, not down the responder chain,
// whose window delegate would send it back to the main window as a menu event.
[[nodiscard]] bool canUndoText(wxWindow& window, bool redo);
bool undoText(wxWindow& window, bool redo);  // false when there was nothing to undo or redo
// The NSWindow's representedFilename (spec §5, §8: what SetRepresentedFilename set); "" for none.
[[nodiscard]] wxString representedFilename(wxTopLevelWindow& window);
#endif

#if defined(__WXGTK__)
// `row` of a list wxDataViewCtrl at the top edge of its view: gtk_tree_view_scroll_to_cell with
// the row aligned to the top. wxGTK's EnsureVisible scrolls as little as it can (no alignment), and
// GTK 3 can animate the scroll, so a pair of them (to the end, then back up) may not be exact.
void scrollDataViewRowToTop(wxDataViewCtrl& view, std::size_t row);
// The top row of a list wxDataViewCtrl: the first row of gtk_tree_view_get_visible_range (what
// wxGTK's GetTopItem returns), or the row after it when that row's middle is above the view. With
// the view starting exactly on a row's top edge, GTK can name the hidden row above as the first
// visible one (measured on CI). nullopt: no row in view.
[[nodiscard]] std::optional<std::size_t> dataViewTopRow(wxDataViewCtrl& view);
#endif

}  // namespace rawreel
