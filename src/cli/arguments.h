#pragma once

// rawreel-cli's command line (spec §10.3, M2 design §4.6).

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "dvv/base/error.h"

namespace rawreel::cli {

enum class Command : std::uint8_t { Info, Dump, Bench, Version, Help };
enum class View : std::uint8_t { Original, Planes };
enum class Via : std::uint8_t { Auto, Cpu, Gpu };

struct Arguments {
    Command command;
    std::string uri, out;
    std::int64_t frame = 0, count = 1, frames = 60, inflight = 4;
    View view = View::Original;
    Via via = Via::Auto;
    bool json = false;
    bool readOnly = false;              // bench --read-only: produce only (the file read stage)
    std::optional<std::string> config;  // --config <file> (UTF-8), info, dump and bench
};

// `args` excludes the program name. The first argument is the command (info, dump, bench,
// --version, --help); info, dump and bench take exactly one URI and their own options, each at most
// once, with the value in the next argument. --version accepts only --json; --help takes nothing.
// Every error is Errc::Format with a message for people.
[[nodiscard]] dvv::Result<Arguments> parseArguments(std::span<const std::string_view> args);

// The --help text, ending with a newline.
[[nodiscard]] std::string usageText();

}  // namespace rawreel::cli
