# M0 Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the rawreel-workspace infrastructure (presets, bootstrap, third-party superbuild with an SPDX licence gate, privacy lint, reusable CI) and the first library, dvv-base, so that CI is green on Windows and Linux with all base unit tests passing (spec §15, M0).

**Architecture:** All repositories sit side by side in one local workspace folder; build output goes to `../_build` and `../_third_party`, never into a repository. `rawreel-workspace` owns the shared CMake presets (included by every repository), the bootstrap scripts, the third-party superbuild and the CI workflow that every dvv repository calls. `dvv-base` is a static C++23 library exported as the CMake package `dvv_base` (target `dvv::base`); it carries the factory-only creation rule and lifetime registry of spec §9.4, the error model of §11, the capped coroutine task layer of §4, logging, a JSON wrapper, the URI parser of §7.2 and per-user paths.

**Tech Stack:** C++23 (MSVC v145 with `/std:c++23preview`, Clang 19 with libstdc++ 14), CMake 4.3 with presets and workflow presets, Ninja, doctest 2.5.3, nlohmann/json 3.12.0, clang-format/clang-tidy 19, GitHub Actions, PowerShell 7 and POSIX sh.

## Global Constraints

- Spec: `rawreel-workspace/docs/superpowers/specs/2026-10-19-rawreel-design.md` (approved). Decisions in its §17 are final.
- Language: **C++23**, no modules, no `import std`; standard library preferred, no Boost (§2).
- Toolchain: **CMake ≥ 4.2** (`cmake_minimum_required(VERSION 4.2)`); Windows **Visual Studio 2026** (MSVC v145) with `/std:c++23preview` set explicitly; Linux **Clang ≥ 19** with **libstdc++ ≥ 14**; GCC and libc++ are not supported (§2, §17).
- Warnings: `/W4 /WX` and `-Wall -Wextra -Wpedantic -Werror` for our code only; third-party headers are SYSTEM (`/external:anglebrackets /external:W0`) (§13.3).
- Format: clang-format 19, Google style, 4-space indent, 100 columns (§14).
- Names: PascalCase types, camelCase functions/methods, snake_case variables, trailing underscore for private members, UPPER_SNAKE constants, `I` prefix for interfaces; headers `.h` with `#pragma once`, sources `.cpp` (§14). Public struct fields are camelCase (as in the spec's `SourceInfo::frameCount`).
- Managed `dvv::` objects: `final` classes, `CreationKey<F>` first constructor parameter, `Managed<T>` base, creation only through the authority's factory, smart-pointer ownership, strong references only upwards, `weak_ptr` or ids for observers (§9.4).
- Licence: MIT; no GPL dependency in any shipped artifact; the configure-time SPDX gate is authoritative (§13.2).
- Tests: doctest + CTest; TDD; fixtures generated, never committed (§12).
- Privacy (workspace rules): no real names, personal e-mail addresses, host names, IP addresses or absolute machine paths in any committed file or message; examples use `X:` drives and `<name>` placeholders. Commits use the GitHub handle and its noreply e-mail, set per repository.
- Commits: conventional commits, each ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- GitHub: creating repositories and pushing happen only after the user's explicit go-ahead at that time (Task 20).
- Shells: PowerShell on Windows; WSL (Ubuntu 24.04) only for Linux builds. Run git commands from Windows; use WSL only to configure, build and test.
- Commands below run from the workspace root (the folder that contains `rawreel-workspace`) unless a step says otherwise.

## Notes from verifying this plan

Every file in this plan was built and tested before the plan was written: MSVC Debug/Release/ASan, Clang 19 Debug/Release/ASan+UBSan/TSan, GCC 13 Debug/Release (GCC 14 was not installed), clang-format and clang-tidy 19 clean, actionlint clean, privacy lint clean. Findings that shaped the code:

1. CMake maps C++23 to `/std:c++latest` for MSVC; setting `CMAKE_CXX23_STANDARD_COMPILE_OPTION` to `-std:c++23preview` gives `LanguageStandard=stdcpp23` and `_MSVC_LANG == 202302`. CMake also scans for modules by default, so targets set `CXX_SCAN_FOR_MODULES OFF`.
2. doctest finds functions named `toString` by ADL and expects them to return its own string type, so dvv uses `errcName`, `logLevelName` and `formatUri` instead. Tests define `DOCTEST_CONFIG_USE_STD_HEADERS`, otherwise doctest only forward-declares `std::ostream`.
3. GCC guarantees the tail call of coroutine symmetric transfer only when optimising; at `-O0` every synchronously completed `co_await` nests a stack frame. Clang and MSVC are fine. The stack test uses fewer iterations on unoptimised GCC; this is recorded as a risk in spec §16 (Task 19).
4. Windows `MAX_PATH`: ExternalProject trees under very long folders fail with `C1083`; keep the workspace at a short path.
5. clang-format would move `<windows.h>` after `<shlobj.h>` (breaks the build) and misreads `co_return v * v`; the code avoids both.
6. MSVC ASan needs its runtime DLL from the compiler folder on `PATH`; the CMake module adds it to the test environment.
7. `-ffile-prefix-map` keeps machine paths out of Clang binaries; MSVC still embeds absolute `__FILE__`/PDB paths, which M4 packaging must address (recorded in §16 by Task 19).

## Changes during execution (2026-10-20, after Task 15)

- **GCC dropped** (spec §17): Linux builds use Clang 19 with libstdc++ 14 (`libstdc++-14-dev`, no GCC compiler). The `ninja-gcc` presets, the `gcc` bootstrap toolchain and the GCC-only stack-test branch were removed; `DvvCompilerOptions.cmake` now stops configuration for any non-MSVC compiler other than Clang and for libstdc++ older than 14. Finding 3 above no longer applies. Code listings in Tasks 0–15 are historical; later tasks below are updated.
- **Compile-fail tests drive the top-level build tree:** a Task 9 review fix had passed `PROJECT_BINARY_DIR`, which breaks the workspace build (dvv-base is a subproject there). They use `CMAKE_BINARY_DIR` again.
- **Listings older than review fixes:** Tasks 9–15 got review fixes (for example the compile-fail runner with expected diagnostics). Where a later task lists a whole existing file (Task 16's `tests/CMakeLists.txt`), add only the new entries to the committed file; do not copy the listing over it.
- **Tasks 16–18 review changes:** `checkApp` also rejects `.` and `..`; `DVV_TRY` carries a `NOLINT(bugprone-macro-parentheses)` because its first argument is a declaration; the privacy lint also allows GitHub's own committer `noreply@github.com` (pull-request merge commits and web merges); the Windows tool-test step runs one command per step (pwsh only checks the last exit code); the sanitizer jobs install `libclang-rt-19-dev` explicitly. The committed files, not the Task 3 and Task 18 listings, are authoritative.

## File Structure

```text
<workspace root>/
  rawreel-workspace/
    .gitattributes, .gitignore, LICENSE, README.md
    CMakeLists.txt                 workspace build: all repos from sibling folders + tool tests
    CMakePresets.json              includes cmake/DvvPresets.json
    repos.txt                      sibling repositories in dependency order
    bootstrap.ps1, bootstrap.sh    clone siblings, set hooks + identity, build third-party
    cmake/DvvPresets.json          the one set of presets every repository includes
    cmake/dvv-versions.cmake       FetchContent declarations + sibling check
    cmake/DvvSpdx.cmake            SPDX expression evaluator (licence gate)
    cmake/DvvThirdParty.cmake      dvv_third_party() declaration + gate
    deps/versions.cmake            third-party list: version, URL, SHA256, SPDX
    deps/CMakeLists.txt            superbuild (ExternalProject) + notices
    deps/write-notices.cmake       THIRD_PARTY_NOTICES.txt generator
    tools/privacy-lint.cmake       privacy lint (files + commits)
    tools/githooks/pre-push        runs the lint before every push
    tools/requirements.txt         pinned cmake, ninja, clang-format, clang-tidy
    tests/spdx/test_spdx.cmake, tests/spdx/test_third_party_gate.cmake
    tests/privacy/test_privacy_lint.cmake
    .github/workflows/dvv-ci.yml   reusable CI for dvv repositories
    .github/workflows/workspace.yml   tool tests + nightly integration
  dvv-base/
    .gitattributes, .gitignore, .clang-format, .clang-tidy, LICENSE, README.md
    CMakeLists.txt, CMakePresets.json
    cmake/DvvCompilerOptions.cmake   C++23, warnings, sanitizers, path mapping (installed)
    cmake/dvv_baseConfig.cmake.in
    include/dvv/base/  environment.h error.h lifetime.h leak_check.h executor.h executors.h
                       task.h when_all.h log.h json.h uri.h paths.h testing/test_authority.h
    src/               environment.cpp error.cpp lifetime.cpp leak_check.cpp executors.cpp
                       log.cpp json.cpp uri.cpp paths.cpp
    tests/             CMakeLists.txt main.cpp test_*.cpp compile_fail/ package/
    .github/workflows/ci.yml       calls the reusable workflow
```

---

### Task 0: Prerequisites (the user runs these once)

**Files:** none.

- [ ] **Step 1: Check the Windows tools**

Run (PowerShell):

```powershell
cmake --version; git --version; python --version
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property catalog_productDisplayVersion
```

Expected: CMake ≥ 4.2, Python 3.x, Visual Studio `18.x`.

- [ ] **Step 2: Install the Linux toolchain in WSL (needs sudo, so the user runs it)**

The user runs this in a WSL terminal (sudo asks for the password):

```sh
sudo apt-get update && sudo apt-get install -y clang-19 libstdc++-14-dev ninja-build python3-venv
```

- [ ] **Step 3: Verify the WSL compiler and standard library**

Run: `wsl -d Ubuntu-24.04 -e bash -lc 'clang++-19 --version | head -1; clang++-19 -v -x c++ -E /dev/null 2>&1 | grep "Selected GCC"'`
Expected: `Ubuntu clang version 19.x` and `Selected GCC installation: /usr/lib/gcc/x86_64-linux-gnu/14`.

(The pinned Python tools are installed in Task 1, after `tools/requirements.txt` exists.)

---

### Task 1: Workspace repository hygiene

**Files:**
- Create: `rawreel-workspace/.gitattributes`, `rawreel-workspace/.gitignore`, `rawreel-workspace/LICENSE`, `rawreel-workspace/tools/requirements.txt`
- Modify: all tracked files (line-ending renormalisation)

**Interfaces:**
- Produces: LF line endings everywhere except `*.ps1`, `*.bat`, `*.cmd`, `*.sln`; pinned tool versions in `tools/requirements.txt` used by Task 0, CI and the README.

- [ ] **Step 1: Write the failing check**

Run: `git -C rawreel-workspace check-attr eol -- README.md bootstrap.ps1`
Expected: `eol: unspecified` for both (no `.gitattributes` yet).

- [ ] **Step 2: Create `rawreel-workspace/.gitattributes`**

```text
# Line endings: LF in the repository and working tree; CRLF only where Windows tools need it.
* text=auto eol=lf
*.ps1 text eol=crlf
*.bat text eol=crlf
*.cmd text eol=crlf
*.sln text eol=crlf
*.sh text eol=lf
tools/githooks/* text eol=lf

*.png binary
*.jpg binary
*.ico binary
*.pdf binary
```

- [ ] **Step 3: Create `rawreel-workspace/.gitignore`**

```text
# Build output lives outside the repositories (../_build, ../_third_party); these guard against
# accidental in-tree builds and editor files.
/build/
/out/
/.vs/
/.vscode/
/.idea/
CMakeUserPresets.json
*.user
```

- [ ] **Step 4: Create `rawreel-workspace/LICENSE`**

```text
MIT License

Copyright (c) 2026 The rawreel authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

- [ ] **Step 5: Create `rawreel-workspace/tools/requirements.txt`**

```text
# Pinned developer and CI tools: python -m pip install -r tools/requirements.txt
cmake==4.3.2
ninja==1.13.2
clang-format==19.1.7
clang-tidy==19.1.0
```

- [ ] **Step 6: Install the pinned tools (Windows and WSL)**

```powershell
python -m pip install --user -r rawreel-workspace/tools/requirements.txt
wsl -d Ubuntu-24.04 -e bash -lc 'python3 -m venv ~/.venvs/rawreel && ~/.venvs/rawreel/bin/pip install -r rawreel-workspace/tools/requirements.txt && (grep -q venvs/rawreel ~/.profile || echo "export PATH=\$HOME/.venvs/rawreel/bin:\$PATH" >> ~/.profile)'
```

`wsl` starts in the current folder, so the relative path works. Check: `wsl -d Ubuntu-24.04 -e bash -lc 'cmake --version | head -1; clang-format --version'` → `cmake version 4.3.2`, `clang-format version 19.1.7`. On Windows, `cmake --version` must still report ≥ 4.2 (the pip `cmake` may now come first on `PATH`; both are fine).

- [ ] **Step 7: Renormalise and verify**

```powershell
git -C rawreel-workspace add --renormalize .
git -C rawreel-workspace add .gitattributes .gitignore LICENSE tools/requirements.txt
git -C rawreel-workspace check-attr eol -- README.md bootstrap.ps1
git -C rawreel-workspace ls-files --eol | Select-String 'i/crlf'
```

Expected: `README.md: eol: lf`, `bootstrap.ps1: eol: crlf`; the last command prints nothing.

- [ ] **Step 8: Commit**

```powershell
git -C rawreel-workspace commit -m "chore: add gitattributes, gitignore, licence and pinned tools" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: SPDX licence-expression gate

**Files:**
- Create: `rawreel-workspace/cmake/DvvSpdx.cmake`
- Test: `rawreel-workspace/tests/spdx/test_spdx.cmake`

**Interfaces:**
- Produces: `dvv_spdx_evaluate(<expression> <linkage> <out_allowed> <out_reason>)` where `<linkage>` is `static`, `runtime` or `tool`; lists `DVV_SPDX_PERMISSIVE`, `DVV_SPDX_RUNTIME_ONLY`, `DVV_SPDX_EXCEPTIONS`. Used by Task 4.

- [ ] **Step 1: Write the failing test** `rawreel-workspace/tests/spdx/test_spdx.cmake`

```cmake
# Unit tests for cmake/DvvSpdx.cmake. Run: cmake -P tests/spdx/test_spdx.cmake
cmake_minimum_required(VERSION 4.2)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/DvvSpdx.cmake")

set(failures 0)
macro(expect expression linkage expected)
    dvv_spdx_evaluate("${expression}" "${linkage}" allowed reason)
    if(NOT allowed EQUAL ${expected})
        message(SEND_ERROR "FAIL: '${expression}' (${linkage}) gave ${allowed}, expected ${expected}; ${reason}")
        math(EXPR failures "${failures} + 1")
    endif()
endmacro()

expect("MIT" static 1)
expect("BSD-2-Clause-Patent" static 1)
expect("MIT OR GPL-2.0-only" static 1)
expect("MIT AND GPL-2.0-only" static 0)
expect("GPL-3.0-or-later" static 0)
expect("AGPL-3.0-only" runtime 0)
expect("LGPL-2.1-or-later" static 0)
expect("LGPL-2.1-or-later" runtime 1)
expect("LGPL-2.0-or-later WITH WxWindows-exception-3.1" static 1)
expect("GPL-3.0-or-later WITH Bison-exception-2.2" static 1)
expect("Apache-2.0 WITH LLVM-exception" static 1)
expect("GPL-2.0-or-later WITH Classpath-exception-2.0" static 0)
expect("(MIT OR Apache-2.0) AND BSD-3-Clause" static 1)
expect("MIT OR Apache-2.0 AND GPL-2.0-only" static 1)
expect("(MIT OR Apache-2.0) AND GPL-2.0-only" static 0)
expect("(MIT" static 0)
expect("MIT)" static 0)
expect("MIT AND" static 0)
expect("MIT WITH" static 0)
expect("MIT BSD-3-Clause" static 0)
expect("" static 0)
expect("MIT" shipped 0)

dvv_spdx_evaluate("MIT AND GPL-2.0-only" static allowed reason)
if(NOT reason MATCHES "GPL-2.0-only")
    message(SEND_ERROR "FAIL: reason does not name the rejected licence: '${reason}'")
    math(EXPR failures "${failures} + 1")
endif()

if(failures GREATER 0)
    message(FATAL_ERROR "${failures} SPDX test(s) failed")
endif()
message(STATUS "SPDX tests passed")
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake -P rawreel-workspace/tests/spdx/test_spdx.cmake`
Expected: FAIL with `include could not find requested file: .../cmake/DvvSpdx.cmake`.

- [ ] **Step 3: Implement `rawreel-workspace/cmake/DvvSpdx.cmake`**

```cmake
# SPDX licence-expression gate (spec §13.2).
#
# dvv_spdx_evaluate(<expression> <linkage> <out_allowed> <out_reason>)
#   <linkage> is one of: static (linked into a shipped binary), runtime (loaded at run time, never
#   linked) or tool (used only at build time). Sets <out_allowed> to 1 or 0 and <out_reason> to a
#   human-readable explanation when the expression is rejected.
include_guard(GLOBAL)

set(DVV_SPDX_PERMISSIVE
    MIT MIT-0 BSD-2-Clause BSD-3-Clause BSD-2-Clause-Patent Apache-2.0 Zlib BSL-1.0 ISC
    Unlicense CC0-1.0 Unicode-3.0 Unicode-DFS-2016)
# Allowed only for libraries that are loaded at run time or used as build tools.
set(DVV_SPDX_RUNTIME_ONLY LGPL-2.1-only LGPL-2.1-or-later LGPL-3.0-only LGPL-3.0-or-later)
# "<licence> WITH <exception>" is allowed when the exception is listed here.
set(DVV_SPDX_EXCEPTIONS WxWindows-exception-3.1 Bison-exception-2.2 LLVM-exception)

function(_dvv_spdx_operand_allowed operand linkage out)
    if(operand MATCHES "^([^~]+)~WITH~(.+)$")
        set(licence "${CMAKE_MATCH_1}")
        set(exception "${CMAKE_MATCH_2}")
        if(exception IN_LIST DVV_SPDX_EXCEPTIONS)
            set(${out} 1 PARENT_SCOPE)
            return()
        endif()
        _dvv_spdx_operand_allowed("${licence}" "${linkage}" licence_ok)
        set(${out} ${licence_ok} PARENT_SCOPE)
        return()
    endif()
    if(operand IN_LIST DVV_SPDX_PERMISSIVE)
        set(${out} 1 PARENT_SCOPE)
    elseif(operand IN_LIST DVV_SPDX_RUNTIME_ONLY AND NOT linkage STREQUAL "static")
        set(${out} 1 PARENT_SCOPE)
    else()
        set(${out} 0 PARENT_SCOPE)
    endif()
endfunction()

function(dvv_spdx_evaluate expression linkage out_allowed out_reason)
    set(${out_allowed} 0 PARENT_SCOPE)
    if(NOT linkage MATCHES "^(static|runtime|tool)$")
        set(${out_reason} "unknown linkage '${linkage}'" PARENT_SCOPE)
        return()
    endif()

    # Tokenise: parentheses become separate tokens.
    string(REGEX REPLACE "([()])" " \\1 " spaced "${expression}")
    separate_arguments(raw_tokens UNIX_COMMAND "${spaced}")
    if(NOT raw_tokens)
        set(${out_reason} "empty expression" PARENT_SCOPE)
        return()
    endif()

    # Fold "<licence> WITH <exception>" into a single operand "<licence>~WITH~<exception>".
    set(tokens "")
    set(pending_with FALSE)
    foreach(token IN LISTS raw_tokens)
        if(token STREQUAL "WITH")
            list(LENGTH tokens count)
            if(count EQUAL 0 OR pending_with)
                set(${out_reason} "syntax error near WITH in '${expression}'" PARENT_SCOPE)
                return()
            endif()
            set(pending_with TRUE)
        elseif(pending_with)
            list(POP_BACK tokens licence)
            if(licence MATCHES "^(AND|OR|\\(|\\))$" OR token MATCHES "^(AND|OR|\\(|\\))$")
                set(${out_reason} "syntax error near WITH in '${expression}'" PARENT_SCOPE)
                return()
            endif()
            list(APPEND tokens "${licence}~WITH~${token}")
            set(pending_with FALSE)
        else()
            list(APPEND tokens "${token}")
        endif()
    endforeach()
    if(pending_with)
        set(${out_reason} "syntax error: WITH without exception in '${expression}'" PARENT_SCOPE)
        return()
    endif()

    # Shunting-yard to reverse Polish notation; AND binds tighter than OR.
    set(output "")
    set(ops "")
    foreach(token IN LISTS tokens)
        if(token STREQUAL "(")
            list(APPEND ops "(")
        elseif(token STREQUAL ")")
            set(found FALSE)
            while(ops)
                list(POP_BACK ops top)
                if(top STREQUAL "(")
                    set(found TRUE)
                    break()
                endif()
                list(APPEND output "${top}")
            endwhile()
            if(NOT found)
                set(${out_reason} "syntax error: unbalanced ')' in '${expression}'" PARENT_SCOPE)
                return()
            endif()
        elseif(token STREQUAL "AND" OR token STREQUAL "OR")
            while(ops)
                list(GET ops -1 top)
                if(top STREQUAL "AND" OR (top STREQUAL "OR" AND token STREQUAL "OR"))
                    list(POP_BACK ops)
                    list(APPEND output "${top}")
                else()
                    break()
                endif()
            endwhile()
            list(APPEND ops "${token}")
        else()
            list(APPEND output "${token}")
        endif()
    endforeach()
    while(ops)
        list(POP_BACK ops top)
        if(top STREQUAL "(")
            set(${out_reason} "syntax error: unbalanced '(' in '${expression}'" PARENT_SCOPE)
            return()
        endif()
        list(APPEND output "${top}")
    endwhile()

    # Evaluate the RPN; remember rejected operands for the message.
    set(stack "")
    set(rejected "")
    foreach(token IN LISTS output)
        if(token STREQUAL "AND" OR token STREQUAL "OR")
            list(LENGTH stack depth)
            if(depth LESS 2)
                set(${out_reason} "syntax error: missing operand in '${expression}'" PARENT_SCOPE)
                return()
            endif()
            list(POP_BACK stack right)
            list(POP_BACK stack left)
            if(token STREQUAL "AND")
                if(left AND right)
                    list(APPEND stack 1)
                else()
                    list(APPEND stack 0)
                endif()
            else()
                if(left OR right)
                    list(APPEND stack 1)
                else()
                    list(APPEND stack 0)
                endif()
            endif()
        else()
            _dvv_spdx_operand_allowed("${token}" "${linkage}" ok)
            if(NOT ok)
                string(REPLACE "~" " " shown "${token}")
                list(APPEND rejected "${shown}")
            endif()
            list(APPEND stack ${ok})
        endif()
    endforeach()
    list(LENGTH stack depth)
    if(NOT depth EQUAL 1)
        set(${out_reason} "syntax error: missing operator in '${expression}'" PARENT_SCOPE)
        return()
    endif()
    if(stack)
        set(${out_allowed} 1 PARENT_SCOPE)
        set(${out_reason} "" PARENT_SCOPE)
    else()
        list(JOIN rejected ", " rejected_text)
        set(${out_reason} "not allowed for ${linkage} linkage: ${rejected_text}" PARENT_SCOPE)
    endif()
endfunction()
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake -P rawreel-workspace/tests/spdx/test_spdx.cmake`
Expected: `-- SPDX tests passed`.

- [ ] **Step 5: Commit**

```powershell
git -C rawreel-workspace add cmake/DvvSpdx.cmake tests/spdx/test_spdx.cmake
git -C rawreel-workspace commit -m "feat: add SPDX licence-expression gate" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Privacy lint and pre-push hook

**Files:**
- Create: `rawreel-workspace/tools/privacy-lint.cmake`, `rawreel-workspace/tools/githooks/pre-push`
- Test: `rawreel-workspace/tests/privacy/test_privacy_lint.cmake`

**Interfaces:**
- Produces: `cmake -DREPO=<dir> [-DMODE=tree|commits|prepush] [-DEXTRA_TERMS=<a;b>] -P tools/privacy-lint.cmake` (exit code ≠ 0 on findings; output names location and kind only). The hook is activated per repository by bootstrap (Task 5) through `core.hooksPath`.

- [ ] **Step 1: Write the failing test** `rawreel-workspace/tests/privacy/test_privacy_lint.cmake`

```cmake
# Tests for tools/privacy-lint.cmake.
# Run: cmake -DWORK_DIR=<empty scratch dir> -P tests/privacy/test_privacy_lint.cmake
# Offending strings are assembled from pieces so this file itself passes the lint.
cmake_minimum_required(VERSION 4.2)
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "pass -DWORK_DIR=<scratch directory>")
endif()
find_program(GIT_EXECUTABLE git REQUIRED)
set(lint "${CMAKE_CURRENT_LIST_DIR}/../../tools/privacy-lint.cmake")
set(failures 0)

set(drive "C")
set(other_drive "D")
set(home "home")
set(at "@")
set(octet "168")
set(bad_user_path "${drive}:\\Users\\alice\\clip.yuv")
set(bad_drive_path "${other_drive}:/media/clip.yuv")
set(bad_home "/${home}/alice/clip.yuv")
set(bad_email "alice${at}mail.test.org")
set(bad_ip "192.${octet}.1.7")
set(noreply "12345+someone${at}users.noreply.github.com")

function(make_repo name)
    set(dir "${WORK_DIR}/${name}")
    file(REMOVE_RECURSE "${dir}")
    file(MAKE_DIRECTORY "${dir}")
    execute_process(COMMAND "${GIT_EXECUTABLE}" init -q "${dir}" COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dir}" config user.name someone)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dir}" config user.email "${noreply}")
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dir}" config commit.gpgsign false)
    set(repo_dir "${dir}" PARENT_SCOPE)
endfunction()

function(commit_file dir file content message)
    file(WRITE "${dir}/${file}" "${content}")
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dir}" add -A COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dir}" commit -q -m "${message}"
                    COMMAND_ERROR_IS_FATAL ANY)
endfunction()

# expect_lint(<repo> <mode> <extra terms> <0|1 expected failure> <expected text or "">)
function(expect_lint dir mode terms expect_failure expected_text)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DREPO=${dir}" "-DMODE=${mode}" "-DEXTRA_TERMS=${terms}" -P "${lint}"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(all "${out}${err}")
    if(expect_failure AND status EQUAL 0)
        message(SEND_ERROR "FAIL: expected lint failure for ${dir} (${mode})\n${all}")
        set(failures 1 PARENT_SCOPE)
    elseif(NOT expect_failure AND NOT status EQUAL 0)
        message(SEND_ERROR "FAIL: expected clean lint for ${dir} (${mode})\n${all}")
        set(failures 1 PARENT_SCOPE)
    elseif(expected_text AND NOT all MATCHES "${expected_text}")
        message(SEND_ERROR "FAIL: output lacks '${expected_text}' for ${dir}\n${all}")
        set(failures 1 PARENT_SCOPE)
    endif()
endfunction()

