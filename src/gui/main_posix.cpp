// rawreel-gui's entry point on Linux and macOS: the arguments are UTF-8 already. runGui() parses
// them; on Linux it then does the X11 start-up (XInitThreads, GDK limited to X11, the DISPLAY
// check) after --help and before wxEntry(). macOS has no such step: Cocoa needs no display
// variable, and the files Finder or the Dock opens come through App::MacOpenFiles(), not argv.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <cstdio>
#include <exception>
#include <string_view>
#include <vector>

#include "core/gui_arguments.h"
#include "gui/gui_main.h"

int main(int argc, char** argv) {
    try {
        const std::vector<std::string_view> args(argv + 1, argv + argc);
        return rawreel::runGui(args, [&argc, argv] { return wxEntry(argc, argv); });
    } catch (const std::exception& error) {
        std::fprintf(stderr, "rawreel-gui: %s\n", error.what());
        return rawreel::exit_code::BAD_ARGUMENTS;
    }
}
