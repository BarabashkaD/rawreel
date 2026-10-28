# M1b Platform and Presentation Implementation Plan

> For agentic workers: executed subagent-driven. Each task runs in a fresh session. Two critical
> reviewers and an arbiter review each task.

**Goal:** a wx window on Windows with a Vulkan viewport showing a shader test pattern; resize and
minimise work.

**Exit criterion (U-WIN):** on Windows (NVIDIA), `rawreel-gui` shows the test pattern in its Vulkan
viewport and redraws through resize, maximise, minimise/restore and close, with no validation
message and no leak (`rawreel.gui_selftest` and the manual check of Task 12a). Every repository
passes the Windows matrix, the lint and the privacy lint. The Linux sanity build compiles; nothing
is tested on Linux.

**Design (binding):** the approved M1b design: scope, decisions, APIs, threading handshake, tests,
task list. Its decisions log comes with it. The user decision U-WIN (decisions.md) narrows it to
Windows for v1.0; see "U-WIN changes" at the end.

## Global Constraints
- **Platform (U-WIN, user decision, binding):** v1.0 supports Windows only; Linux support follows
  later. Keep the code portable in every step: Windows-only code lives in `*_win32.cpp` /
  `*_msw.cpp` files or `#if defined(_WIN32)` blocks, and Linux-only code (X11/GTK paths) keeps
  compiling but is not run. Nothing is tested on Linux.
- **Toolchains:** C++23. MSVC v145 (`vs2026`, `vs2026-asan`). Clang 19 with libstdc++ 14 in WSL
  Ubuntu 24.04: clang-format, and `ninja-clang` only for the checkpoint build (clang-tidy and the
  Linux sanity build); `ninja-clang-asan` and `ninja-clang-tsan` are not run. Warnings are errors
  for our code.
  Third-party sources compiled into a dvv target never fail the build on warnings. Third-party
  headers are always SYSTEM.
- **Per-task checks (U-PROC, binding).** Every task runs them once, on its final commit, in each
  repository it changed (`<repo>`). Nothing else runs per task: no `vs2026` Release, no
  `vs2026-asan`, no clang-tidy, no WSL build, no Linux sanity build. A failure is fixed in a new
  commit and the check repeated. `<GPU env>` is
  `$env:DVV_REQUIRE_GPU='1'; $env:DVV_VK_VALIDATION='sync'; $env:DVV_VK_VALIDATION_REQUIRED='1'`.
  1. Windows Debug build and all tests, GPU and display tests on NVIDIA (`run_in_background: true`
     when long):
     `<GPU env>; Push-Location <repo>; cmake --preset vs2026 | Out-Null; cmake --build --preset vs2026; ctest --preset vs2026 --output-on-failure 2>&1 | Select-String 'tests passed|tests failed|Skipped|error'; Pop-Location`.
     Expected: `100% tests passed`, no GPU, display or GUI test skipped, 0 validation messages.
  2. Concurrency repeats, only in a risky task that names them: its tests ×10 in Debug,
     `<GPU env>; Push-Location <repo>; ctest --preset vs2026 -R '<regex>' --repeat until-fail:10 --output-on-failure 2>&1 | Select-Object -Last 5; Pop-Location`.
  3. clang-format on the files the task changed (WSL, no build). `<base>` is the repository's head
     before the task's first commit (record `git -C <repo> rev-parse HEAD` when the task starts):
     `wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd <repo> && git diff --name-only -z --diff-filter=d <base> -- "*.h" "*.cpp" | xargs -0 -r clang-format --dry-run --Werror && echo FORMAT-OK'`.
     `git diff <base>` compares the working tree (run it after `git add -A`), so it works before and
     after the commit. In a repository the task creates, `git ls-files -z "*.h" "*.cpp"` replaces it.
     Expected `FORMAT-OK`; a reported file is fixed with `clang-format -i` in WSL.
  4. Privacy lint, commits mode:
     `cmake -DREPO=<repo> -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake`.
     Expected `-- privacy-lint: clean (commits)`.
- **Checkpoints (U-PROC):** Task 10, Task 12a, Task 13b (final). Tasks 0-9 ran the former full
  matrix in every task, so the first checkpoint covers Task 10 only.
- **Checkpoint checks (U-PROC, U-PROC-tidy, binding).** A checkpoint task runs them after its own
  steps (its Per-task checks included), on its final commits, in every repository changed since the
  previous checkpoint (`<repo>`). `<cp-base>` is the repository's head at the previous checkpoint:
  the controller records the heads in its ledger at each checkpoint and names them in the brief; a
  repository created since then uses `git ls-files` in place of the `git diff`. The two Windows runs
  go in one background call per repository (never two package tests at once); the WSL build may run
  in parallel.
  1. Windows Release:
     `<GPU env>; Push-Location <repo>; cmake --build --preset vs2026-release; ctest --preset vs2026-release --output-on-failure 2>&1 | Select-String 'tests passed|tests failed|Skipped|error'; Pop-Location`.
  2. Windows ASan, then the ×10 concurrency repeats of each risky task since the previous checkpoint
     under ASan:
     `<GPU env>; Push-Location <repo>; cmake --workflow --preset vs2026-asan 2>&1 | Select-String 'tests passed|tests failed|Skipped|AddressSanitizer'; Pop-Location` and
     `<GPU env>; Push-Location <repo>; ctest --preset vs2026-asan -R '<regex>' --repeat until-fail:10 --output-on-failure 2>&1 | Select-Object -Last 5; Pop-Location`.
     Expected: all passed, no AddressSanitizer report. A report with a dvv frame is fixed in code;
     one entirely inside the NVIDIA driver or the Vulkan loader is recorded and reported, not
     suppressed.
  3. clang-tidy on the C++ sources changed since `<cp-base>`, after one WSL `ninja-clang` configure +
     build with no tests (`run_in_background: true`). This build is also the Linux sanity build:
     `wsl -d Ubuntu-24.04 -e bash -lc 'set -o pipefail && . _build/tools/linux/env.sh && cd <repo> && cmake --preset ninja-clang >/dev/null && cmake --build --preset ninja-clang 2>&1 | tail -n 30 && git diff --name-only -z --diff-filter=d <cp-base> HEAD -- ":(glob)src/**/*.cpp" ":(glob)tests/*.cpp" ":(glob)tools/*.cpp" ":(exclude,glob)**/*_win32.cpp" ":(exclude,glob)**/*_msw.cpp" | xargs -0 -r -P "$(nproc)" -n 1 clang-tidy -p ../_build/<repo>/ninja-clang --quiet && echo TIDY-OK'`.
     Expected `TIDY-OK`. A build error in our code is a portability bug and is fixed; a toolchain or
     third-party failure is logged in the plan's "Changes during execution" and reported, not fixed.
  A checkpoint failure is fixed (a `fix:` commit, then the Per-task checks and the failed checkpoint
  check again) before the next task starts. The final task's checkpoint covers everything: every
  repository of the milestone, clang-tidy on all sources (`git ls-files -z` with the same pathspecs
  replaces the `git diff`), and the ASan repeats of every risky task of the milestone.
- **Risk accepted (U-WIN):** Windows has no TSan, so data races are found only by review and
  `vs2026-asan` until Linux returns.
- **Git and repositories:**
  - Branch `m1b` in rawreel-workspace, dvv-base, dvv-registry and dvv-gpu (created from the `m1a`
    heads).
  - New repositories dvv-platform and rawreel are created with `git init -b m1b`, local only.
  - No remotes, no push. Commits use the noreply identity from bootstrap, with the trailer
    `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- **Privacy:** no machine paths, user names, host names or personal e-mails in committed files or
  messages. Run the privacy lint in commits mode before finishing a task.
- **Shells:**
  - Windows: the PowerShell tool only; git only from PowerShell, except the read-only
    `git ls-files` and `git diff --name-only` inside the WSL format and tidy commands.
  - Linux (clang-format and the checkpoint build only): `wsl -d Ubuntu-24.04 -e bash -lc
    '. _build/tools/linux/env.sh && …'` from the workspace root. WSL has no passwordless sudo.
- **GPU and display tests:**
  - Exit 77 means skip; `DVV_REQUIRE_GPU=1` turns a skip into a failure.
  - NVIDIA on Windows: run with `DVV_VK_VALIDATION=sync` set in the same command.
  - Zero validation messages.
- **Licence gate:** M1b allows `libpng-2.0`, `IJG`, `X11` and `Unicode-DFS-2016` for static
  linking (decisions D4 and D6), in addition to the M1a lists.
- **Long runs:** a tool call stops after 10 minutes, so long runs use `run_in_background`.
  The Windows presets and the WSL checkpoint build may run in parallel. The Per-task checks run
  once, on a task's final commit; the workspace workflow runs once per task group.
- **Spec §9.4** applies to every managed type.

# M1b plan, part 1: Tasks 0-4

Conventions for every task below:
- Run commands from the workspace root, the folder that contains `rawreel-workspace`.
- Use the PowerShell tool on Windows. Linux runs as
  `wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && <command>'`, also from the
  workspace root.
- Run git from PowerShell only, except the read-only `git ls-files` and `git diff --name-only`
  inside the WSL format and tidy commands.
- From Task 10 on, each task ends with the "Per-task checks" of Global Constraints, and a checkpoint
  task also with the "Checkpoint checks" (U-PROC).
- Every commit ends with the trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Nothing is pushed.
- A tool call stops after 10 minutes. Bootstraps and workflow presets that take longer run with
  `run_in_background`. Windows and WSL runs may go in parallel.
- Privacy lint after every commit: `cmake -DREPO=<repo> -DMODE=commits
  "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake`.
  Expected: `-- privacy-lint: clean (commits)`.

Facts the plan writer checked on 2026-10-23, so implementers need not repeat them:
- `wxWidgets-3.3.4.tar.bz2` was downloaded. It is 34,038,491 bytes and its SHA256 is
  `815c16cafa4fcbfcf25c5c0a3418309c62257b8a3b6f91d85a1367a134f98c98`, as the design states.
- Every wx CMake option named in Task 4 exists in 3.3.4 (`build/cmake/options.cmake`,
  `init.cmake`, `toolkit.cmake`).
- wx sets `cmake_minimum_required(VERSION 3.10...4.1)`, so CMP0141 is NEW.
- The licence files that Task 4 names exist at those paths in the archive.
- Vulkan-ValidationLayers 1.4.365 never includes `X11/Xlib-xcb.h`. Its Linux WSI options use only
  the pkg-config modules `xcb`, `x11`, `xrandr` and `wayland-client`.

---

### Task 0: Branches and Linux prerequisites (verification only)

**Goal:** Confirm that the four existing repositories are on branch `m1b`, created from their
`m1a` heads with the right commit identity. Confirm that WSL has every package the M1b builds
need, apart from the two recorded gaps (D5), and that Vulkan presentation works on the WSLg display.

**Files:** none. This task makes no commit.

**Interfaces:**
- Produces:
  - Branch `m1b` checked out in `rawreel-workspace`, `dvv-base`, `dvv-registry` and `dvv-gpu`.
    The controller already created them from the `m1a` heads: rawreel-workspace `63ab483`,
    dvv-base `c8f88a3`, dvv-registry `8fff8cb`, dvv-gpu `7d6cad6`.
  - A short report: branch heads, prerequisites, display and Vulkan results, and whether
    `xvfb-run` exists.
- The D5 plan choice below holds for the whole M1b plan.

**Note (plan choice, D5: `libx11-xcb-dev`): it is not needed and is not installed.** The only M1b
code that could need its header `X11/Xlib-xcb.h` is the validation layer's Linux WSI. Its source
never includes that header, and its CMake checks only the pkg-config modules `xcb`, `x11`,
`xrandr` and `wayland-client`; all four are installed. Nothing else in M1b uses the header:
- wxWidgets 3.3.4 does not include it, and does not look for `x11-xcb`.
- dvv-platform uses plain Xlib.
- Lavapipe needs only the run-time library `libx11-xcb1`, which is installed.

So the bootstrap preflight (Task 4) checks no `x11-xcb` module, and the apt line it prints leaves
`libx11-xcb-dev` out. Task 4 reports this so that Task 13a's CI package list and the workspace
README leave it out as well. The rejected alternatives were a user-space `apt-get download` +
`dpkg -x` extraction and building the layer without XCB locally; both are workarounds for a header
that nothing uses.

**Note (environment, D5: xvfb):** `xvfb` is missing and cannot be installed without the user's sudo
password. Local display tests use WSLg (`DISPLAY=:0`). The `xvfb-run` variants of later tasks are
skipped locally while `command -v xvfb-run` finds nothing, and they run in CI. Once the user has
run `sudo apt-get install -y xvfb`, they run locally too.

- [ ] **Step 1: Check the branches and heads**

```powershell
foreach ($r in 'rawreel-workspace','dvv-base','dvv-registry','dvv-gpu') {
    "== $r"
    git -C $r status --short --branch
    git -C $r log --oneline m1a..HEAD
    git -C $r merge-base --is-ancestor m1a HEAD; "m1a is ancestor: $($LASTEXITCODE -eq 0)"
    git -C $r config user.email
}
```

Expected, per repository:
- `## m1b` and no other status line. A repository with uncommitted changes is reported to the
  controller and not touched.
- `m1a is ancestor: True`.
- `m1a..HEAD` empty. The one exception: rawreel-workspace may already hold `docs:` commits that add
  the M1b plan.
- An e-mail address ending in `@users.noreply.github.com`.

- [ ] **Step 2: Check the Linux development packages (pkg-config)**

```powershell
wsl -d Ubuntu-24.04 -e bash -lc 'for m in gtk+-3.0 x11 xcb xrandr wayland-client wayland-protocols xkbcommon zlib libpng; do if pkg-config --exists $m; then echo "$m ok $(pkg-config --modversion $m)"; else echo "$m MISSING"; fi; done'
```

Expected: every line reads `ok`, for example `gtk+-3.0 ok 3.24.41`, `x11 ok 1.8.7`,
`xcb ok 1.15`, `xrandr ok 1.5.2` and `wayland-client ok 1.22.0`. Any `MISSING` line is reported to
the controller: the user then installs the package. Task 4's bootstrap would stop on it.

- [ ] **Step 3: Confirm D5 against the validation-layer source that is already extracted**

```powershell
wsl -d Ubuntu-24.04 -e bash -lc 'cd _build/third-party/clang/Vulkan-ValidationLayers-prefix/src/Vulkan-ValidationLayers && (grep -rl "Xlib-xcb.h" . || echo "no Xlib-xcb.h include") && grep -n "pkg_check_modules" CMakeLists.txt'
```

Expected:
- `no Xlib-xcb.h include`.
- Four `pkg_check_modules` lines, for `xcb`, `x11`, `xrandr` and `wayland-client`.

If the folder is missing (a cleaned build tree), skip this step and say so in the report.

- [ ] **Step 4: Check the display and Vulkan presentation in WSL**

```powershell
wsl -d Ubuntu-24.04 -e bash -lc 'echo "DISPLAY=$DISPLAY"; vulkaninfo --summary 2>&1 | grep -E "VK_KHR_xlib_surface|deviceName|apiVersion"; timeout 30 vulkaninfo 2>&1 | grep -A2 "Presentable Surfaces"'
```

Expected:
- `DISPLAY=:0`.
- `VK_KHR_xlib_surface` among the instance extensions.
- A `deviceName = llvmpipe (...)` line with `apiVersion = 1.4.x` (1.4.318 on 2026-10-23).
- `Presentable Surfaces:` followed by a `VK_KHR_xlib_surface` line.

If the highest `apiVersion` is below 1.4, report it: the user updates `mesa-vulkan-drivers`.

- [ ] **Step 5: Check xvfb**

```powershell
wsl -d Ubuntu-24.04 -e bash -lc 'if command -v xvfb-run >/dev/null; then xvfb-run -a -s "-screen 0 1920x1080x24" vulkaninfo --summary 2>&1 | grep -E "deviceName|VK_KHR_xlib_surface"; else echo "xvfb-run missing: xvfb variants are skipped locally (D5)"; fi'
```

Expected on 2026-10-23: `xvfb-run missing: ...`. Once xvfb is installed, the output lists
`llvmpipe` and `VK_KHR_xlib_surface`.

- [ ] **Step 6: Report.** Give the four heads, the package list, and the display and Vulkan results.
  Say whether xvfb is present. No commit.

**Notes:** Design §2 D11, D12; §9.2; §10 Task 0; decisions log D5.

---

### Task 1: UTF-8 environment API (dvv-base)

**Goal:** Make `dvv::getEnvironment` and `dvv::setEnvironment` take and return UTF-8 on every
platform, with the same name and value checks on Windows and Linux. On Windows, a value that is set
must be visible to `GetEnvironmentVariableW`, and so to the Vulkan loader and child processes.

**Files:**
- Modify `dvv-base/include/dvv/base/environment.h`: the contract comments. The signatures do not
  change.
- Modify `dvv-base/src/environment.cpp`: the UTF-8 checks and the Windows wide-character
  implementation.
- Create `dvv-base/tests/test_environment.cpp`: the environment tests. The existing
  `"environment round trip"` case moves here from `tests/test_paths.cpp`.
- Modify `dvv-base/tests/test_paths.cpp`: remove the moved case. The `#include
  "dvv/base/environment.h"` stays, because the XDG test uses it.
- Modify `dvv-base/tests/CMakeLists.txt`: add `test_environment.cpp`, and add
  `DVV_BASE_TEST_INHERITED` to the test environment.
- Modify `dvv-base/README.md`: one paragraph on the environment API.

**Interfaces** (header in full):

```cpp
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace dvv {

// Reads an environment variable. Names and values are UTF-8 on every platform. On Windows the
// value comes from the process environment block (GetEnvironmentVariableW), which the CRT, child
// processes and the Vulkan loader share. nullopt when the variable is not set, when the name is
// empty, contains '=' or '\0' or is not valid UTF-8, or (Windows) when the value is not valid
// UTF-16 (an unpaired surrogate). Not synchronised with other threads that read or write the
// environment.
[[nodiscard]] std::optional<std::string> getEnvironment(std::string_view name);

// Sets the variable, or removes it when `value` is empty. On Windows it goes through the CRT's
// wide environment (_wputenv_s), which also updates the process environment block. False when the
// name is empty, contains '=' or '\0' or is not valid UTF-8, when the value contains '\0' or is not
// valid UTF-8, or when the platform call fails. Not synchronised with other threads.
bool setEnvironment(std::string_view name, std::string_view value);

}  // namespace dvv
```

- Consumes: nothing new.
- Produces: the widened contract. Task 12a uses UTF-8 environment values on Windows, and 12b uses
  `setEnvironment("GDK_BACKEND", "x11")`. dvv-gpu's existing callers (`DVV_VK_VALIDATION`,
  `DVV_REQUIRE_GPU`, the tests' `ScopedEnvironment`) keep working unchanged.

**Note (plan choice): reading on Windows uses `GetEnvironmentVariableW`, not `_wdupenv_s`.** Design
§4.1 names `_wdupenv_s`. In the UCRT (Windows SDK 10.0.26100 source,
`ucrt/env/environment_initialization.cpp`), a program whose entry point is `main` starts with only
the narrow environment, decoded with the ANSI code page. The first wide call then clones the wide
environment from that narrow copy, again through `CP_ACP` (`initialize_environment_by_cloning_nolock`).
An inherited value with characters outside the ANSI code page, such as `日本` on a Western-European
system, would therefore read back as `?`.

`GetEnvironmentVariableW` reads the process block, which is exact. It also sees everything
`_wputenv_s` writes, because `_wputenv_s` updates the wide CRT copy, the narrow copy and the process
block (`ucrt/env/setenv.cpp`). Writing stays as the design says. Test case 6 below catches the
inherited-value case.

**Note (plan choice): NUL bytes.** A name or value with an embedded `'\0'` cannot be passed to the
C or Win32 environment calls without truncation, so it counts as invalid: `nullopt` or `false`.

**Note (plan choice): Linux.** The name checks apply as on Windows (empty, `'='`, `'\0'`, invalid
UTF-8). `setEnvironment` rejects a value that is not valid UTF-8 or contains `'\0'`, as the header
says. `getEnvironment` returns the stored bytes unchanged, because Linux values are bytes ("Linux
is unchanged except for the name checks").

**Behaviour:**
- `isValidUtf8(text)` (private, in an anonymous namespace) accepts exactly the well-formed UTF-8 of
  RFC 3629. It rejects:
  - overlong forms (for example `C0 AF`);
  - the surrogates U+D800 to U+DFFF (for example `ED A0 80`);
  - code points above U+10FFFF (for example `F4 90 80 80`);
  - stray continuation bytes, truncated sequences and the bytes `F8` to `FF`.
- `validName(name)`: not empty, no `'='`, no `'\0'`, `isValidUtf8`.
  `validValue(value)`: no `'\0'`, `isValidUtf8`. The empty value is valid and means "remove".
- `getEnvironment`:
  - An invalid name gives `nullopt`, without a platform call.
  - Windows: convert the name with `MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS)`, read with
    `GetEnvironmentVariableW` and convert the result with `WideCharToMultiByte(CP_UTF8,
    WC_ERR_INVALID_CHARS)`. A failed conversion (an unpaired surrogate) gives `nullopt`. A variable
    set to an empty string (possible only from outside the process) gives `""`.
  - Linux: `std::getenv`.
- `setEnvironment`:
  - An invalid name or value gives `false`, without a platform call.
  - Windows: `_wputenv_s(name, value) == 0`. An empty value removes the variable.
  - Linux: `setenv(name, value, 1) == 0`, or `unsetenv(name) == 0` for an empty value.

**Code** (the Windows conversions and the size loop are easy to get wrong; this is the whole file):

```cpp
#include "dvv/base/environment.h"

#include <cstddef>
#include <cstdlib>

#if defined(_WIN32)
#include <windows.h>

#include <climits>
#endif

namespace dvv {

namespace {

// Well-formed UTF-8 (RFC 3629): no overlong forms, no surrogates, nothing above U+10FFFF.
bool isValidUtf8(std::string_view text) noexcept {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80) {
            ++i;
            continue;
        }
        std::size_t length = 0;
        char32_t code = 0;
        char32_t minimum = 0;
        if ((lead & 0xE0) == 0xC0) {
            length = 2;
            code = lead & 0x1F;
            minimum = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3;
            code = lead & 0x0F;
            minimum = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4;
            code = lead & 0x07;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (text.size() - i < length) {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80) {
                return false;
            }
            code = (code << 6) | (next & 0x3F);
        }
        if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
            return false;
        }
        i += length;
    }
    return true;
}

bool validValue(std::string_view value) noexcept {
    return value.find('\0') == std::string_view::npos && isValidUtf8(value);
}

bool validName(std::string_view name) noexcept {
    return !name.empty() && name.find('=') == std::string_view::npos && validValue(name);
}

#if defined(_WIN32)
std::optional<std::wstring> toWide(std::string_view text) {
    if (text.empty()) {
        return std::wstring();
    }
    if (text.size() > static_cast<std::size_t>(INT_MAX)) {
        return std::nullopt;
    }
    const int length = static_cast<int>(text.size());
    const int size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
    if (size <= 0) {
        return std::nullopt;
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, wide.data(),
                            size) != size) {
        return std::nullopt;
    }
    return wide;
}

std::optional<std::string> toUtf8(std::wstring_view text) {
    if (text.empty()) {
        return std::string();
    }
    if (text.size() > static_cast<std::size_t>(INT_MAX)) {
        return std::nullopt;
    }
    const int length = static_cast<int>(text.size());
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length,
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return std::nullopt;
    }
    std::string narrow(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, narrow.data(),
                            size, nullptr, nullptr) != size) {
        return std::nullopt;
    }
    return narrow;
}
#endif

}  // namespace

std::optional<std::string> getEnvironment(std::string_view name) {
    if (!validName(name)) {
        return std::nullopt;
    }
#if defined(_WIN32)
    const auto wide_name = toWide(name);
    if (!wide_name) {
        return std::nullopt;
    }
    std::wstring buffer(128, L'\0');
    for (;;) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetEnvironmentVariableW(wide_name->c_str(), buffer.data(),
                                                     static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
                return std::nullopt;
            }
            return std::string();  // set to an empty value
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return toUtf8(buffer);
        }
        buffer.resize(length);  // too small: `length` includes the terminator; try again
    }
#else
    const std::string key(name);
    const char* value = std::getenv(key.c_str());
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

bool setEnvironment(std::string_view name, std::string_view value) {
    if (!validName(name) || !validValue(value)) {
        return false;
    }
#if defined(_WIN32)
    const auto wide_name = toWide(name);
    const auto wide_value = toWide(value);
    if (!wide_name || !wide_value) {
        return false;
    }
    return _wputenv_s(wide_name->c_str(), wide_value->c_str()) == 0;
#else
    const std::string key(name);
    if (value.empty()) {
        return unsetenv(key.c_str()) == 0;
    }
    const std::string text(value);
    return setenv(key.c_str(), text.c_str(), 1) == 0;
#endif
}

}  // namespace dvv
```

If clang-tidy (bugprone, performance, naming) flags something in this code, fix it without changing
the behaviour.

**Tests first** (`tests/test_environment.cpp`, doctest). The UTF-8 sample is written with escapes so
that the source encoding cannot matter:

```cpp
// "Grüße-日本-🎞" (U+00FC, U+00DF, U+65E5, U+672C, U+1F39E)
constexpr std::string_view UTF8_SAMPLE = "Gr\xC3\xBC\xC3\x9F" "e-\xE6\x97\xA5\xE6\x9C\xAC-\xF0\x9F\x8E\x9E";
#if defined(_WIN32)
constexpr std::wstring_view WIDE_SAMPLE = L"Grüße-日本-\U0001F39E";
#endif
```

1. `"environment round trip"`: moved unchanged from `test_paths.cpp`.
2. `"environment values round-trip as UTF-8"`:
   - `REQUIRE(setEnvironment("DVV_BASE_TEST_UTF8", UTF8_SAMPLE))`.
   - `CHECK(getEnvironment("DVV_BASE_TEST_UTF8") == std::string(UTF8_SAMPLE))`.
   - Windows only: `GetEnvironmentVariableW(L"DVV_BASE_TEST_UTF8", ...)` returns
     `WIDE_SAMPLE.size()` and the buffer equals `WIDE_SAMPLE`.
   - Then `REQUIRE(setEnvironment("DVV_BASE_TEST_UTF8", ""))` and
     `CHECK_FALSE(getEnvironment("DVV_BASE_TEST_UTF8").has_value())`.
   - Windows only: `GetEnvironmentVariableW` now returns 0, with
     `GetLastError() == ERROR_ENVVAR_NOT_FOUND`.
3. `"non-ASCII names work"`:
   - The name is `"DVV_BASE_TEST_\xC3\x9C"`.
   - `setEnvironment(name, "x")` is true, then `getEnvironment(name) == "x"`.
   - Then remove it: `setEnvironment(name, "")` is true, and `getEnvironment(name)` is `nullopt`.
4. `"invalid names are rejected"`:
   - The names are `""`, `"A=B"`, `"=A"`, `"\xC3"`, `"\xC0\xAF"`, `"\xED\xA0\x80"` and
     `std::string("A\0B", 3)`.
   - For each, `CHECK_FALSE(setEnvironment(name, "v"))` and
     `CHECK_FALSE(getEnvironment(name).has_value())`.
   - Then `CHECK_FALSE(getEnvironment("A").has_value())`, so the NUL name did not set `A`.
5. `"invalid values are rejected and change nothing"`:
   - `REQUIRE(setEnvironment("DVV_BASE_TEST_BAD", "kept"))` first.
   - Then `setEnvironment("DVV_BASE_TEST_BAD", v)` is false for each of `"\xFF"`,
     `"\xF4\x90\x80\x80"` and `std::string("a\0b", 3)`.
   - `getEnvironment("DVV_BASE_TEST_BAD") == "kept"` still holds.
   - Clean up with `setEnvironment("DVV_BASE_TEST_BAD", "")`.
6. `"a variable inherited from the parent process reads as UTF-8"`:
   - Read `const auto value = getEnvironment("DVV_BASE_TEST_INHERITED");`.
   - When it is `nullopt`, call `MESSAGE("DVV_BASE_TEST_INHERITED is set only by CTest; skipped")`
     and return.
   - Otherwise `CHECK(*value == std::string(UTF8_SAMPLE))`.
7. Windows only, `"a value that is not valid UTF-16 reads as nullopt"`:
   - `REQUIRE(_wputenv_s(L"DVV_BASE_TEST_SURROGATE", L"a\xD800" L"b") == 0)`. The literal is split
     so that `b` is not read as a hex digit.
   - `CHECK_FALSE(getEnvironment("DVV_BASE_TEST_SURROGATE").has_value())`.
   - Clean up with `_wputenv_s(L"DVV_BASE_TEST_SURROGATE", L"")`.

The Windows-only parts include `<windows.h>` behind `#if defined(_WIN32)`. The test target already
gets `WIN32_LEAN_AND_MEAN`, `NOMINMAX` and `UNICODE` from `dvv_set_compiler_options`.

`tests/CMakeLists.txt` adds the source to `dvv_base_tests` and changes the test properties to:

```cmake
set_tests_properties(dvv_base.unit PROPERTIES
    TIMEOUT 300
    # DVV_BASE_TEST_INHERITED checks that a value set by the parent process reads as UTF-8.
    ENVIRONMENT "DVV_LEAK_CHECK=fail;DVV_BASE_TEST_INHERITED=Grüße-日本-🎞"
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
```

The CMake file is UTF-8, and CTest passes the value to the child process as UTF-16 on Windows.

**Which tests fail first, against the old implementation:**
- Case 4 on both platforms: names with invalid UTF-8 are accepted, as are `'\0'` names, which then
  set `A`.
- Case 5 on both platforms: invalid values are accepted.
- On Windows also case 2 (the narrow `_putenv_s` stores the UTF-8 bytes as ANSI text, so
  `GetEnvironmentVariableW` sees mojibake), case 6 (`_dupenv_s` returns ANSI bytes) and case 7.

- [ ] **Step 1:** Write `tests/test_environment.cpp`. Move the round-trip case out of
  `test_paths.cpp` and update `tests/CMakeLists.txt` as shown.
- [ ] **Step 2: Run the tests and see them fail**

```powershell
Push-Location dvv-base; cmake --preset vs2026; cmake --build --preset vs2026; ctest --preset vs2026 -R dvv_base.unit --output-on-failure; Pop-Location
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd dvv-base && cmake --preset ninja-clang >/dev/null && cmake --build --preset ninja-clang && ctest --preset ninja-clang -R dvv_base.unit --output-on-failure | tail -30'
```

Expected: `dvv_base.unit` fails on Windows in cases 2, 4, 5, 6 and 7, and on Linux in cases 4
and 5.
- [ ] **Step 3:** Write the new `src/environment.cpp` and `environment.h` as given.
- [ ] **Step 4:** Rerun Step 2. Expected: `dvv_base.unit` passes on both platforms, and case 6 runs
  (it does not print the skip message under CTest).
- [ ] **Step 5:** Add this paragraph to `README.md`, after the build instructions:

  > `dvv::getEnvironment` and `dvv::setEnvironment` take and return UTF-8 names and values on
  > every platform. On Windows they read the process environment block and write through the CRT's
  > wide environment, so child processes and the Vulkan loader see the same values. Invalid UTF-8,
  > empty names and names with `=` are rejected.

- [ ] **Step 6: Full matrix for dvv-base, and its dependants** (run in the background, Windows and
  WSL in parallel):

```powershell
Push-Location dvv-base; cmake --workflow --preset vs2026; cmake --workflow --preset vs2026-asan; Pop-Location
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd dvv-base && for p in ninja-clang ninja-clang-asan ninja-clang-tsan; do cmake --workflow --preset $p 2>&1 | grep -E "tests passed|tests failed|Error"; done'
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd dvv-base && git ls-files -z "*.h" "*.cpp" | xargs -0 clang-format --dry-run --Werror && git ls-files -z ":(glob)src/*.cpp" ":(glob)tests/*.cpp" | xargs -0 -n 4 -P "$(nproc)" clang-tidy -p ../_build/dvv-base/ninja-clang --quiet && echo LINT-OK'
Push-Location rawreel-workspace; cmake --workflow --preset vs2026; Pop-Location
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd rawreel-workspace && cmake --workflow --preset ninja-clang 2>&1 | grep -E "tests passed|tests failed|Error"'
```

**Expected results:**
- dvv-base: `100% tests passed` with 5 tests for `vs2026` (Debug and Release) and `ninja-clang`
  (Debug and Release), and 4 tests for `vs2026-asan`, `ninja-clang-asan` and `ninja-clang-tsan`,
  which have no package test.
- `LINT-OK`.
- The workspace workflows pass every test, as before Task 1. They cover dvv-gpu, whose GPU tests
  read and set environment variables through this API. GPU tests run on NVIDIA and lavapipe and
  are not skipped.

- [ ] **Step 7: Commit and lint**

```powershell
git -C dvv-base add include/dvv/base/environment.h src/environment.cpp tests/test_environment.cpp tests/test_paths.cpp tests/CMakeLists.txt README.md
git -C dvv-base commit -m "feat: read and write the environment as UTF-8 on every platform" -m "Names and values are checked as well-formed UTF-8 without '=' or NUL. On Windows the values go through the wide CRT environment (_wputenv_s) and are read from the process environment block, so inherited values outside the ANSI code page survive and the Vulkan loader sees what the program sets." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
cmake -DREPO=dvv-base -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake
```

**Notes:**
- Design §4.1, §8.1 (dvv-base row), D17. The M1a leftover is closed.
- Risk: if CTest on Windows does not pass the UTF-8 `ENVIRONMENT` value through as UTF-16, case 6
  fails although the code is correct. Check it by running
  `cmd /c "set DVV_BASE_TEST_INHERITED=..."` by hand. If CTest is the cause, drop case 6 and the
  property entry, and report it. Do not weaken the other cases.

---

### Task 2: Rename `ComponentRegistry::create<I>` to `createComponent<I>` (dvv-registry)

**Goal:** Give the component factory the name `createComponent<I>`, in line with dvv-gpu's
`createContext` and `createBuffer`. Update every caller, test, the README and the spec's §9.4
example. No alias is kept.

**Files:**
- Modify `dvv-registry/include/dvv/registry/component_registry.h`:
  - Rename the member template declaration. Its comment block keeps the same text.
  - Rename the out-of-class definition `ComponentRegistry::create(std::string_view id, ...)`.
  - The static `create(const Capabilities&)` and the comment
    `// For create(caps) only (Entry is private).` stay.
- Modify the callers:
  - `dvv-registry/tests/test_component_registry.cpp`: every `->create<`. Also rename three test
    names:
    - `"create builds a component with validated and completed parameters"` becomes
      `"createComponent builds ..."`.
    - `"create records its caller as the creation site"` becomes `"createComponent records ..."`.
    - `"create reports unknown ids, other interfaces and invalid values"` becomes
      `"createComponent reports ..."`.
    - The comment `a second create still reaches the constructor` becomes
      `a second createComponent still reaches the constructor`.
  - `dvv-registry/tests/link_check.cpp`, `dvv-registry/tests/package/main.cpp` and
    `dvv-registry/tests/compile_fail/direct_construction.cpp` (line 82).
- Modify `dvv-registry/README.md`:
  - Line 54: `registry->createComponent<IGreeting>(...)`.
  - Lines 69 and 72: `` `create()` `` becomes `` `createComponent()` ``.
- Modify `rawreel-workspace/docs/superpowers/specs/2026-10-19-rawreel-design.md`, the §9.4
  lifetime-registry example at lines 626 to 628.

**Interfaces:**

```cpp
template <class I>
[[nodiscard]] Result<std::unique_ptr<I>> createComponent(
    std::string_view id, const ParameterValues& values = {},
    std::source_location where = std::source_location::current());
```

- Behaviour, errors (`Unsupported`, `Format`, `OutOfMemory`, `Internal`) and the creation-site
  recording are unchanged.
- Produces: the name `createComponent<I>` for M2+ callers. Nothing outside dvv-registry calls it
  yet: `git -C dvv-gpu grep "create<"` finds nothing.
- The M1a plan document is a historical record and is not edited.

**Tests first:** the existing tests are the specification. Rename the calls in the tests first, then
build: the build fails with `no member named 'createComponent'` (Clang) or C2039 (MSVC). Then rename
the header.

- [ ] **Step 1:** Record the current test counts:
  `Push-Location dvv-registry; cmake --workflow --preset vs2026 2>&1 | Select-String "tests passed"; Pop-Location`.
  Expected: `100% tests passed, 0 tests failed out of 9`, twice.
- [ ] **Step 2:** Rename the calls in `tests/` (all four files) and the three test names.
- [ ] **Step 3: Build and see it fail.**
  `Push-Location dvv-registry; cmake --build --preset vs2026 2>&1 | Select-String "error" | Select -First 5; Pop-Location`.
  Expected: C2039 `'createComponent': is not a member of 'dvv::ComponentRegistry'`.
- [ ] **Step 4:** Rename the declaration and the definition in `component_registry.h`. Then run
  clang-format on the changed files. Lines grow by 9 characters, and a call on line 395 then exceeds
  the 100-column limit:

```powershell
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd dvv-registry && clang-format -i include/dvv/registry/component_registry.h tests/test_component_registry.cpp tests/link_check.cpp tests/package/main.cpp tests/compile_fail/direct_construction.cpp'
```

- [ ] **Step 5:** Update the README lines. Then check that nothing is left:
  `git -C dvv-registry grep -n -E "(->|\.)create<|\bcreate\(\)"`. Expected: no output.
- [ ] **Step 6: Full matrix** (in the background, in parallel):

```powershell
Push-Location dvv-registry; cmake --workflow --preset vs2026; cmake --workflow --preset vs2026-asan; Pop-Location
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd dvv-registry && for p in ninja-clang ninja-clang-asan ninja-clang-tsan; do cmake --workflow --preset $p 2>&1 | grep -E "tests passed|tests failed|Error"; done'
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd dvv-registry && git ls-files -z "*.h" "*.cpp" | xargs -0 clang-format --dry-run --Werror && git ls-files -z ":(glob)src/*.cpp" ":(glob)tests/*.cpp" | xargs -0 -n 4 -P "$(nproc)" clang-tidy -p ../_build/dvv-registry/ninja-clang --quiet && echo LINT-OK'
```

**Expected results:**
- `out of 9` for `vs2026` and `ninja-clang` (Debug and Release): unit, two link tests, package and
  five compile-fail tests.
- `out of 8` for `vs2026-asan`, `ninja-clang-asan` and `ninja-clang-tsan`, which have no package
  test.
- `LINT-OK`.

The doctest case count of `dvv_registry_tests` is unchanged.
- [ ] **Step 7: Commit dvv-registry and lint**

```powershell
git -C dvv-registry add include/dvv/registry/component_registry.h tests README.md
git -C dvv-registry commit -m "refactor: rename ComponentRegistry::create<I> to createComponent<I>" -m "The factory now reads like dvv-gpu's createContext and createBuffer; the static create(caps) authority keeps its name. No alias: nothing is published yet." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
cmake -DREPO=dvv-registry -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake
```

- [ ] **Step 8: Update the spec example and commit it.** In the spec, replace

```text
  time. A factory whose callers should appear there takes the location as a defaulted last parameter,
  e.g. `ComponentRegistry::create<I>(std::string_view id, const ParameterValues& values = {},
  std::source_location where = std::source_location::current())`, so a leak report names the caller,
```

with

```text
  time. A factory whose callers should appear there takes the location as a defaulted last parameter,
  e.g. `ComponentRegistry::createComponent<I>(std::string_view id,
  const ParameterValues& values = {}, std::source_location where = std::source_location::current())`,
  so a leak report names the caller,
```

Join the old continuation text `not the factory. ...` to the new last line, and re-wrap the
paragraph to at most 100 characters per line without changing its wording.

```powershell
git -C rawreel-workspace add docs/superpowers/specs/2026-10-19-rawreel-design.md
git -C rawreel-workspace commit -m "docs: name createComponent<I> in the spec's lifetime-registry example" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
cmake -DREPO=rawreel-workspace -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake
```

**Notes:**
- Design §4.2, D17; decisions log D3.
- Note (plan choice): the spec line changes here rather than in Task 13b. The design asks for the
  rename and the spec example together (§4.2), and Task 13b's §9.4 update only adds the M1b rows.
- No workspace build is needed: no other repository uses dvv-registry in M1b.

---

### Task 3: Superbuild `CONFIGS` and licence-gate additions (rawreel-workspace)

**Goal:**
- Let a third-party declaration build and install several configurations (`CONFIGS Debug Release`)
  with a multi-config generator. A single-config generator rejects any `CONFIGS` other than
  `Release` at configure time, naming the dependency.
- Add the licence ids that the wxWidgets declarations of Task 4 need to the permissive allowlist,
  with tests.

**Precondition: user decisions.** The controller confirms them before dispatching the task.
- **D4 (accepted 2026-10-23, revisable):** add `libpng-2.0`.
- **U1 (new, asked by this plan): add `IJG` and `X11`.** Checking the wx 3.3.4 sources found code
  under other licences, compiled into `wxcore` on both platforms whatever the build options:
  - `src/common/quantize.cpp`, the `wxQuantize` colour quantizer, is guarded only by
    `wxUSE_IMAGE`. Its header reads `Licence: wxWindows licence + JPEG library licence` and
    `This file is part of the Independent JPEG Group's software`. SPDX `IJG`. Its conditions are in
    `src/jpeg/README` ("LEGAL ISSUES"): the documentation of a binary distribution must say that the
    software is based in part on the work of the Independent JPEG Group. Task 4's notices file
    carries that README.
  - `src/common/xpmdecod.cpp` and `src/common/imagxpm.cpp`, the XPM decoder used for icons on GTK,
    contain ImageMagick (2001) and libXpm (GROUPE BULL) code. Both are under the MIT text with an
    added "the name ... shall not be used in advertising" clause, which is SPDX `X11` in form.
    Debian labels libXpm "Expat".

  Recommendation: allow both for static linking. Both are permissive and have no copyleft; the
  M3 PNG exporter case (D4) applies in the same way.

  If U1 is declined, add only `libpng-2.0` in this task. Task 4 then stops at its licence step for
  a new decision: no wx build can avoid `quantize.cpp`, so D13 (the GUI toolkit) would reopen.

**Files:**
- Modify `rawreel-workspace/cmake/DvvThirdParty.cmake`:
  - the `CONFIGS` keyword, its checks and the recorded property;
  - the new function `dvv_third_party_build_configs`;
  - the header comment.
- Modify `rawreel-workspace/deps/CMakeLists.txt`: per-configuration build and install commands.
- Modify `rawreel-workspace/cmake/DvvSpdx.cmake`: `libpng-2.0`, plus `IJG` and `X11` if U1 is
  accepted, join `DVV_SPDX_PERMISSIVE`.
- Modify `rawreel-workspace/tests/spdx/test_spdx.cmake`, `tests/spdx/test_third_party_gate.cmake`
  and `tests/deps/test_superbuild_scripts.cmake`: the new cases.

**Interfaces:**

The new header comment in `DvvThirdParty.cmake`:

```cmake
# dvv_third_party(<name> VERSION <v> URL <url> SHA256 <hash> SPDX <expression>
#                 LINKAGE static|runtime|tool LICENSE_FILES <path or glob in archive>...
#                 [ONLY_ON Windows|Linux] [DEPENDS <name>...] [CONFIGS Debug|Release...]
#                 [PREBUILT | CMAKE_ARGS <args>...] [POST_INSTALL <script>])
# ...
#   CONFIGS        configurations built and installed, in this order (default Release). Only a
#                  multi-config generator (Visual Studio) can build more than Release; not with
#                  PREBUILT.
```

The new function (in `DvvThirdParty.cmake`, after `dvv_third_party`):

```cmake
# dvv_third_party_build_configs(<name> <multi_config> <out_var>): the configurations to build and
# install for <name>, in order. A single-config generator builds Release only, so a declaration
# with other CONFIGS fails configuration there.
function(dvv_third_party_build_configs name multi_config out_var)
    get_property(configs GLOBAL PROPERTY "DVV_THIRD_PARTY_${name}_CONFIGS")
    if(NOT multi_config AND NOT configs STREQUAL "Release")
        list(JOIN configs " " shown)
        message(FATAL_ERROR "dvv_third_party(${name}): CONFIGS ${shown} needs a multi-config "
                            "generator (Visual Studio); a single-config generator builds Release only")
    endif()
    set(${out_var} ${configs} PARENT_SCOPE)
endfunction()
```

Changes inside `dvv_third_party`:
- `cmake_parse_arguments`: the multi-value list becomes `"LICENSE_FILES;DEPENDS;CMAKE_ARGS;CONFIGS"`.
- After the `PREBUILT`/`CMAKE_ARGS` check:

```cmake
    if(arg_PREBUILT AND arg_CONFIGS)
        message(FATAL_ERROR "dvv_third_party(${name}): PREBUILT takes no CONFIGS")
    endif()
    foreach(config IN LISTS arg_CONFIGS)
        if(NOT config MATCHES "^(Debug|Release)$")
            message(FATAL_ERROR "dvv_third_party(${name}): CONFIGS takes Debug and Release only, "
                                "not '${config}'")
        endif()
    endforeach()
    if(NOT arg_CONFIGS)
        set(arg_CONFIGS Release)
    endif()
```

- The recorded keys become `VERSION URL SHA256 SPDX LINKAGE LICENSE_FILES DEPENDS PREBUILT
  CMAKE_ARGS POST_INSTALL CONFIGS`.

`deps/CMakeLists.txt`:
- Read the generator kind once, before the loop:
  `get_property(multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)`.
- The non-`PREBUILT` branch builds its command lists like this:

```cmake
    dvv_third_party_build_configs(${name} "${multi_config}" configs)
    set(build_command "")
    set(install_command "")
    foreach(config IN LISTS configs)
        if(NOT build_command STREQUAL "")
            list(APPEND build_command COMMAND)
            list(APPEND install_command COMMAND)
        endif()
        list(APPEND build_command "${CMAKE_COMMAND}" --build <BINARY_DIR> --config ${config}
                                  --parallel)
        list(APPEND install_command "${CMAKE_COMMAND}" --install <BINARY_DIR> --config ${config})
    endforeach()
    ExternalProject_Add(${name}
        ...unchanged arguments...
        BUILD_COMMAND ${build_command}
        INSTALL_COMMAND ${install_command}
                COMMAND ${post_install})
```

- For every Release-only declaration, the generated build and install commands are byte-for-byte
  the old ones (`--config Release --parallel` and `--install ... --config Release`). Existing
  dependencies are therefore not rebuilt.
- `common_args` keeps `-DCMAKE_BUILD_TYPE=Release`; multi-config generators ignore it.
- Update the file's header comment: "Release, plus Debug where a declaration lists `CONFIGS Debug
  Release` (Visual Studio only)".

`DvvSpdx.cmake`:

```cmake
set(DVV_SPDX_PERMISSIVE
    MIT MIT-0 BSD-2-Clause BSD-3-Clause BSD-2-Clause-Patent Apache-2.0 Zlib BSL-1.0 ISC
    Unlicense CC0-1.0 Unicode-3.0 Unicode-DFS-2016 libpng-2.0 IJG X11)
# libpng-2.0 (not OSI-approved, zlib-style; user decision D4), IJG and X11 (code bundled in
# wxWidgets' wxcore; user decision U1) are allowed for static linking (spec §17).
```

If U1 is declined, leave out `IJG X11` and the half of the comment that mentions them.

- Produces for Task 4: the `CONFIGS` keyword, the default `Release`, and an allowlist that accepts
  both wx expressions:
  - Windows: `LGPL-2.0-or-later WITH WxWindows-exception-3.1 AND IJG AND X11 AND Unicode-DFS-2016
    AND Zlib AND libpng-2.0`.
  - Linux: `LGPL-2.0-or-later WITH WxWindows-exception-3.1 AND IJG AND X11 AND Unicode-DFS-2016 AND
    Zlib`.

**Tests first.**

`tests/spdx/test_spdx.cmake`, added before the `dvv_spdx_evaluate("MIT AND GPL-2.0-only" ...)`
block:

```cmake
# wxWidgets (M1b): libpng (version 2 licence; user decision D4); the JPEG-derived quantizer (IJG)
# and the XPM decoder (X11) bundled in wxcore (user decision U1).
expect("libpng-2.0" static 1)
expect("IJG" static 1)
expect("X11" static 1)
set(wx_windows_spdx "LGPL-2.0-or-later WITH WxWindows-exception-3.1 AND IJG AND X11 AND \
Unicode-DFS-2016 AND Zlib AND libpng-2.0")
set(wx_linux_spdx "LGPL-2.0-or-later WITH WxWindows-exception-3.1 AND IJG AND X11 AND \
Unicode-DFS-2016 AND Zlib")
expect("${wx_windows_spdx}" static 1)
expect("${wx_linux_spdx}" static 1)
expect("LGPL-2.0-or-later AND Zlib" static 0)
```

The last case checks that the exception still matters. If U1 is declined, keep only the
`libpng-2.0` case.

`tests/spdx/test_third_party_gate.cmake`:
- After the `PREBUILT takes no CMAKE_ARGS` case:

```cmake
set(code "set(CMAKE_SYSTEM_NAME Windows)")
decl(code probe MIT static CONFIGS Debug Release)
expect("${code}" 0 "accepted")
set(code "set(CMAKE_SYSTEM_NAME Windows)")
decl(code probe MIT static CONFIGS Debug RelWithDebInfo)
expect("${code}" 1 "dvv_third_party\\(probe\\): CONFIGS takes Debug and Release only, not 'RelWithDebInfo'")
set(code "set(CMAKE_SYSTEM_NAME Windows)")
decl(code probe MIT tool PREBUILT CONFIGS Debug Release)
expect("${code}" 1 "dvv_third_party\\(probe\\): PREBUILT takes no CONFIGS")
```

- In the "accepted set" block:
  - `user` gets `CONFIGS Debug Release`:
    `decl(code user MIT static DEPENDS headers tool CONFIGS Debug Release CMAKE_ARGS -DA=1 -DB=<INSTALL_DIR>)`.
  - `user_CONFIGS` and `headers_CONFIGS` join the `foreach(key ...)` list.
  - Two expectations are added:
    `expect("${code}" 0 "user_CONFIGS=<Debug.Release>")` and
    `expect("${code}" 0 "headers_CONFIGS=<Release>")`.
  - The existing `user_CMAKE_ARGS=<-DA=1.-DB=<INSTALL_DIR>>` expectation shows that `CONFIGS`
    before `CMAKE_ARGS` parses correctly.

`tests/deps/test_superbuild_scripts.cmake`, a new section before the final message:

```cmake
# dvv_third_party_build_configs(): a single-config generator builds Release only.
set(probe "${WORK_DIR}/configs/probe.cmake")
file(WRITE "${probe}" "include(\"${CMAKE_CURRENT_LIST_DIR}/../../cmake/DvvThirdParty.cmake\")
set(CMAKE_SYSTEM_NAME Windows)
set(configs_arg \"\")
if(PROBE_CONFIGS STREQUAL \"both\")
    set(configs_arg CONFIGS Debug Release)
elseif(PROBE_CONFIGS STREQUAL \"release\")
    set(configs_arg CONFIGS Release)
endif()
dvv_third_party(probe VERSION 1.0 URL https://example.com/p.tgz
    SHA256 0000000000000000000000000000000000000000000000000000000000000000
    SPDX MIT LINKAGE static LICENSE_FILES LICENSE \${configs_arg})
dvv_third_party_build_configs(probe \"\${MULTI_CONFIG}\" configs)
message(STATUS \"configs=<\${configs}>\")
")
run_script(1 "dvv_third_party\\(probe\\): CONFIGS Debug Release needs a multi-config generator"
           "${probe}" -DMULTI_CONFIG=0 -DPROBE_CONFIGS=both)
run_script(0 "configs=<Release>" "${probe}" -DMULTI_CONFIG=0 -DPROBE_CONFIGS=none)
run_script(0 "configs=<Release>" "${probe}" -DMULTI_CONFIG=0 -DPROBE_CONFIGS=release)
run_script(0 "configs=<Debug;Release>" "${probe}" -DMULTI_CONFIG=1 -DPROBE_CONFIGS=both)
```

**Which tests fail first:**
- `test_spdx`: `'libpng-2.0' (static) gave 0, expected 1`.
- `test_third_party_gate`: the `CONFIGS` declaration is `rejected: ... unexpected 'CONFIGS;Debug;Release'`.
- `test_superbuild_scripts`: `Unknown CMake command "dvv_third_party_build_configs"`.

- [ ] **Step 1: Check the precondition.** The dispatch message states D4 and U1. If it does not,
  stop and ask the controller.
- [ ] **Step 2:** Add the test cases above, then run the tests and see them fail:

```powershell
cmake -P rawreel-workspace/tests/spdx/test_spdx.cmake
cmake -P rawreel-workspace/tests/spdx/test_third_party_gate.cmake
cmake "-DWORK_DIR=_build/scratch" -P rawreel-workspace/tests/deps/test_superbuild_scripts.cmake
```

  Expected: each fails with the message given above.
- [ ] **Step 3:** Change `DvvSpdx.cmake`, then rerun `test_spdx`. Expected:
  `-- SPDX tests passed`.
- [ ] **Step 4:** Change `DvvThirdParty.cmake` and `deps/CMakeLists.txt`, then rerun all three
  tests and `cmake -P rawreel-workspace/tests/deps/test_versions.cmake`, on Windows and in WSL:

```powershell
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd rawreel-workspace && cmake -P tests/spdx/test_spdx.cmake && cmake -P tests/spdx/test_third_party_gate.cmake && cmake -DWORK_DIR=../_build/scratch -P tests/deps/test_superbuild_scripts.cmake && cmake -P tests/deps/test_versions.cmake'
```

  Expected on both platforms:
  - `SPDX tests passed`, `third-party gate tests passed`, `superbuild script tests passed` and
    `versions tests passed`.
  - The versions lists are unchanged:
    - `Windows: nlohmann_json, doctest, Vulkan-Headers, VulkanMemoryAllocator, vk-bootstrap, slang,
      Vulkan-Utility-Libraries, SPIRV-Headers, SPIRV-Tools, Vulkan-ValidationLayers`.
    - The same names for Linux.
- [ ] **Step 5: Prove that existing dependencies are not rebuilt.** The bootstrap stamp changes
  because `DvvThirdParty.cmake` and `DvvSpdx.cmake` changed. Run both bootstraps in the background,
  in parallel:

```powershell
pwsh -NoProfile -File rawreel-workspace/bootstrap.ps1 -Toolchain msvc *> _build/m1b-t3-bootstrap-msvc.log; "exit=$LASTEXITCODE"
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd rawreel-workspace && ./bootstrap.sh --toolchain clang > ../_build/m1b-t3-bootstrap-clang.log 2>&1; echo exit=$?'
```

  Expected: `exit=0` for both, with `Third-party dependencies for <toolchain> installed.` at the end
  of the log, within a few minutes. The superbuild reconfigures, and the external projects only
  check their stamps. If a log shows a dependency being rebuilt (its configure output or compiler
  lines), the generated commands differ from the old ones: compare `build_command` with the
  previous `BUILD_COMMAND` and fix the difference.
- [ ] **Step 6: Commit (two commits) and lint**

```powershell
git -C rawreel-workspace add cmake/DvvSpdx.cmake tests/spdx/test_spdx.cmake
git -C rawreel-workspace commit -m "build: allow libpng-2.0, IJG and X11 for static linking" -m "wxWidgets bundles libpng on Windows (user decision D4) and compiles a JPEG-derived colour quantizer (IJG) and an ImageMagick/libXpm-derived XPM decoder (X11) into wxcore on every platform (user decision U1)." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git -C rawreel-workspace add cmake/DvvThirdParty.cmake deps/CMakeLists.txt tests/spdx/test_third_party_gate.cmake tests/deps/test_superbuild_scripts.cmake
git -C rawreel-workspace commit -m "build: let a third-party dependency build Debug and Release (CONFIGS)" -m "A declaration lists the configurations to build and install, in order (default Release). Visual Studio builds each; a single-config generator rejects anything but Release at configure time. Release-only dependencies get the same commands as before, so nothing is rebuilt." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
cmake -DREPO=rawreel-workspace -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake
```

  If U1 was declined, the first commit's message names only `libpng-2.0` and D4.

**Expected results:** the four workspace script tests pass on Windows and Linux, and both bootstraps
succeed without rebuilding a dependency. No preset run is needed: the task changes no C++ and no
build of a repository. The workspace workflows run once for the group, in Task 4.

**Notes:**
- Design §9.1, §8.3, D13, D14; decisions log D4.
- Spec §17 records D4 and U1 in Task 13b.
- The `X11` id is the plan writer's reading of the licence text (MIT plus a no-endorsement clause).
  If the user prefers to treat the text as `MIT`, as Debian does, drop `X11` from the list and from
  the Task 4 expressions. The tests change the same way.

---

### Task 4: wxWidgets 3.3.4 and the WSI validation layer (rawreel-workspace)

**Goal:**
- Build wxWidgets 3.3.4 as a static library in the third-party prefix: Debug and Release with
  MSVC, Release with Clang/GTK3. Settle R1 with a consumer test.
- Build the Linux validation layer with its window-system support.
- Add a Linux prerequisite preflight to `bootstrap.sh`.
- Record build times and the wx configuration.

**Precondition:** Task 3 is committed and U1 was accepted. If U1 was declined, do Steps 1 to 4 and
the VVL and preflight parts only, then stop and report.

**Files:**
- Modify `rawreel-workspace/deps/versions.cmake`:
  - append the two wxWidgets declarations;
  - replace the three `-DBUILD_WSI_*_SUPPORT=OFF` arguments of Vulkan-ValidationLayers with
    explicit ON: `-DBUILD_WSI_XCB_SUPPORT=ON -DBUILD_WSI_XLIB_SUPPORT=ON
    -DBUILD_WSI_XLIB_XRANDR_SUPPORT=ON -DBUILD_WSI_WAYLAND_SUPPORT=ON` (explicit, so a reused cache
    is overridden and a missing `-dev` package fails the configure; `--no-warn-unused-cli` from
    `deps/CMakeLists.txt` covers Windows), and update its comment;
  - in the header comment, change "Later milestones append wxWidgets here" to "Later milestones
    append their dependencies here".
- Modify `rawreel-workspace/bootstrap.sh`: the prerequisite preflight.
- Create `rawreel-workspace/tests/deps/wx-consumer/CMakeLists.txt` and
  `rawreel-workspace/tests/deps/wx-consumer/main.cpp`: the consumer program.
- Create `rawreel-workspace/tests/deps/run-wx-consumer.cmake`: builds and runs the consumer in each
  configuration.
- Modify `rawreel-workspace/CMakeLists.txt`: the CTest `workspace.wx_consumer`.

**Interfaces:**
- Produces, in `_third_party/msvc`:
  - the static wx libraries in `lib/vc_x64_lib`: `wxbase33u[d]`, `wxmsw33u[d]_core` and the other
    enabled libraries, plus `wxzlib[d]` and `wxpng[d]`;
  - the package `lib/cmake/wxWidgets-3.3/wxWidgetsConfig.cmake`, with imported targets `wx::core`
    and `wx::base` (aliases of `wx::wxcore` and `wx::wxbase`). Debug libraries use `/MDd`, Release
    `/MD`.
- Produces, in `_third_party/clang`: the Release-only `libwx_gtk3u_core-3.3.a`,
  `libwx_baseu-3.3.a` and so on, with the same package.
- Consumers use `find_package(wxWidgets 3.3 CONFIG REQUIRED COMPONENTS core base)` and link
  `wx::core wx::base`. Tasks 12a and 12b rely on exactly this.
- CTest `workspace.wx_consumer`:
  - configuration Debug only (`CONFIGURATIONS Debug`);
  - created only when the prefix holds wx and `DVV_SANITIZE` is empty;
  - 0 on success, a CMake `FATAL_ERROR` otherwise.
- `bootstrap.sh`: missing Linux development packages give exit 1 before any clone or build, with the
  apt line printed.
- Consumes from Task 3: `CONFIGS`, and the allowlist with `libpng-2.0`, `IJG` and `X11`.

**The licence audit** (plan writer, 2026-10-23, wx 3.3.4 sources). It extends the design's
expressions, as design D14 allows ("If Task 4 finds more bundled code, its ids are added"):
- `Unicode-DFS-2016`, already on the allowlist. `src/common/languageinfo.cpp` and
  `include/wx/private/lang_*.h` are compiled into wxbase. They hold tables that `misc/languages/README`
  calls "derived from public data of the Unicode organization". The text in
  `misc/languages/data/unicode/unicode-license.txt` ("UNICODE, INC. LICENSE AGREEMENT - DATA FILES
  AND SOFTWARE") is SPDX `Unicode-DFS-2016`.
- `IJG` (`src/common/quantize.cpp`) and `X11` (`src/common/xpmdecod.cpp`, `imagxpm.cpp`): see Task 3,
  U1.
- `src/common/extended.c` (Ken Turkowski's IEEE extended-precision routines, a custom notice) is
  compiled only with `wxUSE_APPLE_IEEE`. Plan choice: `-DwxUSE_APPLE_IEEE=OFF` on both platforms.
  The only user is `wxDataStream`'s 80-bit double format, which rawreel does not use, and turning it
  off keeps a licence without an SPDX id out.
- `src/gtk/eggtrayicon.c` (LGPL) is in `GTK2_SRC` only, so it is not built for GTK3.
- The scan covered `src/{common,msw,generic,gtk,unix,aui,html,propgrid,ribbon,xml}` and
  `include/wx`. No other file carries a licence line other than the wxWindows licence or a foreign
  copyright.
- `src/png/LICENSE` (libpng 1.6.59) contains the version-2 licence and, for code from versions up to
  1.6.35, the version-1 text (SPDX `Libpng`). Plan choice: `libpng-2.0` only, as vcpkg and Conan
  declare libpng 1.6.36+. This is listed as a doubt for the user.

**Code: `deps/versions.cmake` (append at the end).** The escaped generator expression in the
Windows declaration is tested, see the notes:

```cmake
# wxWidgets (spec §2, §10.2), static. MSVC builds Debug and Release (Debug builds of rawreel link the
# /MDd libraries); Debug objects carry their debug information (/Z7, no separate PDB, so a Debug
# consumer link shows no LNK4099), Release objects none. Regex, XML, image formats other than PNG,
# web, media and GL parts are off: not needed through v1, and they keep the licence small. wxcore
# always contains a JPEG-derived colour quantizer (IJG), an ImageMagick/libXpm-derived XPM decoder
# (X11) and Unicode CLDR-derived language tables (Unicode-DFS-2016); nanosvg is bundled (Zlib).
# wxUSE_APPLE_IEEE is off: its extended.c has a licence without an SPDX id.
# The genex is escaped once, because ExternalProject evaluates its arguments in the superbuild:
# wx receives $<$<CONFIG:Debug>:Embedded>.
dvv_third_party(wxWidgets
    VERSION 3.3.4
    URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.4/wxWidgets-3.3.4.tar.bz2
    SHA256 815c16cafa4fcbfcf25c5c0a3418309c62257b8a3b6f91d85a1367a134f98c98
    SPDX "LGPL-2.0-or-later WITH WxWindows-exception-3.1 AND IJG AND X11 AND Unicode-DFS-2016 \
AND Zlib AND libpng-2.0"
    LINKAGE static
    LICENSE_FILES docs/licence.txt docs/lgpl.txt src/jpeg/README src/common/xpmdecod.cpp
                  misc/languages/data/unicode/unicode-license.txt 3rdparty/nanosvg/LICENSE.txt
                  src/zlib/LICENSE src/png/LICENSE
    ONLY_ON Windows
    CONFIGS Debug Release
    CMAKE_ARGS -DwxBUILD_TOOLKIT=msw -DwxBUILD_SHARED=OFF -DwxBUILD_USE_STATIC_RUNTIME=OFF
               -DwxBUILD_SAMPLES=OFF -DwxBUILD_TESTS=OFF -DwxBUILD_DEMOS=OFF -DwxBUILD_INSTALL=ON
               -DwxBUILD_CXX_STANDARD=17
               -DwxUSE_ZLIB=builtin -DwxUSE_LIBPNG=builtin -DwxUSE_LIBJPEG=OFF -DwxUSE_LIBTIFF=OFF
               -DwxUSE_LIBWEBP=OFF -DwxUSE_LUNASVG=OFF -DwxUSE_NANOSVG=builtin -DwxUSE_REGEX=OFF
               -DwxUSE_EXPAT=OFF -DwxUSE_XML=OFF -DwxUSE_XRC=OFF -DwxUSE_RICHTEXT=OFF
               -DwxUSE_STC=OFF -DwxUSE_WEBVIEW=OFF -DwxUSE_MEDIACTRL=OFF -DwxUSE_OPENGL=OFF
               -DwxUSE_WEBREQUEST=OFF -DwxUSE_SECRETSTORE=OFF -DwxUSE_APPLE_IEEE=OFF
               "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=$<1:$><$<1:$><CONFIG:Debug$<ANGLE-R>:Embedded$<ANGLE-R>")

# Linux: Release only (no debug ABI split in libstdc++), GTK 3, the system's zlib and libpng. Options
# that wx would otherwise detect from installed -dev packages are pinned, so CI runners (which have
# more of them) build the same library as WSL.
dvv_third_party(wxWidgets
    VERSION 3.3.4
    URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.4/wxWidgets-3.3.4.tar.bz2
    SHA256 815c16cafa4fcbfcf25c5c0a3418309c62257b8a3b6f91d85a1367a134f98c98
    SPDX "LGPL-2.0-or-later WITH WxWindows-exception-3.1 AND IJG AND X11 AND Unicode-DFS-2016 \
AND Zlib"
    LINKAGE static
    LICENSE_FILES docs/licence.txt docs/lgpl.txt src/jpeg/README src/common/xpmdecod.cpp
                  misc/languages/data/unicode/unicode-license.txt 3rdparty/nanosvg/LICENSE.txt
    ONLY_ON Linux
    CMAKE_ARGS -DwxBUILD_TOOLKIT=gtk3 -DwxBUILD_SHARED=OFF
               -DwxBUILD_SAMPLES=OFF -DwxBUILD_TESTS=OFF -DwxBUILD_DEMOS=OFF -DwxBUILD_INSTALL=ON
               -DwxBUILD_CXX_STANDARD=17
               -DwxUSE_ZLIB=sys -DwxUSE_LIBPNG=sys -DwxUSE_LIBJPEG=OFF -DwxUSE_LIBTIFF=OFF
               -DwxUSE_LIBWEBP=OFF -DwxUSE_LUNASVG=OFF -DwxUSE_NANOSVG=builtin -DwxUSE_REGEX=OFF
               -DwxUSE_EXPAT=OFF -DwxUSE_XML=OFF -DwxUSE_XRC=OFF -DwxUSE_RICHTEXT=OFF
               -DwxUSE_STC=OFF -DwxUSE_WEBVIEW=OFF -DwxUSE_MEDIACTRL=OFF -DwxUSE_OPENGL=OFF
               -DwxUSE_WEBREQUEST=OFF -DwxUSE_SECRETSTORE=OFF -DwxUSE_APPLE_IEEE=OFF
               -DwxUSE_LIBSDL=OFF -DwxUSE_LIBNOTIFY=OFF -DwxUSE_LIBMSPACK=OFF
               -DwxUSE_LIBGNOMEVFS=OFF -DwxUSE_GLCANVAS_EGL=OFF -DwxUSE_SPELLCHECK=OFF
               -DwxUSE_LIBLZMA=OFF -DwxUSE_WEBREQUEST_CURL=OFF
               -DwxUSE_XTEST=OFF -DwxUSE_APPINDICATOR=OFF -DwxUSE_DETECT_SM=OFF)
```

Note (plan choice): the Linux declaration also pins `wxUSE_XTEST`, `wxUSE_APPINDICATOR` and
`wxUSE_DETECT_SM` OFF. wx would otherwise detect them from libxtst-dev,
libayatana-appindicator3-dev and libsm-dev, which are not in the §9.2 list but may be on CI
runners. That is the design's own rule ("options wx would otherwise detect are pinned"), applied
to the three detections that `build/cmake/init.cmake` performs and the design list missed. The
remaining detections come from GTK 3's own dependency closure (cairo, fontconfig/pangoft2,
gtk-unix-print, wayland-client/protocols, xkbcommon) or from glibc (iconv), so they are the same on
WSL and on CI.

The new comment above Vulkan-ValidationLayers replaces the old one (its three `BUILD_WSI_*=OFF`
arguments are replaced with explicit ON: `-DBUILD_WSI_XCB_SUPPORT=ON -DBUILD_WSI_XLIB_SUPPORT=ON
-DBUILD_WSI_XLIB_XRANDR_SUPPORT=ON -DBUILD_WSI_WAYLAND_SUPPORT=ON`):

```cmake
# Needs Python 3 to build. UPDATE_DEPS would clone every dependency at configure time. Bundled in
# layers/external: parallel_hashmap (Apache-2.0), xxhash.h (BSD-2-Clause), inplace_function.h
# (BSL-1.0) and VMA (MIT). On Linux the layer is built with its XCB, Xlib (+XRandR) and Wayland WSI
# support (pkg-config modules xcb, x11, xrandr and wayland-client), so it validates surfaces and
# swapchains; Windows always has Win32 WSI. The WSI options are explicit ON, so a build tree
# configured without them is reconfigured. The post-install step lets the Linux manifest find its
# library.
```

**Code: `bootstrap.sh` preflight.** Insert it after the option parsing and before the
`workspace_repo=` line, so that it runs before any clone or build, also when the prefix is up to
date:

```sh
# Linux build prerequisites (spec §13.4): GTK 3 for wxWidgets, the window-system libraries of the
# Vulkan validation layer, zlib and libpng. Checked before anything is cloned or built.
apt_line="sudo apt-get install -y libgtk-3-dev pkgconf libx11-dev libxcb1-dev libxrandr-dev \
libwayland-dev zlib1g-dev libpng-dev xvfb xauth"
missing=""
if command -v pkg-config >/dev/null 2>&1; then
    for module in gtk+-3.0 x11 xcb xrandr wayland-client zlib libpng; do
        pkg-config --exists "$module" || missing="$missing $module"
    done
else
    missing=" pkg-config"
fi
if [ -n "$missing" ]; then
    echo "Missing Linux development packages (pkg-config:$missing). Install them with:" >&2
    echo "  $apt_line" >&2
    exit 1
fi
```

**Code: the consumer.** `tests/deps/wx-consumer/CMakeLists.txt`:

```cmake
# Builds a small wx console program against the third-party prefix with the dvv compiler settings
# (warnings as errors, C++23, NOMINMAX/WIN32_LEAN_AND_MEAN/UNICODE on MSVC): the same way rawreel-gui
# will consume wx. Driven by run-wx-consumer.cmake.
cmake_minimum_required(VERSION 4.2)
project(wx_consumer LANGUAGES CXX)
include("${DVV_CMAKE_DIR}/DvvCompilerOptions.cmake")
find_package(wxWidgets 3.3 CONFIG REQUIRED COMPONENTS core base)
add_executable(wx_consumer main.cpp)
target_link_libraries(wx_consumer PRIVATE wx::core wx::base)
dvv_set_compiler_options(wx_consumer)
```

`tests/deps/wx-consumer/main.cpp`:

```cpp
// wx include first, as in rawreel's GUI sources (design R11).
#include <wx/wx.h>

#include <wx/image.h>
#include <wx/init.h>

#include <cstdio>

int main(int argc, char** argv) {
    wxInitializer initializer(argc, argv);  // console initialisation: no display needed
    if (!initializer.IsOk()) {
        std::fputs("wxInitializer failed\n", stderr);
        return 1;
    }
    // "Grüße": UTF-8 in, UTF-8 out.
    const wxString text = wxString::FromUTF8("Gr\xC3\xBC\xC3\x9F" "e");
    if (text.length() != 5 || text.utf8_string() != "Gr\xC3\xBC\xC3\x9F" "e") {
        return 2;
    }
    wxImage image(4, 2);  // references wxcore, so its link dependencies are real (design R3)
    image.SetRGB(1, 1, 10, 20, 30);
    if (!image.IsOk() || image.GetGreen(1, 1) != 20) {
        return 3;
    }
    std::puts("wx consumer ok");
    return 0;
}
```

`tests/deps/run-wx-consumer.cmake`:

```cmake
# Builds tests/deps/wx-consumer against the third-party prefix in each configuration and runs it
# (M1b design §8.3, R1). A Debug link that reports LNK4099 (missing wx PDB) fails.
# Called by workspace.wx_consumer with -DSOURCE_DIR, -DBUILD_DIR, -DGENERATOR, -DPLATFORM,
# -DCXX_COMPILER, -DPREFIX_PATH (comma-separated), -DDVV_CMAKE_DIR and -DCONFIGS (comma-separated).
cmake_minimum_required(VERSION 4.2)
foreach(variable IN ITEMS SOURCE_DIR BUILD_DIR GENERATOR CXX_COMPILER PREFIX_PATH DVV_CMAKE_DIR
                          CONFIGS)
    if("${${variable}}" STREQUAL "")
        message(FATAL_ERROR "pass -D${variable}")
    endif()
endforeach()
string(REPLACE "," ";" prefix_path "${PREFIX_PATH}")
string(REPLACE "," ";" configs "${CONFIGS}")

file(REMOVE_RECURSE "${BUILD_DIR}")
file(MAKE_DIRECTORY "${BUILD_DIR}")
# A list value cannot pass through a command line intact; an initial-cache file keeps it whole.
set(initial_cache "${BUILD_DIR}/initial-cache.cmake")
file(WRITE "${initial_cache}" "set(CMAKE_PREFIX_PATH \"${prefix_path}\" CACHE STRING \"\")\n")
set(configure "${CMAKE_COMMAND}" -C "${initial_cache}" -S "${SOURCE_DIR}" -B "${BUILD_DIR}/tree"
    -G "${GENERATOR}" "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}" "-DDVV_CMAKE_DIR=${DVV_CMAKE_DIR}")
if(PLATFORM)
    list(APPEND configure -A "${PLATFORM}")
endif()
execute_process(COMMAND ${configure} RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "configuring the wx consumer failed (${status})")
endif()

foreach(config IN LISTS configs)
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}/tree" --config ${config}
                    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    message("${out}${err}")
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "building the wx consumer (${config}) failed (${status})")
    endif()
    if("${out}${err}" MATCHES "LNK4099")
        message(FATAL_ERROR "the ${config} link reports LNK4099: wx debug information is missing")
    endif()
    file(GLOB executable "${BUILD_DIR}/tree/${config}/wx_consumer"
                         "${BUILD_DIR}/tree/${config}/wx_consumer.exe")
    if(NOT executable)
        message(FATAL_ERROR "wx_consumer (${config}) not found")
    endif()
    execute_process(COMMAND ${executable} RESULT_VARIABLE status OUTPUT_VARIABLE out)
    if(NOT status EQUAL 0 OR NOT out MATCHES "wx consumer ok")
        message(FATAL_ERROR "wx_consumer (${config}) failed (${status}): ${out}")
    endif()
    message(STATUS "wx consumer ${config}: ok")
endforeach()
```

The workspace presets use multi-config generators (Visual Studio, Ninja Multi-Config), which is
why the executable lies in `<tree>/<config>/`.

`CMakeLists.txt` (workspace), appended after the existing `add_test` calls:

```cmake
# wxWidgets consumer (M1b design §8.3): Debug and Release consumers on MSVC and a Debug consumer on
# Clang (against the Release-only wx) build, link and run against the prefix. Not under sanitizers.
find_file(DVV_WX_CONFIG wxWidgetsConfig.cmake PATHS ${CMAKE_PREFIX_PATH}
          PATH_SUFFIXES lib/cmake/wxWidgets-3.3 NO_DEFAULT_PATH NO_CACHE)
if(DVV_WX_CONFIG AND NOT DVV_SANITIZE)
    if(MSVC)
        set(wx_consumer_configs "Debug,Release")
    else()
        set(wx_consumer_configs "Debug")
    endif()
    string(REPLACE ";" "," wx_prefix_path "${CMAKE_PREFIX_PATH}")
    add_test(NAME workspace.wx_consumer CONFIGURATIONS Debug
             COMMAND ${CMAKE_COMMAND}
                     -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/deps/wx-consumer
                     -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/wx-consumer
                     -DGENERATOR=${CMAKE_GENERATOR}
                     -DPLATFORM=${CMAKE_GENERATOR_PLATFORM}
                     -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
                     -DPREFIX_PATH=${wx_prefix_path}
                     -DDVV_CMAKE_DIR=${DVV_CMAKE_DIR}
                     -DCONFIGS=${wx_consumer_configs}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/deps/run-wx-consumer.cmake)
    set_tests_properties(workspace.wx_consumer PROPERTIES TIMEOUT 900)
endif()
```

**Tests first:**
- `run-wx-consumer.cmake` is the test. Write it and the consumer first, then run it by hand against
  the current prefix, which has no wx yet. It fails with `Could not find a package configuration
  file provided by "wxWidgets"`.
- `test_versions.cmake` then lists `wxWidgets` last for both platforms.
- The bootstrap preflight's failure path is tested by hiding every pkg-config module.

- [ ] **Step 1:** Write the three consumer files and the `CMakeLists.txt` addition. Then run the
  test by hand and see it fail (`DVV_CMAKE_DIR` is the sibling's module folder):

```powershell
cmake -DSOURCE_DIR=rawreel-workspace/tests/deps/wx-consumer -DBUILD_DIR=_build/scratch/wx-consumer "-DGENERATOR=Visual Studio 18 2026" -DPLATFORM=x64 -DCXX_COMPILER=cl "-DPREFIX_PATH=$PWD/_third_party/msvc" "-DDVV_CMAKE_DIR=$PWD/dvv-base/cmake" -DCONFIGS=Debug -P rawreel-workspace/tests/deps/run-wx-consumer.cmake
```

  Expected: `Could not find a package configuration file provided by "wxWidgets"`, then
  `configuring the wx consumer failed`.
- [ ] **Step 2:** Append the wx declarations to `deps/versions.cmake`, replace the three VVL
  `BUILD_WSI_*=OFF` arguments with explicit ON (`-DBUILD_WSI_XCB_SUPPORT=ON
  -DBUILD_WSI_XLIB_SUPPORT=ON -DBUILD_WSI_XLIB_XRANDR_SUPPORT=ON -DBUILD_WSI_WAYLAND_SUPPORT=ON`)
  and update both comments. Then run:

```powershell
cmake -P rawreel-workspace/tests/deps/test_versions.cmake
```

  Expected:
  - `Windows: nlohmann_json, doctest, Vulkan-Headers, VulkanMemoryAllocator, vk-bootstrap, slang,
    Vulkan-Utility-Libraries, SPIRV-Headers, SPIRV-Tools, Vulkan-ValidationLayers, wxWidgets`.
  - The same names for Linux.
  - `-- versions tests passed`.
- [ ] **Step 3:** Add the preflight to `bootstrap.sh`. Test the failure path; no build starts,
  because `PKG_CONFIG_LIBDIR` points at an empty folder:

```powershell
wsl -d Ubuntu-24.04 -e bash -lc 'mkdir -p /tmp/dvv-no-pc && cd rawreel-workspace && PKG_CONFIG_LIBDIR=/tmp/dvv-no-pc PKG_CONFIG_PATH= ./bootstrap.sh --no-clone; echo exit=$?'
```

  Expected:
  - `Missing Linux development packages (pkg-config: gtk+-3.0 x11 xcb xrandr wayland-client zlib libpng).`
  - The apt line.
  - `exit=1`.
- [ ] **Step 4:** Rerun the script tests on both platforms:

```powershell
cmake -P rawreel-workspace/tests/spdx/test_spdx.cmake
cmake -P rawreel-workspace/tests/spdx/test_third_party_gate.cmake
cmake "-DWORK_DIR=_build/scratch" -P rawreel-workspace/tests/deps/test_superbuild_scripts.cmake
cmake -P rawreel-workspace/tests/deps/test_versions.cmake
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd rawreel-workspace && cmake -P tests/spdx/test_spdx.cmake && cmake -P tests/spdx/test_third_party_gate.cmake && cmake -DWORK_DIR=../_build/scratch -P tests/deps/test_superbuild_scripts.cmake && cmake -P tests/deps/test_versions.cmake'
```

  Expected: all pass.
- [ ] **Step 5: Bootstrap both toolchains, timed, in the background and in parallel.**
  `UseMultiToolTask` and MSBuild FileTracker need short paths: keep the workspace at a short path, as
  in M0 finding 4.

```powershell
$sw = [Diagnostics.Stopwatch]::StartNew(); pwsh -NoProfile -File rawreel-workspace/bootstrap.ps1 -Toolchain msvc *> _build/m1b-t4-bootstrap-msvc.log; "exit=$LASTEXITCODE elapsed=$($sw.Elapsed)"
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd rawreel-workspace && start=$(date +%s) && ./bootstrap.sh --toolchain clang > ../_build/m1b-t4-bootstrap-clang.log 2>&1; echo "exit=$? elapsed=$(( $(date +%s) - start ))s"'
```

  Expected: `exit=0` for both.
  - Windows builds wx Debug and Release. The Vulkan-ValidationLayers configure reruns because its
    arguments changed, but nothing is recompiled.
  - Linux builds wx Release and rebuilds the validation layer with WSI defines, about 10 to 13
    minutes for the layer.
  - Record both elapsed times (design estimate: +8 min MSVC, +5 min Linux for wx) and the prefix
    sizes:
    - `"{0:N0} MB" -f ((Get-ChildItem _third_party/msvc -Recurse -File | Measure-Object Length -Sum).Sum / 1MB)`;
    - `du -sh _third_party/clang` in WSL.

  If a step fails:
  - An unknown wx option, or a configure error: fix the option and record the change.
  - `LNK`, `C1`, or other compile errors inside wx: report them to the controller with the log
    excerpt.
- [ ] **Step 6: Check what was built and installed**

```powershell
Get-ChildItem _third_party/msvc/lib/vc_x64_lib -Filter *.lib | ForEach-Object Name
Select-String -Path _build/third-party/msvc/wxWidgets-prefix/src/wxWidgets-build/CMakeCache.txt -Pattern '^wx(USE|BUILD)_(ZLIB|LIBPNG|NANOSVG|REGEX|EXPAT|LIBJPEG|LIBTIFF|LIBWEBP|LUNASVG|APPLE_IEEE|TOOLKIT|SHARED)[:=]' | ForEach-Object Line
Select-String -Path _third_party/msvc/THIRD_PARTY_NOTICES.txt -Pattern '^wxWidgets 3.3.4|^--- (src/jpeg/README|src/common/xpmdecod.cpp|misc/languages/data/unicode/unicode-license.txt|src/png/LICENSE) ---' | ForEach-Object Line
wsl -d Ubuntu-24.04 -e bash -lc 'ls _third_party/clang/lib/libwx*; grep -E "^wxUSE_[A-Z0-9_]+:" _build/third-party/clang/wxWidgets-prefix/src/wxWidgets-build/CMakeCache.txt > _build/m1b-t4-wx-linux-options.txt; grep -E "^wxUSE_(XTEST|APPINDICATOR|DETECT_SM|SECRETSTORE|LIBNOTIFY|LIBSDL|SPELLCHECK|MEDIACTRL|WEBVIEW|WAYLAND|GTKPRINT|CAIRO|PRIVATE_FONTS|LIBICONV|ZLIB|LIBPNG|NANOSVG|APPLE_IEEE)[:=]" _build/m1b-t4-wx-linux-options.txt; grep -c "" _third_party/clang/THIRD_PARTY_NOTICES.txt'
```

Expected on Windows:
- The bundled third-party libraries are exactly `wxzlib.lib`, `wxzlibd.lib`, `wxpng.lib` and
  `wxpngd.lib`.
- There is no `wxregex*`, `wxexpat*`, `wxjpeg*`, `wxtiff*`, `wxscintilla*`, `wxlexilla*`,
  `wxwebp*` or `wxlunasvg*`.
- Each wx library has a Release and a `d`-suffixed Debug file.
- The cache shows `wxUSE_ZLIB=builtin`, `wxUSE_LIBPNG=builtin`, `wxUSE_NANOSVG=builtin`,
  `wxUSE_APPLE_IEEE=OFF` and `wxBUILD_TOOLKIT=msw`.
- The notices list `wxWidgets 3.3.4` and the four named licence files.

Expected on Linux:
- `libwx_baseu-3.3.a`, `libwx_gtk3u_core-3.3.a` and the other enabled libraries, with no
  `libwxregex*`, `libwxexpat*`, `libwxzlib*` or `libwxpng*`.
- `wxUSE_ZLIB=sys`, `wxUSE_LIBPNG=sys`, `wxUSE_NANOSVG=builtin`.
- Every pinned option is `OFF`.

Then compare the full option dump `_build/m1b-t4-wx-linux-options.txt` with the CI sequence: every
`wxUSE_*` that is `ON` and not pinned must depend only on GTK 3's dependency closure, on glibc, or on
a package in the preflight's apt line. Any other one is pinned to its WSL value. Then the
declaration changes, and Steps 5 and 6 repeat for Linux. Report the list of auto-detected options
with their values. The CI apt list itself changes in Task 13a.
- [ ] **Step 7: Validation layer with WSI, on the WSLg display**

```powershell
wsl -d Ubuntu-24.04 -e bash -lc 'VK_ADD_LAYER_PATH=$PWD/_third_party/clang/share/vulkan/explicit_layer.d VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation vulkaninfo --summary 2>&1 | grep -iE "VUID|error|Invalid VkSurfaceKHR" || echo NO-LAYER-ERRORS; VK_ADD_LAYER_PATH=$PWD/_third_party/clang/share/vulkan/explicit_layer.d VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation timeout 60 vulkaninfo 2>&1 | grep -A2 "Presentable Surfaces"'
```

  Expected:
  - `NO-LAYER-ERRORS`. Before this task the layer printed `Invalid VkSurfaceKHR` errors, because it
    had no Xlib support.
  - `Presentable Surfaces:` with `VK_KHR_xlib_surface`, so the layer is loaded and passes surface
    queries.
  - If `xvfb-run` exists, repeat the first command under
    `xvfb-run -a -s "-screen 0 1920x1080x24"`.
- [ ] **Step 8: Run the consumer by hand** (the Step 1 command for msvc with
  `-DCONFIGS=Debug,Release`), and in WSL:

```powershell
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cmake -DSOURCE_DIR=rawreel-workspace/tests/deps/wx-consumer -DBUILD_DIR=_build/scratch/wx-consumer-clang "-DGENERATOR=Ninja Multi-Config" -DCXX_COMPILER=clang++-19 -DPREFIX_PATH=$PWD/_third_party/clang -DDVV_CMAKE_DIR=$PWD/dvv-base/cmake -DCONFIGS=Debug -P rawreel-workspace/tests/deps/run-wx-consumer.cmake'
```

  Expected: `wx consumer Debug: ok` and `wx consumer Release: ok` on Windows, with no `LNK4099` in
  the Debug output; `wx consumer Debug: ok` on Linux. If a check fails:
  - **R1, the Windows Debug build finds the Release `setup.h` or libraries, or links against the
    wrong runtime (LNK2038):** apply the design's fallback. Add a second Windows declaration,
    `wxWidgets-debug`, with `CONFIGS Debug` and `-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>/wx-debug`
    after the common arguments. The Release declaration keeps `CONFIGS Release`. The consumer then
    selects the prefix with `$<CONFIG>`, and the same mechanism is recorded for rawreel (Task 12a).
    Report it.
  - **R11, `<wx/wx.h>` fails to compile under the dvv definitions** (`WIN32_LEAN_AND_MEAN`,
    `NOMINMAX`, `UNICODE`): report it with the error. The fix belongs to Task 12a (design R11); do
    not change dvv-base here.
  - **R3, the Clang link reports undefined GTK, X11 or pthread symbols:** in the consumer, add
    `find_package(PkgConfig REQUIRED)`, `pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)`
    and `find_package(X11 REQUIRED)`, and link `PkgConfig::GTK3 X11::X11` on Linux. Report that R3
    is confirmed; rawreel does the same in Task 12b.
  - **The Linux consumer crashes at start without a display:** run it with `DISPLAY=:0`. CI runs
    under `xvfb-run` from Task 13a. Report the case.
- [ ] **Step 9: Commit, in two commits**

```powershell
git -C rawreel-workspace add bootstrap.sh deps/versions.cmake
git -C rawreel-workspace commit -m "build: add wxWidgets 3.3.4 and the validation layer's Linux WSI support" -m "wxWidgets is built static: Debug and Release with MSVC (Debug objects carry /Z7 debug information), Release with Clang and GTK 3; image formats other than PNG, regex, XML, web, media and GL are off, and the Linux options wx would detect are pinned. Its licence expression covers the code bundled in wxcore (IJG, X11, Unicode-DFS-2016, Zlib) and, on Windows, libpng. The Linux validation layer now builds its XCB, Xlib and Wayland support. bootstrap.sh checks the Linux development packages first and prints the apt line." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git -C rawreel-workspace add CMakeLists.txt tests/deps/wx-consumer tests/deps/run-wx-consumer.cmake
git -C rawreel-workspace commit -m "test: build and run a wxWidgets consumer against the third-party prefix" -m "Debug and Release on MSVC (no LNK4099), Debug on Clang against the Release-only wx, with the dvv compiler settings." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
cmake -DREPO=rawreel-workspace -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake
```

- [ ] **Step 10: Full matrix for the workspace group (Tasks 3 and 4), on the final commit.** Run in
  the background, one preset per call; Windows and WSL in parallel:

```powershell
Push-Location rawreel-workspace; cmake --workflow --preset vs2026 *> ../_build/m1b-t4-vs2026.log; "exit=$LASTEXITCODE"; Pop-Location
Push-Location rawreel-workspace; cmake --workflow --preset vs2026-asan *> ../_build/m1b-t4-vs2026-asan.log; "exit=$LASTEXITCODE"; Pop-Location
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd rawreel-workspace && for p in ninja-clang ninja-clang-asan ninja-clang-tsan; do DVV_VK_VALIDATION=sync DVV_VK_VALIDATION_REQUIRED=1 DVV_REQUIRE_GPU=1 cmake --workflow --preset $p > ../_build/m1b-t4-$p.log 2>&1; echo "$p exit=$?"; grep -E "tests passed|tests failed" ../_build/m1b-t4-$p.log; done'
```

**Expected results:**
- Every run ends with `100% tests passed`. `workspace.wx_consumer` runs in the Debug test step of
  `vs2026` and `ninja-clang`, and is absent from the sanitizer presets and the Release test steps.
- All other counts are as before Task 3, plus this one test.
- On Linux the GPU tests run on lavapipe, with synchronization validation through the rebuilt WSI
  layer, and report zero validation messages.
- On Windows the GPU tests run on NVIDIA.
- `privacy-lint: clean`.

No C++ changed in a dvv repository, so there is no clang-format or clang-tidy run. The consumer is
covered by `/W4 /WX` and `-Werror` through `dvv_set_compiler_options`.
- [ ] **Step 11: Report to the controller.** The controller copies this into the plan's "Changes
  during execution":
  - the bootstrap times and prefix sizes (R14);
  - the R1 result: one prefix, or the fallback;
  - the R3 and R11 results;
  - the Linux auto-detected option list and any extra pins;
  - the confirmation of the licence audit;
  - for Task 13a: the apt list without `libx11-xcb-dev` (D5) and the preflight line.

**Notes:**
- Design §9.1, §9.2, §8.3, D13, D14, R1, R3, R10, R11, R14; spec §15 (the deferred VVL note,
  removed in Task 13b).
- **Note (plan choice): Debug-only `/Z7`.** Design §9.1 passes
  `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded`. That would also put debug information into every
  Release object (CMP0141 applies a plain value to all configurations), which would grow the cached
  prefix for nothing (R14). The escaped generator expression gives wx
  `$<$<CONFIG:Debug>:Embedded>`.

  ExternalProject evaluates generator expressions in its arguments once, in the superbuild,
  `$<1:$>` yields `$` and `$<ANGLE-R>` yields `>`. The plan writer tested this on 2026-10-23 with
  CMake 4.3 and Visual Studio 18 2026: an ExternalProject built with `CONFIGS`-style
  `--config Debug`/`--config Release` commands. The inner cache held
  `$<$<CONFIG:Debug>:Embedded>`, the Debug `.vcxproj` had `DebugInformationFormat OldStyle` (`/Z7`)
  and the Release one had none. The quoted argument passes MSBuild's custom-build step intact.
- **Fallback (R10):** if wx 3.3.4 fails to build with these options for reasons outside our
  control, pin 3.3.3. Take its SHA256 from the GitHub release digest, confirm it by download, and
  repeat the licence audit for that version.
- `LICENSE_FILES src/common/xpmdecod.cpp` copies the whole source file into the notices. It is the
  only place that holds the ImageMagick and libXpm notices, which both require their text to go with
  copies.
- The CI cache key formula is unchanged. The first CI run after publishing rebuilds the prefix once.

### Task 5: dvv-platform skeleton and `DynamicLibrary`

**Goal:** Create the local repository `dvv-platform` (branch `m1b`, no remote) with the standard dvv skeleton, a package test and `dvv::platform::DynamicLibrary` (Win32 and POSIX, with a system-directory-only search mode), and wire it into the workspace build.

**Files:**
- Create (repository `dvv-platform`, sibling of `dvv-base`):
  - copied from dvv-gpu unchanged: `.clang-format`, `.clang-tidy` (its `HeaderFilterRegex` already covers private headers in `src/`), `.gitattributes`, `.gitignore`, `LICENSE`, `CMakePresets.json`;
  - `.github/workflows/ci.yml` (reusable CI, `repo: dvv-platform`), `README.md`;
  - `CMakeLists.txt` (targets, install, package), `cmake/dvv_platformConfig.cmake.in`;
  - `include/dvv/platform/dynamic_library.h` (public API);
  - `src/dynamic_library.cpp` (platform-independent part: argument checks, move, unload), `src/dynamic_library_detail.h` (the three platform functions), `src/dynamic_library_win32.cpp` (`LoadLibraryExW`), `src/dynamic_library_posix.cpp` (`dlopen`);
  - `tests/CMakeLists.txt`, `tests/main.cpp` (doctest main with leak check), `tests/test_module.cpp` (the `MODULE` library under test), `tests/test_dynamic_library.cpp`;
  - `tests/package/CMakeLists.txt`, `tests/package/main.cpp`, `tests/package/run-package-test.cmake`.
- Modify (rawreel-workspace): `repos.txt`, `cmake/dvv-versions.cmake`, `cmake/DvvPresets.json`, `CMakeLists.txt`, `README.md`.

**Interfaces:**
- Consumes: Task 0 (branch `m1b` in every existing repository; bootstrap state), dvv-base package `dvv_base` 0.1 (`DVV_CMAKE_DIR`, `dvv_set_compiler_options`, `DVV_TEST_ENVIRONMENT`, `DVV_SANITIZE`, option `DVV_BUILD_TESTS`, `dvv::Result`, `dvv::makeError`, `dvv::Errc`, `dvv::finishWithLeakCheck`, `dvv::LifetimeRegistry`), doctest 2.5 from the third-party prefix. `./rawreel-workspace/bootstrap.ps1 -NoClone` (copies the commit identity and hooks path into every repository in `repos.txt`).
- Produces (CMake): project `dvv_platform` 0.1.0; target `dvv_platform` (STATIC, alias `dvv::platform`, `EXPORT_NAME platform`, PUBLIC `dvv::base`, PRIVATE `${CMAKE_DL_LIBS}`); package `dvv_platform` (`SameMinorVersion`, `lib/cmake/dvv_platform`, `find_dependency(dvv_base 0.1)`); CTest `dvv_platform.unit` and `dvv_platform.package` (resource lock `dvv_build_tree`, not under sanitizers). Workspace: `dvv-platform` in `repos.txt` (before `dvv-gpu`), `FetchContent_Declare(dvv_platform ...)`, preset cache variable `FETCHCONTENT_SOURCE_DIR_DVV_PLATFORM`, `dvv_platform` in `FetchContent_MakeAvailable` and `dvv_check_siblings`.
- Produces (C++, `include/dvv/platform/dynamic_library.h`, used by Task 6's surface test and Task 7's loader):

```cpp
#pragma once

// Run-time libraries (spec §10.4): a move-only RAII handle, exempt from the managed-object rules of
// spec §9.4 like other handles. The Vulkan loader is opened with LibrarySearch::SystemDirectory on
// Windows, so a vulkan-1.dll planted next to the executable or in the current directory is never
// loaded.

#include <cstdint>
#include <string>
#include <string_view>

#include "dvv/base/error.h"

namespace dvv::platform {

enum class LibrarySearch : std::uint8_t {
    Default,          // Windows LoadLibraryExW(name, nullptr, 0); Linux dlopen(RTLD_NOW|RTLD_LOCAL)
    SystemDirectory,  // Windows LOAD_LIBRARY_SEARCH_SYSTEM32 only (not app dir, cwd, PATH);
                      // Linux as Default. `name` must be a bare file name.
};

// Move-only RAII handle (spec §9.4 "handles"); the destructor unloads unless keepLoaded().
class DynamicLibrary {
public:
    // UTF-8 name. Errc::Unsupported "'<name>' could not be loaded: <system message>"; Errc::Internal
    // for an empty name, a name containing '\0', or a SystemDirectory name containing '/' or '\\'.
    [[nodiscard]] static Result<DynamicLibrary> open(std::string_view name,
                                                     LibrarySearch search = LibrarySearch::Default);
    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    ~DynamicLibrary();

    [[nodiscard]] void* symbol(const char* name) const noexcept;  // nullptr when missing or empty
    template <class Function>
    [[nodiscard]] Function function(const char* name) const noexcept {
        return reinterpret_cast<Function>(symbol(name));
    }
    void keepLoaded() noexcept;  // never unloaded, also after destruction (the Vulkan loader)
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

private:
    DynamicLibrary(void* handle, std::string name) noexcept;
    void release() noexcept;  // unloads unless empty or kept loaded

    void* handle_ = nullptr;
    std::string name_;
    bool keep_loaded_ = false;
};

}  // namespace dvv::platform
```

- Private (`src/dynamic_library_detail.h`, namespace `dvv::platform::detail`), implemented once per platform:
  - `[[nodiscard]] void* loadLibrary(const std::string& name, LibrarySearch search, std::string& error);` returns the handle, or `nullptr` with `error` set to the system's message;
  - `[[nodiscard]] void* findSymbol(void* handle, const char* name) noexcept;`
  - `void unloadLibrary(void* handle) noexcept;`

Note (plan choice): the design lists only `src/dynamic_library_{win32,posix}.cpp`. The platform-independent part (checks, move, release) lives once in `src/dynamic_library.cpp` behind the small private header `src/dynamic_library_detail.h`, so it is compiled and clang-tidied on Linux too.

Note: the repository stays local. Do not add a remote, create a GitHub repository or push. Because `repos.txt` will list `dvv-platform`, the workspace commit of this task must not reach GitHub before `dvv-platform` exists there (publishing is a later, user-approved step).

**Behaviour:**
- `open` checks, in order: empty name or a name containing `'\0'` → `Errc::Internal` "DynamicLibrary::open: the library name is empty or contains a null character"; `SystemDirectory` and the name contains `'/'` or `'\\'` → `Errc::Internal` "DynamicLibrary::open: '<name>' must be a bare file name with LibrarySearch::SystemDirectory" (on both platforms); then `detail::loadLibrary`; `nullptr` → `Errc::Unsupported` "'<name>' could not be loaded: <error>".
- Windows: the name is converted with `MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS)`; invalid UTF-8 gives the error text "the name is not valid UTF-8" (so `Unsupported`). `SystemDirectory` passes `LOAD_LIBRARY_SEARCH_SYSTEM32`, `Default` passes 0. The system message is `FormatMessageW` of `GetLastError()`, converted to UTF-8, trailing whitespace and `.` trimmed, followed by ` (error <code>)`. The thread error mode is `SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX` during the call (no "missing DLL" dialog box), then restored.
- Linux: `dlopen(name, RTLD_NOW | RTLD_LOCAL)` for both modes; the error is `dlerror()` (or "unknown error").
- `symbol`: `nullptr` for an empty (moved-from) handle; otherwise `GetProcAddress` / `dlsym`.
- Move construction and assignment take handle, name and the keep-loaded flag and leave the source empty (`symbol()` returns `nullptr`, `name()` is empty). Move assignment first releases the target's own library. `release()` (destructor, move assignment) calls `detail::unloadLibrary` unless the handle is empty or `keepLoaded()` was called.

**Code** (platform calls):

`src/dynamic_library.cpp`:

```cpp
#include "dvv/platform/dynamic_library.h"

#include <format>
#include <utility>

#include "dynamic_library_detail.h"

namespace dvv::platform {

Result<DynamicLibrary> DynamicLibrary::open(std::string_view name, LibrarySearch search) {
    if (name.empty() || name.find('\0') != std::string_view::npos) {
        return makeError(Errc::Internal,
                         "DynamicLibrary::open: the library name is empty or contains a null "
                         "character");
    }
    if (search == LibrarySearch::SystemDirectory &&
        name.find_first_of("/\\") != std::string_view::npos) {
        return makeError(Errc::Internal,
                         std::format("DynamicLibrary::open: '{}' must be a bare file name with "
                                     "LibrarySearch::SystemDirectory",
                                     name));
    }
    std::string owned(name);
    std::string error;
    void* const handle = detail::loadLibrary(owned, search, error);
    if (handle == nullptr) {
        return makeError(Errc::Unsupported,
                         std::format("'{}' could not be loaded: {}", owned, error));
    }
    return DynamicLibrary(handle, std::move(owned));
}

DynamicLibrary::DynamicLibrary(void* handle, std::string name) noexcept
    : handle_(handle), name_(std::move(name)) {}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)),
      name_(std::exchange(other.name_, {})),
      keep_loaded_(std::exchange(other.keep_loaded_, false)) {}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
    if (this != &other) {
        release();
        handle_ = std::exchange(other.handle_, nullptr);
        name_ = std::exchange(other.name_, {});
        keep_loaded_ = std::exchange(other.keep_loaded_, false);
    }
    return *this;
}

DynamicLibrary::~DynamicLibrary() {
    release();
}

void* DynamicLibrary::symbol(const char* name) const noexcept {
    return handle_ != nullptr ? detail::findSymbol(handle_, name) : nullptr;
}

void DynamicLibrary::keepLoaded() noexcept {
    keep_loaded_ = true;
}

void DynamicLibrary::release() noexcept {
    if (handle_ != nullptr && !keep_loaded_) {
        detail::unloadLibrary(handle_);
    }
    handle_ = nullptr;
}

}  // namespace dvv::platform
```

`src/dynamic_library_win32.cpp`:

```cpp
#include <windows.h>

#include <climits>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "dynamic_library_detail.h"

namespace dvv::platform::detail {

namespace {

// UTF-8 to UTF-16; nullopt for invalid UTF-8.
std::optional<std::wstring> widen(std::string_view text) {
    if (text.size() > static_cast<std::size_t>(INT_MAX)) {
        return std::nullopt;
    }
    const int length = static_cast<int>(text.size());
    const int size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
    if (size <= 0) {
        return std::nullopt;
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, wide.data(), size);
    return wide;
}

// The system's text for an error code, in UTF-8, with the code.
std::string systemMessage(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                            FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0,
                                        nullptr);
    std::string text;
    if (length > 0 && buffer != nullptr) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length),
                                             nullptr, 0, nullptr, nullptr);
        if (size > 0) {
            text.resize(static_cast<std::size_t>(size));
            WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), text.data(), size,
                                nullptr, nullptr);
        }
    }
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' ||
                             text.back() == '.')) {
        text.pop_back();
    }
    return std::format("{} (error {})", text.empty() ? "unknown error" : text, code);
}

}  // namespace

void* loadLibrary(const std::string& name, LibrarySearch search, std::string& error) {
    const auto wide = widen(name);
    if (!wide) {
        error = "the name is not valid UTF-8";
        return nullptr;
    }
    const DWORD flags =
        search == LibrarySearch::SystemDirectory ? LOAD_LIBRARY_SEARCH_SYSTEM32 : DWORD{0};
    // No "missing DLL" dialog box while loading.
    DWORD old_mode = 0;
    const bool mode_set =
        SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &old_mode) != FALSE;
    HMODULE const module = LoadLibraryExW(wide->c_str(), nullptr, flags);
    const DWORD code = module == nullptr ? GetLastError() : DWORD{0};
    if (mode_set) {
        SetThreadErrorMode(old_mode, nullptr);
    }
    if (module == nullptr) {
        error = systemMessage(code);
    }
    return module;
}

void* findSymbol(void* handle, const char* name) noexcept {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
}

void unloadLibrary(void* handle) noexcept {
    FreeLibrary(static_cast<HMODULE>(handle));
}

}  // namespace dvv::platform::detail
```

`src/dynamic_library_posix.cpp`:

```cpp
#include <dlfcn.h>

#include <string>

#include "dynamic_library_detail.h"

namespace dvv::platform::detail {

void* loadLibrary(const std::string& name, LibrarySearch /*search*/, std::string& error) {
    void* const handle = dlopen(name.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* const text = dlerror();
        error = text != nullptr ? text : "unknown error";
    }
    return handle;
}

void* findSymbol(void* handle, const char* name) noexcept {
    return dlsym(handle, name);
}

void unloadLibrary(void* handle) noexcept {
    dlclose(handle);
}

}  // namespace dvv::platform::detail
```

`CMakeLists.txt` (Task 6 extends it):

```cmake
cmake_minimum_required(VERSION 4.2)
project(dvv_platform VERSION 0.1.0 DESCRIPTION "dvv platform layer" LANGUAGES CXX)

option(DVV_BUILD_TESTS "Build the dvv tests" ${PROJECT_IS_TOP_LEVEL})

# dvv-base: inside the workspace build cmake/dvv-versions.cmake has declared it first, so this
# declaration is ignored there (spec §3.1). Stand-alone builds take it from the sibling folder
# (FETCHCONTENT_SOURCE_DIR_DVV_BASE in the workspace presets), from an installed package or from Git.
include(FetchContent)
FetchContent_Declare(dvv_base
    GIT_REPOSITORY "https://github.com/BarabashkaD/dvv-base.git"
    GIT_TAG main
    FIND_PACKAGE_ARGS 0.1)
# A stand-alone build tests only this repository (the normal variable hides the cache entry from
# the dependencies' option()); as a subproject the parent's value is kept, and the workspace
# build tests every repository.
if(PROJECT_IS_TOP_LEVEL)
    set(DVV_BUILD_TESTS OFF)
endif()
FetchContent_MakeAvailable(dvv_base)
if(PROJECT_IS_TOP_LEVEL)
    unset(DVV_BUILD_TESTS)
endif()
if(NOT DVV_CMAKE_DIR)
    message(FATAL_ERROR "dvv_base did not define DVV_CMAKE_DIR")
endif()
include("${DVV_CMAKE_DIR}/DvvCompilerOptions.cmake")
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(WIN32)
    set(platform_sources src/dynamic_library_win32.cpp)
else()
    set(platform_sources src/dynamic_library_posix.cpp)
endif()
add_library(dvv_platform STATIC
    src/dynamic_library.cpp
    ${platform_sources})
add_library(dvv::platform ALIAS dvv_platform)
set_target_properties(dvv_platform PROPERTIES EXPORT_NAME platform)
target_include_directories(dvv_platform PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
# dlopen (Linux); CMAKE_DL_LIBS is empty on Windows.
target_link_libraries(dvv_platform PUBLIC dvv::base PRIVATE ${CMAKE_DL_LIBS})
dvv_set_compiler_options(dvv_platform)

install(TARGETS dvv_platform EXPORT dvv_platformTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
set(config_dir ${CMAKE_INSTALL_LIBDIR}/cmake/dvv_platform)
install(EXPORT dvv_platformTargets NAMESPACE dvv:: DESTINATION ${config_dir})
configure_package_config_file(cmake/dvv_platformConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/dvv_platformConfig.cmake INSTALL_DESTINATION ${config_dir})
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/dvv_platformConfigVersion.cmake
    COMPATIBILITY SameMinorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/dvv_platformConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/dvv_platformConfigVersion.cmake
    DESTINATION ${config_dir})

if(DVV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

`cmake/dvv_platformConfig.cmake.in`:

```cmake
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)
find_dependency(dvv_base 0.1)

include("${CMAKE_CURRENT_LIST_DIR}/dvv_platformTargets.cmake")

check_required_components(dvv_platform)
```

`tests/CMakeLists.txt`:

```cmake
find_package(doctest 2.5 REQUIRED CONFIG)

# A tiny run-time library for the DynamicLibrary tests. It is built into the same output directory
# as the test executable (both are defined here), so LibrarySearch::SystemDirectory must not find
# it while the default search does.
add_library(dvv_platform_test_module MODULE test_module.cpp)
dvv_set_compiler_options(dvv_platform_test_module)

add_executable(dvv_platform_tests
    main.cpp
    test_dynamic_library.cpp)
target_link_libraries(dvv_platform_tests PRIVATE dvv::platform doctest::doctest)
# doctest otherwise only forward-declares std::ostream, which breaks printing std types.
target_compile_definitions(dvv_platform_tests PRIVATE
    DOCTEST_CONFIG_USE_STD_HEADERS
    "DVV_PLATFORM_TEST_MODULE=\"$<TARGET_FILE:dvv_platform_test_module>\""
    "DVV_PLATFORM_TEST_MODULE_NAME=\"$<TARGET_FILE_NAME:dvv_platform_test_module>\"")
add_dependencies(dvv_platform_tests dvv_platform_test_module)
dvv_set_compiler_options(dvv_platform_tests)
add_test(NAME dvv_platform.unit COMMAND dvv_platform_tests)
# The timeout turns a hang into a failure long before the CI job limit.
set_tests_properties(dvv_platform.unit PROPERTIES
    TIMEOUT 300
    ENVIRONMENT DVV_LEAK_CHECK=fail
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")

# Package test (spec §3.1): install, then build a consumer with find_package(dvv_platform) and
# another with dvv_platform in-tree. The whole build tree is installed, so the prefix also holds
# dvv_base when it is a subproject here. Skipped for sanitizer builds, whose consumers would need the
# same runtime flags.
if(NOT DVV_SANITIZE)
    string(REPLACE ";" "," deps_prefix_path "${CMAKE_PREFIX_PATH}")
    add_test(NAME dvv_platform.package
             COMMAND ${CMAKE_COMMAND}
                     -DBUILD_DIR=${CMAKE_BINARY_DIR}
                     -DCONFIG=$<CONFIG>
                     -DPREFIX=${CMAKE_CURRENT_BINARY_DIR}/package-prefix
                     -DSOURCE_DIR=${dvv_platform_SOURCE_DIR}
                     -DBASE_SOURCE_DIR=${dvv_base_SOURCE_DIR}
                     -DCONSUMER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/package
                     -DCONSUMER_BUILD=${CMAKE_CURRENT_BINARY_DIR}/package-consumer
                     -DGENERATOR=${CMAKE_GENERATOR}
                     -DPLATFORM=${CMAKE_GENERATOR_PLATFORM}
                     -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
                     -DDEPS_PREFIX_PATH=${deps_prefix_path}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/package/run-package-test.cmake)
    # Package and compile-fail tests of every dvv repository drive the same build tree in the
    # workspace build, so they share one lock name.
    set_tests_properties(dvv_platform.package PROPERTIES RESOURCE_LOCK dvv_build_tree)
endif()
```

`tests/test_module.cpp`:

```cpp
// A tiny run-time library for the DynamicLibrary tests.
#if defined(_WIN32)
#define DVV_TEST_EXPORT __declspec(dllexport)
#else
#define DVV_TEST_EXPORT __attribute__((visibility("default")))
#endif

extern "C" DVV_TEST_EXPORT int dvvTestValue() {
    return 42;
}
```

`tests/main.cpp`: identical to `dvv-registry/tests/main.cpp` (doctest `DOCTEST_CONFIG_IMPLEMENT`, `LifetimeRegistry::instance().setDetailed(true)`, `finishWithLeakCheck(result)` unless `context.shouldExit()`).

Package test: `tests/package/run-package-test.cmake` is `dvv-registry/tests/package/run-package-test.cmake` with `dvv_registry`/`DVV_REGISTRY` replaced by `dvv_platform`/`DVV_PLATFORM` (header comment, the `-DDVV_PLATFORM_SOURCE_DIR=${SOURCE_DIR}` argument of the in-tree consumer). `tests/package/CMakeLists.txt`:

```cmake
# Consumer of dvv_platform; built by run-package-test.cmake. With DVV_PLATFORM_SOURCE_DIR it takes
# dvv_platform in-tree through FetchContent (as the workspace build does), otherwise the installed
# package.
cmake_minimum_required(VERSION 4.2)
project(dvv_platform_package_consumer LANGUAGES CXX)

if(DVV_PLATFORM_SOURCE_DIR)
    include(FetchContent)
    FetchContent_Declare(dvv_platform SOURCE_DIR "${DVV_PLATFORM_SOURCE_DIR}")
    FetchContent_MakeAvailable(dvv_platform)
else()
    find_package(dvv_platform 0.1 REQUIRED)
endif()
if(NOT DVV_CMAKE_DIR)
    message(FATAL_ERROR "dvv_base did not define DVV_CMAKE_DIR")
endif()
include("${DVV_CMAKE_DIR}/DvvCompilerOptions.cmake")

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE dvv::platform)
dvv_set_compiler_options(consumer)
```

`tests/package/main.cpp`:

```cpp
#include <cstdio>

#include "dvv/platform/dynamic_library.h"

int main() {
#if defined(_WIN32)
    const auto library = dvv::platform::DynamicLibrary::open(
        "kernel32.dll", dvv::platform::LibrarySearch::SystemDirectory);
    const char* const symbol = "GetTickCount64";
#else
    const auto library = dvv::platform::DynamicLibrary::open("libc.so.6");
    const char* const symbol = "getpid";
#endif
    if (!library) {
        std::printf("%s\n", library.error().describe().c_str());
        return 1;
    }
    if (library->symbol(symbol) == nullptr) {
        std::printf("%s has no %s\n", library->name().c_str(), symbol);
        return 1;
    }
    std::puts("dvv_platform package works");
    return 0;
}
```

**Tests first** (`tests/test_dynamic_library.cpp`; `using dvv::platform::DynamicLibrary; using dvv::platform::LibrarySearch;`, `using TestFunction = int (*)();`):
1. "a library opened by its path gives its functions": `open(DVV_PLATFORM_TEST_MODULE)` has a value; `name() == DVV_PLATFORM_TEST_MODULE`; `function<TestFunction>("dvvTestValue")` is not null and returns 42; `symbol("dvvTestValue") != nullptr`.
2. "a missing symbol is null": on the opened module `symbol("dvvNoSuchSymbol") == nullptr` and `function<TestFunction>("dvvNoSuchSymbol") == nullptr`.
3. "a missing library is unsupported": `open("dvv-test-no-such-library")` → `Errc::Unsupported`, `message.starts_with("'dvv-test-no-such-library' could not be loaded: ")` and the message is longer than that prefix (a system text follows).
4. "an empty name and a path with SystemDirectory are internal errors": `open("")` and `open("", LibrarySearch::SystemDirectory)` → `Errc::Internal`; `open(std::string_view("a\0b", 3))` → `Internal`; `open("sub/x.dll", SystemDirectory)` and `open("sub\\x.dll", SystemDirectory)` → `Internal` with the message containing "must be a bare file name"; `open("sub/x.dll")` (Default) → `Unsupported`, not `Internal`.
5. (Windows only, inside `#if defined(_WIN32)`) "SystemDirectory loads system libraries but not the application's": the module file exists in the test executable's directory (`GetModuleFileNameW(nullptr, ...)`, parent path `/ DVV_PLATFORM_TEST_MODULE_NAME`, `REQUIRE(std::filesystem::exists(...))`); `open(DVV_PLATFORM_TEST_MODULE_NAME, SystemDirectory)` → `Errc::Unsupported`; `open(DVV_PLATFORM_TEST_MODULE_NAME)` (Default, which searches the application directory) has a value; `open("kernel32.dll", SystemDirectory)` has a value and `symbol("GetTickCount64") != nullptr`.
6. "a moved library leaves an empty handle behind": `DynamicLibrary moved = std::move(*opened)`: `moved.symbol("dvvTestValue") != nullptr`, the source's `symbol(...) == nullptr` and `name().empty()`; move-assigning a second opened module into `moved` keeps `moved` usable (`function` returns 42).
7. (Windows only) "a destroyed library is unloaded": open the module, destroy the object, then `GetModuleHandleW(<UTF-16 of DVV_PLATFORM_TEST_MODULE_NAME>) == nullptr` (the base name, which the loader matches against loaded modules; a `$<TARGET_FILE>` path has forward slashes).
8. "keepLoaded keeps the library loaded after destruction": open the module, take the function pointer, call `keepLoaded()`, destroy the object (scope), then the module is still loaded: Windows `GetModuleHandleW(<UTF-16 of DVV_PLATFORM_TEST_MODULE_NAME>) != nullptr`; Linux `dlopen(DVV_PLATFORM_TEST_MODULE, RTLD_NOW | RTLD_NOLOAD) != nullptr` (then `dlclose` that extra reference); and the function pointer still returns 42.

Note (plan choice): case 7 is Windows-only; on Linux the sanitizer runtimes may keep a `dlclose`d library mapped, which would make the check flaky. A kept-loaded module never unloads again, so case 8 must run after every other case that opens the module: keep it last in the file (doctest runs cases in file order by default).

The first failure is a link error: with only the header and an `src/dynamic_library.cpp` that contains just its `#include`, `dvv_platform_tests` fails to link (`LNK2019` / `undefined reference` to `dvv::platform::DynamicLibrary::open`).

**Steps:**

All commands run from the workspace root (the folder that contains `rawreel-workspace`) in the PowerShell tool unless a step says otherwise.

- [ ] **Step 1: Create the repository and its identity**

```powershell
git init -b m1b dvv-platform
```

Write `rawreel-workspace/repos.txt` so that it reads exactly:

```text
# Repositories that live next to rawreel-workspace, in dependency order. rawreel follows in M1b,
# dvv-media and dvv-components in later milestones.
dvv-base
dvv-registry
dvv-platform
dvv-gpu
```

Run `./rawreel-workspace/bootstrap.ps1 -NoClone`. Expected: `Third-party dependencies for msvc are up to date.` (Task 4 left the prefix up to date; if a third-party build starts instead, stop it and finish Task 4 first). Check: `git -C dvv-platform config user.email` ends with `@users.noreply.github.com`; `git -C dvv-platform config core.hooksPath` is `../rawreel-workspace/tools/githooks`. `repos.txt` is committed in Step 10.

- [ ] **Step 2: Copy the shared files**

```powershell
Copy-Item dvv-gpu/.clang-format, dvv-gpu/.clang-tidy, dvv-gpu/.gitattributes, dvv-gpu/.gitignore, dvv-gpu/LICENSE, dvv-gpu/CMakePresets.json dvv-platform/
Get-Content dvv-platform/CMakePresets.json
```

Expected: the presets file includes `../rawreel-workspace/cmake/DvvPresets.json` and nothing else; `.clang-tidy` has `HeaderFilterRegex: '.*/(include/dvv|src)/.*'`.

- [ ] **Step 3: CI workflow and README**

`dvv-platform/.github/workflows/ci.yml` is `dvv-registry/.github/workflows/ci.yml` with `repo: dvv-platform`. `dvv-platform/README.md`:

````markdown
# dvv-platform

Platform layer of the dvv stack used by rawreel: run-time libraries (`DynamicLibrary`, with a
system-directory-only search for libraries such as the Vulkan loader). C++23; depends on
[dvv-base](https://github.com/BarabashkaD/dvv-base). Nothing in it depends on a GUI toolkit.

Build it through [rawreel-workspace](https://github.com/BarabashkaD/rawreel-workspace), which
provides the presets, the third-party dependencies and the CI workflow:

```sh
cmake --workflow --preset vs2026     # Linux: ninja-clang
```

Licence: MIT.
````

- [ ] **Step 4: Build files and the package test**

Create `CMakeLists.txt`, `cmake/dvv_platformConfig.cmake.in`, `tests/CMakeLists.txt`, `tests/main.cpp`, `tests/test_module.cpp` and the three `tests/package/` files as given above.

- [ ] **Step 5: Write the failing tests**

Create `include/dvv/platform/dynamic_library.h` (above), `tests/test_dynamic_library.cpp` (cases 1–8), and an `src/dynamic_library.cpp` containing only `#include "dvv/platform/dynamic_library.h"`, plus the two platform files containing only `#include "dynamic_library_detail.h"` and `src/dynamic_library_detail.h` (declarations above).

Run: `Push-Location dvv-platform; cmake --workflow --preset vs2026; Pop-Location`
Expected: the build fails linking `dvv_platform_tests` with unresolved `dvv::platform::DynamicLibrary` members.

- [ ] **Step 6: Implement `DynamicLibrary`**

Fill in `src/dynamic_library.cpp`, `src/dynamic_library_win32.cpp` and `src/dynamic_library_posix.cpp` as given above.

Run: `Push-Location dvv-platform; cmake --workflow --preset vs2026; Pop-Location`, then `ctest --test-dir _build/dvv-platform/vs2026 -C Debug -R 'dvv_platform\.unit' -V | Select-String 'test cases'`.
Expected: Debug and Release `100% tests passed, 0 tests failed out of 2` (`dvv_platform.unit`, `dvv_platform.package`, which prints `dvv_platform package works` twice); doctest `test cases: 8 | 8 passed | 0 failed`.

- [ ] **Step 7: Full matrix, lint and commit**

Stage first (the lint lists come from `git ls-files`): `git -C dvv-platform add -A`.

Run these two blocks in parallel (`run_in_background` each; a tool call stops after 10 minutes):

```powershell
Push-Location dvv-platform; cmake --workflow --preset vs2026-asan; Pop-Location
```

```powershell
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd dvv-platform && for p in ninja-clang ninja-clang-asan ninja-clang-tsan; do cmake --workflow --preset $p 2>&1 | grep -E "tests passed|tests failed|Error|error:"; done'
```

Then lint. Git runs only in PowerShell; it writes the file lists that WSL reads:

```powershell
New-Item -ItemType Directory -Force _build/lint | Out-Null
$format = git -C dvv-platform ls-files -- '*.h' '*.cpp'
$tidy = git -C dvv-platform ls-files -- ':(glob)src/**/*.cpp' ':(glob)tests/*.cpp' ':(exclude,glob)**/*_win32.cpp'
[IO.File]::WriteAllText("$PWD/_build/lint/dvv-platform-format.txt", ($format -join "`n") + "`n")
[IO.File]::WriteAllText("$PWD/_build/lint/dvv-platform-tidy.txt", ($tidy -join "`n") + "`n")
```

Run format and tidy in parallel (two background calls):

```powershell
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd dvv-platform && xargs -a ../_build/lint/dvv-platform-format.txt -d "\n" -P 4 -n 8 clang-format --dry-run --Werror && echo FORMAT-OK'
```

```powershell
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd dvv-platform && xargs -a ../_build/lint/dvv-platform-tidy.txt -d "\n" -P 4 -n 1 clang-tidy -p ../_build/dvv-platform/ninja-clang --quiet && echo TIDY-OK'
```

Expected: `vs2026-asan` `100% tests passed, 0 tests failed out of 1` (no package test under sanitizers); Linux `out of 2` for `ninja-clang` (Debug and Release), `out of 1` for ASan and TSan; `FORMAT-OK` and `TIDY-OK` without diagnostics (doctest on Linux: 6 cases, the two Windows-only cases are compiled out). The Win32 sources are not clang-tidied (they are not in the Linux compile database). If clang-format reports a file, run `clang-format -i` on it in WSL and repeat Steps 6–7.

```powershell
git -C dvv-platform add -A
@'
feat: add dvv-platform with DynamicLibrary

The new repository mirrors dvv-registry (presets from rawreel-workspace, reusable CI, package test,
MIT licence) and depends on dvv-base only.

DynamicLibrary is a move-only handle over LoadLibraryExW or dlopen. LibrarySearch::SystemDirectory
loads only from the Windows system directory (LOAD_LIBRARY_SEARCH_SYSTEM32), so a library planted
next to the executable, in the current directory or on PATH is never loaded; it accepts bare file
names only. keepLoaded() keeps a library for the rest of the process.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
'@ | git -C dvv-platform commit -F -
cmake -DREPO=dvv-platform -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake
```

Expected: `-- privacy-lint: clean (commits)`.

- [ ] **Step 8: Wire dvv-platform into the workspace (check first)**

Run: `ctest --test-dir _build/rawreel-workspace/vs2026 -N -C Release | Select-String dvv_platform`
Expected before the change: no output. Also note the total test count the workspace build reports now (`ctest --test-dir _build/rawreel-workspace/vs2026 -N -C Release | Select-String 'Total Tests'`); call it N.

Edit `rawreel-workspace/cmake/dvv-versions.cmake`: after the `FetchContent_Declare(dvv_registry ...)` call insert

```cmake
FetchContent_Declare(dvv_platform
    GIT_REPOSITORY "${DVV_GIT_BASE_URL}/dvv-platform.git"
    GIT_TAG main
    FIND_PACKAGE_ARGS 0.1)
```

Edit `rawreel-workspace/cmake/DvvPresets.json` (`dvv-base-settings` cache variables): after the `"FETCHCONTENT_SOURCE_DIR_DVV_REGISTRY"` line insert

```json
        "FETCHCONTENT_SOURCE_DIR_DVV_PLATFORM": "${sourceDir}/../dvv-platform",
```

(`FETCHCONTENT_SOURCE_DIR_DVV_GPU` stays the last entry, without a trailing comma.)

Edit `rawreel-workspace/CMakeLists.txt`: replace

```cmake
FetchContent_MakeAvailable(dvv_base dvv_registry dvv_gpu)
dvv_check_siblings(dvv_base dvv_registry dvv_gpu)
```

with

```cmake
FetchContent_MakeAvailable(dvv_base dvv_registry dvv_platform dvv_gpu)
dvv_check_siblings(dvv_base dvv_registry dvv_platform dvv_gpu)
```

Edit `rawreel-workspace/README.md`: in the repository table, after the `dvv-registry` row add

```text
| dvv-platform | native windows and Vulkan surfaces, run-time libraries, a native test window | M1b |
```

and in the layout block replace the line `  dvv-registry/` with the two lines `  dvv-registry/` and `  dvv-platform/`.

- [ ] **Step 9: Run the workspace build**

Run (background, about 10 minutes): `Push-Location rawreel-workspace; cmake --workflow --preset vs2026; Pop-Location`, then `ctest --test-dir _build/rawreel-workspace/vs2026 -N -C Release | Select-String 'dvv_platform|Total Tests'`.
Expected: Debug and Release `100% tests passed, 0 tests failed out of <N + 2>`; the listing shows `dvv_platform.unit` and `dvv_platform.package`.

- [ ] **Step 10: Privacy lint and commit the workspace change**

```powershell
git -C rawreel-workspace add repos.txt cmake/dvv-versions.cmake cmake/DvvPresets.json CMakeLists.txt README.md
@'
build: add dvv-platform to the workspace

repos.txt lists it before dvv-gpu (which will use it), dvv-versions.cmake declares it, the presets
point FETCHCONTENT_SOURCE_DIR_DVV_PLATFORM at the sibling folder, and the workspace build makes it
available and checks that it came from there.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
'@ | git -C rawreel-workspace commit -F -
cmake -DREPO=rawreel-workspace -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake
```

Expected: `-- privacy-lint: clean (commits)`. Do not push.

**Expected results:** dvv-platform: 2 CTest tests (1 under sanitizers), 8 doctest cases on Windows and 6 on Linux; workspace build N + 2 tests.

**Commit:** two commits (Steps 7 and 10), messages as given.

**Notes:** Design §3 (dvv-platform targets), §4.3 (`DynamicLibrary`), D3, §6 (handle, exempt from §9.4), §7 (`DynamicLibrary` errors), §8.1 (`DynamicLibrary` tests). Risk: `RTLD_NOLOAD` in case 7 keeps an extra reference; the test must `dlclose` it. If `LOAD_LIBRARY_SEARCH_SYSTEM32` ever returns `ERROR_INVALID_PARAMETER` (only on Windows 7 without KB2533623), that platform is out of scope.

---

### Task 6: Native windows, surfaces and `TestWindow`

**Goal:** Add the native window description (`NativeWindow` variant), Vulkan surface creation for Win32 and Xlib, and the test-only `dvv::platform::testing::TestWindow`, with a display test that creates a real surface on a real window.

**Files:**
- Create (dvv-platform):
  - `include/dvv/platform/native_window.h`, `include/dvv/platform/surface.h`, `include/dvv/platform/testing/test_window.h` (public API);
  - `src/native_window.cpp` (names, default, `windowSystemOf`);
  - `src/surface.cpp` (extension lists, the checks and the dispatch of `createSurface`), `src/surface_detail.h` (per-platform creators), `src/surface_win32.cpp`, `src/surface_xlib.cpp`;
  - `src/testing/test_window_win32.cpp`, `src/testing/test_window_xlib.cpp`;
  - `tests/test_native_window.cpp` (unit), `tests/surface_main.cpp` (display test main with the 77 rules), `tests/surface_support.h`, `tests/surface_support.cpp` (`VulkanInstance` helper), `tests/test_surface.cpp`, `tests/test_test_window.cpp`.
- Modify (dvv-platform): `CMakeLists.txt` (Vulkan headers, X11, sources, `dvv_platform_testing`), `cmake/dvv_platformConfig.cmake.in`, `tests/CMakeLists.txt`, `tests/package/CMakeLists.txt`, `tests/package/main.cpp`, `README.md`.

**Interfaces:**
- Consumes: Task 5 (`dvv_platform`, `DynamicLibrary`, `LibrarySearch`, the package test); Task 1 (`dvv::getEnvironment`, UTF-8 contract); Task 4 (third-party prefix up to date: Vulkan-Headers 1.4.365 package `VulkanHeaders`, target `Vulkan::Headers`); WSL with WSLg (`DISPLAY=:0`) and `libx11-dev`.
- Produces (CMake): `dvv_platform` gains PUBLIC `Vulkan::Headers` and PUBLIC compile definition `VK_NO_PROTOTYPES`, and on Linux the X11 include directory (SYSTEM, PRIVATE: headers only); new target `dvv_platform_testing` (STATIC, alias `dvv::platform_testing`, `EXPORT_NAME platform_testing`, PUBLIC `dvv::platform`, PRIVATE `user32` or `X11::X11`), installed and exported in `dvv_platformTargets`; the package config adds `find_dependency(VulkanHeaders 1.4.365 EXACT CONFIG)` (with FindVulkan disabled) and, on Linux, `find_dependency(X11)`. CTest `dvv_platform.surface` (labels `display`, `SKIP_RETURN_CODE 77`). Task 7 (dvv-gpu) links `dvv::platform`; Task 9 links `dvv::platform_testing` in `dvv_gpu_present_tests`; rawreel links both.
- Produces (C++): `include/dvv/platform/native_window.h` and `include/dvv/platform/surface.h` exactly as in design §4.3 (copied here), and `TestWindow` with one private constructor added:

```cpp
// include/dvv/platform/native_window.h
#pragma once

// Native windows as the platform layer sees them (spec §3.1, §10.4): plain handles, no toolkit.

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

namespace dvv::platform {

enum class WindowSystem : std::uint8_t { Win32, Xlib };
[[nodiscard]] std::string_view windowSystemName(WindowSystem system) noexcept;  // "Win32", "Xlib"
// The window system this build presents to: Win32 on Windows, Xlib on Linux.
[[nodiscard]] WindowSystem defaultWindowSystem() noexcept;

struct PixelSize {  // physical pixels
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    [[nodiscard]] constexpr bool empty() const noexcept { return width == 0 || height == 0; }
    friend constexpr bool operator==(PixelSize, PixelSize) noexcept = default;
};
struct Win32Window {  // HINSTANCE, HWND
    void* hinstance = nullptr;
    void* hwnd = nullptr;
};
struct XlibWindow {  // Display*, XID
    void* display = nullptr;
    unsigned long window = 0;
};
// The caller keeps the window (and, for Xlib, the display, opened after XInitThreads()) alive for
// as long as any surface made from it exists.
using NativeWindow = std::variant<std::monostate, Win32Window, XlibWindow>;
// nullopt for std::monostate.
[[nodiscard]] std::optional<WindowSystem> windowSystemOf(const NativeWindow& window) noexcept;

}  // namespace dvv::platform
```

```cpp
// include/dvv/platform/surface.h
#pragma once

// Vulkan surfaces for native windows (spec §10.4). Vulkan C types only, without prototypes
// (VK_NO_PROTOTYPES): every function comes through vkGetInstanceProcAddr. Results stay VkResult;
// dvv-gpu converts them to dvv::Error in one place.

#include <expected>
#include <span>

#include <vulkan/vulkan_core.h>

#include "dvv/platform/native_window.h"

namespace dvv::platform {

// {"VK_KHR_surface", "VK_KHR_win32_surface"} or {"VK_KHR_surface", "VK_KHR_xlib_surface"}.
[[nodiscard]] std::span<const char* const> surfaceInstanceExtensions(WindowSystem system) noexcept;
// Through vkGetInstanceProcAddr(instance, "vkCreate...SurfaceKHR"). Errors:
// VK_ERROR_EXTENSION_NOT_PRESENT (function unavailable: extension not enabled, or a window system
// this build lacks), VK_ERROR_INITIALIZATION_FAILED (monostate or null handles), else the driver's.
// The caller destroys the surface with vkDestroySurfaceKHR before the window.
[[nodiscard]] std::expected<VkSurfaceKHR, VkResult> createSurface(
    VkInstance instance, PFN_vkGetInstanceProcAddr get_instance_proc_addr,
    const NativeWindow& window) noexcept;

}  // namespace dvv::platform
```

```cpp
// include/dvv/platform/testing/test_window.h  (target dvv::platform_testing; tests only)
#pragma once

#include <memory>
#include <string_view>

#include "dvv/base/error.h"
#include "dvv/platform/native_window.h"

namespace dvv::platform::testing {

// A visible top-level window for display tests: CreateWindowExW, or XCreateSimpleWindow on its own
// Display (XInitThreads() once before the first XOpenDisplay). Move-only; every member is called on
// the thread that created it. Destroy every surface and presenter made from it first.
class TestWindow {
public:
    // Errc::Unsupported "no display ..." (Linux without DISPLAY, or the display cannot be opened);
    // Errc::Internal for an empty size or when the window cannot be created or mapped.
    [[nodiscard]] static Result<TestWindow> create(PixelSize client_size,
                                                   std::string_view title = "dvv test window");
    TestWindow(TestWindow&& other) noexcept;
    TestWindow& operator=(TestWindow&& other) noexcept;
    TestWindow(const TestWindow&) = delete;
    TestWindow& operator=(const TestWindow&) = delete;
    ~TestWindow();

    [[nodiscard]] NativeWindow native() const noexcept;  // std::monostate when moved from
    [[nodiscard]] PixelSize clientSize() const;           // as the window system reports it
    void resize(PixelSize client_size);  // synchronous (SetWindowPos / XResizeWindow + XSync)
    void minimise();  // Win32 SW_MINIMIZE; Xlib unmap (no window manager under Xvfb)
    void restore();   // Win32 SW_RESTORE; Xlib map
    void pump();      // Win32 dispatches pending messages; Xlib XSync and drops queued events

private:
    struct State;
    explicit TestWindow(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

}  // namespace dvv::platform::testing
```

- Private: `src/surface_detail.h` (namespace `dvv::platform::detail`) declares, for the build's own window system only, `std::expected<VkSurfaceKHR, VkResult> createWin32Surface(VkInstance, PFN_vkGetInstanceProcAddr, const Win32Window&) noexcept` (Windows) or `createXlibSurface(VkInstance, PFN_vkGetInstanceProcAddr, const XlibWindow&) noexcept` (Linux).

Note (plan choice): the design's `TestWindow` has no constructor that `create()` can use; a private `explicit TestWindow(std::unique_ptr<State>) noexcept` is added. `src/surface.cpp` and `src/surface_detail.h` are added next to the design's per-platform files (the checks are written once).

**Behaviour:**
- `windowSystemName`: `Win32` → "Win32", `Xlib` → "Xlib". `defaultWindowSystem()`: `Win32` when `_WIN32` is defined, else `Xlib`. `windowSystemOf`: `monostate` → `nullopt`, `Win32Window` → `Win32`, `XlibWindow` → `Xlib`.
- `surfaceInstanceExtensions` works for both systems on both platforms (they are only names): `{VK_KHR_SURFACE_EXTENSION_NAME, "VK_KHR_win32_surface"}`, `{VK_KHR_SURFACE_EXTENSION_NAME, "VK_KHR_xlib_surface"}`, as `static constexpr std::array<const char*, 2>`.
- `createSurface` checks in this order: `monostate` → `VK_ERROR_INITIALIZATION_FAILED`; a window system other than `defaultWindowSystem()` (this build lacks it) → `VK_ERROR_EXTENSION_NOT_PRESENT`, without touching `instance`; `instance == VK_NULL_HANDLE` or `get_instance_proc_addr == nullptr` → `VK_ERROR_INITIALIZATION_FAILED`; then the platform creator: a null `hinstance`/`hwnd` (Win32) or null `display`/`window == 0` (Xlib) → `VK_ERROR_INITIALIZATION_FAILED`; `get_instance_proc_addr(instance, "vkCreateWin32SurfaceKHR" | "vkCreateXlibSurfaceKHR") == nullptr` → `VK_ERROR_EXTENSION_NOT_PRESENT` (the loader returns null for WSI functions of extensions that are not enabled); otherwise the driver's result.
- `TestWindow` (Win32): the process is made per-monitor DPI aware (v2) once, before the first window, so every size is in physical pixels on every thread (the presenter thread queries the surface; rawreel-gui's manifest does the same); a failure of that call (already set) is ignored. One window class `L"dvvTestWindow"` (registered once, `DefWindowProcW` except `WM_GETMINMAXINFO`, which sets `ptMinTrackSize = {1, 1}` so the small client sizes of Task 10 such as 97×61 are possible), style `WS_OVERLAPPEDWINDOW`, no background brush. `create` makes the window, `resize`s it to the client size, shows it with `SW_SHOWNOACTIVATE`, and pumps. `resize` uses `AdjustWindowRectExForDpi(..., GetDpiForWindow(hwnd))` and `SetWindowPos(SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)`; if `GetClientRect` then differs from the request (and the window is not iconic), it corrects the outer size once by the difference. `clientSize` is `GetClientRect` (0×0 while minimised). The title is converted from UTF-8 with `MultiByteToWideChar(CP_UTF8, 0, ...)`.
- `TestWindow` (Xlib): `DISPLAY` unset or empty → `Unsupported` "no display: DISPLAY is not set"; `XInitThreads()` once (`std::call_once`); `XOpenDisplay(nullptr)` fails → `Unsupported` "no display: cannot open '<DISPLAY>'". `XCreateSimpleWindow` on the root window of the default screen, black border and background, `XStoreName`, `XMapWindow`, then wait (polling `XGetWindowAttributes` after `XSync`, 10 ms steps, 2 s cap) until `map_state == IsViewable`; a timeout → `Internal` "the test window was not mapped within 2 s". `resize`: `XResizeWindow` + `XSync`, then poll up to 2 s until the reported size matches (a window manager applies it asynchronously; no failure when it does not, the caller compares `clientSize()`). `minimise`: `XUnmapWindow` + `XSync`, poll until not viewable; `restore`: `XMapWindow` + `XSync`, poll until viewable. `pump`: `XSync(display, False)`, then `XNextEvent` while `XPending` (no input is selected, so the queue stays small). The destructor destroys the window and closes the display.
- A moved-from `TestWindow` has no state: `native()` returns `monostate`, the destructor does nothing; other members must not be called.

**Code** (platform calls):

`src/surface.cpp`:

```cpp
#include "dvv/platform/surface.h"

#include <array>
#include <variant>

#include "surface_detail.h"

namespace dvv::platform {

namespace {

constexpr std::array<const char*, 2> WIN32_EXTENSIONS{VK_KHR_SURFACE_EXTENSION_NAME,
                                                      "VK_KHR_win32_surface"};
constexpr std::array<const char*, 2> XLIB_EXTENSIONS{VK_KHR_SURFACE_EXTENSION_NAME,
                                                     "VK_KHR_xlib_surface"};

}  // namespace

std::span<const char* const> surfaceInstanceExtensions(WindowSystem system) noexcept {
    switch (system) {
        case WindowSystem::Win32: return WIN32_EXTENSIONS;
        case WindowSystem::Xlib: return XLIB_EXTENSIONS;
    }
    return {};
}

std::expected<VkSurfaceKHR, VkResult> createSurface(VkInstance instance,
                                                    PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                                                    const NativeWindow& window) noexcept {
    const auto system = windowSystemOf(window);
    if (!system) {
        return std::unexpected(VK_ERROR_INITIALIZATION_FAILED);
    }
    if (*system != defaultWindowSystem()) {
        return std::unexpected(VK_ERROR_EXTENSION_NOT_PRESENT);  // this build lacks it
    }
    if (instance == VK_NULL_HANDLE || get_instance_proc_addr == nullptr) {
        return std::unexpected(VK_ERROR_INITIALIZATION_FAILED);
    }
#if defined(_WIN32)
    return detail::createWin32Surface(instance, get_instance_proc_addr,
                                      std::get<Win32Window>(window));
#else
    return detail::createXlibSurface(instance, get_instance_proc_addr,
                                     std::get<XlibWindow>(window));
#endif
}

}  // namespace dvv::platform
```

`src/surface_win32.cpp`:

```cpp
#include <windows.h>
// vulkan_win32.h needs the Windows types (HINSTANCE, HWND, SECURITY_ATTRIBUTES).
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_win32.h>

#include "surface_detail.h"

namespace dvv::platform::detail {

std::expected<VkSurfaceKHR, VkResult> createWin32Surface(
    VkInstance instance, PFN_vkGetInstanceProcAddr get_instance_proc_addr,
    const Win32Window& window) noexcept {
    if (window.hinstance == nullptr || window.hwnd == nullptr) {
        return std::unexpected(VK_ERROR_INITIALIZATION_FAILED);
    }
    const auto create = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        get_instance_proc_addr(instance, "vkCreateWin32SurfaceKHR"));
    if (create == nullptr) {
        return std::unexpected(VK_ERROR_EXTENSION_NOT_PRESENT);
    }
    const VkWin32SurfaceCreateInfoKHR info{.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
                                           .pNext = nullptr,
                                           .flags = 0,
                                           .hinstance = static_cast<HINSTANCE>(window.hinstance),
                                           .hwnd = static_cast<HWND>(window.hwnd)};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult result = create(instance, &info, nullptr, &surface);
    if (result != VK_SUCCESS) {
        return std::unexpected(result);
    }
    return surface;
}

}  // namespace dvv::platform::detail
```

`src/surface_xlib.cpp` is the same shape with `#include <X11/Xlib.h>`, `<vulkan/vulkan_core.h>`, `<vulkan/vulkan_xlib.h>` (in that order: `vulkan_xlib.h` needs `Display` and `Window`), the null check `window.display == nullptr || window.window == 0`, `"vkCreateXlibSurfaceKHR"`, `PFN_vkCreateXlibSurfaceKHR` and

```cpp
    const VkXlibSurfaceCreateInfoKHR info{.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
                                          .pNext = nullptr,
                                          .flags = 0,
                                          .dpy = static_cast<Display*>(window.display),
                                          .window = static_cast<Window>(window.window)};
```

X11 defines macros such as `None`, `Bool`, `Status`, `Success`, `True` and `False`: include X11 only in `surface_xlib.cpp` and `test_window_xlib.cpp`, never in a header. clang-format (`IncludeBlocks: Regroup`) puts `<X11/...>` before `"surface_detail.h"`; that order is fine because the dvv headers it pulls in use none of those names.

`src/testing/test_window_win32.cpp`:

```cpp
#include "dvv/platform/testing/test_window.h"

#include <format>
#include <string>
#include <utility>

#include <windows.h>

namespace dvv::platform::testing {

struct TestWindow::State {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
};

namespace {

constexpr wchar_t CLASS_NAME[] = L"dvvTestWindow";
constexpr DWORD STYLE = WS_OVERLAPPEDWINDOW;
constexpr DWORD EX_STYLE = 0;

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_GETMINMAXINFO) {
        // Tests use client sizes far below the default minimum tracking size.
        reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {1, 1};
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

// Once per process: per-monitor DPI awareness (v2), so sizes are physical pixels on every thread,
// and the window class. False when the class cannot be registered.
bool prepareProcess(HINSTANCE instance) {
    static const bool READY = [instance] {
        // Fails when the awareness was set already (e.g. by a manifest); either way sizes are
        // physical then.
        static_cast<void>(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = &windowProc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = CLASS_NAME;
        return RegisterClassExW(&window_class) != 0;
    }();
    return READY;
}

std::wstring widen(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = static_cast<int>(text.size());
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), length, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size > 0 ? size : 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), length, wide.data(), size);
    return wide;
}

}  // namespace

Result<TestWindow> TestWindow::create(PixelSize client_size, std::string_view title) {
    if (client_size.empty()) {
        return makeError(Errc::Internal, "TestWindow::create: the client size is empty");
    }
    HINSTANCE const instance = GetModuleHandleW(nullptr);
    if (!prepareProcess(instance)) {
        return makeError(Errc::Internal, "TestWindow::create: RegisterClassExW failed");
    }
    HWND const hwnd = CreateWindowExW(EX_STYLE, CLASS_NAME, widen(title).c_str(), STYLE,
                                      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                      nullptr, nullptr, instance, nullptr);
    if (hwnd == nullptr) {
        return makeError(Errc::Internal,
                         std::format("CreateWindowExW failed (error {})", GetLastError()));
    }
    TestWindow window(std::make_unique<State>(State{.instance = instance, .hwnd = hwnd}));
    window.resize(client_size);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    window.pump();
    return window;
}

TestWindow::TestWindow(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}
TestWindow::TestWindow(TestWindow&& other) noexcept = default;
TestWindow& TestWindow::operator=(TestWindow&& other) noexcept = default;

TestWindow::~TestWindow() {
    if (state_ && state_->hwnd != nullptr) {
        DestroyWindow(state_->hwnd);
    }
}

NativeWindow TestWindow::native() const noexcept {
    if (!state_) {
        return std::monostate{};
    }
    return Win32Window{.hinstance = state_->instance, .hwnd = state_->hwnd};
}

PixelSize TestWindow::clientSize() const {
    RECT rect{};
    GetClientRect(state_->hwnd, &rect);
    return {.width = static_cast<std::uint32_t>(rect.right > 0 ? rect.right : 0),
            .height = static_cast<std::uint32_t>(rect.bottom > 0 ? rect.bottom : 0)};
}

void TestWindow::resize(PixelSize client_size) {
    HWND const hwnd = state_->hwnd;
    RECT rect{0, 0, static_cast<LONG>(client_size.width), static_cast<LONG>(client_size.height)};
    AdjustWindowRectExForDpi(&rect, STYLE, FALSE, EX_STYLE, GetDpiForWindow(hwnd));
    SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    const PixelSize actual = clientSize();
    if (actual != client_size && IsIconic(hwnd) == FALSE) {
        // The frame differs from what AdjustWindowRectExForDpi assumed: correct it once.
        RECT outer{};
        GetWindowRect(hwnd, &outer);
        const LONG width = (outer.right - outer.left) + static_cast<LONG>(client_size.width) -
                           static_cast<LONG>(actual.width);
        const LONG height = (outer.bottom - outer.top) + static_cast<LONG>(client_size.height) -
                            static_cast<LONG>(actual.height);
        SetWindowPos(hwnd, nullptr, 0, 0, width, height,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    pump();
}

void TestWindow::minimise() {
    ShowWindow(state_->hwnd, SW_MINIMIZE);
    pump();
}

void TestWindow::restore() {
    ShowWindow(state_->hwnd, SW_RESTORE);
    pump();
}

void TestWindow::pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

}  // namespace dvv::platform::testing
```

`src/testing/test_window_xlib.cpp` (the essential calls; the rest follows the Behaviour section):

```cpp
#include "dvv/platform/testing/test_window.h"

#include <chrono>
#include <format>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <X11/Xlib.h>

#include "dvv/base/environment.h"

namespace dvv::platform::testing {

struct TestWindow::State {
    Display* display = nullptr;
    Window window = 0;
};

namespace {

// Polls `done` after XSync in 10 ms steps for up to 2 s; true once it holds.
template <class Done>
bool waitUntil(Display* display, Done done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (;;) {
        XSync(display, False);
        if (done()) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

XWindowAttributes attributes(Display* display, Window window) {
    XWindowAttributes result{};
    XGetWindowAttributes(display, window, &result);
    return result;
}

}  // namespace

Result<TestWindow> TestWindow::create(PixelSize client_size, std::string_view title) {
    if (client_size.empty()) {
        return makeError(Errc::Internal, "TestWindow::create: the client size is empty");
    }
    const auto name = getEnvironment("DISPLAY");
    if (!name || name->empty()) {
        return makeError(Errc::Unsupported, "no display: DISPLAY is not set");
    }
    // The Vulkan driver uses the display from the presenter thread as well.
    static std::once_flag threads;
    std::call_once(threads, [] { XInitThreads(); });
    Display* const display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return makeError(Errc::Unsupported, std::format("no display: cannot open '{}'", *name));
    }
    const int screen = DefaultScreen(display);
    const Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0,
                                              client_size.width, client_size.height, 0,
                                              BlackPixel(display, screen),
                                              BlackPixel(display, screen));
    TestWindow result(std::make_unique<State>(State{.display = display, .window = window}));
    const std::string owned_title(title);
    XStoreName(display, window, owned_title.c_str());
    XMapWindow(display, window);
    if (!waitUntil(display,
                   [&] { return attributes(display, window).map_state == IsViewable; })) {
        return makeError(Errc::Internal, "the test window was not mapped within 2 s");
    }
    return result;
}
```

The destructor calls `XDestroyWindow` then `XCloseDisplay` when `state_` is set; `native()` returns `XlibWindow{.display = display, .window = window}`; `clientSize()` returns the `width`/`height` of `XGetWindowAttributes`; `resize`, `minimise`, `restore` and `pump` as in Behaviour. The default Xlib error handler terminates the process on a protocol error; that is acceptable for a test helper.

`CMakeLists.txt` changes (replace the Task 5 block from `if(WIN32)` down to `dvv_set_compiler_options(dvv_platform)`, and the `install(TARGETS ...)` line):

```cmake
# Vulkan comes only from the pinned packages in the third-party prefix, never from CMake's FindVulkan
# module, which would pick up an installed Vulkan SDK through VULKAN_SDK.
set(CMAKE_DISABLE_FIND_PACKAGE_Vulkan ON)
find_package(VulkanHeaders 1.4.365 EXACT CONFIG REQUIRED)
if(NOT WIN32)
    find_package(X11 REQUIRED)
endif()

if(WIN32)
    set(platform_sources src/dynamic_library_win32.cpp src/surface_win32.cpp)
    set(test_window_source src/testing/test_window_win32.cpp)
else()
    set(platform_sources src/dynamic_library_posix.cpp src/surface_xlib.cpp)
    set(test_window_source src/testing/test_window_xlib.cpp)
endif()
add_library(dvv_platform STATIC
    src/dynamic_library.cpp
    src/native_window.cpp
    src/surface.cpp
    ${platform_sources})
add_library(dvv::platform ALIAS dvv_platform)
set_target_properties(dvv_platform PROPERTIES EXPORT_NAME platform)
target_include_directories(dvv_platform PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
if(NOT WIN32)
    # Only the headers (Display, Window for VkXlibSurfaceCreateInfoKHR); nothing calls Xlib here.
    target_include_directories(dvv_platform SYSTEM PRIVATE ${X11_INCLUDE_DIR})
endif()
# Vulkan's C headers are part of the interface, always without prototypes: every Vulkan function
# comes through vkGetInstanceProcAddr. dlopen on Linux (CMAKE_DL_LIBS is empty on Windows).
target_compile_definitions(dvv_platform PUBLIC VK_NO_PROTOTYPES)
target_link_libraries(dvv_platform PUBLIC dvv::base Vulkan::Headers PRIVATE ${CMAKE_DL_LIBS})
dvv_set_compiler_options(dvv_platform)

# Native test windows for the display tests of dvv-platform, dvv-gpu and rawreel (spec §12).
# Installed and exported: a stand-alone dvv-gpu build may use an installed dvv_platform.
add_library(dvv_platform_testing STATIC ${test_window_source})
add_library(dvv::platform_testing ALIAS dvv_platform_testing)
set_target_properties(dvv_platform_testing PROPERTIES EXPORT_NAME platform_testing)
if(WIN32)
    target_link_libraries(dvv_platform_testing PUBLIC dvv::platform PRIVATE user32)
else()
    target_link_libraries(dvv_platform_testing PUBLIC dvv::platform PRIVATE X11::X11)
endif()
dvv_set_compiler_options(dvv_platform_testing)
```

and `install(TARGETS dvv_platform dvv_platform_testing EXPORT dvv_platformTargets)`.

`cmake/dvv_platformConfig.cmake.in` becomes:

```cmake
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)
find_dependency(dvv_base 0.1)
# The pinned Vulkan-Headers package only, never CMake's FindVulkan module; the consumer's own
# setting is restored afterwards (this file runs in the consumer's scope).
set(_dvv_platform_disable_vulkan "${CMAKE_DISABLE_FIND_PACKAGE_Vulkan}")
set(CMAKE_DISABLE_FIND_PACKAGE_Vulkan ON)
find_dependency(VulkanHeaders 1.4.365 EXACT CONFIG)
set(CMAKE_DISABLE_FIND_PACKAGE_Vulkan "${_dvv_platform_disable_vulkan}")
unset(_dvv_platform_disable_vulkan)
if(NOT WIN32)
    find_dependency(X11)  # dvv::platform_testing links X11::X11
endif()

include("${CMAKE_CURRENT_LIST_DIR}/dvv_platformTargets.cmake")

check_required_components(dvv_platform)
```

`tests/CMakeLists.txt` additions: `test_native_window.cpp` joins `dvv_platform_tests`; then

```cmake
# Display tests (spec §12, label display): a real window and a Vulkan surface for it. The executable
# exits with 77 (skipped) without a Vulkan loader, a driver with the surface extensions or a display,
# unless DVV_REQUIRE_GPU=1.
add_executable(dvv_platform_surface_tests
    surface_main.cpp
    surface_support.cpp
    test_surface.cpp
    test_test_window.cpp)
target_link_libraries(dvv_platform_surface_tests PRIVATE dvv::platform dvv::platform_testing
                                                         doctest::doctest)
target_compile_definitions(dvv_platform_surface_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
dvv_set_compiler_options(dvv_platform_surface_tests)
add_test(NAME dvv_platform.surface COMMAND dvv_platform_surface_tests)
set_tests_properties(dvv_platform.surface PROPERTIES
    LABELS display
    SKIP_RETURN_CODE 77
    TIMEOUT 120
    ENVIRONMENT DVV_LEAK_CHECK=fail
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
```

Package consumer: `tests/package/CMakeLists.txt` links `dvv::platform dvv::platform_testing`; `tests/package/main.cpp` additionally checks `dvv::platform::windowSystemName(dvv::platform::defaultWindowSystem())` is `"Win32"` (Windows) or `"Xlib"` (Linux) and keeps the address of `&dvv::platform::createSurface` and `&dvv::platform::testing::TestWindow::create` in `volatile` function-pointer variables (so both libraries are linked), then prints `dvv_platform package works`.

`tests/surface_support.h` (namespace `dvv::platform::test`):

```cpp
// A Vulkan instance for the surface tests, made through the system's Vulkan loader
// ("vulkan-1.dll" with LibrarySearch::SystemDirectory, or "libvulkan.so.1"). The loader stays
// loaded for the rest of the process (keepLoaded()), as in dvv-gpu.
class VulkanInstance {
public:
    // Errc::Unsupported when the loader is missing, or vkCreateInstance returns
    // VK_ERROR_INCOMPATIBLE_DRIVER or VK_ERROR_EXTENSION_NOT_PRESENT; Errc::Internal otherwise.
    [[nodiscard]] static Result<VulkanInstance> create(std::span<const char* const> extensions);
    VulkanInstance(VulkanInstance&& other) noexcept;
    VulkanInstance& operator=(VulkanInstance&& other) noexcept;
    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;
    ~VulkanInstance();  // vkDestroyInstance

    [[nodiscard]] VkInstance handle() const noexcept;
    [[nodiscard]] PFN_vkGetInstanceProcAddr getInstanceProcAddr() const noexcept;
    void destroySurface(VkSurfaceKHR surface) const noexcept;  // vkDestroySurfaceKHR
    ...
};
```

The instance uses `VkApplicationInfo{.apiVersion = VK_API_VERSION_1_3}`, no layers, and the given extensions; `vkCreateInstance` comes from `getInstanceProcAddr(nullptr, "vkCreateInstance")`, `vkDestroyInstance` and `vkDestroySurfaceKHR` from `getInstanceProcAddr(instance, ...)`.

`tests/surface_main.cpp` (the skip rules are binding):

```cpp
// Surface tests (CTest dvv_platform.surface, label display). Without a Vulkan loader, a driver that
// offers the surface extensions, or a display, the executable exits with 77 (CTest: skipped),
// unless DVV_REQUIRE_GPU=1 turns that into a failure (Linux CI under Xvfb).
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdio>
#include <exception>
#include <expected>
#include <optional>
#include <print>
#include <string>

#include "dvv/base/environment.h"
#include "dvv/base/leak_check.h"
#include "dvv/base/lifetime.h"
#include "dvv/platform/surface.h"
#include "dvv/platform/testing/test_window.h"
#include "surface_support.h"

namespace {

constexpr int EXIT_SKIPPED = 77;

// nullopt when the tests can run; the reason when something they need is missing
// (Errc::Unsupported); an error for any other failure.
dvv::Result<std::optional<std::string>> missingRequirement() {
    const auto instance = dvv::platform::test::VulkanInstance::create(
        dvv::platform::surfaceInstanceExtensions(dvv::platform::defaultWindowSystem()));
    if (!instance) {
        if (instance.error().code != dvv::Errc::Unsupported) {
            return std::unexpected(instance.error());
        }
        return instance.error().describe();
    }
    const auto window = dvv::platform::testing::TestWindow::create({.width = 64, .height = 64});
    if (!window) {
        if (window.error().code != dvv::Errc::Unsupported) {
            return std::unexpected(window.error());
        }
        return window.error().describe();
    }
    return std::nullopt;
}

int run(int argc, char** argv) {
    dvv::LifetimeRegistry::instance().setDetailed(true);
    const auto missing = missingRequirement();
    if (!missing) {
        std::println(stderr, "surface tests failed: {}", missing.error().describe());
        return 1;
    }
    if (missing->has_value()) {
        if (dvv::getEnvironment("DVV_REQUIRE_GPU") == "1") {
            std::println(stderr, "surface tests failed: DVV_REQUIRE_GPU=1, but {}", **missing);
            return 1;
        }
        std::println("surface tests skipped: {}", **missing);
        return EXIT_SKIPPED;
    }
    doctest::Context context(argc, argv);
    const int result = context.run();
    if (context.shouldExit()) {
        return result;
    }
    return dvv::finishWithLeakCheck(result);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "surface tests failed: %s\n", error.what());
        return 1;
    }
}
```

**Tests first:**

`tests/test_native_window.cpp` (unit; runs everywhere, Windows CI included):
1. "window systems have names and a default": `windowSystemName(WindowSystem::Win32) == "Win32"`, `windowSystemName(WindowSystem::Xlib) == "Xlib"`; `defaultWindowSystem()` is `Win32` under `_WIN32`, else `Xlib`.
2. "windowSystemOf names the window's alternative": `NativeWindow{}` → `nullopt`; `NativeWindow{Win32Window{}}` → `Win32`; `NativeWindow{XlibWindow{}}` → `Xlib`.
3. "a pixel size is empty when a side is 0": `PixelSize{}`, `{1, 0}`, `{0, 1}` are `empty()`; `{1, 1}` is not; `PixelSize{2, 3} == PixelSize{2, 3}` and `!= PixelSize{3, 2}`.
4. "each window system needs VK_KHR_surface and its own extension": `surfaceInstanceExtensions(Win32)` has size 2, `[0] == std::string_view("VK_KHR_surface")`, `[1] == "VK_KHR_win32_surface"`; `Xlib` → `"VK_KHR_surface"`, `"VK_KHR_xlib_surface"`.
5. "createSurface rejects an empty window and a window system this build lacks": with `VK_NULL_HANDLE` and `nullptr` for instance and function: `createSurface(..., NativeWindow{})` → `error() == VK_ERROR_INITIALIZATION_FAILED`; the other system's window with non-null fake handles (`Win32Window{reinterpret_cast<void*>(1), reinterpret_cast<void*>(1)}` on Linux, `XlibWindow{reinterpret_cast<void*>(1), 1}` on Windows) → `VK_ERROR_EXTENSION_NOT_PRESENT`; the own system's window with a null instance → `VK_ERROR_INITIALIZATION_FAILED`.

`tests/test_surface.cpp` and `tests/test_test_window.cpp` (display executable; each case creates its own `VulkanInstance` and `TestWindow` and `REQUIRE`s them):
6. "a surface is created for a test window and destroyed": instance with `surfaceInstanceExtensions(defaultWindowSystem())`, window 320×240, `createSurface(instance.handle(), instance.getInstanceProcAddr(), window.native())` has a value that is not `VK_NULL_HANDLE`; `instance.destroySurface(*surface)` before the window goes.
7. "a window with null handles is rejected": with that instance, `Win32Window{.hinstance = <the window's>, .hwnd = nullptr}` (Windows) or `XlibWindow{.display = <the window's>, .window = 0}` and `XlibWindow{.display = nullptr, .window = <the window's>}` (Linux) → `VK_ERROR_INITIALIZATION_FAILED`.
8. "an instance without the surface extensions cannot create surfaces": `VulkanInstance::create({})`, then `createSurface` for a real window → `VK_ERROR_EXTENSION_NOT_PRESENT`.
9. "a test window reports, changes and restores its client size": `TestWindow::create({320, 240})`: `clientSize() == PixelSize{320, 240}`, `windowSystemOf(native()) == defaultWindowSystem()`; `resize({200, 150})` → `clientSize() == PixelSize{200, 150}`; `minimise()`, `pump()`; on Windows `clientSize().empty()`; `restore()`, `pump()` → `clientSize() == PixelSize{200, 150}`; `TestWindow moved = std::move(window)`: `moved.clientSize() == PixelSize{200, 150}` and `std::holds_alternative<std::monostate>(window.native())` (the moved-from object; clang-tidy's use-after-move check may need `// NOLINT(bugprone-use-after-move)` on that line, with the reason in the comment).

The first failure: with the headers in place and no sources, `dvv_platform_tests` fails to link (`windowSystemName`, `createSurface` unresolved). The display executable is added in Step 4.

**Steps:**

- [ ] **Step 1: Unit tests first**

Create the three headers, `tests/test_native_window.cpp` (cases 1–5), add it to `dvv_platform_tests`, and add `src/native_window.cpp` and `src/surface.cpp` containing only their includes (plus the CMake changes for Vulkan headers above, so the headers compile).
Run: `Push-Location dvv-platform; cmake --workflow --preset vs2026; Pop-Location`
Expected: link errors for `dvv::platform::windowSystemName`, `defaultWindowSystem`, `windowSystemOf`, `surfaceInstanceExtensions`, `createSurface`.

- [ ] **Step 2: Implement native windows and surfaces**

Write `src/native_window.cpp`, `src/surface.cpp`, `src/surface_detail.h`, `src/surface_win32.cpp`, `src/surface_xlib.cpp`.
Run the same workflow. Expected: Debug and Release `100% tests passed, 0 tests failed out of 2`; doctest in `dvv_platform.unit`: 13 cases on Windows.

- [ ] **Step 3: `TestWindow`**

Write `src/testing/test_window_win32.cpp`, `src/testing/test_window_xlib.cpp`, add `dvv_platform_testing` and the install/config changes.

- [ ] **Step 4: Display tests**

Write `tests/surface_support.h`, `tests/surface_support.cpp`, `tests/surface_main.cpp`, `tests/test_surface.cpp`, `tests/test_test_window.cpp` (cases 6–9) and the CTest entry. Update the package consumer.
Run on Windows (local NVIDIA; a few windows appear on the desktop, do not touch them):

```powershell
Push-Location dvv-platform; cmake --workflow --preset vs2026; Pop-Location
$env:DVV_REQUIRE_GPU = '1'; ctest --test-dir _build/dvv-platform/vs2026 -C Debug -L display -V | Select-String 'test cases|skipped|Passed|Failed'; Remove-Item Env:DVV_REQUIRE_GPU
```

Expected: Debug and Release `100% tests passed, 0 tests failed out of 3`; the display run prints `test cases: 4 | 4 passed | 0 failed` (no `surface tests skipped`).

- [ ] **Step 5: Linux on WSLg, and the Xvfb variant when available**

```powershell
wsl -d Ubuntu-24.04 -e bash -lc 'echo "DISPLAY=$DISPLAY"; . _build/tools/linux/env.sh && cd dvv-platform && cmake --workflow --preset ninja-clang 2>&1 | grep -E "tests passed|tests failed|Error|error:" && DVV_REQUIRE_GPU=1 ctest --test-dir ../_build/dvv-platform/ninja-clang -C Debug -L display -V | grep -E "test cases|skipped"'
wsl -d Ubuntu-24.04 -e bash -lc 'if command -v xvfb-run >/dev/null; then . _build/tools/linux/env.sh && DVV_REQUIRE_GPU=1 xvfb-run -a -s "-screen 0 1920x1080x24" ctest --test-dir _build/dvv-platform/ninja-clang -C Debug -L display --output-on-failure; else echo "xvfb-run is not installed: the Xvfb variant is skipped locally (CI runs it)"; fi'
```

Expected: `DISPLAY=:0`; `100% tests passed ... out of 3` (Debug and Release); `test cases: 4 | 4 passed` on lavapipe under WSLg; the Xvfb line passes or prints the skip message.

- [ ] **Step 6: Full matrix, lint and commit**

`git -C dvv-platform add -A`, then in parallel (two background calls):

```powershell
Push-Location dvv-platform; cmake --workflow --preset vs2026-asan; Pop-Location
```

```powershell
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd dvv-platform && for p in ninja-clang-asan ninja-clang-tsan; do cmake --workflow --preset $p 2>&1 | grep -E "tests passed|tests failed|Error|error:"; done'
```

Lint: regenerate the two file lists exactly as in Task 5 Step 7 (PowerShell: `git -C dvv-platform ls-files -- '*.h' '*.cpp'` into `_build/lint/dvv-platform-format.txt`; `git -C dvv-platform ls-files -- ':(glob)src/**/*.cpp' ':(glob)tests/*.cpp' ':(exclude,glob)**/*_win32.cpp'` into `_build/lint/dvv-platform-tidy.txt`, joined with `"`n"` through `[IO.File]::WriteAllText`) and run the two WSL commands of Task 5 Step 7 (`xargs -a ... -d "\n" -P 4 -n 8 clang-format --dry-run --Werror`, `xargs -a ... -d "\n" -P 4 -n 1 clang-tidy -p ../_build/dvv-platform/ninja-clang --quiet`) in parallel.

Expected: `vs2026-asan` `out of 2` (no package test); Linux ASan and TSan `out of 2`, the display test runs (WSLg) under both; `FORMAT-OK`, `TIDY-OK`.

If a sanitizer reports a leak or race whose stack has a frame only inside libX11, libxcb or the Vulkan driver (R9): add `tests/lsan.supp` (`leak:libX11.so`) or `tests/tsan.supp` (`race:libvulkan_lvp.so`, `race:libX11.so`, `race:libxcb.so`) and set `LSAN_OPTIONS=suppressions=...` / `TSAN_OPTIONS=suppressions=...` for `dvv_platform.surface` only, under `if(DVV_SANITIZE MATCHES "address")` / `if(DVV_SANITIZE STREQUAL "thread")`, with a header comment as in `dvv-gpu/tests/tsan.supp`; a report with a frame in dvv code is a bug to fix, not to suppress.

Update the first paragraph of `README.md`: "Platform layer of the dvv stack used by rawreel: native window descriptions (`NativeWindow`), Vulkan surface creation for Win32 and Xlib, run-time libraries (`DynamicLibrary`, with a system-directory-only search for libraries such as the Vulkan loader), and a native test window for display tests (`dvv::platform_testing`). C++23; depends on dvv-base and the pinned Vulkan-Headers. Nothing in it depends on a GUI toolkit."

```powershell
git -C dvv-platform add -A
@'
feat: describe native windows, create Vulkan surfaces and add a test window

NativeWindow is a closed variant of Win32 and Xlib handles. createSurface() makes a VkSurfaceKHR
for it through vkGetInstanceProcAddr and returns the VkResult, so dvv-gpu keeps the one conversion
to dvv::Error; surfaceInstanceExtensions() names the instance extensions a window system needs.

dvv::platform_testing provides TestWindow, a plain CreateWindowExW or XCreateSimpleWindow window
that display tests resize, minimise and restore without a GUI toolkit. The surface test exits with
77 without a loader, a driver or a display, unless DVV_REQUIRE_GPU=1.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
'@ | git -C dvv-platform commit -F -
cmake -DREPO=dvv-platform -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake
```

Expected: `-- privacy-lint: clean (commits)`.

**Expected results:** dvv-platform 3 CTest tests (`unit`, `surface`, `package`; 2 under sanitizers). `dvv_platform.unit`: 13 doctest cases on Windows, 11 on Linux. `dvv_platform.surface`: 4 cases, run (not skipped) on Windows with NVIDIA, on WSLg and under Xvfb; Windows CI skips it with 77 (no loader).

**Commit:** one commit, message above.

**Notes:** Design D2 (two free functions over a variant, no `ISurfaceFactory`), D11 (raw native windows for presentation tests), D12 (77 and `DVV_REQUIRE_GPU`), §4.3, §6 (`TestWindow` exempt), §7 (`TestWindow` errors), §8.1 (dvv-platform rows). Risks: under the WSLg window manager the window size may settle a little later; `resize` polls for it. If `AdjustWindowRectExForDpi` and the correction still miss the size (an unusual frame), the tests compare with `clientSize()` and fail loudly, which is the intended signal. Lavapipe's `vkCreateXlibSurfaceKHR` needs no X calls; the X server is used only from Task 9 on.

---

### Task 7: dvv-gpu on dvv-platform: loader and window-system opt-in

**Goal:** dvv-gpu depends on dvv-platform, loads the Vulkan loader through `DynamicLibrary` (System32 only on Windows), and gains the opt-in window-system support: `GpuRegistryOptions::windowSystem`, `OptionalFeatures::swapchain` (also in `dvv-gpu-info`), `ContextOptions::presentation` and `ExecutionContext::presentation()`.

**Files:**
- Modify (dvv-gpu):
  - `CMakeLists.txt` (declare and link `dvv_platform`; drop `${CMAKE_DL_LIBS}`), `cmake/dvv_gpuConfig.cmake.in` (`find_dependency(dvv_platform 0.1)`);
  - `src/loader.h`, `src/loader.cpp` (`DynamicLibrary`, `loaderLibrary`);
  - `src/instance.h`, `src/instance.cpp` (window-system extensions, their check, `InstanceCore::windowSystem()`);
  - `include/dvv/gpu/gpu_registry.h`, `src/gpu_registry.cpp` (`windowSystem` option and accessor, the presentation check in `createContext`);
  - `include/dvv/gpu/gpu_info.h`, `src/gpu_info.cpp` (`OptionalFeatures::swapchain`, JSON);
  - `src/devices.h`, `src/devices.cpp` (detect `VK_KHR_swapchain`; `deviceExtensions`);
  - `src/device_core.h`, `src/device_core.cpp` (`presentation` parameter and accessor);
  - `include/dvv/gpu/execution_context.h`, `src/execution_context.cpp` (`ContextOptions::presentation`, `presentation()`);
  - `tools/dvv_gpu_info.cpp` (text line);
  - `tests/CMakeLists.txt` (new unit file; package test argument), `tests/package/run-package-test.cmake`, `tests/test_loader.cpp`, `tests/test_gpu_info.cpp`, `tests/run-gpu-info.cmake`, `tests/gpu_test_registry.cpp`, `tests/gpu_test_context.cpp`.
- Create (dvv-gpu): `tests/test_window_system.cpp`.

**Interfaces:**
- Consumes: Task 6 (`dvv::platform` with `WindowSystem`, `windowSystemName`, `defaultWindowSystem`, `surfaceInstanceExtensions`, `DynamicLibrary`, `LibrarySearch`; package `dvv_platform` 0.1); Task 5 (workspace wiring: `FETCHCONTENT_SOURCE_DIR_DVV_PLATFORM` in the presets); Task 4 (validation layer with Linux WSI support in the prefix).
- Produces (CMake): `dvv_gpu` links PUBLIC `dvv::platform`; `FetchContent_Declare(dvv_platform GIT_REPOSITORY "https://github.com/BarabashkaD/dvv-platform.git" GIT_TAG main FIND_PACKAGE_ARGS 0.1)` next to dvv_base and `FetchContent_MakeAvailable(dvv_base dvv_platform)`; the package test passes `-DPLATFORM_SOURCE_DIR=${dvv_platform_SOURCE_DIR}` and the in-tree consumer gets `-DFETCHCONTENT_SOURCE_DIR_DVV_PLATFORM=${PLATFORM_SOURCE_DIR}`.
- Produces (C++, public; Task 9 and rawreel rely on them):

```cpp
// gpu_info.h
struct OptionalFeatures {
    bool shaderInt16 = false;
    bool shaderFloat16 = false;
    bool hostImageCopy = false;
    bool memoryBudget = false;  // VK_EXT_memory_budget
    bool swapchain = false;     // VK_KHR_swapchain (device extension)
};

// gpu_registry.h (#include "dvv/platform/native_window.h")
struct GpuRegistryOptions {
    // ... applicationName, validation, validationRequired unchanged ...
    // Set: the instance also enables platform::surfaceInstanceExtensions(*windowSystem), so
    // contexts can present (Errc::Unsupported when the loader lacks them). Unset: headless.
    std::optional<platform::WindowSystem> windowSystem = std::nullopt;
};
class GpuRegistry {
    // ... existing members ...
    // The window system the instance was created for; nullopt for a headless registry.
    [[nodiscard]] std::optional<platform::WindowSystem> windowSystem() const noexcept;
};

// execution_context.h
struct ContextOptions {
    std::filesystem::path pipelineCacheFile{};
    // Enables VK_KHR_swapchain. Errc::Internal when the registry has no window system;
    // Errc::Unsupported when the device lacks the extension (GpuInfo::optional.swapchain).
    bool presentation = false;
};
class ExecutionContext {
    // ... existing members ...
    // Whether VK_KHR_swapchain is enabled (ContextOptions::presentation).
    [[nodiscard]] bool presentation() const noexcept;
};
```

- Produces (C++, private, namespace `dvv::gpu::detail`):
  - `src/loader.h`: `struct LoaderLibrary { std::string name; platform::LibrarySearch search; };` and `[[nodiscard]] LoaderLibrary loaderLibrary(const std::string& library_name);` — empty name → `{"vulkan-1.dll", SystemDirectory}` on Windows, `{"libvulkan.so.1", SystemDirectory}` on Linux (where it equals Default); a non-empty name (tests) → `{library_name, Default}`. `loadVulkanLoader` and `vulkanLoader` keep their signatures and error texts.
  - `src/instance.h`: `[[nodiscard]] std::span<const char* const> windowSystemExtensions(const GpuRegistryOptions& options) noexcept;` (empty when `windowSystem` is unset, else `platform::surfaceInstanceExtensions(*options.windowSystem)`); `[[nodiscard]] Result<void> requireInstanceExtensions(std::span<const char* const> required, std::span<const vk::ExtensionProperties> available, std::string_view window_system_name);` (`Errc::Unsupported` "the Vulkan loader does not offer <extension>, which the <name> window system needs" for the first missing one); `InstanceCore::windowSystem() const noexcept -> std::optional<platform::WindowSystem>`.
  - `src/devices.h`: `[[nodiscard]] Result<std::vector<const char*>> deviceExtensions(const GpuInfo& info, bool presentation);` — `VK_EXT_memory_budget` when `info.optional.memoryBudget`; `VK_KHR_swapchain` when `presentation`; `presentation && !info.optional.swapchain` → `Errc::Unsupported` "device <id> (<name>) does not support VK_KHR_swapchain, which presentation needs".
  - `src/device_core.h`: `DeviceCore::create(std::shared_ptr<const GpuRegistry>, const InstanceCore&, const PhysicalDevice&, const GpuInfo&, bool presentation = false)` (the default keeps the existing white-box test call compiling) and `[[nodiscard]] bool presentation() const noexcept`.

**Behaviour:**
- Loader: `loadVulkanLoader(name)` opens `loaderLibrary(name)` with `platform::DynamicLibrary::open`; failure → `Errc::Unsupported` "Vulkan loader not found (<describe(name)>)" (the `DynamicLibrary` message is not added; the existing test checks the exact text); missing `vkGetInstanceProcAddr` → "Vulkan loader not found (<describe(name)> has no vkGetInstanceProcAddr)"; where `describe("")` is "the default library" and otherwise `'<name>'`. Each opened library is `keepLoaded()` and moved into the never-destroyed `LoadedLibraries` (`std::vector<platform::DynamicLibrary>` instead of `unique_ptr<vk::detail::DynamicLoader>`). `vk::detail::DynamicLoader` is no longer used anywhere.
- Instance: `InstanceCore::create` enumerates the instance extensions (it already does, for debug utils), then `requireInstanceExtensions(windowSystemExtensions(options), extensions.value, windowSystemName(*options.windowSystem))` when `windowSystem` is set; `buildInstance` gets the extension span and calls `builder.enable_extension(name)` for each; `set_headless(true)` stays (vk-bootstrap must not add its own choice of surface extensions). The window system is stored in the core and logged in the "Vulkan instance created" line (", window system Win32" / ", headless").
- `createContext`: id check first (unchanged); then `options.presentation && !instance_->windowSystem()` → `Errc::Internal` "ContextOptions::presentation needs a GpuRegistry created with a window system (GpuRegistryOptions::windowSystem)"; then `DeviceCore::create(..., options.presentation)`, which builds its extension list with `deviceExtensions(info, presentation)` (`Unsupported` passes through) after the existing usable check.
- Devices: `querySupport` sets `support.optional.swapchain` when the device lists `vk::KHRSwapchainExtensionName` (independent of the registry's window system: `dvv-gpu-info` stays headless and still reports it).
- `toJson`: `optionalFeatures` gains `"swapchain"`. `dvv-gpu-info` text: the optional line ends with `, VK_EXT_memory_budget <yes|no>, VK_KHR_swapchain <yes|no>`.
- Unchanged: the default registry is headless; `dvv-gpu-info` and its exit codes; every M1a test.

**Code:**

`src/loader.cpp` (complete):

```cpp
#include "loader.h"

#include <format>
#include <mutex>
#include <utility>
#include <vector>

#include "dvv/platform/dynamic_library.h"

namespace dvv::gpu::detail {

namespace {

// Process infrastructure: the loaded libraries, intentionally never destroyed (see loader.h).
struct LoadedLibraries {
    std::mutex mutex;
    std::vector<platform::DynamicLibrary> libraries;
    PFN_vkGetInstanceProcAddr defaultLoader = nullptr;
};

LoadedLibraries& loadedLibraries() {
    static auto* const LIBRARIES = new LoadedLibraries();
    return *LIBRARIES;
}

std::string describe(const std::string& library_name) {
    return library_name.empty() ? std::string("the default library")
                                : std::format("'{}'", library_name);
}

}  // namespace

LoaderLibrary loaderLibrary(const std::string& library_name) {
    if (!library_name.empty()) {
        return {.name = library_name, .search = platform::LibrarySearch::Default};
    }
#if defined(_WIN32)
    // Only from the system directory: a vulkan-1.dll next to the executable, in the current
    // directory or on PATH is never loaded.
    return {.name = "vulkan-1.dll", .search = platform::LibrarySearch::SystemDirectory};
#else
    return {.name = "libvulkan.so.1", .search = platform::LibrarySearch::SystemDirectory};
#endif
}

Result<PFN_vkGetInstanceProcAddr> loadVulkanLoader(const std::string& library_name) {
    const LoaderLibrary wanted = loaderLibrary(library_name);
    auto library = platform::DynamicLibrary::open(wanted.name, wanted.search);
    if (!library) {
        return makeError(Errc::Unsupported,
                         std::format("Vulkan loader not found ({})", describe(library_name)));
    }
    const auto get_instance_proc_addr =
        library->function<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    if (get_instance_proc_addr == nullptr) {
        return makeError(Errc::Unsupported,
                         std::format("Vulkan loader not found ({} has no vkGetInstanceProcAddr)",
                                     describe(library_name)));
    }
    library->keepLoaded();
    auto& loaded = loadedLibraries();
    const std::scoped_lock lock(loaded.mutex);
    loaded.libraries.push_back(std::move(*library));
    return get_instance_proc_addr;
}

// vulkanLoader() unchanged.

}  // namespace dvv::gpu::detail
```

Note: a library without `vkGetInstanceProcAddr` is not kept loaded (the `DynamicLibrary` unloads it on return), unlike before; nothing holds a pointer into it.

**Tests first:**

Unit (`dvv_gpu_tests`):
1. `tests/test_loader.cpp` "the default loader is the platform's library from the system directory": `loaderLibrary("")` → name `"vulkan-1.dll"` (Windows) / `"libvulkan.so.1"` (Linux), search `SystemDirectory`.
2. "a named loader is searched the usual way": `loaderLibrary("x/libvulkan-test.so")` → same name, `Default`.
3. "a library without vkGetInstanceProcAddr is not a Vulkan loader": `loadVulkanLoader("kernel32.dll")` (Windows) / `loadVulkanLoader("libc.so.6")` (Linux) → `Errc::Unsupported`, message `"Vulkan loader not found ('kernel32.dll' has no vkGetInstanceProcAddr)"` (resp. `'libc.so.6'`).
4. (Windows only) "the default loader comes from the system directory": `vulkanLoader()`; on `Unsupported` → `MESSAGE("no Vulkan loader")` and return (Windows CI); otherwise `GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(gpa), &module)` succeeds and the parent folder of `GetModuleFileNameW(module)` equals `GetSystemDirectoryW` (case-insensitive `_wcsicmp` on `std::filesystem::path(...).parent_path().native()`).
   The existing case "a missing Vulkan loader is reported as unsupported" stays unchanged and must still pass.
5. `tests/test_window_system.cpp` "a headless registry asks for no surface extensions": `windowSystemExtensions(GpuRegistryOptions{}).empty()`.
6. "a window system adds VK_KHR_surface and its platform extension": `windowSystemExtensions({.windowSystem = WindowSystem::Win32})` equals `{"VK_KHR_surface", "VK_KHR_win32_surface"}` (compare as `std::string_view`); `Xlib` → `{"VK_KHR_surface", "VK_KHR_xlib_surface"}`.
7. "missing instance extensions are unsupported": build `std::vector<vk::ExtensionProperties>` with `extensionName` filled (`std::ranges::copy` of the name into the array); required `{"VK_KHR_surface", "VK_KHR_xlib_surface"}` against `[VK_KHR_surface]` → `Errc::Unsupported`, message `"the Vulkan loader does not offer VK_KHR_xlib_surface, which the Xlib window system needs"`; against both → has a value; empty required against nothing → has a value.
8. `tests/test_gpu_info.cpp` "presentation needs VK_KHR_swapchain": `GpuInfo` with `optional.swapchain = false`: `deviceExtensions(info, true)` → `Errc::Unsupported`, message contains "VK_KHR_swapchain"; `deviceExtensions(info, false)` → empty vector; with `swapchain = true, memoryBudget = true`: `deviceExtensions(info, true)` contains `"VK_KHR_swapchain"` and `"VK_EXT_memory_budget"` (as `std::string_view`), `deviceExtensions(info, false)` contains only `"VK_EXT_memory_budget"`.
9. The existing "device information formats as text and JSON" case additionally checks `json.at("optionalFeatures").at("swapchain") == false`, and after `info.optional.swapchain = true` the new JSON has `true`.

GPU (`dvv_gpu_device_tests`; each creates what it needs, checks `validationMessageCount() == 0` at the end):
10. `gpu_test_registry.cpp` "the default registry is headless": `GpuTest` fixture: `registry().windowSystem() == std::nullopt`; `Access::instance(registry()).dispatch().vkGetInstanceProcAddr(instance, "vkDestroySurfaceKHR") == nullptr`.
11. "a registry with the platform's window system enables the surface extensions": `GpuRegistry::create({.windowSystem = platform::defaultWindowSystem()})` has a value; `windowSystem() == defaultWindowSystem()`; `vkGetInstanceProcAddr(instance, "vkDestroySurfaceKHR") != nullptr` and the platform's `"vkCreateWin32SurfaceKHR"` / `"vkCreateXlibSurfaceKHR"` is not null; the best device reports `optional.swapchain == true`; `validationMessageCount() == 0`.
12. "a window system the loader lacks is unsupported": the other system (`Xlib` on Windows, `Win32` on Linux) → `Errc::Unsupported`, the message contains `"VK_KHR_xlib_surface"` (resp. `"VK_KHR_win32_surface"`).
13. `gpu_test_context.cpp` "a presentation context enables VK_KHR_swapchain": windowed registry; `createContext(best, {.presentation = true})` has a value, `presentation()` is true and `Access::core(*context).dispatch().vkCreateSwapchainKHR != nullptr`; `createContext(best)` on the same registry: `presentation()` false and `vkCreateSwapchainKHR == nullptr`.
14. "presentation needs a registry with a window system": `GpuTest` fixture (headless): `registry().createContext(device(), {.presentation = true})` → `Errc::Internal`.
15. `tests/run-gpu-info.cmake`: `string(JSON swapchain_type TYPE "${json}" devices 0 optionalFeatures swapchain)` must be `BOOLEAN`; the text output must match `VK_KHR_swapchain (yes|no)`.

The first failure: the unit tests do not compile (`loaderLibrary`, `windowSystemExtensions`, `requireInstanceExtensions`, `deviceExtensions`, `OptionalFeatures::swapchain`, `GpuRegistryOptions::windowSystem` unknown).

**Steps:**

- [ ] **Step 1: Depend on dvv-platform**

Edit `dvv-gpu/CMakeLists.txt`: after the `FetchContent_Declare(dvv_base ...)` call add

```cmake
# dvv-platform (native windows, Vulkan surfaces, run-time libraries): declared by the workspace
# first as well; stand-alone builds take it from the sibling folder
# (FETCHCONTENT_SOURCE_DIR_DVV_PLATFORM in the workspace presets).
FetchContent_Declare(dvv_platform
    GIT_REPOSITORY "https://github.com/BarabashkaD/dvv-platform.git"
    GIT_TAG main
    FIND_PACKAGE_ARGS 0.1)
```

replace `FetchContent_MakeAvailable(dvv_base)` with `FetchContent_MakeAvailable(dvv_base dvv_platform)`, and replace the `target_link_libraries(dvv_gpu ...)` call with

```cmake
# The Vulkan loader is opened through dvv-platform's DynamicLibrary, which links the dl library.
target_link_libraries(dvv_gpu
    PUBLIC dvv::base dvv::platform Vulkan::Headers
    PRIVATE $<BUILD_INTERFACE:GPUOpen::VulkanMemoryAllocator>)
```

In `cmake/dvv_gpuConfig.cmake.in` add `find_dependency(dvv_platform 0.1)` after `find_dependency(dvv_base 0.1)`. In `tests/CMakeLists.txt` add `-DPLATFORM_SOURCE_DIR=${dvv_platform_SOURCE_DIR}` after `-DBASE_SOURCE_DIR=...` of `dvv_gpu.package`; in `tests/package/run-package-test.cmake` add `"-DFETCHCONTENT_SOURCE_DIR_DVV_PLATFORM=${PLATFORM_SOURCE_DIR}"` after the `FETCHCONTENT_SOURCE_DIR_DVV_BASE` argument and `-DPLATFORM_SOURCE_DIR` to the header comment's argument list.

Run: `Push-Location dvv-gpu; cmake --workflow --preset vs2026; Pop-Location` (background).
Expected: unchanged results, `100% tests passed, 0 tests failed out of 13` (Debug and Release); the package test builds both consumers with dvv_platform.

- [ ] **Step 2: Loader tests first**

Add cases 1–4 to `tests/test_loader.cpp`. Build: expected compile errors (`loaderLibrary`, `LoaderLibrary` unknown).

- [ ] **Step 3: Loader through `DynamicLibrary`**

Change `src/loader.h` (add `#include "dvv/platform/dynamic_library.h"`, `LoaderLibrary`, `loaderLibrary`; update its comment: the default loader is opened from the system directory on Windows) and `src/loader.cpp` (above).
Run: `Push-Location dvv-gpu; cmake --workflow --preset vs2026; Pop-Location` (background). Expected: `100% tests passed, 0 tests failed out of 13`; `dvv_gpu.unit` has 4 more cases on Windows (3 on Linux); the GPU tests still run on the local GPU (the loader now comes from System32). Commit:

```powershell
git -C dvv-gpu add CMakeLists.txt cmake/dvv_gpuConfig.cmake.in src/loader.h src/loader.cpp tests/CMakeLists.txt tests/package/run-package-test.cmake tests/test_loader.cpp
@'
build: load the Vulkan loader through dvv-platform

dvv-gpu now depends on dvv-platform. The loader is opened with DynamicLibrary instead of
vk::detail::DynamicLoader: on Windows vulkan-1.dll comes only from the system directory
(LOAD_LIBRARY_SEARCH_SYSTEM32), so a DLL planted next to the executable, in the current directory or
on PATH is never loaded. Loaded libraries are still never unloaded; the error texts are unchanged.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
'@ | git -C dvv-gpu commit -F -
```

- [ ] **Step 4: Window-system and presentation unit tests first**

Add cases 5–9 (`tests/test_window_system.cpp` joins `dvv_gpu_tests` in `tests/CMakeLists.txt`). Build: expected compile errors (`windowSystemExtensions`, `requireInstanceExtensions`, `deviceExtensions`, `OptionalFeatures::swapchain`, `GpuRegistryOptions::windowSystem` unknown).

- [ ] **Step 5: Implement the window-system opt-in and presentation contexts**

Registry: `windowSystemExtensions`, `requireInstanceExtensions`, the stored window system, `buildInstance`'s extension list, `GpuRegistryOptions::windowSystem`, `GpuRegistry::windowSystem()`. Devices and contexts: `OptionalFeatures::swapchain` (detection, JSON, the tool's text line), `deviceExtensions`, `DeviceCore::create(..., presentation)` and `presentation()`, `ContextOptions::presentation`, the check in `createContext`, `ExecutionContext::presentation()`.
Run: `Push-Location dvv-gpu; cmake --workflow --preset vs2026; Pop-Location`. Expected: `100% tests passed, 0 tests failed out of 13`; compared with the start of the task, `dvv_gpu.unit` has 8 more doctest cases on Windows (7 on Linux).

- [ ] **Step 6: GPU tests**

Add cases 10–15 and run them with synchronization validation on the local GPU (background):

```powershell
$env:DVV_VK_VALIDATION = 'sync'; $env:DVV_REQUIRE_GPU = '1'; Push-Location dvv-gpu; cmake --workflow --preset vs2026; Pop-Location; ctest --test-dir _build/dvv-gpu/vs2026 -C Debug -R 'dvv_gpu\.gpu$' -V | Select-String 'test cases|skipped'; Remove-Item Env:DVV_VK_VALIDATION, Env:DVV_REQUIRE_GPU
_build/dvv-gpu/vs2026/Debug/dvv-gpu-info.exe | Select-String 'VK_KHR_swapchain'
```

Expected: `100% tests passed, 0 tests failed out of 13` (Debug and Release); the GPU doctest summary has 5 more passed cases than before this task and no skip; the tool line shows `VK_KHR_swapchain yes`.

- [ ] **Step 7: Full matrix, workspace build, lint and commit**

`git -C dvv-gpu add -A`, then in parallel (background calls):

```powershell
$env:DVV_VK_VALIDATION = 'sync'; Push-Location dvv-gpu; cmake --workflow --preset vs2026-asan; Pop-Location; Remove-Item Env:DVV_VK_VALIDATION
```

```powershell
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && export DVV_REQUIRE_GPU=1 DVV_VK_VALIDATION=sync DVV_VK_VALIDATION_REQUIRED=1 && cd dvv-gpu && for p in ninja-clang ninja-clang-asan ninja-clang-tsan; do cmake --workflow --preset $p 2>&1 | grep -E "tests passed|tests failed|Error|error:"; done'
```

After the Windows run, the workspace build (background): `$env:DVV_VK_VALIDATION = 'sync'; Push-Location rawreel-workspace; cmake --workflow --preset vs2026; Pop-Location; Remove-Item Env:DVV_VK_VALIDATION` — expected `100% tests passed, 0 tests failed out of <N + 3>`, where N is the total noted in Task 5 Step 8 (Task 5's `dvv_platform.unit` and `.package`, Task 6's `dvv_platform.surface`; this task adds no CTest test). The workspace build now configures dvv-gpu with dvv-platform in-tree, which is what this run proves.

Lint (git in PowerShell, WSL reads the lists; format and tidy in parallel):

```powershell
New-Item -ItemType Directory -Force _build/lint | Out-Null
$format = git -C dvv-gpu ls-files -- '*.h' '*.cpp'
$tidy = git -C dvv-gpu ls-files -- ':(glob)src/*.cpp' ':(glob)tests/*.cpp' ':(glob)tools/*.cpp'
[IO.File]::WriteAllText("$PWD/_build/lint/dvv-gpu-format.txt", ($format -join "`n") + "`n")
[IO.File]::WriteAllText("$PWD/_build/lint/dvv-gpu-tidy.txt", ($tidy -join "`n") + "`n")
```

```powershell
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd dvv-gpu && xargs -a ../_build/lint/dvv-gpu-format.txt -d "\n" -P 4 -n 8 clang-format --dry-run --Werror && echo FORMAT-OK'
```

```powershell
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd dvv-gpu && xargs -a ../_build/lint/dvv-gpu-tidy.txt -d "\n" -P 4 -n 1 clang-tidy -p ../_build/dvv-gpu/ninja-clang --quiet && echo TIDY-OK'
```

Expected: `vs2026-asan` `out of 12`; Linux `out of 13` for `ninja-clang` (Debug and Release), `out of 12` for ASan and TSan, GPU tests run on lavapipe with sync validation; `FORMAT-OK`, `TIDY-OK`.

```powershell
git -C dvv-gpu add -A
@'
feat: opt in to window-system extensions and presentation contexts

GpuRegistryOptions::windowSystem enables VK_KHR_surface and the platform's surface extension on the
instance (Errc::Unsupported when the loader lacks them); without it the registry stays headless, as
dvv-gpu-info is. ContextOptions::presentation enables VK_KHR_swapchain on the device: Errc::Internal
for a headless registry, Errc::Unsupported for a device without the extension.
OptionalFeatures::swapchain reports the extension, also in dvv-gpu-info's text and JSON.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
'@ | git -C dvv-gpu commit -F -
cmake -DREPO=dvv-gpu -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake
```

Expected: `-- privacy-lint: clean (commits)`.

**Expected results:** dvv-gpu keeps 13 CTest tests (12 under sanitizers); `dvv_gpu.unit` +8 doctest cases on Windows (+7 on Linux); `dvv_gpu.gpu` +5 cases, run with zero validation messages on NVIDIA and on lavapipe; the workspace build N + 3 tests.

**Commit:** two commits (Steps 3 and 7).

**Notes:** Design D3 (System32 loading, `keepLoaded`), D15 (opt-in), §4.4, §7 rows 1–3, §8.1 (dvv-gpu `unit` and `gpu` rows). Plan choices: (a) the loader-extension check is done by dvv-gpu itself before vk-bootstrap (`requireInstanceExtensions`) for a clear message; vk-bootstrap still receives the names through `enable_extension`, with `set_headless(true)` kept. (b) The `swapchain` flag is detected for every registry, headless included. Risks: cases 10 and 13 rely on the loader returning null from `vkGetInstanceProcAddr` for WSI functions of disabled instance extensions and on the driver returning null from `vkGetDeviceProcAddr` for disabled device extensions (guaranteed with `maintenance5`, which dvv-gpu requires). If a loader or driver returns non-null there, drop only that negative check and record it under "Changes during execution".

---

### Task 8: `PresenterLogic` and `presentBlit`

**Goal:** Add the presenter's pure decision logic (`detail::PresenterLogic`, all 15 design cases as unit tests that run on every platform) and the internal recorder operation `presentBlit` that blits a client image into a swapchain image and leaves it in `PRESENT_SRC_KHR`, proven by readback.

**Files:**
- Create (dvv-gpu): `src/presenter_logic.h`, `src/presenter_logic.cpp` (pure state machine), `src/present_blit.h` (the blit filter rule), `tests/test_presenter_logic.cpp` (unit), `tests/test_present_blit.cpp` (unit), `tests/gpu_test_present_blit.cpp` (GPU).
- Modify (dvv-gpu): `CMakeLists.txt` (`src/presenter_logic.cpp`), `include/dvv/gpu/command_recorder.h` (private `presentBlit`, `friend struct detail::Access`), `src/command_recorder.cpp`, `src/access.h` (`Access::presentBlit`), `src/device_core.h`, `src/device_core.cpp` (`formatFeatures`), `tests/CMakeLists.txt`.

**Interfaces:**
- Consumes: Task 7 (`GpuRegistryOptions::windowSystem`, `ContextOptions::presentation`, `ExecutionContext::presentation()`, `platform::PixelSize`, `platform::defaultWindowSystem`); M1a `CommandRecorder` internals (`checkImage`, `check`, `prepareTransfer`, `COLOR_RANGE`, `COLOR_LAYERS`, `describe`), `detail::Access`, `DeviceCore`, `test::submitAndWait`, `ExecutorFactory::makeThreadPool`.
- Produces (for Task 9's `PresenterCore`): `src/presenter_logic.h` (below, complete), `DeviceCore::formatFeatures`, `Access::presentBlit`, `detail::presentBlitFilter`.

```cpp
// src/presenter_logic.h
#pragma once

// The presenter's decisions (spec §4; M1b design D10, 5.2-5.7), free of Vulkan calls and threads so
// that every rule is unit-tested on every platform. The presenter thread copies the GUI's request
// block in (setRequests), asks next() what to do, executes it and reports the outcome. Not
// thread-safe: only the presenter thread uses it.

#include <cstdint>
#include <optional>

#include <vulkan/vulkan_core.h>

#include "dvv/base/error.h"
#include "dvv/platform/native_window.h"

namespace dvv::gpu::detail {

// What a WSI call returned, as far as the presenter distinguishes it.
enum class VkOutcome : std::uint8_t {
    Success,
    Suboptimal,
    OutOfDate,
    Timeout,
    SurfaceLost,
    DeviceLost,
    OtherError
};

// VK_SUCCESS -> Success, VK_SUBOPTIMAL_KHR -> Suboptimal, VK_ERROR_OUT_OF_DATE_KHR -> OutOfDate,
// VK_TIMEOUT and VK_NOT_READY -> Timeout, VK_ERROR_SURFACE_LOST_KHR -> SurfaceLost,
// VK_ERROR_DEVICE_LOST -> DeviceLost, anything else -> OtherError.
[[nodiscard]] VkOutcome outcomeOf(VkResult result) noexcept;

class PresenterLogic {
public:
    enum class Action : std::uint8_t {
        Wait,               // nothing to do until a GUI request, a completion or stop
        WaitAndRetry,       // the acquire timed out: wait 10 ms, then call retryElapsed()
        RecreateSwapchain,  // design 5.3
        RenderFrame,        // design 5.2 steps 1-5
        Present,            // design 5.2 step 6 (the frame in flight has completed)
        Exit,               // stop or failed: teardown (5.5)
    };

    // Out-of-date results in a row after which the surface counts as hidden until a GUI event.
    static constexpr int OUT_OF_DATE_LIMIT = 4;

    // The GUI's request block, copied under the presenter mutex at the top of every loop.
    // request_seq grows with every resize, setVisible and requestFrame; stop is sticky.
    void setRequests(platform::PixelSize extent, bool visible, std::uint64_t request_seq,
                     bool stop) noexcept;
    // A swapchain was created for the hint `extent` (the extentHint() of the decision): stores it,
    // clears the recreate mark and the surface-hidden state, and sets the internal re-request.
    void swapchainCreated(platform::PixelSize extent) noexcept;
    // The surface reported extent 0: hidden until request_seq changes; the request stays pending.
    void surfaceHidden() noexcept;
    // The result of vkAcquireNextImageKHR and, for Success and Suboptimal, of the fence wait that
    // follows it (a fence wait that ends in DEVICE_LOST is reported as DeviceLost, the 5 s cap as
    // OtherError).
    void acquired(VkOutcome outcome) noexcept;
    // The 10 ms after WaitAndRetry have passed.
    void retryElapsed() noexcept;
    // The frame's submission was handed to the submit thread: phase InFlight; the request of the
    // current snapshot is served (servedSeq = request_seq of the snapshot); the internal
    // re-request is cleared.
    void frameStarted() noexcept;
    // The provider failed or its image was unusable: the request is served, the phase stays Idle.
    void frameSkipped() noexcept;
    // The frame in flight completed; an error fails the presenter.
    void frameCompleted(std::optional<Errc> error) noexcept;
    // The result of vkQueuePresentKHR for the completed frame.
    void presented(VkOutcome outcome) noexcept;

    [[nodiscard]] Action next() const noexcept;
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    // Not visible, a 0-sized hint, or a surface hidden until the next GUI event.
    [[nodiscard]] bool hidden() const noexcept;
    [[nodiscard]] bool inFlight() const noexcept { return phase_ == Phase::InFlight; }
    // For PresentFrameInfo: true only when the previous provider call's frame completed.
    [[nodiscard]] bool previousFrameCompleted() const noexcept { return previous_completed_; }
    // The GUI's latest size hint (used for swapchain creation when currentExtent is 0xFFFFFFFF).
    [[nodiscard]] platform::PixelSize extentHint() const noexcept { return extent_; }

private:
    enum class Phase : std::uint8_t { Idle, InFlight, PresentPending };

    [[nodiscard]] bool framePending() const noexcept;
    void outOfDate() noexcept;

    platform::PixelSize extent_{};
    bool visible_ = false;
    std::uint64_t request_seq_ = 0;
    bool stop_ = false;

    Phase phase_ = Phase::Idle;
    std::uint64_t served_seq_ = 0;
    bool internal_request_ = false;  // after a swapchain creation or a present OUT_OF_DATE
    bool swapchain_ = false;
    bool recreate_ = false;  // out-of-date or suboptimal seen
    platform::PixelSize created_extent_{};
    std::optional<std::uint64_t> hidden_seq_{};  // hidden while request_seq_ equals it
    int out_of_date_streak_ = 0;
    bool retry_ = false;
    bool previous_completed_ = false;
    bool failed_ = false;
};

}  // namespace dvv::gpu::detail
```

- Private recorder operation (`include/dvv/gpu/command_recorder.h`, private section; `namespace detail { class DeviceCore; class Submitter; struct Access; }` and `friend struct detail::Access;` are added):

```cpp
    // Moves `source` to TRANSFER_SRC_OPTIMAL (tracked like every GpuImage), moves the swapchain
    // image UNDEFINED -> TRANSFER_DST_OPTIMAL, blits the whole source onto the whole destination
    // (presentBlitFilter: NEAREST when the sizes match; otherwise LINEAR when the source format has
    // SAMPLED_IMAGE_FILTER_LINEAR, else NEAREST; it never fails for this reason), then moves the
    // swapchain image to PRESENT_SRC_KHR (destination stage and access NONE). Fails the recorder
    // (Internal) for an image of another device, without TRANSFER_SRC usage or whose format lacks
    // BLIT_SRC, and for a null swapchain image or an empty extent. Reached through
    // detail::Access::presentBlit; the context must have presentation enabled.
    void presentBlit(GpuImage& source, VkImage swapchain_image, VkExtent2D swapchain_extent);
```

- `src/access.h`: `static void presentBlit(CommandRecorder& recorder, GpuImage& source, VkImage swapchain_image, VkExtent2D swapchain_extent) { recorder.presentBlit(source, swapchain_image, swapchain_extent); }` (include `"dvv/gpu/command_recorder.h"`).
- `src/device_core.h`: `// The optimal-tiling features of a format (VkFormatProperties3), queried once per format and cached. Any thread.` `[[nodiscard]] VkFormatFeatureFlags2 formatFeatures(VkFormat format) const;` with private `mutable std::mutex format_mutex_;` and `mutable std::vector<std::pair<VkFormat, VkFormatFeatureFlags2>> format_features_;`. Task 9's image check (design 5.2 step 2) uses the same cache.
- `src/present_blit.h`:

```cpp
#pragma once

#include <vulkan/vulkan_core.h>

namespace dvv::gpu::detail {

// The filter of CommandRecorder::presentBlit: NEAREST for a 1:1 copy, LINEAR for scaling when the
// source format supports linear filtering, NEAREST otherwise.
[[nodiscard]] constexpr VkFilter presentBlitFilter(VkExtent2D source, VkExtent2D destination,
                                                   VkFormatFeatureFlags2 source_features) noexcept {
    if (source.width == destination.width && source.height == destination.height) {
        return VK_FILTER_NEAREST;
    }
    return (source_features & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0
               ? VK_FILTER_LINEAR
               : VK_FILTER_NEAREST;
}

}  // namespace dvv::gpu::detail
```

**Behaviour** (`PresenterLogic`; binding):
- `next()` evaluates, in this order: InFlight → `Wait` (the thread never exits with a frame in flight); failed or stop → `Exit` (a completed frame is not presented after stop); PresentPending → `Present`; retry pending → `WaitAndRetry`; `hidden()` → `Wait`; no frame pending (`request_seq <= served_seq` and no internal re-request) → `Wait`; no swapchain, recreate mark set, or hint different from the hint of the last creation → `RecreateSwapchain`; otherwise `RenderFrame`.
- `acquired`: Success → streak 0; Suboptimal → recreate mark, streak 0 (the frame goes on); OutOfDate → recreate mark, `previousFrameCompleted` false, out-of-date streak +1 (reaching `OUT_OF_DATE_LIMIT` stores `hidden_seq = request_seq` and resets the streak); Timeout → retry pending, `previousFrameCompleted` false; SurfaceLost, DeviceLost, OtherError → failed. The request is never served by `acquired` (only `frameStarted` and `frameSkipped` serve it).
- `presented` (only in PresentPending; back to Idle): Success → streak 0; Suboptimal → recreate mark, streak 0; OutOfDate → recreate mark, internal re-request, streak +1 (same limit rule); SurfaceLost, DeviceLost, OtherError and Timeout (never returned by a present) → failed.
- `frameCompleted` (only in InFlight): no error → PresentPending, `previousFrameCompleted` true; an error → failed, Idle, `previousFrameCompleted` false.
- `frameSkipped`: `served_seq = request_seq`, internal re-request cleared, `previousFrameCompleted` false, Idle.
- `setRequests` overwrites extent, visible and `request_seq`; `stop` is ORed in. A call in the wrong phase (`frameCompleted` outside InFlight, `presented` outside PresentPending, `frameStarted` outside Idle) is a programming error: `assert` in Debug, ignored in Release.

**Code** (the logic is short and binding, so here it is in full):

```cpp
// src/presenter_logic.cpp
#include "presenter_logic.h"

#include <cassert>

namespace dvv::gpu::detail {

VkOutcome outcomeOf(VkResult result) noexcept {
    switch (result) {
        case VK_SUCCESS: return VkOutcome::Success;
        case VK_SUBOPTIMAL_KHR: return VkOutcome::Suboptimal;
        case VK_ERROR_OUT_OF_DATE_KHR: return VkOutcome::OutOfDate;
        case VK_TIMEOUT:
        case VK_NOT_READY: return VkOutcome::Timeout;
        case VK_ERROR_SURFACE_LOST_KHR: return VkOutcome::SurfaceLost;
        case VK_ERROR_DEVICE_LOST: return VkOutcome::DeviceLost;
        default: return VkOutcome::OtherError;
    }
}

void PresenterLogic::setRequests(platform::PixelSize extent, bool visible,
                                 std::uint64_t request_seq, bool stop) noexcept {
    extent_ = extent;
    visible_ = visible;
    request_seq_ = request_seq;
    stop_ = stop_ || stop;
}

void PresenterLogic::swapchainCreated(platform::PixelSize extent) noexcept {
    swapchain_ = true;
    recreate_ = false;
    created_extent_ = extent;
    hidden_seq_.reset();
    internal_request_ = true;
}

void PresenterLogic::surfaceHidden() noexcept {
    hidden_seq_ = request_seq_;
}

void PresenterLogic::acquired(VkOutcome outcome) noexcept {
    switch (outcome) {
        case VkOutcome::Success: out_of_date_streak_ = 0; break;
        case VkOutcome::Suboptimal:
            recreate_ = true;
            out_of_date_streak_ = 0;
            break;
        case VkOutcome::OutOfDate:
            recreate_ = true;
            previous_completed_ = false;
            outOfDate();
            break;
        case VkOutcome::Timeout:
            retry_ = true;
            previous_completed_ = false;
            break;
        default: failed_ = true; break;
    }
}

void PresenterLogic::retryElapsed() noexcept {
    retry_ = false;
}

void PresenterLogic::frameStarted() noexcept {
    assert(phase_ == Phase::Idle);
    phase_ = Phase::InFlight;
    served_seq_ = request_seq_;
    internal_request_ = false;
}

void PresenterLogic::frameSkipped() noexcept {
    served_seq_ = request_seq_;
    internal_request_ = false;
    previous_completed_ = false;
}

void PresenterLogic::frameCompleted(std::optional<Errc> error) noexcept {
    assert(phase_ == Phase::InFlight);
    if (phase_ != Phase::InFlight) {
        return;
    }
    if (error) {
        failed_ = true;
        phase_ = Phase::Idle;
        previous_completed_ = false;
        return;
    }
    phase_ = Phase::PresentPending;
    previous_completed_ = true;
}

void PresenterLogic::presented(VkOutcome outcome) noexcept {
    assert(phase_ == Phase::PresentPending);
    if (phase_ != Phase::PresentPending) {
        return;
    }
    phase_ = Phase::Idle;
    switch (outcome) {
        case VkOutcome::Success: out_of_date_streak_ = 0; break;
        case VkOutcome::Suboptimal:
            recreate_ = true;
            out_of_date_streak_ = 0;
            break;
        case VkOutcome::OutOfDate:
            recreate_ = true;
            internal_request_ = true;
            outOfDate();
            break;
        default: failed_ = true; break;
    }
}

PresenterLogic::Action PresenterLogic::next() const noexcept {
    if (phase_ == Phase::InFlight) {
        return Action::Wait;
    }
    if (failed_ || stop_) {
        return Action::Exit;
    }
    if (phase_ == Phase::PresentPending) {
        return Action::Present;
    }
    if (retry_) {
        return Action::WaitAndRetry;
    }
    if (hidden() || !framePending()) {
        return Action::Wait;
    }
    if (!swapchain_ || recreate_ || extent_ != created_extent_) {
        return Action::RecreateSwapchain;
    }
    return Action::RenderFrame;
}

bool PresenterLogic::hidden() const noexcept {
    return !visible_ || extent_.empty() || (hidden_seq_ && *hidden_seq_ == request_seq_);
}

bool PresenterLogic::framePending() const noexcept {
    return request_seq_ > served_seq_ || internal_request_;
}

void PresenterLogic::outOfDate() noexcept {
    if (++out_of_date_streak_ >= OUT_OF_DATE_LIMIT) {
        hidden_seq_ = request_seq_;
        out_of_date_streak_ = 0;
    }
}

}  // namespace dvv::gpu::detail
```

`CommandRecorder::presentBlit` (Vulkan synchronisation; complete):

```cpp
void CommandRecorder::presentBlit(GpuImage& source, VkImage swapchain_image,
                                  VkExtent2D swapchain_extent) {
    if (!usable()) {
        return;
    }
    if (!checkImage("presentBlit", source, VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
        return;
    }
    const VkFormatFeatureFlags2 features = core_->formatFeatures(source.format());
    if (!check((features & VK_FORMAT_FEATURE_2_BLIT_SRC_BIT) != 0, "presentBlit",
               std::format("{} has a format that cannot be a blit source", describe(source))) ||
        !check(swapchain_image != VK_NULL_HANDLE && swapchain_extent.width > 0 &&
                   swapchain_extent.height > 0,
               "presentBlit", "the swapchain image is null or empty")) {
        return;
    }
    const VkImageLayout source_layout =
        prepareTransfer(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    const vk::CommandBuffer commands(command_buffer_);
    const vk::Image target(swapchain_image);
    // The presentation engine was done with the image when the acquire fence signalled, and the
    // host waited for that fence before this submission, so there is nothing to wait for (stage
    // NONE). UNDEFINED discards the old contents.
    const vk::ImageMemoryBarrier2 to_transfer{.srcStageMask = vk::PipelineStageFlagBits2::eNone,
                                              .srcAccessMask = vk::AccessFlagBits2::eNone,
                                              .dstStageMask = vk::PipelineStageFlagBits2::eBlit,
                                              .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                              .oldLayout = vk::ImageLayout::eUndefined,
                                              .newLayout = vk::ImageLayout::eTransferDstOptimal,
                                              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                              .image = target,
                                              .subresourceRange = COLOR_RANGE};
    commands.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_transfer},
                              core_->dispatch());
    const VkExtent2D source_extent{.width = source.width(), .height = source.height()};
    const vk::ImageBlit2 region{
        .srcSubresource = COLOR_LAYERS,
        .srcOffsets = std::array<vk::Offset3D, 2>{
            vk::Offset3D{.x = 0, .y = 0, .z = 0},
            vk::Offset3D{.x = static_cast<std::int32_t>(source_extent.width),
                         .y = static_cast<std::int32_t>(source_extent.height),
                         .z = 1}},
        .dstSubresource = COLOR_LAYERS,
        .dstOffsets = std::array<vk::Offset3D, 2>{
            vk::Offset3D{.x = 0, .y = 0, .z = 0},
            vk::Offset3D{.x = static_cast<std::int32_t>(swapchain_extent.width),
                         .y = static_cast<std::int32_t>(swapchain_extent.height),
                         .z = 1}}};
    commands.blitImage2(
        {.srcImage = vk::Image(source.handle()),
         .srcImageLayout = static_cast<vk::ImageLayout>(source_layout),
         .dstImage = target,
         .dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
         .regionCount = 1,
         .pRegions = &region,
         .filter = static_cast<vk::Filter>(
             detail::presentBlitFilter(source_extent, swapchain_extent, features))},
        core_->dispatch());
    // Presentation needs no destination stage or access: the queue's later present is ordered
    // after the submission completed (the host waited for it).
    const vk::ImageMemoryBarrier2 to_present{.srcStageMask = vk::PipelineStageFlagBits2::eBlit,
                                             .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                             .dstStageMask = vk::PipelineStageFlagBits2::eNone,
                                             .dstAccessMask = vk::AccessFlagBits2::eNone,
                                             .oldLayout = vk::ImageLayout::eTransferDstOptimal,
                                             .newLayout = vk::ImageLayout::ePresentSrcKHR,
                                             .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                             .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                             .image = target,
                                             .subresourceRange = COLOR_RANGE};
    commands.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_present},
                              core_->dispatch());
}
```

(`command_recorder.cpp` includes `<array>`, `<cstdint>` and `"present_blit.h"`. If `vk::ArrayWrapper1D` does not accept the `std::array` in a designated initialiser, fill `srcOffsets`/`dstOffsets` after construction; the values are what matters.)

`DeviceCore::formatFeatures` queries `physical_.getFormatProperties2<vk::FormatProperties2, vk::FormatProperties3>(static_cast<vk::Format>(format), dispatch_)` once per format (outside the lock), returns `static_cast<VkFormatFeatureFlags2>(chain.get<vk::FormatProperties3>().optimalTilingFeatures)` and caches it under `format_mutex_` (a second thread that raced stores nothing new).

**Tests first:**

`tests/test_presenter_logic.cpp` (unit; every platform, Windows CI included). Helpers: `constexpr PixelSize SIZE{320, 240};` `void completeFrame(PresenterLogic&)` (`REQUIRE(next() == RenderFrame)`, `acquired(Success)`, `frameStarted()`, `REQUIRE(next() == Wait)`, `frameCompleted(std::nullopt)`, `REQUIRE(next() == Present)`, `presented(Success)`); `PresenterLogic presenting()` (new logic, `setRequests(SIZE, true, 1, false)`, `REQUIRE(next() == RecreateSwapchain)`, `swapchainCreated(SIZE)`, `completeFrame`, `REQUIRE(next() == Wait)`).
1. "outcomeOf sorts Vulkan results": `VK_SUCCESS`→Success, `VK_SUBOPTIMAL_KHR`→Suboptimal, `VK_ERROR_OUT_OF_DATE_KHR`→OutOfDate, `VK_TIMEOUT` and `VK_NOT_READY`→Timeout, `VK_ERROR_SURFACE_LOST_KHR`→SurfaceLost, `VK_ERROR_DEVICE_LOST`→DeviceLost, `VK_ERROR_OUT_OF_HOST_MEMORY` and `VK_ERROR_NATIVE_WINDOW_IN_USE_KHR`→OtherError.
2. "a new logic is hidden and waits": `next() == Wait`, `hidden()`, `!failed()`, `!inFlight()`, `!previousFrameCompleted()`.
3. "the first request creates the swapchain, then renders and presents one frame": `presenting()` succeeds; afterwards `previousFrameCompleted()` and `next() == Wait`.
4. (design case 1) "no frame while hidden or 0-sized; the request is served once visible": from `presenting()`: `setRequests(SIZE, false, 2, false)` → `hidden()`, `Wait`; `setRequests({0, 0}, true, 3, false)` → `hidden()`, `Wait`; `setRequests(SIZE, true, 4, false)` → `RenderFrame`; `completeFrame` → `Wait` (one frame only).
5. (case 2) "several resizes give one recreation at the latest hint": from `presenting()`: `setRequests({640, 480}, true, 2, false)`, `setRequests({97, 61}, true, 3, false)`, `setRequests({800, 600}, true, 4, false)` → `RecreateSwapchain`; `swapchainCreated({800, 600})` → `RenderFrame`; `completeFrame` → `Wait`; `setRequests({800, 600}, true, 5, false)` → `RenderFrame` (the same hint never re-creates, whatever the surface's real size); complete; `setRequests({801, 600}, true, 6, false)` → `RecreateSwapchain`.
6. (case 3) "requests during a frame in flight give exactly one more frame": from `presenting()`: `setRequests(SIZE, true, 2, false)` → `RenderFrame`, `acquired(Success)`, `frameStarted()`; `setRequests(SIZE, true, 3/4/5, false)` → `Wait` each time (`inFlight()`); `frameCompleted(nullopt)` → `Present`; `presented(Success)` → `RenderFrame`; `completeFrame` from the acquire on → `Wait`.
7. (case 4) "out-of-date and suboptimal results recreate the swapchain before the next frame": (a) acquire OutOfDate: request 2 → `RenderFrame`, `acquired(OutOfDate)` → `RecreateSwapchain` (the request is still pending); `swapchainCreated(SIZE)` → `RenderFrame`. (b) present OutOfDate: a fresh `presenting()`, request 2, run to `Present`, `presented(OutOfDate)` → `RecreateSwapchain` with no new GUI request (internal re-request); `swapchainCreated(SIZE)` → `RenderFrame`. (c) acquire Suboptimal: request 2 → `RenderFrame`, `acquired(Suboptimal)`, `frameStarted()`, `frameCompleted(nullopt)`, `presented(Success)` → `Wait`; request 3 → `RecreateSwapchain`. (d) present Suboptimal: same as (c) with `acquired(Success)` and `presented(Suboptimal)`.
8. (cases 5 and 13) "four out-of-date results in a row hide the surface until a GUI event": from `presenting()`, request 2; three times: `RenderFrame` → `acquired(OutOfDate)` → `RecreateSwapchain` → `swapchainCreated(SIZE)`; the fourth `acquired(OutOfDate)` → `hidden()` and `Wait`; `setRequests(SIZE, true, 2, false)` again (no GUI event) → still `hidden()`, `Wait`; `setRequests(SIZE, true, 3, false)` → `!hidden()`, `RecreateSwapchain`.
9. (case 6) "a timeout drops the frame and retries after the deadline": from `presenting()`, request 2 → `RenderFrame`, `acquired(Timeout)` → `WaitAndRetry`; `setRequests(SIZE, true, 2, false)` → still `WaitAndRetry`; `retryElapsed()` → `RenderFrame`; `!previousFrameCompleted()`.
10. (case 7) "a lost surface fails the presenter": `acquired(SurfaceLost)` → `failed()`, `Exit`; separately `presented(SurfaceLost)` after a completed frame → `failed()`, `Exit`.
11. (case 8) "a lost device fails the presenter for good": `acquired(DeviceLost)` → `Exit`; `setRequests(SIZE, true, 99, false)` → still `Exit`, `failed()`.
12. (case 9) "a failed frame fails the presenter without presenting": `frameStarted()`, `frameCompleted(Errc::DeviceLost)` → `failed()`, `Exit` (not `Present`); the same with `Errc::OutOfMemory` and `Errc::Internal`.
13. (cases 10 and 15) "stop during a frame in flight waits for it and exits without presenting": request 2 → `RenderFrame`, `acquired(Success)` (the fence wait has ended), `frameStarted()`; `setRequests(SIZE, true, 2, true)` → `Wait`; `frameCompleted(nullopt)` → `Exit`.
14. (case 15, fence cap) "a failed acquire fence wait fails the presenter": request 2 → `RenderFrame`, then `acquired(OtherError)` (the 5 s cap) → `failed()`, `Exit`; the same with `acquired(DeviceLost)`.
15. (case 11) "stop while idle exits at once": a new logic with `setRequests(SIZE, true, 1, true)` → `Exit`; `presenting()` then `setRequests(SIZE, false, 2, true)` (hidden, request pending) → `Exit`.
16. (case 12) "a request that arrives during the provider call or the acquire is not lost": from `presenting()`: `setRequests(SIZE, true, 2, false)` → `RenderFrame`; (the GUI raises the counter to 3 now, unseen) `acquired(Success)`, `frameStarted()`, `frameCompleted(nullopt)`, `presented(Success)`; `setRequests(SIZE, true, 3, false)` → `RenderFrame`.
17. (case 13, surface) "a surface of extent 0 hides until a new request": a new logic, `setRequests(SIZE, true, 1, false)` → `RecreateSwapchain`; `surfaceHidden()` → `hidden()`, `Wait`; `setRequests(SIZE, true, 1, false)` → `Wait`; `setRequests(SIZE, true, 2, false)` → `RecreateSwapchain`.
18. (case 14) "previousFrameCompleted reports whether the last provider call's frame ran": false initially; true after `presenting()`; false after `acquired(Timeout)`; `retryElapsed()`, a full frame → true; `frameSkipped()` → false; a full frame → true; `acquired(OutOfDate)` → false.
19. "a skipped frame serves its request and stays idle": from `presenting()`, request 2 → `RenderFrame`, `frameSkipped()` → `Wait`, `!inFlight()`; request 3 → `RenderFrame`.

`tests/test_present_blit.cpp` (unit):
20. "the blit filter is NEAREST for 1:1 and LINEAR only for scaling with linear filtering": `presentBlitFilter({4, 4}, {4, 4}, VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT) == VK_FILTER_NEAREST`; `({2, 1}, {4, 1}, that bit) == VK_FILTER_LINEAR`; `({2, 1}, {4, 1}, 0) == VK_FILTER_NEAREST`; `({4, 4}, {2, 2}, VK_FORMAT_FEATURE_2_BLIT_SRC_BIT) == VK_FILTER_NEAREST`.

`tests/gpu_test_present_blit.cpp` (GPU). Fixture `PresentBlitTest`: `GpuRegistry::create({.windowSystem = platform::defaultWindowSystem()})` (validation from the environment) and a context on `bestDevice()` with `{.presentation = true}` (`FAIL` with the error's description otherwise), a one-thread executor; its destructor resets the context and the executor, then `CHECK(registry->validationMessageCount() == 0)`. Helper `std::vector<std::uint8_t> blitAndRead(PresentBlitTest&, std::span<const std::uint8_t> rgba, VkExtent2D source, VkExtent2D target)`:
   - uploads `rgba` through a `HostUpload` buffer (`TRANSFER_SRC`, `flush()`) into an `R8G8B8A8_UNORM` `GpuImage` (`TRANSFER_SRC | TRANSFER_DST`) with `copyBufferToImage`;
   - creates the "swapchain" image directly through VMA (`vmaCreateImage(Access::core(ctx).allocator(), ...)`: 2D, `B8G8R8A8_UNORM`, the target extent, 1 level, 1 layer, optimal tiling, usage `TRANSFER_DST | TRANSFER_SRC`, `VMA_MEMORY_USAGE_AUTO`; destroyed with `vmaDestroyImage` at the end);
   - submission 1: `copyBufferToImage(upload, image)` then `Access::presentBlit(recorder, *image, raw_image, target)`;
   - submission 2 (after submission 1 completed): `recorder.raw()`, then with `Access::core(ctx).dispatch()` a `pipelineBarrier2` with one `vk::ImageMemoryBarrier2{.srcStageMask = eAllCommands, .srcAccessMask = eMemoryWrite, .dstStageMask = eCopy, .dstAccessMask = eTransferRead, .oldLayout = ePresentSrcKHR, .newLayout = eTransferSrcOptimal, queue families ignored, .image = raw_image, colour range}` and `copyImageToBuffer` into a `HostReadback` buffer (`TRANSFER_DST`), then `invalidate()` and return the bytes (BGRA8).
21. "presentBlit copies a same-size image exactly": source 33×17 with `r = x, g = y, b = (x * 7 + y) & 0xFF, a = 255`; target 33×17; for every pixel the readback has `B == b`, `G == g`, `R == r`, `A == 255` (the blit converts RGBA to BGRA).
22. "presentBlit scales with linear filtering": `REQUIRE((Access::core(ctx).formatFeatures(VK_FORMAT_R8G8B8A8_UNORM) & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0)` (mandatory for that format) and the same call again returns the same value; `formatFeatures(VK_FORMAT_B8G8R8A8_UNORM)` has `BLIT_DST`; source 2×1 `[black, white]` (opaque) → target 4×1: red channel (byte 2 of each BGRA pixel) `p0 <= 4`, `48 <= p1 <= 80`, `175 <= p2 <= 207`, `p3 >= 251`.
23. "presentBlit leaves the source in TRANSFER_SRC_OPTIMAL": after case 21's submissions, `image->layout() == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL` (the helper returns the image too, or this case repeats the upload and checks it).
24. "presentBlit refuses an image without TRANSFER_SRC": a `GpuImage` with usage `TRANSFER_DST | SAMPLED` → the submission's result is `Errc::Internal`, the message contains `"presentBlit"` and `"lacks the usage"`; no validation message.
25. "presentBlit refuses an image of another device": a second presentation context on the same device; its image in a submission of the first context → `Errc::Internal`, message contains `"belongs to another device"`.

The first failure: `test_presenter_logic.cpp` does not compile (`presenter_logic.h` missing); later `gpu_test_present_blit.cpp` does not compile (`Access::presentBlit`, `formatFeatures`).

**Steps:**

- [ ] **Step 1: Logic tests first**

Write `tests/test_presenter_logic.cpp` (cases 1–19) and add it to `dvv_gpu_tests`. Build `Push-Location dvv-gpu; cmake --workflow --preset vs2026; Pop-Location`: expected compile error (`presenter_logic.h` not found).

- [ ] **Step 2: Implement `PresenterLogic`**

Add `src/presenter_logic.h`, `src/presenter_logic.cpp` (above) and `src/presenter_logic.cpp` to `add_library(dvv_gpu ...)` (alphabetical position after `src/pipeline_cache.cpp`). Run the workflow; expected `100% tests passed, 0 tests failed out of 13`, `dvv_gpu.unit` with 19 more doctest cases. Also run the unit tests on Linux once (`wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd dvv-gpu && cmake --workflow --preset ninja-clang 2>&1 | grep -E "tests passed|tests failed|Error|error:"'`).

```powershell
git -C dvv-gpu add CMakeLists.txt src/presenter_logic.h src/presenter_logic.cpp tests/CMakeLists.txt tests/test_presenter_logic.cpp
@'
feat: add the presenter's decision logic

PresenterLogic decides what the presenter thread does next: wait, retry after an acquire timeout,
recreate the swapchain, render a frame, present the completed one, or exit. It counts GUI requests
by a sequence number so that none is lost while a frame is in flight, recreates the swapchain only
when its hint changed or Vulkan reported out-of-date or suboptimal, treats four out-of-date results
in a row as hidden until the next GUI event, and fails for good on a lost surface or device. It
makes no Vulkan calls, so every rule is a unit test on every platform.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
'@ | git -C dvv-gpu commit -F -
```

- [ ] **Step 3: `presentBlit` tests first**

Write `tests/test_present_blit.cpp` (case 20; joins `dvv_gpu_tests`) and `tests/gpu_test_present_blit.cpp` (cases 21–25; joins `dvv_gpu_device_tests`). Build: expected compile errors (`present_blit.h`, `Access::presentBlit`, `DeviceCore::formatFeatures`).

- [ ] **Step 4: Implement `formatFeatures`, `presentBlit` and the access hook**

As specified above. Run with synchronization validation on the local GPU (background):

```powershell
$env:DVV_VK_VALIDATION = 'sync'; $env:DVV_REQUIRE_GPU = '1'; Push-Location dvv-gpu; cmake --workflow --preset vs2026; Pop-Location; ctest --test-dir _build/dvv-gpu/vs2026 -C Debug -R 'dvv_gpu\.gpu$' -V | Select-String 'test cases|skipped|presentBlit'; Remove-Item Env:DVV_VK_VALIDATION, Env:DVV_REQUIRE_GPU
```

Expected: `100% tests passed, 0 tests failed out of 13` (Debug and Release); the GPU summary has 5 more passed cases, none skipped, and no Vulkan validation line in the output.

- [ ] **Step 5: Full matrix, lint and commit**

`git -C dvv-gpu add -A`, then in parallel (background calls):

```powershell
$env:DVV_VK_VALIDATION = 'sync'; Push-Location dvv-gpu; cmake --workflow --preset vs2026-asan; Pop-Location; Remove-Item Env:DVV_VK_VALIDATION
```

```powershell
wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && export DVV_REQUIRE_GPU=1 DVV_VK_VALIDATION=sync DVV_VK_VALIDATION_REQUIRED=1 && cd dvv-gpu && for p in ninja-clang ninja-clang-asan ninja-clang-tsan; do cmake --workflow --preset $p 2>&1 | grep -E "tests passed|tests failed|Error|error:"; done'
```

Lint: write `_build/lint/dvv-gpu-format.txt` and `_build/lint/dvv-gpu-tidy.txt` in PowerShell exactly as in Task 7 Step 7 (`git -C dvv-gpu ls-files -- '*.h' '*.cpp'` and `git -C dvv-gpu ls-files -- ':(glob)src/*.cpp' ':(glob)tests/*.cpp' ':(glob)tools/*.cpp'`, joined with `"`n"`, `[IO.File]::WriteAllText`), then run the two WSL commands of Task 7 Step 7 (`xargs -a ../_build/lint/dvv-gpu-format.txt -d "\n" -P 4 -n 8 clang-format --dry-run --Werror` and `xargs -a ../_build/lint/dvv-gpu-tidy.txt -d "\n" -P 4 -n 1 clang-tidy -p ../_build/dvv-gpu/ninja-clang --quiet`) in parallel.

Expected: `vs2026-asan` `out of 12`; Linux `out of 13` (ninja-clang, Debug and Release) and `out of 12` (ASan, TSan) with the GPU tests running on lavapipe under sync validation; `FORMAT-OK`, `TIDY-OK`.

```powershell
git -C dvv-gpu add -A
@'
feat: blit a frame into a swapchain image with presentBlit

presentBlit is an internal recorder operation for the presenter: it moves the client's image to
TRANSFER_SRC_OPTIMAL like every tracked image, discards the swapchain image into
TRANSFER_DST_OPTIMAL, blits the whole image (nearest for 1:1, linear for scaling when the format
allows it) and leaves the swapchain image in PRESENT_SRC_KHR. Images of another device, without
TRANSFER_SRC usage or with a format that cannot be a blit source fail the submission. The device
caches the format features it is asked for.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
'@ | git -C dvv-gpu commit -F -
cmake -DREPO=dvv-gpu -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake
```

Expected: `-- privacy-lint: clean (commits)`.

**Expected results:** dvv-gpu still 13 CTest tests (12 under sanitizers); `dvv_gpu.unit` +20 doctest cases (19 logic, 1 filter) on every platform; `dvv_gpu.gpu` +5 cases, run (not skipped) on NVIDIA and lavapipe with synchronization validation and zero messages.

**Commit:** two commits (Steps 2 and 5).

**Notes:** Design D4 (fence acquire, host wait, semaphore-less present), D5 (one frame in flight), D6 (client image blitted, `presentBlit` internal through `detail::Access`), D10 (pure logic), §4.5 (`presentBlit` contract), §5.1–5.3 (request counter, internal re-request, recreation trigger), §5.7 (all 15 logic cases: cases 1–15 map to tests 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 8/17, 18, 13/14). Plan choices: (a) `retryElapsed()` and `inFlight()`/`extentHint()` are added to the indicative signatures (the thread needs them to leave `WaitAndRetry`, to build its wait predicate, and to pass the hint to the swapchain creation); (b) the out-of-date streak counts acquire and present results alike and is reset by any Success or Suboptimal; (c) a failed presenter's `next()` is `Exit` (design case 8 "then only Exit"), so the thread tears down at once and `failure()` stays readable; (d) `presentBlit` also refuses a source format without `BLIT_SRC` and a null or empty target, which would otherwise be invalid usage (both `Internal`, like the design's other image errors); (e) the format-feature cache lives in `DeviceCore`, shared with Task 9's image check. Risk (R6): if synchronization validation reports a hazard for the test's own `PRESENT_SRC_KHR → TRANSFER_SRC_OPTIMAL` barrier in submission 2 (it relies on the timeline wait between the submissions), record it, keep `srcStageMask = ALL_COMMANDS`, and raise it to the arbiter before changing `presentBlit` itself; the presenter's real path (Task 9) has the same ordering through the acquire fence and the host wait.

### Task 9: Presenter: thread loop, every Action, stop

**Goal:** Add `dvv::gpu::Presenter`: the presenter thread executes every `PresenterLogic` action (Wait, WaitAndRetry, RecreateSwapchain, RenderFrame, Present, Exit), owns surface, swapchain and the acquire fence, and stops without hanging, also on Windows where the window thread must keep serving sent messages. Prove it with the new `dvv_gpu_present_tests` executable (design §8.2 cases 1, 6, 7, 9 and 10).

**Files** (all in `dvv-gpu`):
- Create `include/dvv/gpu/presenter.h`: public API (design §4.5), given in full below.
- Modify `include/dvv/gpu/execution_context.h`: include `presenter.h` and declare `createPresenter`.
- Create `src/presenter_core.h`, `src/presenter_core.cpp`: the pure helpers (format, image count, extent, composite alpha), `PresenterMailbox`, `FrameJob`, `PresenterCore` (thread loop, swapchain, acquire/present, stop).
- Create `src/thread_wait.h`, `src/thread_wait_win32.cpp`: the Windows wait that dispatches sent messages only (design §5.5, R15).
- Create `src/presenter.cpp`: `Presenter` and `ExecutionContext::createPresenter`.
- Modify `CMakeLists.txt`: new sources, `thread_wait_win32.cpp` and `user32` on Windows only.
- Modify `tests/CMakeLists.txt`: `test_presenter_core.cpp` in `dvv_gpu_tests`; the new `dvv_gpu_present_tests` executable and CTest `dvv_gpu.present`; compile-fail case 8.
- Create `tests/test_presenter_core.cpp` (unit), `tests/present_fixture.h`, `tests/present_fixture.cpp`, `tests/present_main.cpp`, `tests/present_test_presenter.cpp`.
- Modify `tests/compile_fail/direct_construction.cpp`: case 8.

**Interfaces**

Consumes:
- Task 6 (dvv-platform): `dvv/platform/native_window.h` (`WindowSystem`, `windowSystemName`, `defaultWindowSystem`, `PixelSize`, `Win32Window`, `XlibWindow`, `NativeWindow`, `windowSystemOf`), `dvv/platform/surface.h` (`createSurface(VkInstance, PFN_vkGetInstanceProcAddr, const NativeWindow&) noexcept -> std::expected<VkSurfaceKHR, VkResult>`), target `dvv::platform_testing` with `dvv::platform::testing::TestWindow` (`create`, `native`, `clientSize`, `resize`, `minimise`, `restore`, `pump`).
- Task 7 (dvv-gpu): `GpuRegistryOptions::windowSystem`, `GpuRegistry::windowSystem()`, `ContextOptions::presentation`, `ExecutionContext::presentation()`, `GpuInfo::optional.swapchain`; the instance enables the surface extensions and the device `VK_KHR_swapchain`, so `DeviceCore::dispatch()` has the WSI function pointers.
- Task 8 (dvv-gpu): `src/presenter_logic.h` with `detail::VkOutcome` and `detail::PresenterLogic` (design §5.7), and `detail::Access::presentBlit(CommandRecorder& recorder, GpuImage& source, VkImage swapchain_image, VkExtent2D swapchain_extent)` (design §4.5), and `DeviceCore::formatFeatures` (the image and swapchain-format blit checks).
- The core relies on this `PresenterLogic` contract. Check it against Task 8's header and tests in Step 1. If Task 8 behaves differently, adapt the core, not the logic, and note the difference in the commit message:
  1. Default-constructible: no swapchain, Idle, `servedSeq` 0, hidden until the first `setRequests`.
  2. `next()` order: InFlight → `Wait`; failed or stop → `Exit`; PresentPending → `Present`; retry pending → `WaitAndRetry`; hidden or no frame pending → `Wait`; no swapchain, recreate mark (out-of-date/suboptimal) or hint ≠ hint of the last creation → `RecreateSwapchain`; else `RenderFrame`. `swapchainCreated()` sets the internal re-request.
  3. After `acquired(Timeout)`, `next()` returns `WaitAndRetry` until `retryElapsed()`; `setRequests()` does not clear it. The core calls `retryElapsed()` after the 10 ms wait (F1).
  4. After `frameCompleted(std::nullopt)`, `next()` returns `Present` without stop and `Exit` with stop.
  5. `acquired`/`presented` with `SurfaceLost`, `DeviceLost`, `OtherError` and `frameCompleted(error)` mark it failed; `next()` then returns `Exit` at once, or `Wait` while a frame is in flight.
  6. Task 8 has no dedicated failure method. The core reports a failed caps query, swapchain creation, queue-idle wait or an exception in `step` through `acquired(<failure outcome>)`.

Produces (later tasks rely on these):
- The public header `dvv/gpu/presenter.h` exactly as below; `ExecutionContext::createPresenter(PresenterOptions)`.
- `dvv_gpu_present_tests` with its fixture `dvv::gpu::test::PresentTest`, `testFrameProvider`, `ProviderControl`, `ErrorLog`, `pumpUntil` (Task 10 adds cases to it). CTest `dvv_gpu.present`, labels `gpu;display`, `SKIP_RETURN_CODE 77`.
- Compile-fail case `dvv_gpu.compile_fail.8` (direct `Presenter` construction).

**Behaviour** (binding: design §5.1-§5.5, §7)
- `createPresenter` order (design §6: fallible work before the `noexcept` constructor):
  1. Checks, each `Errc::Internal`: the context has presentation; `options.frames` is not empty; `windowSystemOf(options.window)` has a value equal to `registry()->windowSystem()` (so `monostate` and the other platform's window both fail).
  2. `make_unique<PresenterCore>`. The core only allocates here.
  3. `core->initialise()`: surface, then the checks and the fence. Surface errors convert through `vkError`: `EXTENSION_NOT_PRESENT` and `INITIALIZATION_FAILED` become `Unsupported`, OOM becomes `OutOfMemory`, `NATIVE_WINDOW_IN_USE` becomes `Internal`. Then: the queue family cannot present gives `Unsupported`; no `TRANSFER_DST` swapchain usage gives `Unsupported`; no surface format gives `Unsupported`; the chosen format without `BLIT_DST` gives `Unsupported`. Then the fence is created.
  4. `core->start()`: `std::system_error` becomes `Internal`, `std::bad_alloc` becomes `OutOfMemory`.
  5. `make_unique<Presenter>` with `CreationKey<ExecutionContext>(LifetimeRegistry::instance().issue<Presenter>(objectId()))`.
  On every failure the core's destructor releases whatever exists.
- Format (D16): the first entry of the surface's list that is `B8G8R8A8_UNORM` or `R8G8B8A8_UNORM` with `SRGB_NONLINEAR`, else the first entry. **Note (plan choice):** list order decides between the two UNORM formats.
- Image count: `max(minImageCount + 1, 2)`, capped at `maxImageCount` when it is not 0.
- Composite alpha: OPAQUE, else INHERIT. **Note (plan choice):** else PRE_MULTIPLIED, else POST_MULTIPLIED, so creation never fails for this reason.
- Extent: `currentExtent`; when it is `0xFFFFFFFF`, the hint clamped to `[minImageExtent, maxImageExtent]` (D8).
- The loop, RenderFrame steps 1-5, completion, Present, recreation, surface lost and stop follow design §5.2-§5.5 exactly, as coded below.
- **Note (plan choice):** a frame provider that throws is treated like a provider error: `std::bad_alloc` gives `OutOfMemory`, any other `std::exception` gives `Internal`. The frame is skipped and `onError` is called.
- `onError` is called on the presenter thread, never under the presenter mutex. Frame errors call it each time. A failure calls it once.
- `stats()` and `failure()` copy under the presenter mutex. `resize`, `setVisible` and `requestFrame` each set their field, increment `requestSeq` under the mutex and notify; they never block on Vulkan.
- The thread never calls Vulkan while it holds the presenter mutex. `vkQueuePresentKHR` and `vkQueueWaitIdle` run under `DeviceCore::lockQueue()`.

**Code**

`include/dvv/gpu/presenter.h` (complete):

```cpp
#pragma once

// Presentation to one native window (spec §4, §10.2): a Presenter runs its own thread that
// acquires swapchain images, hands each frame's commands to the context's submit thread and
// presents. One frame is in flight at a time.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <vulkan/vulkan_core.h>

#include "dvv/base/error.h"
#include "dvv/base/lifetime.h"
#include "dvv/gpu/resources.h"
#include "dvv/gpu/submission.h"
#include "dvv/platform/native_window.h"

namespace dvv::gpu {

class ExecutionContext;

namespace detail {
class PresenterCore;
struct Access;
}  // namespace detail

struct PresentFrameInfo {
    platform::PixelSize extent;  // swapchain extent: render at this size for a 1:1 blit
    VkFormat format;             // swapchain format (informational; the blit converts)
    std::uint64_t frameNumber;   // 0, 1, 2, ... per provider call
    // True only when the previous provider call's frame was submitted and completed (its record
    // function ran). False after a dropped frame, a provider error, and for the first call.
    bool previousFrameCompleted;
};

// `image` is blitted to the swapchain image (linear scaling when sizes differ and the format has
// SAMPLED_IMAGE_FILTER_LINEAR, else nearest). It must belong to this context, have TRANSFER_SRC
// usage and a format with BLIT_SRC (e.g. R8G8B8A8_UNORM). `record` (may be empty) runs first in
// the same submission. Both are released on the presenter thread once the submission has
// completed.
struct PresentFrame {
    RecordFunction record{};
    std::shared_ptr<GpuImage> image{};
};

// Called on the presenter thread, at most once per frame and never while a frame is in flight (so
// one image can be reused). Must not block long (a new image on resize is fine). An error skips
// the frame and goes to onError. The provider is called before the acquire: the record function of
// a dropped frame is destroyed without running (see PresentFrameInfo::previousFrameCompleted).
using FrameProvider = std::move_only_function<Result<PresentFrame>(const PresentFrameInfo&)>;
// Called on the presenter thread; the GUI forwards it (e.g. wx CallAfter).
using PresenterErrorHandler = std::move_only_function<void(const Error&) noexcept>;

struct PresenterOptions {
    platform::NativeWindow window{};  // must outlive the presenter
    platform::PixelSize extent{};     // current client size in physical pixels (a hint)
    bool visible = true;
    FrameProvider frames{};
    PresenterErrorHandler onError{};
    std::string name = "presenter";  // debug names and log lines
};

struct PresenterStats {
    std::uint64_t framesPresented = 0;
    std::uint64_t frameErrors = 0;          // frames skipped (provider error or unusable image)
    std::uint64_t swapchainsCreated = 0;    // first creation included
    platform::PixelSize presentedExtent{};  // of the latest presented frame
    bool hidden = false;                    // not visible, extent 0, or surface extent 0
    bool failed = false;                    // stopped presenting; see failure()
};

// Presents to one native window on its own thread (spec §4): acquire, hand the frame's commands to
// the submit thread, present, recreate the swapchain on resize/out-of-date/suboptimal. Created by
// ExecutionContext::createPresenter(); keeps the context alive. resize, setVisible, requestFrame
// and stats may be called from any thread.
class Presenter final : public Managed<Presenter> {
public:
    Presenter(CreationKey<ExecutionContext> key, std::shared_ptr<ExecutionContext> context,
              std::unique_ptr<detail::PresenterCore> core) noexcept;
    // Waits for the frame in flight (about one frame) and queue idle, destroys swapchain and
    // surface. Destroy it before the window. On Windows the wait dispatches cross-thread sent
    // messages only.
    ~Presenter();

    void resize(platform::PixelSize extent) noexcept;  // hint; latest wins; requests a frame
    void setVisible(bool visible) noexcept;  // hidden: no frames, no acquires; requests a frame
    void requestFrame() noexcept;            // coalesced until the next frame starts
    [[nodiscard]] PresenterStats stats() const;
    [[nodiscard]] std::optional<Error> failure() const;  // why presenting stopped
    [[nodiscard]] const std::shared_ptr<ExecutionContext>& context() const noexcept {
        return context_;
    }

private:
    friend struct detail::Access;
    // `context_` is declared before `core_` on purpose: the core (thread, swapchain, surface) is
    // destroyed first, while the device and instance are alive.
    std::shared_ptr<ExecutionContext> context_;
    std::unique_ptr<detail::PresenterCore> core_;
};

}  // namespace dvv::gpu
```

`include/dvv/gpu/execution_context.h`: add `#include "dvv/gpu/presenter.h"` with the other includes. `presenter.h` does not include `execution_context.h`, so there is no cycle. Add this public member below `presentation()` (Task 7):

```cpp
    // A presenter for one native window (spec §4, §10.2; see presenter.h). Errc::Internal on a
    // context without ContextOptions::presentation, for an empty frame provider and for a window of
    // another window system (or none); Errc::Unsupported when the device's queue cannot present to
    // the window or no swapchain format and usage fit (TRANSFER_DST usage, BLIT_DST format);
    // surface errors as converted by dvv-gpu (a window system the loader lacks: Unsupported); a
    // presenter thread that cannot start: Internal (OutOfMemory for std::bad_alloc).
    [[nodiscard]] Result<std::unique_ptr<Presenter>> createPresenter(PresenterOptions options);
```

`src/thread_wait.h` (complete):

```cpp
#pragma once

// Waiting for a thread on Windows while serving cross-thread sent messages (design M1b §5.5, R15).

namespace dvv::gpu::detail {

#if defined(_WIN32)
// Returns once the thread whose HANDLE is `thread` has ended. Meanwhile it dispatches only the
// messages other threads send to the calling thread (SendMessage, e.g. from DXGI-based WSI to the
// window's thread); posted messages (input, wx events) stay queued. A failed wait is logged and
// returns; the caller's join() then waits without dispatching.
void waitForThreadDispatchingSentMessages(void* thread) noexcept;
#endif

}  // namespace dvv::gpu::detail
```

`src/thread_wait_win32.cpp` (complete; Windows only; its own translation unit, so `<windows.h>` never meets `vulkan.hpp`):

```cpp
#include "thread_wait.h"

#include <windows.h>

#include "dvv/base/log.h"

namespace dvv::gpu::detail {

void waitForThreadDispatchingSentMessages(void* thread) noexcept {
    const HANDLE handle = thread;
    while (true) {
        const DWORD woken =
            MsgWaitForMultipleObjectsEx(1, &handle, INFINITE, QS_SENDMESSAGE, MWMO_INPUTAVAILABLE);
        if (woken == WAIT_OBJECT_0) {
            return;  // the thread has ended
        }
        if (woken == WAIT_OBJECT_0 + 1) {
            // PeekMessageW dispatches the pending sent messages; with PM_QS_SENDMESSAGE and
            // PM_NOREMOVE it neither removes nor dispatches posted ones.
            MSG message{};
            static_cast<void>(
                PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE));
            continue;
        }
        DVV_LOG_ERROR("waiting for a presenter thread failed (Windows error {}); joining without "
                      "dispatching messages",
                      GetLastError());
        return;
    }
}

}  // namespace dvv::gpu::detail
```

`src/presenter_core.h` (complete):

```cpp
#pragma once

// The thread behind a Presenter (design M1b §5): surface, swapchain, acquire fence and the loop that
// executes PresenterLogic's actions. An internal part of Presenter (spec §9.4: exempt, owned only by
// its Presenter, created only by ExecutionContext::createPresenter()).
//
// Threads: the presenter thread owns every Vulkan object here and the logic; the GUI thread only
// sets the request block (resize, setVisible, requestFrame) and reads stats and failure; the
// context's waiter thread posts frame completions into the mailbox. The request block, mailbox,
// stats and failure are guarded by one mutex. No Vulkan call is made under it.

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "dvv/base/error.h"
#include "dvv/base/executor.h"
#include "dvv/gpu/presenter.h"
#include "presenter_logic.h"
#include "vk.h"

namespace dvv::gpu {
class ExecutionContext;
}  // namespace dvv::gpu

namespace dvv::gpu::detail {

class DeviceCore;
class InstanceCore;
class PresenterCore;

// D16: the first B8G8R8A8_UNORM or R8G8B8A8_UNORM format with SRGB_NONLINEAR, else the first;
// nullopt for an empty list.
[[nodiscard]] std::optional<VkSurfaceFormatKHR> chooseSurfaceFormat(
    std::span<const VkSurfaceFormatKHR> formats) noexcept;
// max(minImageCount + 1, 2), capped at maxImageCount when that is not 0.
[[nodiscard]] std::uint32_t swapchainImageCount(const VkSurfaceCapabilitiesKHR& caps) noexcept;
// currentExtent; when it is 0xFFFFFFFF, the hint clamped to [minImageExtent, maxImageExtent] (D8).
[[nodiscard]] VkExtent2D swapchainExtent(const VkSurfaceCapabilitiesKHR& caps,
                                         platform::PixelSize hint) noexcept;
// OPAQUE, else INHERIT, else PRE_MULTIPLIED, else POST_MULTIPLIED.
[[nodiscard]] VkCompositeAlphaFlagBitsKHR compositeAlpha(VkCompositeAlphaFlagsKHR supported) noexcept;

// The presenter's executor (design §5.1): the waiter thread posts frame completions here, and the
// presenter thread runs them. post() pushes under the presenter mutex and notifies; it never runs
// work inline.
class PresenterMailbox final : public IExecutor {
public:
    explicit PresenterMailbox(PresenterCore& core) noexcept : core_(&core) {}
    void post(std::move_only_function<void()> work) override;

private:
    PresenterCore* core_;
};

class PresenterCore {
public:
    // Allocates only; no Vulkan object exists yet. `context` and `instance` outlive the core: the
    // Presenter holds the context, which holds the registry and its instance.
    PresenterCore(ExecutionContext& context, std::shared_ptr<DeviceCore> device,
                  const InstanceCore& instance, PresenterOptions options);
    PresenterCore(const PresenterCore&) = delete;
    PresenterCore& operator=(const PresenterCore&) = delete;
    PresenterCore(PresenterCore&&) = delete;
    PresenterCore& operator=(PresenterCore&&) = delete;
    // stop(), then destroys what still exists (only a core whose thread never ran has anything).
    ~PresenterCore();

    // Surface, present support, swapchain usage and format, acquire fence (errors: see
    // ExecutionContext::createPresenter).
    [[nodiscard]] Result<void> initialise();
    // Starts the presenter thread; throws std::system_error or std::bad_alloc.
    void start();
    // Sets stop and waits until the thread has ended (design §5.5), dispatching sent messages on
    // Windows. Idempotent; never called on the presenter thread.
    void stop() noexcept;

    void resize(platform::PixelSize extent) noexcept;
    void setVisible(bool visible) noexcept;
    void requestFrame() noexcept;
    [[nodiscard]] PresenterStats stats() const;
    [[nodiscard]] std::optional<Error> failure() const;

    // The mailbox: queues work for the presenter thread; throws std::bad_alloc when it cannot.
    void post(std::move_only_function<void()> work);
    // Called by the frame's coroutine on the presenter thread when its submission has finished.
    void frameFinished(Result<std::uint64_t> result) noexcept;

private:
    using Action = PresenterLogic::Action;

    // The request block (design §5.1). Every resize, setVisible and requestFrame increments
    // requestSeq.
    struct Requests {
        platform::PixelSize extent{};
        bool visible = true;
        std::uint64_t requestSeq = 0;
        bool stop = false;
    };

    void run() noexcept;
    void waitForWork(std::unique_lock<std::mutex>& lock);
    void step(Action action);
    void renderFrame();
    [[nodiscard]] Result<PresentFrame> callProvider(const PresentFrameInfo& info);
    [[nodiscard]] Result<void> checkImage(const std::shared_ptr<GpuImage>& image);
    [[nodiscard]] VkOutcome acquire(std::uint32_t& index);
    void startFrame(PresentFrame frame, std::uint32_t index) noexcept;
    void processCompletion() noexcept;
    void present();
    void recreateSwapchain();
    void failSwapchain(VkResult result, std::string_view what);
    void teardown() noexcept;
    void releaseVulkanObjects() noexcept;
    void fail(Error error) noexcept;
    void reportFrameError(const Error& error);
    void publishState();

    // Fixed after construction.
    ExecutionContext* context_;
    std::shared_ptr<DeviceCore> device_;
    VkInstance instance_;
    PFN_vkGetInstanceProcAddr get_instance_proc_addr_;
    platform::NativeWindow window_;
    FrameProvider frames_;
    PresenterErrorHandler on_error_;
    std::string name_;
    PresenterMailbox mailbox_executor_;

    // Presenter thread only (and initialise()/the destructor while no thread runs).
    PresenterLogic logic_;
    Requests snapshot_{};
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSurfaceFormatKHR format_{};
    VkFence fence_ = VK_NULL_HANDLE;
    bool fence_leaked_ = false;  // the 5 s cap of §5.2 step 4 passed: never destroyed
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> images_;
    VkExtent2D extent_{};
    std::uint32_t acquired_index_ = 0;
    bool in_flight_ = false;
    std::optional<Result<std::uint64_t>> completion_;
    std::uint64_t frame_number_ = 0;

    // Shared, under mutex_.
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    Requests requests_{};
    std::vector<std::move_only_function<void()>> mailbox_;
    PresenterStats stats_{};
    std::optional<Error> failure_;

    std::jthread thread_;  // started by start(), joined by stop()
};

}  // namespace dvv::gpu::detail
```

`src/presenter_core.cpp` (complete; the implementer may reword comments and messages, but must keep the order of operations, the locking and the outcomes):

```cpp
#include "presenter_core.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <coroutine>
#include <exception>
#include <format>
#include <new>
#include <utility>

#include "access.h"
#include "device_core.h"
#include "dvv/base/log.h"
#include "dvv/gpu/command_recorder.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/platform/surface.h"
#include "instance.h"
#include "thread_wait.h"
#include "vk_result.h"

namespace dvv::gpu::detail {

namespace {

using namespace std::chrono_literals;

constexpr std::uint64_t ACQUIRE_TIMEOUT_NS = 100'000'000;  // §5.2 step 3
constexpr std::uint64_t FENCE_SLICE_NS = 100'000'000;      // §5.2 step 4
constexpr auto FENCE_CAP = 5s;                             // §5.2 step 4
constexpr auto RETRY_DELAY = 10ms;                         // §5.2 WaitAndRetry
constexpr std::uint32_t UNDEFINED_EXTENT = 0xFFFFFFFFU;

Error surfaceLost() {
    return makeError(Errc::Internal,
                     "the window's Vulkan surface was lost (was the window destroyed before its "
                     "presenter?)")
        .error();
}

// detail::FrameJob (design §5.1): a fire-and-forget coroutine that awaits one frame's submission.
// It runs at once (suspend_never), suspends in co_await submit(), is resumed on the presenter
// thread by the mailbox once the GPU has finished, and frees its frame when it returns. The body
// catches everything, so unhandled_exception() is unreachable.
struct FrameJob {
    struct promise_type {
        FrameJob get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
    };
};

// Pointers, not references: the coroutine frame keeps its parameters. The core outlives the job
// (its thread never exits with a frame in flight); the context outlives the core.
FrameJob runFrame(PresenterCore* core, ExecutionContext* context, IExecutor* mailbox,
                  RecordFunction record) {
    Result<std::uint64_t> result{};
    try {
        result = co_await context->submit(*mailbox, std::move(record));
    } catch (const std::bad_alloc&) {
        // SubmitOperation::await_suspend could not queue the submission (§5.1).
        result = makeError(Errc::OutOfMemory, "queuing a presenter frame: out of memory");
    } catch (const std::exception& error) {
        result = makeError(Errc::Internal,
                           std::format("queuing a presenter frame failed: {}", error.what()));
    } catch (...) {
        result = makeError(Errc::Internal, "queuing a presenter frame failed");
    }
    core->frameFinished(std::move(result));
}

}  // namespace

std::optional<VkSurfaceFormatKHR> chooseSurfaceFormat(
    std::span<const VkSurfaceFormatKHR> formats) noexcept {
    for (const VkSurfaceFormatKHR& format : formats) {
        if ((format.format == VK_FORMAT_B8G8R8A8_UNORM ||
             format.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    if (formats.empty()) {
        return std::nullopt;
    }
    return formats.front();
}

std::uint32_t swapchainImageCount(const VkSurfaceCapabilitiesKHR& caps) noexcept {
    std::uint32_t count = std::max(caps.minImageCount + 1, 2U);
    if (caps.maxImageCount != 0) {
        count = std::min(count, caps.maxImageCount);
    }
    return count;
}

VkExtent2D swapchainExtent(const VkSurfaceCapabilitiesKHR& caps,
                           platform::PixelSize hint) noexcept {
    if (caps.currentExtent.width != UNDEFINED_EXTENT) {
        return caps.currentExtent;
    }
    return {.width = std::clamp(hint.width, caps.minImageExtent.width, caps.maxImageExtent.width),
            .height =
                std::clamp(hint.height, caps.minImageExtent.height, caps.maxImageExtent.height)};
}

VkCompositeAlphaFlagBitsKHR compositeAlpha(VkCompositeAlphaFlagsKHR supported) noexcept {
    for (const VkCompositeAlphaFlagBitsKHR bit :
         {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
          VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
        if ((supported & bit) != 0) {
            return bit;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;  // the specification requires at least one bit
}

void PresenterMailbox::post(std::move_only_function<void()> work) {
    core_->post(std::move(work));
}

PresenterCore::PresenterCore(ExecutionContext& context, std::shared_ptr<DeviceCore> device,
                             const InstanceCore& instance, PresenterOptions options)
    : context_(&context),
      device_(std::move(device)),
      instance_(static_cast<VkInstance>(instance.instance())),
      get_instance_proc_addr_(instance.dispatch().vkGetInstanceProcAddr),
      window_(options.window),
      frames_(std::move(options.frames)),
      on_error_(std::move(options.onError)),
      name_(std::move(options.name)),
      mailbox_executor_(*this) {
    // The presenter starts with one pending request (§5.1): requestSeq 1, snapshot 0.
    requests_ = {.extent = options.extent, .visible = options.visible, .requestSeq = 1};
    stats_.hidden = !options.visible || options.extent.empty();
}

PresenterCore::~PresenterCore() {
    stop();
    releaseVulkanObjects();
}

Result<void> PresenterCore::initialise() {
    const auto surface = platform::createSurface(instance_, get_instance_proc_addr_, window_);
    if (!surface) {
        return vkError(surface.error(),
                       std::format("creating the window surface of presenter '{}'", name_));
    }
    surface_ = *surface;
    const auto& api = device_->dispatch();
    const auto physical = static_cast<VkPhysicalDevice>(device_->physicalDevice());

    VkBool32 supported = VK_FALSE;
    DVV_TRY_VOID(vkCheck(api.vkGetPhysicalDeviceSurfaceSupportKHR(
                             physical, device_->queueFamily(), surface_, &supported),
                         "vkGetPhysicalDeviceSurfaceSupportKHR"));
    if (supported == VK_FALSE) {
        return makeError(Errc::Unsupported,
                         std::format("presenter '{}': the queue of device {} cannot present to "
                                     "the window",
                                     name_, device_->id()));
    }
    VkSurfaceCapabilitiesKHR caps{};
    DVV_TRY_VOID(
        vkCheck(api.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface_, &caps),
                "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
    if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
        return makeError(Errc::Unsupported,
                         std::format("presenter '{}': swapchain images of device {} cannot be "
                                     "transfer destinations",
                                     name_, device_->id()));
    }
    std::vector<VkSurfaceFormatKHR> formats;
    VkResult listed = VK_INCOMPLETE;
    while (listed == VK_INCOMPLETE) {
        std::uint32_t count = 0;
        DVV_TRY_VOID(vkCheck(
            api.vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface_, &count, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR"));
        formats.resize(count);
        listed = api.vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface_, &count,
                                                          formats.data());
        formats.resize(count);
    }
    DVV_TRY_VOID(vkCheck(listed, "vkGetPhysicalDeviceSurfaceFormatsKHR"));
    const std::optional<VkSurfaceFormatKHR> format = chooseSurfaceFormat(formats);
    if (!format) {
        return makeError(Errc::Unsupported,
                         std::format("presenter '{}': the window has no surface format", name_));
    }
    if ((device_->formatFeatures(format->format) & VK_FORMAT_FEATURE_2_BLIT_DST_BIT) == 0) {
        return makeError(Errc::Unsupported,
                         std::format("presenter '{}': the swapchain format {} cannot be a blit "
                                     "destination",
                                     name_, vk::to_string(static_cast<vk::Format>(format->format))));
    }
    format_ = *format;
    const VkFenceCreateInfo fence_info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    DVV_TRY_VOID(vkCheck(api.vkCreateFence(static_cast<VkDevice>(device_->device()), &fence_info,
                                           nullptr, &fence_),
                         "vkCreateFence"));
    device_->setDebugName(vk::Fence(fence_), name_ + " acquire fence");
    return {};
}

void PresenterCore::start() {
    thread_ = std::jthread([this] { run(); });
}

void PresenterCore::stop() noexcept {
    if (!thread_.joinable()) {
        return;
    }
    assert(thread_.get_id() != std::this_thread::get_id() &&
           "a presenter must not be destroyed on its own thread");
    {
        const std::lock_guard lock(mutex_);
        requests_.stop = true;
    }
    wake_.notify_all();
#if defined(_WIN32)
    // R15: the presenter thread may be inside vkAcquireNextImageKHR or vkQueuePresentKHR, waiting
    // for a message that the WSI sends to this thread (the window's thread).
    waitForThreadDispatchingSentMessages(thread_.native_handle());
#endif
    thread_.join();
}

void PresenterCore::resize(platform::PixelSize extent) noexcept {
    {
        const std::lock_guard lock(mutex_);
        requests_.extent = extent;
        ++requests_.requestSeq;
    }
    wake_.notify_one();
}

void PresenterCore::setVisible(bool visible) noexcept {
    {
        const std::lock_guard lock(mutex_);
        requests_.visible = visible;
        ++requests_.requestSeq;  // setVisible(true) requests a frame (§5.1)
    }
    wake_.notify_one();
}

void PresenterCore::requestFrame() noexcept {
    {
        const std::lock_guard lock(mutex_);
        ++requests_.requestSeq;
    }
    wake_.notify_one();
}

PresenterStats PresenterCore::stats() const {
    const std::lock_guard lock(mutex_);
    return stats_;
}

std::optional<Error> PresenterCore::failure() const {
    const std::lock_guard lock(mutex_);
    return failure_;
}

void PresenterCore::post(std::move_only_function<void()> work) {
    {
        const std::lock_guard lock(mutex_);
        mailbox_.push_back(std::move(work));
    }
    wake_.notify_one();
}

void PresenterCore::frameFinished(Result<std::uint64_t> result) noexcept {
    completion_ = std::move(result);
}

// §5.2: one iteration per wake-up. The thread never exits while a frame is in flight, so a
// suspended FrameJob is never destroyed (SubmitOperation contract).
void PresenterCore::run() noexcept {
    std::vector<std::move_only_function<void()>> work;
    while (true) {
        {
            std::unique_lock lock(mutex_);
            waitForWork(lock);
            snapshot_ = requests_;
            work.swap(mailbox_);  // mailbox_ gets the empty vector back: no allocation
        }
        logic_.setRequests(snapshot_.extent, snapshot_.visible, snapshot_.requestSeq,
                           snapshot_.stop);
        for (auto& item : work) {
            item();  // a frame completion: resumes its FrameJob, which calls frameFinished()
        }
        work.clear();
        processCompletion();
        const Action action = logic_.next();
        if (action == Action::Exit) {
            teardown();
            publishState();
            return;
        }
        // An exception here is an allocation failure outside the guarded spots (swapchain image
        // list, debug names, error texts). No frame is in flight then: startFrame() is noexcept.
        try {
            step(action);
        } catch (const std::bad_alloc&) {
            fail(makeError(Errc::OutOfMemory, "presenter: out of memory").error());
            logic_.acquired(VkOutcome::OtherError);
        } catch (const std::exception& error) {
            fail(makeError(Errc::Internal, std::format("presenter '{}': {}", name_, error.what()))
                     .error());
            logic_.acquired(VkOutcome::OtherError);
        }
        publishState();
    }
}

void PresenterCore::waitForWork(std::unique_lock<std::mutex>& lock) {
    const Action due = logic_.next();  // with the previous snapshot
    if (due != Action::Wait && due != Action::WaitAndRetry) {
        return;  // RecreateSwapchain, RenderFrame, Present or Exit: no wait
    }
    // While stopping with a frame in flight, only its completion matters (§5.2).
    const bool completion_only = snapshot_.stop && in_flight_;
    const auto woken = [this, completion_only] {
        if (!mailbox_.empty()) {
            return true;
        }
        if (completion_only) {
            return false;
        }
        return requests_.requestSeq != snapshot_.requestSeq || requests_.stop != snapshot_.stop;
    };
    if (due == Action::WaitAndRetry) {
        // §5.2: the 10 ms after an acquire timeout. Only stop ends it early; requests are served
        // after it (no frame is in flight, so the mailbox stays empty).
        wake_.wait_for(lock, RETRY_DELAY, [this] { return requests_.stop; });
        logic_.retryElapsed();
        return;
    }
    wake_.wait(lock, woken);
}

void PresenterCore::step(Action action) {
    switch (action) {
        case Action::Wait:
        case Action::WaitAndRetry: return;  // waitForWork() waited and called retryElapsed()
        case Action::RecreateSwapchain: recreateSwapchain(); return;
        case Action::RenderFrame: renderFrame(); return;
        case Action::Present: present(); return;
        case Action::Exit: return;  // run() handles it
    }
}

// §5.2 RenderFrame, steps 1-5.
void PresenterCore::renderFrame() {
    const PresentFrameInfo info{
        .extent = {.width = extent_.width, .height = extent_.height},
        .format = format_.format,
        .frameNumber = frame_number_++,
        .previousFrameCompleted = logic_.previousFrameCompleted()};
    Result<PresentFrame> provided = callProvider(info);  // step 1
    if (provided) {
        if (auto usable = checkImage(provided->image); !usable) {  // step 2
            provided = std::unexpected(std::move(usable).error());  // the frame is released unrun
        }
    }
    if (!provided) {
        reportFrameError(provided.error());
        logic_.frameSkipped();  // the request counts as served; nothing was acquired
        return;
    }
    std::uint32_t index = 0;
    const VkOutcome acquired = acquire(index);  // steps 3 and 4
    logic_.acquired(acquired);
    if (acquired != VkOutcome::Success && acquired != VkOutcome::Suboptimal) {
        return;  // dropped: the record function is destroyed here without running
    }
    logic_.frameStarted();  // step 5
    startFrame(std::move(*provided), index);
}

Result<PresentFrame> PresenterCore::callProvider(const PresentFrameInfo& info) {
    try {
        return frames_(info);
    } catch (const std::bad_alloc&) {
        return makeError(Errc::OutOfMemory,
                         std::format("the frame provider of presenter '{}' ran out of memory",
                                     name_));
    } catch (const std::exception& error) {
        return makeError(Errc::Internal, std::format("the frame provider of presenter '{}' "
                                                     "threw: {}",
                                                     name_, error.what()));
    }
}

Result<void> PresenterCore::checkImage(const std::shared_ptr<GpuImage>& image) {
    if (!image) {
        return makeError(Errc::Internal,
                         std::format("presenter '{}': the frame has no image", name_));
    }
    if (&Access::core(*image) != device_.get()) {
        return makeError(Errc::Internal,
                         std::format("presenter '{}': image '{}' belongs to another context", name_,
                                     image->desc().name));
    }
    if ((image->desc().usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0) {
        return makeError(Errc::Internal,
                         std::format("presenter '{}': image '{}' lacks "
                                     "VK_IMAGE_USAGE_TRANSFER_SRC_BIT",
                                     name_, image->desc().name));
    }
    if ((device_->formatFeatures(image->format()) & VK_FORMAT_FEATURE_2_BLIT_SRC_BIT) == 0) {
        return makeError(Errc::Internal,
                         std::format("presenter '{}': the format {} of image '{}' cannot be a "
                                     "blit source",
                                     name_, vk::to_string(static_cast<vk::Format>(image->format())),
                                     image->desc().name));
    }
    return {};
}

// §5.2 steps 3 and 4: acquire with the fence, then wait for it on the host.
VkOutcome PresenterCore::acquire(std::uint32_t& index) {
    const auto& api = device_->dispatch();
    const auto device = static_cast<VkDevice>(device_->device());
    const VkResult result = api.vkAcquireNextImageKHR(device, swapchain_, ACQUIRE_TIMEOUT_NS,
                                                      VK_NULL_HANDLE, fence_, &index);
    switch (result) {
        case VK_SUCCESS:
        case VK_SUBOPTIMAL_KHR: break;
        case VK_TIMEOUT:
        case VK_NOT_READY: return VkOutcome::Timeout;
        case VK_ERROR_OUT_OF_DATE_KHR: return VkOutcome::OutOfDate;
        case VK_ERROR_SURFACE_LOST_KHR: fail(surfaceLost()); return VkOutcome::SurfaceLost;
        case VK_ERROR_DEVICE_LOST:
            fail(vkError(result, "vkAcquireNextImageKHR").error());
            return VkOutcome::DeviceLost;
        default:
            fail(vkError(result, "vkAcquireNextImageKHR").error());
            return VkOutcome::OtherError;
    }
    // Binding (§5.2 step 4): once an image is acquired, wait for the fence until it signals or the
    // device is lost, also when stop is set; stop is acted on only afterwards.
    const auto deadline = std::chrono::steady_clock::now() + FENCE_CAP;
    while (true) {
        const VkResult waited = api.vkWaitForFences(device, 1, &fence_, VK_TRUE, FENCE_SLICE_NS);
        if (waited == VK_SUCCESS) {
            break;
        }
        if (waited != VK_TIMEOUT) {
            fail(vkError(waited, "vkWaitForFences (acquire fence)").error());
            return waited == VK_ERROR_DEVICE_LOST ? VkOutcome::DeviceLost : VkOutcome::OtherError;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            fence_leaked_ = true;  // destroying a fence the driver may still signal is invalid
            fail(makeError(Errc::Internal, "acquire fence did not signal").error());
            return VkOutcome::OtherError;
        }
    }
    if (const VkResult reset = api.vkResetFences(device, 1, &fence_); reset != VK_SUCCESS) {
        fail(vkError(reset, "vkResetFences").error());
        return reset == VK_ERROR_DEVICE_LOST ? VkOutcome::DeviceLost : VkOutcome::OtherError;
    }
    return result == VK_SUCCESS ? VkOutcome::Success : VkOutcome::Suboptimal;
}

// §5.2 step 5: hand the frame to the submit thread through a FrameJob.
void PresenterCore::startFrame(PresentFrame frame, std::uint32_t index) noexcept {
    in_flight_ = true;
    acquired_index_ = index;
    try {
        RecordFunction record = [client = std::move(frame.record), image = std::move(frame.image),
                                 target = images_[index],
                                 extent = extent_](CommandRecorder& recorder) mutable {
            if (client) {
                client(recorder);
            }
            // D6: blit into the swapchain image and move it to PRESENT_SRC_KHR (Task 8).
            Access::presentBlit(recorder, *image, target, extent);
        };
        runFrame(this, context_, &mailbox_executor_, std::move(record));
    } catch (const std::bad_alloc&) {
        completion_ = makeError(Errc::OutOfMemory, "starting a presenter frame: out of memory");
    } catch (const std::exception& error) {
        completion_ = makeError(Errc::Internal,
                                std::format("starting a presenter frame failed: {}", error.what()));
    }
    processCompletion();  // a frame that could not be queued has completed already
}

// §5.2 step 6, first half: the logic learns the frame's outcome.
void PresenterCore::processCompletion() noexcept {
    if (!completion_) {
        return;
    }
    Result<std::uint64_t> result = std::move(*completion_);
    completion_.reset();
    in_flight_ = false;
    if (result) {
        logic_.frameCompleted(std::nullopt);
        return;
    }
    const Errc code = result.error().code;
    fail(std::move(result).error());  // DeviceLost, Internal or OutOfMemory: Failed (§7)
    logic_.frameCompleted(code);
}

// §5.2 step 6, second half: present without wait semaphores (D4), under the queue lock.
void PresenterCore::present() {
    const VkPresentInfoKHR info{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                .waitSemaphoreCount = 0,
                                .pWaitSemaphores = nullptr,
                                .swapchainCount = 1,
                                .pSwapchains = &swapchain_,
                                .pImageIndices = &acquired_index_};
    VkResult result = VK_SUCCESS;
    {
        const auto queue_lock = device_->lockQueue();
        result = device_->dispatch().vkQueuePresentKHR(static_cast<VkQueue>(device_->queue()),
                                                       &info);
    }
    VkOutcome outcome = VkOutcome::Success;
    switch (result) {
        case VK_SUCCESS: break;
        case VK_SUBOPTIMAL_KHR: outcome = VkOutcome::Suboptimal; break;
        case VK_ERROR_OUT_OF_DATE_KHR: outcome = VkOutcome::OutOfDate; break;
        case VK_ERROR_SURFACE_LOST_KHR:
            fail(surfaceLost());
            outcome = VkOutcome::SurfaceLost;
            break;
        case VK_ERROR_DEVICE_LOST:
            fail(vkError(result, "vkQueuePresentKHR").error());
            outcome = VkOutcome::DeviceLost;
            break;
        default:
            fail(vkError(result, "vkQueuePresentKHR").error());
            outcome = VkOutcome::OtherError;
            break;
    }
    if (outcome == VkOutcome::Success || outcome == VkOutcome::Suboptimal) {
        const std::lock_guard lock(mutex_);
        ++stats_.framesPresented;
        stats_.presentedExtent = {.width = extent_.width, .height = extent_.height};
    }
    logic_.presented(outcome);
}

// §5.3: only in phase Idle (the logic asks for it only then).
void PresenterCore::recreateSwapchain() {
    const auto& api = device_->dispatch();
    const auto device = static_cast<VkDevice>(device_->device());
    VkSurfaceCapabilitiesKHR caps{};
    if (const VkResult result = api.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            static_cast<VkPhysicalDevice>(device_->physicalDevice()), surface_, &caps);
        result != VK_SUCCESS) {
        failSwapchain(result, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        return;
    }
    const VkExtent2D extent = swapchainExtent(caps, snapshot_.extent);
    if (extent.width == 0 || extent.height == 0) {
        logic_.surfaceHidden();  // step 2: keep the old swapchain and the request
        return;
    }
    const VkSwapchainCreateInfoKHR info{.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                                        .surface = surface_,
                                        .minImageCount = swapchainImageCount(caps),
                                        .imageFormat = format_.format,
                                        .imageColorSpace = format_.colorSpace,
                                        .imageExtent = extent,
                                        .imageArrayLayers = 1,
                                        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                        .preTransform = caps.currentTransform,
                                        .compositeAlpha = compositeAlpha(caps.supportedCompositeAlpha),
                                        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
                                        .clipped = VK_TRUE,
                                        .oldSwapchain = swapchain_};
    VkSwapchainKHR created = VK_NULL_HANDLE;
    if (const VkResult result = api.vkCreateSwapchainKHR(device, &info, nullptr, &created);
        result != VK_SUCCESS) {
        // The old swapchain is retired all the same; teardown() destroys it.
        failSwapchain(result, "vkCreateSwapchainKHR");
        return;
    }
    const VkSwapchainKHR old = std::exchange(swapchain_, created);
    images_.clear();
    extent_ = extent;
    if (old != VK_NULL_HANDLE) {
        // D7: presentation is done with the old images once the queue is idle (accepted practice
        // without VK_KHR_swapchain_maintenance1; exact retirement is F3).
        VkResult idle = VK_SUCCESS;
        {
            const auto queue_lock = device_->lockQueue();
            idle = api.vkQueueWaitIdle(static_cast<VkQueue>(device_->queue()));
        }
        api.vkDestroySwapchainKHR(device, old, nullptr);
        if (idle != VK_SUCCESS) {
            failSwapchain(idle, "vkQueueWaitIdle");
            return;
        }
    }
    std::uint32_t count = 0;
    VkResult listed = api.vkGetSwapchainImagesKHR(device, swapchain_, &count, nullptr);
    if (listed == VK_SUCCESS) {
        images_.resize(count);
        listed = api.vkGetSwapchainImagesKHR(device, swapchain_, &count, images_.data());
    }
    if (listed != VK_SUCCESS) {
        failSwapchain(listed, "vkGetSwapchainImagesKHR");
        return;
    }
    device_->setDebugName(vk::SwapchainKHR(swapchain_), name_ + " swapchain");
    for (std::size_t i = 0; i < images_.size(); ++i) {
        device_->setDebugName(vk::Image(images_[i]), std::format("{} swapchain image {}", name_, i));
    }
    {
        const std::lock_guard lock(mutex_);
        ++stats_.swapchainsCreated;
    }
    DVV_LOG_DEBUG("presenter '{}': swapchain {}x{}, {} images", name_, extent.width, extent.height,
                  images_.size());
    logic_.swapchainCreated(snapshot_.extent);  // stores the hint used; internal re-request
}

void PresenterCore::failSwapchain(VkResult result, std::string_view what) {
    if (result == VK_ERROR_SURFACE_LOST_KHR) {
        fail(surfaceLost());
        logic_.acquired(VkOutcome::SurfaceLost);  // plan choice: see the logic contract, rule 6
        return;
    }
    fail(vkError(result, std::format("{} (presenter '{}')", what, name_)).error());
    logic_.acquired(result == VK_ERROR_DEVICE_LOST ? VkOutcome::DeviceLost
                                                   : VkOutcome::OtherError);
}

// §5.5: Exit. No frame is in flight here.
void PresenterCore::teardown() noexcept {
    VkResult idle = VK_SUCCESS;
    {
        const auto queue_lock = device_->lockQueue();
        idle = device_->dispatch().vkQueueWaitIdle(static_cast<VkQueue>(device_->queue()));
    }
    if (idle != VK_SUCCESS) {
        DVV_LOG_ERROR("presenter '{}': vkQueueWaitIdle failed while closing: {}", name_,
                      vk::to_string(static_cast<vk::Result>(idle)));
    }
    releaseVulkanObjects();
}

void PresenterCore::releaseVulkanObjects() noexcept {
    const auto& api = device_->dispatch();
    const auto device = static_cast<VkDevice>(device_->device());
    if (swapchain_ != VK_NULL_HANDLE) {
        api.vkDestroySwapchainKHR(device, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    images_.clear();
    if (fence_ != VK_NULL_HANDLE && !fence_leaked_) {
        api.vkDestroyFence(device, fence_, nullptr);
    }
    fence_ = VK_NULL_HANDLE;
    if (surface_ != VK_NULL_HANDLE) {
        api.vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
}

// Failed is terminal (§7): the first error is kept and reported once.
void PresenterCore::fail(Error error) noexcept {
    {
        const std::lock_guard lock(mutex_);
        if (failure_) {
            return;
        }
        failure_ = std::move(error);
        stats_.failed = true;
    }
    // failure_ is written once, so reading it unlocked from here on is safe.
    DVV_LOG_ERROR("presenter '{}' stopped presenting: {}", name_, failure_->describe());
    if (on_error_) {
        on_error_(*failure_);
    }
}

void PresenterCore::reportFrameError(const Error& error) {
    {
        const std::lock_guard lock(mutex_);
        ++stats_.frameErrors;
    }
    DVV_LOG_WARN("presenter '{}' skipped a frame: {}", name_, error.describe());
    if (on_error_) {
        on_error_(error);
    }
}

void PresenterCore::publishState() {
    const bool hidden = !snapshot_.visible || snapshot_.extent.empty() || logic_.hidden();
    const bool failed = logic_.failed();
    const std::lock_guard lock(mutex_);
    stats_.hidden = hidden;
    stats_.failed = stats_.failed || failed;
}

}  // namespace dvv::gpu::detail
```

`src/presenter.cpp` (complete):

```cpp
#include "dvv/gpu/presenter.h"

#include <format>
#include <new>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

#include "access.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/gpu_registry.h"
#include "presenter_core.h"

namespace dvv::gpu {

static_assert(ManagedType<Presenter>);

Presenter::Presenter(CreationKey<ExecutionContext> key, std::shared_ptr<ExecutionContext> context,
                     std::unique_ptr<detail::PresenterCore> core) noexcept
    : Managed(std::move(key)), context_(std::move(context)), core_(std::move(core)) {}

Presenter::~Presenter() {
    // Joins the thread and destroys swapchain, surface and fence while the context (device and
    // instance) is alive; the member order guarantees the same.
    core_.reset();
}

void Presenter::resize(platform::PixelSize extent) noexcept {
    core_->resize(extent);
}

void Presenter::setVisible(bool visible) noexcept {
    core_->setVisible(visible);
}

void Presenter::requestFrame() noexcept {
    core_->requestFrame();
}

PresenterStats Presenter::stats() const {
    return core_->stats();
}

std::optional<Error> Presenter::failure() const {
    return core_->failure();
}

Result<std::unique_ptr<Presenter>> ExecutionContext::createPresenter(PresenterOptions options) {
    if (!presentation()) {
        return makeError(Errc::Internal,
                         std::format("presenter '{}': the context of device {} was created "
                                     "without ContextOptions::presentation",
                                     options.name, deviceId()));
    }
    if (!options.frames) {
        return makeError(Errc::Internal,
                         std::format("presenter '{}' has no frame provider", options.name));
    }
    const std::optional<platform::WindowSystem> system = platform::windowSystemOf(options.window);
    const std::optional<platform::WindowSystem> wanted = registry_->windowSystem();
    if (!system || system != wanted) {
        const std::string_view have = system ? platform::windowSystemName(*system) : "no window";
        const std::string_view need =
            wanted ? platform::windowSystemName(*wanted) : "no window system";
        return makeError(Errc::Internal,
                         std::format("presenter '{}': the window is {}, the registry presents "
                                     "to {}",
                                     options.name, have, need));
    }
    auto core = std::make_unique<detail::PresenterCore>(
        *this, core_, detail::Access::instance(*registry_), std::move(options));
    DVV_TRY_VOID(core->initialise());
    try {
        core->start();
    } catch (const std::system_error& error) {
        return makeError(Errc::Internal,
                         std::format("the presenter thread could not start: {}", error.what()));
    } catch (const std::bad_alloc&) {
        return makeError(Errc::OutOfMemory, "the presenter thread could not start: out of memory");
    }
    return std::make_unique<Presenter>(
        CreationKey<ExecutionContext>(LifetimeRegistry::instance().issue<Presenter>(objectId())),
        shared_from_this(), std::move(core));
}

}  // namespace dvv::gpu
```

`CMakeLists.txt` (dvv-gpu): add `src/presenter.cpp` and `src/presenter_core.cpp` to the `add_library(dvv_gpu STATIC ...)` list, in alphabetical order (`src/presenter_logic.cpp` is already there from Task 8). After `dvv_set_compiler_options(dvv_gpu)` add:

```cmake
# The presenter's stop waits while dispatching sent messages on Windows (design M1b §5.5, R15).
if(WIN32)
    target_sources(dvv_gpu PRIVATE src/thread_wait_win32.cpp)
    target_link_libraries(dvv_gpu PRIVATE user32)
endif()
```

`tests/CMakeLists.txt`:
- Add `test_presenter_core.cpp` to `dvv_gpu_tests`.
- Insert after the `dvv_gpu.gpu`/`dvv_gpu.gpu_info` block:

```cmake
# Presentation tests (design M1b §8.2): raw native windows from dvv::platform_testing, no wx. They
# exit with 77 without a usable device that can present or without a display, unless
# DVV_REQUIRE_GPU=1. Linux CI runs them under xvfb-run.
add_executable(dvv_gpu_present_tests
    present_fixture.cpp
    present_main.cpp
    present_test_presenter.cpp)
target_link_libraries(dvv_gpu_present_tests PRIVATE dvv::gpu dvv::platform_testing
                                                    doctest::doctest)
target_compile_definitions(dvv_gpu_present_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
dvv_set_compiler_options(dvv_gpu_present_tests)
add_test(NAME dvv_gpu.present COMMAND dvv_gpu_present_tests)
set_tests_properties(dvv_gpu.present PROPERTIES
    LABELS "gpu;display"
    SKIP_RETURN_CODE 77
    TIMEOUT 300
    ENVIRONMENT "${gpu_test_environment}"
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
```

- Compile-fail: update the comment ("cases 1-8 ... or a presenter constructed without a key"). Append the pattern `"C2661: 'dvv::gpu::Presenter::Presenter'"` to the MSVC list and `"no matching constructor for initialization of 'dvv::gpu::Presenter'"` to the Clang list. Change `foreach(case IN ITEMS 1 2 3 4 5 6 7)` to `... 1 2 3 4 5 6 7 8`.

`tests/compile_fail/direct_construction.cpp`: add `#include "dvv/gpu/presenter.h"`, the header line `//   8  creating a Presenter without its context`, and:

```cpp
#elif DVV_COMPILE_FAIL_CASE == 8
    // Every argument but the key.
    auto presenter = std::make_unique<dvv::gpu::Presenter>(
        std::shared_ptr<dvv::gpu::ExecutionContext>{}, nullptr);
```

**Test fixture** (`tests/present_fixture.h`, namespace `dvv::gpu::test`; describe-level, the implementer writes the bodies):
- `struct ProviderControl { std::atomic<std::uint64_t> calls{0}; std::atomic<bool> failNext{false}; std::atomic<bool> withoutTransferSrc{false}; };` These are shared by a test (test thread) and its provider (presenter thread). `failNext` is used by Task 10.
- `class ErrorLog`: `void add(const Error&)`, `std::vector<Error> errors() const`, `std::size_t size() const`. All under its own mutex.
- `FrameProvider testFrameProvider(std::shared_ptr<ExecutionContext> context, std::shared_ptr<ProviderControl> control)`. On each call it increments `calls`. When `failNext.exchange(false)` is true, it returns `makeError(Errc::Io, "injected provider failure")`. When the extent changed, or on the first call, it creates two resources:
  - a buffer: `size = w*h*4`, usage `TRANSFER_SRC | TRANSFER_DST`, `MemoryUsage::DeviceOnly`, name `"present test pixels"`;
  - an image: `R8G8B8A8_UNORM` at the extent, usage `TRANSFER_SRC | TRANSFER_DST` (only `TRANSFER_DST` when `withoutTransferSrc`), name `"present test image"`.
  It returns `PresentFrame{.record = [buffer, image, value](CommandRecorder& r) { r.fillBuffer(*buffer, value); r.copyBufferToImage(*buffer, *image); }, .image = image}`. Here `value = 0xFF000000U | (frameNumber * 0x00102030U & 0x00FFFFFFU)`. A resource creation error is returned as is.
- `bool pumpUntil(platform::testing::TestWindow& window, const std::function<bool()>& done, std::chrono::milliseconds limit = std::chrono::seconds(5))`. It loops `window.pump(); if (done()) return true; sleep 5 ms;` until `limit`, then returns `done()`.
- `class PresentTest` (base of `TEST_CASE_FIXTURE`). Its constructor makes the following, each `REQUIRE`d:
  - `GpuRegistry::create({.windowSystem = platform::defaultWindowSystem()})` (validation from the environment);
  - `createContext(*bestDevice(), {.presentation = true})`;
  - `TestWindow::create({320, 240}, "dvv present test")`;
  - a `ProviderControl` and an `ErrorLog`.
  Members: `registry()`, `context()`, `contextPointer()`, `window()`, `control()`, `errors()`. `PresenterOptions options()` returns `{.window = window().native(), .extent = window().clientSize(), .visible = true, .frames = testFrameProvider(contextPointer(), controlPointer), .onError = [log](const Error& e) noexcept { log->add(e); }, .name = "present test"}`. Hold the log through a `shared_ptr` and wrap `add` in try/catch inside the handler. `std::unique_ptr<Presenter> makePresenter()` calls `createPresenter(options())` with `REQUIRE`. The destructor releases the context first, then `CHECK(registry().validationMessageCount() == 0)`. Presenters are locals of the test body, so they are destroyed before the fixture.
- `tests/present_main.cpp`: as `gpu_main.cpp`, with a probe extended for presentation. A missing GPU is any of these:
  - `GpuRegistry::create({.validation = Off, .windowSystem = defaultWindowSystem()})` fails with `Unsupported`;
  - there is no `bestDevice()`;
  - the best device lacks `optional.swapchain` ("the best Vulkan device cannot present (no VK_KHR_swapchain)").
  No display is `TestWindow::create({64, 64})` failing with `Unsupported` ("no display"). Either one prints `GPU presentation tests skipped: <reason>` and exits 77, or, with `DVV_REQUIRE_GPU=1`, prints `... failed: DVV_REQUIRE_GPU=1, but <reason>` and exits 1. Any other probe error exits 1. Then doctest runs, followed by `finishWithLeakCheck`.

**Tests first**

`tests/test_presenter_core.cpp` (unit, in `dvv_gpu_tests`, includes `"presenter_core.h"`):
- `"chooseSurfaceFormat prefers an 8-bit UNORM format with sRGB non-linear colour"`: `{B8G8R8A8_SRGB/SRGB_NONLINEAR, B8G8R8A8_UNORM/SRGB_NONLINEAR}` → `B8G8R8A8_UNORM`; `{R8G8B8A8_UNORM/SRGB_NONLINEAR}` → `R8G8B8A8_UNORM`; `{A2B10G10R10_UNORM_PACK32/SRGB_NONLINEAR, B8G8R8A8_UNORM/EXTENDED_SRGB_LINEAR_EXT}` → the first entry (`A2B10G10R10_UNORM_PACK32`); `{}` → `nullopt`.
- `"swapchainImageCount asks for one more than the minimum, at least two, within the maximum"`: min 2/max 0 → 3; min 1/max 0 → 2; min 2/max 2 → 2; min 3/max 8 → 4.
- `"swapchainExtent takes the surface's extent and clamps the hint only when the surface has none"`: current 800×600, hint 1×1 → 800×600; current `0xFFFFFFFF`, min 1×1, max 4096×4096, hint 5000×300 → 4096×300; current 0×0 → 0×0.
- `"compositeAlpha prefers opaque, then inherit"`: OPAQUE|INHERIT → OPAQUE; INHERIT|PRE_MULTIPLIED → INHERIT; POST_MULTIPLIED only → POST_MULTIPLIED.

`tests/present_test_presenter.cpp` (cases of design §8.2; `TEST_CASE_FIXTURE(PresentTest, ...)` unless stated):
1. `"a presenter presents frames at the window's size"`: `p = makePresenter()`. `REQUIRE(pumpUntil(window(), [&] { p->requestFrame(); return p->stats().framesPresented >= 3; }))`. Then `CHECK(p->stats().presentedExtent == window().clientSize())`, `CHECK(p->stats().swapchainsCreated >= 1)`, `CHECK_FALSE(p->failure())`, `CHECK(errors().size() == 0)`, `CHECK(control().calls >= 3)`.
6. `"an image without TRANSFER_SRC is reported, skipped and never blitted"`: `control().withoutTransferSrc = true`; `p = makePresenter()`. `REQUIRE(pumpUntil(window(), [&] { p->requestFrame(); return errors().size() >= 1; }))`. Then the first error has `code == Errc::Internal` and a message containing `"TRANSFER_SRC"`; `p->stats().frameErrors >= 1`; `p->stats().framesPresented == 0`; `CHECK_FALSE(p->failure())`. The fixture checks validation 0.
7. `"destroying a presenter right after a request neither hangs nor leaks"`: 20 times: `p = makePresenter()`; `REQUIRE(pumpUntil(... framesPresented >= 1 ...))`; `p->requestFrame()`; measure `p.reset()` with `steady_clock`. Then `CHECK(elapsed < 6 s)`. After the loop: `CHECK(errors().size() == 0)`. On Windows (NVIDIA) this is the R15 detector: a non-pumping join would hang here.
9. `"wrong configurations are refused with Internal"`:
   - `registry().createContext(device, {})`, then `->createPresenter(options())` → `Errc::Internal`.
   - `options()` with `.window = std::monostate{}` → `Internal`.
   - With the other platform's window (`#if defined(_WIN32) XlibWindow{} #else Win32Window{} #endif`) → `Internal`.
   - With `.frames = {}` → `Internal`.
   - A headless registry `GpuRegistry::create({})`, then `createContext(best, {.presentation = true})` → `Internal`.
   Each one: `REQUIRE_FALSE(result)` and `CHECK(result.error().code == Errc::Internal)`.
10. `"a presenter's objects are gone after it is destroyed"` (plain `TEST_CASE`, no fixture): `before = LifetimeRegistry::instance().liveCount()`. Create the registry, context, window and presenter as the fixture does, then pump until 1 frame. `CHECK` that `typeStats()` shows exactly 1 live type whose name ends in `Presenter`. Reset the presenter, then `CHECK` 0 live presenters. Reset the context; check that the registry's `validationMessageCount() == 0`; reset the registry. `CHECK(LifetimeRegistry::instance().liveCount() == before)`.

Compile-fail case 8 must fail with the patterns above; case 0 still builds with `presenter.h` included.

**First failure:** after Step 2 the build of `dvv_gpu_tests` fails because `presenter_core.h` does not exist (MSVC C1083, Clang `'presenter_core.h' file not found`). After the helpers exist, `dvv_gpu.unit` passes. The present tests are the next red-to-green step.

**Steps**

Commands run from the workspace root with the PowerShell tool. Linux runs through `wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && …'`. Run git only in PowerShell, except the read-only `git ls-files` inside the WSL lint commands.

- [ ] **Step 1: Check what Tasks 6-8 provide.** Run `Select-String -Path dvv-gpu/src/presenter_logic.h -Pattern 'enum class|void |bool |Action'` and `Select-String -Path dvv-gpu/src/access.h -Pattern 'presentBlit'`. Compare them with the Consumes list and the six contract rules. Read Task 8's logic tests for rules 3-6. Record any difference to adapt in Step 5.
- [ ] **Step 2: Write the tests.** Write `tests/test_presenter_core.cpp`, the present fixture/main/case files, compile-fail case 8 and the `tests/CMakeLists.txt` changes listed above.
- [ ] **Step 3: See it fail.** `Push-Location dvv-gpu; cmake --preset vs2026; cmake --build --preset vs2026 --target dvv_gpu_tests 2>&1 | Select-String 'error' | Select-Object -First 3; Pop-Location`. Expected: `C1083: Cannot open include file: 'presenter_core.h'`.
- [ ] **Step 4: Header and helpers.** Write `presenter.h`, the `execution_context.h` change, `presenter_core.h`, and the four helpers in `presenter_core.cpp` (with a temporary empty class body if needed to compile). Run `Push-Location dvv-gpu; cmake --build --preset vs2026 --target dvv_gpu_tests; ctest --preset vs2026 -R 'dvv_gpu\.unit' --output-on-failure; Pop-Location`. Expected: `100% tests passed`.
- [ ] **Step 5: The core, presenter and Windows wait.** Write the rest of `presenter_core.cpp`, `presenter.cpp`, `thread_wait.h`, `thread_wait_win32.cpp` and the `CMakeLists.txt` change. Run `Push-Location dvv-gpu; cmake --build --preset vs2026; Pop-Location`. Expected: no warning, no error. Then run `Select-String -Path dvv-gpu/src/presenter_core.cpp -Pattern 'retryElapsed'`. Expected: it finds one call, `logic_.retryElapsed();` in `waitForWork` (the comment in `step()` also matches).
- [ ] **Step 6: Windows (NVIDIA).** Run `$env:DVV_REQUIRE_GPU='1'; $env:DVV_VK_VALIDATION='sync'; $env:DVV_VK_VALIDATION_REQUIRED='1'; Push-Location dvv-gpu; ctest --preset vs2026 -R 'dvv_gpu\.(unit|present|compile_fail\.8)' --output-on-failure; Pop-Location`. Expected: `100% tests passed, 0 tests failed out of 3`; no `Skipped`; no `Vulkan validation` line in the output. If sync validation reports a hazard for the semaphore-less present, record the full message (VUID and text), stop, and report to the controller. R6 needs a submit-path change, which the controller and arbiter decide. Do not change `submit`.
- [ ] **Step 7: R15 detector.** Run `$env:DVV_REQUIRE_GPU='1'; $env:DVV_VK_VALIDATION='sync'; Push-Location dvv-gpu; ctest --preset vs2026 -R 'dvv_gpu\.present' --repeat until-fail:5 --output-on-failure; Pop-Location`. Expected: 5 passes; no run longer than about 60 s.
- [ ] **Step 8: WSL on the WSLg display, and the Xvfb variant when it exists.**
  ```powershell
  wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd dvv-gpu && cmake --preset ninja-clang && cmake --build --preset ninja-clang && export DVV_REQUIRE_GPU=1 DVV_VK_VALIDATION=sync DVV_VK_VALIDATION_REQUIRED=1 && ctest --preset ninja-clang -R "dvv_gpu\.(unit|present|compile_fail\.8)" --output-on-failure && if command -v xvfb-run >/dev/null; then xvfb-run -a -s "-screen 0 1920x1080x24" ctest --preset ninja-clang -R "dvv_gpu\.present" --output-on-failure; else echo "xvfb-run not installed: Xvfb variant skipped locally (CI runs it)"; fi'
  ```
  Expected: all passed and not skipped (lavapipe on WSLg). Then either the Xvfb run passes or the skip line appears.
- [ ] **Step 9: Lint** (WSL, after the ninja-clang build):
  ```powershell
  wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd dvv-gpu && git ls-files -z "*.h" "*.cpp" | xargs -0 -r -P "$(nproc)" -n 8 clang-format --dry-run --Werror && git ls-files -z ":(glob)src/**/*.cpp" ":(glob)tests/*.cpp" ":(glob)tools/*.cpp" ":(exclude,glob)**/*_win32.cpp" ":(exclude,glob)**/*_msw.cpp" | xargs -0 -r -P "$(nproc)" -n 1 clang-tidy -p ../_build/dvv-gpu/ninja-clang --quiet && echo LINT-OK'
  ```
  Expected: `LINT-OK` and no diagnostics. `thread_wait_win32.cpp` is not in the Linux compile database, which is why it is excluded.
- [ ] **Step 10: Commit** (then the full matrix on this commit):
  ```powershell
  git -C dvv-gpu add -A
  git -C dvv-gpu commit -m "feat: add the Presenter with its thread, swapchain, acquire/present and stop" -m "Implements design M1b §5: fence acquire with host wait, semaphore-less present, lazy swapchain recreation after queue idle, and a stop that waits for the frame in flight. On Windows the stop dispatches cross-thread sent messages while it waits. Adds dvv_gpu_present_tests (cases 1, 6, 7, 9, 10) and compile-fail case 8." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```
- [ ] **Step 11: Full matrix on the final commit.** Run these two in parallel with `run_in_background: true`:
  - Windows: `$env:DVV_REQUIRE_GPU='1'; $env:DVV_VK_VALIDATION='sync'; Push-Location dvv-gpu; cmake --workflow --preset vs2026 2>&1 | Select-String 'tests passed|tests failed|Skipped|error'; cmake --workflow --preset vs2026-asan 2>&1 | Select-String 'tests passed|tests failed|Skipped|error'; Pop-Location`
  - WSL: `wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd dvv-gpu && export DVV_REQUIRE_GPU=1 DVV_VK_VALIDATION=sync DVV_VK_VALIDATION_REQUIRED=1 && for p in ninja-clang ninja-clang-asan ninja-clang-tsan; do echo "== $p"; cmake --workflow --preset $p 2>&1 | grep -E "tests passed|tests failed|Skipped|error|WARNING: ThreadSanitizer|ERROR: AddressSanitizer|LeakSanitizer"; done'`
  Then run the privacy lint: `cmake -DREPO=dvv-gpu -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake`.
  A sanitizer report whose every relevant frame is inside libX11, libxcb or Mesa's WSI gets the R9 library-scoped suppression: `race:libX11.so` / `race:libxcb.so` in `tests/tsan.supp`, and `tests/lsan.supp` with `leak:libX11.so`, passed as `LSAN_OPTIONS=suppressions=…` only to `dvv_gpu.present` when `DVV_SANITIZE` contains `address`. Comment it and commit it as `test: suppress …` with the trailer. A report with a frame in dvv code is a defect to fix in this task.

**Expected results:**
- Two more CTest entries than before: `dvv_gpu.present` and `dvv_gpu.compile_fail.8`. vs2026 Debug and Release: all passed. vs2026-asan: all passed (the package test is not registered under sanitizers). ninja-clang*: all passed.
- `dvv_gpu.present` runs (not skipped) on NVIDIA and on lavapipe (WSLg; Xvfb when installed) and reports 0 validation messages with `sync`.
- Windows CI will skip it with 77 (no GPU).
- LINT-OK; privacy lint clean.

**Commit:** `feat: add the Presenter with its thread, swapchain, acquire/present and stop` (Step 10), and, only if needed, `test: suppress …` (Step 11). Each ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

**Notes:**
- Design references: D4-D10, §4.5, §5.1-§5.5, §6, §7, §8.2, R6, R15.
- **Note (plan choice):** the Windows wait lives in its own `*_win32.cpp` translation unit. `<windows.h>` macros (`MemoryBarrier`, `CreateSemaphore`) must not meet `vulkan.hpp`, and the `*_win32.cpp` name keeps it out of the Linux clang-tidy run.
- **Note (plan choice):** WSI calls go through the C function pointers of `DeviceCore::dispatch()` (`api.vkAcquireNextImageKHR`, …). This makes every `VkResult` explicit, with no Vulkan-Hpp result-type conversion on these success-code-rich calls.
- The core reads `snapshot_`, `in_flight_` and the logic inside `waitForWork` under the mutex. All three are presenter-thread state, so this adds no sharing.
- Risks: R6 (sync validation and the semaphore-less present; fallback per R6 after escalation), R15 (covered by Step 7).

---

### Task 10: Presenter: resize, minimise, errors and sanitizers

**Goal:** Add design §8.2 cases 2, 3, 4, 5 and 8, fix what they find in the Task 9 core (recreation, hidden handling, out-of-date/suboptimal, provider errors), and pass the Windows AddressSanitizer preset (`vs2026-asan`) with the display tests running on NVIDIA (in the task's checkpoint checks, U-PROC). Measure R7 (time under the queue lock in `vkQueuePresentKHR`) on NVIDIA. Windows only (U-WIN): the Linux runs (lavapipe, Xvfb, ASan, TSan) and the R9 Linux suppressions are v1.x.

**Files** (dvv-gpu):
- Create `tests/present_test_events.cpp`: cases 2, 3, 4, 5, 8.
- Modify `tests/CMakeLists.txt`: add `present_test_events.cpp` to `dvv_gpu_present_tests`.
- Modify `src/presenter_core.cpp` (and only if a case shows a logic defect, `src/presenter_logic.cpp` with a new logic unit case that reproduces it).

**Interfaces:**
- Consumes (Task 9): `Presenter`, `PresentTest`, `testFrameProvider` with `ProviderControl::failNext`, `ErrorLog`, `pumpUntil`, `TestWindow::{resize, minimise, restore, pump, clientSize}`.
- Produces: no new API. The presenter is verified on Windows for resize, minimise/restore, `setVisible`, provider errors and two presenters per context. Tasks 11 and 12a rely on that.

**Behaviour** (what the cases pin down; design §5.2-§5.3, §5.7):
- A resize hint that changes after the last creation triggers exactly one recreation at the latest hint. `currentExtent` stays authoritative (D8).
- A minimised Win32 window (`currentExtent` 0×0) makes the presenter `hidden` without failing. A request while hidden does nothing until the next GUI call.
- `setVisible(false)` stops frames despite requests (one in-flight frame tolerated); `setVisible(true)` resumes.
- A provider error reaches `onError` once, increments `frameErrors` and does not fail the presenter. The next good frame presents.
- Two presenters on one context present concurrently without data races. Windows has no TSan (U-WIN risk): races are found by review and `vs2026-asan`.

**Tests first** (`tests/present_test_events.cpp`, `TEST_CASE_FIXTURE(PresentTest, …)`; every case ends with `CHECK_FALSE(p->failure())` and `CHECK(errors().size() == <n>)`; the fixture checks validation 0):
2. `"each resize presents at the new extent"`: `p = makePresenter()`, then pump until ≥ 1 frame. For each `size` in `{640×480, 97×61, 640×480}`:
   - `created = p->stats().swapchainsCreated`; `window().resize(size)`; `p->resize(window().clientSize())`;
   - `REQUIRE(pumpUntil(window(), [&] { p->requestFrame(); auto s = p->stats(); return s.presentedExtent == window().clientSize() && s.swapchainsCreated > created; }))`;
   - `CHECK(window().clientSize() == size)`.
   `errors().size() == 0`.
3. `"minimise and restore"`: pump until ≥ 1 frame.
   - Win32 only, the 0-extent path (D8): `window().minimise(); p->resize({321, 241});`. The hint is non-zero and differs from the last creation's hint, which forces a recreation; the hint value is irrelevant because `currentExtent` decides. Then `REQUIRE(pumpUntil(window(), [&] { p->requestFrame(); return p->stats().hidden; }))` and `CHECK_FALSE(p->stats().failed)`.
   - Both platforms, as the GUI does: `p->setVisible(false)`. Xlib only: `window().minimise()` (unmap). The Xlib branch stays in the code (portable, compiled by the lint build) but is not run in v1.0 (U-WIN). Pump 100 ms.
   - Then `window().restore(); p->resize(window().clientSize()); p->setVisible(true); n = p->stats().framesPresented;` and `REQUIRE(pumpUntil(window(), [&] { p->requestFrame(); auto s = p->stats(); return s.framesPresented > n && !s.hidden; }))`.
   `errors().size() == 0`.
4. `"setVisible(false) stops frames until setVisible(true)"`: pump until ≥ 1 frame. `p->setVisible(false); n = p->stats().framesPresented;` Pump for 300 ms, calling `p->requestFrame()` on every iteration (`pumpUntil` with a predicate that only requests and returns false; ignore its result). Then `CHECK(p->stats().framesPresented <= n + 1)` and `CHECK(p->stats().hidden)`. Then `p->setVisible(true)` and `REQUIRE(pumpUntil(... framesPresented > n + 1 ...))`. `errors().size() == 0`.
5. `"a provider error is reported once and the next frame presents"`: pump until ≥ 1 frame. `control().failNext = true; p->requestFrame();` `REQUIRE(pumpUntil(window(), [&] { return errors().size() >= 1; }))`. Then `CHECK(errors().size() == 1)`, `CHECK(errors().errors()[0].code == Errc::Io)`, the message contains `"injected provider failure"`, `CHECK(p->stats().frameErrors == 1)`. Then `n = framesPresented` and `REQUIRE(pumpUntil(... requestFrame ...; framesPresented > n))`. `errors().size() == 1`.
8. `"two presenters on one context present concurrently"`: a second `TestWindow::create({200, 150}, "dvv present test 2")`, a second `ProviderControl` and `ErrorLog`, and `q = context().createPresenter({... second window ...})`. For 1 s: `p->requestFrame(); q->requestFrame(); window().pump(); second.pump(); sleep 5 ms`. Then `REQUIRE` with a 5 s pump loop over both windows that each has `framesPresented >= 1`. `CHECK` neither failed, both error logs empty. Destroy `q` before the second window. On Windows it runs under `vs2026-asan`; the TSan run is v1.x (U-WIN).

**First failure:** none is guaranteed, because these cases test Task 9's code. Run them first. A failing case names the defect: fix it in the core, or in the logic with a new logic unit case. If all pass at once, say so in the commit message.

**Checkpoint task (U-PROC):** Task 10 is a checkpoint. It covers Task 10 only (Tasks 0-9 ran the former full matrix), in `dvv-gpu`; `<cp-base>` is the `dvv-gpu` head before Task 10's first commit (Task 9's last commit).

**Steps** (from the workspace root; WSL only for clang-format in Step 5 and the checkpoint build in Step 6; git only in PowerShell, except the read-only `git ls-files` and `git diff --name-only` inside the WSL commands). Record `$base = git -C dvv-gpu rev-parse HEAD` before Step 1; it is both `<base>` and `<cp-base>`:
- [ ] **Step 1: Write `present_test_events.cpp` and add it to CMake.**
- [ ] **Step 2: Run on Windows (NVIDIA):** `$env:DVV_REQUIRE_GPU='1'; $env:DVV_VK_VALIDATION='sync'; $env:DVV_VK_VALIDATION_REQUIRED='1'; Push-Location dvv-gpu; cmake --build --preset vs2026; ctest --preset vs2026 -R 'dvv_gpu\.present' --output-on-failure; Pop-Location`. Fix and repeat until green. VUID-VkSwapchainCreateInfoKHR-imageExtent-01274 may appear only in an interactive drag-resize (R6/§8.4). It must not appear in these tests, where `TestWindow::resize` is synchronous.
- [ ] **Step 3: R7 measurement (temporary, not committed).** First commit any `src/` fix from Step 2 as `fix: …` (trailer), then check that `git -C dvv-gpu status --short src` is empty. Only then add the instrumentation. Locally wrap the `vkQueuePresentKHR` call in `present()` with `std::chrono::steady_clock` and keep the maximum. Log it in `teardown()` at Warn: `presenter '<name>': longest present <µs> µs`. Run cases 2 and 8 on NVIDIA, then `git -C dvv-gpu checkout -- src/presenter_core.cpp` to drop it. If the maximum exceeds about 1 ms in steady state (ignore the first frame after a recreation), apply R7's first fallback: `swapchainImageCount` returns `max(minImageCount + 2, 3)`, capped, and its unit test changes accordingly. Record the number in the commit message body.
- [ ] **Step 4: Commit:**
  ```powershell
  git -C dvv-gpu add -A
  git -C dvv-gpu commit -m "test: cover presenter resize, minimise, visibility, provider errors and two presenters" -m "<what the cases found and fixed; R7: longest present under the queue lock on NVIDIA <x> µs>" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```
  The R7-fallback change (if any) is part of this commit.
- [ ] **Step 5: Per-task checks on the final commit** (Global Constraints), in `dvv-gpu`:
  - Windows Debug, all tests (`run_in_background: true`): `$env:DVV_REQUIRE_GPU='1'; $env:DVV_VK_VALIDATION='sync'; $env:DVV_VK_VALIDATION_REQUIRED='1'; Push-Location dvv-gpu; cmake --preset vs2026 | Out-Null; cmake --build --preset vs2026; ctest --preset vs2026 --output-on-failure 2>&1 | Select-String 'tests passed|tests failed|Skipped|error'; Pop-Location`.
  - clang-format on the changed files: `wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd dvv-gpu && git diff --name-only -z --diff-filter=d <base> -- "*.h" "*.cpp" | xargs -0 -r clang-format --dry-run --Werror && echo FORMAT-OK'`.
  - Privacy lint: `cmake -DREPO=dvv-gpu -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake`.
- [ ] **Step 6: Checkpoint checks** (Global Constraints), in `dvv-gpu`. The Windows call and the WSL build run in parallel, each with `run_in_background: true`:
  - Windows Release, then ASan: `$env:DVV_REQUIRE_GPU='1'; $env:DVV_VK_VALIDATION='sync'; $env:DVV_VK_VALIDATION_REQUIRED='1'; Push-Location dvv-gpu; cmake --build --preset vs2026-release; ctest --preset vs2026-release --output-on-failure 2>&1 | Select-String 'tests passed|tests failed|Skipped|error'; cmake --workflow --preset vs2026-asan 2>&1 | Select-String 'tests passed|tests failed|Skipped|AddressSanitizer'; Pop-Location`. Expected: all passed, `dvv_gpu.present` not skipped (case 8 runs under ASan), no AddressSanitizer report. A report with a dvv frame is fixed in code. A report entirely inside the NVIDIA driver or the Vulkan loader is recorded in the plan's "Changes during execution" and reported, not suppressed. Task 10 has no ×10 repeats.
  - clang-tidy after the `ninja-clang` configure + build (no tests; also the Linux sanity build):
    ```powershell
    wsl -d Ubuntu-24.04 -e bash -lc 'set -o pipefail && . _build/tools/linux/env.sh && cd dvv-gpu && cmake --preset ninja-clang >/dev/null && cmake --build --preset ninja-clang 2>&1 | tail -n 30 && git diff --name-only -z --diff-filter=d <cp-base> HEAD -- ":(glob)src/**/*.cpp" ":(glob)tests/*.cpp" ":(glob)tools/*.cpp" ":(exclude,glob)**/*_win32.cpp" ":(exclude,glob)**/*_msw.cpp" | xargs -0 -r -P "$(nproc)" -n 1 clang-tidy -p ../_build/dvv-gpu/ninja-clang --quiet && echo TIDY-OK'
    ```
    Expected `TIDY-OK`. A build failure that is our portability bug (for example the Xlib branch of case 3) is fixed; a toolchain or third-party failure is logged and reported.
  - A failure is fixed with a `fix:` commit, then the Per-task checks and the failed check run again, before Task 11 starts.

**Expected results:** `dvv_gpu.present` has 10 cases (1-10), all passed, not skipped on NVIDIA, with 0 validation messages under `sync`. Per-task checks: vs2026 Debug passes, FORMAT-OK, privacy lint clean. Checkpoint: vs2026 Release and vs2026-asan pass; no AddressSanitizer report; TIDY-OK (the `ninja-clang` build compiles the Xlib branch of case 3). The CTest count is unchanged from Task 9.

**Commit:** `test: cover presenter resize, minimise, visibility, provider errors and two presenters` (plus `fix: …` if needed), each with the trailer.

**Notes:**
- Design references: §5.2-§5.3, §5.7 cases 1-6, §8.2 cases 2-5 and 8, D8, D9, R6, R7.
- **Note (plan choice):** the R7 instrumentation is not committed. Only the number is kept, in the commit message and later in the ledger (Task 13b).
- **U-WIN:** the WSLg/Xvfb runs on lavapipe, `ninja-clang-asan`/`ninja-clang-tsan`, the R9 library-scoped suppressions (`tests/tsan.supp`, `tests/lsan.supp`) and the R5 lavapipe/Xvfb fallback are v1.x (future.md).

---

### Task 11: rawreel skeleton and test-pattern renderer

**Goal:** Create the local `rawreel` repository with the standard skeleton and `rawreel_render`: the CPU reference `testPatternPixel`, the `test_pattern.slang` compute shader, `TestPatternRenderer`, and the argument parser. Prove the GPU output equals the CPU reference exactly, and wire `rawreel` into the workspace.

**Files:**
- New repository `rawreel` (sibling of `rawreel-workspace`, `git init -b m1b`, no remote):
  - `CMakePresets.json`, `.clang-format`, `.clang-tidy`, `.gitattributes`, `.gitignore`, `LICENSE`: byte-identical copies of `dvv-gpu`'s.
  - `README.md`: short; Task 13b completes it.
  - `CMakeLists.txt`: the project, dependencies and `rawreel_render`.
  - `src/arguments.h`, `src/arguments.cpp`: `Arguments`, `parseArguments`, `skipExitCode`, exit codes, `USAGE`.
  - `src/test_pattern.h`, `src/test_pattern.cpp`: `testPatternPixel`.
  - `src/test_pattern_renderer.h`, `src/test_pattern_renderer.cpp`: `TestPatternRenderer`.
  - `shaders/test_pattern.slang`.
  - `tests/CMakeLists.txt`, `tests/main.cpp`, `tests/test_arguments.cpp`, `tests/test_test_pattern.cpp`, `tests/gpu_main.cpp`, `tests/gpu_test_renderer.cpp`.
  - `tests/tsan.supp`: a byte-identical copy of `dvv-gpu/tests/tsan.supp`. Kept so the build files stay portable; the Linux TSan run that uses it is v1.x (U-WIN).
- `rawreel-workspace`: `repos.txt`, `cmake/dvv-versions.cmake`, `CMakeLists.txt`, `cmake/DvvPresets.json` (the rawreel wiring).

**Interfaces:**
- Consumes:
  - Task 9: `dvv::gpu::{ExecutionContext, PresentFrame, PresentFrameInfo, GpuImage, GpuBuffer, CommandRecorder, ComputePipeline}`, `dvv_add_shaders`.
  - Task 5: the `dvv_platform` FetchContent name and `FETCHCONTENT_SOURCE_DIR_DVV_PLATFORM`.
  - dvv-base: `getEnvironment`, `Result`, `makeError`, `Task`, `syncWait`, `ExecutorFactory`.
- Produces, used by Task 12a:

```cpp
// src/arguments.h
namespace rawreel {
namespace exit_code {
inline constexpr int SUCCESS = 0;
inline constexpr int BAD_ARGUMENTS = 1;    // also: the GUI could not start, an unexpected exception
inline constexpr int GPU_ERROR = 4;        // no loader, no usable device, presenter failure
inline constexpr int SELFTEST_FAILED = 6;  // a --selftest check failed (spec §10.2)
inline constexpr int SKIPPED = 77;         // --selftest only: no usable device or no display
}  // namespace exit_code

struct Arguments {
    bool selftest = false;
    bool help = false;
};
// "--selftest", "--help" (each at most once is not checked; repeats are harmless); anything else
// is Errc::Format "unknown argument '<arg>'".
[[nodiscard]] dvv::Result<Arguments> parseArguments(std::span<const std::string_view> args);
// 77, or 4 when DVV_REQUIRE_GPU=1 turns a skip into a failure (design D12).
[[nodiscard]] int skipExitCode();
// The --help text, ending with a newline.
inline constexpr char USAGE[] = R"(Usage: rawreel-gui [--selftest] [--help]

  --selftest  open the window, present, resize, minimise and restore it, close it, and exit
  --help      print this text

Exit codes: 0 success; 1 bad arguments, or the GUI could not start; 4 GPU error (no Vulkan
loader, no usable device, presenter failure); 6 a --selftest check failed; 77 (--selftest only)
no usable device or no display, unless DVV_REQUIRE_GPU=1 (then 4).
)";
}  // namespace rawreel

// src/test_pattern.h
namespace rawreel {
// Test pattern (design §4.6): 75 % colour bars (191) in the upper two thirds, a horizontal grey ramp
// in the lower third, a 1-pixel white border. Integer math, sRGB-encoded RGBA8 code values, R in
// the low byte, alpha 255. Exactly what shaders/test_pattern.slang computes.
[[nodiscard]] std::uint32_t testPatternPixel(std::uint32_t x, std::uint32_t y, std::uint32_t width,
                                             std::uint32_t height) noexcept;
}  // namespace rawreel

// src/test_pattern_renderer.h
namespace rawreel {
class TestPatternRenderer {  // not thread-safe; used by one presenter's provider
public:
    // The compute pipeline for test_pattern.slang (16 bytes of push constants).
    [[nodiscard]] static dvv::Result<std::unique_ptr<TestPatternRenderer>> create(
        std::shared_ptr<dvv::gpu::ExecutionContext> context);
    // Re-renders when the extent changed or !info.previousFrameCompleted; otherwise returns the
    // same image and no commands.
    [[nodiscard]] dvv::Result<dvv::gpu::PresentFrame> frame(const dvv::gpu::PresentFrameInfo& info);
};
}  // namespace rawreel
```

- CMake: target `rawreel_render` (STATIC; PUBLIC `dvv::gpu`, `dvv::base`; PUBLIC include `src`; shaders namespace `rawreel_shaders`; header `rawreel_shaders/test_pattern.h` with `rawreel_shaders::testPattern()`). CTest `rawreel.unit` and `rawreel.gpu` (label `gpu`, `SKIP_RETURN_CODE 77`). The variable `gpu_test_environment` in `tests/CMakeLists.txt` is reused by Task 12a.

**Behaviour:**
- `testPatternPixel(x, y, w, h)`, with `rgba(r, g, b) = r | g << 8 | b << 16 | 0xFF << 24`:
  1. A border pixel (`x == 0 || y == 0 || x == w - 1 || y == h - 1`) is `rgba(255, 255, 255)`.
  2. Else, when `y * 3 < h * 2`, it is bar `x * 7 / w` of white, yellow, cyan, green, magenta, red, blue = `(191,191,191) (191,191,0) (0,191,191) (0,191,0) (191,0,191) (191,0,0) (0,0,191)`.
  3. Else it is grey `v = x * 255 / max(w - 1, 1)`.
  All arithmetic is `uint32_t`. Callers pass `x < w`, `y < h`.
- `TestPatternRenderer::frame`:
  - When there is no image yet, or `info.extent != extent_`, it (re)creates the buffer (`w*h*4` bytes, `STORAGE_BUFFER | TRANSFER_SRC`, `DeviceOnly`, name `"test pattern pixels"`) and the image (`R8G8B8A8_UNORM`, `TRANSFER_SRC | TRANSFER_DST`, name `"test pattern"`).
  - When it re-renders (new resources or `!info.previousFrameCompleted`), it returns `{.record = [pipeline, buffer, image, w, h](CommandRecorder& r) { r.dispatch(*pipeline, std::as_bytes(std::span(&push, 1)), (w + 7) / 8, (h + 7) / 8); r.copyBufferToImage(*buffer, *image); }, .image = image_}`, with push `{buffer->deviceAddress(), w, h}`.
  - Otherwise it returns `{.record = {}, .image = image_}`.
  - Errors from resource creation pass through.
  - The push-constant struct is `struct TestPatternConstants { std::uint64_t pixels; std::uint32_t width; std::uint32_t height; };` with `static_assert(sizeof == 16)` and offsets 0/8/12.
- `parseArguments`: an empty list gives defaults. `--selftest` and `--help` set their flags. Anything else returns `Errc::Format` with the message `unknown argument '<arg>'`.

**Code** (`shaders/test_pattern.slang`, complete, because CPU and GPU must agree bit for bit):

```slang
// rawreel's test pattern (design M1b §4.6): one thread per pixel writes sRGB-encoded RGBA8 code
// values (R in the low byte) to a buffer passed by device address. It must match
// rawreel::testPatternPixel() exactly.
struct PushConstants
{
    uint* pixels;  // width * height values, row by row (buffer device address)
    uint width;
    uint height;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

static const uint BAR = 191;  // 75 % of 255, rounded down

uint rgba(uint r, uint g, uint b)
{
    return r | (g << 8) | (b << 16) | (255u << 24);
}

uint patternPixel(uint x, uint y, uint width, uint height)
{
    if (x == 0 || y == 0 || x == width - 1 || y == height - 1)
    {
        return rgba(255, 255, 255);
    }
    if (y * 3 < height * 2)
    {
        switch (x * 7 / width)
        {
        case 0: return rgba(BAR, BAR, BAR);
        case 1: return rgba(BAR, BAR, 0);
        case 2: return rgba(0, BAR, BAR);
        case 3: return rgba(0, BAR, 0);
        case 4: return rgba(BAR, 0, BAR);
        case 5: return rgba(BAR, 0, 0);
        default: return rgba(0, 0, BAR);
        }
    }
    uint v = x * 255 / max(width - 1, 1u);
    return rgba(v, v, v);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    if (id.x < pc.width && id.y < pc.height)
    {
        pc.pixels[id.y * pc.width + id.x] = patternPixel(id.x, id.y, pc.width, pc.height);
    }
}
```

`rawreel/CMakeLists.txt` (complete for this task; Task 12a appends the GUI block before `if(DVV_BUILD_TESTS)`):

```cmake
cmake_minimum_required(VERSION 4.2)
project(rawreel VERSION 0.1.0 DESCRIPTION "rawreel raw video player" LANGUAGES CXX)

option(DVV_BUILD_TESTS "Build the dvv tests" ${PROJECT_IS_TOP_LEVEL})

# The dvv libraries. Inside the workspace build cmake/dvv-versions.cmake has declared them first,
# so these declarations are ignored there (spec §3.1). Stand-alone builds take them from the
# sibling folders (FETCHCONTENT_SOURCE_DIR_<NAME> in the workspace presets), from installed
# packages or from Git.
include(FetchContent)
FetchContent_Declare(dvv_base
    GIT_REPOSITORY "https://github.com/BarabashkaD/dvv-base.git"
    GIT_TAG main
    FIND_PACKAGE_ARGS 0.1)
FetchContent_Declare(dvv_platform
    GIT_REPOSITORY "https://github.com/BarabashkaD/dvv-platform.git"
    GIT_TAG main
    FIND_PACKAGE_ARGS 0.1)
FetchContent_Declare(dvv_gpu
    GIT_REPOSITORY "https://github.com/BarabashkaD/dvv-gpu.git"
    GIT_TAG main
    FIND_PACKAGE_ARGS 0.1)
# A stand-alone build tests only this repository (the normal variable hides the cache entry from
# the dependencies' option()); as a subproject the parent's value is kept, and the workspace
# build tests every repository.
if(PROJECT_IS_TOP_LEVEL)
    set(DVV_BUILD_TESTS OFF)
endif()
FetchContent_MakeAvailable(dvv_base dvv_platform dvv_gpu)
if(PROJECT_IS_TOP_LEVEL)
    unset(DVV_BUILD_TESTS)
endif()
if(NOT DVV_CMAKE_DIR)
    message(FATAL_ERROR "dvv_base did not define DVV_CMAKE_DIR")
endif()
include("${DVV_CMAKE_DIR}/DvvCompilerOptions.cmake")

# rawreel_render: everything of rawreel-gui that needs no wx (test pattern, renderer, arguments),
# so unit, GPU and sanitizer builds use it without the GUI.
add_library(rawreel_render STATIC
    src/arguments.cpp
    src/test_pattern.cpp
    src/test_pattern_renderer.cpp)
target_include_directories(rawreel_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rawreel_render PUBLIC dvv::gpu dvv::base)
dvv_add_shaders(rawreel_render NAMESPACE rawreel_shaders SHADERS shaders/test_pattern.slang)
dvv_set_compiler_options(rawreel_render)

if(DVV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

`rawreel/tests/CMakeLists.txt` (complete for this task):

```cmake
find_package(doctest 2.5 REQUIRED CONFIG)

add_executable(rawreel_tests main.cpp test_arguments.cpp test_test_pattern.cpp)
target_link_libraries(rawreel_tests PRIVATE rawreel_render doctest::doctest)
target_compile_definitions(rawreel_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
dvv_set_compiler_options(rawreel_tests)
add_test(NAME rawreel.unit COMMAND rawreel_tests)
set_tests_properties(rawreel.unit PROPERTIES
    TIMEOUT 300
    ENVIRONMENT DVV_LEAK_CHECK=fail
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")

# GPU tests (spec §12): exit 77 without a usable Vulkan 1.4 device unless DVV_REQUIRE_GPU=1. The
# loader is pointed at the validation layer of the third-party prefix, as in dvv-gpu.
find_file(DVV_VK_VALIDATION_LAYER_MANIFEST VkLayer_khronos_validation.json
    PATHS ${CMAKE_PREFIX_PATH} PATH_SUFFIXES bin share/vulkan/explicit_layer.d NO_DEFAULT_PATH)
set(gpu_test_environment DVV_LEAK_CHECK=fail)
if(DVV_VK_VALIDATION_LAYER_MANIFEST)
    cmake_path(GET DVV_VK_VALIDATION_LAYER_MANIFEST PARENT_PATH layer_dir)
    list(APPEND gpu_test_environment "VK_ADD_LAYER_PATH=${layer_dir}")
endif()
# The software driver is not instrumented; see tsan.supp.
if(DVV_SANITIZE STREQUAL "thread")
    list(APPEND gpu_test_environment
         "TSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tsan.supp")
endif()

add_executable(rawreel_gpu_tests gpu_main.cpp gpu_test_renderer.cpp)
target_link_libraries(rawreel_gpu_tests PRIVATE rawreel_render doctest::doctest)
target_compile_definitions(rawreel_gpu_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
dvv_set_compiler_options(rawreel_gpu_tests)
add_test(NAME rawreel.gpu COMMAND rawreel_gpu_tests)
set_tests_properties(rawreel.gpu PROPERTIES
    LABELS gpu
    SKIP_RETURN_CODE 77
    TIMEOUT 300
    ENVIRONMENT "${gpu_test_environment}"
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
```

Workspace wiring (rawreel-workspace):
- `repos.txt`: append `rawreel` after `dvv-gpu`. Make the header comment name the remaining later repositories (dvv-media, dvv-components).
- `cmake/dvv-versions.cmake`: append the declaration below. It has no `FIND_PACKAGE_ARGS` because rawreel is an application and not an installed package.
  ```cmake
  FetchContent_Declare(rawreel
      GIT_REPOSITORY "${DVV_GIT_BASE_URL}/rawreel.git"
      GIT_TAG main)
  ```
- `CMakeLists.txt`: `FetchContent_MakeAvailable(... dvv_gpu rawreel)` and `dvv_check_siblings(... dvv_gpu rawreel)`, keeping `dvv_platform` from Task 5 in both lists.
- `cmake/DvvPresets.json`: in `dvv-base-settings.cacheVariables` add `"FETCHCONTENT_SOURCE_DIR_RAWREEL": "${sourceDir}/../rawreel"`.

**Tests first:**
- `tests/test_test_pattern.cpp`: `"testPatternPixel"`:
  - 1×1 → `0xFFFFFFFF`;
  - 33×17 corners (0,0), (32,0), (0,16), (32,16) → `0xFFFFFFFF`;
  - 33×17 (1,1) → `0xFFBFBFBF` (bar 0); (5,1) → `0xFF00BFBF` (bar 1, yellow); (31,1) → `0xFFBF0000` (bar 6, blue);
  - 33×17 (1,15) → `0xFF070707` (ramp 255/32 = 7); (31,15) → `0xFFF7F7F7` (7905/32 = 247);
  - 640×360 (320,100) → `0xFF00BF00` (bar 3, green); (639,359) → `0xFFFFFFFF`; (638,300) → `0xFFFEFEFE` (162690/639 = 254).
- `tests/test_arguments.cpp`: `"parseArguments"`: `{}` gives both false; `{"--selftest"}` gives selftest; `{"--help"}` gives help; `{"--selftest","--help"}` gives both; `{"--bogus"}` gives `Errc::Format` with a message containing `--bogus`; `{"selftest"}` is an error. `"skipExitCode"`: save `DVV_REQUIRE_GPU`; unset gives 77; `"1"` gives 4; `"0"` gives 77; restore it.
- `tests/gpu_test_renderer.cpp` (main like dvv-gpu's `gpu_main.cpp`: exit 77 / `DVV_REQUIRE_GPU`). It uses its own `submitAndWait`: `dvv::Task<Result<uint64_t>> submitTask(ctx, executor, record) { co_return co_await ctx.submit(executor, std::move(record)); }` and `syncWait`, with a one-thread pool from `ExecutorFactory::makeThreadPool(1)`. Cases:
  - `"the shader draws testPatternPixel exactly"`: for 1×1, 33×17, 640×360, call `renderer->frame({extent, VK_FORMAT_B8G8R8A8_UNORM, 0, false})`. Submit `frame.record`, then `copyImageToBuffer(*frame.image, readback)` in one submission (readback `HostReadback`, `TRANSFER_DST`). `invalidate()`, then require every value to equal `testPatternPixel(x, y, w, h)` (count mismatches, `CHECK(mismatches == 0)`).
  - `"frame() renders only when needed"`: the first call has a non-empty record. Submit it. The same extent with `previousFrameCompleted = true` gives an empty record and the same image pointer. With `previousFrameCompleted = false` the record is non-empty and the image is the same. A new extent gives a non-empty record and a new image with the new size.
  - Both end with the registry's `validationMessageCount() == 0`, after the context is released.
- First failure: the first build fails because `test_pattern.h` is missing. After the stubs exist, `testPatternPixel` returning 0 makes every pattern case fail.

**Steps** (from the workspace root; WSL only for clang-format in Step 8; git only in PowerShell, except the read-only `git ls-files` and `git diff --name-only` inside the WSL commands). Record `$wsBase = git -C rawreel-workspace rev-parse HEAD` before Step 1 (`<base>` for rawreel-workspace; rawreel is new):
- [ ] **Step 1: Create the repository and its identity.**
  ```powershell
  New-Item -ItemType Directory rawreel | Out-Null
  git -C rawreel init -b m1b
  git -C rawreel config user.name (git -C rawreel-workspace config user.name)
  git -C rawreel config user.email (git -C rawreel-workspace config user.email)
  git -C rawreel config core.hooksPath ../rawreel-workspace/tools/githooks
  foreach ($f in 'CMakePresets.json','.clang-format','.clang-tidy','.gitattributes','.gitignore','LICENSE') { Copy-Item "dvv-gpu/$f" "rawreel/$f" }
  New-Item -ItemType Directory rawreel/tests | Out-Null; Copy-Item dvv-gpu/tests/tsan.supp rawreel/tests/tsan.supp
  ```
  Expected: `git -C rawreel config user.email` ends with `@users.noreply.github.com`.
- [ ] **Step 2: Workspace wiring** (four files as above). The workspace configure check runs in Step 6, once rawreel has its `CMakeLists.txt`.
- [ ] **Step 3: Tests first.** Write the test files and `tests/CMakeLists.txt`, plus a `README.md` with one paragraph: the purpose (rawreel-gui, M1b test pattern) and that it builds through rawreel-workspace.
- [ ] **Step 4: Build to see the failure.** Write `CMakeLists.txt` with the source files present as stubs (`testPatternPixel` returns 0, `frame()` returns `Internal`). Run `Push-Location rawreel; cmake --preset vs2026; cmake --build --preset vs2026; ctest --preset vs2026 -R 'rawreel\.unit' --output-on-failure; Pop-Location`. Expected: the `testPatternPixel` cases fail.
- [ ] **Step 5: Implement** `test_pattern.cpp`, `arguments.cpp`, the shader and the renderer. Then run `$env:DVV_REQUIRE_GPU='1'; $env:DVV_VK_VALIDATION='sync'; $env:DVV_VK_VALIDATION_REQUIRED='1'; Push-Location rawreel; cmake --build --preset vs2026; ctest --preset vs2026 --output-on-failure; Pop-Location`. Expected: `rawreel.unit` and `rawreel.gpu` pass, and the GPU test is not skipped.
- [ ] **Step 6: The workspace configure:** `Push-Location rawreel-workspace; cmake --preset vs2026 2>&1 | Select-String 'error|CMake Error'; Pop-Location`. Expected: no output (no `dvv_check_siblings` error; rawreel is taken from its sibling folder).
- [ ] **Step 7: Commit both repositories:**
  ```powershell
  git -C rawreel add -A
  git -C rawreel commit -m "feat: add the rawreel skeleton with the test-pattern renderer" -m "rawreel_render: testPatternPixel (CPU reference), test_pattern.slang, TestPatternRenderer for a Presenter's frame provider, and the rawreel-gui argument parser and exit codes. GPU output equals the reference exactly at 1x1, 33x17 and 640x360." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  git -C rawreel-workspace add -A
  git -C rawreel-workspace commit -m "build: add rawreel to the workspace" -m "repos.txt, dvv-versions.cmake, the workspace build and the presets now include the local rawreel repository." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```
- [ ] **Step 8: Per-task checks on the final commits** (Global Constraints):
  - Windows Debug, all tests, `rawreel` (`run_in_background: true`): `$env:DVV_REQUIRE_GPU='1'; $env:DVV_VK_VALIDATION='sync'; $env:DVV_VK_VALIDATION_REQUIRED='1'; Push-Location rawreel; cmake --preset vs2026 | Out-Null; cmake --build --preset vs2026; ctest --preset vs2026 --output-on-failure 2>&1 | Select-String 'tests passed|tests failed|Skipped|error'; Pop-Location`.
  - clang-format on every C++ file of the new repository: `wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd rawreel && git ls-files -z "*.h" "*.cpp" | xargs -0 -r clang-format --dry-run --Werror && echo FORMAT-OK'`. rawreel-workspace has no C++ change.
  - The privacy lint on `rawreel` and `rawreel-workspace`: `foreach ($r in 'rawreel','rawreel-workspace') { cmake "-DREPO=$r" -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake }`.
  Not a checkpoint: Release, ASan and clang-tidy run at the Task 12a checkpoint. The workspace workflow itself runs once for the group, in Task 13b. Here only its configure (Step 6) is checked.

**Expected results:** `rawreel` passes 2 CTest entries (`rawreel.unit`, `rawreel.gpu`) in vs2026 Debug. The GPU test runs (not skipped) on NVIDIA with 0 validation messages. The workspace configures. FORMAT-OK; privacy lint clean. Nothing runs on Linux (U-WIN); the Linux build of rawreel is first compiled at the Task 12a checkpoint.

**Commit:** `feat: add the rawreel skeleton with the test-pattern renderer` (rawreel) and `build: add rawreel to the workspace` (rawreel-workspace), both with the trailer.

**Notes:**
- Design references: D1, §3 (rawreel), §4.6, §8.1 rawreel rows, R12 (the repository name clash only matters at publishing).
- **Note (plan choice):** `parseArguments`, `skipExitCode`, the exit codes and `USAGE` live in `rawreel_render`, the one wx-free library, so they are unit-tested and also usable by sanitizer builds. Design §3 lists only the test pattern there; no new target is added for these.
- **Note (plan choice):** `Errc::Format` for an unknown argument. Exit code 1 also covers "the GUI could not start" and an unexpected exception, because the design defines no other code for them.
- rawreel has no package test (it is not installed) and no `.github/workflows/ci.yml` yet (Task 13a adds it).

---

### Task 12a: `rawreel-gui` on Windows: wx frame, viewport, `--selftest`

**Goal:** Build `rawreel-gui` on Windows: `wmain` entry, wx App with GPU startup, the main frame, the MSW `VulkanViewport` with the presenter handshake of design §5.6, the `--selftest` script, the manifest resource and CTest `rawreel.gui_selftest`. It passes on local NVIDIA hardware and is checked manually against the exit criterion. On other platforms `rawreel-gui` is built with a plain `main` and a stub `nativeWindowOf`, so the GUI code keeps compiling and stays portable; it is not run there (U-WIN; the Linux GUI is v1.x).

**Files** (rawreel):
- Create `src/app.h`, `src/app.cpp`: `rawreel::App` (startup, exit code, `OnExit`).
- Create `src/main_frame.h`, `src/main_frame.cpp`: `MainFrame` (forwards iconize/show, close order, owns the selftest).
- Create `src/vulkan_viewport.h`, `src/vulkan_viewport.cpp`: `VulkanViewport` (presenter handshake).
- Create `src/native_window.h`: `nativeWindowOf` declaration. Create `src/native_window_msw.cpp`: the MSW implementation. Create `src/native_window_stub.cpp`: the stub for other platforms.
- Create `src/selftest.h`, `src/selftest.cpp`: the `--selftest` script.
- Create `src/main.cpp`: the entry point (`wmain` on Windows, a plain `main` elsewhere).
- Create `src/rawreel-gui.rc`: the wx resources and manifest.
- Modify `CMakeLists.txt`: the GUI block. Modify `tests/CMakeLists.txt`: `rawreel.gui_selftest`.

**Interfaces:**
- Consumes:
  - Task 11: `rawreel_render` (`parseArguments`, `skipExitCode`, `exit_code::*`, `USAGE`, `TestPatternRenderer`).
  - Tasks 9/10: `Presenter`, `createPresenter`, `PresenterOptions`, `PresenterStats`.
  - Task 7: `GpuRegistryOptions::windowSystem`, `ContextOptions::presentation`, `GpuInfo::optional.swapchain`.
  - Task 6: `platform::defaultWindowSystem`, `NativeWindow`, `Win32Window`.
  - Task 4: wx 3.3.4 in the prefix with imported targets `wx::core wx::base`. If Task 4 took the R1 fallback, use the `rawreel::wx` INTERFACE target that its "Changes during execution" entry defines, copied into rawreel's `CMakeLists.txt` as written there.
- Produces:
  - Executable target `rawreel_gui` (`OUTPUT_NAME rawreel-gui`), created only when `DVV_SANITIZE` is empty, on every platform.
  - CTest `rawreel.gui_selftest`, labels `gpu;display;gui`, registered on Windows only (U-WIN).
  - `rawreel::nativeWindowOf(wxWindow&)`: MSW; a `std::monostate` stub elsewhere (the GTK version is v1.x).
  - `rawreel::App::exitCode()` / `setArguments()`, and the `--selftest` script with its hide/show switch.

```cpp
// src/native_window.h
namespace rawreel {
// The native handle of a wx window for dvv::platform::createSurface. MSW: Win32Window{instance of
// the window's class, HWND}. Other toolkits: std::monostate until their native-window path exists
// (wxGTK/X11 follows with Linux support), so the viewport reports Unsupported.
[[nodiscard]] dvv::platform::NativeWindow nativeWindowOf(wxWindow& window);
}  // namespace rawreel

// src/app.h  (wxDECLARE_APP(rawreel::App) at global scope after the namespace)
namespace rawreel {
class App final : public wxApp {
public:
    // main() passes the parsed arguments before wxEntry().
    static void setArguments(const Arguments& arguments) noexcept;
    [[nodiscard]] static const Arguments& arguments() noexcept;
    // The exit code the App decided (0 after a passed selftest, 4, 6, 77); nullopt otherwise. main()
    // reads it after wxEntry(), which returns -1 when OnInit() fails.
    [[nodiscard]] static std::optional<int> exitCode() noexcept;
    // Records the exit code (the first one wins), reports `message` (stderr through the log; a
    // modal wxMessageBox as well when not --selftest and code != 0) and closes the top window.
    void finish(int code, const std::string& message);
    bool OnInit() override;
    int OnExit() override;
    [[nodiscard]] dvv::gpu::GpuRegistry* registry() const noexcept { return registry_.get(); }

private:
    [[nodiscard]] bool startGpu();
    std::shared_ptr<dvv::gpu::GpuRegistry> registry_;
    std::shared_ptr<dvv::gpu::ExecutionContext> context_;
    std::unique_ptr<TestPatternRenderer> renderer_;
};
}  // namespace rawreel
```

**Behaviour** (binding: design §5.6, §4.6 exit codes, §6 shutdown order):
- **Entry (Windows):** `int wmain(int argc, wchar_t** argv)` with link options `/ENTRY:wmainCRTStartup /MANIFEST:NO` and `wxIMPLEMENT_APP_NO_MAIN(rawreel::App)`. It converts `argv[1..]` to UTF-8 with `wxString(argv[i]).utf8_string()` and parses **before** `wxEntry`:
  - a bad argument prints `rawreel-gui: <message>` and `USAGE` to stderr and exits 1;
  - `--help` prints `USAGE` to stdout and exits 0.
  Then it adds a `dvv::StderrSink` to the logger, calls `App::setArguments` and `wxEntry(argc, argv)`. The exit code is `App::exitCode()` when set; else 0 when `wxEntry` returned 0, else 1 with `rawreel-gui: the GUI could not start`. The process returns `dvv::finishWithLeakCheck(code)`. An escaping `std::exception` prints and exits 1.
- **Entry (other platforms, compiled, not run in v1.0):** `int main(int argc, char** argv)` takes the arguments as UTF-8 and calls the same `run`. The Linux start-up of the design (`XInitThreads`, `GDK_BACKEND=x11`, the `DISPLAY` check before wx) is v1.x (U-WIN).
- **`App::OnInit`:** it must not call `wxApp::OnInit()`, which would parse the command line again and reject `--selftest`. Steps:
  1. `startGpu()`:
     - `GpuRegistry::create({.applicationName = "rawreel", .windowSystem = platform::defaultWindowSystem()})`;
     - `bestDevice()` whose `optional.swapchain` is true;
     - `defaultPipelineCacheFile("rawreel", info)`. **Note (plan choice):** when this fails, log a warning and use the in-memory cache (empty path).
     - `createContext(id, {.pipelineCacheFile = file, .presentation = true})`;
     - `TestPatternRenderer::create(context)`.
     A failed registry with `Unsupported`, no best device, or a best device without swapchain is "no usable device": `--selftest` exits `skipExitCode()` (77, or 4 with `DVV_REQUIRE_GPU=1`), otherwise 4. Any other failure exits 4. The message is `Vulkan could not start: <error.describe()>`, or `no Vulkan 1.4 device that can present to a window`. It goes to stderr; without `--selftest` it is also shown in a modal `wxMessageBox` (spec §10.2: modal only for fatal startup). Then `return false`.
  2. `new MainFrame(context_, *renderer_)`, `frame->Show()`, then `frame->viewport()->CallAfter(...)`, which calls `startPresenting()`. A failure there is `App::finish(exit_code::GPU_ERROR, "the viewport could not present: " + describe)`.
  3. With `--selftest`: `frame->startSelfTest()`.
- **`App::OnExit`:**
  1. `renderer_.reset()`; `context_.reset()` (drains; teardown messages are counted now).
  2. With `--selftest`, when no code is set yet: if `registry_->validationMessageCount() != 0`, then `finish`-style code 6 with `rawreel-gui --selftest: step 5 (close) failed: <n> validation messages`; else code 0 and `rawreel-gui --selftest: passed` on stderr.
  3. `registry_.reset()`; `return wxApp::OnExit();`.
  When `OnInit` fails, wx does not call `OnExit`. The members are released when wx deletes the App inside `wxEntry`, still before the leak check.
- **`MainFrame`:**
  - Title `rawreel`, client size `FromDIP(wxSize(960, 540))`, one child `VulkanViewport` (wxFrame lays out a single child to fill the client area).
  - It binds `wxEVT_ICONIZE` → `viewport_->topLevelIconized(event.IsIconized())`, `wxEVT_SHOW` → `viewport_->topLevelShown(event.IsShown())` and `wxEVT_CLOSE_WINDOW`; all `Skip()`.
  - `onClose`: stops and deletes the selftest, then `viewport_->Destroy(); viewport_ = nullptr;` (a child's `Destroy()` deletes it now, so its destructor resets the presenter first), then `event.Skip()` so wx destroys the frame.
  - `startSelfTest()` creates the `SelfTest`.
- **`VulkanViewport`** (code below).
- **`--selftest` script** (`SelfTest`, owned by the frame):
  - A `wxTimer` ticks every 50 ms. Every tick calls `presenter->requestFrame()`, also during the wait of step 3, which proves that requests are ignored while hidden. Each step has a 5 s deadline from its start.
  - A missed deadline: `App::finish(6, "rawreel-gui --selftest: step <k> (<name>) failed: <what was seen>")`.
  - `presenter->failure()` at any tick: `App::finish(4, "rawreel-gui --selftest: the presenter failed: <describe>")`.
  - No presenter yet after 5 s in step 1 counts as a missed step 1.
  - Steps:
    1. `first frames`: `framesPresented >= 2 && presentedExtent == viewport.physicalClientSize()`.
    2. `resize 800x600`, then `resize 333x177`: `frame.SetClientSize(wxSize(800, 600))`, then `(333, 177)`, in logical pixels (no `FromDIP`). Pass when `presentedExtent == viewport.physicalClientSize()`, `swapchainsCreated` exceeds its value at the step start, and the viewport's physical size differs from the previous step's.
    3. `hide`: Windows `frame.Iconize(true)`; elsewhere `frame.Hide()` (an `#if defined(_WIN32)` switch; the other branch is compiled, not run in v1.0). Record `n = framesPresented` at the step start. After 300 ms (6 ticks): `framesPresented <= n + 1`, `hidden`, `!failed`.
    4. `restore`: Windows `frame.Iconize(false)`; elsewhere `frame.Show()`. Record `n` at the step start. Pass when `framesPresented > n && !hidden`.
    5. `close`: `failure()` empty and `!stats().failed`, else code 4. Then `frame.Close()`. `OnExit` checks validation; `finishWithLeakCheck` in `main` checks leaks.
- **`.rc` and manifest:** the program carries exactly one RT_MANIFEST (id 1), from `wx/msw/wx.rc`, with common controls v6 and `PerMonitorV2` DPI awareness. The linker writes none (`/MANIFEST:NO`).
- **R11:** every rawreel GUI source includes `<wx/wx.h>` first. If `NOMINMAX`/`WIN32_LEAN_AND_MEAN`/`UNICODE` from `dvv_set_compiler_options` still conflict with wx's MSW headers, add the optional keyword `NO_WIN32_LEAN_AND_MEAN` to `dvv_set_compiler_options` in dvv-base. It skips that one definition, is used by `rawreel_gui`, and is committed to dvv-base on `m1b` with its own commit. Only do this if the build needs it.

**Code**

`src/vulkan_viewport.h` / `.cpp` (complete; the presenter handshake of design §5.6):

```cpp
// vulkan_viewport.h
#pragma once

#include <wx/wx.h>

#include <memory>

#include "dvv/base/error.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/presenter.h"
#include "dvv/platform/native_window.h"

namespace rawreel {

class TestPatternRenderer;

// The viewport (design M1b §5.6): a native child window that owns one Presenter, which shows the
// test pattern. The GUI thread only calls the presenter's non-blocking members.
class VulkanViewport final : public wxWindow {
public:
    VulkanViewport(wxWindow* parent, std::shared_ptr<dvv::gpu::ExecutionContext> context,
                   TestPatternRenderer& renderer);
    VulkanViewport(const VulkanViewport&) = delete;
    VulkanViewport& operator=(const VulkanViewport&) = delete;
    // Resets the presenter first (binding, §5.6): its thread is joined while the native window
    // still exists, and no CallAfter from its error handler can target this object afterwards.
    ~VulkanViewport() override;

    // Creates the presenter for the realised native window; call it through CallAfter once the
    // top-level frame is shown.
    [[nodiscard]] dvv::Result<void> startPresenting();
    [[nodiscard]] dvv::gpu::Presenter* presenter() const noexcept { return presenter_.get(); }
    // Forwarded by the frame (wxEVT_ICONIZE and wxEVT_SHOW of the top-level window).
    void topLevelIconized(bool iconized);
    void topLevelShown(bool shown);
    // The client area in physical pixels (wx 3.3 ToPhys).
    [[nodiscard]] dvv::platform::PixelSize physicalClientSize() const;

private:
    void onPaint(wxPaintEvent& event);
    void onSize(wxSizeEvent& event);
    void showError(const dvv::Error& error);  // GUI thread

    std::shared_ptr<dvv::gpu::ExecutionContext> context_;
    TestPatternRenderer* renderer_;  // owned by the App, outlives every viewport
    std::unique_ptr<dvv::gpu::Presenter> presenter_;
    wxString status_;  // a presenter failure, drawn instead of the image
};

}  // namespace rawreel
```

```cpp
// vulkan_viewport.cpp
#include <wx/wx.h>  // first (R11)

#include "vulkan_viewport.h"

#include <algorithm>
#include <cstdio>
#include <utility>
#include <variant>

#include "dvv/base/log.h"
#include "native_window.h"
#include "test_pattern_renderer.h"

namespace rawreel {

VulkanViewport::VulkanViewport(wxWindow* parent,
                               std::shared_ptr<dvv::gpu::ExecutionContext> context,
                               TestPatternRenderer& renderer)
    : context_(std::move(context)), renderer_(&renderer) {
    // wx never erases the background, so nothing paints over the presented image.
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE);
    Bind(wxEVT_PAINT, &VulkanViewport::onPaint, this);
    Bind(wxEVT_SIZE, &VulkanViewport::onSize, this);
}

VulkanViewport::~VulkanViewport() {
    presenter_.reset();
}

dvv::Result<void> VulkanViewport::startPresenting() {
    const dvv::platform::NativeWindow window = nativeWindowOf(*this);
    if (std::holds_alternative<std::monostate>(window)) {
        return dvv::makeError(dvv::Errc::Unsupported,
                              "the viewport has no native Win32 or X11 window");
    }
    TestPatternRenderer* renderer = renderer_;
    auto presenter = context_->createPresenter(
        {.window = window,
         .extent = physicalClientSize(),
         .visible = IsShownOnScreen(),
         .frames = [renderer](const dvv::gpu::PresentFrameInfo& info) {
             return renderer->frame(info);
         },
         .onError =
             [this](const dvv::Error& error) noexcept {
                 // Presenter thread. CallAfter allocates, so it may throw; the handler may not.
                 try {
                     CallAfter([this, error] { showError(error); });
                 } catch (...) {
                     std::fputs("rawreel-gui: a presenter error could not reach the GUI\n", stderr);
                 }
             },
         .name = "viewport"});
    if (!presenter) {
        return std::unexpected(std::move(presenter).error());
    }
    presenter_ = std::move(*presenter);
    return {};
}

void VulkanViewport::topLevelIconized(bool iconized) {
    if (presenter_) {
        presenter_->setVisible(!iconized);
    }
}

void VulkanViewport::topLevelShown(bool shown) {
    if (presenter_) {
        presenter_->setVisible(shown);
    }
}

dvv::platform::PixelSize VulkanViewport::physicalClientSize() const {
    const wxSize size = ToPhys(GetClientSize());
    return {.width = static_cast<std::uint32_t>(std::max(size.x, 0)),
            .height = static_cast<std::uint32_t>(std::max(size.y, 0))};
}

void VulkanViewport::onPaint(wxPaintEvent& /*event*/) {
    wxPaintDC dc(this);  // validates the update region, whatever is drawn
    if (!status_.empty()) {
        dc.SetBackground(*wxBLACK_BRUSH);
        dc.Clear();
        dc.SetTextForeground(*wxWHITE);
        dc.DrawText(status_, FromDIP(wxPoint(8, 8)));
        return;
    }
    if (presenter_) {
        presenter_->requestFrame();
    }
}

void VulkanViewport::onSize(wxSizeEvent& event) {
    if (presenter_) {
        presenter_->resize(physicalClientSize());
    }
    event.Skip();
}

void VulkanViewport::showError(const dvv::Error& error) {
    DVV_LOG_ERROR("viewport: {}", error.describe());
    if (presenter_ && presenter_->failure()) {
        status_ = wxString::FromUTF8(error.describe());
        Refresh();
    }
}

}  // namespace rawreel
```

`src/native_window_msw.cpp` (complete):

```cpp
#include <wx/wx.h>  // first (R11)
#include <wx/msw/wrapwin.h>

#include "native_window.h"

namespace rawreel {

dvv::platform::NativeWindow nativeWindowOf(wxWindow& window) {
    const auto hwnd = static_cast<HWND>(window.GetHWND());
    // The module that registered the window class: rawreel-gui itself (wx is linked statically).
    const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
    return dvv::platform::Win32Window{.hinstance = instance, .hwnd = hwnd};
}

}  // namespace rawreel
```

`src/native_window_stub.cpp` (complete; built where wx is not MSW):

```cpp
#include <wx/wx.h>  // first (R11)

#include <variant>

#include "native_window.h"

namespace rawreel {

// No native-window path for this toolkit yet: wxGTK/X11 follows with Linux support. The viewport
// then reports Unsupported. Built so that the GUI code keeps compiling on every platform.
dvv::platform::NativeWindow nativeWindowOf(wxWindow& /*window*/) {
    return std::monostate{};
}

}  // namespace rawreel
```

`src/main.cpp` (complete):

```cpp
// rawreel-gui entry point (spec §10.2; design M1b §5.6). The arguments are parsed before wx
// starts. The App's exit code is kept in App::exitCode(), because wxEntry() returns -1 when
// OnInit() fails.
#include <wx/wx.h>  // first: wx's Windows headers before anything else includes <windows.h> (R11)

#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app.h"
#include "arguments.h"
#include "dvv/base/leak_check.h"
#include "dvv/base/log.h"

wxIMPLEMENT_APP_NO_MAIN(rawreel::App);

namespace {

// Parses, then runs wx through `start_wx`; returns the process exit code.
template <class StartWx>
int run(std::span<const std::string_view> args, StartWx start_wx) {
    const dvv::Result<rawreel::Arguments> arguments = rawreel::parseArguments(args);
    if (!arguments) {
        std::fprintf(stderr, "rawreel-gui: %s\n\n%s", arguments.error().message.c_str(),
                     rawreel::USAGE);
        return rawreel::exit_code::BAD_ARGUMENTS;
    }
    if (arguments->help) {
        std::fputs(rawreel::USAGE, stdout);
        return rawreel::exit_code::SUCCESS;
    }
    dvv::Logger::instance().addSink(std::make_shared<dvv::StderrSink>());
    rawreel::App::setArguments(*arguments);
    const int wx_result = start_wx();
    int code = rawreel::exit_code::SUCCESS;
    if (const std::optional<int> decided = rawreel::App::exitCode(); decided) {
        code = *decided;
    } else if (wx_result != 0) {
        std::fputs("rawreel-gui: the GUI could not start\n", stderr);
        code = rawreel::exit_code::BAD_ARGUMENTS;
    }
    return dvv::finishWithLeakCheck(code);
}

}  // namespace

#if defined(_WIN32)
// wmain with /ENTRY:wmainCRTStartup: a GUI-subsystem program with wide arguments (§5.6).
int wmain(int argc, wchar_t** argv) {
    try {
        std::vector<std::string> utf8;
        for (int i = 1; i < argc; ++i) {
            utf8.push_back(wxString(argv[i]).utf8_string());
        }
        const std::vector<std::string_view> args(utf8.begin(), utf8.end());
        return run(args, [&argc, argv] { return wxEntry(argc, argv); });
    } catch (const std::exception& error) {
        std::fprintf(stderr, "rawreel-gui: %s\n", error.what());
        return rawreel::exit_code::BAD_ARGUMENTS;
    }
}
#else
// Other platforms: the arguments are UTF-8 already. Compiled, not run in v1.0; the Linux start-up
// (XInitThreads, GDK_BACKEND=x11, the DISPLAY check) comes with Linux support.
int main(int argc, char** argv) {
    try {
        const std::vector<std::string_view> args(argv + 1, argv + argc);
        return run(args, [&argc, argv] { return wxEntry(argc, argv); });
    } catch (const std::exception& error) {
        std::fprintf(stderr, "rawreel-gui: %s\n", error.what());
        return rawreel::exit_code::BAD_ARGUMENTS;
    }
}
#endif
```

`src/rawreel-gui.rc` (complete):

```rc
// rawreel-gui resources: wxWidgets' standard resources and its application manifest (common
// controls v6, per-monitor DPI awareness v2). The linker writes no manifest (/MANIFEST:NO), so
// this is the program's only one.
#define wxUSE_RC_MANIFEST 1
#define wxUSE_DPI_AWARE_MANIFEST 2
#include "wx/msw/wx.rc"
```

`rawreel/CMakeLists.txt`: insert before `if(DVV_BUILD_TESTS)`:

```cmake
# rawreel-gui (spec §10.2). Not built for sanitizer builds on any platform (design M1b F13): wx and
# GTK are not instrumented, and MSVC ASan cannot link the non-ASan static wx. rawreel_render and its
# tests still build and run there. v1.0 runs it on Windows only; elsewhere it is built with a stub
# native window, so the GUI code stays portable.
if(NOT DVV_SANITIZE)
    find_package(wxWidgets CONFIG REQUIRED COMPONENTS core base)
    add_executable(rawreel_gui WIN32
        src/app.cpp
        src/main.cpp
        src/main_frame.cpp
        src/selftest.cpp
        src/vulkan_viewport.cpp)
    set_target_properties(rawreel_gui PROPERTIES OUTPUT_NAME rawreel-gui)
    # wx is an imported target, so its headers are SYSTEM includes (our /WX does not apply to them).
    target_link_libraries(rawreel_gui PRIVATE rawreel_render dvv::platform dvv::gpu dvv::base
                                              wx::core wx::base)
    dvv_set_compiler_options(rawreel_gui)
    if(WIN32)
        target_sources(rawreel_gui PRIVATE src/native_window_msw.cpp src/rawreel-gui.rc)
        # wmain() in a GUI-subsystem program; the manifest comes from wx.rc (rawreel-gui.rc).
        target_link_options(rawreel_gui PRIVATE /ENTRY:wmainCRTStartup /MANIFEST:NO)
    else()
        # No native viewport outside Windows yet (Linux support follows).
        target_sources(rawreel_gui PRIVATE src/native_window_stub.cpp)
    endif()
endif()
```

`rawreel/tests/CMakeLists.txt`: append

```cmake
# GUI smoke test (spec §12; design M1b §5.6): rawreel-gui --selftest. It exits 77 without a usable
# device or display unless DVV_REQUIRE_GPU=1. Only where rawreel_gui exists (no sanitizer builds),
# and on Windows only: elsewhere the viewport has no native window yet.
if(TARGET rawreel_gui AND WIN32)
    add_test(NAME rawreel.gui_selftest COMMAND rawreel_gui --selftest)
    set_tests_properties(rawreel.gui_selftest PROPERTIES
        LABELS "gpu;display;gui"
        SKIP_RETURN_CODE 77
        TIMEOUT 120
        ENVIRONMENT "${gpu_test_environment};NO_AT_BRIDGE=1"
        ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
endif()
```

**Tests first:**
- `rawreel.gui_selftest` is the test: it is registered first and fails until the GUI exists (no `rawreel_gui` target → the test is not registered → `ctest -R gui_selftest` reports "No tests were found", which counts as the failing state).
- Unit additions in `tests/test_arguments.cpp`: `"USAGE names every exit code"`. It `CHECK`s that `USAGE` contains `"0 success"`, `"1 bad arguments"`, `"4 GPU error"`, `"6 a --selftest check failed"`, `"77"`.

**Checkpoint task (U-PROC):** Task 12a is a checkpoint. It covers Tasks 11 and 12a: `rawreel` (created in Task 11, so clang-tidy runs on all its sources) and `dvv-base` if R11 changed it (`<cp-base>` = its head before Task 11). rawreel-workspace has no C++ sources; its workflow runs in Task 13b.

**Steps** (from the workspace root; WSL only for clang-format in Step 8 and the checkpoint build in Step 9; git only in PowerShell, except the read-only `git ls-files` and `git diff --name-only` inside the WSL commands):
- [ ] **Step 1: Register the test and see it missing.** Add the `tests/CMakeLists.txt` block and the `USAGE` unit case. Run `Push-Location rawreel; cmake --preset vs2026; ctest --preset vs2026 -R 'rawreel\.gui_selftest'; Pop-Location`. Expected: `No tests were found!!!`.
- [ ] **Step 2: Write the sources and the CMake block.** Build: `Push-Location rawreel; cmake --preset vs2026; cmake --build --preset vs2026; Pop-Location`. Expected: no warning or error in Debug (`/MDd`, Debug wx). The Release build (`/MD`, Release wx) runs in the Step 9 checkpoint. If wx headers clash with `WIN32_LEAN_AND_MEAN`/`NOMINMAX`, follow R11 (see Behaviour). If LNK2038 (`RuntimeLibrary` mismatch) or LNK4099 appears, the Debug/Release wx split of Task 4 (R1) is not being used: check which `wxWidgets` config `find_package` picked (`cmake --preset vs2026 --log-level=VERBOSE`).
- [ ] **Step 3: The manifest.**
  ```powershell
  $mt = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\mt.exe" | Sort-Object FullName | Select-Object -Last 1
  $exe = (Resolve-Path _build/rawreel/vs2026/Debug/rawreel-gui.exe).Path
  $out = Join-Path ([IO.Path]::GetTempPath()) 'rawreel-gui.manifest.xml'
  & $mt.FullName -nologo "-inputresource:$exe;#1" "-out:$out"; Get-Content $out | Select-String 'Common-Controls|version="6|PerMonitorV2'; Remove-Item $out
  ```
  Expected: three matches: `Microsoft.Windows.Common-Controls`, `version="6.0.0.0"`, `PerMonitorV2`. Also check that no second manifest resource exists: `-inputresource:$exe;#2` must fail. <!-- version, not an address: privacy-lint: allow -->
- [ ] **Step 4: The selftest on NVIDIA:** `$env:DVV_REQUIRE_GPU='1'; $env:DVV_VK_VALIDATION='sync'; $env:DVV_VK_VALIDATION_REQUIRED='1'; Push-Location rawreel; ctest --preset vs2026 -R 'rawreel\.gui_selftest' --output-on-failure; Pop-Location`. Expected: passed in Debug, not skipped (Release runs in the Step 9 checkpoint); the output ends with `rawreel-gui --selftest: passed`; no `Vulkan validation` lines; no leak report.
- [ ] **Step 5: Exit codes.** `& _build/rawreel/vs2026/Debug/rawreel-gui.exe --bogus 2>&1 | Out-Host; $LASTEXITCODE` → 1. `--help` → 0, and `USAGE` is printed when piped (a GUI-subsystem program prints only to redirected handles). Run the selftest with the loader hidden: `$env:VK_LOADER_DRIVERS_SELECT='none-such'; & …\rawreel-gui.exe --selftest 2>&1 | Out-Host; $LASTEXITCODE` → 77. With `$env:DVV_REQUIRE_GPU='1'` as well → 4. Clear both variables afterwards. (`VK_LOADER_DRIVERS_SELECT` with a filter that matches nothing makes the loader report no device. If this loader version ignores it, record that and skip the check.)
- [ ] **Step 6: Manual exit-criterion check (with the user).**
  1. Start `$env:DVV_VK_VALIDATION='sync'; Start-Process _build/rawreel/vs2026/Debug/rawreel-gui.exe -RedirectStandardError (Join-Path ([IO.Path]::GetTempPath()) 'rawreel-gui.err.txt')`.
  2. After 3 s, capture a screenshot of the primary screen with .NET (`System.Windows.Forms.Screen`, `System.Drawing.Graphics.CopyFromScreen`) into the session scratchpad (never into a repository) and show it to the user.
  3. Ask the user to drag-resize, maximise, minimise/restore and close the window, and to confirm that the pattern redraws at each size.
  4. Then read the stderr file: no validation message, except VUID-VkSwapchainCreateInfoKHR-imageExtent-01274 during a drag-resize (known false positive, §8.4), and no leak report.
  Record the result in the commit message.
- [ ] **Step 7: Commit:**
  ```powershell
  git -C rawreel add -A
  git -C rawreel commit -m "feat: add rawreel-gui on Windows with the Vulkan viewport and --selftest" -m "wmain entry, wx App with GPU startup and exit codes, MainFrame forwarding iconize and show, MSW VulkanViewport owning a Presenter (design M1b §5.6), the selftest script, and the wx manifest (common controls v6, PerMonitorV2). Other platforms build it with a plain main and a stub native window; v1.0 runs it on Windows only. Manual check: <result>." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```
- [ ] **Step 8: Per-task checks on the final commit** (Global Constraints), in `rawreel` (and `dvv-base` if R11 changed it, with `cd dvv-base` and its `<base>`):
  - Windows Debug, all tests (`run_in_background: true`): `$env:DVV_REQUIRE_GPU='1'; $env:DVV_VK_VALIDATION='sync'; $env:DVV_VK_VALIDATION_REQUIRED='1'; Push-Location rawreel; cmake --preset vs2026 | Out-Null; cmake --build --preset vs2026; ctest --preset vs2026 --output-on-failure 2>&1 | Select-String 'tests passed|tests failed|Skipped|error'; Pop-Location`. vs2026 now has 3 CTest entries.
  - clang-format on the changed files (`<base>` = the `rawreel` head before this task): `wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd rawreel && git diff --name-only -z --diff-filter=d <base> -- "*.h" "*.cpp" | xargs -0 -r clang-format --dry-run --Werror && echo FORMAT-OK'`.
  - Privacy lint: `cmake -DREPO=rawreel -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake`.
- [ ] **Step 9: Checkpoint checks** (Global Constraints; Tasks 11 and 12a). The Windows call and the WSL build run in parallel, each with `run_in_background: true`:
  - Windows Release, then ASan, in `rawreel`: `$env:DVV_REQUIRE_GPU='1'; $env:DVV_VK_VALIDATION='sync'; $env:DVV_VK_VALIDATION_REQUIRED='1'; Push-Location rawreel; cmake --build --preset vs2026-release; ctest --preset vs2026-release --output-on-failure 2>&1 | Select-String 'tests passed|tests failed|Skipped|error'; cmake --workflow --preset vs2026-asan 2>&1 | Select-String 'tests passed|tests failed|Skipped|AddressSanitizer'; Pop-Location`. Release has 3 CTest entries (the selftest runs, not skipped, and ends with `rawreel-gui --selftest: passed`; an LNK2038/LNK4099 here means the wx Debug/Release split of R1 is not used, see Step 2); vs2026-asan has 2, because `rawreel_gui` is not created under `DVV_SANITIZE`. No ×10 repeats in Tasks 11-12a.
  - clang-tidy on all `rawreel` sources after the `ninja-clang` configure + build (no tests). This build is also the Linux sanity build of the GUI build files: `rawreel-gui` must compile and link on Linux against the static wxGTK of the prefix, so clang-tidy sees every GUI source in the compile database; `native_window_msw.cpp` is excluded:
    ```powershell
    wsl -d Ubuntu-24.04 -e bash -lc 'set -o pipefail && . _build/tools/linux/env.sh && cd rawreel && cmake --preset ninja-clang >/dev/null && cmake --build --preset ninja-clang 2>&1 | tail -n 30 && git ls-files -z ":(glob)src/**/*.cpp" ":(glob)tests/*.cpp" ":(glob)tools/*.cpp" ":(exclude,glob)**/*_win32.cpp" ":(exclude,glob)**/*_msw.cpp" | xargs -0 -r -P "$(nproc)" -n 1 clang-tidy -p ../_build/rawreel/ninja-clang --quiet && echo TIDY-OK'
    ```
    Expected `TIDY-OK`. If the link misses system libraries that the static wx needs (R3), add them in the `else()` branch with `target_link_libraries(rawreel_gui PRIVATE …)` and a comment (our portability bug). A toolchain or third-party failure is logged and reported, not fixed. Nothing is run on Linux.
  - If dvv-base changed (R11): the same two blocks with `dvv-base` (`-p ../_build/dvv-base/ninja-clang`, and `git diff --name-only -z --diff-filter=d <cp-base> HEAD -- <the same pathspecs>` in place of `git ls-files -z`).
  - A failure is fixed with a `fix:` commit, then the Per-task checks and the failed check run again, before Task 13a starts.

**Expected results:**
- Per-task checks: Windows Debug: `rawreel.unit`, `rawreel.gpu` and `rawreel.gui_selftest` pass; the selftest runs (not skipped) on NVIDIA with `sync` validation. FORMAT-OK; privacy lint clean.
- Checkpoint: Windows Release: the same 3 entries pass, the selftest not skipped. vs2026-asan: `rawreel.unit` and `rawreel.gpu` pass; no AddressSanitizer report. Linux: `rawreel-gui` compiles and links in the checkpoint build; no test is run there. TIDY-OK.
- The manifest checks pass. The exit codes are 1/0/77/4 as in Step 5. The manual check is confirmed by the user.

**Commit:** `feat: add rawreel-gui on Windows with the Vulkan viewport and --selftest` (plus `build: …` in dvv-base only if R11 was needed), with the trailer.

**Notes:**
- Design references: D1, D9, D11, D12, §3 rawreel, §4.6, §5.6, §6, §8.1, §8.4, R1, R3, R10, R11.
- **Note (plan choice):** the selftest's `SetClientSize` uses logical pixels and compares with `physicalClientSize()`, so DPI scaling cannot fail it. `SetClientSize(800, 600)` must differ from the initial `FromDIP(960, 540)` at every scale, which holds.
- **Note (plan choice):** on Windows `--help` and error texts go to the standard handles, which a GUI-subsystem program has only when they are redirected (CTest, pipes). `rawreel-gui --help | Out-Host` shows them; no console is attached.
- **Note (plan choice, U-WIN):** `rawreel_gui` is built on every platform with a stub `nativeWindowOf` outside Windows, so the GUI sources stay portable and are in the Linux compile database for clang-tidy (CI's lint job tidies `src/**/*.cpp`). The test is registered on Windows only. The Linux GUI (former Task 12b: GTK native child, `XInitThreads`, `GDK_BACKEND=x11`, the `DISPLAY` check, Linux selftests, the R4 fallback) is v1.x (future.md).
- The R1 fallback (`rawreel::wx` with `$<CONFIG>` selection) is used only if Task 4 recorded it.

---

### Task 12b: `rawreel-gui` on Linux (dropped from v1.0, U-WIN)

Not executed. The user decision U-WIN limits v1.0 to Windows, so `rawreel-gui` on Linux (the `main` with `XInitThreads` and `GDK_BACKEND=x11`, the `DISPLAY` check, the GTK native child window, the GTK/X11 link block, the WSLg and Xvfb selftests, the R4 fallback and the manual WSLg check) moves to v1.x; see the `[M1b->v1.x]` entry in future.md. The original task text stays in `m1b-plan-part3.md`. What the Linux build needs to compile (a plain `main`, the `nativeWindowOf` stub, the hide/show switch of the selftest) is part of Task 12a.

---

### Task 13a: CI wiring

**Goal:** Wire the CI for M1b without adding Linux test jobs (U-WIN). The existing Linux jobs of the reusable workflow and the workspace workflow would break with M1b: bootstrap now needs the window-system and GUI packages, and dvv-platform and dvv-gpu now have display tests that fail without a display under `DVV_REQUIRE_GPU=1`. So those existing jobs install the packages and run their unchanged workflows under Xvfb; no job or matrix entry is added. The new repositories get their `ci.yml`. clang-tidy skips Windows-only sources on Linux. The workspace README gets the apt line. Checked locally with actionlint and the WSL package check; the tidy file lists are exercised by the Task 13b checkpoint (U-PROC-tidy); no Linux test is run locally. Nothing is pushed.

**Files:**
- `rawreel-workspace/.github/workflows/dvv-ci.yml`: full new content below.
- `rawreel-workspace/.github/workflows/workspace.yml`: full new content below.
- `rawreel-workspace/README.md`: the Linux prerequisites line and a display-tests note.
- `dvv-platform/.github/workflows/ci.yml` (create if Task 5 has not; the content must be exactly as below).
- `rawreel/.github/workflows/ci.yml` (create).

**Interfaces:**
- Consumes:
  - Task 4: `bootstrap.sh` preflight (`pkg-config --exists gtk+-3.0 x11 xcb xrandr wayland-client zlib libpng`), wx and the WSI validation layer in the superbuild.
  - Tasks 5-12a: the repositories, CTest labels and the `*_win32.cpp`/`*_msw.cpp` naming of Windows-only sources.
  - Tasks 3/4: any new workspace script tests.
- Produces: the CI definitions that become live at publishing. A sibling repository's CI calls `dvv-ci.yml@main`, so the workflow change must reach rawreel-workspace `main` first (publishing checklist).

**Behaviour** (design §9.2-§9.3, narrowed by U-WIN):
- No new job and no new matrix entry. The existing Linux build jobs (all three presets) and the lint job install `libgtk-3-dev pkgconf libx11-dev libxcb1-dev libxrandr-dev libwayland-dev zlib1g-dev libpng-dev xvfb xauth`, which bootstrap's preflight (Task 4) requires.
- The existing Linux build and test step runs `xvfb-run -a -s "-screen 0 1920x1080x24" cmake --workflow --preset <preset>` with `NO_AT_BRIDGE=1`, because the existing `DVV_REQUIRE_GPU=1` fails display tests that find no display. This keeps the existing jobs green; it adds no test.
- Windows is unchanged. Display and GUI tests skip with 77 there (no GPU).
- The cache key formula is unchanged.
- The lint job's tidy step excludes `*_win32.cpp` and `*_msw.cpp`, which are not in the Linux compile database.
- `workspace.yml`: the existing integration job's Linux leg installs the same list and runs under `xvfb-run` with `NO_AT_BRIDGE=1`. It builds rawreel, including `rawreel-gui` with the stub viewport, but registers no GUI selftest on Linux. The GUI `--selftest` is a CTest only on Windows, where CI has no GPU, so in v1.0 it runs locally on NVIDIA (Tasks 12a and 13b).
- `rawreel/.github/workflows/ci.yml` calls the reusable workflow like every repository, so rawreel gets the existing matrix; no Linux job is defined for it.
- The Linux legs are not run locally (U-WIN). They first run when the repositories are published; a failure there is handled then (decisions: existing Linux jobs are fixed only when they break).
- The tools job lists every `cmake -P` script test in `tests/`. **Note (plan choice):** Tasks 3 and 4 extend existing script files (`test_third_party_gate.cmake`, `test_superbuild_scripts.cmake`, `test_spdx.cmake`), so no step is added unless Step 1 finds a new script file. `workspace.wx_consumer` needs the third-party prefix and runs in the integration job's workspace build.

**Code**

`rawreel-workspace/.github/workflows/dvv-ci.yml` (complete):

```yaml
# Reusable CI for every dvv repository (spec §13.4). A repository calls it with
#   jobs: { ci: { uses: <owner>/rawreel-workspace/.github/workflows/dvv-ci.yml@main,
#                 with: { repo: <repository name> } } }
name: dvv-ci

on:
  workflow_call:
    inputs:
      repo:
        description: Name of the calling repository, e.g. dvv-base
        required: true
        type: string

permissions:
  contents: read

jobs:
  build:
    name: ${{ matrix.name }}
    runs-on: ${{ matrix.os }}
    timeout-minutes: 90
    strategy:
      fail-fast: false
      matrix:
        include:
          - { name: windows-msvc, os: windows-2025, toolchain: msvc, preset: vs2026 }
          - { name: windows-msvc-asan, os: windows-2025, toolchain: msvc, preset: vs2026-asan }
          - { name: linux-clang, os: ubuntu-24.04, toolchain: clang, preset: ninja-clang }
          - { name: linux-clang-asan, os: ubuntu-24.04, toolchain: clang, preset: ninja-clang-asan }
          - { name: linux-clang-tsan, os: ubuntu-24.04, toolchain: clang, preset: ninja-clang-tsan }
    steps:
      - uses: actions/checkout@v5
        with:
          path: ${{ inputs.repo }}
      - uses: actions/checkout@v5
        with:
          repository: ${{ github.repository_owner }}/rawreel-workspace
          path: rawreel-workspace
      - uses: actions/setup-python@v6
        with:
          python-version: "3.13"
      - name: Install tools
        run: python -m pip install -r rawreel-workspace/tools/requirements.txt
      - name: Install Linux compiler
        if: runner.os == 'Linux'
        run: sudo apt-get update && sudo apt-get install -y clang-19 libclang-rt-19-dev libstdc++-14-dev
      # Window-system and GUI packages (spec §13.4): bootstrap builds wxWidgets on GTK 3 and the
      # validation layer with Xlib, XCB and Wayland support; display tests run under Xvfb.
      - name: Install Linux window-system packages
        if: runner.os == 'Linux'
        run: sudo apt-get install -y libgtk-3-dev pkgconf libx11-dev libxcb1-dev libxrandr-dev libwayland-dev zlib1g-dev libpng-dev xvfb xauth
      # Linux runs the GPU tests on Mesa's lavapipe (Vulkan 1.4 from noble-updates; spec §13.4).
      - name: Install and check the Vulkan device (Linux)
        if: runner.os == 'Linux'
        run: |
          sudo apt-get install -y mesa-vulkan-drivers libvulkan1 vulkan-tools
          cmake -P rawreel-workspace/tools/check-vulkan-device.cmake
      - name: Check Visual Studio 2026
        if: runner.os == 'Windows'
        shell: pwsh
        run: |
          $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
          $version = & $vswhere -latest -property catalog_productDisplayVersion
          if (-not $version.StartsWith('18.')) { throw "Visual Studio 2026 (18.x) required, found $version" }
          Write-Host "Visual Studio $version"
      # The key hashes the inputs of bootstrap's own up-to-date stamp, so a restored prefix is not
      # rebuilt. A miss builds all of it; the validation layer and wxWidgets take most of that time.
      - name: Restore third-party
        id: third-party
        uses: actions/cache/restore@v5
        with:
          path: _third_party/${{ matrix.toolchain }}
          key: third-party-${{ runner.os }}-${{ matrix.toolchain }}-${{ hashFiles('rawreel-workspace/deps/**', 'rawreel-workspace/cmake/DvvThirdParty.cmake', 'rawreel-workspace/cmake/DvvSpdx.cmake') }}
      - name: Bootstrap (Windows)
        if: runner.os == 'Windows'
        shell: pwsh
        run: ./rawreel-workspace/bootstrap.ps1 -Toolchain ${{ matrix.toolchain }}
      - name: Bootstrap (Linux)
        if: runner.os == 'Linux'
        run: ./rawreel-workspace/bootstrap.sh --toolchain ${{ matrix.toolchain }}
      # Saved right after bootstrap, so a failing build or test does not throw the third-party build
      # away. When parallel jobs miss together, the first save wins and the others only warn.
      - name: Save third-party
        if: steps.third-party.outputs.cache-hit != 'true'
        uses: actions/cache/save@v5
        with:
          path: _third_party/${{ matrix.toolchain }}
          key: ${{ steps.third-party.outputs.cache-primary-key }}
      # Windows runners have no GPU: the GPU, display and GUI tests exit with 77 and CTest reports
      # them as skipped.
      - name: Build and test (Windows)
        if: runner.os == 'Windows'
        working-directory: ${{ inputs.repo }}
        run: cmake --workflow --preset ${{ matrix.preset }}
      # On Linux the GPU and display tests must run (a missing device or display fails them) with
      # synchronization validation through the third-party prefix's layer, on a virtual X server.
      # NO_AT_BRIDGE keeps GTK from looking for the accessibility bus. TSAN_OPTIONS stays unset: the
      # GPU tests pass their own suppressions through CTest.
      - name: Build and test (Linux)
        if: runner.os == 'Linux'
        working-directory: ${{ inputs.repo }}
        env:
          DVV_REQUIRE_GPU: "1"
          DVV_VK_VALIDATION: sync
          DVV_VK_VALIDATION_REQUIRED: "1"
          NO_AT_BRIDGE: "1"
        run: xvfb-run -a -s "-screen 0 1920x1080x24" cmake --workflow --preset ${{ matrix.preset }}

  lint:
    runs-on: ubuntu-24.04
    timeout-minutes: 60
    steps:
      - uses: actions/checkout@v5
        with:
          path: ${{ inputs.repo }}
          fetch-depth: 0
      - uses: actions/checkout@v5
        with:
          repository: ${{ github.repository_owner }}/rawreel-workspace
          path: rawreel-workspace
      - uses: actions/setup-python@v6
        with:
          python-version: "3.13"
      - name: Install tools
        run: |
          python -m pip install -r rawreel-workspace/tools/requirements.txt
          sudo apt-get update && sudo apt-get install -y clang-19 libstdc++-14-dev
      # As in the build job: bootstrap builds wxWidgets and the validation layer, and the build
      # compiles rawreel-gui against GTK 3.
      - name: Install Linux window-system packages
        run: sudo apt-get install -y libgtk-3-dev pkgconf libx11-dev libxcb1-dev libxrandr-dev libwayland-dev zlib1g-dev libpng-dev xvfb xauth
      - name: Privacy lint
        run: cmake "-DREPO=${{ inputs.repo }}" -DMODE=commits -P rawreel-workspace/tools/privacy-lint.cmake
      - name: Format
        working-directory: ${{ inputs.repo }}
        run: git ls-files -z '*.h' '*.cpp' | xargs -0 -r clang-format --dry-run --Werror
      # The same cache entry as the linux-clang build jobs.
      - name: Restore third-party
        id: third-party
        uses: actions/cache/restore@v5
        with:
          path: _third_party/clang
          key: third-party-${{ runner.os }}-clang-${{ hashFiles('rawreel-workspace/deps/**', 'rawreel-workspace/cmake/DvvThirdParty.cmake', 'rawreel-workspace/cmake/DvvSpdx.cmake') }}
      - name: Bootstrap
        run: ./rawreel-workspace/bootstrap.sh --toolchain clang
      - name: Save third-party
        if: steps.third-party.outputs.cache-hit != 'true'
        uses: actions/cache/save@v5
        with:
          path: _third_party/clang
          key: ${{ steps.third-party.outputs.cache-primary-key }}
      # clang-tidy reads sources that include generated files (e.g. dvv_add_shaders headers), which
      # exist only after a build.
      - name: Build
        working-directory: ${{ inputs.repo }}
        run: |
          cmake --preset ninja-clang
          cmake --build --preset ninja-clang
      - name: Tidy
        working-directory: ${{ inputs.repo }}
        run: |
          # ** reaches src subfolders; * stops at '/' for tests and tools, so tests/compile_fail and
          # tests/package (built separately) stay out. Windows-only sources (*_win32.cpp, *_msw.cpp)
          # are not in the Linux compile database.
          git ls-files -z ':(glob)src/**/*.cpp' ':(glob)tests/*.cpp' ':(glob)tools/*.cpp' \
              ':(exclude,glob)**/*_win32.cpp' ':(exclude,glob)**/*_msw.cpp' |
            xargs -0 -r clang-tidy -p "../_build/${{ inputs.repo }}/ninja-clang" --quiet
```

`rawreel-workspace/.github/workflows/workspace.yml`: keep the header, triggers, permissions and the `tools` job byte-identical, plus any script-test step Step 1 finds. Replace the `integration` job with:

```yaml
  integration:
    if: github.event_name == 'schedule' || github.event_name == 'workflow_dispatch'
    name: integration-${{ matrix.preset }}
    runs-on: ${{ matrix.os }}
    timeout-minutes: 90
    strategy:
      fail-fast: false
      matrix:
        include:
          - { os: windows-2025, toolchain: msvc, preset: vs2026 }
          - { os: ubuntu-24.04, toolchain: clang, preset: ninja-clang }
    steps:
      - uses: actions/checkout@v5
        with:
          path: rawreel-workspace
      - uses: actions/setup-python@v6
        with:
          python-version: "3.13"
      - name: Install tools
        run: python -m pip install -r rawreel-workspace/tools/requirements.txt
      - name: Install Linux compiler
        if: runner.os == 'Linux'
        run: sudo apt-get update && sudo apt-get install -y clang-19 libstdc++-14-dev
      # As in dvv-ci.yml: wxWidgets on GTK 3, the validation layer with window-system support, Xvfb.
      - name: Install Linux window-system packages
        if: runner.os == 'Linux'
        run: sudo apt-get install -y libgtk-3-dev pkgconf libx11-dev libxcb1-dev libxrandr-dev libwayland-dev zlib1g-dev libpng-dev xvfb xauth
      # As in dvv-ci.yml: Linux runs the GPU tests on lavapipe, Windows has no GPU.
      - name: Install and check the Vulkan device (Linux)
        if: runner.os == 'Linux'
        run: |
          sudo apt-get install -y mesa-vulkan-drivers libvulkan1 vulkan-tools
          cmake -P rawreel-workspace/tools/check-vulkan-device.cmake
      # Same key as dvv-ci.yml (caches are per repository, so this one belongs to rawreel-workspace).
      - name: Restore third-party
        id: third-party
        uses: actions/cache/restore@v5
        with:
          path: _third_party/${{ matrix.toolchain }}
          key: third-party-${{ runner.os }}-${{ matrix.toolchain }}-${{ hashFiles('rawreel-workspace/deps/**', 'rawreel-workspace/cmake/DvvThirdParty.cmake', 'rawreel-workspace/cmake/DvvSpdx.cmake') }}
      - name: Bootstrap (Windows)
        if: runner.os == 'Windows'
        shell: pwsh
        run: ./rawreel-workspace/bootstrap.ps1 -Toolchain ${{ matrix.toolchain }}
      - name: Bootstrap (Linux)
        if: runner.os == 'Linux'
        run: ./rawreel-workspace/bootstrap.sh --toolchain ${{ matrix.toolchain }}
      - name: Save third-party
        if: steps.third-party.outputs.cache-hit != 'true'
        uses: actions/cache/save@v5
        with:
          path: _third_party/${{ matrix.toolchain }}
          key: ${{ steps.third-party.outputs.cache-primary-key }}
      - name: Build and test everything (Windows)
        if: runner.os == 'Windows'
        working-directory: rawreel-workspace
        run: cmake --workflow --preset ${{ matrix.preset }}
      # Builds rawreel-gui too; its --selftest is a CTest on Windows only (v1.0 supports Windows).
      - name: Build and test everything (Linux)
        if: runner.os == 'Linux'
        working-directory: rawreel-workspace
        env:
          DVV_REQUIRE_GPU: "1"
          DVV_VK_VALIDATION: sync
          DVV_VK_VALIDATION_REQUIRED: "1"
          NO_AT_BRIDGE: "1"
        run: xvfb-run -a -s "-screen 0 1920x1080x24" cmake --workflow --preset ${{ matrix.preset }}
```

`dvv-platform/.github/workflows/ci.yml` and `rawreel/.github/workflows/ci.yml` (identical except `repo:`):

```yaml
name: ci

on:
  push:
    branches: [main]
  pull_request:
  workflow_dispatch:

permissions:
  contents: read

jobs:
  dvv:
    uses: BarabashkaD/rawreel-workspace/.github/workflows/dvv-ci.yml@main
    with:
      repo: dvv-platform   # rawreel: repo: rawreel
```

(Write `repo: dvv-platform` / `repo: rawreel` without the trailing comment.)

`rawreel-workspace/README.md`, under "Prerequisites", Linux bullet, append:

> For wxWidgets, the validation layer's window-system support and the display tests: `sudo apt-get install -y libgtk-3-dev pkgconf libx11-dev libxcb1-dev libxrandr-dev libwayland-dev zlib1g-dev libpng-dev xvfb xauth` (`bootstrap.sh` checks for them first and prints this line when one is missing).

After the GPU-tests table, add one paragraph: display tests (CTest label `display`; `gui` for `rawreel-gui --selftest`, Windows only) need a display. v1.0 supports and tests Windows only; Linux builds but is not tested locally. CI runs its existing Linux workflows under `xvfb-run -a -s "-screen 0 1920x1080x24"` with `NO_AT_BRIDGE=1`. `DVV_REQUIRE_GPU=1` also turns a missing display into a failure.

**Tests first:** the checks of Steps 2-4 fail on the old files. The tidy command without the exclusions would fail on `dvv-platform`'s `*_win32.cpp` (Windows headers on Linux); the Task 13b checkpoint runs it with the exclusions. The package check reports `xvfb` missing when it is not installed locally (expected, D5).

**Steps** (from the workspace root; WSL only for the package check in Step 3; git only in PowerShell). Task 13a is not a checkpoint: it changes no C++ source, and its YAML Format and Tidy commands are exercised by the Task 13b final checkpoint (U-PROC, U-PROC-tidy):
- [ ] **Step 1: Inventory the script tests:** `Get-ChildItem rawreel-workspace/tests -Recurse -Filter 'test_*.cmake' | ForEach-Object Name`. Compare the result with the `tools` job steps. Add a step (same pattern as the others) for any script that has none, and record it.
- [ ] **Step 2: Write the five files.** Then check the workflows: `_build/tools/actionlint/bin/actionlint.exe rawreel-workspace/.github/workflows/dvv-ci.yml rawreel-workspace/.github/workflows/workspace.yml dvv-platform/.github/workflows/ci.yml rawreel/.github/workflows/ci.yml; "exit $LASTEXITCODE"`. Expected: no output, `exit 0`. If the exe is missing, use the M1a venv recipe.
- [ ] **Step 3: The Linux package check** (no Linux build or test run here, U-WIN, U-PROC-tidy): `wsl -d Ubuntu-24.04 -e bash -lc 'dpkg-query -W -f="\${Status} \${Package}\n" libgtk-3-dev pkgconf libx11-dev libxcb1-dev libxrandr-dev libwayland-dev zlib1g-dev libpng-dev xvfb xauth 2>&1'`. Expected: `install ok installed` for each build package. `xvfb` and `xauth` are only needed by the CI test legs; record them if missing (D5), nothing else. The YAML's Format and Tidy commands (with their `*_win32.cpp`/`*_msw.cpp` exclusions) run on `dvv-platform`, `dvv-gpu` and `rawreel` in the Task 13b checkpoint, whose tidy command uses the same file lists.
- [ ] **Step 4: Windows is unchanged** (Per-task checks, Windows Debug): `$env:DVV_REQUIRE_GPU='1'; $env:DVV_VK_VALIDATION='sync'; $env:DVV_VK_VALIDATION_REQUIRED='1'; Push-Location rawreel; cmake --preset vs2026 | Out-Null; cmake --build --preset vs2026; ctest --preset vs2026 --output-on-failure 2>&1 | Select-String 'tests passed|tests failed|Skipped|error'; Pop-Location`. Expected: `100% tests passed`, 3 entries, nothing skipped. No C++ file changes, so the clang-format check has nothing to check.
- [ ] **Step 5: Privacy check of the new text:** run the privacy lint in tree mode on `rawreel-workspace`, `dvv-platform` and `rawreel` (`cmake -DREPO=<repo> -DMODE=tree "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake`). Expected: clean.
- [ ] **Step 6: Commit** (three repositories):
  ```powershell
  git -C rawreel-workspace add -A
  git -C rawreel-workspace commit -m "ci: install the window-system packages and run the Linux jobs under Xvfb" -m "The existing Linux jobs of dvv-ci.yml and the nightly integration job install GTK 3, X11/XCB/Wayland headers and Xvfb, which bootstrap and the display tests now need, and run their workflows under xvfb-run with NO_AT_BRIDGE=1; no job is added. Tidy skips Windows-only sources. README lists the apt packages. Takes effect when published (workspace main first)." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  git -C dvv-platform add -A
  git -C dvv-platform commit -m "ci: call the reusable dvv workflow" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  git -C rawreel add -A
  git -C rawreel commit -m "ci: call the reusable dvv workflow" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```
  Skip the dvv-platform commit if Task 5 already committed an identical file (`git -C dvv-platform status --short` is empty).
- [ ] **Step 7: Privacy lint in commits mode** (Per-task checks) on the three repositories: `foreach ($r in 'rawreel-workspace','dvv-platform','rawreel') { cmake "-DREPO=$r" -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake }`. Nothing is pushed.

**Expected results:** actionlint reports nothing (`exit 0`). The build packages are installed in WSL. Windows Debug is unchanged (3 entries pass in `rawreel`). Privacy lint clean. No WSL build, no clang-tidy and no CI Linux test leg run in this task; the tidy file lists are checked at the Task 13b checkpoint.

**Commit:** `ci: install the window-system packages and run the Linux jobs under Xvfb` (rawreel-workspace), `ci: call the reusable dvv workflow` (dvv-platform if needed, rawreel), each with the trailer.

**Notes:**
- Design references: §9.2, §9.3, §13 of the spec (§13.4), D11, D12, R5, R12, R14.
- **Note (plan choice):** the tidy exclusion relies on the file-name convention `*_win32.cpp` (dvv-platform, dvv-gpu) and `*_msw.cpp` (rawreel) for Windows-only sources. Windows-only files are therefore not tidied (M1a ran tidy on Linux only as well); clang-format still covers them.
- **Note (plan choice, U-WIN):** the Xvfb wrapper and the package install keep the existing Linux jobs working after M1b; they are maintenance of existing jobs, not new Linux tests. A Linux failure at the first published run (R5: Xvfb with lavapipe) is handled then; its fallbacks need the user's decision.
- Publishing order (§13.6): push the workspace `main` (with this `dvv-ci.yml`) before the
  repositories that call it. Only with the user's explicit go-ahead.

---

### Task 13b: Spec, READMEs and final verification

**Goal:** Bring the spec and every README in line with the M1b code and the U-WIN decision, update the progress ledger, and run the final verification: the Windows matrix (vs2026, vs2026-asan) on every repository with the GPU, display and GUI tests running (not skipped) on NVIDIA, the workspace workflow on Windows, the lint, the privacy lint, and one Linux sanity build (configure + build, no tests).

**Files:**
- `rawreel-workspace/docs/superpowers/specs/2026-10-19-rawreel-design.md`: the sections listed below.
- `rawreel-workspace/README.md`: the repositories table and layout (`dvv-platform`, `rawreel`), the M1b status line, a short "rawreel-gui" paragraph.
- `dvv-platform/README.md`: complete it (Task 5 created it).
- `dvv-gpu/README.md`: a "Presentation" section.
- `rawreel/README.md`: how to run it, `--selftest`, exit codes.
- `rawreel-workspace/.git/sdd/progress.md`: the ledger (not committed; inside `.git`).

**Interfaces:**
- Consumes: the results of Tasks 0-13a (Task 12b was dropped, U-WIN). Their "Changes during execution" entries in the plan, the R7 number (Task 10) and the Task 4 timings.
- Produces: the M1b completion state.

**Behaviour: spec updates** (design §10 list; each edit states the M1b fact, short, in the spec's style):
- **Supported platforms** (wherever the spec states them): v1.0 supports Windows only; the code stays portable and Linux builds, but Linux support (tests, sanitizers, the GUI) follows in v1.x (user decision U-WIN).
- **§2** GPU row: the loader comes through dvv-platform's `DynamicLibrary` (`vulkan-1.dll` from System32 only; `libvulkan.so.1`), not `vk::detail::DynamicLoader`.
- **§3.1:** dvv-platform contents: `NativeWindow`, `createSurface`, `surfaceInstanceExtensions`, `DynamicLibrary`, test-only `TestWindow` in `dvv::platform_testing`. `IAsyncFileReader` arrives in M2. The dependency graph of design §3.
- **§4:** the presenter handshake per D4 (fence acquire, host wait, semaphore-less present, rendering through the submit thread), D7 (old swapchain destroyed after `vkQueueWaitIdle`, replacing "old images retire through the context") and D8 (`currentExtent` authoritative; `resize` is a hint). The sentence "destroying a presenter blocks the GUI for about one frame plus a queue-idle wait; on Windows the wait dispatches sent messages only".
- **§9.4:** `Presenter` is managed (authority `ExecutionContext::createPresenter`, `unique_ptr`). `DynamicLibrary` joins the handle list; `TestWindow` is an exempt test helper.
- **§10.2:** the M1b `--selftest` (no URI; the script of design §5.6), Windows only in v1.0, and the exit codes 0/1/4/6/77. The 10-frame `--selftest <uri>` remains the M3 form.
- **§10.4:** `NativeWindow` + `createSurface` replace `ISurfaceFactory`; `IDynamicLibrary` becomes `DynamicLibrary` (a handle). The Linux X11 recipe is not exercised in v1.0: `rawreel-gui` builds on Linux with a stub native window, and the GTK native child is v1.x (U-WIN).
- **§11:** the presenter errors from design §7, as a short table or list.
- **§12:** the `display` and `gui` CTest labels, `TestWindow`-based presentation tests, `DVV_REQUIRE_GPU=1` also covering a missing display, the GUI smoke test not built under sanitizers and registered on Windows only. v1.0 tests on Windows only (NVIDIA locally); Linux gets one sanity build (no tests) per milestone; the existing CI Linux jobs run under Xvfb.
- **§13.2:** the superbuild's `CONFIGS` option, wxWidgets 3.3.4 (static; MSVC Debug+Release; GTK3 on Linux; the disabled parts), `libpng-2.0` in the allowlist, the validation layer with WSI.
- **§13.4:** the apt list, `xvfb-run` and `NO_AT_BRIDGE` for the existing Linux jobs, no new Linux jobs in v1.0, the GUI not built under sanitizers, and the GUI `--selftest` running locally on NVIDIA (CI's Windows runners have no GPU).
- **§15:** M1b marked done with its demo (on Windows). In "Deferred from M1a", remove the three M1b leftovers (vulkan-1.dll from System32, the VVL WSI note, `createComponent` / env API if listed). Add the M1b deferrals: design §12 F1-F14 in one line each, with their target milestone, and the U-WIN deferrals to v1.x (rawreel-gui on Linux, the Linux tests and sanitizers of M1b).
- **§16:** the wx and WSI risks as they stand after M1b: R1 resolution, R4 not exercised (v1.x), R5 open until the first published CI run, R7 measurement (NVIDIA), R8, R9 deferred with Linux; the accepted U-WIN risk (no TSan on Windows).
- **§17:** D1-D17 in short form, the user's `libpng-2.0` allowlist decision (static linking, M3 PNG exporter) and U-WIN.

**READMEs:**
- `dvv-platform/README.md`:
  - purpose and dependencies;
  - the API summary of design §4.3, one short paragraph per header;
  - `LibrarySearch::SystemDirectory`;
  - the `TestWindow` target for tests;
  - the `surface` display test (`display` label, exit 77 rules, `DVV_REQUIRE_GPU`).
- `dvv-gpu/README.md` "Presentation":
  - opt-in (`GpuRegistryOptions::windowSystem`, `ContextOptions::presentation`, `OptionalFeatures::swapchain`);
  - `createPresenter` with a 10-line example (provider returning `PresentFrame`);
  - threading rules (any-thread `resize`/`setVisible`/`requestFrame`; provider and `onError` on the presenter thread);
  - one frame in flight;
  - destroy before the window;
  - `dvv_gpu_present_tests` (`gpu;display`).
- `rawreel/README.md`:
  - what `rawreel-gui` shows in M1b;
  - how to build and run it on Windows (`cmake --workflow --preset vs2026`; the path of the executable under `_build/rawreel/<preset>/<config>/`);
  - `--selftest` and the exit codes (copy `USAGE`);
  - platforms: Windows only in v1.0; Linux builds, but its viewport has no native window yet (v1.x);
  - not built under sanitizers.
- `rawreel-workspace/README.md`:
  - update the wording of the `dvv-platform` row and layout line added in Task 5 (M1b: native windows, Vulkan surfaces, dynamic libraries); add the `rawreel` row and layout line (M1b: `rawreel-gui` with a Vulkan viewport and test pattern);
  - the status line "M0, M1a and M1b are done (v1.0 targets Windows); M2 adds media (dvv-media)".

**Ledger** (`rawreel-workspace/.git/sdd/progress.md`): append an M1b section:
- the commit heads per repository;
- the measured numbers (R7 present time under the queue lock on NVIDIA, Task 4 bootstrap times and cache size);
- the risks that fired and how they were handled (R1, R5, R6, R11), and those deferred with Linux (R4, R9);
- the Linux sanity build result, including any logged toolchain or third-party failure;
- the result of the Windows manual exit-criterion check (Task 12a Step 6);
- the items deferred to M2/M3 (design F1-F14 and anything recorded during execution) and to v1.x (U-WIN).

**Tests first:** this task adds no code. Its "test" is the final matrix and the doc checks below, which must be red-free.

**Checkpoint task (U-PROC):** Task 13b is the milestone's final checkpoint and covers everything: every repository, clang-tidy on all sources, Release and ASan everywhere. M1b has no ×10 concurrency repeats.

**Steps** (from the workspace root; WSL only for the checkpoint builds and the Linux sanity build in Step 5; git only in PowerShell, except the read-only `git ls-files` inside the WSL commands):
- [ ] **Step 1: Collect the facts:** read the plan's "Changes during execution", the commit messages of Tasks 0-13a (`git -C <repo> log --oneline m1a..m1b` for the four old repositories, `git -C <repo> log --oneline` for the two new ones), and the R7/Task 4 numbers.
- [ ] **Step 2: Spec edits** (list above). Keep lines at or under 100 columns where the section already does, and do not reflow untouched text. Check: `Select-String -Path rawreel-workspace/docs/superpowers/specs/2026-10-19-rawreel-design.md -Pattern 'ISurfaceFactory|IDynamicLibrary|DynamicLoader'`. Expected: only historical mentions that say what replaced them.
- [ ] **Step 3: READMEs** (four files). Check the commands they show by running them: `rawreel-gui --help`, and the paths shown exist after the Step 5 builds.
- [ ] **Step 4: Commit the docs:**
  ```powershell
  git -C rawreel-workspace add -A; git -C rawreel-workspace commit -m "docs: align the spec and the README with M1b" -m "Spec §2, §3.1, §4, §9.4, §10.2, §10.4, §11, §12, §13.2, §13.4, §15, §16, §17: platform layer, presenter handshake, rawreel-gui --selftest and exit codes, wxWidgets and the WSI layer, Windows-only v1.0 (U-WIN), M1b decisions and deferrals." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  git -C dvv-platform add -A; git -C dvv-platform commit -m "docs: describe the platform layer in the README" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  git -C dvv-gpu add -A; git -C dvv-gpu commit -m "docs: describe presentation in the README" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  git -C rawreel add -A; git -C rawreel commit -m "docs: describe rawreel-gui, --selftest and its exit codes" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```
- [ ] **Step 5: Per-task checks and the final checkpoint checks** (Global Constraints). Each command uses `run_in_background: true`; the Windows presets and the WSL builds run in parallel; one repository per call if needed. Never run two Windows package tests at the same time.
  - Windows Debug (Per-task checks) and Release (checkpoint), then ASan (checkpoint), for each of `dvv-base dvv-registry dvv-platform dvv-gpu rawreel`: `$env:DVV_REQUIRE_GPU='1'; $env:DVV_VK_VALIDATION='sync'; $env:DVV_VK_VALIDATION_REQUIRED='1'; Push-Location <repo>; cmake --workflow --preset vs2026 2>&1 | Select-String 'tests passed|tests failed|Skipped|error'; cmake --workflow --preset vs2026-asan 2>&1 | Select-String 'tests passed|tests failed|Skipped|AddressSanitizer'; Pop-Location`. A report with a dvv frame is fixed; one entirely inside the NVIDIA driver or the Vulkan loader is logged and reported, not suppressed.
  - The workspace workflow on Windows, once for the group: `$env:DVV_REQUIRE_GPU='1'; $env:DVV_VK_VALIDATION='sync'; Push-Location rawreel-workspace; cmake --workflow --preset vs2026 2>&1 | Select-String 'tests passed|tests failed|Skipped|error'; Pop-Location`.
  - Workspace script tests (Windows): `Push-Location rawreel-workspace; cmake -P tests/spdx/test_spdx.cmake; cmake -P tests/spdx/test_third_party_gate.cmake; cmake -P tests/deps/test_versions.cmake; cmake "-DWORK_DIR=$env:TEMP/superbuild-scripts-test" -P tests/deps/test_superbuild_scripts.cmake; Pop-Location`.
  - The Linux sanity build, once: the workspace `ninja-clang` configure and build, which builds every repository, with no tests: `wsl -d Ubuntu-24.04 -e bash -lc '. _build/tools/linux/env.sh && cd rawreel-workspace && { cmake --preset ninja-clang && cmake --build --preset ninja-clang; } >/tmp/m1b-sanity.log 2>&1; rc=$?; tail -n 20 /tmp/m1b-sanity.log; echo "exit=$rc"'`. Expected: `exit=0`. Our portability bugs are fixed (Step 6); a toolchain or third-party failure is logged in the ledger and reported, not fixed.
  - Checkpoint clang-tidy on all sources of each C++ repository (`dvv-base dvv-registry dvv-platform dvv-gpu rawreel`), after its `ninja-clang` configure + build with no tests (part of the Linux sanity build), with the full clang-format and the file lists of the CI Format and Tidy steps (Task 13a): `wsl -d Ubuntu-24.04 -e bash -lc 'set -o pipefail && . _build/tools/linux/env.sh && cd <repo> && cmake --preset ninja-clang >/dev/null && cmake --build --preset ninja-clang 2>&1 | tail -n 30 && git ls-files -z "*.h" "*.cpp" | xargs -0 -r -P "$(nproc)" -n 8 clang-format --dry-run --Werror && git ls-files -z ":(glob)src/**/*.cpp" ":(glob)tests/*.cpp" ":(glob)tools/*.cpp" ":(exclude,glob)**/*_win32.cpp" ":(exclude,glob)**/*_msw.cpp" | xargs -0 -r -P "$(nproc)" -n 1 clang-tidy -p ../_build/<repo>/ninja-clang --quiet && echo LINT-OK'`. Our portability bugs are fixed; a toolchain or third-party failure is logged in the ledger and reported.
  - Privacy lint, commits mode, on all six repositories: `foreach ($r in 'rawreel-workspace','dvv-base','dvv-registry','dvv-platform','dvv-gpu','rawreel') { cmake "-DREPO=$r" -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake }`.
- [ ] **Step 6: Fix and repeat.** A failure is fixed in the repository it belongs to, with a `fix:` commit and the trailer. The affected repository's Windows runs (Debug, Release, ASan) and its checkpoint lint are then repeated, plus the workspace workflow if the fix touches more than one repository, and the Linux sanity build if the fix was for it.
- [ ] **Step 7: Ledger.** Update `rawreel-workspace/.git/sdd/progress.md` (not tracked). Then check that all six repositories are on `m1b` with a clean tree: `foreach ($r in 'rawreel-workspace','dvv-base','dvv-registry','dvv-platform','dvv-gpu','rawreel') { git -C $r status --short --branch }`. Expected `## m1b` and no other line. Nothing is pushed.

**Expected results:**
- Every repository: vs2026 Debug (Per-task checks), vs2026 Release and vs2026-asan (final checkpoint) report `100% tests passed`.
- No GPU, display or GUI test is skipped on NVIDIA. The validation count is 0 everywhere under `sync`. No AddressSanitizer report.
- `rawreel.gui_selftest` runs in vs2026 (not in vs2026-asan).
- The workspace workflow passes on Windows. The Linux sanity build compiles every repository, or its toolchain/third-party failure is logged. LINT-OK for every C++ repository. Privacy lint clean on all six. All trees clean on `m1b`.

**Commit:** the four `docs: …` commits of Step 4 and any `fix: …` from Step 6, each with the trailer.

**Notes:**
- Design references: §10 spec-update list, §11 risks (for §16), §12 future (for §15), decisions log D4 (`libpng-2.0`), U-WIN.
- Publishing (creating GitHub repositories, pushes) is not part of M1b. It needs the user's
  explicit go-ahead at that time.
- **U-WIN:** no WSL test runs, no Linux ASan/TSan, no Xvfb runs. The Linux tests, Linux sanitizers and `rawreel-gui` on Linux are v1.x (future.md).

---

## Arbiter fixes applied

- F1 -> Task 9: `presenter_core.cpp` `waitForWork` (after `WaitAndRetry`, a 10 ms wait woken only by stop, then `retryElapsed()`), the `step()` comment, and the `retryElapsed` check after the Step 5 build.
- F2 -> Task 9 Interfaces: `PresenterLogic` contract rules 1-6 replaced; the "another name" sentence for the blit hook deleted.
- F3 -> Task 4: the VVL `BUILD_WSI_*` arguments are explicit `ON` (Files bullet, VVL comment, Step 2).
- F4 -> Task 13a (Consumes, Behaviour bullet 1, the three YAML `apt-get install` lines, the README line, Step 3.1) and Task 13b Notes: `libx11-xcb-dev` and `x11-xcb` removed.
- F5 -> Task 10 Step 4 (commit the `src/` fixes before the R7 instrumentation) and Step 7.
- F6 -> Task 9 Step 6 and Notes/Risks: an R6 hazard is escalated, `submit` is not changed.
- F7 -> Task 4 Step 4; Task 10 Steps 3, 6 and 8; Task 11 Steps 7 and 9; Task 12a Steps 7 and 9; Task 12b Step 7: the referenced commands are inlined.
- F8 -> Global Constraints (Shells), part 1 Conventions and the step preambles of Tasks 9-13b (Task 11 has the same preamble and got the same exception): read-only `git ls-files` allowed in the WSL lint commands. Canonical lint in Task 9 Step 9, Task 10 Step 6, Task 11 Step 7, Task 12a Step 7, Task 12b Step 7 and Task 13b Step 5; Task 13a `dvv-ci.yml` Tidy step uses `src/**/*.cpp` with the updated comment.
- F9 -> Task 13a Step 2 (actionlint instead of the PyYAML parse) and its Expected results.
- F10 -> Task 5 tests 7 and 8: `GetModuleHandleW` with `DVV_PLATFORM_TEST_MODULE_NAME` (base name).
- F11 -> Task 9: `PresenterCore::formatFeatures` and `format_features_` removed; `checkImage` and `initialise` use `DeviceCore::formatFeatures` with the `VK_FORMAT_FEATURE_2_*` bits; Consumes (Task 8 line) updated.
- F12 -> Task 9 `thread_wait_win32.cpp`: `MsgWaitForMultipleObjectsEx(..., MWMO_INPUTAVAILABLE)`.
- F13 -> Task 13a `dvv-ci.yml`, `build` job: `timeout-minutes: 90`.
- F14 -> Task 13b READMEs, `rawreel-workspace/README.md`: update the `dvv-platform` row and layout line from Task 5, add the `rawreel` row and layout line.

## U-WIN changes

User decision U-WIN (decisions.md, 2026-10-23): v1.0 supports Windows only; the code stays portable; nothing is tested on Linux. Applied to the remaining tasks; Tasks 0-9 are unchanged.
- Header: Goal says "on Windows"; a new **Exit criterion (U-WIN)** line (Windows NVIDIA GUI check, Windows matrix, lint, privacy lint, a compiling Linux sanity build, no Linux tests).
- Global Constraints: new Platform bullet (portable code, `*_win32.cpp`/`*_msw.cpp`/`#if defined(_WIN32)`, Linux-only code compiled not run); Toolchains keep only `ninja-clang` for the lint build and the sanity build; new bullets for the per-task full matrix (vs2026, vs2026-asan on NVIDIA, clang-format, clang-tidy, privacy lint), the lint build, the Linux sanity build and the accepted TSan risk; the WSLg/xvfb shell note and the lavapipe test line are removed; Long runs: the Windows presets and the WSL lint build may run in parallel.
- Task 10: Windows only. The WSL/Xvfb step, the Linux ASan/TSan runs, the R9 suppressions (`tests/tsan.supp`, `tests/lsan.supp` and their Files line) and the lavapipe R7 number are removed and moved to v1.x (future.md). Case 3 keeps its Xlib branch in the code (compiled by the lint build, not run); case 8 runs under vs2026-asan. Steps renumbered 1-7 (ASan is Step 4, lint with its build is Step 5); the R5 note is replaced by the U-WIN note.
- Task 11: "used by Task 12a" (was 12a/12b); `tests/tsan.supp` kept as a portable build file. Step 6 is only the workspace configure (the WSL workflow run is removed); Step 7 lint includes the lint build; Step 9 matrix is Windows + lint + privacy lint; Expected results without ASan/TSan on Linux and without lavapipe.
- Task 12a: `rawreel_gui` is built on every platform outside sanitizer builds; outside Windows with a plain `main` (`#else` branch of `main.cpp`) and the new `src/native_window_stub.cpp` (`std::monostate`), so the GUI sources stay portable and are in the Linux compile database for clang-tidy. `rawreel.gui_selftest` is registered with `if(TARGET rawreel_gui AND WIN32)`. The selftest hide/show switch is an `#if defined(_WIN32)` in 12a. Step 7 is the lint build, which is also this task's Linux sanity build (it changes Linux build files); the WSL workflow run is removed. Step 9 matrix is Windows + lint + privacy lint. References to Task 12b removed.
- Task 12b: dropped from v1.0; its content is one `[M1b->v1.x]` line in future.md (original text in `m1b-plan-part3.md`).
- Task 13a: no Linux test job is added. The existing Linux jobs of `dvv-ci.yml` and `workspace.yml` keep the package install and the `xvfb-run` wrapper only so that they do not break (bootstrap preflight, display tests under `DVV_REQUIRE_GPU=1`); the YAML is otherwise unchanged from the arbitrated version, except the workspace Linux comment (no GUI selftest on Linux). rawreel's `ci.yml` calls the reusable workflow like every repository. Step 3 keeps the package check and the verbatim lint commands (with the lint build) and drops the local Linux build-and-test run; the Linux CI legs are first exercised at publishing. Consumes "Tasks 5-12a"; commit message reworded.
- Task 13b: final verification on Windows (vs2026 and vs2026-asan for every repository, the workspace workflow on Windows, script tests), the lint for every C++ repository, the privacy lint, and one Linux sanity build (the workspace `ninja-clang` configure + build, no tests). The WSL/Xvfb test loops are removed. The spec and README edits state the Windows-only v1.0, the stub Linux GUI and the U-WIN deferrals; the ledger records the sanity build result and the Task 12a manual check.
- future.md: two `[M1b->v1.x]` lines (reason: U-WIN): rawreel-gui on Linux (former Task 12b) and the Linux parts of Task 10.

## U-PROC changes

User decisions U-PROC and U-PROC-tidy (decisions.md, 2026-10-23), applied to Tasks 10-13b; Tasks 0-9 are unchanged.
- Global Constraints: the per-task full matrix, the lint build and the per-task Linux sanity build are replaced by "Per-task checks" (vs2026 Debug build + all tests on NVIDIA, clang-format on the changed files, privacy lint), a "Checkpoints" list (Tasks 10, 12a, 13b) and "Checkpoint checks" (vs2026 Release, vs2026-asan with the ×10 repeats, clang-tidy on the files changed since the last checkpoint after one WSL `ninja-clang` configure + build that is also the Linux sanity build). Toolchains, Shells (read-only `git diff --name-only` allowed in WSL), Long runs and the part 1 Conventions follow.
- Task 10 (checkpoint, chosen because its goal is the ASan pass): the ASan step and the lint step are removed; commit is Step 4, Per-task checks Step 5, Checkpoint checks Step 6.
- Task 11: the lint step is removed; the matrix step becomes the Per-task checks (Step 8). Release, ASan and clang-tidy move to the Task 12a checkpoint.
- Task 12a (checkpoint, GUI lifecycle): Debug only in Steps 2 and 4; the per-task lint/sanity build is removed; Step 8 Per-task checks, Step 9 Checkpoint checks (Release with the selftest, ASan, the `ninja-clang` build that links `rawreel-gui` on Linux, clang-tidy on all rawreel sources).
- Task 13a: the per-task WSL lint builds are removed (the package check stays); Windows Debug and the privacy lint are its checks; the YAML tidy file lists are exercised at the Task 13b checkpoint.
- Task 13b (final checkpoint): Debug and Release, ASan, the full lint with its `ninja-clang` builds, the workspace Linux sanity build, script tests and the privacy lint on every repository.
- Expected results of Tasks 10-13b name the Per-task and Checkpoint results separately.
- **Review fixes (uwin-uproc-review.md, controller decision):** in Task 13b Step 5, the Linux sanity build writes the configure and the build into one fresh log, `{ cmake --preset ninja-clang && cmake --build --preset ninja-clang; } >/tmp/m1b-sanity.log 2>&1`. A failed configure can no longer show a stale build log.

## Changes during execution

Recorded by Task 13b (2026-10-23) from the task reports, arbiter files and the progress ledger.

- Task 1: `WIDE_SAMPLE` is written with `\u`/`\U` escapes; fix 185623a rejects over-long environment
  values and clears OS-only variables on Windows.
- Task 3: the brief's predicted red message for `test_third_party_gate` was wrong (unknown words fold
  into `LICENSE_FILES`).
- Task 4: `run-wx-consumer.cmake` writes `CMAKE_PREFIX_PATH` as a bracket argument (Windows
  backslashes). Fix 60896d5: `wxUSE_WAYLAND=OFF` on Linux (wx compiled MIT wayland-scanner output).
  The licence gate also needed `IJG` and `X11` (wxcore's colour quantizer and XPM decoder; user
  decision U1, decisions log D6), and `wxUSE_APPLE_IEEE=OFF`. Timings: MSVC bootstrap 4 min 36 s,
  prefix 1,262 MB (wx libraries 697 MB); Linux bootstrap 14 min 39 s, prefix 954 MB (wx 43 MB); a
  wx-only Linux rebuild 254 s. R1: one MSVC prefix holds Debug and Release (no `wxWidgets-debug`
  fallback). R11: `<wx/wx.h>` first compiles cleanly under `/W4 /WX`. R3 was not proven by the
  consumer (no window code); the rawreel-gui links on Windows (Task 12a) and in the Linux sanity
  builds needed no extra system library. The LNK4099 check is effective only when the wx build tree
  is absent (cached CI prefix).
- Task 5: `GetModuleHandleW(<base name>)` checks unloading; fix 2ba3ce4 softens the
  SystemDirectory claim (an already-loaded module of that name is returned) and tests the invalid
  UTF-8 name.
- Task 6: `TestWindow::State` owns the handles; `windowSystemOf` uses `std::holds_alternative`; fix
  80f2eda: the test window has no `WS_SYSMENU`, so 97x61 clients are possible on Win32.
- Task 7: the `unset(DVV_BUILD_TESTS)` is guarded by `PROJECT_IS_TOP_LEVEL` in dvv-platform
  (3479252), dvv-registry (6472093), dvv-gpu (2c1c7b3) and the plan templates; the dvv-gpu README
  got a short Presentation section already in Task 7.
- Task 8: `frameStarted` is guarded in Release; fix f98132a refuses non-blittable sources in
  `presentBlit` (`presentBlitSource`) and pins the logic (`OUT_OF_DATE_LIMIT == 4`).
- Task 9: `checkImage` rejects what `presentBlit` rejects (device, generation, usage, format);
  2c07f7e notifies the mailbox under its mutex (a real TSan race in our code); create-info structs
  are fully initialised (Clang's `-Wmissing-designated-field-initializers`); fix 55dfc98 refuses
  non-UNORM/SRGB swapchain formats, hardens stop (`std::terminate` instead of `assert`) and documents
  the R15 thread rules. R6 did not fire: no synchronization-validation hazard on NVIDIA.
- Task 10: R7 measured on NVIDIA with sync validation: the steady-state maximum time under the queue
  lock per run was usually 0.14-0.86 ms in case 8 (about 97 presents per presenter), with single
  outliers of 1.2, 1.3 and 2.4 ms; case 2 (presents right after resizes) 0.29-2.06 ms; without
  validation 0.14-1.0 ms. The fallback (more images) was not applied; the instrumentation was not
  committed. The Linux parts moved to v1.x (U-WIN).
- Task 11: `TestPatternRenderer` uses a private passkey; `create(nullptr)` is `Internal`.
- Task 12a: `<wx/wx.h>` is kept first with `// clang-format off/on`; the selftest closes through
  `CallAfter`; `~VulkanViewport` moves the presenter out before destroying it (re-entrancy during
  `~Presenter`); `--selftest` logs through `wxLogStderr`. Fix 2b686b0: an early close fails the
  selftest with 6. 7876bbe: the tidy fix in Task 11's GPU test. Manual exit-criterion check: the user
  ran `rawreel-gui` (Debug, sync validation) on 2026-10-23, saw the pattern, resized and closed it:
  OK, no VUID, error or leak lines; the automated runs covered maximise, minimise/restore and 10
  closes of 157-208 ms.
- Task 12b: dropped from v1.0 (U-WIN); one `[M1b->v1.x]` line in future.md.
- Task 13a: dvv-platform needed no CI commit (Task 5's `ci.yml` was already final). Every CI run
  fails at bootstrap until all repositories in `repos.txt` exist on GitHub (controller decision from
  the Task 13a review: a publishing-checklist item, no YAML change).
- Task 13b: the spec and four READMEs were aligned (rawreel-workspace 0ad6748, dvv-platform 5a11502,
  dvv-gpu fcac319, rawreel 6302259); the final checkpoint was green with no fix commit (results in
  the progress ledger).

### Minor items logged during M1b (for the M2 planning)

Sources: the arbiter files `sdd-m1b/task-<n>-arbiter.md`, `task-10-review.md`, `task-11-review.md`
and the progress ledger. Target in brackets where one was set.

- Task 1 (dvv-base env API): the report's claim that `WIDE_SAMPLE` is fully escaped is wrong (only
  the emoji is; builds fine with `/utf-8`); case 6 passes silently if the CTest `DVV_LEAK_CHECK`
  property is lost (`REQUIRE` it when set); on Windows a variable set to an empty value from outside
  the process reads back as unset.
- Task 2 (dvv-registry): README example line 102-104 columns; static `create(caps)` returns a bare
  `unique_ptr` while `createComponent<I>` returns `Result` (M2 API review).
- Task 3 (superbuild `CONFIGS`): duplicates, reverse order (`Release Debug`) and lists without
  Release are accepted (a reversed order leaves Debug files under Release names); a bare `CONFIGS`
  silently means Release; the per-config command loop has no automated test; a Debug declaration
  applicable to Linux is rejected only at the Linux superbuild configure; a misspelt keyword after
  `LICENSE_FILES` is swallowed into that list (M2: validate keywords); the gate case
  `CONFIGS Debug Release` passed before the change.
- Task 4 (wx): absolute build paths (`__FILE__`, `wxINSTALL_PREFIX`) end up in the static wx
  libraries (M4, before the first binary release: prefix maps in `deps/CMakeLists.txt`
  `common_args`, decide on `wxINSTALL_PREFIX`); the consumer link does not exercise window code, so
  R3 was proven only by the rawreel-gui links (Windows Task 12a, Linux sanity build); the LNK4099
  check is effective only when the wx build tree is absent (cached CI prefix); unused wx libraries
  are built (html, ribbon, qa, net; about +0.7 GB on MSVC) and the exported `wx::wxqa` links a
  missing `wxxml` (when R14, the CI cache size, bites: `wxUSE_DEBUGREPORT=OFF`, `wxUSE_RIBBON=OFF`
  after a design check); three sibling `run-package-test.cmake` scripts keep the quoted
  `CMAKE_PREFIX_PATH` pattern (use a bracket argument); `workspace.wx_consumer` silently disappears
  when the prefix has no wx (assert it with `ctest -N`); the `bootstrap.sh` preflight failure path
  is untested.
- Task 5 (`DynamicLibrary`): move assignment releasing the target's library and `keep_loaded_`
  travelling with a move are untested; the test binary embeds the absolute path of the test module
  (build output only).
- Task 6 (surfaces, `TestWindow`): nothing is checked after `minimise()` on Linux; Xlib `create`
  does not poll the size or set `WM_NORMAL_HINTS`; `XStoreName` garbles UTF-8 titles
  (`Xutf8SetWMProperties`); `SW_MINIMIZE`/`SW_RESTORE` change the active window on a desktop
  (`SW_SHOWMINNOACTIVE`/`SW_SHOWNOACTIVATE`); X11 headers before the dvv headers (M2:
  `IncludeIsMainSourceRegex` for `_win32|_xlib`); the `CMAKE_DISABLE_FIND_PACKAGE_Vulkan`
  save/restore is a no-op in dvv-platform and dvv-gpu (drop it in both); the Win32 `createSurface`
  test covers only `hwnd == nullptr`; `TestWindow::create({0, 0})` and the no-display path are
  untested; `std::get` inside a `noexcept` function in `surface.cpp` (use `get_if` if tidy flags it).
- Task 7 (loader, opt-in): `vk::detail::DynamicLoader` is still compiled in
  (`#define VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL 0` in `src/vk.h`, M2); test_loader case 1 cannot
  fail for the System32 property (a planted `vulkan-1` module, M2); no white-box test that
  `DeviceCore::create` returns `Unsupported` before creating a device (M2); stand-alone dvv-gpu CI
  needs a published dvv-platform; stale `_deps/dvv_platform-build/CTestTestfile.cmake` in old build
  trees must be deleted (or configure fresh).
- Task 8 (`PresenterLogic`, `presentBlit`): the Release-only "ignored" path of wrong-phase calls is
  untested; the GPU fixtures fail instead of skipping when the device lacks `VK_KHR_swapchain` or
  the loader lacks a surface extension (a presentation-aware probe in `gpu_main.cpp`).
- Task 9 (Presenter): a provider that throws a non-`std::exception` type ends in `std::terminate`
  (accepted contract, M2); re-entrant calls during `~Presenter` were handled in rawreel's viewport
  (Task 12a), not in dvv-gpu.
- Task 10 (presenter events): case 8 checks only `framesPresented >= 1` per presenter (a stronger
  per-presenter progress check would catch a stall); case 3 never asserts that a request while hidden
  presents nothing; the R7 commit message rounds the numbers more favourably than the report (see
  the R7 line).
- Task 11 (rawreel renderer): pin bars 2, 4 and 5 and the bar/ramp boundary rows at 33×17 with fixed
  values in the unit test; `repos.txt` lists `rawreel` (R12, publishing).
- Task 12a (rawreel-gui): wx asserts and `OnExceptionInMainLoop` can open modal dialogs in an
  unattended `--selftest`, an exception escaping the main loop can turn into exit 0 after the dialog,
  and `SelfTest::Notify()` bypasses wx's `SafelyProcessEvent` guard (one merged item, M2: a member
  `wxTimer` with `Bind(wxEVT_TIMER)` or a try wrapper, plus an assert handler); the automated R15
  hang detector is the CTest `TIMEOUT 120` (the 6 s bound was checked by 10 closes of 157-208 ms
  and the user's close); 7876bbe edited Task 11's GPU test during Task 11's review.
- Task 13a (CI): every CI run fails at bootstrap until all repositories in `repos.txt` exist on
  GitHub (publishing checklist, no YAML change).
