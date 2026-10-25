#pragma once

// rawreel-gui's command line (design M3 §4.5), wx-free so the unit tests and sanitizer builds use
// it. main() parses before wxEntry().

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dvv/base/error.h"

namespace rawreel {

namespace exit_code {
inline constexpr int SUCCESS = 0;
inline constexpr int BAD_ARGUMENTS = 1;    // also: the GUI could not start, an unexpected exception
inline constexpr int GPU_ERROR = 4;        // no loader, no usable device, presenter failure
inline constexpr int SELFTEST_FAILED = 6;  // a --selftest check failed (spec §10.2)
inline constexpr int SKIPPED = 77;         // --selftest only: no usable device or no display
}  // namespace exit_code

enum class Appearance : std::uint8_t { System, Light, Dark };
enum class Scenario : std::uint8_t { None, Smoke, Player, Lost, Perf };
// "smoke", ...; "" for None.
[[nodiscard]] std::string_view scenarioName(Scenario scenario) noexcept;

struct GuiArguments {
    Appearance appearance = Appearance::System;
    std::optional<std::uint32_t> gpu;  // index into GpuRegistry::devices() (dvv-gpu-info order)
    Scenario selftest = Scenario::None;
    std::uint32_t inflight = 4;         // --inflight K, 1..64, only with --selftest perf
    std::optional<std::string> uri;     // positional, at most one (opened as the stream, frame 0)
    std::optional<std::string> config;  // --config <file> (UTF-8): replaces rawreel.json by the exe
    bool help = false;
};
// Options take their value in the next argument. Every error is Errc::Format with one of:
// "unknown argument '<a>'" (an unknown --option), "option --x needs a value", "option --x given
// twice", "bad value '<v>' for --appearance (system, light, dark)", "bad value '<v>' for --selftest
// (smoke, player, lost, perf)", "bad number '<v>' for --gpu", "bad number '<v>' for
// --inflight (1 to 64)", "--inflight is valid only with --selftest perf", "at most one URI".
// "--help" anywhere sets help and stops checking.
[[nodiscard]] dvv::Result<GuiArguments> parseGuiArguments(std::span<const std::string_view> args);
// 77, or 4 when DVV_REQUIRE_GPU=1 turns a skip into a failure (M1b D12).
[[nodiscard]] int skipExitCode();

inline constexpr char USAGE[] =
    R"(Usage: rawreel-gui [--appearance system|light|dark] [--gpu N] [--selftest SCENARIO]
                   [--inflight K] [--config FILE] [URI]

  --appearance  light or dark windows, or the system setting (default)
  --gpu N       use GPU N (the index dvv-gpu-info lists) instead of the best one
  --selftest S  run a scripted check and exit: smoke, player, lost or perf
  --inflight K  frames in flight for --selftest perf (default 4)
  --config F    read config file F instead of rawreel.json next to rawreel-gui
  --help        print this text
  URI           a source to open (a path, file:..., synthetic:...)

Exit codes: 0 success; 1 bad arguments, or the GUI could not start; 4 GPU error (no Vulkan
loader, no usable device, presenter failure); 6 a --selftest check failed; 77 (--selftest only)
no usable device or no display, unless DVV_REQUIRE_GPU=1 (then 4).
)";

}  // namespace rawreel
