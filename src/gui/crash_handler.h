#pragma once

// Windows minidump on an unhandled exception (design M4 v2 D22; spec §11). wx-free; compiled into
// rawreel_core. Other platforms: both functions do nothing; Linux has no crash handler in v1.0:
// a crash leaves a core dump where the system enables them.

#include <string>
#include <string_view>

namespace rawreel {

class CrashHandler {
public:
    // Call first thing in main()/App::OnInit(). Loads dbghelp.dll from System32
    // (LibrarySearch::SystemDirectory, kept loaded), starts a dump thread and installs the
    // unhandled-exception filter. On a crash the filter wakes the dump thread, which writes
    // <userDataDir("rawreel")>/crashes/<exe>-<yyyymmdd-hhmmss>-<pid>.dmp (MiniDumpNormal), prints
    // "rawreel: crash dump <path>" on stderr and logs an error line; the process then ends with the
    // exception code. The first crashing thread wins; later ones wait for the end. The state is
    // never destroyed, so a crash at exit is dumped too. `exe`: "rawreel-cli" or "rawreel-gui".
    // Returns "" or the "crash dumps are off: <reason>" warning, which the caller logs once its log
    // sinks exist (install runs before them).
    [[nodiscard]] static std::string install(std::string_view exe);
    // Test hook (design D22), RAWREEL_TEST_CRASH: "1" raises an access violation
    // (RaiseException(EXCEPTION_ACCESS_VIOLATION), not abort); "stack" overflows the stack;
    // "threads" raises an access violation on four threads at once; "exit" raises one in a static
    // destructor after main() returns. Otherwise returns.
    static void crashIfRequested();
};

}  // namespace rawreel
