#pragma once

// The shared part of rawreel-panel-lab's entry (private to the lab): the per-OS
// panel_lab_win32.cpp and panel_lab_posix.cpp hand panel_lab.cpp's run() the arguments as UTF-8.

#include <span>
#include <string_view>

#include "dvv/base/move_only_function.h"

namespace panel_lab {

// The lab's exit code for a failure.
inline constexpr int FAILED = 1;

// Parses `args` (the arguments after the program name), then runs wx through `start_wx`; returns
// the process exit code.
int run(std::span<const std::string_view> args, dvv::MoveOnlyFunction<int()> start_wx);

}  // namespace panel_lab
