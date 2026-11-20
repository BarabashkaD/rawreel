// rawreel-gui entry point (spec §10.2; design M1b §5.6). The arguments are parsed before wx
// starts. The App's exit code is kept in App::exitCode(), because wxEntry() returns -1 when
// OnInit() fails. The per-OS entry points are in main_win32.cpp (wmain) and main_posix.cpp (main).
// clang-format off
#include <wx/wx.h>  // first: wx's Windows headers before anything else includes <windows.h> (R11)
// clang-format on

#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "core/gui_arguments.h"
#include "core/vulkan_driver.h"
#include "dvv/base/leak_check.h"
#include "dvv/base/log.h"
#include "dvv/base/move_only_function.h"
#include "gui/app.h"
#include "gui/gui_main.h"
#include "gui/gui_platform.h"

wxIMPLEMENT_APP_NO_MAIN(rawreel::App);

// Declared in gui/gui_main.h; the per-OS entries call it.
int rawreel::runGui(std::span<const std::string_view> args,
                    dvv::MoveOnlyFunction<int()> start_wx) {
    // First: before anything can open the Vulkan loader.
    dvv::Result<dvv::gpu::DriverChoice> driver = rawreel::applyBundledDriver();
    dvv::Result<rawreel::GuiArguments> arguments = rawreel::parseGuiArguments(args);
    if (!arguments) {
        std::fprintf(stderr, "rawreel-gui: %s\n\n%s", arguments.error().message.c_str(),
                     rawreel::USAGE);
        return rawreel::exit_code::BAD_ARGUMENTS;
    }
    if (arguments->help) {
        std::fputs(rawreel::USAGE, stdout);
        return rawreel::exit_code::SUCCESS;
    }
    // Before wxEntry(), on Linux: XInitThreads(), GDK limited to X11, then the DISPLAY check.
    // Without a usable X display, GTK would fail inside wxEntry() with an unclear message.
    if (const std::optional<std::string> problem = rawreel::prepareGuiStartUp(); problem) {
        std::fprintf(stderr, "rawreel-gui: %s\n", problem->c_str());
        return arguments->selftest != rawreel::Scenario::None ? rawreel::skipExitCode()
                                                              : rawreel::exit_code::BAD_ARGUMENTS;
    }
    // At Info, the default level, until the App sets it to the active log level (log level
    // design §2.1).
    auto stderr_sink = std::make_shared<dvv::StderrSink>();
    dvv::Logger::instance().addSink(stderr_sink, dvv::LogLevel::Info);
    rawreel::App::setArguments(std::move(*arguments), std::move(stderr_sink), std::move(driver));
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
