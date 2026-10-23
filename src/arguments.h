#pragma once

// rawreel-gui's command line and exit codes (spec §10.2, design M1b D12).

#include <span>
#include <string_view>

#include "dvv/base/error.h"

namespace rawreel {

namespace exit_code {
inline constexpr int SUCCESS = 0;
inline constexpr int BAD_ARGUMENTS = 1;    // also: the GUI could not start, an unexpected exception
inline constexpr int GPU_ERROR = 4;        // no loader, no usable device, presenter failure
inline constexpr int SELFTEST_FAILED = 6;  // a --selftest check failed (spec §10.2)
inline constexpr int SKIPPED = 77;         // --selftest only: no usable device or no display
}  // namespace exit_code

struct Arguments {
    bool selftest = false;
    bool help = false;
};

// "--selftest", "--help" (each at most once is not checked; repeats are harmless); anything else
// is Errc::Format "unknown argument '<arg>'".
[[nodiscard]] dvv::Result<Arguments> parseArguments(std::span<const std::string_view> args);

// 77, or 4 when DVV_REQUIRE_GPU=1 turns a skip into a failure (design D12).
[[nodiscard]] int skipExitCode();

// The --help text, ending with a newline.
inline constexpr char USAGE[] = R"(Usage: rawreel-gui [--selftest] [--help]

  --selftest  open the window, present, resize, minimise and restore it, close it, and exit
  --help      print this text

Exit codes: 0 success; 1 bad arguments, or the GUI could not start; 4 GPU error (no Vulkan
loader, no usable device, presenter failure); 6 a --selftest check failed; 77 (--selftest only)
no usable device or no display, unless DVV_REQUIRE_GPU=1 (then 4).
)";

}  // namespace rawreel
