// rawreel-gui's entry point on Windows (spec §7.9): wmain receives the arguments as UTF-16 and
// converts them to UTF-8, then main.cpp's runGui() takes over.

// clang-format off
#include <wx/wx.h>  // first: wx's Windows headers before anything else includes <windows.h> (R11)
// clang-format on

#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include "core/exit_code.h"
#include "core/gui_arguments.h"
#include "core/utf8_arguments_win32.h"
#include "dvv/base/error.h"
#include "gui/gui_main.h"

// wmain with /ENTRY:wmainCRTStartup: a GUI-subsystem program with wide arguments (§5.6). Only
// argv[1..] is converted: runGui never reads the program's own path, so a name that is not valid
// Unicode there does not stop the start (a GUI-subsystem program's stderr is not seen).
int wmain(int argc, wchar_t** argv) {
    try {
        const int skipped = argc > 0 ? 1 : 0;
        const dvv::Result<std::vector<std::string>> utf8 =
            rawreel::utf8Arguments(argc - skipped, argv + skipped);
        if (!utf8) {
            std::fprintf(stderr, "rawreel-gui: %s\n", utf8.error().message.c_str());
            return rawreel::exit_code::BAD_ARGUMENTS;
        }
        const std::vector<std::string_view> args(utf8->begin(), utf8->end());
        return rawreel::runGui(args, [&argc, argv] { return wxEntry(argc, argv); });
    } catch (const std::exception& error) {
        std::fprintf(stderr, "rawreel-gui: %s\n", error.what());
        return rawreel::exit_code::BAD_ARGUMENTS;
    }
}