make_repo(clean)
commit_file("${repo_dir}" "notes.md"
    "Example paths: X:\\Users\\<name>\\clip.yuv and /home/<name>/clip.yuv\nlocalhost 127.0.0.1\nCo-Authored-By: Claude <noreply${at}anthropic.com>\n"
    "docs: add notes")
expect_lint("${repo_dir}" commits "" 0 "")

make_repo(user_path)
commit_file("${repo_dir}" "a.txt" "open ${bad_user_path}\n" "add a")
expect_lint("${repo_dir}" tree "" 1 "a.txt:1: Windows user profile path")

make_repo(drive_path)
commit_file("${repo_dir}" "a.txt" "line one\nopen ${bad_drive_path}\n" "add a")
expect_lint("${repo_dir}" tree "" 1 "a.txt:2: absolute drive path")

make_repo(home_path)
commit_file("${repo_dir}" "a.txt" "${bad_home}\n" "add a")
expect_lint("${repo_dir}" tree "" 1 "home directory path")

make_repo(email)
commit_file("${repo_dir}" "a.txt" "contact ${bad_email}\n" "add a")
expect_lint("${repo_dir}" tree "" 1 "not a noreply address")

make_repo(ip)
commit_file("${repo_dir}" "a.txt" "host ${bad_ip}\n" "add a")
expect_lint("${repo_dir}" tree "" 1 "IPv4 address")

make_repo(allowed)
commit_file("${repo_dir}" "a.txt" "host ${bad_ip} # privacy-lint: allow\n" "add a")
expect_lint("${repo_dir}" tree "" 0 "")

make_repo(message)
commit_file("${repo_dir}" "a.txt" "fine\n" "fix: build on ${bad_user_path}")
expect_lint("${repo_dir}" tree "" 0 "")
expect_lint("${repo_dir}" commits "" 1 "message:1: Windows user profile path")

make_repo(author)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${repo_dir}" config user.email "${bad_email}")
commit_file("${repo_dir}" "a.txt" "fine\n" "add a")
expect_lint("${repo_dir}" commits "" 1 "author or committer e-mail")

make_repo(terms)
commit_file("${repo_dir}" "a.txt" "built on Workstation-42\n" "add a")
expect_lint("${repo_dir}" tree "workstation-42" 1 "local user or host name")
expect_lint("${repo_dir}" tree "ab" 0 "")

make_repo(leak_in_output)
commit_file("${repo_dir}" "a.txt" "${bad_email}\n" "add a")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DREPO=${repo_dir}" -P "${lint}"
                OUTPUT_VARIABLE out ERROR_VARIABLE err)
string(FIND "${out}${err}" "${bad_email}" position)
if(position GREATER_EQUAL 0)
    message(SEND_ERROR "FAIL: lint output repeats the private text")
    set(failures 1)
endif()

if(failures)
    message(FATAL_ERROR "privacy-lint tests failed")
endif()
message(STATUS "privacy-lint tests passed")
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake "-DWORK_DIR=$env:TEMP/dvv-privacy-test" -P rawreel-workspace/tests/privacy/test_privacy_lint.cmake`
Expected: FAIL — the clean cases report `FAIL: expected clean lint ...` because `tools/privacy-lint.cmake` does not exist yet.

- [ ] **Step 3: Implement `rawreel-workspace/tools/privacy-lint.cmake`**

```cmake
# Privacy lint (spec §13.3): absolute machine paths, non-noreply e-mail addresses, IP addresses and
# caller-supplied terms (user and host name) in tracked files and commit metadata.
#
# Usage: cmake -DREPO=<repo dir> [-DMODE=tree|commits|prepush] [-DEXTRA_TERMS=<a;b>]
#              -P tools/privacy-lint.cmake
#   tree     tracked files at HEAD (default)
#   commits  tracked files + every commit reachable from HEAD
#   prepush  tracked files + commits not yet on any remote
# A line containing "privacy-lint: allow" is skipped. Findings report the location and kind only,
# never the matched text, so CI logs do not repeat private data.
cmake_minimum_required(VERSION 4.2)

if(NOT DEFINED REPO)
    message(FATAL_ERROR "privacy-lint: pass -DREPO=<repository directory>")
endif()
if(NOT DEFINED MODE)
    set(MODE tree)
endif()
if(NOT MODE MATCHES "^(tree|commits|prepush)$")
    message(FATAL_ERROR "privacy-lint: unknown MODE '${MODE}'")
endif()
find_program(GIT_EXECUTABLE git REQUIRED)

# Placeholders keep ';', '[', ']' and '\' from breaking CMake list handling.
string(ASCII 1 PL_SEMI)
string(ASCII 2 PL_LBR)
string(ASCII 3 PL_RBR)
string(ASCII 4 PL_BSL)
string(ASCII 30 PL_RS)
string(ASCII 31 PL_US)

set(terms "")
foreach(term IN LISTS EXTRA_TERMS)
    string(STRIP "${term}" term)
    string(TOLOWER "${term}" term)
    string(LENGTH "${term}" length)
    if(length GREATER_EQUAL 3)
        list(APPEND terms "${term}")
    endif()
endforeach()

set_property(GLOBAL PROPERTY PL_FINDINGS 0)

function(_pl_escape text out)
    string(REPLACE "\\" "${PL_BSL}" text "${text}")
    string(REPLACE ";" "${PL_SEMI}" text "${text}")
    string(REPLACE "[" "${PL_LBR}" text "${text}")
    string(REPLACE "]" "${PL_RBR}" text "${text}")
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

function(_pl_unescape text out)
    string(REPLACE "${PL_BSL}" "\\" text "${text}")
    string(REPLACE "${PL_SEMI}" ";" text "${text}")
    string(REPLACE "${PL_LBR}" "[" text "${text}")
    string(REPLACE "${PL_RBR}" "]" text "${text}")
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

function(_pl_report where kind)
    message(STATUS "privacy-lint: ${where}: ${kind}")
    get_property(count GLOBAL PROPERTY PL_FINDINGS)
    math(EXPR count "${count} + 1")
    set_property(GLOBAL PROPERTY PL_FINDINGS ${count})
endfunction()

function(_pl_email_allowed email out)
    if(email MATCHES "@users\\.noreply\\.github\\.com$"
       OR email STREQUAL "noreply@anthropic.com"
       OR email MATCHES "@example\\.(com|org|net)$")
        set(${out} 1 PARENT_SCOPE)
    else()
        set(${out} 0 PARENT_SCOPE)
    endif()
endfunction()

# Checks one unescaped line of text and reports every kind of finding once.
function(_pl_check_line where text)
    if(text MATCHES "privacy-lint: allow")
        return()
    endif()
    if(text MATCHES "[A-Za-z]:[\\/]+[Uu][Ss][Ee][Rr][Ss][\\/]+[A-Za-z0-9._-]")
        _pl_report("${where}" "Windows user profile path")
    elseif(text MATCHES "(^|[^A-Za-z0-9])[A-WYZa-wyz]:[\\/][A-Za-z0-9_]")
        _pl_report("${where}" "absolute drive path (use X: in examples)")
    endif()
    if(text MATCHES "/(home|Users)/[A-Za-z0-9._-]")
        _pl_report("${where}" "home directory path")
    endif()
    string(REGEX MATCHALL "[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z][A-Za-z]+" emails "${text}")
    foreach(email IN LISTS emails)
        _pl_email_allowed("${email}" ok)
        if(NOT ok)
            _pl_report("${where}" "e-mail address that is not a noreply address")
            break()
        endif()
    endforeach()
    if(text MATCHES "(^|[^0-9.])([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)([^0-9.]|$)")
        if(NOT CMAKE_MATCH_2 MATCHES "^(127\\.0\\.0\\.1|0\\.0\\.0\\.0)$")
            _pl_report("${where}" "IPv4 address")
        endif()
    endif()
    if(terms)
        string(TOLOWER "${text}" lower)
        foreach(term IN LISTS terms)
            string(FIND "${lower}" "${term}" position)
            if(position GREATER_EQUAL 0)
                _pl_report("${where}" "local user or host name")
                break()
            endif()
        endforeach()
    endif()
endfunction()

# Checks every line of a block of text; <prefix> is used as "<prefix>:<line>".
function(_pl_check_text prefix text)
    _pl_escape("${text}" escaped)
    string(REPLACE "\r" "" escaped "${escaped}")
    string(REPLACE "\n" ";" lines "${escaped}")
    set(number 0)
    foreach(line IN LISTS lines)
        math(EXPR number "${number} + 1")
        _pl_unescape("${line}" plain)
        _pl_check_line("${prefix}:${number}" "${plain}")
    endforeach()
endfunction()

# 1. Tracked files.
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${REPO}" -c core.quotepath=off ls-files
    OUTPUT_VARIABLE listing RESULT_VARIABLE status OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "privacy-lint: '${REPO}' is not a git repository")
endif()
string(REPLACE "\n" ";" files "${listing}")
foreach(file IN LISTS files)
    set(path "${REPO}/${file}")
    if(NOT EXISTS "${path}" OR IS_DIRECTORY "${path}")
        continue()
    endif()
    _pl_check_line("path ${file}" "${file}")
    file(READ "${path}" head LIMIT 4096 HEX)
    if(head MATCHES "^(..)*00")
        continue()  # binary file
    endif()
    file(READ "${path}" content)
    _pl_check_text("${file}" "${content}")
endforeach()

# 2. Commit metadata and messages.
if(NOT MODE STREQUAL "tree")
    if(MODE STREQUAL "commits")
        set(range HEAD)
    else()
        set(range HEAD --not --remotes)
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${REPO}" log ${range}
                "--format=%h%x1f%an%x1f%ae%x1f%cn%x1f%ce%x1f%B%x1e"
        OUTPUT_VARIABLE log RESULT_VARIABLE status ERROR_QUIET)
    if(status EQUAL 0)
        _pl_escape("${log}" log)
        string(REPLACE "${PL_RS}" ";" records "${log}")
        foreach(record IN LISTS records)
            string(STRIP "${record}" record)
            if(record STREQUAL "")
                continue()
            endif()
            string(REPLACE "${PL_US}" ";" fields "${record}")
            list(GET fields 0 hash)
            list(GET fields 1 author)
            list(GET fields 2 author_email)
            list(GET fields 3 committer)
            list(GET fields 4 committer_email)
            list(GET fields 5 body)
            foreach(email IN ITEMS "${author_email}" "${committer_email}")
                _pl_unescape("${email}" email)
                _pl_email_allowed("${email}" ok)
                if(NOT ok)
                    _pl_report("commit ${hash}" "author or committer e-mail is not a noreply address")
                endif()
            endforeach()
            _pl_unescape("${author} ${committer}" names)
            _pl_check_line("commit ${hash} names" "${names}")
            _pl_unescape("${body}" body)
            _pl_check_text("commit ${hash} message" "${body}")
        endforeach()
    endif()
endif()

get_property(count GLOBAL PROPERTY PL_FINDINGS)
if(count GREATER 0)
    message(FATAL_ERROR "privacy-lint: ${count} finding(s) in ${REPO}")
endif()
message(STATUS "privacy-lint: clean (${MODE})")
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake "-DWORK_DIR=$env:TEMP/dvv-privacy-test" -P rawreel-workspace/tests/privacy/test_privacy_lint.cmake`
Expected: `-- privacy-lint tests passed`.

- [ ] **Step 5: Create the hook `rawreel-workspace/tools/githooks/pre-push`** (LF line endings, executable)

```sh
#!/bin/sh
# Privacy lint before every push (spec §13.3): tracked files plus the commits not yet on a remote.
# The local user and host name are added as extra terms; they are never written anywhere.
repo=$(git rev-parse --show-toplevel) || exit 1
hooks=$(cd "$(dirname "$0")" && pwd)
user=${USER:-${USERNAME:-}}
host=$(hostname 2>/dev/null || true)
exec cmake "-DREPO=$repo" -DMODE=prepush "-DEXTRA_TERMS=$user;$host" -P "$hooks/../privacy-lint.cmake"
```

- [ ] **Step 6: Lint this repository with the local user and host name**

Run: `cmake -DREPO=rawreel-workspace -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake`
Expected: `-- privacy-lint: clean (commits)`.

- [ ] **Step 7: Commit** (the hook needs the executable bit for Linux and CI)

```powershell
git -C rawreel-workspace add tools/privacy-lint.cmake tests/privacy/test_privacy_lint.cmake
git -C rawreel-workspace add --chmod=+x tools/githooks/pre-push
git -C rawreel-workspace commit -m "feat: add privacy lint and pre-push hook" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Third-party superbuild

**Files:**
- Create: `rawreel-workspace/cmake/DvvThirdParty.cmake`, `rawreel-workspace/deps/versions.cmake`, `rawreel-workspace/deps/CMakeLists.txt`, `rawreel-workspace/deps/write-notices.cmake`
- Test: `rawreel-workspace/tests/spdx/test_third_party_gate.cmake`

**Interfaces:**
- Consumes: `dvv_spdx_evaluate` (Task 2).
- Produces: `dvv_third_party(<name> VERSION URL SHA256 SPDX LINKAGE LICENSE_FILE [CMAKE_ARGS])`; the superbuild `cmake -S deps -B <build> -DDVV_THIRD_PARTY_PREFIX=<prefix>` installs packages `nlohmann_json` 3.12.0 and `doctest` 2.5.3 (target `doctest::doctest`) plus `<prefix>/THIRD_PARTY_NOTICES.txt`. Used by Task 5 (bootstrap) and every C++ build.

- [ ] **Step 1: Write the failing test** `rawreel-workspace/tests/spdx/test_third_party_gate.cmake`

```cmake
# dvv_third_party() must stop configuration for a disallowed licence and accept an allowed one.
# Run: cmake -P tests/spdx/test_third_party_gate.cmake
cmake_minimum_required(VERSION 4.2)
set(module "${CMAKE_CURRENT_LIST_DIR}/../../cmake/DvvThirdParty.cmake")
set(hash "0000000000000000000000000000000000000000000000000000000000000000")

if(DEFINED PROBE_SPDX)
    include("${module}")
    dvv_third_party(probe VERSION 1.0 URL https://example.com/probe.tar.gz SHA256 ${hash}
                    SPDX "${PROBE_SPDX}" LINKAGE static LICENSE_FILE LICENSE)
    message(STATUS "accepted")
    return()
endif()

function(expect spdx expect_failure expected_text)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DPROBE_SPDX=${spdx}" -P "${CMAKE_CURRENT_LIST_FILE}"
                    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(expect_failure AND status EQUAL 0)
        message(FATAL_ERROR "FAIL: '${spdx}' was accepted")
    endif()
    if(NOT expect_failure AND NOT status EQUAL 0)
        message(FATAL_ERROR "FAIL: '${spdx}' was rejected:\n${err}")
    endif()
    if(NOT "${out}${err}" MATCHES "${expected_text}")
        message(FATAL_ERROR "FAIL: output for '${spdx}' lacks '${expected_text}':\n${out}${err}")
    endif()
endfunction()

expect("GPL-3.0-only" 1 "Licence gate: probe 1.0 \\(GPL-3.0-only\\) rejected")
expect("MIT" 0 "accepted")
message(STATUS "third-party gate tests passed")
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake -P rawreel-workspace/tests/spdx/test_third_party_gate.cmake`
Expected: FAIL — `FAIL: output for 'GPL-3.0-only' lacks 'Licence gate: ...'`, because the probe stops at `include could not find requested file: .../cmake/DvvThirdParty.cmake`.

- [ ] **Step 3: Implement `rawreel-workspace/cmake/DvvThirdParty.cmake`**

```cmake
# Third-party dependency declarations (spec §13.2).
#
# dvv_third_party(<name> VERSION <v> URL <url> SHA256 <hash> SPDX <expression>
#                 LINKAGE static|runtime|tool LICENSE_FILE <path in archive>
#                 [CMAKE_ARGS <args>...])
# Records the dependency in global properties and fails configuration when its SPDX expression is
# not allowed for its linkage.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/DvvSpdx.cmake")

function(dvv_third_party name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "VERSION;URL;SHA256;SPDX;LINKAGE;LICENSE_FILE"
                          "CMAKE_ARGS")
    foreach(required IN ITEMS VERSION URL SHA256 SPDX LINKAGE LICENSE_FILE)
        if(NOT arg_${required})
            message(FATAL_ERROR "dvv_third_party(${name}): ${required} is required")
        endif()
    endforeach()
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "dvv_third_party(${name}): unexpected '${arg_UNPARSED_ARGUMENTS}'")
    endif()
    if(NOT arg_SHA256 MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR "dvv_third_party(${name}): SHA256 must be lower-case hex")
    endif()
    string(LENGTH "${arg_SHA256}" hash_length)
    if(NOT hash_length EQUAL 64)
        message(FATAL_ERROR "dvv_third_party(${name}): SHA256 must have 64 hex digits")
    endif()

    dvv_spdx_evaluate("${arg_SPDX}" "${arg_LINKAGE}" allowed reason)
    if(NOT allowed)
        message(FATAL_ERROR "Licence gate: ${name} ${arg_VERSION} (${arg_SPDX}) rejected: ${reason}")
    endif()

    set_property(GLOBAL APPEND PROPERTY DVV_THIRD_PARTY_NAMES "${name}")
    foreach(key IN ITEMS VERSION URL SHA256 SPDX LINKAGE LICENSE_FILE CMAKE_ARGS)
        set_property(GLOBAL PROPERTY "DVV_THIRD_PARTY_${name}_${key}" "${arg_${key}}")
    endforeach()
endfunction()
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake -P rawreel-workspace/tests/spdx/test_third_party_gate.cmake`
Expected: `-- third-party gate tests passed`.

- [ ] **Step 5: Create `rawreel-workspace/deps/versions.cmake`** (hashes verified against the release assets)

```cmake
# Every third-party dependency with version, download URL, SHA256 and SPDX licence (spec §13.2).
# Later milestones append wxWidgets, Vulkan-Headers/Vulkan-Hpp, vk-bootstrap and VMA here.

dvv_third_party(nlohmann_json
    VERSION 3.12.0
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    SHA256 42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    SPDX MIT
    LINKAGE static
    LICENSE_FILE LICENSE.MIT
    CMAKE_ARGS -DJSON_BuildTests=OFF -DJSON_Install=ON)

dvv_third_party(doctest
    VERSION 2.5.3
    URL https://github.com/doctest/doctest/releases/download/v2.5.3/doctest-v2.5.3.tar.gz
    SHA256 eb4a39729d091e43d4d593888bf4d145ee9a0f0e13f7404484b6f2fb60d5d50f
    SPDX MIT
    LINKAGE static
    LICENSE_FILE LICENSE.txt
    CMAKE_ARGS -DDOCTEST_WITH_TESTS=OFF -DDOCTEST_WITH_MAIN_IN_STATIC_LIB=OFF)
```

- [ ] **Step 6: Create `rawreel-workspace/deps/CMakeLists.txt`**

```cmake
# Third-party superbuild (spec §13.2): downloads, verifies, builds and installs every dependency
# from versions.cmake into DVV_THIRD_PARTY_PREFIX and writes THIRD_PARTY_NOTICES.txt there.
#
#   cmake -S deps -B <build dir> -DDVV_THIRD_PARTY_PREFIX=<install dir>
#   cmake --build <build dir> --config Release
cmake_minimum_required(VERSION 4.2)
project(dvv_third_party LANGUAGES CXX)

include(ExternalProject)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/DvvThirdParty.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/versions.cmake")

if(NOT DVV_THIRD_PARTY_PREFIX)
    message(FATAL_ERROR "Set DVV_THIRD_PARTY_PREFIX to the install directory")
endif()
cmake_path(ABSOLUTE_PATH DVV_THIRD_PARTY_PREFIX BASE_DIRECTORY "${CMAKE_BINARY_DIR}"
           NORMALIZE OUTPUT_VARIABLE prefix)

set(common_args
    "-DCMAKE_INSTALL_PREFIX=${prefix}"
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")

get_property(names GLOBAL PROPERTY DVV_THIRD_PARTY_NAMES)
set(notice_entries "")
foreach(name IN LISTS names)
    foreach(key IN ITEMS VERSION URL SHA256 SPDX LICENSE_FILE CMAKE_ARGS)
        get_property(${key} GLOBAL PROPERTY "DVV_THIRD_PARTY_${name}_${key}")
    endforeach()
    ExternalProject_Add(${name}
        URL "${URL}"
        URL_HASH "SHA256=${SHA256}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CMAKE_ARGS ${common_args} ${CMAKE_ARGS}
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --config Release
        INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR> --config Release)
    ExternalProject_Get_Property(${name} SOURCE_DIR)
    list(APPEND notice_entries "${name}|${VERSION}|${SPDX}|${URL}|${SOURCE_DIR}/${LICENSE_FILE}")
endforeach()

# $<SEMICOLON> keeps the list in one command-line argument.
string(REPLACE ";" "$<SEMICOLON>" notice_entries "${notice_entries}")
add_custom_target(third_party_notices ALL
    COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${prefix}/THIRD_PARTY_NOTICES.txt"
            "-DENTRIES=${notice_entries}"
            -P "${CMAKE_CURRENT_LIST_DIR}/write-notices.cmake"
    DEPENDS ${names}
    VERBATIM)
```

- [ ] **Step 7: Create `rawreel-workspace/deps/write-notices.cmake`**

```cmake
# Writes THIRD_PARTY_NOTICES.txt. Called by deps/CMakeLists.txt with
#   -DOUTPUT=<file> -DENTRIES=<name|version|spdx|url|licence file;...>
cmake_minimum_required(VERSION 4.2)
set(text "Third-party software used by rawreel and the dvv libraries\n")
foreach(entry IN LISTS ENTRIES)
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 name)
    list(GET fields 1 version)
    list(GET fields 2 spdx)
    list(GET fields 3 url)
    list(GET fields 4 licence_file)
    if(NOT EXISTS "${licence_file}")
        message(FATAL_ERROR "Licence file of ${name} not found")
    endif()
    file(READ "${licence_file}" licence)
    string(APPEND text "\n==============================================================\n"
                       "${name} ${version}\nLicence: ${spdx}\nSource: ${url}\n\n${licence}\n")
endforeach()
file(WRITE "${OUTPUT}" "${text}")
```

- [ ] **Step 8: Build the superbuild for MSVC and check the result**

```powershell
cmake -S rawreel-workspace/deps -B _build/third-party/msvc -G "Visual Studio 18 2026" -A x64 "-DDVV_THIRD_PARTY_PREFIX=$PWD/_third_party/msvc"
cmake --build _build/third-party/msvc --config Release
Get-ChildItem _third_party/msvc/lib/cmake, _third_party/msvc/share/cmake | ForEach-Object Name
Get-Content _third_party/msvc/THIRD_PARTY_NOTICES.txt -TotalCount 6
```

Expected: `doctest` and `nlohmann_json` package folders; the notices start with `Third-party software used by rawreel and the dvv libraries` and list `nlohmann_json 3.12.0 / Licence: MIT`.

- [ ] **Step 9: Commit**

