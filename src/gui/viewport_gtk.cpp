// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <memory>
#include <string>
#include <utility>

#include "dvv/base/error.h"
#include "dvv/platform/native_window.h"
#include "gui/native_window.h"

// Last: Xlib's macros (None, Bool, Status, True, False) stay out of the headers above.
#include <X11/Xlib.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

namespace rawreel {

namespace {

dvv::Result<ViewportWindow> failure(const std::string& message) {
    return dvv::makeError(dvv::Errc::Internal, message);
}

// The deleter of the presenter's connection: called after the presenter, its surface and its
// swapchain are gone, while the window still exists (Viewport::stopPresenting). XCloseDisplay
// syncs, so X errors of earlier unchecked requests on this connection reach the process-wide
// Xlib error handler here. That handler is GDK's: it ignores errors on a Display GDK did not open
// (checked with GTK 3.24 and a BadWindow), so they cannot end the process.
void closeConnection(void* display) noexcept {
    XCloseDisplay(static_cast<Display*>(display));
}

}  // namespace

dvv::Result<ViewportWindow> nativeWindowOf(wxWindow& window) {
    auto* const widget = static_cast<GtkWidget*>(window.GetHandle());
    if (widget == nullptr) {
        return failure("the window has no GTK widget");
    }
    // The GdkWindow exists only once the widget is realized; the frame may not be mapped yet.
    gtk_widget_realize(widget);
    // The window's own drawing GdkWindow only: gtk_widget_get_window() may return the parent's
    // GdkWindow, and Vulkan would then present over the whole parent.
    GdkWindow* const gdk_window = window.GTKGetDrawingWindow();
    if (gdk_window == nullptr) {
        return failure("the GTK widget has no GdkWindow");
    }
    if (!GDK_IS_X11_WINDOW(gdk_window)) {
        return failure(std::string("the GdkWindow is a ") + G_OBJECT_TYPE_NAME(gdk_window) +
                       ", not an X11 window; rawreel-gui needs X11 or XWayland");
    }
    // Vulkan presents to this window's own XID: make it a native X window (its ancestors become
    // native too).
    if (gdk_window_ensure_native(gdk_window) == FALSE) {
        return failure("the GdkWindow could not be made native");
    }
    // The presenter uses its own connection, so the X server must have created the window (and
    // received every earlier request of GDK's connection) before that connection names it.
    GdkDisplay* const display = gdk_window_get_display(gdk_window);
    gdk_display_sync(display);
    Display* const own = XOpenDisplay(DisplayString(gdk_x11_display_get_xdisplay(display)));
    if (own == nullptr) {
        // The display name is left out: it can name a host, and users share this text.
        return failure("cannot open a second X connection to GTK's display for the presenter");
    }
    std::shared_ptr<void> connection(own, closeConnection);
    return ViewportWindow{
        .window =
            dvv::platform::XlibWindow{.display = own, .window = gdk_x11_window_get_xid(gdk_window)},
        .connection = std::move(connection)};
}

void syncNativeWindow(wxWindow& window) {
    // A flush alone would not order GDK's requests before the presenter's on its own connection.
    if (GdkWindow* const gdk_window = window.GTKGetDrawingWindow(); gdk_window != nullptr) {
        gdk_display_sync(gdk_window_get_display(gdk_window));
    }
}

}  // namespace rawreel
