// rawreel-cli: the shared run of both entries (spec §7.9, M2 design §4.6); main_win32.cpp and
// main_posix.cpp hold the entry points.

#include <algorithm>
#include <cstdio>
#include <exception>
#include <iterator>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include "cli/arguments.h"
#include "cli/cli_main.h"
#include "cli/commands.h"
#include "cli/output.h"
#include "core/vulkan_driver.h"
#include "dvv/base/json.h"
#include "dvv/base/leak_check.h"
#include "dvv/base/lifetime.h"
#include "dvv/base/log.h"
#include "dvv/platform/crash_handler.h"

namespace {

void setUpLogging() {
    dvv::Logger::instance().setLevel(dvv::LogLevel::Warn);
    dvv::Logger::instance().addSink(std::make_shared<dvv::StderrSink>());
#if !defined(NDEBUG)
    dvv::LifetimeRegistry::instance().setDetailed(true);
#endif
}

// `all` includes the program name.
int runWith(const std::vector<std::string>& all) {
    const std::vector<std::string_view> args(std::next(all.begin(), all.empty() ? 0 : 1),
                                             all.end());
    const auto parsed = rawreel::cli::parseArguments(args);
    if (!parsed) {
        if (std::ranges::contains(args, std::string_view("--json"))) {
            std::println(
                "{}",
                dvv::json::dump(rawreel::cli::errorJson("Arguments", parsed.error().message), 2));
        } else {
            std::print(stderr, "error: {}\n\n{}", parsed.error().message,
                       rawreel::cli::usageText());
        }
        return rawreel::cli::exit_code::BAD_ARGUMENTS;
    }
    return rawreel::cli::run(*parsed);
}

// Runs the command and returns the exit code after the leak check; every object of the run is
// destroyed inside run().
int guarded(const std::vector<std::string>& all) {
    int code = rawreel::cli::exit_code::INPUT_ERROR;
    try {
        code = runWith(all);
    } catch (const std::exception& error) {
        std::println(stderr, "internal error: {}", error.what());
    } catch (...) {
        std::println(stderr, "internal error: unknown exception");
    }
    return dvv::finishWithLeakCheck(code);
}

// An exception outside guarded()'s handlers (setting up the logger, converting the arguments,
// printing an internal error) ends the process with exit 2.
int lastResort() noexcept {
    std::fputs("internal error: unexpected exception\n", stderr);
    return rawreel::cli::exit_code::INPUT_ERROR;
}

}  // namespace

int runCli(dvv::MoveOnlyFunction<dvv::Result<std::vector<std::string>>()> arguments) noexcept {
    // First: before anything can open the Vulkan loader.
    const dvv::Result<dvv::gpu::DriverChoice> driver = rawreel::applyBundledDriver();
    try {
        // The logger, then the crash handler (it logs its own warning), then the test hook.
        setUpLogging();
        dvv::platform::installCrashHandler({.app = "rawreel", .process = "rawreel-cli"});
        dvv::platform::crashIfRequested("RAWREEL_TEST_CRASH");
        if (!driver) {
            DVV_LOG_WARN("the bundled Vulkan driver is not used: {}", driver.error().message);
        }
        const auto all = arguments();
        if (!all) {
            std::println(stderr, "error: {}", all.error().message);
            return rawreel::cli::exit_code::BAD_ARGUMENTS;
        }
        return guarded(*all);
    } catch (...) {
        return lastResort();
    }
}
