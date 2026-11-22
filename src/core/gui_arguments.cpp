#include "core/gui_arguments.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <utility>

#include "core/exit_code.h"
#include "core/native_os.h"
#include "core/number_text.h"
#include "dvv/base/environment.h"

namespace rawreel {

namespace {

constexpr std::array<std::pair<std::string_view, Scenario>, 5> SCENARIOS{{
    {"smoke", Scenario::Smoke},
    {"player", Scenario::Player},
    {"lost", Scenario::Lost},
    {"perf", Scenario::Perf},
    {"about", Scenario::About},
}};

constexpr std::uint32_t MAX_INFLIGHT = 64;
constexpr std::uint32_t MAX_DEBUG_SEEK_DELAY = 60000;

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
    bool delay_seen = false;
    bool transport_seen = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        // A process serial number that LaunchServices may pass on macOS; not a URI.
        if (NATIVE_OS == Os::MacOs && arg.starts_with("-psn_")) {
            continue;
        }
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
        } else if (arg == "--debug-seek-delay") {
            seen = &delay_seen;
        } else if (arg == "--debug-transport") {
            seen = &transport_seen;
        } else {
            return dvv::makeError(dvv::Errc::Format, std::format("unknown argument '{}'", arg));
        }
        if (*seen) {
            return dvv::makeError(dvv::Errc::Format, std::format("option {} given twice", arg));
        }
        if (arg == "--debug-transport") {  // the only flag
            *seen = true;
            arguments.debugTransport = true;
            continue;
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
            const std::optional<std::uint32_t> gpu = parseDecimal<std::uint32_t>(value);
            if (!gpu) {
                return dvv::makeError(dvv::Errc::Format,
                                      std::format("bad number '{}' for --gpu", value));
            }
            arguments.gpu = gpu;
        } else if (arg == "--debug-seek-delay") {
            const std::optional<std::uint32_t> delay = parseDecimal<std::uint32_t>(value);
            if (!delay || *delay < 1 || *delay > MAX_DEBUG_SEEK_DELAY) {
                return dvv::makeError(
                    dvv::Errc::Format,
                    std::format("bad number '{}' for --debug-seek-delay (1 to {})", value,
                                MAX_DEBUG_SEEK_DELAY));
            }
            arguments.debugSeekDelay = *delay;
        } else if (arg == "--config") {
            arguments.config = std::string(value);
        } else if (arg == "--selftest") {
            const std::optional<Scenario> scenario = parseScenario(value);
            if (!scenario) {
                return dvv::makeError(
                    dvv::Errc::Format,
                    std::format("bad value '{}' for --selftest (smoke, player, lost, perf, about)",
                                value));
            }
            arguments.selftest = *scenario;
        } else {
            const std::optional<std::uint32_t> inflight = parseDecimal<std::uint32_t>(value);
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

std::string guiUsageText() {
    return std::format(
        R"(Usage: rawreel-gui [--appearance system|light|dark] [--gpu N] [--selftest SCENARIO]
                   [--inflight K] [--config FILE] [--debug-seek-delay MS]
                   [--debug-transport] [URI]

  --appearance  light or dark windows, or the system setting (default)
  --gpu N       use GPU N (the index dvv-gpu-info lists) instead of the best one
  --selftest S  run a scripted check and exit: smoke, player, lost, perf or about
  --inflight K  frames in flight for --selftest perf (default {})
  --config F    read config file F instead of the default rawreel.json
  --debug-seek-delay MS  developer: every frame takes MS ms longer to load
  --debug-transport  developer: log the transport bar's notifications
  --help        print this text
  URI           a source to open (a path, file:..., synthetic:...)

Exit codes: 0 success; 1 bad arguments, or the GUI could not start; 4 GPU error (no Vulkan
loader, no usable device, presenter failure); 6 a --selftest check failed; 77 (--selftest only)
no usable device or no display, unless DVV_REQUIRE_GPU=1 (then 4).
)",
        DEFAULT_INFLIGHT);
}

}  // namespace rawreel