```powershell
git -C rawreel-workspace add cmake/DvvThirdParty.cmake deps tests/spdx/test_third_party_gate.cmake
git -C rawreel-workspace commit -m "feat: add third-party superbuild with licence gate" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Shared presets and bootstrap scripts

**Files:**
- Create: `rawreel-workspace/cmake/DvvPresets.json`, `rawreel-workspace/repos.txt`, `rawreel-workspace/bootstrap.ps1`, `rawreel-workspace/bootstrap.sh`

**Interfaces:**
- Consumes: the superbuild (Task 4), the hook (Task 3).
- Produces: configure/build/test/workflow presets `vs2026`, `vs2026-release` (build/test only), `vs2026-asan`, `ninja-gcc`, `ninja-gcc-release`, `ninja-clang`, `ninja-clang-release`, `ninja-clang-asan`, `ninja-clang-tsan`; build trees at `../_build/<repo>/<preset>`; third-party prefixes `../_third_party/{msvc,gcc,clang}`; cache variables `DVV_BUILD_TESTS=ON`, `FETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER`, `FETCHCONTENT_SOURCE_DIR_DVV_BASE=../dvv-base`, `DVV_SANITIZE`. Every repository's `CMakePresets.json` includes `../rawreel-workspace/cmake/DvvPresets.json`. `bootstrap.ps1 [-Toolchain msvc|gcc|clang] [-NoClone] [-Force]`, `bootstrap.sh [--toolchain gcc|clang] [--no-clone] [--force]`.

- [ ] **Step 1: Create `rawreel-workspace/cmake/DvvPresets.json`**

```json
{
  "version": 6,
  "cmakeMinimumRequired": { "major": 4, "minor": 2, "patch": 0 },
  "configurePresets": [
    {
      "name": "dvv-base-settings",
      "hidden": true,
      "binaryDir": "${sourceDir}/../_build/${sourceDirName}/${presetName}",
      "cacheVariables": {
        "DVV_BUILD_TESTS": "ON",
        "FETCHCONTENT_TRY_FIND_PACKAGE_MODE": "NEVER",
        "FETCHCONTENT_SOURCE_DIR_DVV_BASE": "${sourceDir}/../dvv-base"
      }
    },
    {
      "name": "vs2026",
      "displayName": "Windows, Visual Studio 2026",
      "inherits": "dvv-base-settings",
      "generator": "Visual Studio 18 2026",
      "architecture": "x64",
      "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Windows" },
      "cacheVariables": { "CMAKE_PREFIX_PATH": "${sourceDir}/../_third_party/msvc" }
    },
    {
      "name": "vs2026-asan",
      "displayName": "Windows, Visual Studio 2026, AddressSanitizer",
      "inherits": "vs2026",
      "cacheVariables": { "DVV_SANITIZE": "address" }
    },
    {
      "name": "ninja-gcc",
      "displayName": "Linux, GCC 14",
      "inherits": "dvv-base-settings",
      "generator": "Ninja Multi-Config",
      "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Linux" },
      "cacheVariables": {
        "CMAKE_CXX_COMPILER": "g++-14",
        "CMAKE_PREFIX_PATH": "${sourceDir}/../_third_party/gcc"
      }
    },
    {
      "name": "ninja-clang",
      "displayName": "Linux, Clang 19",
      "inherits": "dvv-base-settings",
      "generator": "Ninja Multi-Config",
      "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Linux" },
      "cacheVariables": {
        "CMAKE_CXX_COMPILER": "clang++-19",
        "CMAKE_PREFIX_PATH": "${sourceDir}/../_third_party/clang",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },
    {
      "name": "ninja-clang-asan",
      "displayName": "Linux, Clang 19, AddressSanitizer + UndefinedBehaviorSanitizer",
      "inherits": "ninja-clang",
      "cacheVariables": { "DVV_SANITIZE": "address;undefined" }
    },
    {
      "name": "ninja-clang-tsan",
      "displayName": "Linux, Clang 19, ThreadSanitizer",
      "inherits": "ninja-clang",
      "cacheVariables": { "DVV_SANITIZE": "thread" }
    }
  ],
  "buildPresets": [
    { "name": "vs2026", "configurePreset": "vs2026", "configuration": "Debug" },
    { "name": "vs2026-release", "configurePreset": "vs2026", "configuration": "Release" },
    { "name": "vs2026-asan", "configurePreset": "vs2026-asan", "configuration": "Debug" },
    { "name": "ninja-gcc", "configurePreset": "ninja-gcc", "configuration": "Debug" },
    { "name": "ninja-gcc-release", "configurePreset": "ninja-gcc", "configuration": "Release" },
    { "name": "ninja-clang", "configurePreset": "ninja-clang", "configuration": "Debug" },
    { "name": "ninja-clang-release", "configurePreset": "ninja-clang", "configuration": "Release" },
    { "name": "ninja-clang-asan", "configurePreset": "ninja-clang-asan", "configuration": "Debug" },
    { "name": "ninja-clang-tsan", "configurePreset": "ninja-clang-tsan", "configuration": "Debug" }
  ],
  "testPresets": [
    {
      "name": "dvv-test-settings",
      "hidden": true,
      "output": { "outputOnFailure": true },
      "execution": { "noTestsAction": "error" },
      "environment": { "DVV_LEAK_CHECK": "fail" }
    },
    { "name": "vs2026", "inherits": "dvv-test-settings", "configurePreset": "vs2026", "configuration": "Debug" },
    { "name": "vs2026-release", "inherits": "dvv-test-settings", "configurePreset": "vs2026", "configuration": "Release" },
    { "name": "vs2026-asan", "inherits": "dvv-test-settings", "configurePreset": "vs2026-asan", "configuration": "Debug" },
    { "name": "ninja-gcc", "inherits": "dvv-test-settings", "configurePreset": "ninja-gcc", "configuration": "Debug" },
    { "name": "ninja-gcc-release", "inherits": "dvv-test-settings", "configurePreset": "ninja-gcc", "configuration": "Release" },
    { "name": "ninja-clang", "inherits": "dvv-test-settings", "configurePreset": "ninja-clang", "configuration": "Debug" },
    { "name": "ninja-clang-release", "inherits": "dvv-test-settings", "configurePreset": "ninja-clang", "configuration": "Release" },
    { "name": "ninja-clang-asan", "inherits": "dvv-test-settings", "configurePreset": "ninja-clang-asan", "configuration": "Debug" },
    { "name": "ninja-clang-tsan", "inherits": "dvv-test-settings", "configurePreset": "ninja-clang-tsan", "configuration": "Debug" }
  ],
  "workflowPresets": [
    {
      "name": "vs2026",
      "steps": [
        { "type": "configure", "name": "vs2026" },
        { "type": "build", "name": "vs2026" },
        { "type": "test", "name": "vs2026" },
        { "type": "build", "name": "vs2026-release" },
        { "type": "test", "name": "vs2026-release" }
      ]
    },
    {
      "name": "vs2026-asan",
      "steps": [
        { "type": "configure", "name": "vs2026-asan" },
        { "type": "build", "name": "vs2026-asan" },
        { "type": "test", "name": "vs2026-asan" }
      ]
    },
    {
      "name": "ninja-gcc",
      "steps": [
        { "type": "configure", "name": "ninja-gcc" },
        { "type": "build", "name": "ninja-gcc" },
        { "type": "test", "name": "ninja-gcc" },
        { "type": "build", "name": "ninja-gcc-release" },
        { "type": "test", "name": "ninja-gcc-release" }
      ]
    },
    {
      "name": "ninja-clang",
      "steps": [
        { "type": "configure", "name": "ninja-clang" },
        { "type": "build", "name": "ninja-clang" },
        { "type": "test", "name": "ninja-clang" },
        { "type": "build", "name": "ninja-clang-release" },
        { "type": "test", "name": "ninja-clang-release" }
      ]
    },
    {
      "name": "ninja-clang-asan",
      "steps": [
        { "type": "configure", "name": "ninja-clang-asan" },
        { "type": "build", "name": "ninja-clang-asan" },
        { "type": "test", "name": "ninja-clang-asan" }
      ]
    },
    {
      "name": "ninja-clang-tsan",
      "steps": [
        { "type": "configure", "name": "ninja-clang-tsan" },
        { "type": "build", "name": "ninja-clang-tsan" },
        { "type": "test", "name": "ninja-clang-tsan" }
      ]
    }
  ]
}
```

- [ ] **Step 2: Create `rawreel-workspace/repos.txt` without repositories yet** (Task 6 adds `dvv-base`)

```text
# Repositories that live next to rawreel-workspace, in dependency order. Later milestones append
# dvv-registry, dvv-platform, dvv-gpu, dvv-media, dvv-components and rawreel.
```

- [ ] **Step 3: Create `rawreel-workspace/bootstrap.ps1`**

```powershell
<#
.SYNOPSIS
    Prepares the rawreel workspace (spec §13.2).
.DESCRIPTION
    Clones missing sibling repositories listed in repos.txt, points every repository at the shared
    git hooks, copies the commit identity of rawreel-workspace to repositories without one, and builds
    the third-party dependencies into ../_third_party/<toolchain>. Nothing machine-specific is written
    into any repository.
.PARAMETER Toolchain
    msvc (default), gcc or clang.
.PARAMETER NoClone
    Fail instead of cloning a missing repository.
.PARAMETER Force
    Rebuild the third-party dependencies even if they are up to date.
#>
param(
    [ValidateSet('msvc', 'gcc', 'clang')]
    [string] $Toolchain = 'msvc',
    [switch] $NoClone,
    [switch] $Force
)
$ErrorActionPreference = 'Stop'

function Invoke-Native {
    param([string] $File, [string[]] $Arguments)
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File $($Arguments -join ' ') failed with exit code $LASTEXITCODE" }
}

$workspaceRepo = $PSScriptRoot
$root = Split-Path $workspaceRepo -Parent
$repos = Get-Content (Join-Path $workspaceRepo 'repos.txt') |
    ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') }

# Clone URL: DVV_GIT_BASE_URL, otherwise the origin of rawreel-workspace without its own name.
$baseUrl = $env:DVV_GIT_BASE_URL
if (-not $baseUrl) {
    $origin = git -C $workspaceRepo remote get-url origin 2>$null
    if ($LASTEXITCODE -eq 0 -and $origin) { $baseUrl = $origin -replace '/rawreel-workspace(\.git)?$', '' }
}
$userName = git -C $workspaceRepo config user.name
$userEmail = git -C $workspaceRepo config user.email

Invoke-Native git @('-C', $workspaceRepo, 'config', 'core.hooksPath', 'tools/githooks')
foreach ($repo in $repos) {
    $dir = Join-Path $root $repo
    if (-not (Test-Path $dir)) {
        if ($NoClone) { throw "${repo} is missing next to rawreel-workspace" }
        if (-not $baseUrl) { throw "Cannot clone ${repo}: set DVV_GIT_BASE_URL or add an origin remote" }
        Invoke-Native git @('clone', "$baseUrl/$repo.git", $dir)
    }
    Invoke-Native git @('-C', $dir, 'config', 'core.hooksPath', '../rawreel-workspace/tools/githooks')
    if ($userName -and -not (git -C $dir config --local user.name)) {
        Invoke-Native git @('-C', $dir, 'config', 'user.name', $userName)
    }
    if ($userEmail -and -not (git -C $dir config --local user.email)) {
        Invoke-Native git @('-C', $dir, 'config', 'user.email', $userEmail)
    }
}

# Third-party superbuild, skipped while deps/versions.cmake is unchanged.
$versions = Join-Path $workspaceRepo 'deps/versions.cmake'
$hash = (Get-FileHash $versions -Algorithm SHA256).Hash.ToLowerInvariant()
$prefix = Join-Path $root "_third_party/$Toolchain"
$stamp = Join-Path $prefix 'versions.sha256'
if (-not $Force -and (Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $hash)) {
    Write-Host "Third-party dependencies for $Toolchain are up to date."
    exit 0
}
$build = Join-Path $root "_build/third-party/$Toolchain"
$configure = @('-S', (Join-Path $workspaceRepo 'deps'), '-B', $build, "-DDVV_THIRD_PARTY_PREFIX=$prefix")
switch ($Toolchain) {
    'msvc' { $configure += @('-G', 'Visual Studio 18 2026', '-A', 'x64') }
    'gcc' { $configure += @('-G', 'Ninja', '-DCMAKE_CXX_COMPILER=g++-14') }
    'clang' { $configure += @('-G', 'Ninja', '-DCMAKE_CXX_COMPILER=clang++-19') }
}
Invoke-Native cmake $configure
Invoke-Native cmake @('--build', $build, '--config', 'Release')
Set-Content -Path $stamp -Value $hash -NoNewline
Write-Host "Third-party dependencies for $Toolchain installed."
```

- [ ] **Step 4: Create `rawreel-workspace/bootstrap.sh`** (LF, executable)

```sh
#!/bin/sh
# Prepares the rawreel workspace (spec §13.2); the Linux counterpart of bootstrap.ps1.
# Clones missing sibling repositories listed in repos.txt, points every repository at the shared git
# hooks, copies the commit identity of rawreel-workspace to repositories without one, and builds the
# third-party dependencies into ../_third_party/<toolchain>.
#
# Usage: ./bootstrap.sh [--toolchain gcc|clang] [--no-clone] [--force]
set -eu

toolchain=gcc
no_clone=0
force=0
while [ $# -gt 0 ]; do
    case "$1" in
        --toolchain) toolchain=$2; shift 2 ;;
        --no-clone) no_clone=1; shift ;;
        --force) force=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
case "$toolchain" in
    gcc) compiler=g++-14 ;;
    clang) compiler=clang++-19 ;;
    *) echo "toolchain must be gcc or clang" >&2; exit 2 ;;
esac

workspace_repo=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$workspace_repo")

base_url=${DVV_GIT_BASE_URL:-}
if [ -z "$base_url" ]; then
    origin=$(git -C "$workspace_repo" remote get-url origin 2>/dev/null || true)
    base_url=$(printf '%s' "$origin" | sed -E 's#/rawreel-workspace(\.git)?$##')
fi
user_name=$(git -C "$workspace_repo" config user.name || true)
user_email=$(git -C "$workspace_repo" config user.email || true)

git -C "$workspace_repo" config core.hooksPath tools/githooks
grep -v '^[[:space:]]*#' "$workspace_repo/repos.txt" | while read -r repo; do
    [ -n "$repo" ] || continue
    dir="$root/$repo"
    if [ ! -d "$dir" ]; then
        [ "$no_clone" -eq 0 ] || { echo "$repo is missing next to rawreel-workspace" >&2; exit 1; }
        [ -n "$base_url" ] || { echo "Cannot clone $repo: set DVV_GIT_BASE_URL" >&2; exit 1; }
        git clone "$base_url/$repo.git" "$dir"
    fi
    git -C "$dir" config core.hooksPath ../rawreel-workspace/tools/githooks
    if [ -n "$user_name" ] && ! git -C "$dir" config --local user.name >/dev/null; then
        git -C "$dir" config user.name "$user_name"
    fi
    if [ -n "$user_email" ] && ! git -C "$dir" config --local user.email >/dev/null; then
        git -C "$dir" config user.email "$user_email"
    fi
done

