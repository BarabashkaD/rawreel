#pragma once

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <memory>

#include "dvv/base/error.h"
#include "dvv/platform/pixel_size.h"
#include "dvv/platform/window_handle.h"

namespace rawreel {

// The window a viewport presenter draws to, and what that presenter's surface needs besides it.
struct ViewportWindow {
    dvv::platform::WindowHandle window;  // never empty
    // What the window lends the presenter, released when the last copy goes. Release it only after
    // the presenter, its surface and its swapchain are destroyed, and while the window still
    // exists, on the GUI thread. wxGTK: the presenter's own X connection (the handle's Display* is
    // this connection), closed. wxOSX: the child NSView that carries the CAMetalLayer, removed from
    // the window's view (so the layer outlives every surface made from it). Empty on MSW.
    std::shared_ptr<void> lent;
};

// The window handle of a wx window for the viewport's presenter, and what the window lends it.
// MSW: Win32Window{instance of the window's class, HWND}. wxGTK: XlibWindow{a private Display*
// opened on GDK's display, the window's XID}; the window is realized, made native and synced to
// the X server first. A private connection keeps the presenter's large image transfers off GDK's
// connection, so the GUI thread does not wait for them. An error if the window has no native X11
// window (GDK does not run on X11) or the connection cannot be opened. wxOSX: MetalWindow{the
// CAMetalLayer of a new layer-backed child NSView of the window's view (GetHandle())}; the child
// covers the window's bounds, never takes clicks or drops (they reach the window's view) and lives
// as long as `lent`; while it lives, a backing scale change (the window moved to another display)
// updates the layer's contentsScale and calls Viewport::resizePresenter() on the main thread when
// `window` is a Viewport.
[[nodiscard]] dvv::Result<ViewportWindow> viewportWindowOf(wxWindow& window);

// Call before the presenter is told the new size. wxGTK: waits until the X server has processed
// every pending request of GDK's connection (the window's new geometry among them), so the
// presenter's own connection reads the new size and its first swapchain fits. wxOSX: sets the
// child view's frame to the window's bounds and its layer's contentsScale to the backing scale
// (drawableSize is left to the swapchain). MSW: nothing (the presenter shares the window's state
// directly).
void syncNativeWindow(wxWindow& window);

// The presenter's extent. wxOSX: physicalPixelSize() of the child view's size in points and its
// layer's contentsScale; without a child view, of the window's client size and its content scale
// factor. MSW and wxGTK: the client area in physical pixels (wx 3.3 ToPhys), negative sizes as 0.
[[nodiscard]] dvv::platform::PixelSize nativePixelSize(const wxWindow& window);

// Backing pixels per window unit, the scale of nativePixelSize(). wxOSX: the child view's layer
// contentsScale (it follows the backing_scale test hook); without a child view, and on MSW and
// wxGTK, the window's content scale factor.
[[nodiscard]] double nativeContentScale(const wxWindow& window);

// Test only (the backing_scale selftest). wxOSX: `scale` > 0 replaces the window's backing scale
// factor for the child view (0 gives it back), then the child's viewDidChangeBackingProperties
// runs, as Cocoa runs it after a display change. False when the window has no child view (no
// presenter). MSW and wxGTK: false (no hook).
bool setBackingScaleForTest(wxWindow& window, double scale);

}  // namespace rawreel
