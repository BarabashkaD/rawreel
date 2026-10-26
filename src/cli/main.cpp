// rawreel-cli entry point (spec §10.3, M2 design §4.6). On Windows wmain receives the arguments as
// UTF-16 and converts them to UTF-8 (D19); the console subsystem picks the wmain CRT entry itself.

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
#include "cli/commands.h"
#include "cli/output.h"
#include "cli/utf8_arguments.h"
#include "dvv/base/json.h"
#include "dvv/base/leak_check.h"
#include "dvv/base/lifetime.h"
#include "dvv/base/log.h"
#include "gui/crash_handler.h"

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

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
    try {
        // The crash handler first (design M4 v2 D22), then the logger, then the test hook.
        const std::string crash_warning = rawreel::CrashHandler::install("rawreel-cli");
        setUpLogging();
        if (!crash_warning.empty()) {
            DVV_LOG_WARN("{}", crash_warning);  // now that the sinks exist
        }
        rawreel::CrashHandler::crashIfRequested();
        auto all = rawreel::cli::utf8Arguments(argc, argv);
        if (!all) {
            std::println(stderr, "error: {}", all.error().message);
            return rawreel::cli::exit_code::BAD_ARGUMENTS;
        }
        return guarded(*all);
    } catch (...) {
        return lastResort();
    }
}
#else
int main(int argc, char** argv) {
    try {
        // The crash handler first (design M4 v2 D22), then the logger, then the test hook.
        const std::string crash_warning = rawreel::CrashHandler::install("rawreel-cli");
        setUpLogging();
        if (!crash_warning.empty()) {
            DVV_LOG_WARN("{}", crash_warning);  // now that the sinks exist
        }
        rawreel::CrashHandler::crashIfRequested();
        return guarded(std::vector<std::string>(argv, argv + argc));
    } catch (...) {
        return lastResort();
    }
}
#endif
