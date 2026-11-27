#pragma once

// rawreel-gui's command line (design M3 §4.5), wx-free so the unit tests and sanitizer builds use
// it. main() parses before wxEntry().

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/player.h"
#include "dvv/base/error.h"

namespace rawreel {

enum class Appearance : std::uint8_t { System, Light, Dark };
enum class Scenario : std::uint8_t { None, Smoke, Player, Lost, Perf, About, Inspector };
// "smoke", ...; "" for None.
[[nodiscard]] std::string_view scenarioName(Scenario scenario) noexcept;

struct GuiArguments {
    Appearance appearance = Appearance::System;
    std::optional<std::uint32_t> gpu;  // index into GpuInstance::devices() (dvv-gpu-info order)
    Scenario selftest = Scenario::None;
    std::uint32_t inflight = DEFAULT_INFLIGHT;  // --inflight K, 1..64, only with --selftest perf
    std::optional<std::string> uri;     // positional, at most one (opened as the stream, frame 0)
    std::optional<std::string> config;  // --config <file> (UTF-8): replaces the default file
    std::uint32_t debugSeekDelay = 0;   // --debug-seek-delay MS, 1..60000 (0: off): developer
    bool debugTransport = false;        // --debug-transport (a flag): developer
    bool help = false;
};
// Options take their value in the next argument, except the flag --debug-transport. Every error is
// Errc::Format with one of: "unknown argument '<a>'" (an unknown --option), "option --x needs a
// value", "option --x given twice", "bad value '<v>' for --appearance (system, light, dark)", "bad
// value '<v>' for --selftest (smoke, player, lost, perf, about)", "bad number '<v>' for --gpu",
// "bad number '<v>' for --inflight (1 to 64)", "bad number '<v>' for --debug-seek-delay (1 to
// 60000)",
// "--inflight is valid only with --selftest perf", "at most one URI".
// "--help" anywhere sets help and stops checking.
[[nodiscard]] dvv::Result<GuiArguments> parseGuiArguments(std::span<const std::string_view> args);
// 77, or 4 when DVV_REQUIRE_GPU=1 turns a skip into a failure (M1b D12).
[[nodiscard]] int skipExitCode();
// rawreel-gui's X11 start-up check (Linux, XWayland included): `display` is DISPLAY's value
// (nullopt when unset), `can_open` whether XOpenDisplay succeeded. nullopt when the GUI can start,
// else "no X11 display (DISPLAY is not set); rawreel-gui needs X11 or XWayland" (unset or empty)
// or "cannot open X display '<value>'; rawreel-gui needs X11 or XWayland".
[[nodiscard]] std::optional<std::string> x11DisplayProblem(
    const std::optional<std::string>& display, bool can_open);

// The --help text, and the text after a bad argument.
[[nodiscard]] std::string guiUsageText();

}  // namespace rawreel
