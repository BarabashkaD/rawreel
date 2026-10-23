#pragma once

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "dvv/platform/native_window.h"

namespace rawreel {

// The native handle of a wx window for dvv::platform::createSurface. MSW: Win32Window{instance of
// the window's class, HWND}. Other toolkits: std::monostate until their native-window path exists
// (wxGTK/X11 follows with Linux support), so the viewport reports Unsupported.
[[nodiscard]] dvv::platform::NativeWindow nativeWindowOf(wxWindow& window);

}  // namespace rawreel