hash=$(sha256sum "$workspace_repo/deps/versions.cmake" | cut -d' ' -f1)
prefix="$root/_third_party/$toolchain"
stamp="$prefix/versions.sha256"
if [ "$force" -eq 0 ] && [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$hash" ]; then
    echo "Third-party dependencies for $toolchain are up to date."
    exit 0
fi
build="$root/_build/third-party/$toolchain"
cmake -S "$workspace_repo/deps" -B "$build" -G Ninja "-DCMAKE_CXX_COMPILER=$compiler" \
    "-DDVV_THIRD_PARTY_PREFIX=$prefix"
cmake --build "$build" --config Release
printf '%s' "$hash" > "$stamp"
echo "Third-party dependencies for $toolchain installed."
```

- [ ] **Step 5: Run bootstrap twice on Windows**

```powershell
./rawreel-workspace/bootstrap.ps1
./rawreel-workspace/bootstrap.ps1
git -C rawreel-workspace config core.hooksPath
```

Expected: first run ends with `Third-party dependencies for msvc installed.`, the second with `... are up to date.`; the hooks path is `tools/githooks`.

- [ ] **Step 6: Run bootstrap in WSL**

Run: `wsl -d Ubuntu-24.04 -e bash -lc './rawreel-workspace/bootstrap.sh --toolchain clang'`
Expected: ends with `installed.`; `_third_party/clang` exists.

- [ ] **Step 7: Commit**

```powershell
git -C rawreel-workspace add cmake/DvvPresets.json repos.txt bootstrap.ps1
git -C rawreel-workspace add --chmod=+x bootstrap.sh
git -C rawreel-workspace commit -m "feat: add shared presets and bootstrap scripts" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: dvv-base repository skeleton with `Error` and environment access

**Files:**
- Create: repository `dvv-base` (sibling of `rawreel-workspace`) with `.gitattributes`, `.gitignore` (same content as the workspace files from Task 1), `LICENSE` (same text as Task 1), `.clang-format`, `.clang-tidy`, `README.md`, `CMakePresets.json`, `CMakeLists.txt`, `cmake/DvvCompilerOptions.cmake`, `cmake/dvv_baseConfig.cmake.in`, `include/dvv/base/error.h`, `include/dvv/base/environment.h`, `src/error.cpp`, `src/environment.cpp`, `tests/CMakeLists.txt`, `tests/main.cpp`, `tests/test_error.cpp`, `tests/package/CMakeLists.txt`, `tests/package/main.cpp`, `tests/package/run-package-test.cmake`
- Modify: `rawreel-workspace/repos.txt`

**Interfaces:**
- Produces (C++): `enum class dvv::Errc : std::uint8_t { Io, Format, Unsupported, OutOfMemory, DeviceLost, ShaderCompile, Cancelled, Internal }`; `std::string_view dvv::errcName(Errc) noexcept`; `std::string_view dvv::sourceFileName(const std::source_location&) noexcept`; `struct dvv::Error { Errc code; std::string message; std::source_location where; std::shared_ptr<const Error> cause; Error wrap(Errc, std::string, std::source_location = current()) const; std::string describe() const; }`; `template <class T = void> using dvv::Result = std::expected<T, Error>`; `std::unexpected<Error> dvv::makeError(Errc, std::string, std::source_location = current())`; macros `DVV_TRY(declaration, expression)`, `DVV_TRY_VOID(expression)`; `std::optional<std::string> dvv::getEnvironment(std::string_view)`; `bool dvv::setEnvironment(std::string_view, std::string_view)` (empty value removes).
- Produces (CMake): target `dvv_base` / alias `dvv::base`, package `dvv_base` 0.1.0 (`SameMinorVersion`), option `DVV_BUILD_TESTS`, cache variable `DVV_SANITIZE`, variable `DVV_TEST_ENVIRONMENT`, function `dvv_set_compiler_options(<target>)`; `DvvCompilerOptions.cmake` installed next to the package config.

- [ ] **Step 1: Create the repository and its identity**

```powershell
git init -b main dvv-base
Copy-Item rawreel-workspace/.gitattributes, rawreel-workspace/.gitignore, rawreel-workspace/LICENSE dvv-base/
```

Append `dvv-base` as the last line of `rawreel-workspace/repos.txt`, then run `./rawreel-workspace/bootstrap.ps1 -NoClone`. It copies `user.name`/`user.email` from rawreel-workspace into dvv-base and sets its hooks path.
Check: `git -C dvv-base config user.email` ends with `@users.noreply.github.com`; `git -C dvv-base config core.hooksPath` is `../rawreel-workspace/tools/githooks`.

- [ ] **Step 2: Create `dvv-base/.clang-format`**

```yaml
---
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 100
AccessModifierOffset: -4
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false
AllowShortCaseLabelsOnASingleLine: true
DerivePointerAlignment: false
PointerAlignment: Left
IncludeBlocks: Regroup
IncludeCategories:
  - Regex: '^<doctest/'
    Priority: 1
  - Regex: '^<[a-z_]+>$'
    Priority: 2
  - Regex: '^<'
    Priority: 3
  - Regex: '^"'
    Priority: 4
...
```

- [ ] **Step 3: Create `dvv-base/.clang-tidy`**

```yaml
---
# Naming follows spec §14; checks stay modest so findings are worth fixing.
Checks: >
  -*,
  bugprone-*,
  -bugprone-easily-swappable-parameters,
  performance-*,
  readability-identifier-naming
WarningsAsErrors: '*'
HeaderFilterRegex: '.*/include/dvv/.*'
CheckOptions:
  readability-identifier-naming.NamespaceCase: lower_case
  readability-identifier-naming.ClassCase: CamelCase
  readability-identifier-naming.StructCase: CamelCase
  readability-identifier-naming.EnumCase: CamelCase
  readability-identifier-naming.EnumConstantCase: CamelCase
  readability-identifier-naming.TypeAliasCase: CamelCase
  readability-identifier-naming.TypeAliasIgnoredRegexp: '^promise_type$'
  readability-identifier-naming.ClassIgnoredRegexp: '^promise_type$'
  readability-identifier-naming.TemplateParameterCase: CamelCase
  readability-identifier-naming.FunctionCase: camelBack
  readability-identifier-naming.MethodCase: camelBack
  readability-identifier-naming.MethodIgnoredRegexp: '^(get_return_object|initial_suspend|final_suspend|return_value|return_void|unhandled_exception|await_ready|await_suspend|await_resume)$'
  readability-identifier-naming.VariableCase: lower_case
  readability-identifier-naming.ParameterCase: lower_case
  readability-identifier-naming.PublicMemberCase: camelBack
  readability-identifier-naming.PrivateMemberCase: lower_case
  readability-identifier-naming.PrivateMemberSuffix: _
  readability-identifier-naming.ProtectedMemberCase: lower_case
  readability-identifier-naming.ProtectedMemberSuffix: _
  readability-identifier-naming.ConstexprVariableCase: UPPER_CASE
  readability-identifier-naming.GlobalConstantCase: UPPER_CASE
  readability-identifier-naming.StaticConstantCase: UPPER_CASE
  readability-identifier-naming.MacroDefinitionCase: UPPER_CASE
  readability-identifier-naming.MacroDefinitionIgnoredRegexp: '^co_try$'
...
```

- [ ] **Step 4: Create `dvv-base/README.md`**

````markdown
# dvv-base

Foundation library of the dvv stack used by rawreel: errors (`Result`, `DVV_TRY`, `co_try`),
factory-only creation with a lifetime registry and leak check, coroutine tasks and executors,
logging, a JSON wrapper, a URI parser and per-user paths. C++23, no dependencies beyond the standard
library and nlohmann/json.

Build it through [rawreel-workspace](https://github.com/BarabashkaD/rawreel-workspace), which
provides the presets, the third-party dependencies and the CI workflow:

```sh
cmake --workflow --preset vs2026     # Linux: ninja-gcc or ninja-clang
```

Licence: MIT.
````

- [ ] **Step 5: Create `dvv-base/CMakePresets.json`**

```json
{
  "version": 6,
  "include": [ "../rawreel-workspace/cmake/DvvPresets.json" ]
}
```

- [ ] **Step 6: Create `dvv-base/cmake/DvvCompilerOptions.cmake`**

```cmake
# Shared compiler settings for every dvv repository (spec §2, §13.3). Installed with dvv_base so
# higher repositories include the same file through find_package(dvv_base) or the workspace.
include_guard(DIRECTORY)

# CMake maps C++23 to /std:c++latest for MSVC, which follows C++26 drafts; use the C++23 switch.
if(MSVC)
    set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "-std:c++23preview")
    set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "-std:c++23preview")
endif()

set(DVV_SANITIZE "" CACHE STRING "Sanitizers: empty, address, address;undefined or thread")
# Tests use it as ENVIRONMENT_MODIFICATION (the MSVC ASan runtime DLL lives next to cl.exe).
set(DVV_TEST_ENVIRONMENT "")
if(DVV_SANITIZE)
    if(MSVC)
        if(NOT DVV_SANITIZE STREQUAL "address")
            message(FATAL_ERROR "MSVC supports only DVV_SANITIZE=address")
        endif()
        add_compile_options(/fsanitize=address /Zi)
        add_link_options(/INCREMENTAL:NO)
        cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH msvc_bin_dir)
        set(DVV_TEST_ENVIRONMENT "PATH=path_list_prepend:${msvc_bin_dir}")
    else()
        list(JOIN DVV_SANITIZE "," sanitizers)
        add_compile_options(-fsanitize=${sanitizers} -fno-omit-frame-pointer
                            -fno-sanitize-recover=all)
        add_link_options(-fsanitize=${sanitizers})
    endif()
endif()

# Maps the workspace root out of __FILE__ and std::source_location so binaries carry no machine path.
cmake_path(GET CMAKE_SOURCE_DIR PARENT_PATH DVV_PATH_MAP_ROOT)

# dvv_set_compiler_options(<target>): C++23, no module scanning, warnings as errors for our code.
function(dvv_set_compiler_options target)
    target_compile_features(${target} PUBLIC cxx_std_23)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF CXX_SCAN_FOR_MODULES OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /WX /permissive- /utf-8 /EHsc /Zc:__cplusplus /Zc:preprocessor
            /external:anglebrackets /external:W0)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror "-ffile-prefix-map=${DVV_PATH_MAP_ROOT}/=")
    endif()
endfunction()
```

- [ ] **Step 7: Create `dvv-base/cmake/dvv_baseConfig.cmake.in`**

```cmake
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)
find_dependency(Threads)
find_dependency(nlohmann_json 3.12)

include("${CMAKE_CURRENT_LIST_DIR}/dvv_baseTargets.cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

check_required_components(dvv_base)
```

- [ ] **Step 8: Create `dvv-base/CMakeLists.txt`** with only the first two sources (later tasks add theirs, keeping the list alphabetical; Task 16 shows the final file)

```cmake
cmake_minimum_required(VERSION 4.2)
project(dvv_base VERSION 0.1.0 DESCRIPTION "dvv base library" LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DvvCompilerOptions)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(DVV_BUILD_TESTS "Build the dvv tests" ${PROJECT_IS_TOP_LEVEL})

find_package(Threads REQUIRED)
find_package(nlohmann_json 3.12 REQUIRED CONFIG)

add_library(dvv_base STATIC
    src/environment.cpp
    src/error.cpp)
add_library(dvv::base ALIAS dvv_base)
set_target_properties(dvv_base PROPERTIES EXPORT_NAME base)
target_include_directories(dvv_base PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(dvv_base PUBLIC Threads::Threads nlohmann_json::nlohmann_json)
if(WIN32)
    target_link_libraries(dvv_base PRIVATE shell32 ole32)
endif()
dvv_set_compiler_options(dvv_base)

install(TARGETS dvv_base EXPORT dvv_baseTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
set(config_dir ${CMAKE_INSTALL_LIBDIR}/cmake/dvv_base)
install(EXPORT dvv_baseTargets NAMESPACE dvv:: DESTINATION ${config_dir})
configure_package_config_file(cmake/dvv_baseConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/dvv_baseConfig.cmake INSTALL_DESTINATION ${config_dir})
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/dvv_baseConfigVersion.cmake
    COMPATIBILITY SameMinorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/dvv_baseConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/dvv_baseConfigVersion.cmake
    cmake/DvvCompilerOptions.cmake
    DESTINATION ${config_dir})

if(DVV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Step 9: Create the test harness** — `dvv-base/tests/main.cpp` (Task 9 replaces it with the leak-checking main):

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
```

and `dvv-base/tests/CMakeLists.txt` (Task 9 adds the compile-fail block; later tasks add test files):

```cmake
find_package(doctest 2.5 REQUIRED CONFIG)

add_executable(dvv_base_tests
    main.cpp
    test_error.cpp)
target_link_libraries(dvv_base_tests PRIVATE dvv::base doctest::doctest)
# doctest otherwise only forward-declares std::ostream, which breaks printing std types.
target_compile_definitions(dvv_base_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
dvv_set_compiler_options(dvv_base_tests)
add_test(NAME dvv_base.unit COMMAND dvv_base_tests)
set_tests_properties(dvv_base.unit PROPERTIES
    ENVIRONMENT DVV_LEAK_CHECK=fail
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")

# Package test (spec §3.1): install, then build a consumer with find_package(dvv_base).
# Skipped for sanitizer builds, whose consumers would need the same runtime flags.
if(NOT DVV_SANITIZE)
    string(REPLACE ";" "," deps_prefix_path "${CMAKE_PREFIX_PATH}")
    add_test(NAME dvv_base.package
             COMMAND ${CMAKE_COMMAND}
                     -DBUILD_DIR=${CMAKE_BINARY_DIR}
                     -DCONFIG=$<CONFIG>
                     -DPREFIX=${CMAKE_CURRENT_BINARY_DIR}/package-prefix
                     -DCONSUMER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/package
                     -DCONSUMER_BUILD=${CMAKE_CURRENT_BINARY_DIR}/package-consumer
                     -DGENERATOR=${CMAKE_GENERATOR}
                     -DPLATFORM=${CMAKE_GENERATOR_PLATFORM}
                     -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
                     -DDEPS_PREFIX_PATH=${deps_prefix_path}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/package/run-package-test.cmake)
endif()
```

- [ ] **Step 10: Create the package test files**

`dvv-base/tests/package/CMakeLists.txt`:

```cmake
# Consumer of the installed dvv_base package; built by run-package-test.cmake.
cmake_minimum_required(VERSION 4.2)
project(dvv_base_package_consumer LANGUAGES CXX)

find_package(dvv_base 0.1 REQUIRED)
include(DvvCompilerOptions)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE dvv::base)
dvv_set_compiler_options(consumer)
```

`dvv-base/tests/package/main.cpp`:

```cpp
#include <cstdio>

#include "dvv/base/error.h"

int main() {
    const dvv::Result<int> result = dvv::makeError(dvv::Errc::Format, "probe");
    if (result.has_value() || result.error().code != dvv::Errc::Format) {
        return 1;
    }
    std::puts("dvv_base package works");
    return 0;
}
```

`dvv-base/tests/package/run-package-test.cmake`:

```cmake
# Installs the build tree into a scratch prefix, then configures, builds and runs the consumer.
# Called by the dvv_base.package test with -DBUILD_DIR, -DCONFIG, -DPREFIX, -DCONSUMER_SOURCE,
# -DCONSUMER_BUILD, -DGENERATOR, -DPLATFORM, -DCXX_COMPILER and -DDEPS_PREFIX_PATH.
cmake_minimum_required(VERSION 4.2)

function(run)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "failed (${status}): ${ARGV}")
    endif()
endfunction()

file(REMOVE_RECURSE "${PREFIX}" "${CONSUMER_BUILD}")
run("${CMAKE_COMMAND}" --install "${BUILD_DIR}" --config "${CONFIG}" --prefix "${PREFIX}")

# A list value cannot pass through a command line intact; an initial-cache file keeps it whole.
string(REPLACE "," ";" deps_prefix_path "${DEPS_PREFIX_PATH}")
set(initial_cache "${CONSUMER_BUILD}-cache.cmake")
file(WRITE "${initial_cache}"
     "set(CMAKE_PREFIX_PATH \"${PREFIX};${deps_prefix_path}\" CACHE STRING \"\")\n")
set(configure "${CMAKE_COMMAND}" -C "${initial_cache}" -S "${CONSUMER_SOURCE}"
    -B "${CONSUMER_BUILD}" -G "${GENERATOR}" "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
    "-DCMAKE_BUILD_TYPE=${CONFIG}")
if(PLATFORM)
    list(APPEND configure -A "${PLATFORM}")
endif()
run(${configure})
run("${CMAKE_COMMAND}" --build "${CONSUMER_BUILD}" --config "${CONFIG}")

file(GLOB_RECURSE consumer "${CONSUMER_BUILD}/consumer" "${CONSUMER_BUILD}/consumer.exe")
list(FILTER consumer EXCLUDE REGEX "CMakeFiles")
if(NOT consumer)
    message(FATAL_ERROR "consumer executable not found")
endif()
list(GET consumer 0 consumer)
run("${consumer}")
```

- [ ] **Step 11: Write the failing test** `dvv-base/tests/test_error.cpp`

```cpp
#include <doctest/doctest.h>

#include <charconv>
#include <string>
#include <string_view>

#include "dvv/base/error.h"

namespace {

dvv::Result<int> parseNumber(std::string_view text) {
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return dvv::makeError(dvv::Errc::Format, "not a number");
    }
    return value;
}

dvv::Result<int> doubled(std::string_view text) {
    DVV_TRY(const int value, parseNumber(text));
    return value * 2;
}

dvv::Result<void> mustBePositive(int value) {
    if (value <= 0) {
        return dvv::makeError(dvv::Errc::Format, "not positive");
    }
    return {};
}

dvv::Result<int> checkedDouble(int value) {
    DVV_TRY_VOID(mustBePositive(value));
    return value * 2;
}

}  // namespace

TEST_CASE("Errc names") {
    CHECK(dvv::errcName(dvv::Errc::Io) == "Io");
    CHECK(dvv::errcName(dvv::Errc::Format) == "Format");
    CHECK(dvv::errcName(dvv::Errc::Cancelled) == "Cancelled");
}

TEST_CASE("makeError fills code, message and location") {
    const int line = __LINE__ + 1;
    const dvv::Result<int> result = dvv::makeError(dvv::Errc::Io, "read failed");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == dvv::Errc::Io);
    CHECK(result.error().message == "read failed");
    CHECK(result.error().where.line() == static_cast<std::uint_least32_t>(line));
    CHECK(result.error().cause == nullptr);
}

TEST_CASE("wrap chains errors and describe shows the chain with file names only") {
    const dvv::Error inner{dvv::Errc::Io, "read failed", std::source_location::current(), nullptr};
    const dvv::Error outer = inner.wrap(dvv::Errc::Format, "bad header");
    REQUIRE(outer.cause != nullptr);
    CHECK(outer.cause->message == "read failed");
    const std::string text = outer.describe();
    CHECK(text.starts_with("Format: bad header [test_error.cpp:"));
    CHECK(text.find(" <- Io: read failed [test_error.cpp:") != std::string::npos);
    CHECK(text.find('/') == std::string::npos);
    CHECK(text.find('\\') == std::string::npos);
}

TEST_CASE("DVV_TRY returns the value or the error") {
    CHECK(doubled("21").value() == 42);
    const auto failed = doubled("x");
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().message == "not a number");
}

TEST_CASE("DVV_TRY_VOID returns the error early") {
    CHECK(checkedDouble(4).value() == 8);
    CHECK(checkedDouble(-1).error().message == "not positive");
}
```

- [ ] **Step 12: Run it to verify it fails**

Run: `cmake --workflow --preset vs2026` in `dvv-base` (PowerShell: `Push-Location dvv-base; cmake --workflow --preset vs2026; Pop-Location`)
Expected: FAIL — `Cannot find source file: src/environment.cpp` (the library sources come in Step 13).

- [ ] **Step 13: Implement** `dvv-base/include/dvv/base/error.h`

```cpp
#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace dvv {

enum class Errc : std::uint8_t {
    Io,
    Format,
    Unsupported,
    OutOfMemory,
    DeviceLost,
    ShaderCompile,
    Cancelled,
    Internal,
};

[[nodiscard]] std::string_view errcName(Errc code) noexcept;

// File name without directories, for messages and logs.
[[nodiscard]] std::string_view sourceFileName(const std::source_location& where) noexcept;

struct Error {
    Errc code = Errc::Internal;
    std::string message;  // for people; includes file/frame/component context
    std::source_location where;
    std::shared_ptr<const Error> cause;  // chain

    // Returns a new error whose cause is this one.
    [[nodiscard]] Error wrap(
        Errc outer_code, std::string outer_message,
        std::source_location outer_where = std::source_location::current()) const;

    // "Format: bad header [uri.cpp:42] <- Io: read failed [file.cpp:10]"
    [[nodiscard]] std::string describe() const;
};

template <class T = void>
using Result = std::expected<T, Error>;

[[nodiscard]] std::unexpected<Error> makeError(
    Errc code, std::string message, std::source_location where = std::source_location::current());

}  // namespace dvv

#define DVV_DETAIL_CONCAT_INNER(a, b) a##b
#define DVV_DETAIL_CONCAT(a, b) DVV_DETAIL_CONCAT_INNER(a, b)

// Outside coroutines: DVV_TRY(auto value, parse(text)); returns the error early.
#define DVV_TRY(declaration, expression)                                                  \
    auto DVV_DETAIL_CONCAT(dvv_try_, __LINE__) = (expression);                            \
    if (!DVV_DETAIL_CONCAT(dvv_try_, __LINE__)) {                                         \
        return std::unexpected(std::move(DVV_DETAIL_CONCAT(dvv_try_, __LINE__)).error()); \
    }                                                                                     \
    declaration = std::move(*DVV_DETAIL_CONCAT(dvv_try_, __LINE__))

// Outside coroutines, for Result<void>: DVV_TRY_VOID(writeFile(path));
#define DVV_TRY_VOID(expression)                                      \
    do {                                                              \
        auto dvv_try_void_ = (expression);                            \
        if (!dvv_try_void_) {                                         \
            return std::unexpected(std::move(dvv_try_void_).error()); \
        }                                                             \
    } while (false)
```

`dvv-base/src/error.cpp`:

```cpp
#include "dvv/base/error.h"

#include <format>

namespace dvv {

std::string_view errcName(Errc code) noexcept {
    switch (code) {
        case Errc::Io: return "Io";
        case Errc::Format: return "Format";
        case Errc::Unsupported: return "Unsupported";
        case Errc::OutOfMemory: return "OutOfMemory";
        case Errc::DeviceLost: return "DeviceLost";
        case Errc::ShaderCompile: return "ShaderCompile";
        case Errc::Cancelled: return "Cancelled";
        case Errc::Internal: return "Internal";
    }
    return "Unknown";
}

std::string_view sourceFileName(const std::source_location& where) noexcept {
    std::string_view path = where.file_name();
    const auto slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

Error Error::wrap(Errc outer_code, std::string outer_message,
                  std::source_location outer_where) const {
    return Error{outer_code, std::move(outer_message), outer_where,
                 std::make_shared<const Error>(*this)};
}

std::string Error::describe() const {
    std::string text =
        std::format("{}: {} [{}:{}]", errcName(code), message, sourceFileName(where), where.line());
    for (const Error* next = cause.get(); next != nullptr; next = next->cause.get()) {
        text += std::format(" <- {}: {} [{}:{}]", errcName(next->code), next->message,
                            sourceFileName(next->where), next->where.line());
    }
    return text;
}

std::unexpected<Error> makeError(Errc code, std::string message, std::source_location where) {
    return std::unexpected(Error{code, std::move(message), where, nullptr});
}

}  // namespace dvv
```

`dvv-base/include/dvv/base/environment.h`:

```cpp
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace dvv {

// Reads an environment variable; nullopt when it is not set.
[[nodiscard]] std::optional<std::string> getEnvironment(std::string_view name);

// Sets (or, with an empty value, removes) an environment variable. Returns false on failure.
bool setEnvironment(std::string_view name, std::string_view value);

}  // namespace dvv
```

`dvv-base/src/environment.cpp`:

```cpp
#include "dvv/base/environment.h"

#include <cstdlib>

namespace dvv {

std::optional<std::string> getEnvironment(std::string_view name) {
    const std::string key(name);
#if defined(_WIN32)
    char* buffer = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&buffer, &size, key.c_str()) != 0 || buffer == nullptr) {
        return std::nullopt;
    }
    std::string value(buffer);
    std::free(buffer);
    return value;
#else
    const char* value = std::getenv(key.c_str());
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

bool setEnvironment(std::string_view name, std::string_view value) {
    const std::string key(name);
    const std::string text(value);
#if defined(_WIN32)
    return _putenv_s(key.c_str(), text.c_str()) == 0;
#else
    if (text.empty()) {
        return unsetenv(key.c_str()) == 0;
    }
    return setenv(key.c_str(), text.c_str(), 1) == 0;
#endif
}

}  // namespace dvv
```

- [ ] **Step 14: Run the tests to verify they pass**

Run: `Push-Location dvv-base; cmake --workflow --preset vs2026; Pop-Location`
Expected: Debug and Release each end with `100% tests passed, 0 tests failed out of 2` (`dvv_base.unit`, `dvv_base.package`; the package test prints `dvv_base package works`). Check the C++ standard: `Select-String -Path _build/dvv-base/vs2026/dvv_base.vcxproj -Pattern 'LanguageStandard>' | Select-Object -First 1` → `<LanguageStandard>stdcpp23</LanguageStandard>`.

- [ ] **Step 15: Commit** (both repositories)

```powershell
git -C dvv-base add -A
git -C dvv-base commit -m "feat: add dvv-base skeleton with Error, Result and environment access" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git -C rawreel-workspace add repos.txt
git -C rawreel-workspace commit -m "chore: list dvv-base as a workspace repository" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Workspace build of all repositories

**Files:**
- Create: `rawreel-workspace/cmake/dvv-versions.cmake`, `rawreel-workspace/CMakeLists.txt`, `rawreel-workspace/CMakePresets.json`

**Interfaces:**
- Consumes: presets (Task 5), dvv-base (Task 6), tool tests (Tasks 2–4).
- Produces: `FetchContent_Declare(dvv_base ...)` with `DVV_GIT_BASE_URL` (cache, default `https://github.com/BarabashkaD`), `dvv_check_siblings(<name>...)`; CTest tests `workspace.spdx`, `workspace.third_party_gate`, `workspace.privacy_lint` plus every dvv-base test.

- [ ] **Step 1: Write the failing check**

Run: `Push-Location rawreel-workspace; cmake --workflow --preset vs2026; Pop-Location`
Expected: FAIL — `CMake Error: The source directory ... does not appear to contain CMakeLists.txt` (or no `CMakePresets.json`).

- [ ] **Step 2: Create `rawreel-workspace/cmake/dvv-versions.cmake`**

```cmake
# Declares every dvv repository first, so CMake's "first declaration wins" rule picks one version
# for diamonds (spec §3.1). The workspace presets point FETCHCONTENT_SOURCE_DIR_<NAME> at the sibling
# folders and set FETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER, so no installed package can shadow them.
# Until v1.0 every repository is used at main; tags start with v1.0.
include_guard(GLOBAL)
include(FetchContent)

set(DVV_GIT_BASE_URL "https://github.com/BarabashkaD" CACHE STRING
    "Base URL of the dvv repositories")

FetchContent_Declare(dvv_base
    GIT_REPOSITORY "${DVV_GIT_BASE_URL}/dvv-base.git"
    GIT_TAG main
    FIND_PACKAGE_ARGS 0.1)

# dvv_check_siblings(<name>...): fails unless each dependency was taken from its sibling folder.
function(dvv_check_siblings)
    cmake_path(GET CMAKE_SOURCE_DIR PARENT_PATH root)
    foreach(name IN LISTS ARGN)
        string(REPLACE "_" "-" folder "${name}")
        cmake_path(APPEND root "${folder}" OUTPUT_VARIABLE expected)
        cmake_path(NORMAL_PATH expected)
        set(actual "${${name}_SOURCE_DIR}")
        cmake_path(NORMAL_PATH actual)
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "${name} was taken from '${actual}', expected the sibling folder "
                                "'${folder}'. Run bootstrap and configure through a workspace preset.")
        endif()
    endforeach()
endfunction()
```

- [ ] **Step 3: Create `rawreel-workspace/CMakeLists.txt`**

```cmake
# The whole workspace as one build (one Visual Studio solution): every dvv repository from its
# sibling folder, plus the workspace's own tool tests.
cmake_minimum_required(VERSION 4.2)
project(rawreel_workspace LANGUAGES CXX)

enable_testing()
include(cmake/dvv-versions.cmake)

FetchContent_MakeAvailable(dvv_base)
dvv_check_siblings(dvv_base)

add_test(NAME workspace.spdx
         COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/spdx/test_spdx.cmake)
add_test(NAME workspace.third_party_gate
         COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/spdx/test_third_party_gate.cmake)
add_test(NAME workspace.privacy_lint
         COMMAND ${CMAKE_COMMAND} -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/privacy-lint-test
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/privacy/test_privacy_lint.cmake)
```

- [ ] **Step 4: Create `rawreel-workspace/CMakePresets.json`**

```json
{
  "version": 6,
  "include": [ "cmake/DvvPresets.json" ]
}
```

- [ ] **Step 5: Run the workspace workflow**

Run: `Push-Location rawreel-workspace; cmake --workflow --preset vs2026; Pop-Location`
Expected: Debug and Release: `100% tests passed, 0 tests failed out of 5` (3 workspace tests + `dvv_base.unit` + `dvv_base.package`). `Select-String -Path _build/rawreel-workspace/vs2026/_deps/dvv_base-build/dvv_base.vcxproj -Pattern 'LanguageStandard>' | Select-Object -First 1` shows `stdcpp23`, proving the MSVC switch also applies inside the workspace build.

- [ ] **Step 6: Commit**

```powershell
git -C rawreel-workspace add CMakeLists.txt CMakePresets.json cmake/dvv-versions.cmake
git -C rawreel-workspace commit -m "feat: build all repositories as one workspace" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Lifetime registry and factory-only creation (spec §9.4)

**Files:**
- Create: `dvv-base/include/dvv/base/lifetime.h`, `dvv-base/src/lifetime.cpp`, `dvv-base/include/dvv/base/testing/test_authority.h`
- Test: `dvv-base/tests/test_lifetime.cpp` (the leak-check test case is added in Task 9)
- Modify: `dvv-base/CMakeLists.txt` (add `src/lifetime.cpp`), `dvv-base/tests/CMakeLists.txt` (add `test_lifetime.cpp`)

**Interfaces:**
- Produces: `using dvv::ObjectId = std::uint64_t`; `inline constexpr ObjectId dvv::NO_OWNER = 0`; `struct TypeRecord { std::string name; std::atomic<std::uint64_t> created; std::atomic<std::int64_t> live; }`; `struct TypeStats { std::string_view name; std::uint64_t created; std::int64_t live; }`; `struct LiveObjectInfo { std::string_view type; ObjectId id; ObjectId owner; std::optional<std::source_location> createdAt; std::optional<std::chrono::system_clock::time_point> createdTime; }`; move-only `LifetimeToken` (`id()`, `type()`); `template <class F> class CreationKey` (only `F` constructs; `LifetimeToken take() &&`); `class LifetimeRegistry` (`instance()`, `issue<T>(ObjectId owner = NO_OWNER, std::source_location = current())`, `setDetailed(bool)`, `detailed()`, `liveCount()`, `liveObjects()`, `typeStats()`, `typeRecord(std::string_view)`); `class ManagedObject` (`objectId()`, `ownerId()`, `typeName()`); `template <class T> class Managed : public ManagedObject` with `template <class F> explicit Managed(CreationKey<F>&&)`; `concept ManagedType<T>`; `dvv::test::TestAuthority::makeUnique<T>(args...)` / `makeShared<T>(args...)`.

- [ ] **Step 1: Write the failing test** `dvv-base/tests/test_lifetime.cpp` (Task 9 adds the leak-check case)

```cpp
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "dvv/base/lifetime.h"
#include "dvv/base/testing/test_authority.h"

namespace {

class Widget final : public dvv::Managed<Widget> {
public:
    Widget(dvv::CreationKey<dvv::test::TestAuthority> key, int value) noexcept
        : Managed(std::move(key)), value_(value) {}
    [[nodiscard]] int value() const noexcept { return value_; }

private:
    int value_;
};

class NotFinal : public dvv::Managed<NotFinal> {
public:
    explicit NotFinal(dvv::CreationKey<dvv::test::TestAuthority> key) noexcept
        : Managed(std::move(key)) {}
};

bool isLive(dvv::ObjectId id) {
    const auto objects = dvv::LifetimeRegistry::instance().liveObjects();
    return std::ranges::any_of(objects,
                               [id](const dvv::LiveObjectInfo& info) { return info.id == id; });
}

std::int64_t liveOfType(std::string_view name) {
    for (const auto& stats : dvv::LifetimeRegistry::instance().typeStats()) {
        if (stats.name.find(name) != std::string_view::npos) {
            return stats.live;
        }
    }
    return 0;
}

}  // namespace

static_assert(dvv::ManagedType<Widget>);
static_assert(!dvv::ManagedType<NotFinal>);
static_assert(!std::is_copy_constructible_v<Widget>);
static_assert(!std::is_move_constructible_v<Widget>);

TEST_CASE("managed objects are registered while alive") {
    auto& registry = dvv::LifetimeRegistry::instance();
    const std::size_t before = registry.liveCount();
    auto widget = dvv::test::TestAuthority::makeUnique<Widget>(7);
    CHECK(widget->value() == 7);
    CHECK(widget->objectId() != dvv::NO_OWNER);
    CHECK(widget->typeName().find("Widget") != std::string_view::npos);
    CHECK(registry.liveCount() == before + 1);
    CHECK(isLive(widget->objectId()));
    CHECK(liveOfType("Widget") == 1);

    const dvv::ObjectId id = widget->objectId();
    widget.reset();
    CHECK(registry.liveCount() == before);
    CHECK_FALSE(isLive(id));
    CHECK(liveOfType("Widget") == 0);
}

TEST_CASE("object ids are unique") {
    auto first = dvv::test::TestAuthority::makeShared<Widget>(1);
    auto second = dvv::test::TestAuthority::makeShared<Widget>(2);
    CHECK(first->objectId() != second->objectId());
}

TEST_CASE("detailed mode records the creation location") {
    auto& registry = dvv::LifetimeRegistry::instance();
    registry.setDetailed(false);
    auto plain = dvv::test::TestAuthority::makeUnique<Widget>(1);
    registry.setDetailed(true);
    auto detailed = dvv::test::TestAuthority::makeUnique<Widget>(2);
    for (const auto& info : registry.liveObjects()) {
        if (info.id == plain->objectId()) {
            CHECK_FALSE(info.createdAt.has_value());
        }
        if (info.id == detailed->objectId()) {
            CHECK(info.createdAt.has_value());
            CHECK(info.createdTime.has_value());
        }
    }
}

TEST_CASE("concurrent creation and destruction keeps the count consistent") {
    auto& registry = dvv::LifetimeRegistry::instance();
    const std::size_t before = registry.liveCount();
    std::vector<std::jthread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([] {
            std::vector<std::unique_ptr<Widget>> widgets;
            for (int i = 0; i < 500; ++i) {
                widgets.push_back(dvv::test::TestAuthority::makeUnique<Widget>(i));
            }
        });
    }
    threads.clear();
    CHECK(registry.liveCount() == before);
}
```

Add `test_lifetime.cpp` to `add_executable(dvv_base_tests ...)` in `dvv-base/tests/CMakeLists.txt`.

- [ ] **Step 2: Run it to verify it fails**

Run: `Push-Location dvv-base; cmake --build --preset vs2026; Pop-Location`
Expected: FAIL — `cannot open include file 'dvv/base/lifetime.h'`.

- [ ] **Step 3: Implement** `dvv-base/include/dvv/base/lifetime.h`

```cpp
#pragma once

// Factory-only creation and the lifetime registry (spec §9.4).
//
// A managed class is final, derives Managed<Self> and takes a CreationKey<Authority> as its first
// constructor parameter. Only the authority can create the key, so any other construction fails to
// compile. Every live managed object is linked into the process-wide LifetimeRegistry.

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace dvv {

using ObjectId = std::uint64_t;
inline constexpr ObjectId NO_OWNER = 0;

// One record per managed type for the whole process.
struct TypeRecord {
    std::string name;
    std::atomic<std::uint64_t> created{0};
    std::atomic<std::int64_t> live{0};
};

struct TypeStats {
    std::string_view name;
    std::uint64_t created = 0;
    std::int64_t live = 0;
};

struct LiveObjectInfo {
    std::string_view type;
    ObjectId id = 0;
    ObjectId owner = NO_OWNER;
    std::optional<std::source_location> createdAt;                     // detailed mode only
    std::optional<std::chrono::system_clock::time_point> createdTime;  // detailed mode only
};

class LifetimeRegistry;
class ManagedObject;

// Identity of one object about to be created; issued by LifetimeRegistry::issue<T>().
class LifetimeToken {
public:
    LifetimeToken(LifetimeToken&& other) noexcept = default;
    LifetimeToken& operator=(LifetimeToken&& other) noexcept = default;
    LifetimeToken(const LifetimeToken&) = delete;
    LifetimeToken& operator=(const LifetimeToken&) = delete;
    ~LifetimeToken() = default;

    [[nodiscard]] ObjectId id() const noexcept { return id_; }
    [[nodiscard]] const TypeRecord* type() const noexcept { return type_; }

private:
    friend class LifetimeRegistry;
    friend class ManagedObject;
    LifetimeToken() noexcept = default;

    TypeRecord* type_ = nullptr;
    ObjectId id_ = 0;
    ObjectId owner_ = NO_OWNER;
    std::optional<std::source_location> where_;
    std::optional<std::chrono::system_clock::time_point> time_;
};

// Passkey and lifetime token in one. Only the authority F can create it.
template <class F>
class CreationKey {
public:
    CreationKey(CreationKey&&) noexcept = default;
    CreationKey& operator=(CreationKey&&) noexcept = default;
    CreationKey(const CreationKey&) = delete;
    CreationKey& operator=(const CreationKey&) = delete;
    ~CreationKey() = default;

    [[nodiscard]] LifetimeToken take() && noexcept { return std::move(token_); }

private:
    friend F;
    explicit CreationKey(LifetimeToken token) noexcept : token_(std::move(token)) {}

    LifetimeToken token_;
};

// Process infrastructure: intentionally never destroyed, so it outlives every managed object.
class LifetimeRegistry {
public:
    static LifetimeRegistry& instance() noexcept;

    LifetimeRegistry(const LifetimeRegistry&) = delete;
    LifetimeRegistry& operator=(const LifetimeRegistry&) = delete;
    LifetimeRegistry(LifetimeRegistry&&) = delete;
    LifetimeRegistry& operator=(LifetimeRegistry&&) = delete;

    // Issues the token for one new object of type T.
    template <class T>
    [[nodiscard]] LifetimeToken issue(ObjectId owner = NO_OWNER,
                                      std::source_location where = std::source_location::current());

    // Detailed mode (Debug builds or --diag) also records creation location and time.
    void setDetailed(bool detailed) noexcept;
    [[nodiscard]] bool detailed() const noexcept;

    [[nodiscard]] std::size_t liveCount() const noexcept;
    [[nodiscard]] std::vector<LiveObjectInfo> liveObjects() const;
    [[nodiscard]] std::vector<TypeStats> typeStats() const;

    // Returns the record for a type name, creating it on first use.
    [[nodiscard]] TypeRecord& typeRecord(std::string_view name);

private:
    friend class ManagedObject;
    static constexpr std::size_t SHARD_COUNT = 16;

    struct Shard {
        mutable std::mutex mutex;
        ManagedObject* head = nullptr;
    };

    LifetimeRegistry();
    ~LifetimeRegistry() = default;

    [[nodiscard]] LifetimeToken makeToken(TypeRecord& type, ObjectId owner,
                                          std::source_location where) noexcept;
    void link(ManagedObject& object) noexcept;
    void unlink(ManagedObject& object) noexcept;

    std::array<Shard, SHARD_COUNT> shards_;
    std::atomic<ObjectId> next_id_{1};
    std::atomic<bool> detailed_{false};
    std::atomic<std::size_t> live_count_{0};
    mutable std::mutex types_mutex_;
    std::vector<std::unique_ptr<TypeRecord>> types_;
};

namespace detail {

[[nodiscard]] std::string demangle(const char* name);

template <class T>
TypeRecord& typeRecordOf() {
    static TypeRecord& record = LifetimeRegistry::instance().typeRecord(demangle(typeid(T).name()));
    return record;
}

}  // namespace detail

template <class T>
LifetimeToken LifetimeRegistry::issue(ObjectId owner, std::source_location where) {
    return makeToken(detail::typeRecordOf<T>(), owner, where);
}

// Non-template base: identity reachable through interface pointers.
class ManagedObject {
public:
    ManagedObject(const ManagedObject&) = delete;
    ManagedObject& operator=(const ManagedObject&) = delete;
    ManagedObject(ManagedObject&&) = delete;
    ManagedObject& operator=(ManagedObject&&) = delete;

    [[nodiscard]] ObjectId objectId() const noexcept { return id_; }
    [[nodiscard]] ObjectId ownerId() const noexcept { return owner_; }
    [[nodiscard]] std::string_view typeName() const noexcept { return type_->name; }

protected:
    explicit ManagedObject(LifetimeToken&& token) noexcept;
    ~ManagedObject();  // unregisters; never blocks on anything but the O(1) shard update

private:
    friend class LifetimeRegistry;

    TypeRecord* type_;
    ObjectId id_;
    ObjectId owner_;
    std::optional<std::source_location> where_;
    std::optional<std::chrono::system_clock::time_point> time_;
    ManagedObject* prev_ = nullptr;
    ManagedObject* next_ = nullptr;
};

// CRTP base of every managed class.
template <class T>
class Managed : public ManagedObject {
protected:
    template <class F>
    explicit Managed(CreationKey<F>&& key) noexcept
        : ManagedObject(checked(std::move(key).take())) {}
    ~Managed() = default;

private:
    static LifetimeToken&& checked(LifetimeToken&& token) noexcept {
        assert(token.type() == &detail::typeRecordOf<T>() && "token issued for another type");
        return std::move(token);
    }
};

template <class T>
concept ManagedType = std::is_final_v<T> && std::derived_from<T, Managed<T>> &&
                      !std::is_copy_constructible_v<T> && !std::is_move_constructible_v<T>;

}  // namespace dvv
```

`dvv-base/src/lifetime.cpp` (add `src/lifetime.cpp` to `add_library(dvv_base ...)`):

```cpp
#include "dvv/base/lifetime.h"

#if defined(__GNUC__) || defined(__clang__)
#include <cstdlib>

#include <cxxabi.h>
#endif

namespace dvv {

namespace detail {

std::string demangle(const char* name) {
#if defined(__GNUC__) || defined(__clang__)
    int status = 0;
    char* readable = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    if (status == 0 && readable != nullptr) {
        std::string result(readable);
        std::free(readable);
        return result;
    }
    return name;
#else
    std::string_view text = name;
    for (std::string_view prefix : {"class ", "struct "}) {
        if (text.starts_with(prefix)) {
            text.remove_prefix(prefix.size());
        }
    }
    return std::string(text);
#endif
}

}  // namespace detail

LifetimeRegistry::LifetimeRegistry() = default;

LifetimeRegistry& LifetimeRegistry::instance() noexcept {
    // Intentionally leaked; out of memory at start-up is fatal anyway.
    static auto* registry = new LifetimeRegistry();  // NOLINT(bugprone-unhandled-exception-at-new)
    return *registry;
}

void LifetimeRegistry::setDetailed(bool detailed) noexcept {
    detailed_.store(detailed, std::memory_order_relaxed);
}

bool LifetimeRegistry::detailed() const noexcept {
    return detailed_.load(std::memory_order_relaxed);
}

std::size_t LifetimeRegistry::liveCount() const noexcept {
    return live_count_.load(std::memory_order_relaxed);
}

TypeRecord& LifetimeRegistry::typeRecord(std::string_view name) {
    std::lock_guard lock(types_mutex_);
    for (const auto& record : types_) {
        if (record->name == name) {
            return *record;
        }
    }
    auto record = std::make_unique<TypeRecord>();
    record->name = std::string(name);
    types_.push_back(std::move(record));
    return *types_.back();
}

LifetimeToken LifetimeRegistry::makeToken(TypeRecord& type, ObjectId owner,
                                          std::source_location where) noexcept {
    LifetimeToken token;
    token.type_ = &type;
    token.id_ = next_id_.fetch_add(1, std::memory_order_relaxed);
    token.owner_ = owner;
    if (detailed()) {
        token.where_ = where;
        token.time_ = std::chrono::system_clock::now();
    }
    return token;
}

void LifetimeRegistry::link(ManagedObject& object) noexcept {
    Shard& shard = shards_[object.id_ % SHARD_COUNT];
    {
        std::lock_guard lock(shard.mutex);
        object.next_ = shard.head;
        if (shard.head != nullptr) {
            shard.head->prev_ = &object;
        }
        shard.head = &object;
    }
    object.type_->created.fetch_add(1, std::memory_order_relaxed);
    object.type_->live.fetch_add(1, std::memory_order_relaxed);
    live_count_.fetch_add(1, std::memory_order_relaxed);
}

void LifetimeRegistry::unlink(ManagedObject& object) noexcept {
    Shard& shard = shards_[object.id_ % SHARD_COUNT];
    {
        std::lock_guard lock(shard.mutex);
        if (object.prev_ != nullptr) {
            object.prev_->next_ = object.next_;
        } else {
            shard.head = object.next_;
        }
        if (object.next_ != nullptr) {
            object.next_->prev_ = object.prev_;
        }
    }
    object.type_->live.fetch_sub(1, std::memory_order_relaxed);
    live_count_.fetch_sub(1, std::memory_order_relaxed);
}

std::vector<LiveObjectInfo> LifetimeRegistry::liveObjects() const {
    std::vector<LiveObjectInfo> objects;
    for (const Shard& shard : shards_) {
        std::lock_guard lock(shard.mutex);
        for (const ManagedObject* object = shard.head; object != nullptr; object = object->next_) {
            objects.push_back(LiveObjectInfo{object->type_->name, object->id_, object->owner_,
                                             object->where_, object->time_});
        }
    }
    return objects;
}

std::vector<TypeStats> LifetimeRegistry::typeStats() const {
    std::lock_guard lock(types_mutex_);
    std::vector<TypeStats> stats;
    stats.reserve(types_.size());
    for (const auto& record : types_) {
        stats.push_back(TypeStats{record->name, record->created.load(std::memory_order_relaxed),
                                  record->live.load(std::memory_order_relaxed)});
    }
    return stats;
}

ManagedObject::ManagedObject(LifetimeToken&& token) noexcept
    : type_(token.type_),
      id_(token.id_),
      owner_(token.owner_),
      where_(token.where_),
      time_(token.time_) {
    assert(type_ != nullptr && "a LifetimeToken may be used only once");
    token.type_ = nullptr;
    LifetimeRegistry::instance().link(*this);
}

ManagedObject::~ManagedObject() {
    LifetimeRegistry::instance().unlink(*this);
}

}  // namespace dvv
```

`dvv-base/include/dvv/base/testing/test_authority.h`:

```cpp
#pragma once

// Creation authority for tests only (spec §9.4). Link dvv::base_testing; never use in products.

#include <memory>
#include <utility>

#include "dvv/base/lifetime.h"

namespace dvv::test {

class TestAuthority {
public:
    template <ManagedType T, class... Args>
    [[nodiscard]] static std::unique_ptr<T> makeUnique(Args&&... args) {
        return std::make_unique<T>(key<T>(), std::forward<Args>(args)...);
    }

    template <ManagedType T, class... Args>
    [[nodiscard]] static std::shared_ptr<T> makeShared(Args&&... args) {
        return std::make_shared<T>(key<T>(), std::forward<Args>(args)...);
    }

private:
    template <class T>
    [[nodiscard]] static CreationKey<TestAuthority> key() {
        return CreationKey<TestAuthority>(LifetimeRegistry::instance().issue<T>());
    }
};

}  // namespace dvv::test
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `Push-Location dvv-base; cmake --workflow --preset vs2026; Pop-Location`
Expected: `100% tests passed` in Debug and Release.

- [ ] **Step 5: Commit**

```powershell
git -C dvv-base add -A
git -C dvv-base commit -m "feat: add lifetime registry and factory-only creation keys" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Leak check and compile-fail tests

**Files:**
- Create: `dvv-base/include/dvv/base/leak_check.h`, `dvv-base/src/leak_check.cpp`, `dvv-base/tests/compile_fail/direct_construction.cpp`
- Modify: `dvv-base/tests/main.cpp` (leak-checking main), `dvv-base/tests/test_lifetime.cpp` (add the leak-check case and include), `dvv-base/tests/CMakeLists.txt` (compile-fail block), `dvv-base/CMakeLists.txt` (add `src/leak_check.cpp`)

**Interfaces:**
- Consumes: `LifetimeRegistry`, `LiveObjectInfo` (Task 8), `getEnvironment`, `sourceFileName` (Task 6).
- Produces: `enum class dvv::LeakPolicy : std::uint8_t { Fail, Warn }`; `LeakPolicy defaultLeakPolicy()` (`DVV_LEAK_CHECK=fail|warn`, else Fail in Debug, Warn with `NDEBUG`); `std::string formatLeakReport(std::span<const LiveObjectInfo>)`; `int finishWithLeakCheck(int exit_code, LeakPolicy = defaultLeakPolicy())`. CTest `dvv_base.compile_fail.{1,2,3}` with `WILL_FAIL`, control target `dvv_base_compile_ok`.

- [ ] **Step 1: Write the failing tests**

Replace `dvv-base/tests/test_lifetime.cpp` with its final form (adds `#include "dvv/base/leak_check.h"` and the last test case):

```cpp
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "dvv/base/leak_check.h"
#include "dvv/base/lifetime.h"
#include "dvv/base/testing/test_authority.h"

namespace {

class Widget final : public dvv::Managed<Widget> {
public:
    Widget(dvv::CreationKey<dvv::test::TestAuthority> key, int value) noexcept
        : Managed(std::move(key)), value_(value) {}
    [[nodiscard]] int value() const noexcept { return value_; }

private:
    int value_;
};

class NotFinal : public dvv::Managed<NotFinal> {
public:
    explicit NotFinal(dvv::CreationKey<dvv::test::TestAuthority> key) noexcept
        : Managed(std::move(key)) {}
};

bool isLive(dvv::ObjectId id) {
    const auto objects = dvv::LifetimeRegistry::instance().liveObjects();
    return std::ranges::any_of(objects,
                               [id](const dvv::LiveObjectInfo& info) { return info.id == id; });
}

std::int64_t liveOfType(std::string_view name) {
    for (const auto& stats : dvv::LifetimeRegistry::instance().typeStats()) {
        if (stats.name.find(name) != std::string_view::npos) {
            return stats.live;
        }
    }
    return 0;
}

}  // namespace

static_assert(dvv::ManagedType<Widget>);
static_assert(!dvv::ManagedType<NotFinal>);
static_assert(!std::is_copy_constructible_v<Widget>);
static_assert(!std::is_move_constructible_v<Widget>);

TEST_CASE("managed objects are registered while alive") {
    auto& registry = dvv::LifetimeRegistry::instance();
    const std::size_t before = registry.liveCount();
    auto widget = dvv::test::TestAuthority::makeUnique<Widget>(7);
    CHECK(widget->value() == 7);
    CHECK(widget->objectId() != dvv::NO_OWNER);
    CHECK(widget->typeName().find("Widget") != std::string_view::npos);
    CHECK(registry.liveCount() == before + 1);
    CHECK(isLive(widget->objectId()));
    CHECK(liveOfType("Widget") == 1);

    const dvv::ObjectId id = widget->objectId();
    widget.reset();
    CHECK(registry.liveCount() == before);
    CHECK_FALSE(isLive(id));
    CHECK(liveOfType("Widget") == 0);
}

TEST_CASE("object ids are unique") {
    auto first = dvv::test::TestAuthority::makeShared<Widget>(1);
    auto second = dvv::test::TestAuthority::makeShared<Widget>(2);
    CHECK(first->objectId() != second->objectId());
}

TEST_CASE("detailed mode records the creation location") {
    auto& registry = dvv::LifetimeRegistry::instance();
    registry.setDetailed(false);
    auto plain = dvv::test::TestAuthority::makeUnique<Widget>(1);
    registry.setDetailed(true);
    auto detailed = dvv::test::TestAuthority::makeUnique<Widget>(2);
    for (const auto& info : registry.liveObjects()) {
        if (info.id == plain->objectId()) {
            CHECK_FALSE(info.createdAt.has_value());
        }
        if (info.id == detailed->objectId()) {
            CHECK(info.createdAt.has_value());
            CHECK(info.createdTime.has_value());
        }
    }
}

TEST_CASE("concurrent creation and destruction keeps the count consistent") {
    auto& registry = dvv::LifetimeRegistry::instance();
    const std::size_t before = registry.liveCount();
    std::vector<std::jthread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([] {
            std::vector<std::unique_ptr<Widget>> widgets;
            for (int i = 0; i < 500; ++i) {
                widgets.push_back(dvv::test::TestAuthority::makeUnique<Widget>(i));
            }
        });
    }
    threads.clear();
    CHECK(registry.liveCount() == before);
}

TEST_CASE("leak check reports live objects and applies the policy") {
    CHECK(dvv::finishWithLeakCheck(0, dvv::LeakPolicy::Fail) == 0);
    auto widget = dvv::test::TestAuthority::makeUnique<Widget>(3);
    CHECK(dvv::finishWithLeakCheck(0, dvv::LeakPolicy::Fail) == 1);
    CHECK(dvv::finishWithLeakCheck(5, dvv::LeakPolicy::Fail) == 5);
    CHECK(dvv::finishWithLeakCheck(0, dvv::LeakPolicy::Warn) == 0);
    const auto report = dvv::formatLeakReport(dvv::LifetimeRegistry::instance().liveObjects());
    CHECK(report.find("Widget") != std::string::npos);
    CHECK(report.find("#" + std::to_string(widget->objectId())) != std::string::npos);
}
```

Create `dvv-base/tests/compile_fail/direct_construction.cpp`:

```cpp
// Compile-fail cases for the factory-only rule (spec §9.4). DVV_COMPILE_FAIL_CASE selects one:
//   0  control: creation through the authority compiles
//   1  constructing a CreationKey outside its authority
//   2  default-constructing a managed object
//   3  copying a managed object
#include <memory>
#include <utility>

#include "dvv/base/lifetime.h"
#include "dvv/base/testing/test_authority.h"

namespace {

class Widget final : public dvv::Managed<Widget> {
public:
    explicit Widget(dvv::CreationKey<dvv::test::TestAuthority> key) noexcept
        : Managed(std::move(key)) {}
};

}  // namespace

int main() {
    auto widget = dvv::test::TestAuthority::makeUnique<Widget>();
#if DVV_COMPILE_FAIL_CASE == 1
    dvv::CreationKey<dvv::test::TestAuthority> forged(
        dvv::LifetimeRegistry::instance().issue<Widget>());
    Widget direct(std::move(forged));
#elif DVV_COMPILE_FAIL_CASE == 2
    Widget direct;
#elif DVV_COMPILE_FAIL_CASE == 3
    Widget copy(*widget);
#endif
    return widget->objectId() != dvv::NO_OWNER ? 0 : 1;
}
```

Replace `dvv-base/tests/CMakeLists.txt` (Tasks 10–16 add their test files to `add_executable`):

```cmake
find_package(doctest 2.5 REQUIRED CONFIG)

add_executable(dvv_base_tests
    main.cpp
    test_error.cpp
    test_lifetime.cpp)
target_link_libraries(dvv_base_tests PRIVATE dvv::base doctest::doctest)
# doctest otherwise only forward-declares std::ostream, which breaks printing std types.
target_compile_definitions(dvv_base_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
dvv_set_compiler_options(dvv_base_tests)
add_test(NAME dvv_base.unit COMMAND dvv_base_tests)
set_tests_properties(dvv_base.unit PROPERTIES
    ENVIRONMENT DVV_LEAK_CHECK=fail
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")

# Package test (spec §3.1): install, then build a consumer with find_package(dvv_base).
# Skipped for sanitizer builds, whose consumers would need the same runtime flags.
if(NOT DVV_SANITIZE)
    string(REPLACE ";" "," deps_prefix_path "${CMAKE_PREFIX_PATH}")
    add_test(NAME dvv_base.package
             COMMAND ${CMAKE_COMMAND}
                     -DBUILD_DIR=${CMAKE_BINARY_DIR}
                     -DCONFIG=$<CONFIG>
                     -DPREFIX=${CMAKE_CURRENT_BINARY_DIR}/package-prefix
                     -DCONSUMER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/package
                     -DCONSUMER_BUILD=${CMAKE_CURRENT_BINARY_DIR}/package-consumer
                     -DGENERATOR=${CMAKE_GENERATOR}
                     -DPLATFORM=${CMAKE_GENERATOR_PLATFORM}
                     -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
                     -DDEPS_PREFIX_PATH=${deps_prefix_path}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/package/run-package-test.cmake)
endif()

# Compile-fail tests (spec §9.4). Case 0 is the control and must build; cases 1-3 must not.
set(compile_fail_source ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/direct_construction.cpp)
add_executable(dvv_base_compile_ok ${compile_fail_source})
target_compile_definitions(dvv_base_compile_ok PRIVATE DVV_COMPILE_FAIL_CASE=0)
target_link_libraries(dvv_base_compile_ok PRIVATE dvv::base)
dvv_set_compiler_options(dvv_base_compile_ok)
foreach(case IN ITEMS 1 2 3)
    set(target dvv_base_compile_fail_${case})
    add_executable(${target} EXCLUDE_FROM_ALL ${compile_fail_source})
    target_compile_definitions(${target} PRIVATE DVV_COMPILE_FAIL_CASE=${case})
    target_link_libraries(${target} PRIVATE dvv::base)
    dvv_set_compiler_options(${target})
    add_test(NAME dvv_base.compile_fail.${case}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${target}
                     --config $<CONFIG>)
    set_tests_properties(dvv_base.compile_fail.${case} PROPERTIES WILL_FAIL TRUE)
endforeach()
```

- [ ] **Step 2: Run them to verify they fail**

Run: `Push-Location dvv-base; cmake --build --preset vs2026; Pop-Location`
Expected: FAIL — `cannot open include file 'dvv/base/leak_check.h'`.

- [ ] **Step 3: Implement** `dvv-base/include/dvv/base/leak_check.h`

```cpp
#pragma once

#include <cstdint>
#include <span>
#include <string>

#include "dvv/base/lifetime.h"

namespace dvv {

enum class LeakPolicy : std::uint8_t { Fail, Warn };

// DVV_LEAK_CHECK=fail|warn overrides; otherwise Fail in Debug builds and Warn in Release builds.
[[nodiscard]] LeakPolicy defaultLeakPolicy();

[[nodiscard]] std::string formatLeakReport(std::span<const LiveObjectInfo> leaks);

// Call at the end of main, after the Session, FrameStore and contexts are torn down. Prints the
// live managed objects to stderr and returns the exit code to use: unchanged when nothing leaked or
// the policy is Warn, otherwise non-zero.
[[nodiscard]] int finishWithLeakCheck(int exit_code, LeakPolicy policy = defaultLeakPolicy());

}  // namespace dvv
```

`dvv-base/src/leak_check.cpp` (add `src/leak_check.cpp` to `add_library(dvv_base ...)`):

```cpp
#include "dvv/base/leak_check.h"

#include <cstdio>
#include <format>

#include "dvv/base/environment.h"
#include "dvv/base/error.h"

namespace dvv {

LeakPolicy defaultLeakPolicy() {
    if (const auto value = getEnvironment("DVV_LEAK_CHECK")) {
        if (*value == "fail") {
            return LeakPolicy::Fail;
        }
        if (*value == "warn") {
            return LeakPolicy::Warn;
        }
    }
#if defined(NDEBUG)
    return LeakPolicy::Warn;
#else
    return LeakPolicy::Fail;
#endif
}

std::string formatLeakReport(std::span<const LiveObjectInfo> leaks) {
    std::string report = std::format("{} managed object(s) still alive at exit:\n", leaks.size());
    for (const LiveObjectInfo& leak : leaks) {
        report += std::format("  #{} {} (owner #{})", leak.id, leak.type, leak.owner);
        if (leak.createdAt) {
            report += std::format(" created at {}:{}", sourceFileName(*leak.createdAt),
                                  leak.createdAt->line());
        }
        report += '\n';
    }
    return report;
}

int finishWithLeakCheck(int exit_code, LeakPolicy policy) {
    const auto leaks = LifetimeRegistry::instance().liveObjects();
    if (leaks.empty()) {
        return exit_code;
    }
    std::fputs(formatLeakReport(leaks).c_str(), stderr);
    if (policy == LeakPolicy::Warn) {
        return exit_code;
    }
    return exit_code != 0 ? exit_code : 1;
}

}  // namespace dvv
```

Replace `dvv-base/tests/main.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "dvv/base/leak_check.h"
#include "dvv/base/lifetime.h"

int main(int argc, char** argv) {
    dvv::LifetimeRegistry::instance().setDetailed(true);
    doctest::Context context(argc, argv);
    const int result = context.run();
    if (context.shouldExit()) {
        return result;
    }
    return dvv::finishWithLeakCheck(result);
}
```

- [ ] **Step 4: Run the tests to verify they pass and that each compile-fail case fails for its reason**

```powershell
Push-Location dvv-base; cmake --workflow --preset vs2026; Pop-Location
foreach ($c in 1,2,3) { cmake --build _build/dvv-base/vs2026 --target "dvv_base_compile_fail_$c" --config Debug 2>&1 | Select-String 'error C\d+' | Select-Object -First 1 }
```

Expected: `100% tests passed ... out of 5` in Debug and Release; the three errors are `C2248` (private `CreationKey` constructor), `C2512` (no default constructor) and `C2280` (deleted copy constructor).

- [ ] **Step 5: Commit**

```powershell
git -C dvv-base add -A
git -C dvv-base commit -m "feat: add leak check and compile-fail tests for factory-only creation" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Executors — thread pool and serial executor

**Files:**
- Create: `dvv-base/include/dvv/base/executor.h`, `dvv-base/include/dvv/base/executors.h`, `dvv-base/src/executors.cpp`
- Test: `dvv-base/tests/test_executors.cpp`
- Modify: add `src/executors.cpp` and `test_executors.cpp` to their lists

**Interfaces:**
- Consumes: `Managed`, `CreationKey`, `LifetimeRegistry` (Task 8), `Result`, `makeError` (Task 6).
- Produces: `class dvv::IExecutor { virtual void post(std::move_only_function<void()> work) = 0; }` (never inline; work must not throw); `ThreadPool final : IExecutor, Managed<ThreadPool>` (`threadCount()`; destructor drains then joins; may be destroyed on its own worker); `SerialExecutor final : IExecutor, Managed<SerialExecutor>, enable_shared_from_this` (FIFO, one item at a time, holds its target strongly); authority `ExecutorFactory::makeThreadPool(std::size_t thread_count = 0, std::source_location = current()) -> Result<std::shared_ptr<ThreadPool>>` and `ExecutorFactory::makeSerialExecutor(std::shared_ptr<IExecutor>, std::source_location = current()) -> Result<std::shared_ptr<SerialExecutor>>` (owner = target's object id).

- [ ] **Step 1: Write the failing test** `dvv-base/tests/test_executors.cpp`

```cpp
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <latch>
#include <thread>
#include <vector>

#include "dvv/base/executors.h"

TEST_CASE("thread pool runs every posted item") {
    auto pool = dvv::ExecutorFactory::makeThreadPool(4).value();
    CHECK(pool->threadCount() == 4);
    std::atomic<int> count{0};
    std::latch done(100);
    for (int i = 0; i < 100; ++i) {
        pool->post([&] {
            count.fetch_add(1);
            done.count_down();
        });
    }
    done.wait();
    CHECK(count.load() == 100);
}

TEST_CASE("thread pool with zero threads uses the hardware concurrency") {
    auto pool = dvv::ExecutorFactory::makeThreadPool(0).value();
    CHECK(pool->threadCount() >= 1);
}

TEST_CASE("destroying a thread pool finishes the queued work") {
    std::atomic<int> count{0};
    {
        auto pool = dvv::ExecutorFactory::makeThreadPool(2).value();
        for (int i = 0; i < 50; ++i) {
            pool->post([&] {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                count.fetch_add(1);
            });
        }
    }
    CHECK(count.load() == 50);
}

TEST_CASE("a thread pool may be destroyed by its own worker") {
    auto pool = dvv::ExecutorFactory::makeThreadPool(2).value();
    const dvv::ObjectId id = pool->objectId();
    std::promise<void> released;
    auto future = released.get_future();
    std::latch posted(1);
    pool->post([owner = pool, &released, &posted]() mutable {
        posted.wait();  // run only after the test dropped its own reference
        owner.reset();  // last reference: the pool is destroyed on this worker thread
        released.set_value();
    });
    pool.reset();
    posted.count_down();
    future.wait();
    const auto objects = dvv::LifetimeRegistry::instance().liveObjects();
    CHECK(std::ranges::none_of(objects,
                               [id](const dvv::LiveObjectInfo& info) { return info.id == id; }));
}

TEST_CASE("serial executor runs items in order, one at a time") {
    auto pool = dvv::ExecutorFactory::makeThreadPool(4).value();
    auto serial = dvv::ExecutorFactory::makeSerialExecutor(pool).value();
    CHECK(serial->ownerId() == pool->objectId());
    std::vector<int> order;  // unsynchronised on purpose: the executor serialises access
    std::atomic<int> in_flight{0};
    std::atomic<int> overlaps{0};
    std::latch done(1000);
    for (int i = 0; i < 1000; ++i) {
        serial->post([&, i] {
            if (in_flight.fetch_add(1) != 0) {
                overlaps.fetch_add(1);
            }
            order.push_back(i);
            in_flight.fetch_sub(1);
            done.count_down();
        });
    }
    done.wait();
    CHECK(overlaps.load() == 0);
    REQUIRE(order.size() == 1000);
    CHECK(std::ranges::is_sorted(order));
}

TEST_CASE("serial executor needs a target") {
    const auto result = dvv::ExecutorFactory::makeSerialExecutor(nullptr);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == dvv::Errc::Internal);
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `Push-Location dvv-base; cmake --build --preset vs2026; Pop-Location`
Expected: FAIL — `cannot open include file 'dvv/base/executors.h'`.

- [ ] **Step 3: Implement** `dvv-base/include/dvv/base/executor.h`

```cpp
#pragma once

#include <functional>

namespace dvv {

// Runs work items. Implementations never run posted work inline in post().
class IExecutor {
public:
    virtual ~IExecutor() = default;

    // Queues work. The work must not throw.
    virtual void post(std::move_only_function<void()> work) = 0;
};

}  // namespace dvv
```

`dvv-base/include/dvv/base/executors.h`:

```cpp
#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <source_location>
#include <stop_token>
#include <thread>
#include <vector>

#include "dvv/base/error.h"
#include "dvv/base/executor.h"
#include "dvv/base/lifetime.h"

namespace dvv {

class ExecutorFactory;

// Fixed-size pool of worker threads. The destructor finishes all queued work, then joins.
class ThreadPool final : public IExecutor, public Managed<ThreadPool> {
public:
    ThreadPool(CreationKey<ExecutorFactory> key, std::size_t thread_count);
    ~ThreadPool() override;

    void post(std::move_only_function<void()> work) override;
    [[nodiscard]] std::size_t threadCount() const noexcept { return threads_.size(); }

private:
    struct State;
    static void runWorker(State& state, const std::stop_token& stop);

    std::shared_ptr<State> state_;  // shared with the workers so a worker may destroy the pool
    std::vector<std::jthread> threads_;
};

// Runs posted work one item at a time, in order, on a target executor (an actor's mailbox).
class SerialExecutor final : public IExecutor,
                             public Managed<SerialExecutor>,
                             public std::enable_shared_from_this<SerialExecutor> {
public:
    SerialExecutor(CreationKey<ExecutorFactory> key, std::shared_ptr<IExecutor> target) noexcept;

    void post(std::move_only_function<void()> work) override;

private:
    void drain();

    std::shared_ptr<IExecutor> target_;  // strong reference up to the parent executor
    std::mutex mutex_;
    std::deque<std::move_only_function<void()>> queue_;
    bool scheduled_ = false;
};

// Creation authority for executors.
class ExecutorFactory {
public:
    // thread_count 0 means one thread per hardware thread.
    [[nodiscard]] static Result<std::shared_ptr<ThreadPool>> makeThreadPool(
        std::size_t thread_count = 0, std::source_location where = std::source_location::current());

    [[nodiscard]] static Result<std::shared_ptr<SerialExecutor>> makeSerialExecutor(
        std::shared_ptr<IExecutor> target,
        std::source_location where = std::source_location::current());
};

}  // namespace dvv
```

`dvv-base/src/executors.cpp`:

```cpp
#include "dvv/base/executors.h"

#include <algorithm>
#include <condition_variable>
#include <format>
#include <stop_token>
#include <system_error>

namespace dvv {

struct ThreadPool::State {
    std::mutex mutex;
    std::condition_variable_any wake;
    std::deque<std::move_only_function<void()>> queue;
};

void ThreadPool::runWorker(State& state, const std::stop_token& stop) {
    for (;;) {
        std::move_only_function<void()> work;
        {
            std::unique_lock lock(state.mutex);
            state.wake.wait(lock, stop, [&state] { return !state.queue.empty(); });
            if (state.queue.empty()) {
                return;  // stop requested and the queue is drained
            }
            work = std::move(state.queue.front());
            state.queue.pop_front();
        }
        work();
    }
}

ThreadPool::ThreadPool(CreationKey<ExecutorFactory> key, std::size_t thread_count)
    : Managed(std::move(key)), state_(std::make_shared<State>()) {
    threads_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) {
        threads_.emplace_back(
            [state = state_](const std::stop_token& stop) { runWorker(*state, stop); });
    }
}

ThreadPool::~ThreadPool() {
    for (auto& thread : threads_) {
        thread.request_stop();
    }
    for (auto& thread : threads_) {
        if (thread.get_id() == std::this_thread::get_id()) {
            thread.detach();  // the last owner was released on a worker; it keeps the state alive
        } else if (thread.joinable()) {
            thread.join();
        }
    }
}

void ThreadPool::post(std::move_only_function<void()> work) {
    {
        std::lock_guard lock(state_->mutex);
        state_->queue.push_back(std::move(work));
    }
    state_->wake.notify_one();
}

SerialExecutor::SerialExecutor(CreationKey<ExecutorFactory> key,
                               std::shared_ptr<IExecutor> target) noexcept
    : Managed(std::move(key)), target_(std::move(target)) {}

void SerialExecutor::post(std::move_only_function<void()> work) {
    bool schedule = false;
    {
        std::lock_guard lock(mutex_);
        queue_.push_back(std::move(work));
        schedule = !scheduled_;
        scheduled_ = true;
    }
    if (schedule) {
        target_->post([self = shared_from_this()] { self->drain(); });
    }
}

void SerialExecutor::drain() {
    for (;;) {
        std::move_only_function<void()> work;
        {
            std::lock_guard lock(mutex_);
            if (queue_.empty()) {
                scheduled_ = false;
                return;
            }
            work = std::move(queue_.front());
            queue_.pop_front();
        }
        work();
    }
}

Result<std::shared_ptr<ThreadPool>> ExecutorFactory::makeThreadPool(std::size_t thread_count,
                                                                    std::source_location where) {
    if (thread_count == 0) {
        thread_count = std::max(1U, std::thread::hardware_concurrency());
    }
    try {
        return std::make_shared<ThreadPool>(
            CreationKey<ExecutorFactory>(
                LifetimeRegistry::instance().issue<ThreadPool>(NO_OWNER, where)),
            thread_count);
    } catch (const std::system_error& error) {
        return makeError(
            Errc::Internal,
            std::format("cannot start {} worker threads: {}", thread_count, error.what()), where);
    }
}

Result<std::shared_ptr<SerialExecutor>> ExecutorFactory::makeSerialExecutor(
    std::shared_ptr<IExecutor> target, std::source_location where) {
    if (!target) {
        return makeError(Errc::Internal, "serial executor needs a target executor", where);
    }
    const auto* parent = dynamic_cast<const ManagedObject*>(target.get());
    const ObjectId owner = parent != nullptr ? parent->objectId() : NO_OWNER;
    return std::make_shared<SerialExecutor>(
        CreationKey<ExecutorFactory>(
            LifetimeRegistry::instance().issue<SerialExecutor>(owner, where)),
        std::move(target));
}

}  // namespace dvv
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `Push-Location dvv-base; cmake --workflow --preset vs2026; Pop-Location`
Expected: `100% tests passed` in Debug and Release.

- [ ] **Step 5: Commit**

```powershell
git -C dvv-base add -A
git -C dvv-base commit -m "feat: add thread pool and serial executor" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: Coroutine tasks, `syncWait`, `resumeOn` and `co_try`

**Files:**
- Create: `dvv-base/include/dvv/base/task.h` (header-only)
- Test: `dvv-base/tests/test_task.cpp`
- Modify: add `test_task.cpp` to the test list

**Interfaces:**
- Consumes: `IExecutor` (Task 10), `Result`, `Error` (Task 6).
- Produces: `template <class T = void> class [[nodiscard]] dvv::Task` (lazy, move-only, `valid()`, awaitable as rvalue: `co_await std::move(task)` or `co_await f()`); `template <class T> T dvv::syncWait(Task<T>)` (blocks; rethrows); `auto dvv::resumeOn(IExecutor&)`; macro `co_try(expression)` usable only inside `Task<Result<U>>`; internal `detail::PromiseBase::continuationOrNoop()` and `detail::Promise<T>::failWith(Error)` (used by `whenAll` and `co_try`).

- [ ] **Step 1: Write the failing test** `dvv-base/tests/test_task.cpp`

```cpp
#include <doctest/doctest.h>

#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>

#include "dvv/base/executors.h"
#include "dvv/base/task.h"

namespace {

dvv::Task<int> answer() {
    co_return 42;
}

dvv::Task<int> addOne(int value) {
    co_return value + 1;
}

dvv::Task<int> chained() {
    const int first = co_await answer();
    co_return co_await addOne(first);
}

dvv::Task<void> setFlag(bool& flag) {
    flag = true;
    co_return;
}

dvv::Task<int> throws() {
    throw std::runtime_error("boom");
    co_return 0;
}

dvv::Task<std::unique_ptr<int>> moveOnly() {
    co_return std::make_unique<int>(5);
}

dvv::Task<int> one() {
    co_return 1;
}

dvv::Task<long> manySequential(int count) {
    long sum = 0;
    for (int i = 0; i < count; ++i) {
        sum += co_await one();
    }
    co_return sum;
}

dvv::Task<std::thread::id> threadAfterResume(dvv::IExecutor& executor) {
    co_await dvv::resumeOn(executor);
    co_return std::this_thread::get_id();
}

dvv::Result<int> parse(std::string_view text) {
    if (text == "21") {
        return 21;
    }
    return dvv::makeError(dvv::Errc::Format, "not 21");
}

dvv::Result<void> check(bool ok) {
    if (ok) {
        return {};
    }
    return dvv::makeError(dvv::Errc::Unsupported, "check failed");
}

struct Guard {
    int& destroyed;
    ~Guard() { ++destroyed; }
};

dvv::Task<dvv::Result<int>> doubledAsync(std::string_view text, bool& reached_end, int& destroyed) {
    Guard guard{destroyed};
    const int value = co_try(parse(text));
    reached_end = true;
    co_return value * 2;
}

dvv::Task<dvv::Result<int>> checkedAsync(bool ok) {
    co_try(check(ok));
    co_return 1;
}

dvv::Task<dvv::Result<int>> outerAsync(std::string_view text) {
    bool reached_end = false;
    int destroyed = 0;
    const int value = co_try(co_await doubledAsync(text, reached_end, destroyed));
    co_return value + 1;
}

}  // namespace

TEST_CASE("syncWait returns the task result") {
    CHECK(dvv::syncWait(answer()) == 42);
    CHECK(dvv::syncWait(chained()) == 43);
}

TEST_CASE("void tasks run") {
    bool flag = false;
    dvv::syncWait(setFlag(flag));
    CHECK(flag);
}

TEST_CASE("tasks are lazy") {
    bool flag = false;
    {
        auto task = setFlag(flag);
        CHECK(task.valid());
    }
    CHECK_FALSE(flag);
}

TEST_CASE("exceptions propagate through syncWait") {
    CHECK_THROWS_AS(dvv::syncWait(throws()), std::runtime_error);
}

TEST_CASE("move-only results") {
    auto value = dvv::syncWait(moveOnly());
    REQUIRE(value != nullptr);
    CHECK(*value == 5);
}

TEST_CASE("long chains of synchronous awaits do not grow the stack") {
#if defined(__GNUC__) && !defined(__clang__) && !defined(__OPTIMIZE__)
    // GCC guarantees the tail call of symmetric transfer only when optimising; at -O0 every
    // synchronously completed await nests one call frame.
    constexpr int COUNT = 10000;
#else
    constexpr int COUNT = 200000;
#endif
    CHECK(dvv::syncWait(manySequential(COUNT)) == COUNT);
}

TEST_CASE("resumeOn continues on the executor") {
    auto pool = dvv::ExecutorFactory::makeThreadPool(1).value();
    const auto id = dvv::syncWait(threadAfterResume(*pool));
    CHECK(id != std::this_thread::get_id());
}

TEST_CASE("co_try unwraps a value") {
    bool reached_end = false;
    int destroyed = 0;
    const auto result = dvv::syncWait(doubledAsync("21", reached_end, destroyed));
    CHECK(result.value() == 42);
    CHECK(reached_end);
    CHECK(destroyed == 1);
}

TEST_CASE("co_try completes the task with the error and destroys locals") {
    bool reached_end = false;
    int destroyed = 0;
    const auto result = dvv::syncWait(doubledAsync("x", reached_end, destroyed));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == dvv::Errc::Format);
    CHECK_FALSE(reached_end);
    CHECK(destroyed == 1);
}

TEST_CASE("co_try works with Result<void> and nested tasks") {
    CHECK(dvv::syncWait(checkedAsync(true)).value() == 1);
    CHECK(dvv::syncWait(checkedAsync(false)).error().code == dvv::Errc::Unsupported);
    CHECK(dvv::syncWait(outerAsync("21")).value() == 43);
    CHECK(dvv::syncWait(outerAsync("y")).error().message == "not 21");
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `Push-Location dvv-base; cmake --build --preset vs2026; Pop-Location`
Expected: FAIL — `cannot open include file 'dvv/base/task.h'`.

- [ ] **Step 3: Implement** `dvv-base/include/dvv/base/task.h`

```cpp
#pragma once

// Coroutine task layer (spec §4): Task<T>, syncWait, resumeOn and co_try. Capped by design: no
// generators, no custom allocators. Tasks are lazy, move-only handles; awaiting one starts it and
// resumes the awaiting coroutine by symmetric transfer when it finishes.

#include <condition_variable>
#include <coroutine>
#include <exception>
#include <expected>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "dvv/base/error.h"
#include "dvv/base/executor.h"

namespace dvv {

template <class T = void>
class Task;

namespace detail {

template <class T>
inline constexpr bool IS_RESULT = false;
template <class U>
inline constexpr bool IS_RESULT<std::expected<U, Error>> = true;

class PromiseBase {
public:
    struct FinalAwaiter {
        [[nodiscard]] bool await_ready() const noexcept { return false; }
        template <class P>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<P> handle) const noexcept {
            return handle.promise().continuationOrNoop();
        }
        void await_resume() const noexcept {}
    };

    [[nodiscard]] std::suspend_always initial_suspend() const noexcept { return {}; }
    [[nodiscard]] FinalAwaiter final_suspend() const noexcept { return {}; }

    void setContinuation(std::coroutine_handle<> continuation) noexcept {
        continuation_ = continuation;
    }
    [[nodiscard]] std::coroutine_handle<> continuationOrNoop() const noexcept {
        if (continuation_) {
            return continuation_;
        }
        return std::noop_coroutine();
    }

private:
    std::coroutine_handle<> continuation_;
};

template <class T>
class Promise : public PromiseBase {
public:
    Task<T> get_return_object() noexcept;

    template <class U = T>
        requires std::convertible_to<U&&, T>
    void return_value(U&& value) {
        result_.template emplace<1>(std::forward<U>(value));
    }

    void unhandled_exception() noexcept { result_.template emplace<2>(std::current_exception()); }

    // Used by co_try: completes the task with an error.
    void failWith(Error error) noexcept
        requires IS_RESULT<T>
    {
        result_.template emplace<1>(std::unexpected(std::move(error)));
    }

    T takeResult() {
        if (result_.index() == 2) {
            std::rethrow_exception(std::get<2>(result_));
        }
        return std::move(std::get<1>(result_));
    }

private:
    std::variant<std::monostate, T, std::exception_ptr> result_;
};

template <>
class Promise<void> : public PromiseBase {
public:
    Task<void> get_return_object() noexcept;
    void return_void() const noexcept {}
    void unhandled_exception() noexcept { exception_ = std::current_exception(); }
    void takeResult() const {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
    }

private:
    std::exception_ptr exception_;
};

}  // namespace detail

template <class T>
class [[nodiscard]] Task {
public:
    using promise_type = detail::Promise<T>;
    using Handle = std::coroutine_handle<promise_type>;

    Task() noexcept = default;
    explicit Task(Handle handle) noexcept : handle_(handle) {}
    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() { reset(); }

    [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(handle_); }

    auto operator co_await() && noexcept { return Awaiter{handle_}; }

private:
    struct Awaiter {
        Handle handle;
        [[nodiscard]] bool await_ready() const noexcept { return false; }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) const noexcept {
            handle.promise().setContinuation(awaiting);
            return handle;
        }
        T await_resume() const { return handle.promise().takeResult(); }
    };

    void reset() noexcept {
        if (handle_) {
            handle_.destroy();
            handle_ = {};
        }
    }

    Handle handle_;
};

