// rawreel-cli entry point on Windows (spec §7.9): wmain receives the arguments as UTF-16 and
// converts them to UTF-8 (D19); the console subsystem picks the wmain CRT entry itself.

#include "cli/cli_main.h"
#include "core/utf8_arguments_win32.h"

int wmain(int argc, wchar_t** argv) {
    return runCli([&] { return rawreel::utf8Arguments(argc, argv); });
}
