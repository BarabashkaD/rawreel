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

#include "core/gui_arguments.h"
#include "core/utf8_arguments_win32.h"
#include "dvv/base/error.h"
#include "gui/gui_main.h"

// wmain with /ENTRY:wmainCRTStartup: a GUI-subsystem program with wide arguments (§5.6).
int wmain(int argc, wchar_t** argv) {
    try {
        const dvv::Result<std::vector<std::string>> utf8 = rawreel::utf8Arguments(argc, argv);
        if (!utf8) {
            std::fprintf(stderr, "rawreel-gui: %s\n", utf8.error().message.c_str());
            return rawreel::exit_code::BAD_ARGUMENTS;
        }
        const std::vector<std::string_view> args(utf8->begin() + 1, utf8->end());
        return rawreel::runGui(args, [&argc, argv] { return wxEntry(argc, argv); });
    } catch (const std::exception& error) {
        std::fprintf(stderr, "rawreel-gui: %s\n", error.what());
        return rawreel::exit_code::BAD_ARGUMENTS;
    }
}
