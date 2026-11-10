// The native slider queries on wxGTK (GTK 3). wxSlider's GtkScale is its public m_scale: the wx
// widget itself, or a child of the GtkBox that also holds the label styles' labels. GtkRange
// reports its range rect and slider extent in the coordinates of the GdkWindow it draws into (the
// widget's allocation coordinates; GtkScale has no window of its own); they are converted to the
// scale's own coordinates, then to the wx widget's.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>

#include <gtk/gtk.h>
#include <wx/dataview.h>

#include "dvv/media/types.h"
#include "gui/native_controls.h"

namespace rawreel {

namespace {

struct ScaleGeometry {
    GdkRectangle range{};  // gtk_range_get_range_rect
    gint sliderStart = 0;  // gtk_range_get_slider_range: the thumb's extent
    gint sliderEnd = 0;
    gint toWx = 0;  // added to an x of the two above to give a wx client x
};

std::optional<ScaleGeometry> scaleGeometry(const wxSlider& slider) {
    GtkWidget* const scale = slider.m_scale;
    GtkWidget* const widget = slider.GetHandle();
    if (scale == nullptr || widget == nullptr) {
        return std::nullopt;
    }
    ScaleGeometry geometry;
    gtk_range_get_range_rect(GTK_RANGE(scale), &geometry.range);
    gtk_range_get_slider_range(GTK_RANGE(scale), &geometry.sliderStart, &geometry.sliderEnd);
    GtkAllocation allocation{};
    gtk_widget_get_allocation(scale, &allocation);
    gint widget_x = 0;
    gint widget_y = 0;
    if (gtk_widget_translate_coordinates(scale, widget, -allocation.x, 0, &widget_x, &widget_y) ==
        FALSE) {
        return std::nullopt;  // not realized yet
    }
    geometry.toWx = widget_x;
    return geometry;
}

}  // namespace

// GtkRange places the thumb (its slider gadget's margin box) inside the trough: its left edge
// travels from the range's left end to the right end less the thumb's width.
std::optional<SliderTrack> sliderTrack(const wxSlider& slider) {
    const std::optional<ScaleGeometry> geometry = scaleGeometry(slider);
    if (!geometry) {
        return std::nullopt;
    }
    const int thumb = geometry->sliderEnd - geometry->sliderStart;
    const double half = thumb / 2.0;
    const double start = geometry->toWx + geometry->range.x + half;
    const double end = geometry->toWx + geometry->range.x + geometry->range.width - half;
    return SliderTrack{static_cast<int>(std::lround(start)), static_cast<int>(std::lround(end)),
                       thumb};
}

std::optional<int> nativeThumbCentre(const wxSlider& slider) {
    const std::optional<ScaleGeometry> geometry = scaleGeometry(slider);
    if (!geometry) {
        return std::nullopt;
    }
    const double centre = geometry->toWx + ((geometry->sliderStart + geometry->sliderEnd) / 2.0);
    return static_cast<int>(std::lround(centre));
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

// The window's widget (a button's GtkButton) is the element ATK exposes.
void setAccessibleName(wxWindow& window, const wxString& name) {
    GtkWidget* const widget = window.GetHandle();
    if (widget == nullptr) {
        return;
    }
    if (AtkObject* const accessible = gtk_widget_get_accessible(widget); accessible != nullptr) {
        atk_object_set_name(accessible, name.utf8_str().data());
    }
}

// No icons on the tabs of the dialog.
wxBitmapBundle settingsPageIcon(std::string_view /*sf_symbol*/) {
    return {};
}

// The file manager cannot select a file here: the folder opens.
void revealInFileManager(const std::filesystem::path& file_or_folder) {
    std::error_code error;
    const std::filesystem::path folder = std::filesystem::is_directory(file_or_folder, error)
                                             ? file_or_folder
                                             : file_or_folder.parent_path();
    (void)wxLaunchDefaultApplication(wxString::FromUTF8(dvv::media::utf8FromPath(folder)));
}

// A list model's path is the row alone. Aligned scrolls are deferred by GtkTreeView itself until
// its rows are laid out, like EnsureVisible's.
void scrollDataViewRowToTop(wxDataViewCtrl& view, std::size_t row) {
    GtkWidget* const tree = view.GtkGetTreeView();
    if (tree == nullptr || row > static_cast<std::size_t>(std::numeric_limits<gint>::max())) {
        return;
    }
    gint index = static_cast<gint>(row);
    GtkTreePath* const path = gtk_tree_path_new_from_indicesv(&index, 1);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(tree), path, nullptr, TRUE, 0.0F, 0.0F);
    gtk_tree_path_free(path);
}

}  // namespace rawreel
