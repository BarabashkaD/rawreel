// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <algorithm>
#include <cstdint>

#include <wx/msw/wrapwin.h>

#include "dvv/platform/window_handle_win32.h"
#include "gui/viewport_window.h"

namespace rawreel {

dvv::Result<ViewportWindow> viewportWindowOf(wxWindow& window) {
    const auto hwnd = static_cast<HWND>(window.GetHWND());
    // The module that registered the window class: rawreel-gui itself (wx is linked statically).
    const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
    return ViewportWindow{.window = dvv::platform::windowHandleOf(
                              dvv::platform::Win32Window{.hinstance = instance, .hwnd = hwnd}),
                          .lent = {}};
}

void syncNativeWindow(wxWindow& /*window*/) {}

dvv::platform::PixelSize nativePixelSize(const wxWindow& window) {
    const wxSize size = window.ToPhys(window.GetClientSize());
    return {.width = static_cast<std::uint32_t>(std::max(size.x, 0)),
            .height = static_cast<std::uint32_t>(std::max(size.y, 0))};
}

bool setBackingScaleForTest(wxWindow& /*window*/, double /*scale*/) {
    return false;
}

}  // namespace rawreel