namespace detail {

template <class T>
Task<T> Promise<T>::get_return_object() noexcept {
    return Task<T>{std::coroutine_handle<Promise<T>>::from_promise(*this)};
}

inline Task<void> Promise<void>::get_return_object() noexcept {
    return Task<void>{std::coroutine_handle<Promise<void>>::from_promise(*this)};
}

struct SyncSignal {
    std::mutex mutex;
    std::condition_variable condition;
    bool done = false;

    void notify() {
        std::lock_guard lock(mutex);
        done = true;
        condition.notify_all();  // under the lock: the waiter may destroy us right after
    }
    void wait() {
        std::unique_lock lock(mutex);
        condition.wait(lock, [this] { return done; });
    }
};

class SyncDriver {
public:
    struct promise_type {
        SyncSignal* signal = nullptr;

        SyncDriver get_return_object() noexcept {
            return SyncDriver{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        [[nodiscard]] std::suspend_always initial_suspend() const noexcept { return {}; }
        [[nodiscard]] auto final_suspend() const noexcept {
            struct Awaiter {
                [[nodiscard]] bool await_ready() const noexcept { return false; }
                void await_suspend(std::coroutine_handle<promise_type> handle) const noexcept {
                    handle.promise().signal->notify();
                }
                void await_resume() const noexcept {}
            };
            return Awaiter{};
        }
        void return_void() const noexcept {}
        void unhandled_exception() const noexcept { std::terminate(); }
    };

    explicit SyncDriver(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) {}
    SyncDriver(SyncDriver&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    SyncDriver& operator=(SyncDriver&&) = delete;
    SyncDriver(const SyncDriver&) = delete;
    SyncDriver& operator=(const SyncDriver&) = delete;
    ~SyncDriver() {
        if (handle_) {
            handle_.destroy();
        }
    }

    void start(SyncSignal& signal) {
        handle_.promise().signal = &signal;
        handle_.resume();
    }

private:
    std::coroutine_handle<promise_type> handle_;
};

template <class T>
struct SyncResult {
    std::optional<T> value;
    std::exception_ptr error;
};

template <>
struct SyncResult<void> {
    std::exception_ptr error;
};

template <class T>
SyncDriver driveSync(Task<T> task, SyncResult<T>& result) {
    try {
        if constexpr (std::is_void_v<T>) {
            co_await std::move(task);
        } else {
            result.value.emplace(co_await std::move(task));
        }
    } catch (...) {
        result.error = std::current_exception();
    }
}

template <class T>
class TryAwaiter {
public:
    explicit TryAwaiter(Result<T> result) noexcept : result_(std::move(result)) {}

    [[nodiscard]] bool await_ready() const noexcept { return result_.has_value(); }

    template <class P>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<P> handle) noexcept {
        handle.promise().failWith(std::move(result_).error());
        return handle.promise().continuationOrNoop();
    }

    T await_resume() {
        if constexpr (!std::is_void_v<T>) {
            return std::move(*result_);
        }
    }

private:
    Result<T> result_;
};

template <class T>
[[nodiscard]] TryAwaiter<T> tryAwait(Result<T> result) noexcept {
    return TryAwaiter<T>(std::move(result));
}

}  // namespace detail

// Blocks the calling thread until the task finishes; rethrows its exception. For main() and tests.
template <class T>
T syncWait(Task<T> task) {
    detail::SyncResult<T> result;
    detail::SyncSignal signal;
    auto driver = detail::driveSync(std::move(task), result);
    driver.start(signal);
    signal.wait();
    if (result.error) {
        std::rethrow_exception(result.error);
    }
    if constexpr (!std::is_void_v<T>) {
        return std::move(*result.value);
    }
}

// co_await resumeOn(executor): continues the coroutine on the executor (never inline).
[[nodiscard]] inline auto resumeOn(IExecutor& executor) noexcept {
    struct Awaiter {
        IExecutor* executor;
        [[nodiscard]] bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle) const {
            executor->post([handle] { handle.resume(); });
        }
        void await_resume() const noexcept {}
    };
    return Awaiter{&executor};
}

}  // namespace dvv

