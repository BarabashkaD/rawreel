#include "cli/arguments.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace rawreel::cli {

namespace {

enum class Option : std::uint8_t { Json, Out, Frame, Count, View, Via, Frames, Inflight };

struct OptionInfo {
    std::string_view name;
    Option option;
    bool takesValue;
};

constexpr std::array<OptionInfo, 8> OPTIONS{{
    {.name = "--json", .option = Option::Json, .takesValue = false},
    {.name = "--out", .option = Option::Out, .takesValue = true},
    {.name = "--frame", .option = Option::Frame, .takesValue = true},
    {.name = "--count", .option = Option::Count, .takesValue = true},
    {.name = "--view", .option = Option::View, .takesValue = true},
    {.name = "--via", .option = Option::Via, .takesValue = true},
    {.name = "--frames", .option = Option::Frames, .takesValue = true},
    {.name = "--inflight", .option = Option::Inflight, .takesValue = true},
}};

[[nodiscard]] std::string_view commandName(Command command) noexcept {
    switch (command) {
        case Command::Info: return "info";
        case Command::Dump: return "dump";
        case Command::Bench: return "bench";
        case Command::Version: return "--version";
        case Command::Help: return "--help";
    }
    return "?";
}

[[nodiscard]] bool allowed(Command command, Option option) noexcept {
    switch (command) {
        case Command::Info: return option == Option::Json;
        case Command::Dump:
            return option == Option::Json || option == Option::Out || option == Option::Frame ||
                   option == Option::Count || option == Option::View || option == Option::Via;
        case Command::Bench:
            return option == Option::Json || option == Option::Frames || option == Option::Inflight;
        case Command::Version: return option == Option::Json;
        case Command::Help: return false;
    }
    return false;
}

[[nodiscard]] dvv::Result<std::int64_t> parseNumber(std::string_view name, std::string_view text,
                                                    std::int64_t minimum) {
    std::int64_t value = 0;
    const char* const first = std::to_address(text.begin());
    const char* const end = std::to_address(text.end());
    const auto [last, error] = std::from_chars(first, end, value);
    if (text.empty() || error != std::errc{} || last != end) {
        return dvv::makeError(dvv::Errc::Format, std::format("bad number '{}' for {}", text, name));
    }
    if (value < minimum) {
        return dvv::makeError(
            dvv::Errc::Format,
            std::format("option {} must be at least {}, not {}", name, minimum, value));
    }
    return value;
}

// Applies one option and its value (empty for --json) to `arguments`.
[[nodiscard]] dvv::Result<void> apply(Arguments& arguments, const OptionInfo& info,
                                      std::string_view value) {
    switch (info.option) {
        case Option::Json: arguments.json = true; break;
        case Option::Out: arguments.out = value; break;
        case Option::Frame: {
            DVV_TRY(arguments.frame, parseNumber(info.name, value, 0));
            break;
        }
        case Option::Count: {
            DVV_TRY(arguments.count, parseNumber(info.name, value, 1));
            break;
        }
        case Option::Frames: {
            DVV_TRY(arguments.frames, parseNumber(info.name, value, 1));
            break;
        }
        case Option::Inflight: {
            DVV_TRY(arguments.inflight, parseNumber(info.name, value, 1));
            break;
        }
        case Option::View:
            if (value == "original") {
                arguments.view = View::Original;
            } else if (value == "planes") {
                arguments.view = View::Planes;
            } else {
                return dvv::makeError(
                    dvv::Errc::Format,
                    std::format("bad value '{}' for --view (original or planes)", value));
            }
            break;
        case Option::Via:
            if (value == "auto") {
                arguments.via = Via::Auto;
            } else if (value == "cpu") {
                arguments.via = Via::Cpu;
            } else if (value == "gpu") {
                arguments.via = Via::Gpu;
            } else {
                return dvv::makeError(
                    dvv::Errc::Format,
                    std::format("bad value '{}' for --via (cpu, gpu or auto)", value));
            }
            break;
    }
    return {};
}

}  // namespace

