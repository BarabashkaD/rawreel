// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <wx/msw/wrapwin.h>

#include "gui/native_window.h"

namespace rawreel {

dvv::Result<ViewportWindow> nativeWindowOf(wxWindow& window) {
    const auto hwnd = static_cast<HWND>(window.GetHWND());
    // The module that registered the window class: rawreel-gui itself (wx is linked statically).
    const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
    return ViewportWindow{.window = dvv::platform::Win32Window{.hinstance = instance, .hwnd = hwnd},
                          .connection = {}};
}

void syncNativeWindow(wxWindow& /*window*/) {}

}  // namespace rawreel