// Inside a Task<Result<U>>: unwraps a Result<T> or completes the task with its error (spec §11).
#define co_try(...) (co_await ::dvv::detail::tryAwait(__VA_ARGS__))
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `Push-Location dvv-base; cmake --workflow --preset vs2026; Pop-Location`
Expected: `100% tests passed` in Debug and Release.

- [ ] **Step 5: Commit**

```powershell
git -C dvv-base add -A
git -C dvv-base commit -m "feat: add coroutine Task, syncWait, resumeOn and co_try" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12: `whenAll`

**Files:**
- Create: `dvv-base/include/dvv/base/when_all.h` (header-only)
- Test: `dvv-base/tests/test_when_all.cpp`
- Modify: add `test_when_all.cpp` to the test list

**Interfaces:**
- Consumes: `Task`, `resumeOn`, `syncWait` (Task 11), `ExecutorFactory` (Task 10).
- Produces: `template <class... T> Task<std::tuple<T...>> dvv::whenAll(Task<T>...)` and `template <class T> Task<std::vector<T>> dvv::whenAll(std::vector<Task<T>>)` (non-void `T`; results in argument order; the first exception in argument order is rethrown).

- [ ] **Step 1: Write the failing test** `dvv-base/tests/test_when_all.cpp`

```cpp
#include <doctest/doctest.h>

#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "dvv/base/executors.h"
#include "dvv/base/when_all.h"

namespace {

dvv::Task<int> value(int v) {
    co_return v;
}

dvv::Task<std::string> text(std::string t) {
    co_return t;
}

dvv::Task<int> squareOn(dvv::IExecutor& executor, int v) {
    co_await dvv::resumeOn(executor);
    const int square = v * v;
    co_return square;
}

dvv::Task<int> failing() {
    throw std::runtime_error("child failed");
    co_return 0;
}

dvv::Task<std::tuple<int, std::string>> pair() {
    co_return co_await dvv::whenAll(value(1), text("two"));
}

dvv::Task<std::vector<int>> squares(dvv::IExecutor& executor, int count) {
    std::vector<dvv::Task<int>> tasks;
    for (int i = 0; i < count; ++i) {
        tasks.push_back(squareOn(executor, i));
    }
    co_return co_await dvv::whenAll(std::move(tasks));
}

dvv::Task<std::tuple<int, int>> withFailure() {
    co_return co_await dvv::whenAll(value(1), failing());
}

}  // namespace

TEST_CASE("whenAll returns results in argument order") {
    const auto [number, word] = dvv::syncWait(pair());
    CHECK(number == 1);
    CHECK(word == "two");
}

TEST_CASE("whenAll over a vector runs tasks on a pool") {
    auto pool = dvv::ExecutorFactory::makeThreadPool(4).value();
    const auto results = dvv::syncWait(squares(*pool, 50));
    REQUIRE(results.size() == 50);
    for (int i = 0; i < 50; ++i) {
        CHECK(results[static_cast<std::size_t>(i)] == i * i);
    }
}

TEST_CASE("whenAll over an empty vector completes immediately") {
    auto pool = dvv::ExecutorFactory::makeThreadPool(1).value();
    CHECK(dvv::syncWait(squares(*pool, 0)).empty());
}

TEST_CASE("whenAll rethrows a child's exception") {
    CHECK_THROWS_AS(dvv::syncWait(withFailure()), std::runtime_error);
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `Push-Location dvv-base; cmake --build --preset vs2026; Pop-Location`
Expected: FAIL — `cannot open include file 'dvv/base/when_all.h'`.

- [ ] **Step 3: Implement** `dvv-base/include/dvv/base/when_all.h`

```cpp
#pragma once

// whenAll: starts several tasks and resumes when all have finished. The result order matches the
// argument order. If a task throws, the first exception (in argument order) is rethrown.

#include <array>
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <optional>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dvv/base/task.h"

namespace dvv {

namespace detail {

struct WhenAllCounter {
    std::atomic<std::size_t> remaining;
    std::coroutine_handle<> parent;
};

class WhenAllChild {
public:
    struct promise_type {
        WhenAllCounter* counter = nullptr;

        WhenAllChild get_return_object() noexcept {
            return WhenAllChild{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        [[nodiscard]] std::suspend_always initial_suspend() const noexcept { return {}; }
        [[nodiscard]] auto final_suspend() const noexcept {
            struct Awaiter {
                [[nodiscard]] bool await_ready() const noexcept { return false; }
                std::coroutine_handle<> await_suspend(
                    std::coroutine_handle<promise_type> handle) const noexcept {
                    WhenAllCounter* counter = handle.promise().counter;
                    if (counter->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                        return counter->parent;
                    }
                    return std::noop_coroutine();
                }
                void await_resume() const noexcept {}
            };
            return Awaiter{};
        }
        void return_void() const noexcept {}
        void unhandled_exception() const noexcept { std::terminate(); }
    };

    explicit WhenAllChild(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) {}
    WhenAllChild(WhenAllChild&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    WhenAllChild& operator=(WhenAllChild&&) = delete;
    WhenAllChild(const WhenAllChild&) = delete;
    WhenAllChild& operator=(const WhenAllChild&) = delete;
    ~WhenAllChild() {
        if (handle_) {
            handle_.destroy();
        }
    }

    void start(WhenAllCounter& counter) {
        handle_.promise().counter = &counter;
        handle_.resume();
    }

private:
    std::coroutine_handle<promise_type> handle_;
};

template <class T>
WhenAllChild runWhenAllChild(Task<T> task, std::optional<T>& slot, std::exception_ptr& error) {
    try {
        slot.emplace(co_await std::move(task));
    } catch (...) {
        error = std::current_exception();
    }
}

// Starts every child; the extra count held here keeps the parent suspended until all are started.
class WhenAllStart {
public:
    WhenAllStart(std::span<WhenAllChild> children, WhenAllCounter& counter) noexcept
        : children_(children), counter_(counter) {}

    [[nodiscard]] bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> parent) noexcept {
        counter_.parent = parent;
        for (WhenAllChild& child : children_) {
            child.start(counter_);
        }
        if (counter_.remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            return parent;
        }
        return std::noop_coroutine();
    }
    void await_resume() const noexcept {}

private:
    std::span<WhenAllChild> children_;
    WhenAllCounter& counter_;
};

}  // namespace detail

template <class... T>
Task<std::tuple<T...>> whenAll(Task<T>... tasks) {
    static_assert(sizeof...(T) > 0, "whenAll needs at least one task");
    static_assert((!std::is_void_v<T> && ...), "whenAll needs tasks with a value");
    std::tuple<std::optional<T>...> slots;
    std::array<std::exception_ptr, sizeof...(T)> errors{};
    detail::WhenAllCounter counter{sizeof...(T) + 1, {}};
    auto children = [&]<std::size_t... I>(std::index_sequence<I...>) {
        return std::array<detail::WhenAllChild, sizeof...(T)>{
            detail::runWhenAllChild(std::move(tasks), std::get<I>(slots), errors[I])...};
    }(std::index_sequence_for<T...>{});
    co_await detail::WhenAllStart(children, counter);
    for (const std::exception_ptr& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }
    co_return [&]<std::size_t... I>(std::index_sequence<I...>) {
        return std::tuple<T...>(std::move(*std::get<I>(slots))...);
    }(std::index_sequence_for<T...>{});
}

template <class T>
Task<std::vector<T>> whenAll(std::vector<Task<T>> tasks) {
    static_assert(!std::is_void_v<T>, "whenAll needs tasks with a value");
    std::vector<std::optional<T>> slots(tasks.size());
    std::vector<std::exception_ptr> errors(tasks.size());
    detail::WhenAllCounter counter{tasks.size() + 1, {}};
    std::vector<detail::WhenAllChild> children;
    children.reserve(tasks.size());
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        children.push_back(detail::runWhenAllChild(std::move(tasks[i]), slots[i], errors[i]));
    }
    co_await detail::WhenAllStart(children, counter);
    for (const std::exception_ptr& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }
    std::vector<T> values;
    values.reserve(slots.size());
    for (std::optional<T>& slot : slots) {
        values.push_back(std::move(*slot));
    }
    co_return values;
}

}  // namespace dvv
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `Push-Location dvv-base; cmake --workflow --preset vs2026; Pop-Location`
Expected: `100% tests passed` in Debug and Release.

- [ ] **Step 5: Commit**

```powershell
git -C dvv-base add -A
git -C dvv-base commit -m "feat: add whenAll for tuples and vectors of tasks" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 13: Logger

**Files:**
- Create: `dvv-base/include/dvv/base/log.h`, `dvv-base/src/log.cpp`
- Test: `dvv-base/tests/test_log.cpp`
- Modify: add `src/log.cpp` and `test_log.cpp` to their lists

**Interfaces:**
- Consumes: `Result`, `makeError`, `sourceFileName` (Task 6).
- Produces: `enum class dvv::LogLevel : std::uint8_t { Trace, Debug, Info, Warn, Error, Off }`; `std::string_view logLevelName(LogLevel)`; `struct LogRecord { LogLevel level; std::string_view message; std::source_location where; std::chrono::system_clock::time_point time; }`; `std::string formatLogRecord(const LogRecord&)` (`2026-10-20T12:34:56.789Z INFO  message [file.cpp:42]`); `ILogSink` (`write(const LogRecord&, std::string_view line)`, `flush()`); `StderrSink`, `MemorySink` (`lines()`), `RotatingFileSink::open(path, max_bytes, max_files) -> Result<std::shared_ptr<RotatingFileSink>>`; `Logger::instance()` (`addSink`, `clearSinks`, `setLevel`, `level`, `enabled`, `write`, `flush`); macros `DVV_LOG(level, fmt, args...)`, `DVV_LOG_TRACE/DEBUG/INFO/WARN/ERROR`. The Logger and sinks are process infrastructure, exempt from §9.4 like the registry (Task 19 records this in the spec).

- [ ] **Step 1: Write the failing test** `dvv-base/tests/test_log.cpp`

```cpp
#include <doctest/doctest.h>

#include <filesystem>
#include <memory>
#include <string>

#include "dvv/base/log.h"

namespace {

// Restores the global logger after each test.
struct LoggerScope {
    LoggerScope() { dvv::Logger::instance().clearSinks(); }
    ~LoggerScope() {
        dvv::Logger::instance().clearSinks();
        dvv::Logger::instance().setLevel(dvv::LogLevel::Info);
    }
    LoggerScope(const LoggerScope&) = delete;
    LoggerScope& operator=(const LoggerScope&) = delete;
};

std::filesystem::path freshDirectory(const std::string& name) {
    auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    return dir;
}

}  // namespace

TEST_CASE("log lines carry time, level, message and file name") {
    LoggerScope scope;
    auto sink = std::make_shared<dvv::MemorySink>();
    dvv::Logger::instance().addSink(sink);
    DVV_LOG_INFO("opened {} frames", 3);
    const auto lines = sink->lines();
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].starts_with("20"));
    CHECK(lines[0].find("Z INFO  opened 3 frames [test_log.cpp:") != std::string::npos);
}

TEST_CASE("messages below the level are dropped") {
    LoggerScope scope;
    auto sink = std::make_shared<dvv::MemorySink>();
    dvv::Logger::instance().addSink(sink);
    dvv::Logger::instance().setLevel(dvv::LogLevel::Warn);
    DVV_LOG_INFO("hidden");
    DVV_LOG_WARN("shown");
    const auto lines = sink->lines();
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].find("WARN  shown") != std::string::npos);
}

TEST_CASE("rotating file sink keeps at most max_files old files") {
    LoggerScope scope;
    const auto dir = freshDirectory("dvv-base-log-test");
    const auto path = dir / "test.log";
    auto sink = dvv::RotatingFileSink::open(path, 100, 2).value();
    dvv::Logger::instance().addSink(sink);
    for (int i = 0; i < 20; ++i) {
        DVV_LOG_INFO("line {:02}", i);
    }
    dvv::Logger::instance().flush();
    dvv::Logger::instance().clearSinks();
    sink.reset();
    CHECK(std::filesystem::exists(path));
    CHECK(std::filesystem::exists(dir / "test.log.1"));
    CHECK(std::filesystem::exists(dir / "test.log.2"));
    CHECK_FALSE(std::filesystem::exists(dir / "test.log.3"));
    for (const auto& file : {path, dir / "test.log.1", dir / "test.log.2"}) {
        CHECK(std::filesystem::file_size(file) <= 100);
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("rotating file sink rejects bad limits") {
    const auto dir = freshDirectory("dvv-base-log-bad");
    CHECK_FALSE(dvv::RotatingFileSink::open(dir / "x.log", 0, 1).has_value());
    CHECK_FALSE(dvv::RotatingFileSink::open(dir / "x.log", 10, 0).has_value());
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `Push-Location dvv-base; cmake --build --preset vs2026; Pop-Location`
Expected: FAIL — `cannot open include file 'dvv/base/log.h'`.

- [ ] **Step 3: Implement** `dvv-base/include/dvv/base/log.h`

```cpp
#pragma once

// Small std::format-based logger (spec §11). The Logger and its sinks are process infrastructure:
// like the LifetimeRegistry they must outlive the leak check, so they are not managed objects.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

#include "dvv/base/error.h"

namespace dvv {

enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warn, Error, Off };

[[nodiscard]] std::string_view logLevelName(LogLevel level) noexcept;

struct LogRecord {
    LogLevel level = LogLevel::Info;
    std::string_view message;
    std::source_location where;
    std::chrono::system_clock::time_point time;
};

// "2026-10-20T12:34:56.789Z INFO  message [file.cpp:42]"
[[nodiscard]] std::string formatLogRecord(const LogRecord& record);

class ILogSink {
public:
    virtual ~ILogSink() = default;
    // Called with the logger's lock held; line is formatLogRecord(record) without a newline.
    virtual void write(const LogRecord& record, std::string_view line) = 0;
    virtual void flush() {}
};

class StderrSink final : public ILogSink {
public:
    void write(const LogRecord& record, std::string_view line) override;
    void flush() override;
};

// Keeps lines in memory; for tests and the GUI log panel.
class MemorySink final : public ILogSink {
public:
    void write(const LogRecord& record, std::string_view line) override;
    [[nodiscard]] std::vector<std::string> lines() const;

private:
    mutable std::mutex mutex_;
    std::vector<std::string> lines_;
};

// Writes <path>; when it would exceed max_bytes it is renamed to <path>.1 (older files shift up to
// <path>.<max_files>) and a new file is started.
class RotatingFileSink final : public ILogSink {
public:
    [[nodiscard]] static Result<std::shared_ptr<RotatingFileSink>> open(std::filesystem::path path,
                                                                        std::uintmax_t max_bytes,
                                                                        int max_files);

    void write(const LogRecord& record, std::string_view line) override;
    void flush() override;

private:
    RotatingFileSink(std::filesystem::path path, std::uintmax_t max_bytes, int max_files,
                     std::ofstream stream, std::uintmax_t size);
    void rotate();

    std::filesystem::path path_;
    std::uintmax_t max_bytes_;
    int max_files_;
    std::ofstream stream_;
    std::uintmax_t size_;
};

class Logger {
public:
    static Logger& instance() noexcept;  // intentionally never destroyed

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    void addSink(std::shared_ptr<ILogSink> sink);
    void clearSinks();
    void setLevel(LogLevel level) noexcept;
    [[nodiscard]] LogLevel level() const noexcept;
    [[nodiscard]] bool enabled(LogLevel level) const noexcept;
    void write(LogLevel level, std::string_view message,
               std::source_location where = std::source_location::current());
    void flush();

private:
    Logger() = default;
    ~Logger() = default;

    std::mutex mutex_;
    std::vector<std::shared_ptr<ILogSink>> sinks_;
    std::atomic<LogLevel> level_{LogLevel::Info};
};

}  // namespace dvv

