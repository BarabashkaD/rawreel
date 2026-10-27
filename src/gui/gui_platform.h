#pragma once

// rawreel-gui's Linux start-up: X11 only, so XWayland on Wayland desktops (native
// Wayland is v1.x). wx-free; implemented in gui_platform_gtk.cpp, not built on Windows.

#include <optional>
#include <string>

namespace rawreel {

// Before wxEntry(), on the main thread, before any other X11 or GTK call: XInitThreads()
// (dvv-platform's XlibWindow needs it), restricts GDK to its x11 backend
// (gdk_set_allowed_backends; a GDK_BACKEND value the user set is replaced by "x11"), then checks
// DISPLAY and probes it with XOpenDisplay()/XCloseDisplay(). Returns x11DisplayProblem()'s text,
// nullopt when the GUI can start.
[[nodiscard]] std::optional<std::string> prepareX11StartUp();

}  // namespace rawreel
