// rawreel-gui's Linux start-up (see gui_platform.h).
#include <cstdlib>
#include <optional>
#include <string>

#include "core/gui_arguments.h"
#include "gui/gui_platform.h"

// Last: Xlib's macros (None, Bool, Status, True, False) stay out of the headers above.
#include <X11/Xlib.h>
#include <gdk/gdk.h>

namespace rawreel {

namespace {

std::optional<std::string> environment(const char* name) {
    const char* const value = std::getenv(name);  // NOLINT(concurrency-mt-unsafe): one thread
    return value != nullptr ? std::optional<std::string>(value) : std::nullopt;
}

}  // namespace

std::optional<std::string> prepareX11StartUp() {
    ::XInitThreads();  // the first Xlib call of the process
    // GDK opens only X11 (XWayland on a Wayland desktop) without touching the environment, so the
    // programs rawreel launches keep the user's settings. A GDK_BACKEND the user set takes
    // precedence over the allowed list and could exclude x11, so only a set value is replaced, here
    // while the process has one thread; it is not given back later, because GLib's threads run by
    // then (the measured case: 4 threads in App::OnInit).
    ::gdk_set_allowed_backends("x11");
    if (environment("GDK_BACKEND")) {
        ::setenv("GDK_BACKEND", "x11", 1);  // NOLINT(concurrency-mt-unsafe): one thread
    }
    const std::optional<std::string> display = environment("DISPLAY");
    bool can_open = false;
    if (display && !display->empty()) {
        if (Display* const probe = ::XOpenDisplay(nullptr); probe != nullptr) {
            ::XCloseDisplay(probe);
            can_open = true;
        }
    }
    return x11DisplayProblem(display, can_open);
}

}  // namespace rawreel
