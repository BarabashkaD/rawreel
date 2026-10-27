#include "core/gui_arguments.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "dvv/base/environment.h"

namespace rawreel {

namespace {

constexpr std::array<std::pair<std::string_view, Scenario>, 4> SCENARIOS{{
    {"smoke", Scenario::Smoke},
    {"player", Scenario::Player},
    {"lost", Scenario::Lost},
    {"perf", Scenario::Perf},
}};

constexpr std::uint32_t MAX_INFLIGHT = 64;

// A decimal number without sign, spaces or trailing characters that fits in 32 bits.
std::optional<std::uint32_t> parseNumber(std::string_view text) {
    std::uint32_t value = 0;
    const char* const first = std::to_address(text.begin());
    const char* const end = std::to_address(text.end());
    const auto [ptr, ec] = std::from_chars(first, end, value);
    if (text.empty() || ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::optional<Appearance> parseAppearance(std::string_view text) {
    if (text == "system") {
        return Appearance::System;
    }
    if (text == "light") {
        return Appearance::Light;
    }
    if (text == "dark") {
        return Appearance::Dark;
    }
    return std::nullopt;
}

std::optional<Scenario> parseScenario(std::string_view text) {
    for (const auto& [name, scenario] : SCENARIOS) {
        if (name == text) {
            return scenario;
        }
    }
    return std::nullopt;
}

}  // namespace

std::string_view scenarioName(Scenario scenario) noexcept {
    for (const auto& [name, candidate] : SCENARIOS) {
        if (candidate == scenario) {
            return name;
        }
    }
    return {};
}

dvv::Result<GuiArguments> parseGuiArguments(std::span<const std::string_view> args) {
    GuiArguments arguments;
    if (std::ranges::find(args, std::string_view("--help")) != args.end()) {
        arguments.help = true;
        return arguments;
    }
    bool appearance_seen = false;
    bool gpu_seen = false;
    bool selftest_seen = false;
    bool inflight_seen = false;
    bool config_seen = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (!arg.starts_with("--")) {
            if (arguments.uri) {
                return dvv::makeError(dvv::Errc::Format, "at most one URI");
            }
            arguments.uri = std::string(arg);
            continue;
        }
        bool* seen = nullptr;
        if (arg == "--appearance") {
            seen = &appearance_seen;
        } else if (arg == "--gpu") {
            seen = &gpu_seen;
        } else if (arg == "--selftest") {
            seen = &selftest_seen;
        } else if (arg == "--inflight") {
            seen = &inflight_seen;
        } else if (arg == "--config") {
            seen = &config_seen;
        } else {
            return dvv::makeError(dvv::Errc::Format, std::format("unknown argument '{}'", arg));
        }
        if (*seen) {
            return dvv::makeError(dvv::Errc::Format, std::format("option {} given twice", arg));
        }
        if (i + 1 >= args.size()) {
            return dvv::makeError(dvv::Errc::Format, std::format("option {} needs a value", arg));
        }
        *seen = true;
        const std::string_view value = args[++i];
        if (arg == "--appearance") {
            const std::optional<Appearance> appearance = parseAppearance(value);
            if (!appearance) {
                return dvv::makeError(
                    dvv::Errc::Format,
                    std::format("bad value '{}' for --appearance (system, light, dark)", value));
            }
            arguments.appearance = *appearance;
        } else if (arg == "--gpu") {
            const std::optional<std::uint32_t> gpu = parseNumber(value);
            if (!gpu) {
                return dvv::makeError(dvv::Errc::Format,
                                      std::format("bad number '{}' for --gpu", value));
            }
            arguments.gpu = gpu;
        } else if (arg == "--config") {
            arguments.config = std::string(value);
        } else if (arg == "--selftest") {
            const std::optional<Scenario> scenario = parseScenario(value);
            if (!scenario) {
                return dvv::makeError(
                    dvv::Errc::Format,
                    std::format("bad value '{}' for --selftest (smoke, player, lost, perf)",
                                value));
            }
            arguments.selftest = *scenario;
        } else {
            const std::optional<std::uint32_t> inflight = parseNumber(value);
            if (!inflight || *inflight < 1 || *inflight > MAX_INFLIGHT) {
                return dvv::makeError(
                    dvv::Errc::Format,
                    std::format("bad number '{}' for --inflight (1 to {})", value, MAX_INFLIGHT));
            }
            arguments.inflight = *inflight;
        }
    }
    if (inflight_seen && arguments.selftest != Scenario::Perf) {
        return dvv::makeError(dvv::Errc::Format, "--inflight is valid only with --selftest perf");
    }
    return arguments;
}

int skipExitCode() {
    return dvv::getEnvironment("DVV_REQUIRE_GPU") == "1" ? exit_code::GPU_ERROR
                                                         : exit_code::SKIPPED;
}

std::optional<std::string> x11DisplayProblem(const std::optional<std::string>& display,
                                             bool can_open) {
    if (!display || display->empty()) {
        return std::string(
            "no X11 display (DISPLAY is not set); rawreel-gui needs X11 or XWayland");
    }
    if (!can_open) {
        return std::format("cannot open X display '{}'; rawreel-gui needs X11 or XWayland",
                           *display);
    }
    return std::nullopt;
}

}  // namespace rawreel
