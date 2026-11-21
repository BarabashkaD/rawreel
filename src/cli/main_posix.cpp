// rawreel-cli entry point on macOS and Linux (spec §7.9): the arguments are already UTF-8.

#include <string>
#include <vector>

#include "cli/cli_main.h"

int main(int argc, char** argv) {
    return runCli([&] {
        return dvv::Result<std::vector<std::string>>(std::vector<std::string>(argv, argv + argc));
    });
}
