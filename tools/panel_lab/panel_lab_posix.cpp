// rawreel-panel-lab's entry point on macOS and Linux (spec §7.9): the arguments are already UTF-8;
// panel_lab.cpp's run() takes over.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <cstdio>
#include <exception>
#include <string_view>
#include <vector>

#include "panel_lab_main.h"

int main(int argc, char** argv) {
    try {
        const std::vector<std::string_view> args(argv + 1, argv + argc);
        return panel_lab::run(args, [&argc, argv] { return wxEntry(argc, argv); });
    } catch (const std::exception& error) {
        std::fprintf(stderr, "rawreel-panel-lab: %s\n", error.what());
        return panel_lab::FAILED;
    }
}
