#pragma once

// The shared part of rawreel-cli's entry (private to the target): the per-OS main_win32.cpp and
// main_posix.cpp each hand it the program arguments as UTF-8 and return its exit code.

#include <string>
#include <vector>

#include "dvv/base/error.h"
#include "dvv/base/move_only_function.h"

// Sets up the driver, the logger and the crash handler, then gets the arguments (an error is a
// bad-arguments exit) and runs the command. No exception leaves it. The arguments include the
// program name.
int runCli(dvv::MoveOnlyFunction<dvv::Result<std::vector<std::string>>()> arguments) noexcept;