#define DVV_LOG(level, ...)                                                    \
    do {                                                                       \
        if (::dvv::Logger::instance().enabled(level)) {                        \
            ::dvv::Logger::instance().write((level), std::format(__VA_ARGS__), \
                                            std::source_location::current());  \
        }                                                                      \
    } while (false)

#define DVV_LOG_TRACE(...) DVV_LOG(::dvv::LogLevel::Trace, __VA_ARGS__)
#define DVV_LOG_DEBUG(...) DVV_LOG(::dvv::LogLevel::Debug, __VA_ARGS__)
#define DVV_LOG_INFO(...) DVV_LOG(::dvv::LogLevel::Info, __VA_ARGS__)
#define DVV_LOG_WARN(...) DVV_LOG(::dvv::LogLevel::Warn, __VA_ARGS__)
#define DVV_LOG_ERROR(...) DVV_LOG(::dvv::LogLevel::Error, __VA_ARGS__)
```

`dvv-base/src/log.cpp`:

```cpp
#include "dvv/base/log.h"

#include <cstdio>
#include <system_error>

namespace dvv {

std::string_view logLevelName(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Off: return "OFF";
    }
    return "?";
}

std::string formatLogRecord(const LogRecord& record) {
    const auto time = std::chrono::time_point_cast<std::chrono::milliseconds>(record.time);
    return std::format("{:%Y-%m-%dT%H:%M:%S}Z {:<5} {} [{}:{}]", time, logLevelName(record.level),
                       record.message, sourceFileName(record.where), record.where.line());
}

void StderrSink::write(const LogRecord& /*record*/, std::string_view line) {
    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fputc('\n', stderr);
}

void StderrSink::flush() {
    std::fflush(stderr);
}

void MemorySink::write(const LogRecord& /*record*/, std::string_view line) {
    std::lock_guard lock(mutex_);
    lines_.emplace_back(line);
}

std::vector<std::string> MemorySink::lines() const {
    std::lock_guard lock(mutex_);
    return lines_;
}

Result<std::shared_ptr<RotatingFileSink>> RotatingFileSink::open(std::filesystem::path path,
                                                                 std::uintmax_t max_bytes,
                                                                 int max_files) {
    if (max_bytes == 0 || max_files < 1) {
        return makeError(Errc::Internal, "rotating log needs max_bytes > 0 and max_files >= 1");
    }
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return makeError(Errc::Io,
                             std::format("cannot create log directory: {}", error.message()));
        }
    }
    std::uintmax_t size =
        std::filesystem::exists(path, error) ? std::filesystem::file_size(path, error) : 0;
    if (error) {
        size = 0;
    }
    std::ofstream stream(path, std::ios::binary | std::ios::app);
    if (!stream) {
        return makeError(Errc::Io,
                         std::format("cannot open log file {}", path.filename().string()));
    }
    return std::shared_ptr<RotatingFileSink>(
        new RotatingFileSink(std::move(path), max_bytes, max_files, std::move(stream), size));
}

RotatingFileSink::RotatingFileSink(std::filesystem::path path, std::uintmax_t max_bytes,
                                   int max_files, std::ofstream stream, std::uintmax_t size)
    : path_(std::move(path)),
      max_bytes_(max_bytes),
      max_files_(max_files),
      stream_(std::move(stream)),
      size_(size) {}

void RotatingFileSink::write(const LogRecord& /*record*/, std::string_view line) {
    const std::uintmax_t needed = line.size() + 1;
    if (size_ > 0 && size_ + needed > max_bytes_) {
        rotate();
    }
    stream_.write(line.data(), static_cast<std::streamsize>(line.size()));
    stream_.put('\n');
    size_ += needed;
}

void RotatingFileSink::flush() {
    stream_.flush();
}

void RotatingFileSink::rotate() {
    stream_.close();
    std::error_code error;
    auto numbered = [this](int index) {
        auto name = path_;
        name += std::format(".{}", index);
        return name;
    };
    std::filesystem::remove(numbered(max_files_), error);
    for (int index = max_files_ - 1; index >= 1; --index) {
        if (std::filesystem::exists(numbered(index), error)) {
            std::filesystem::rename(numbered(index), numbered(index + 1), error);
        }
    }
    std::filesystem::rename(path_, numbered(1), error);
    stream_.open(path_, std::ios::binary | std::ios::trunc);
    size_ = 0;
}

Logger& Logger::instance() noexcept {
    // Intentionally leaked; out of memory at start-up is fatal anyway.
    static auto* logger = new Logger();  // NOLINT(bugprone-unhandled-exception-at-new)
    return *logger;
}

void Logger::addSink(std::shared_ptr<ILogSink> sink) {
    std::lock_guard lock(mutex_);
    sinks_.push_back(std::move(sink));
}

void Logger::clearSinks() {
    std::lock_guard lock(mutex_);
    sinks_.clear();
}

void Logger::setLevel(LogLevel level) noexcept {
    level_.store(level, std::memory_order_relaxed);
}

LogLevel Logger::level() const noexcept {
    return level_.load(std::memory_order_relaxed);
}

bool Logger::enabled(LogLevel level) const noexcept {
    return level != LogLevel::Off && level >= this->level();
}

void Logger::write(LogLevel level, std::string_view message, std::source_location where) {
    if (!enabled(level)) {
        return;
    }
    const LogRecord record{level, message, where, std::chrono::system_clock::now()};
    const std::string line = formatLogRecord(record);
    std::lock_guard lock(mutex_);
    for (const auto& sink : sinks_) {
        sink->write(record, line);
    }
}

void Logger::flush() {
    std::lock_guard lock(mutex_);
    for (const auto& sink : sinks_) {
        sink->flush();
    }
}

}  // namespace dvv
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `Push-Location dvv-base; cmake --workflow --preset vs2026; Pop-Location`
Expected: `100% tests passed` in Debug and Release.

- [ ] **Step 5: Commit**

```powershell
git -C dvv-base add -A
git -C dvv-base commit -m "feat: add std::format logger with stderr, memory and rotating file sinks" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 14: `dvv::json` wrapper

**Files:**
- Create: `dvv-base/include/dvv/base/json.h`, `dvv-base/src/json.cpp`
- Test: `dvv-base/tests/test_json.cpp`
- Modify: add `src/json.cpp` and `test_json.cpp` to their lists

**Interfaces:**
- Consumes: `Result`, `makeError` (Task 6); nlohmann/json from the superbuild (Task 4).
- Produces: `using dvv::json::Value = nlohmann::json`; `Result<Value> dvv::json::parse(std::string_view)`; `std::string dvv::json::dump(const Value&, int indent = -1)`; `template <class T> Result<T> dvv::json::get(const Value& object, std::string_view key)` (missing key or wrong type → `Errc::Format`).

- [ ] **Step 1: Write the failing test** `dvv-base/tests/test_json.cpp`

```cpp
#include <doctest/doctest.h>

#include <string>

#include "dvv/base/json.h"

TEST_CASE("parse and typed access") {
    const auto value = dvv::json::parse(R"({"width": 1920, "format": "v210"})").value();
    CHECK(dvv::json::get<int>(value, "width").value() == 1920);
    CHECK(dvv::json::get<std::string>(value, "format").value() == "v210");
}

TEST_CASE("invalid JSON is a format error") {
    const auto value = dvv::json::parse("{\"width\": ");
    REQUIRE_FALSE(value.has_value());
    CHECK(value.error().code == dvv::Errc::Format);
}

TEST_CASE("missing keys and wrong types are format errors") {
    const auto value = dvv::json::parse(R"({"width": "wide"})").value();
    const auto missing = dvv::json::get<int>(value, "height");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().message.find("height") != std::string::npos);
    CHECK(dvv::json::get<int>(value, "width").error().code == dvv::Errc::Format);
    CHECK(dvv::json::get<int>(dvv::json::Value::array(), "x").error().code == dvv::Errc::Format);
}

TEST_CASE("dump") {
    dvv::json::Value value;
    value["a"] = 1;
    CHECK(dvv::json::dump(value) == R"({"a":1})");
    CHECK(dvv::json::dump(value, 2) == "{\n  \"a\": 1\n}");
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `Push-Location dvv-base; cmake --build --preset vs2026; Pop-Location`
Expected: FAIL — `cannot open include file 'dvv/base/json.h'`.

- [ ] **Step 3: Implement** `dvv-base/include/dvv/base/json.h`

```cpp
#pragma once

// Thin dvv::json wrapper over nlohmann/json: parsing and typed access return Result instead of
// throwing (spec §2).

#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "dvv/base/error.h"

namespace dvv::json {

using Value = nlohmann::json;

[[nodiscard]] Result<Value> parse(std::string_view text);

// indent < 0 gives the compact form.
[[nodiscard]] std::string dump(const Value& value, int indent = -1);

// Reads object[key] as T; Errc::Format when the key is missing or has the wrong type.
template <class T>
[[nodiscard]] Result<T> get(const Value& object, std::string_view key) {
    if (!object.is_object()) {
        return makeError(Errc::Format, "JSON value is not an object");
    }
    const auto found = object.find(key);
    if (found == object.end()) {
        return makeError(Errc::Format, std::format("missing JSON key '{}'", key));
    }
    try {
        return found->template get<T>();
    } catch (const nlohmann::json::exception& error) {
        return makeError(Errc::Format, std::format("JSON key '{}': {}", key, error.what()));
    }
}

}  // namespace dvv::json
```

`dvv-base/src/json.cpp`:

```cpp
#include "dvv/base/json.h"

namespace dvv::json {

Result<Value> parse(std::string_view text) {
    try {
        return Value::parse(text);
    } catch (const nlohmann::json::parse_error& error) {
        return makeError(Errc::Format, std::format("invalid JSON: {}", error.what()));
    }
}

std::string dump(const Value& value, int indent) {
    return value.dump(indent, ' ', false, nlohmann::json::error_handler_t::replace);
}

}  // namespace dvv::json
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `Push-Location dvv-base; cmake --workflow --preset vs2026; Pop-Location`
Expected: `100% tests passed` in Debug and Release.

- [ ] **Step 5: Commit**

```powershell
git -C dvv-base add -A
git -C dvv-base commit -m "feat: add dvv::json wrapper" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 15: URI parser (spec §7.2)

**Files:**
- Create: `dvv-base/include/dvv/base/uri.h`, `dvv-base/src/uri.cpp`
- Test: `dvv-base/tests/test_uri.cpp`
- Modify: add `src/uri.cpp` and `test_uri.cpp` to their lists

**Interfaces:**
- Consumes: `Result`, `makeError` (Task 6).
- Produces: `struct dvv::Uri { std::string scheme; std::string body; std::string path; std::vector<std::pair<std::string, std::string>> params; std::optional<std::string_view> param(std::string_view) const; bool operator==(const Uri&) const = default; }`; `Result<Uri> parseUri(std::string_view)`; `std::string formatUri(const Uri&)`; `Result<std::string> percentDecode(std::string_view)`; `std::string percentEncode(std::string_view, std::string_view keep = "/:\\")`. Schemes `file` and `synthetic` are hierarchical; every other scheme (e.g. `lavfi`) is opaque.

- [ ] **Step 1: Write the failing test** `dvv-base/tests/test_uri.cpp`

```cpp
#include <doctest/doctest.h>

#include "dvv/base/uri.h"

TEST_CASE("bare relative path means file:") {
    const auto uri = dvv::parseUri("clip_1920x1080_yuv420p10le.yuv").value();
    CHECK(uri.scheme == "file");
    CHECK(uri.path == "clip_1920x1080_yuv420p10le.yuv");
    CHECK(uri.params.empty());
}

TEST_CASE("Windows drive paths are paths, not schemes") {
    const auto uri = dvv::parseUri("X:\\media\\clip.yuv").value();
    CHECK(uri.scheme == "file");
    CHECK(uri.path == "X:\\media\\clip.yuv");
    CHECK(dvv::parseUri("X:/media/clip.yuv").value().path == "X:/media/clip.yuv");
}

TEST_CASE("bare paths keep percent signs and accept parameters") {
    const auto uri = dvv::parseUri("/data/100%.yuv?fmt=v210").value();
    CHECK(uri.path == "/data/100%.yuv");
    CHECK(uri.param("fmt") == "v210");
}

TEST_CASE("file: URIs with parameters") {
    const auto uri = dvv::parseUri("file:clip.yuv?fmt=v210&size=1920x1080&fps=50").value();
    CHECK(uri.scheme == "file");
    CHECK(uri.path == "clip.yuv");
    REQUIRE(uri.params.size() == 3);
    CHECK(uri.param("size") == "1920x1080");
    CHECK(uri.param("fps") == "50");
    CHECK_FALSE(uri.param("missing").has_value());
}

TEST_CASE("file: URIs are percent-decoded and accept an empty authority") {
    CHECK(dvv::parseUri("file:a%20b.yuv").value().path == "a b.yuv");
    CHECK(dvv::parseUri("file:///data/clip.yuv").value().path == "/data/clip.yuv");
    CHECK(dvv::parseUri("file:///X:/media/clip.yuv").value().path == "X:/media/clip.yuv");
    CHECK(dvv::parseUri("file://localhost/data/a.yuv").value().path == "/data/a.yuv");
    CHECK(dvv::parseUri("FILE:clip.yuv").value().scheme == "file");
}

TEST_CASE("remote authorities are unsupported") {
    const auto uri = dvv::parseUri("file://server/share/clip.yuv");
    REQUIRE_FALSE(uri.has_value());
    CHECK(uri.error().code == dvv::Errc::Unsupported);
}

TEST_CASE("synthetic: URIs") {
    const auto uri = dvv::parseUri("synthetic:gradient?w=33&h=17&fmt=v210").value();
    CHECK(uri.scheme == "synthetic");
    CHECK(uri.path == "gradient");
    CHECK(uri.param("w") == "33");
    CHECK(uri.param("impl") == std::nullopt);
}

TEST_CASE("lavfi: bodies are opaque") {
    const auto uri = dvv::parseUri("lavfi:testsrc2=s=3840x2160:r=60,format=yuv422p10le").value();
    CHECK(uri.scheme == "lavfi");
    CHECK(uri.body == "testsrc2=s=3840x2160:r=60,format=yuv422p10le");
    CHECK(uri.path.empty());
    CHECK(uri.params.empty());
}

TEST_CASE("invalid URIs") {
    CHECK(dvv::parseUri("").error().code == dvv::Errc::Format);
    CHECK(dvv::parseUri("file:a%2").error().code == dvv::Errc::Format);
    CHECK(dvv::parseUri("file:a%zz.yuv").error().code == dvv::Errc::Format);
    CHECK(dvv::parseUri("file:?fmt=v210").error().code == dvv::Errc::Format);
}

TEST_CASE("formatUri round-trips") {
    for (const char* text :
         {"file:clip.yuv?fmt=v210&fps=50", "file:a%20b.yuv", "synthetic:gradient?w=33&h=17",
          "lavfi:testsrc2=s=64x64", "file:X:/media/clip.yuv"}) {
        const auto uri = dvv::parseUri(text).value();
        CHECK(dvv::parseUri(dvv::formatUri(uri)).value() == uri);
    }
    CHECK(dvv::formatUri(dvv::parseUri("file:a%20b.yuv").value()) == "file:a%20b.yuv");
}

TEST_CASE("percent encoding") {
    CHECK(dvv::percentEncode("a b&c") == "a%20b%26c");
    CHECK(dvv::percentDecode("a%20b%26c").value() == "a b&c");
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `Push-Location dvv-base; cmake --build --preset vs2026; Pop-Location`
Expected: FAIL — `cannot open include file 'dvv/base/uri.h'`.

- [ ] **Step 3: Implement** `dvv-base/include/dvv/base/uri.h`

```cpp
#pragma once

// Small RFC 3986 subset for source URIs (spec §7.2).
//   clip.yuv, X:\media\clip.yuv, /data/clip.yuv   bare paths mean file:
//   file:clip_1920x1080.yuv?fmt=v210&fps=50        path + query parameters (percent-decoded)
//   file:///X:/media/a%20b.yuv                     empty or "localhost" authority only
//   synthetic:gradient?w=33&h=17&fmt=v210          like file:
//   lavfi:testsrc2=s=3840x2160:r=60                any other scheme is opaque: body kept unchanged

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dvv/base/error.h"

namespace dvv {

struct Uri {
    std::string scheme;  // lower case; "file" for bare paths
    std::string body;    // everything after "scheme:" exactly as given (the whole text for paths)
    std::string path;    // decoded path for file: and synthetic:, empty for opaque schemes
    std::vector<std::pair<std::string, std::string>> params;  // decoded, in order

    [[nodiscard]] std::optional<std::string_view> param(std::string_view name) const;
    bool operator==(const Uri&) const = default;
};

[[nodiscard]] Result<Uri> parseUri(std::string_view text);

// Canonical text: parseUri(formatUri(uri)) == uri for every parsed uri with a hierarchical scheme.
[[nodiscard]] std::string formatUri(const Uri& uri);

[[nodiscard]] Result<std::string> percentDecode(std::string_view text);
[[nodiscard]] std::string percentEncode(std::string_view text, std::string_view keep = "/:\\");

}  // namespace dvv
```

`dvv-base/src/uri.cpp`:

```cpp
#include "dvv/base/uri.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace dvv {

namespace {

bool isAlpha(char c) noexcept {
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

bool isHierarchical(std::string_view scheme) noexcept {
    return scheme == "file" || scheme == "synthetic";
}

// "X:" followed by a separator or the end: a Windows drive, not a scheme.
bool startsWithDrive(std::string_view text) noexcept {
    return text.size() >= 2 && isAlpha(text[0]) && text[1] == ':' &&
           (text.size() == 2 || text[2] == '\\' || text[2] == '/');
}

std::optional<std::string> schemeOf(std::string_view text) {
    const auto colon = text.find(':');
    if (colon == std::string_view::npos || colon < 2 || startsWithDrive(text)) {
        return std::nullopt;
    }
    const std::string_view candidate = text.substr(0, colon);
    if (!isAlpha(candidate[0])) {
        return std::nullopt;
    }
    for (char c : candidate) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '+' && c != '-' && c != '.') {
            return std::nullopt;
        }
    }
    std::string scheme(candidate);
    std::ranges::transform(scheme, scheme.begin(),
                           [](char c) { return static_cast<char>(std::tolower(c)); });
    return scheme;
}

int hexValue(char c) noexcept {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

Result<std::vector<std::pair<std::string, std::string>>> parseQuery(std::string_view query) {
    std::vector<std::pair<std::string, std::string>> params;
    while (!query.empty()) {
        const auto amp = query.find('&');
        const std::string_view item = query.substr(0, amp);
        query = amp == std::string_view::npos ? std::string_view{} : query.substr(amp + 1);
        if (item.empty()) {
            continue;
        }
        const auto equals = item.find('=');
        auto name = percentDecode(item.substr(0, equals));
        if (!name) {
            return std::unexpected(std::move(name).error());
        }
        auto value = percentDecode(equals == std::string_view::npos ? std::string_view{}
                                                                    : item.substr(equals + 1));
        if (!value) {
            return std::unexpected(std::move(value).error());
        }
        params.emplace_back(std::move(*name), std::move(*value));
    }
    return params;
}

}  // namespace

std::optional<std::string_view> Uri::param(std::string_view name) const {
    for (const auto& [key, value] : params) {
        if (key == name) {
            return value;
        }
    }
    return std::nullopt;
}

Result<std::string> percentDecode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') {
            out += text[i];
            continue;
        }
        if (i + 2 >= text.size()) {
            return makeError(Errc::Format, "truncated percent escape in URI");
        }
        const int high = hexValue(text[i + 1]);
        const int low = hexValue(text[i + 2]);
        if (high < 0 || low < 0) {
            return makeError(Errc::Format, "invalid percent escape in URI");
        }
        out += static_cast<char>(high * 16 + low);
        i += 2;
    }
    return out;
}

std::string percentEncode(std::string_view text, std::string_view keep) {
    static constexpr char HEX[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        const bool unreserved = std::isalnum(byte) != 0 || c == '-' || c == '.' || c == '_' ||
                                c == '~' || keep.find(c) != std::string_view::npos;
        if (unreserved && byte < 0x80) {
            out += c;
        } else {
            out += '%';
            out += HEX[byte >> 4];
            out += HEX[byte & 0x0F];
        }
    }
    return out;
}

Result<Uri> parseUri(std::string_view text) {
    if (text.empty()) {
        return makeError(Errc::Format, "empty URI");
    }
    Uri uri;
    const auto scheme = schemeOf(text);
    if (!scheme) {
        // Bare path: no percent-decoding (paths may contain '%'); '?' starts parameters.
        uri.scheme = "file";
        uri.body = std::string(text);
        const auto question = text.find('?');
        uri.path = std::string(text.substr(0, question));
        if (question != std::string_view::npos) {
            auto params = parseQuery(text.substr(question + 1));
            if (!params) {
                return std::unexpected(std::move(params).error());
            }
            uri.params = std::move(*params);
        }
        if (uri.path.empty()) {
            return makeError(Errc::Format, "URI has an empty path");
        }
        return uri;
    }

    uri.scheme = *scheme;
    uri.body = std::string(text.substr(scheme->size() + 1));
    if (!isHierarchical(uri.scheme)) {
        return uri;
    }

    std::string_view rest = uri.body;
    const auto question = rest.find('?');
    std::string_view path = rest.substr(0, question);
    if (path.starts_with("//")) {
        path.remove_prefix(2);
        const auto slash = path.find('/');
        const std::string_view authority = path.substr(0, slash);
        if (!authority.empty() && authority != "localhost") {
            return makeError(Errc::Unsupported,
                             std::format("URI authority '{}' is not supported", authority));
        }
        path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash);
        if (path.size() >= 3 && path[0] == '/' && startsWithDrive(path.substr(1))) {
            path.remove_prefix(1);  // file:///X:/media → X:/media
        }
    }
    auto decoded = percentDecode(path);
    if (!decoded) {
        return std::unexpected(std::move(decoded).error());
    }
    uri.path = std::move(*decoded);
    if (uri.path.empty()) {
        return makeError(Errc::Format, "URI has an empty path");
    }
    if (question != std::string_view::npos) {
        auto params = parseQuery(rest.substr(question + 1));
        if (!params) {
            return std::unexpected(std::move(params).error());
        }
        uri.params = std::move(*params);
    }
    return uri;
}

