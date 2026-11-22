#pragma once

// The exit codes of rawreel-cli (0-4) and rawreel-gui (0, 1, 4, 6, 77); README "Exit codes".
namespace rawreel::exit_code {
inline constexpr int SUCCESS = 0;
// cli: also a frame range outside the source; gui: also the GUI could not start, an unexpected
// exception.
inline constexpr int BAD_ARGUMENTS = 1;
inline constexpr int INPUT_ERROR = 2;      // cli
inline constexpr int UNSUPPORTED = 3;      // cli; also an ambiguous source
inline constexpr int GPU_ERROR = 4;        // no loader, no usable device, presenter failure
inline constexpr int SELFTEST_FAILED = 6;  // gui: a --selftest check failed (spec §10.2)
inline constexpr int SKIPPED = 77;         // gui --selftest only: no usable device or no display
}  // namespace rawreel::exit_code
