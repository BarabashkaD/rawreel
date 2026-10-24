#pragma once

// rawreel-cli's commands (spec §10.3, M2 design §4.6, §5.7).

#include "cli/arguments.h"

namespace rawreel::cli {

// Runs a parsed command, prints its results (stdout) and errors (stderr, or stdout as JSON with
// --json) and returns the exit code. Every object it creates is destroyed before it returns, in
// the shutdown order of design §5.7, so the caller can run the leak check.
[[nodiscard]] int run(const Arguments& arguments);

}  // namespace rawreel::cli
