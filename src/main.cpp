// rawreel-gui entry point (spec §10.2; design M1b §5.6). The arguments are parsed before wx
// starts. The App's exit code is kept in App::exitCode(), because wxEntry() returns -1 when
// OnInit() fails.
// clang-format off
#include <wx/wx.h>  // first: wx's Windows headers before anything else includes <windows.h> (R11)
// clang-format on

#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app.h"
#include "arguments.h"
#include "dvv/base/leak_check.h"
#include "dvv/base/log.h"

wxIMPLEMENT_APP_NO_MAIN(rawreel::App);

namespace {

// Parses, then runs wx through `start_wx`; returns the process exit code.
template <class StartWx>
int run(std::span<const std::string_view> args, StartWx start_wx) {
    const dvv::Result<rawreel::Arguments> arguments = rawreel::parseArguments(args);
    if (!arguments) {
        std::fprintf(stderr, "rawreel-gui: %s\n\n%s", arguments.error().message.c_str(),
                     rawreel::USAGE);
        return rawreel::exit_code::BAD_ARGUMENTS;
    }
    if (arguments->help) {
        std::fputs(rawreel::USAGE, stdout);
        return rawreel::exit_code::SUCCESS;
    }
    dvv::Logger::instance().addSink(std::make_shared<dvv::StderrSink>());
    rawreel::App::setArguments(*arguments);
    const int wx_result = start_wx();
    int code = rawreel::exit_code::SUCCESS;
    if (const std::optional<int> decided = rawreel::App::exitCode(); decided) {
        code = *decided;
    } else if (wx_result != 0) {
        std::fputs("rawreel-gui: the GUI could not start\n", stderr);
        code = rawreel::exit_code::BAD_ARGUMENTS;
    }
    return dvv::finishWithLeakCheck(code);
}

}  // namespace

#if defined(_WIN32)
// wmain with /ENTRY:wmainCRTStartup: a GUI-subsystem program with wide arguments (§5.6).
int wmain(int argc, wchar_t** argv) {
    try {
        std::vector<std::string> utf8;
        for (int i = 1; i < argc; ++i) {
            utf8.push_back(wxString(argv[i]).utf8_string());
        }
        const std::vector<std::string_view> args(utf8.begin(), utf8.end());
        return run(args, [&argc, argv] { return wxEntry(argc, argv); });
    } catch (const std::exception& error) {
        std::fprintf(stderr, "rawreel-gui: %s\n", error.what());
        return rawreel::exit_code::BAD_ARGUMENTS;
    }
}
#else
// Other platforms: the arguments are UTF-8 already. Compiled, not run in v1.0; the Linux start-up
// (XInitThreads, GDK_BACKEND=x11, the DISPLAY check) comes with Linux support.
int main(int argc, char** argv) {
    try {
        const std::vector<std::string_view> args(argv + 1, argv + argc);
        return run(args, [&argc, argv] { return wxEntry(argc, argv); });
    } catch (const std::exception& error) {
        std::fprintf(stderr, "rawreel-gui: %s\n", error.what());
        return rawreel::exit_code::BAD_ARGUMENTS;
    }
}
#endif
