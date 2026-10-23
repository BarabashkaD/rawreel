// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <wx/msw/wrapwin.h>

#include "native_window.h"

namespace rawreel {

dvv::platform::NativeWindow nativeWindowOf(wxWindow& window) {
    const auto hwnd = static_cast<HWND>(window.GetHWND());
    // The module that registered the window class: rawreel-gui itself (wx is linked statically).
    const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
    return dvv::platform::Win32Window{.hinstance = instance, .hwnd = hwnd};
}

}  // namespace rawreel
