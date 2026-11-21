// The native slider queries on MSW: a wxSlider without label styles is one trackbar control whose
// client coordinates are the wx window's client coordinates (physical pixels, like wx's).

// clang-format off
#include <wx/wx.h>  // first: wx's Windows headers before anything else includes <windows.h> (R11)
// clang-format on

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <commctrl.h>  // TBM_*; needs <windows.h>, which wx/wx.h has included already
#include <shellapi.h>  // ShellExecuteW
#include <windows.h>
#if wxUSE_ACCESSIBILITY
#include <wx/access.h>
#endif

#include <wx/listctrl.h>
#include <wx/msw/wrapcctl.h>  // ListView_GetSelectionMark, ListView_SetSelectionMark

#include "gui/native_controls.h"
#include "gui/native_controls_wx.h"

namespace rawreel {

namespace {

// The trackbar's channel and thumb rectangles in its client coordinates; false without a window.
bool trackbarRects(const wxSlider& slider, RECT& channel, RECT& thumb) {
    const HWND trackbar = static_cast<HWND>(slider.GetHWND());
    if (trackbar == nullptr) {
        return false;
    }
    ::SendMessageW(trackbar, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channel));
    ::SendMessageW(trackbar, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
    return true;
}

#if wxUSE_ACCESSIBILITY
// Answers the name for the window itself; everything else falls back to the control's standard
// MSAA object (wx's wxIAccessible does that for wxACC_NOT_IMPLEMENTED).
class NamedAccessible final : public wxAccessible {
public:
    NamedAccessible(wxWindow* window, const wxString& name) : wxAccessible(window), name_(name) {}

    wxAccStatus GetName(int child_id, wxString* name) override {
        if (child_id != wxACC_SELF || name == nullptr) {
            return wxACC_NOT_IMPLEMENTED;
        }
        *name = name_;
        return wxACC_OK;
    }

private:
    wxString name_;
};
#endif

}  // namespace

// The thumb's centre travels over the channel less half a thumb at each end.
std::optional<SliderTrack> sliderTrack(const wxSlider& slider) {
    RECT channel{};
    RECT thumb{};
    if (!trackbarRects(slider, channel, thumb)) {
        return std::nullopt;
    }
    // The thumb's centre stops half a thumb inside the channel's ends (measured in CI: channel
    // 8..788, thumb 11 wide, centre 13 at the minimum).
    const int width = static_cast<int>(thumb.right - thumb.left);
    return SliderTrack{static_cast<int>(channel.left) + (width / 2),
                       static_cast<int>(channel.right) - (width - (width / 2)), width};
}

std::optional<int> nativeThumbCentre(const wxSlider& slider) {
    RECT channel{};
    RECT thumb{};
    if (!trackbarRects(slider, channel, thumb)) {
        return std::nullopt;
    }
    return static_cast<int>((thumb.left + thumb.right) / 2);
}

// Mouse events on the slider are in its wx client coordinates already.
int sliderEventX(const wxSlider& /*slider*/, int event_x) {
    return event_x;
}

// The thumb's rectangle in the trackbar's client coordinates, which are the wx ones.
std::optional<wxRect> sliderThumbRect(const wxSlider& slider) {
    const HWND trackbar = static_cast<HWND>(slider.GetHWND());
    if (trackbar == nullptr) {
        return std::nullopt;
    }
    RECT thumb{};
    ::SendMessageW(trackbar, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
    return wxRect(static_cast<int>(thumb.left), static_cast<int>(thumb.top),
                  static_cast<int>(thumb.right - thumb.left),
                  static_cast<int>(thumb.bottom - thumb.top));
}

// Not drawn yet on this toolkit (spec §6.4): skimming still previews and moves the number.
void showSliderGhost(wxSlider& /*slider*/, std::optional<int> /*x*/) {}

// Windows: labels keep the normal window text (as File Properties).
void setSecondaryText(wxWindow& /*label*/) {}

// No icons on the tabs of the Windows dialog.
wxBitmapBundle settingsPageIcon(std::string_view /*sf_symbol*/) {
    return {};
}

// Explorer selects a file (explorer /select,"<file>") and opens a folder.
void revealInFileManager(const std::filesystem::path& file_or_folder) {
    std::error_code error;
    const bool folder = std::filesystem::is_directory(file_or_folder, error);
    const std::wstring arguments = (folder ? std::wstring(L"\"") : std::wstring(L"/select,\"")) +
                                   file_or_folder.wstring() + L"\"";
    ::ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
}

// The window owns the wxAccessible (SetAccessible deletes the previous one).
void setAccessibleName(wxWindow& window, const wxString& name) {
#if wxUSE_ACCESSIBILITY
    window.SetAccessible(new NamedAccessible(&window, name));
#else
    // A documented gap (spec §7): this wx build has no wxAccessible.
    static_cast<void>(window);
    static_cast<void>(name);
#endif
}

// wx's own undo of the entry: `window` is the same control.
bool canUndoText(wxWindow& /*window*/, const wxTextEntry& entry, bool redo) {
    return wx_generic::canUndoText(entry, redo);
}

bool undoText(wxWindow& /*window*/, wxTextEntry& entry, bool redo) {
    return wx_generic::undoText(entry, redo);
}

wxString representedFilename(wxTopLevelWindow& /*window*/) {
    return wx_generic::representedFilename();
}

std::optional<std::size_t> dataViewTopRow(wxDataViewCtrl& view) {
    return wx_generic::dataViewTopRow(view);
}

void scrollDataViewRowToTop(wxDataViewCtrl& view, std::size_t row) {
    wx_generic::scrollDataViewRowToTop(view, row);
}

std::optional<std::size_t> listSelectionMark(const wxListCtrl& list) {
    if (const int mark = ListView_GetSelectionMark(static_cast<HWND>(list.GetHWND())); mark >= 0) {
        return static_cast<std::size_t>(mark);
    }
    return std::nullopt;
}

void setListSelectionMark(wxListCtrl& list, std::optional<std::size_t> row) {
    static_cast<void>(ListView_SetSelectionMark(static_cast<HWND>(list.GetHWND()),
                                                row ? static_cast<int>(*row) : -1));
}

}  // namespace rawreel
