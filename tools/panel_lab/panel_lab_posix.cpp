// rawreel-panel-lab's entry point on macOS and Linux (spec §7.9): the arguments are already UTF-8;
// panel_lab.cpp's run() takes over.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <cstdio>
#include <exception>
#include <span>
#include <string_view>
#include <vector>

#include "dvv/base/move_only_function.h"

// panel_lab.cpp: parses `args`, then runs wx through `start_wx`; returns the process exit code.
int run(std::span<const std::string_view> args, dvv::MoveOnlyFunction<int()> start_wx);

namespace {

constexpr int FAILED = 1;  // panel_lab.cpp's exit code for a failure

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::vector<std::string_view> args(argv + 1, argv + argc);
        return run(args, [&argc, argv] { return wxEntry(argc, argv); });
    } catch (const std::exception& error) {
        std::fprintf(stderr, "rawreel-panel-lab: %s\n", error.what());
        return FAILED;
    }
}
