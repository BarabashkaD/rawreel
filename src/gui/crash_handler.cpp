#include "gui/crash_handler.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// clang-format off
#include <dbghelp.h>  // after windows.h
// clang-format on

#include <atomic>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include "dvv/base/log.h"
#include "dvv/base/paths.h"
#include "dvv/platform/dynamic_library.h"

namespace rawreel {
namespace {

using WriteDump = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
                                PMINIDUMP_CALLBACK_INFORMATION);

constexpr std::size_t kFolderCapacity = 1024;  // a longer crash folder path: crash dumps are off
constexpr std::size_t kPathCapacity = kFolderCapacity + 128;
// How long the crashing thread waits for the dump thread before it ends the process anyway.
constexpr DWORD kDumpTimeoutMs = 120'000;

// Process infrastructure, set once by install(). Trivially destructible at namespace scope, so it
// is never destroyed: a crash in a static destructor or at DLL detach still finds valid paths. The
// dump buffers live here, not on a stack, because the crashing thread may have no stack left.
struct CrashState {
    wchar_t base[kFolderCapacity];    // <userDataDir("rawreel")>
    wchar_t folder[kFolderCapacity];  // <base>\crashes
    wchar_t exe[64];                  // L"rawreel-cli"
    WriteDump writeDump;
    HANDLE request;            // auto-reset event: the filter wakes the dump thread
    HANDLE done;               // manual-reset event: the dump thread has finished
    HANDLE thread;             // the dump thread, created by install() (it has a stack of its own)
    DWORD dumpThreadId;        // its id
    std::atomic<DWORD> owner;  // the first crashing thread (0: none yet); later ones wait
    EXCEPTION_POINTERS* pointers;  // of the owner, for the dump thread
    DWORD faultingThreadId;
    wchar_t path[kPathCapacity];
    char utf8[4 * kPathCapacity];
    char line[4 * kPathCapacity + 32];
};
static_assert(std::is_trivially_destructible_v<CrashState>);
constinit CrashState state{};

// Writes the dump of the owner's exception, then "rawreel: crash dump <path>" on stderr and an
// error log line. Runs on the dump thread (or, when that is gone, on the crashing thread).
void writeCrashDump() {
    CrashState& s = state;
    // The folder only now, so a run without a crash creates nothing; ERROR_ALREADY_EXISTS is fine.
    CreateDirectoryW(s.base, nullptr);
    CreateDirectoryW(s.folder, nullptr);
    SYSTEMTIME t{};
    GetLocalTime(&t);
    // WORD fields promote to int: cast each to unsigned for %u (MSVC C4477 under /W4 /WX).
    const int length = swprintf(
        s.path, std::size(s.path), L"%ls\\%ls-%04u%02u%02u-%02u%02u%02u-%lu.dmp", s.folder, s.exe,
        static_cast<unsigned>(t.wYear), static_cast<unsigned>(t.wMonth),
        static_cast<unsigned>(t.wDay), static_cast<unsigned>(t.wHour),
        static_cast<unsigned>(t.wMinute), static_cast<unsigned>(t.wSecond), GetCurrentProcessId());
    if (length <= 0) {
        return;
    }
    const HANDLE file =
        CreateFileW(s.path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    // The faulting thread's id, not the dump thread's: the dump shows the crash as the exception.
    MINIDUMP_EXCEPTION_INFORMATION exception{s.faultingThreadId, s.pointers, FALSE};
    const BOOL written = s.writeDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                                     MiniDumpNormal, &exception, nullptr, nullptr);
    CloseHandle(file);
    if (!written || WideCharToMultiByte(CP_UTF8, 0, s.path, -1, s.utf8,
                                        static_cast<int>(sizeof s.utf8), nullptr, nullptr) <= 0) {
        return;
    }
    const int n = std::snprintf(s.line, sizeof s.line, "rawreel: crash dump %s\n", s.utf8);
    DWORD out = 0;
    if (n > 0) {
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), s.line, static_cast<DWORD>(n), &out, nullptr);
    }
    try {  // after the dump is on disk: the log line may allocate or throw
        DVV_LOG_ERROR("crash: exception 0x{:08X}, dump {}",
                      static_cast<unsigned>(s.pointers->ExceptionRecord->ExceptionCode), s.utf8);
        dvv::Logger::instance().flush();
    } catch (...) {
    }
}

DWORD WINAPI dumpThread(void* /*unused*/) {
    WaitForSingleObject(state.request, INFINITE);
    writeCrashDump();
    SetEvent(state.done);
    return 0;
}