dvv::Result<Arguments> parseArguments(std::span<const std::string_view> args) {
    if (args.empty()) {
        return dvv::makeError(dvv::Errc::Format, "missing command");
    }
    Arguments arguments{};
    const std::string_view command = args.front();
    if (command == "info") {
        arguments.command = Command::Info;
    } else if (command == "dump") {
        arguments.command = Command::Dump;
    } else if (command == "bench") {
        arguments.command = Command::Bench;
    } else if (command == "--version") {
        arguments.command = Command::Version;
    } else if (command == "--help") {
        arguments.command = Command::Help;
    } else {
        return dvv::makeError(dvv::Errc::Format, std::format("unknown command '{}'", command));
    }
    const bool takes_uri = arguments.command == Command::Info ||
                           arguments.command == Command::Dump ||
                           arguments.command == Command::Bench;

    std::vector<Option> seen;
    bool have_uri = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (!arg.starts_with("--")) {
            if (!takes_uri || have_uri) {
                return dvv::makeError(dvv::Errc::Format,
                                      std::format("unexpected argument '{}'", arg));
            }
            arguments.uri = arg;
            have_uri = true;
            continue;
        }
        const auto info = std::ranges::find(OPTIONS, arg, &OptionInfo::name);
        if (info == OPTIONS.end()) {
            return dvv::makeError(dvv::Errc::Format, std::format("unknown option '{}'", arg));
        }
        if (!allowed(arguments.command, info->option)) {
            return dvv::makeError(dvv::Errc::Format,
                                  std::format("option {} is not valid for {}", info->name,
                                              commandName(arguments.command)));
        }
        if (std::ranges::contains(seen, info->option)) {
            return dvv::makeError(dvv::Errc::Format,
                                  std::format("option {} given twice", info->name));
        }
        seen.push_back(info->option);
        std::string_view value;
        if (info->takesValue) {
            if (i + 1 >= args.size()) {
                return dvv::makeError(dvv::Errc::Format,
                                      std::format("option {} needs a value", info->name));
            }
            value = args[++i];
        }
        DVV_TRY_VOID(apply(arguments, *info, value));
    }

    if (takes_uri && !have_uri) {
        return dvv::makeError(dvv::Errc::Format,
                              std::format("missing <uri> for {}", commandName(arguments.command)));
    }
    if (arguments.command == Command::Dump && !std::ranges::contains(seen, Option::Out)) {
        return dvv::makeError(dvv::Errc::Format, "dump needs --out <file>");
    }
    return arguments;
}

std::string usageText() {
    return R"(Usage:
  rawreel-cli info <uri> [--json]
  rawreel-cli dump <uri> --out <file> [--frame N] [--count K] [--view original|planes]
                   [--via cpu|gpu|auto] [--json]
  rawreel-cli bench <uri> [--frames N] [--inflight K] [--json]
  rawreel-cli --version [--json]
  rawreel-cli --help

  info      describe a source: format, size, frame rate, frame count, colour, warnings
  dump      write K frames from frame N to a file: the original bytes (--view original, the
            default) or the unpacked planes (--view planes); --via cpu reads on the host only,
            --via gpu through the GPU (upload, unpack, readback), --via auto (the default) uses
            the GPU when a usable device exists
  bench     read N frames (--frames, default 60) through produce, staging, upload and unpack on
            the GPU, K reads in flight (--inflight, default 4); prints frames/s and per-stage
            throughput
  --json    print the result, and errors, as JSON on stdout

A <uri> is a file path (clip_1920x1080_v210.yuv, clip.y4m), file:<path>?fmt=..&size=WxH, or
synthetic:<gradient|noise>?fmt=<format>&size=WxH[&frames=N][&seed=S].

Exit codes: 0 success; 1 bad arguments (also frames outside the source); 2 input error;
3 unsupported or ambiguous source; 4 GPU error (also no usable GPU device for --via gpu).
)";
}

}  // namespace rawreel::cli