std::string formatUri(const Uri& uri) {
    if (!isHierarchical(uri.scheme)) {
        return uri.scheme + ":" + uri.body;
    }
    std::string text = uri.scheme + ":" + percentEncode(uri.path);
    char separator = '?';
    for (const auto& [name, value] : uri.params) {
        text += separator;
        text += percentEncode(name, "");
        text += '=';
        text += percentEncode(value, ":");
        separator = '&';
    }
    return text;
}

}  // namespace dvv
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `Push-Location dvv-base; cmake --workflow --preset vs2026; Pop-Location`
Expected: `100% tests passed` in Debug and Release.

- [ ] **Step 5: Commit**

```powershell
git -C dvv-base add -A
git -C dvv-base commit -m "feat: add source URI parser" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 16: Per-user paths

**Files:**
- Create: `dvv-base/include/dvv/base/paths.h`, `dvv-base/src/paths.cpp`
- Test: `dvv-base/tests/test_paths.cpp`
- Modify: add `src/paths.cpp` and `test_paths.cpp`; `dvv-base/CMakeLists.txt` and `dvv-base/tests/CMakeLists.txt` now equal their final forms

**Interfaces:**
- Consumes: `Result`, `makeError`, `DVV_TRY`, `DVV_TRY_VOID`, `getEnvironment` (Task 6).
- Produces: `Result<std::filesystem::path> dvv::paths::userDataDir(std::string_view app)`, `userCacheDir(std::string_view app)`, `executablePath()`, `Result<void> ensureDirectory(const std::filesystem::path&)`.

- [ ] **Step 1: Write the failing test** `dvv-base/tests/test_paths.cpp`

```cpp
#include <doctest/doctest.h>

#include <filesystem>

#include "dvv/base/environment.h"
#include "dvv/base/paths.h"

TEST_CASE("user data and cache directories are absolute and end in the app name") {
    const auto data = dvv::paths::userDataDir("rawreel").value();
    CHECK(data.is_absolute());
    CHECK(data.filename() == "rawreel");
    const auto cache = dvv::paths::userCacheDir("rawreel").value();
    CHECK(cache.is_absolute());
    CHECK(cache.string().find("rawreel") != std::string::npos);
    CHECK(cache != data);
}

TEST_CASE("app names must be plain directory names") {
    CHECK_FALSE(dvv::paths::userDataDir("").has_value());
    CHECK_FALSE(dvv::paths::userDataDir("a/b").has_value());
    CHECK_FALSE(dvv::paths::userCacheDir("a\\b").has_value());
}

#if !defined(_WIN32)
TEST_CASE("XDG variables are honoured when absolute") {
    const auto saved = dvv::getEnvironment("XDG_DATA_HOME");
    dvv::setEnvironment("XDG_DATA_HOME", "/tmp/dvv-xdg");
    CHECK(dvv::paths::userDataDir("rawreel").value() == "/tmp/dvv-xdg/rawreel");
    dvv::setEnvironment("XDG_DATA_HOME", "relative/dir");
    CHECK(dvv::paths::userDataDir("rawreel").value().is_absolute());
    dvv::setEnvironment("XDG_DATA_HOME", saved.value_or(""));
}
#endif

TEST_CASE("executable path exists") {
    const auto path = dvv::paths::executablePath().value();
    CHECK(path.is_absolute());
    CHECK(std::filesystem::exists(path));
}

TEST_CASE("ensureDirectory creates nested directories") {
    const auto dir = std::filesystem::temp_directory_path() / "dvv-base-paths-test" / "a" / "b";
    std::filesystem::remove_all(dir.parent_path().parent_path());
    REQUIRE(dvv::paths::ensureDirectory(dir).has_value());
    CHECK(std::filesystem::is_directory(dir));
    std::filesystem::remove_all(dir.parent_path().parent_path());
}

TEST_CASE("environment round trip") {
    REQUIRE(dvv::setEnvironment("DVV_BASE_TEST_VARIABLE", "value"));
    CHECK(dvv::getEnvironment("DVV_BASE_TEST_VARIABLE") == "value");
    REQUIRE(dvv::setEnvironment("DVV_BASE_TEST_VARIABLE", ""));
    CHECK_FALSE(dvv::getEnvironment("DVV_BASE_TEST_VARIABLE").has_value());
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `Push-Location dvv-base; cmake --build --preset vs2026; Pop-Location`
Expected: FAIL — `cannot open include file 'dvv/base/paths.h'`.

- [ ] **Step 3: Implement** `dvv-base/include/dvv/base/paths.h`

```cpp
#pragma once

// Per-user directories and the executable location, discovered at run time (spec §10.4). Nothing
// machine-specific is ever compiled in.
//   Windows: data = FOLDERID_RoamingAppData\<app>, cache = FOLDERID_LocalAppData\<app>\cache
//   Linux:   data = $XDG_DATA_HOME/<app> or ~/.local/share/<app>,
//            cache = $XDG_CACHE_HOME/<app> or ~/.cache/<app>

#include <filesystem>
#include <string_view>

#include "dvv/base/error.h"

namespace dvv::paths {

[[nodiscard]] Result<std::filesystem::path> userDataDir(std::string_view app);
[[nodiscard]] Result<std::filesystem::path> userCacheDir(std::string_view app);
[[nodiscard]] Result<std::filesystem::path> executablePath();

// Creates the directory and its parents if needed.
[[nodiscard]] Result<void> ensureDirectory(const std::filesystem::path& directory);

}  // namespace dvv::paths
```

`dvv-base/src/paths.cpp` (the `clang-format off` block keeps `<windows.h>` first):

```cpp
#include "dvv/base/paths.h"

#include <format>
#include <string>
#include <system_error>
#include <vector>

#include "dvv/base/environment.h"

#if defined(_WIN32)
// windows.h must come first.
// clang-format off
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>
// clang-format on
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace dvv::paths {

namespace {

Result<void> checkApp(std::string_view app) {
    if (app.empty() || app.find_first_of("/\\:") != std::string_view::npos) {
        return makeError(Errc::Internal, "application name must be a plain directory name");
    }
    return {};
}

#if defined(_WIN32)
Result<std::filesystem::path> knownFolder(const KNOWNFOLDERID& id) {
    PWSTR raw = nullptr;
    const HRESULT result = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw);
    if (FAILED(result) || raw == nullptr) {
        CoTaskMemFree(raw);
        return makeError(Errc::Io, "cannot query a Windows known folder");
    }
    std::filesystem::path folder(raw);
    CoTaskMemFree(raw);
    return folder;
}
#else
// $variable when it holds an absolute path, otherwise $HOME/<fallback>.
Result<std::filesystem::path> xdgDir(std::string_view variable, std::string_view fallback) {
    if (const auto value = getEnvironment(variable)) {
        std::filesystem::path dir(*value);
        if (dir.is_absolute()) {
            return dir;
        }
    }
    const auto home = getEnvironment("HOME");
    if (!home || home->empty()) {
        return makeError(Errc::Io, "HOME is not set");
    }
    return std::filesystem::path(*home) / fallback;
}
#endif

}  // namespace

Result<std::filesystem::path> userDataDir(std::string_view app) {
    DVV_TRY_VOID(checkApp(app));
#if defined(_WIN32)
    DVV_TRY(auto base, knownFolder(FOLDERID_RoamingAppData));
#else
    DVV_TRY(auto base, xdgDir("XDG_DATA_HOME", ".local/share"));
#endif
    return base / app;
}

Result<std::filesystem::path> userCacheDir(std::string_view app) {
    DVV_TRY_VOID(checkApp(app));
#if defined(_WIN32)
    DVV_TRY(auto base, knownFolder(FOLDERID_LocalAppData));
    return base / app / "cache";
#else
    DVV_TRY(auto base, xdgDir("XDG_CACHE_HOME", ".cache"));
    return base / app;
#endif
}

Result<std::filesystem::path> executablePath() {
#if defined(_WIN32)
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return makeError(Errc::Io, "GetModuleFileNameW failed");
        }
        if (length < buffer.size()) {
            return std::filesystem::path(std::wstring(buffer.data(), length));
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__linux__)
    std::error_code error;
    auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        return makeError(Errc::Io, std::format("cannot read /proc/self/exe: {}", error.message()));
    }
    return path;
#else
    return makeError(Errc::Unsupported, "executablePath is not implemented on this platform");
#endif
}

Result<void> ensureDirectory(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return makeError(Errc::Io, std::format("cannot create directory: {}", error.message()));
    }
    return {};
}

}  // namespace dvv::paths
```

Final `dvv-base/CMakeLists.txt` (compare; it must match exactly):

```cmake
cmake_minimum_required(VERSION 4.2)
project(dvv_base VERSION 0.1.0 DESCRIPTION "dvv base library" LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DvvCompilerOptions)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(DVV_BUILD_TESTS "Build the dvv tests" ${PROJECT_IS_TOP_LEVEL})

find_package(Threads REQUIRED)
find_package(nlohmann_json 3.12 REQUIRED CONFIG)

add_library(dvv_base STATIC
    src/environment.cpp
    src/error.cpp
    src/executors.cpp
    src/json.cpp
    src/leak_check.cpp
    src/lifetime.cpp
    src/log.cpp
    src/paths.cpp
    src/uri.cpp)
add_library(dvv::base ALIAS dvv_base)
set_target_properties(dvv_base PROPERTIES EXPORT_NAME base)
target_include_directories(dvv_base PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(dvv_base PUBLIC Threads::Threads nlohmann_json::nlohmann_json)
if(WIN32)
    target_link_libraries(dvv_base PRIVATE shell32 ole32)
endif()
dvv_set_compiler_options(dvv_base)

install(TARGETS dvv_base EXPORT dvv_baseTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
set(config_dir ${CMAKE_INSTALL_LIBDIR}/cmake/dvv_base)
install(EXPORT dvv_baseTargets NAMESPACE dvv:: DESTINATION ${config_dir})
configure_package_config_file(cmake/dvv_baseConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/dvv_baseConfig.cmake INSTALL_DESTINATION ${config_dir})
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/dvv_baseConfigVersion.cmake
    COMPATIBILITY SameMinorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/dvv_baseConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/dvv_baseConfigVersion.cmake
    cmake/DvvCompilerOptions.cmake
    DESTINATION ${config_dir})

if(DVV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

Final `dvv-base/tests/CMakeLists.txt` (compare; it must match exactly):

```cmake
find_package(doctest 2.5 REQUIRED CONFIG)

add_executable(dvv_base_tests
    main.cpp
    test_error.cpp
    test_executors.cpp
    test_json.cpp
    test_lifetime.cpp
    test_log.cpp
    test_paths.cpp
    test_task.cpp
    test_uri.cpp
    test_when_all.cpp)
target_link_libraries(dvv_base_tests PRIVATE dvv::base doctest::doctest)
# doctest otherwise only forward-declares std::ostream, which breaks printing std types.
target_compile_definitions(dvv_base_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
dvv_set_compiler_options(dvv_base_tests)
add_test(NAME dvv_base.unit COMMAND dvv_base_tests)
set_tests_properties(dvv_base.unit PROPERTIES
    ENVIRONMENT DVV_LEAK_CHECK=fail
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")

# Package test (spec §3.1): install, then build a consumer with find_package(dvv_base).
# Skipped for sanitizer builds, whose consumers would need the same runtime flags.
if(NOT DVV_SANITIZE)
    string(REPLACE ";" "," deps_prefix_path "${CMAKE_PREFIX_PATH}")
    add_test(NAME dvv_base.package
             COMMAND ${CMAKE_COMMAND}
                     -DBUILD_DIR=${CMAKE_BINARY_DIR}
                     -DCONFIG=$<CONFIG>
                     -DPREFIX=${CMAKE_CURRENT_BINARY_DIR}/package-prefix
                     -DCONSUMER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/package
                     -DCONSUMER_BUILD=${CMAKE_CURRENT_BINARY_DIR}/package-consumer
                     -DGENERATOR=${CMAKE_GENERATOR}
                     -DPLATFORM=${CMAKE_GENERATOR_PLATFORM}
                     -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
                     -DDEPS_PREFIX_PATH=${deps_prefix_path}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/package/run-package-test.cmake)
endif()

# Compile-fail tests (spec §9.4). Case 0 is the control and must build; cases 1-3 must not.
set(compile_fail_source ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/direct_construction.cpp)
add_executable(dvv_base_compile_ok ${compile_fail_source})
target_compile_definitions(dvv_base_compile_ok PRIVATE DVV_COMPILE_FAIL_CASE=0)
target_link_libraries(dvv_base_compile_ok PRIVATE dvv::base)
dvv_set_compiler_options(dvv_base_compile_ok)
foreach(case IN ITEMS 1 2 3)
    set(target dvv_base_compile_fail_${case})
    add_executable(${target} EXCLUDE_FROM_ALL ${compile_fail_source})
    target_compile_definitions(${target} PRIVATE DVV_COMPILE_FAIL_CASE=${case})
    target_link_libraries(${target} PRIVATE dvv::base)
    dvv_set_compiler_options(${target})
    add_test(NAME dvv_base.compile_fail.${case}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${target}
                     --config $<CONFIG>)
    set_tests_properties(dvv_base.compile_fail.${case} PROPERTIES WILL_FAIL TRUE)
endforeach()
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `Push-Location dvv-base; cmake --workflow --preset vs2026; Pop-Location`
Expected: Debug and Release: `100% tests passed, 0 tests failed out of 5`; running `_build/dvv-base/vs2026/tests/Debug/dvv_base_tests.exe` reports `test cases: 54 | 54 passed` on Windows (55 on Linux, which adds the XDG case).

- [ ] **Step 5: Commit**

```powershell
git -C dvv-base add -A
git -C dvv-base commit -m "feat: add per-user data, cache and executable paths" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 17: Linux and sanitizer verification

**Files:** none new (fixes go into the files they concern).

- [ ] **Step 1: Run every Linux workflow in WSL**

```powershell
wsl -d Ubuntu-24.04 -e bash -lc 'cd dvv-base && for p in ninja-clang ninja-clang-asan ninja-clang-tsan; do cmake --workflow --preset $p 2>&1 | grep -E "tests passed|tests failed|error"; done'
```

Expected: every line reads `100% tests passed` (Clang: 5 tests in Debug and Release; ASan and TSan: 4 tests, no package test).

- [ ] **Step 2: Run MSVC AddressSanitizer**

Run: `Push-Location dvv-base; cmake --workflow --preset vs2026-asan; Pop-Location`
Expected: `100% tests passed, 0 tests failed out of 4`.

- [ ] **Step 3: Format and tidy**

```powershell
wsl -d Ubuntu-24.04 -e bash -lc 'cd dvv-base && git ls-files -z "*.h" "*.cpp" | xargs -0 clang-format --dry-run --Werror && git ls-files -z "src/*.cpp" | xargs -0 clang-tidy -p ../_build/dvv-base/ninja-clang --quiet && echo LINT-OK'
```

Expected: `LINT-OK` and no diagnostics. If clang-format reports a file, run `clang-format -i` on it, rebuild, retest and commit `style: apply clang-format`.

- [ ] **Step 4: Check that binaries carry no machine paths (Clang)**

Run: `wsl -d Ubuntu-24.04 -e bash -lc 'strings _build/dvv-base/ninja-clang/tests/Release/dvv_base_tests | grep -c "$(pwd)" || true'`
Expected: `0`.

- [ ] **Step 5: Commit any fixes** (skip if nothing changed)

```powershell
git -C dvv-base add -A
git -C dvv-base commit -m "fix: address Linux and sanitizer findings" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 18: CI workflows

**Files:**
- Create: `rawreel-workspace/.github/workflows/dvv-ci.yml`, `rawreel-workspace/.github/workflows/workspace.yml`, `dvv-base/.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `tools/requirements.txt` (Task 1), bootstrap scripts (Task 5), presets (Task 5), privacy lint (Task 3), tool tests (Tasks 2–4).
- Produces: reusable workflow `dvv-ci.yml` (`workflow_call` input `repo`) with build jobs `windows-msvc`, `windows-msvc-asan`, `linux-clang`, `linux-clang-asan`, `linux-clang-tsan` and a `lint` job (privacy, format, tidy); `workspace.yml` with `tools` (every push/PR) and nightly `integration`. Linux Wayland build-only and lavapipe jobs arrive with the code that needs them (M1b, M1a); macOS jobs only after the user's approval.

- [ ] **Step 1: Create `rawreel-workspace/.github/workflows/dvv-ci.yml`**

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
        run: sudo apt-get update && sudo apt-get install -y clang-19 libstdc++-14-dev
      - name: Check Visual Studio 2026
        if: runner.os == 'Windows'
        shell: pwsh
        run: |
          $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
          $version = & $vswhere -latest -property catalog_productDisplayVersion
          if (-not $version.StartsWith('18.')) { throw "Visual Studio 2026 (18.x) required, found $version" }
          Write-Host "Visual Studio $version"
      - uses: actions/cache@v4
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
      - name: Build and test
        working-directory: ${{ inputs.repo }}
        run: cmake --workflow --preset ${{ matrix.preset }}

  lint:
    runs-on: ubuntu-24.04
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
      - name: Privacy lint
        run: cmake "-DREPO=${{ inputs.repo }}" -DMODE=commits -P rawreel-workspace/tools/privacy-lint.cmake
      - name: Format
        working-directory: ${{ inputs.repo }}
        run: git ls-files -z '*.h' '*.cpp' | xargs -0 -r clang-format --dry-run --Werror
      - name: Bootstrap
        run: ./rawreel-workspace/bootstrap.sh --toolchain clang
      - name: Tidy
        working-directory: ${{ inputs.repo }}
        run: |
          cmake --preset ninja-clang
          git ls-files -z 'src/*.cpp' |
            xargs -0 -r clang-tidy -p "../_build/${{ inputs.repo }}/ninja-clang" --quiet
```

- [ ] **Step 2: Create `rawreel-workspace/.github/workflows/workspace.yml`**

```yaml
# CI of rawreel-workspace itself (spec §13.4): tool tests on every change, and a nightly build of
# all repositories at main from source.
name: workspace

on:
  push:
    branches: [main]
  pull_request:
  schedule:
    - cron: "0 3 * * *"
  workflow_dispatch:

permissions:
  contents: read

jobs:
  tools:
    name: tools-${{ matrix.os }}
    runs-on: ${{ matrix.os }}
    strategy:
      fail-fast: false
      matrix:
        os: [ubuntu-24.04, windows-2025]
    steps:
      - uses: actions/checkout@v5
        with:
          fetch-depth: 0
      - uses: actions/setup-python@v6
        with:
          python-version: "3.13"
      - name: Install tools
        run: python -m pip install -r tools/requirements.txt
      - name: SPDX gate tests
        run: |
          cmake -P tests/spdx/test_spdx.cmake
          cmake -P tests/spdx/test_third_party_gate.cmake
      - name: Privacy lint tests
        run: cmake "-DWORK_DIR=${{ runner.temp }}/privacy-lint-test" -P tests/privacy/test_privacy_lint.cmake
      - name: Privacy lint
        run: cmake "-DREPO=${{ github.workspace }}" -DMODE=commits -P tools/privacy-lint.cmake

  integration:
    if: github.event_name == 'schedule' || github.event_name == 'workflow_dispatch'
    name: integration-${{ matrix.preset }}
    runs-on: ${{ matrix.os }}
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
      - name: Bootstrap (Windows)
        if: runner.os == 'Windows'
        shell: pwsh
        run: ./rawreel-workspace/bootstrap.ps1 -Toolchain ${{ matrix.toolchain }}
      - name: Bootstrap (Linux)
        if: runner.os == 'Linux'
        run: ./rawreel-workspace/bootstrap.sh --toolchain ${{ matrix.toolchain }}
      - name: Build and test everything
        working-directory: rawreel-workspace
        run: cmake --workflow --preset ${{ matrix.preset }}
```

- [ ] **Step 3: Create `dvv-base/.github/workflows/ci.yml`**

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
      repo: dvv-base
```

- [ ] **Step 4: Validate with actionlint** (pip wheel, no install outside the user site)

```powershell
python -m pip install --target _build/tools/actionlint actionlint-py
& _build/tools/actionlint/bin/actionlint.exe -shellcheck= rawreel-workspace/.github/workflows/dvv-ci.yml rawreel-workspace/.github/workflows/workspace.yml dvv-base/.github/workflows/ci.yml
```

Expected: no output, exit code 0.

- [ ] **Step 5: Commit**

```powershell
git -C rawreel-workspace add .github
git -C rawreel-workspace commit -m "ci: add reusable dvv workflow and workspace workflow" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git -C dvv-base add .github
git -C dvv-base commit -m "ci: call the reusable dvv workflow" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 19: Documentation, spec notes and final local verification

**Files:**
- Create: `rawreel-workspace/README.md` (replace if present)
- Modify: `rawreel-workspace/docs/superpowers/specs/2026-10-19-rawreel-design.md` (§9.4, §13.1, §16), workspace `CLAUDE.md` (outside every repository)

- [ ] **Step 1: Create `rawreel-workspace/README.md`**

````markdown
# rawreel-workspace

Shared build infrastructure for [rawreel](docs/superpowers/specs/2026-10-19-rawreel-design.md) and
the `dvv` libraries: CMake presets, bootstrap scripts, the third-party superbuild with its licence
gate, the privacy lint and the reusable CI workflow.

## Layout

All repositories live side by side in one folder; build output stays outside them:

```text
<workspace>/
  rawreel-workspace/     this repository
  dvv-base/              ...one folder per repository listed in repos.txt
  _third_party/<toolchain>/   installed dependencies (created by bootstrap)
  _build/<repo>/<preset>/     build trees (created by CMake presets)
```

## Prerequisites

- Windows: Visual Studio 2026 (C++ workload), Git, Python 3.
- Linux: Clang 19 (`clang-19`) with libstdc++ 14 (`libstdc++-14-dev`), Ninja, Git, Python 3. GCC
  is not supported.
- Both: `python -m pip install -r tools/requirements.txt` (CMake 4.3, Ninja, clang-format and
  clang-tidy 19).

## Getting started

```sh
git clone <url>/rawreel-workspace.git
cd rawreel-workspace
./bootstrap.ps1                      # Windows (PowerShell); Linux: ./bootstrap.sh
cmake --workflow --preset vs2026     # Linux: ninja-clang, ninja-clang-asan, ninja-clang-tsan
```

`bootstrap` clones missing sibling repositories, sets their git hooks and commit identity and builds
the third-party dependencies. The workspace build compiles every repository from its sibling folder
as one solution; each repository can also be built alone with the same preset names.

## Checks

- `cmake -P tests/spdx/test_spdx.cmake` and `cmake -P tests/spdx/test_third_party_gate.cmake`
- `cmake -DWORK_DIR=<scratch dir> -P tests/privacy/test_privacy_lint.cmake`
- `cmake -DREPO=<repo> -DMODE=commits -P tools/privacy-lint.cmake` (also the pre-push hook)
````

- [ ] **Step 2: Record the findings in the spec**

In §9.4, after the *Value types* bullet of **Scope**, add:

```markdown
- *Process infrastructure* (exempt): `LifetimeRegistry`, `Logger` and its sinks. They are never
  destroyed, so they outlive the leak check.
```

In §13.1, after the presets sentence, add:

```markdown
The presets themselves live once in `rawreel-workspace/cmake/DvvPresets.json`; every repository's
`CMakePresets.json` only includes that file.
```

In §16, replace the bullet `**VS 2026 runner image:** pin the exact GitHub Actions image in M0.` with:

```markdown
- **VS 2026 runner image:** resolved in M0. GitHub's `windows-2025` image ships Visual Studio 2026
  since June 2026; the CI asserts version 18.x before building, so an image change fails loudly.
```

In §16, also add a bullet:

```markdown
- **Machine paths in MSVC binaries:** Clang builds map the workspace root away with
  `-ffile-prefix-map`; MSVC still embeds absolute `__FILE__` and PDB paths. M4 packaging must strip
  them (e.g. `/PDBALTPATH:%_PDB%`, build in a neutral root) before release artifacts are published.
```

- [ ] **Step 3: Full local verification**

```powershell
Push-Location rawreel-workspace; cmake --workflow --preset vs2026; Pop-Location
wsl -d Ubuntu-24.04 -e bash -lc 'cd rawreel-workspace && cmake --workflow --preset ninja-clang 2>&1 | grep -E "tests passed|tests failed"'
foreach ($repo in 'rawreel-workspace','dvv-base') { cmake "-DREPO=$repo" -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake }
```

Expected: workspace Debug and Release `100% tests passed, 0 tests failed out of 8` on Windows and Linux; both lint runs `clean (commits)`.

- [ ] **Step 4: Commit**

```powershell
git -C rawreel-workspace add README.md docs/superpowers/specs/2026-10-19-rawreel-design.md
git -C rawreel-workspace commit -m "docs: add workspace README and record M0 findings in the spec" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 5: Update the workspace `CLAUDE.md` state** (not in any repository): M0 implemented locally; next step M1a plan; publishing waits for the user's go-ahead.

---

### Task 20: Publish and confirm CI (only after the user's explicit go-ahead)

**Files:** none.

- [ ] **Step 1: STOP and ask the user** for permission to create the public GitHub repositories `rawreel-workspace` and `dvv-base` under their account and to push `main` of both. Do not continue without an explicit yes given at that time. (Renaming the old `rawreel` repository is not part of M0.)

- [ ] **Step 2: Scan before pushing**

```powershell
foreach ($repo in 'rawreel-workspace','dvv-base') { cmake "-DREPO=$repo" -DMODE=commits "-DEXTRA_TERMS=$env:USERNAME;$env:COMPUTERNAME" -P rawreel-workspace/tools/privacy-lint.cmake; git -C $repo log --format='%an <%ae> | %cn <%ce>' | Sort-Object -Unique }
```

Expected: `clean (commits)` twice; only the GitHub handle with its `@users.noreply.github.com` address.

- [ ] **Step 3: Create the repositories and push** — workspace first, because dvv-base's CI calls its workflow

```powershell
gh repo create BarabashkaD/rawreel-workspace --public --source rawreel-workspace --remote origin --push
gh repo create BarabashkaD/dvv-base --public --source dvv-base --remote origin --push
```

The pre-push hook runs the privacy lint for each push.

- [ ] **Step 4: Watch CI until green**

```powershell
gh run watch --repo BarabashkaD/dvv-base --exit-status (gh run list --repo BarabashkaD/dvv-base --limit 1 --json databaseId --jq '.[0].databaseId')
gh run watch --repo BarabashkaD/rawreel-workspace --exit-status (gh run list --repo BarabashkaD/rawreel-workspace --limit 1 --json databaseId --jq '.[0].databaseId')
```

Expected: all jobs succeed (dvv-base: 5 build jobs + lint; workspace: 2 tool jobs). If a job fails, use superpowers:systematic-debugging, fix, commit and push again.

- [ ] **Step 5: Trigger the nightly integration once**

Run: `gh workflow run workspace.yml --repo BarabashkaD/rawreel-workspace` and watch it as in Step 4.
Expected: `integration-vs2026` and `integration-ninja-clang` succeed. M0 is complete: CI green on Windows and Linux with the base unit tests.