// Small stack use only: a stack overflow runs this on a thread with little stack left.
LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* pointers) {
    CrashState& s = state;
    const DWORD self = GetCurrentThreadId();
    if (self == s.dumpThreadId) {
        return EXCEPTION_EXECUTE_HANDLER;  // the dump itself crashed: end the process
    }
    DWORD expected = 0;
    if (!s.owner.compare_exchange_strong(expected, self)) {
        if (expected == self) {
            return EXCEPTION_EXECUTE_HANDLER;  // a fault inside the filter: end the process
        }
        Sleep(INFINITE);  // another thread is dumping and ends the process; never hand it to WER
    }
    s.pointers = pointers;
    s.faultingThreadId = self;
    if (WaitForSingleObject(s.thread, 0) == WAIT_TIMEOUT) {  // the dump thread is alive
        SetEvent(s.request);
        const HANDLE waits[2] = {s.done, s.thread};
        WaitForMultipleObjects(2, waits, FALSE, kDumpTimeoutMs);
    } else {
        // ExitProcess ends every other thread before the DLL detach: dump on this thread.
        writeCrashDump();
    }
    return EXCEPTION_EXECUTE_HANDLER;  // ends the process with the exception code, no WER dialog
}

// Test hooks (RAWREEL_TEST_CRASH, design D22).

// Recurses until the stack overflows (EXCEPTION_STACK_OVERFLOW); `depth` never gets negative.
int overflowStack(int depth) {
    volatile char frame[4096];
    frame[0] = static_cast<char>(depth);
    if (depth >= 0) {
        return overflowStack(depth + 1) + frame[0];
    }
    return frame[0];
}

// Several threads crash at the same moment; the first one's dump must win.
void crashOnThreads() {
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&go] {
            while (!go.load()) {
            }
            RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        });
    }
    go.store(true);
    for (std::thread& thread : threads) {
        thread.join();
    }
}

// Crashes in its destructor when armed. Constructed before main(), so it is destroyed after every
// static that install() or later code creates: a crash at exit, after their destructors ran.
struct ExitCrash {
    std::atomic<bool> armed{false};
    ExitCrash() = default;
    ExitCrash(const ExitCrash&) = delete;
    ExitCrash& operator=(const ExitCrash&) = delete;
    ~ExitCrash() {
        if (armed.load()) {
            RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        }
    }
};
ExitCrash exitCrash;

template <std::size_t N>
bool copyText(wchar_t (&target)[N], std::wstring_view text) {
    if (text.size() >= N) {
        return false;
    }
    text.copy(target, text.size());
    target[text.size()] = L'\0';
    return true;
}

}  // namespace

std::string CrashHandler::install(std::string_view exe) {
    if (state.writeDump != nullptr) {
        return {};  // installed already
    }
    auto library = dvv::platform::DynamicLibrary::open(
        "dbghelp.dll", dvv::platform::LibrarySearch::SystemDirectory);
    if (!library) {
        return "crash dumps are off: " + library.error().describe();
    }
    auto base = dvv::paths::userDataDir("rawreel");
    if (!base) {
        return "crash dumps are off: " + base.error().describe();
    }
    const auto writeDump = library->function<WriteDump>("MiniDumpWriteDump");
    if (writeDump == nullptr) {
        return "crash dumps are off: MiniDumpWriteDump not found";
    }
    const std::wstring exe_name(exe.begin(), exe.end());  // ASCII names only
    if (!copyText(state.base, base->wstring()) ||
        !copyText(state.folder, (*base / "crashes").wstring()) || !copyText(state.exe, exe_name)) {
        return "crash dumps are off: the crash folder path is too long";
    }
    // Process-lifetime handles, never closed (the filter may run at any time until the end).
    state.request = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    state.done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (state.request == nullptr || state.done == nullptr) {
        return "crash dumps are off: CreateEventW failed (error " + std::to_string(GetLastError()) +
               ")";
    }
    // The dump runs on this thread with its own stack (Microsoft's advice for MiniDumpWriteDump,
    // which needs more stack than a stack overflow leaves).
    state.thread = CreateThread(nullptr, 256 * 1024, &dumpThread, nullptr,
                                STACK_SIZE_PARAM_IS_A_RESERVATION, &state.dumpThreadId);
    if (state.thread == nullptr) {
        return "crash dumps are off: CreateThread failed (error " + std::to_string(GetLastError()) +
               ")";
    }
    library->keepLoaded();
    state.writeDump = writeDump;
    SetUnhandledExceptionFilter(&onUnhandledException);
    return {};
}

void CrashHandler::crashIfRequested() {
    wchar_t value[16] = {};
    const DWORD length = GetEnvironmentVariableW(L"RAWREEL_TEST_CRASH", value, 16);
    if (length == 0 || length >= 16) {
        return;
    }
    const std::wstring_view mode(value, length);
    if (mode == L"1") {
        RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    } else if (mode == L"stack") {
        static_cast<void>(overflowStack(0));
    } else if (mode == L"threads") {
        crashOnThreads();
    } else if (mode == L"exit") {
        exitCrash.armed.store(true);  // the process crashes after main(), in the static destructors
    }
}

}  // namespace rawreel
#else
namespace rawreel {
std::string CrashHandler::install(std::string_view /*exe*/) {
    return {};
}
void CrashHandler::crashIfRequested() {}
}  // namespace rawreel
#endif
