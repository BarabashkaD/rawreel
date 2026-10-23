#include "arguments.h"

#include <format>

#include "dvv/base/environment.h"

namespace rawreel {

dvv::Result<Arguments> parseArguments(std::span<const std::string_view> args) {
    Arguments arguments;
    for (const std::string_view arg : args) {
        if (arg == "--selftest") {
            arguments.selftest = true;
        } else if (arg == "--help") {
            arguments.help = true;
        } else {
            return dvv::makeError(dvv::Errc::Format, std::format("unknown argument '{}'", arg));
        }
    }
    return arguments;
}

int skipExitCode() {
    return dvv::getEnvironment("DVV_REQUIRE_GPU") == "1" ? exit_code::GPU_ERROR
                                                         : exit_code::SKIPPED;
}

}  // namespace rawreel
