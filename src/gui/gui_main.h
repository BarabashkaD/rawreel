#pragma once

// The shared part of rawreel-gui's entry (private to rawreel-gui): the per-OS main_win32.cpp and
// main_posix.cpp hand main.cpp's runGui() the arguments as UTF-8.

#include <span>
#include <string_view>

#include "dvv/base/move_only_function.h"

namespace rawreel {

// Parses `args` (the arguments after the program name), then runs wx through `start_wx`; returns
// the process exit code.
int runGui(std::span<const std::string_view> args, dvv::MoveOnlyFunction<int()> start_wx);

}  // namespace rawreel
