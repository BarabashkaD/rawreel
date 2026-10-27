#pragma once

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <memory>

#include "dvv/base/error.h"
#include "dvv/platform/native_window.h"

namespace rawreel {

// The native window a viewport presenter draws to, and what that presenter's surface needs besides
// the window.
struct ViewportWindow {
    dvv::platform::NativeWindow window;  // never std::monostate
    // wxGTK: the presenter's own X connection (the window's Display* is this connection), closed
    // when the last copy goes. Release it only after the presenter, its surface and its swapchain
    // are destroyed, and while the window still exists. Empty on MSW.
    std::shared_ptr<void> connection;
};

// The native window of a wx window for the viewport's presenter. MSW: Win32Window{instance of the
// window's class, HWND}. wxGTK: XlibWindow{a private Display* opened on GDK's display, the
// window's XID}; the window is realized, made native and synced to the X server first. A private
// connection keeps the presenter's large image transfers off GDK's connection, so the GUI thread
// does not wait for them. An error if the window has no native X11 window (GDK does not run on
// X11) or the connection cannot be opened. Other toolkits: an error, so the viewport reports a
// failure.
[[nodiscard]] dvv::Result<ViewportWindow> nativeWindowOf(wxWindow& window);

// Call before the presenter is told the new size. wxGTK: waits until the X server has processed
// every pending request of GDK's connection (the window's new geometry among them), so the
// presenter's own connection reads the new size and its first swapchain fits. MSW and other
// toolkits: nothing (the presenter shares the window's state directly).
void syncNativeWindow(wxWindow& window);

}  // namespace rawreel
