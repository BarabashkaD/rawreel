// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>

#include "dvv/media/types.h"
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

// No secondary text colour for this toolkit.
void setSecondaryText(wxWindow& /*label*/) {}

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

}  // namespace rawreel
