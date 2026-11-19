# dvv-build 0.5.0 and dvv-base 0.5.0: the per-OS rule, the names, and dvv-base's shared helpers

Status: design, revision 3 (2026-11-18: revision 2 approved, plus the errata in §13); reviewed by an optimistic and a pessimistic
reviewer (findings S-o1..S-o12, S-p1..S-p12). §12 lists what changed.
Repositories: dvv-build (0.4.2 → 0.5.0) and dvv-base (0.4.1 → 0.5.0). These are steps 1 and 2 of
the global order of the dvv release train. Every later release of the train builds on them.
Code is cited at the audit baseline (dvv-build@3ddc842, dvv-base@d3bd517 and the dependents' shas
at the audit baseline). "Spec's call" marks a choice this spec makes without the user. §11 lists the two
spec questions with the answers applied, for the user's confirmation.

## 1. Intent

The audit found the same mechanism written several times, with small differences, across the
seven repositories (D-0003). It found per-OS code with no single written rule or check (D-0004). It
found a suffix vocabulary that was never written down (D-0007), public flags passed as bare `bool`
(D-0006), and a dvv-base callback type that differs from its siblings (D-0008). The two lowest
repositories own the fix for all of these:

- **dvv-build** owns the build rules and CI. It gets the one written per-OS rule and three tools
  that read the same suffix lists (L3-0004-1): `dvv_os_sources`, the `os-blocks` gate, and Tidy's
  file list taken from the compile database (plus Format on `*.mm`). It also gets the written
  naming vocabulary (L3-0007-1).
- **dvv-base** is the lowest library. It becomes the one owner of UTF-8 and path text (L3-0003-1),
  local time (L3-0003-2) and the UTF-16 conversion (L3-0003-7). It ships the `DvvOsSources` module
  to its dependents (L3-0004-1's install line). It gets named enum forms beside its two bool
  flags (L3-0006-5), and a `setHeader` that takes the same callable type as `IExecutor::post`
  (L3-0008-1, decision 7). It also starts the deprecations that its 1.0.0 completes
  (L3-FIX-1: F-L1-dvv-base-1, F-L1-dvv-base-3).

The dvv rule binds every item (dvv-build@3ddc842:README.md:74-83). Compatible means the same major
and at least the minimum, also for 0.x. A break comes only with a major, and only after a
deprecation. So both releases are **additive minors**. Each deprecation is a CHANGELOG
"Deprecated" entry only. Removals wait for dvv-base 1.0.0 in the R7-d8 window (confirmed by the
user, the user's confirmed decisions). The one behaviour change in a minor, an
empty `std::function` passed to `setHeader`, is listed in §11 for the user's confirmation.

The user's standing preference binds every choice (the user's confirmed decisions): the cleanest API and the better long-term solution, and the size of a change is never a
reason. Where a case left a choice open, this spec takes the cleaner option even when it touches
more code. §4.x and §5.x give each "Approaches considered" with the reason for the choice.

## 2. Goals, non-goals, anti-goals

Goals:
1. One written per-OS rule in dvv-build. A CMake helper selects per-OS files by suffix. A CI gate
   counts OS `#if` blocks against a per-repo baseline that may only shrink. Tidy sees exactly the
   files the Linux build compiles. Format covers `*.mm`.
2. One written naming vocabulary in dvv-build's README: suffix roles, a glossary, and the known
   misfits with the cases that rename them.
3. dvv-base owns UTF-8 validation, path text and UTF-16↔UTF-8 conversion (Windows), with one
   algorithm per job. A path has two text forms with two clearly named functions: **path text**
   (`pathText`, the round-trip form, the native bytes on POSIX) and **display text**
   (`pathDisplayText`, always well-formed UTF-8, U+FFFD for what has no UTF-8 form). A file whose
   name is not UTF-8 on Linux stays openable; U+FFFD appears only in display text. dvv-base's own
   copies are deleted in 0.5.0. The other repositories delete theirs when they pin 0.5.0 (§6).
4. dvv-base owns the local-time conversion: one checked function.
5. dvv-base's public signatures name their flags: an enum form beside each bool form, and the bool
   forms deprecated.
6. `SplitFileSink::setHeader` takes a `dvv::MoveOnlyFunction`. Every caller form that compiles
   today still compiles, and `nullptr` means "no header" on every toolchain. An empty
   `std::function` is a callable that throws at the first header; the header documents it and a
   test pins it (§11, question 2).
7. dvv-base's public surface loses, by deprecation, what nothing uses: `Errc::ShaderCompile` and
   `RotatingFileSink` (L3-FIX-1). `MemorySink` stays in log.h; its stale comment is corrected.

Non-goals:
- Moving existing per-OS code into suffixed files (L3-0004-4..9 in later 0.5.x patches and in the
  dependents). This spec ships only the rule, the tools and the new files.
- Tidy jobs on Windows or macOS. Tidy stays Linux-only, as today. The README states this residual
  of F-L1-dvv-build-1.
- Removing any public form. That happens in dvv-base 1.0.0 (step 9 of the train).
- The GPU codes in `Errc` (`DeviceLost`). F-L1-dvv-base-1's stage-C premise was refuted (RF-d2).
  Only the producer-less code goes.
- The system log sink's opaque member, and the per-OS moves of paths and environment (dvv-base
  0.5.x, L3-0004-4).
- A lossless path text on Windows for a name that holds an unpaired surrogate (a WTF-8 form). On
  POSIX the round trip is lossless; on Windows such a name gets U+FFFD in both forms and stays
  unopenable through text, as it is today (§5.1).

Anti-goals (each would be a failure even if it worked):
- A `[[deprecated]]` attribute anywhere. Every dvv target builds with -Werror and /WX
  (dvv-build@3ddc842:cmake/DvvCompilerOptions.cmake:170-177), so the attribute would break every
  consumer's build at a minor bump.
- A caller form that compiles against dvv-base 0.4.1 and fails against 0.5.0.
- A gate that passes silently: a missing compile database, an empty Tidy list while candidates
  exist, a malformed baseline or a stale baseline row must each fail loudly.
- A new OS `#if` block in a shared dvv-base file.
- Any change to deliberate per-OS behaviour (R0-d23).
- Display text used as a file's identity: a U+FFFD in a URI, a library name or a hash key.

## 3. Constraints

- **C++23 on the three CI toolchains**: clang-19 with libstdc++-14 (ubuntu-24.04), MSVC from
  Visual Studio 2026 (windows-2025), and AppleClang 21 with Apple libc++ (xcode-27). Sources are
  the audit's toolchain gate and its Apple libc++ feature probe. Apple libc++ lacks
  `std::move_only_function` (probe: `__cpp_lib_move_only_function absent`), so `dvv::MoveOnlyFunction`
  uses the polyfill there (dvv-base@d3bd517:include/dvv/base/move_only_function.h:138-144). The
  chrono time-zone database is out of the gate. The C++20 calendar types (`year_month_day`,
  `hh_mm_ss`) pass (the audit's chrono probe). This spec uses only
  `std::filesystem`, `<optional>`, `<string_view>`, `<compare>`, `<chrono>` calendar types and
  `localtime_s`/`localtime_r`, all of which are already in use.
- **The dvv rule** (§1): additive in 0.5.0; deprecation through the CHANGELOG only; removal only at
  the major.
- **-Werror and /WX** on every dvv target (dvv-build@3ddc842:cmake/DvvCompilerOptions.cmake:168-177).
  A new overload must not create an ambiguous call at any existing site (§5.5 checks this). Every
  code sketch in this spec compiles on all three toolchains as written: no `if constexpr` outside
  a template, and every hex escape in a literal ends at a non-hex character or at a literal split.
- **CMake 4.2 minimum** (dvv-base@d3bd517:CMakeLists.txt:1). The dvv-build tools are CMake scripts
  run with `cmake -P`, as are the existing ones (dvv-build@3ddc842:CMakeLists.txt:29-75).
- **Lockstep**: dvv-base 0.5.0 includes and installs `DvvOsSources.cmake`, so it requires
  dvv-build 0.5.0. Every repository that raises dvv-base to 0.5.0 also raises dvv-build to 0.5.0
  (fetch-deps, dvv-build@3ddc842:tools/fetch-deps.cmake:283-301).
- **Repo policy**: one squashed commit per pull request, and the user merges. Every dependency pin
  is on a tag.

## 4. dvv-build 0.5.0

### 4.1 `cmake/DvvOsSources.cmake`: the suffix lists and `dvv_os_sources`

Public API (CMake):
```cmake
# The per-OS file rule: per-OS code lives in <stem>_<suffix>.{cpp,mm,h}. The suffix names the OS,
# the window system or the toolkit backend. A platform may build several suffixes ("layered" files).
set(DVV_OS_SUFFIXES_WIN32 win32 msw)
set(DVV_OS_SUFFIXES_APPLE posix macos metal cocoa)
set(DVV_OS_SUFFIXES_OTHER posix linux xlib gtk)
set(DVV_OS_SUFFIX_FALLBACK stub)

dvv_os_all_suffixes(<out-var>)            # union of the three lists plus "stub", de-duplicated
dvv_os_suffixes_for(<WIN32|APPLE|OTHER> <out-var>)   # one platform's list
dvv_os_suffixes(<out-var>)                # this build's list, from CMAKE_SYSTEM_NAME
dvv_os_sources(<target> [OPTIONAL] <stem>...)
```

Behaviour:
- Including the module defines variables and functions only. It runs no target command, so both a
  project and `cmake -P` (the gate) can include it.
- `dvv_os_suffixes` maps `CMAKE_SYSTEM_NAME`: `Windows` → WIN32, `Darwin` → APPLE, anything else
  → OTHER. It never uses the host system. In script mode (`CMAKE_SYSTEM_NAME` unset) it fails with
  FATAL_ERROR "dvv_os_suffixes needs a project (CMAKE_SYSTEM_NAME is not set)".
- `dvv_os_sources(<target> [OPTIONAL] <stem>...)`. Each stem is a path without an extension,
  relative to `CMAKE_CURRENT_SOURCE_DIR`, for example `src/local_time`. For each stem, in order:
  1. Collect every existing `<stem>_<suffix>.cpp` and `<stem>_<suffix>.mm` for each suffix of this
     build's list, in list order. If one suffix has both a `.cpp` and a `.mm`, fail with
     FATAL_ERROR "`<stem>_<suffix>`: both .cpp and .mm".
  2. If any file was found, add all of them as PRIVATE sources ("layered" files: on macOS
     `file_reader_posix.cpp` and `file_reader_macos.cpp` are both added; they must define
     disjoint symbols).
  3. If none was found, add `<stem>_stub.cpp` if it exists.
  4. If none of those exists: without OPTIONAL, fail with FATAL_ERROR "`<stem>`: no file for
     <platform> and no `<stem>_stub.cpp`". With OPTIONAL, add nothing.
  5. Typo guard, OPTIONAL or not: if no `<stem>_<s>.{cpp,mm}` exists for any `s` of
     `dvv_os_all_suffixes`, fail with "`<stem>`: matches no per-OS file on any platform".
- Each added `.mm` gets `COMPILE_OPTIONS -fobjc-arc` (appended through
  `set_property(SOURCE … TARGET_DIRECTORY <target> APPEND …)`). Adding a `.mm` while
  `CMAKE_OBJCXX_COMPILER_LOADED` is false fails with "`<file>`: enable OBJCXX before
  dvv_os_sources".
- Per-OS headers (`*_win32.h`) are not added. They need no build step.
- Sources are always PRIVATE (spec's call, see below).

Approaches considered:
- Visibility argument `<PRIVATE|PUBLIC|INTERFACE>` (the case's shape) or always PRIVATE (chosen).
  A PUBLIC or INTERFACE source would compile into every consumer, which no dvv target wants. A
  keyword that has one valid value is noise.
- `-fobjc-arc` unconditional (chosen), or a `NO_ARC` keyword. All three `.mm` files want ARC today
  (dvv-platform@3478d4f:CMakeLists.txt:91-92, rawreel@5a48754:CMakeLists.txt:176-177). A keyword
  can be added in a later minor without breaking anything, once a non-ARC file exists.
- Several suffixes for one stem on one platform: allowed (chosen). Layering is the first planned
  use (L3-0004-4: posix + macos).
- `dvv_os_all_suffixes`, `dvv_os_suffixes_for` and `dvv_os_suffixes` are public so that the gate,
  the tests and a dependent's own CMake can name a platform's list. They are pure functions.

### 4.2 `tools/os-blocks.cmake`: the gate

Command: `cmake -DREPO=<dir> [-DMODE=check|write] [-DBASELINE=<file>] -P tools/os-blocks.cmake`.
`MODE` defaults to `check`. `BASELINE` defaults to `<REPO>/os-blocks.txt`. The tool includes
`../cmake/DvvOsSources.cmake` for `dvv_os_all_suffixes`.

Definitions:
- **Files**: `git -C <REPO> ls-files -- '*.h' '*.cpp' '*.mm'` (tracked files only, so submodule
  contents and build trees are out). Paths under `tests/` are dropped, and so are files whose stem
  ends in `_<s>` for any `s` of `dvv_os_all_suffixes` (per-OS files may hold anything). If
  `git ls-files` fails, the tool fails.
- **OS group**: an `#if`/`#ifdef`/`#ifndef` group whose condition, or the condition of any of its
  `#elif`/`#elifdef`/`#elifndef` lines, names `_WIN32`, `__APPLE__`, `__linux__`, `TARGET_OS_*`,
  `__WXMSW__`, `__WXOSX__*` or `__WXGTK__`. This is the census pattern. Directives may have
  spaces after `#`, and a backslash continues a condition onto the next line. `_MSC_VER` and
  `__clang__` are compiler conditionals, not OS ones (Y1 P2).
- **Counted block**: an outermost OS group, from its `#if` line to its `#endif` line inclusive. An
  OS group nested in a counted block is part of it. An OS group nested only in non-OS groups is
  outermost. A block counts when it spans **more than 5 lines** and holds at least one code line.
- **Free lines** (spec's call, S-o9): a line is not a code line when it is blank, a comment (lines
  inside `/* … */` included), a conditional directive of the group itself or of a nested group
  (`#if…`, `#elif…`, `#else`, `#endif`), `#include`, `#pragma`, `#error` or `#warning`. Every other
  line is a code line, including `#define` and `#undef`: a macro table that differs by OS is
  per-OS code. So an include-and-pragma block is free, and a `#define` block is not.
- **Baseline**: a tab-separated text file. Each line is `<count>\t<path>`: one row per file that
  has counted blocks, with the number of its counted blocks, sorted by path, under one comment
  header line ("# os-blocks baseline, written by dvv-build tools/os-blocks.cmake; it may only
  shrink"). It sits in the repository root next to `dvv-deps.txt`. A missing file is an empty
  baseline.

`check` reads the baseline first. A line other than the header that is not `<count>\t<path>`, a
count that is not a positive decimal integer (0 included), and a path that appears twice each fail
at once with "bad baseline line N: <reason>". Then it collects every other failure and reports
them together, then fails with FATAL_ERROR if there was any:
- a file's count is above its row, or the file has no row and its count is above 0:
  "`<path>`: N OS blocks over 5 lines (baseline M): move the code into a per-OS file";
- a file's count is below its row: "`<path>`: N blocks, baseline M: shrink the baseline (MODE=write)";
- a row names a file that is gone, or that is now per-OS or under tests/: "stale row `<path>`".

On success it prints "os-blocks: OK (<n> files, <k> counted blocks, baseline <b> rows)".

`write` rewrites the baseline from the tree. It writes a row only for a file whose count is above
0. If there are zero rows it deletes the file, which is D-0004's end state: the gate then guards
zero blocks with no data file.

CI (dvv-ci.yml lint job, which does not run for dvv-build itself,
dvv-build@3ddc842:.github/workflows/dvv-ci.yml:581-582): a new step "OS blocks" after "Privacy
lint" and before "Format", with `if: ${{ !cancelled() }}`, running
`cmake "-DREPO=${{ inputs.repo }}" -P "$DVV_BUILD/tools/os-blocks.cmake"`.

Approaches considered:
- Baseline in each repository (chosen) or a central list in dvv-build. A per-repo file shrinks with
  that repository's own move commits and needs no dvv-build release to shrink. A central list
  would couple every move to a dvv-build tag.
- Count tests (rejected, spec's call): test-scenario guards are a choice of which tests run, as in
  the census. The `tools/` folder is counted.
- Count per file (chosen) or record each block's line span. Line spans break on every unrelated
  edit above a block. A per-file count is stable, and the ratchet ("shrink the baseline") forces
  the count down when a block moves out.
- Free lines: every directive (S-o9's first option) or only the non-code directives (chosen). A
  `#define` under `_WIN32` changes what the code means per OS, which is what the rule moves into
  per-OS files.

### 4.3 Tidy from the compile database; Format on `*.mm`

`tools/tidy-files.cmake -DDB=<compile_commands.json> -DREPO=<dir> -DOUT=<file>` writes, one per
line, the de-duplicated and sorted repo-relative paths that meet all three conditions:
1. the path is in the compile database;
2. the file is tracked;
3. the path matches today's globs `src/**/*.cpp`, `tests/*.cpp` or `tools/*.cpp`, through
   `git ls-files ':(glob)src/**/*.cpp' ':(glob)tests/*.cpp' ':(glob)tools/*.cpp'`. This keeps
   tests/compile_fail, tests/package and tools/<sub>/ out, as today.

Reading the database (spec's call, for speed): CMake's `string(JSON)` parses the whole document
on every call, and a loop over the entries would parse it once per entry. So the tool parses the
whole database once, for `string(JSON n LENGTH)`. Then it takes every line that holds a `"file"`
member with `file(STRINGS <db> lines ENCODING UTF-8 REGEX "^[ \t]*\"file\"[ \t]*:")` (the layout
of the database CMake writes; `ENCODING UTF-8` keeps a non-ASCII path on one line). It strips one
trailing comma from each line, wraps it as `{<line>}`, and reads it with `string(JSON GET)`. That
gives correct unescaping at a small cost per entry. If the number of `"file"` lines differs from
`n`, the tool fails with "unexpected compile database layout" (a one-line, minified database
fails this way). A relative `file` is resolved against its entry's `directory`, which is read the
same way. The tool also fails when the database is missing, and when the candidates of step 3 are
not empty but the result is empty (the paths did not match).

CI Tidy step (working directory `${{ inputs.repo }}`, as today): first
```bash
cmake "-DDB=$GITHUB_WORKSPACE/$DVV_ROOT/_build/$DVV_REPO/ninja-clang/compile_commands.json" \
      -DREPO=. "-DOUT=$RUNNER_TEMP/tidy-files.txt" -P "$DVV_BUILD/tools/tidy-files.cmake"
```
then `tr '\n' '\0' < "$RUNNER_TEMP/tidy-files.txt" | xargs -0 -r -P "$(nproc)" -n 2 clang-tidy -p
"$GITHUB_WORKSPACE/$DVV_ROOT/_build/$DVV_REPO/ninja-clang" --quiet`, in bash with pipefail. The
`_win32`/`_msw` excludes and their comment are removed
(dvv-build@3ddc842:.github/workflows/dvv-ci.yml:639-651). `CMAKE_EXPORT_COMPILE_COMMANDS` on
`ninja-clang` (dvv-build@3ddc842:cmake/DvvPresets.json:39) gets a comment saying that Tidy depends
on it.

Format step: `git ls-files -z '*.h' '*.cpp' '*.mm'`
(dvv-build@3ddc842:.github/workflows/dvv-ci.yml:598-601). The repositories' `.clang-format` files
set no `Language:` section, so the one style applies to Objective-C++ too.

### 4.4 README: "Names" and "Per-OS code" (L3-0007-1, L3-0004-1)

- The tools list (dvv-build@3ddc842:README.md:1-8) adds `os-blocks`, `tidy-files` and `DvvOsSources`.
- New section "## Names" after "## Versions": the suffix-role table, the glossary and the misfit
  list, verbatim from L3-0007-1's Proposed shape. The acceptance check is L3-0007-1's (every
  suffix of the R7 census has a row; it names L3-0002-3, L3-0004-3, D-0001, L3-0007-2..6).
- New section "## Per-OS code" with the rule text: per-OS code lives in suffixed files that
  `dvv_os_sources` selects; a shared file may hold an OS `#if` of at most 5 lines, with a comment
  and the same types on every branch; a block of only includes, pragmas and `#error` lines is
  free at any length, a `#define` is code; one selector vocabulary per layer (CMake by platform
  `WIN32` / `APPLE` / other, gui C++ by wx toolkit macro, all other C++ by OS macro). The gate
  checks only the length; the other two conditions are review items. Windows-only and macOS-only
  files are not tidied (Tidy runs on Linux), and Tidy reads `ninja-clang`'s compile database, so a
  repository's preset keeps `CMAKE_EXPORT_COMPILE_COMMANDS` on.
- Spec's call: the vocabulary lives in dvv-build's README only. Other repositories do not get a
  link. rawreel's design spec points to the README when rawreel adopts the gate (§6).
- Spec's call: everything ships in 0.5.0 rather than a docs-only 0.4.3. That is one tag and one
  review, and no repository re-pins for the README.

### 4.5 dvv-build tests

All are CMake scripts registered in dvv-build@3ddc842:CMakeLists.txt:29-75 as `dvv_build.<name>`.
The self workflow runs them on all three OS jobs. The script-only ones (os_blocks, tidy_files)
are also added to the `tools` job of dvv-build@3ddc842:.github/workflows/ci.yml:21-28.
- `dvv_build.os_suffixes` (`tests/cmake/test_os_suffixes.cmake`, script mode): the three lists and
  the union; `dvv_os_suffixes_for` for each platform; `dvv_os_suffixes` in script mode fails with
  the stated message.
- `dvv_build.os_sources` (`tests/cmake/test_os_sources.cmake`, which configures fixture projects
  with `-DGENERATOR`, as test_dvv_deps.cmake does):
  - a stem with `_win32`, `_posix`, `_macos` and `_linux` files adds exactly this platform's
    files: on macOS both posix and macos (layered), on Linux posix and linux, on Windows win32;
  - a stem with only `_stub` adds the stub;
  - a stem with no file fails configure with the "no file" message;
  - OPTIONAL with only `_win32` adds nothing outside Windows;
  - a misspelt stem fails even with OPTIONAL;
  - `x_cocoa.cpp` together with `x_cocoa.mm` fails;
  - on macOS, a `.mm` has `-fobjc-arc` in its COMPILE_OPTIONS, and a `.mm` without OBJCXX fails.
- `dvv_build.os_blocks` (`tests/tools/test_os_blocks.cmake`, fixture git repositories under
  WORK_DIR):
  - a 6-line `_WIN32` block with code in `a.cpp` fails against an empty baseline;
  - the same block in `a_win32.cpp` passes; a row for a file renamed to `a_win32.cpp` is stale;
  - a 6-line block of only `#include` and `#pragma` lines passes, and a 5-line block with code
    passes;
  - a 6-line block whose only lines besides its directives are inside `/* … */` passes;
  - a 6-line `_WIN32` block of `#define` lines counts;
  - a nested OS group counts once;
  - `#  if defined(_WIN32)` (spaces after `#`) counts, and so does `#ifdef __APPLE__`;
  - an `#if defined(__clang__)` group with an `#elif defined(_WIN32)` counts;
  - an `#if FOO` group that holds a 6-line `#if defined(_WIN32)` block with code counts once;
  - a continued `#if defined(_WIN32) \` condition counts;
  - a counted block in `x.h` and one in `x.mm` each count;
  - a block in `tests/x.cpp` is ignored, and a row for a file moved under `tests/` is stale;
  - a count below its row fails with the shrink message, and a row for a deleted file is stale;
  - a baseline with a malformed line, a duplicate path or a count of 0 fails with "bad baseline
    line N";
  - `write` then `check` passes, and `write` writes no 0 row;
  - `write` on a clean tree deletes the file, and with no baseline file and one block, `check`
    fails.
- `dvv_build.tidy_files` (`tests/tools/test_tidy_files.cmake`): a fixture database with a
  duplicate entry, a relative `file`, an untracked generated file, a `tests/compile_fail/x.cpp`
  and an out-of-repo dependency source yields only the tracked glob matches, once each. A tracked
  `src/ü.cpp` in the database is kept with its name intact. A missing database fails. A database
  that names none of the candidates fails. A one-line (minified) database fails with "unexpected
  compile database layout".

### 4.6 dvv-build CHANGELOG 0.5.0

```markdown
## [0.5.0] - <date>

The per-OS file rule and its checks, and the naming vocabulary.

### Added
- `cmake/DvvOsSources.cmake`: one suffix list per platform (`DVV_OS_SUFFIXES_WIN32`, `_APPLE`,
  `_OTHER`, `DVV_OS_SUFFIX_FALLBACK`), the queries `dvv_os_all_suffixes`, `dvv_os_suffixes_for`
  and `dvv_os_suffixes`, and `dvv_os_sources(<target> [OPTIONAL] <stem>...)`, which adds this
  platform's `<stem>_<suffix>.{cpp,mm}` (several when layered), else `<stem>_stub.cpp`; `.mm`
  files get `-fobjc-arc`. dvv-base installs it next to `DvvCompilerOptions.cmake`.
- `tools/os-blocks.cmake` and the lint step "OS blocks": an OS `#if` block over 5 lines with code
  in a file without a per-OS suffix fails, against `os-blocks.txt` in the repository (one row per
  file with its block count), which may only shrink (a missing file is an empty baseline).
- `tools/tidy-files.cmake`: Tidy's file list is the compile database ∩ tracked files ∩ the source
  globs, so no per-OS file of another platform reaches clang-tidy.
- README "Names" (suffix roles, glossary, known misfits) and "Per-OS code" (the rule).

### Changed
- Format also checks `*.mm`.
- Tidy no longer excludes `*_win32.cpp` / `*_msw.cpp` by name. It reads `ninja-clang`'s compile
  database; a repository's preset must keep `CMAKE_EXPORT_COMPILE_COMMANDS` on.

Adopting: a repository that raises its dvv-build line to 0.5.0 commits `os-blocks.txt` (written by
`-DMODE=write`) and reformats its `.mm` files in the same commit.
```

## 5. dvv-base 0.5.0

### 5.1 `dvv/base/utf8.h`: path text and display text (L3-0003-1)

A path has two text forms, and every caller needs exactly one of them (decision on S-p1, §11):
- **Path text** is the round-trip form. It names the file again: in a URI, a library name for
  `dlopen`, a hash key, a stored setting, a command line, FFmpeg's open call. On POSIX it is the
  path's own bytes, which need not be UTF-8, so a Linux file whose name is not UTF-8 stays
  openable through every such route. It is never shown.
- **Display text** is always well-formed UTF-8, for messages, logs, UI text (wx needs valid UTF-8:
  `wxString::FromUTF8` yields an empty string for ill-formed input, for example at
  rawreel@5a48754:src/gui/main_frame.cpp:145) and name grammars. Anything without a UTF-8 form
  becomes U+FFFD. It is never turned back into a path.

```cpp
// include/dvv/base/utf8.h
#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace dvv {
// Well-formed UTF-8 (RFC 3629, Unicode Table 3-7): no overlong forms, no surrogates, nothing
// above U+10FFFF, no truncated sequence.
[[nodiscard]] bool isValidUtf8(std::string_view text) noexcept;

// Path text, the round-trip form: pathFromText(pathText(p)) == p. On POSIX the path's own bytes,
// unchanged; they need not be UTF-8. On Windows the UTF-8 form of the UTF-16 name; an unpaired
// surrogate becomes U+FFFD, the one lossy case. Use it where the text names the file again (a
// URI, a library name, a hash key, a stored setting, an argument for the OS or FFmpeg). Never
// show it: use pathDisplayText. Never throws std::system_error (it may throw std::bad_alloc).
[[nodiscard]] std::string pathText(const std::filesystem::path& path);

// The path that `text` names: the inverse of pathText, and the path of any UTF-8 text. On POSIX
// the bytes become the path unchanged. On Windows the UTF-8 becomes UTF-16, never through the
// ANSI code page, and each maximal ill-formed subpart becomes U+FFFD. Never throws
// std::system_error (it may throw std::bad_alloc).
[[nodiscard]] std::filesystem::path pathFromText(std::string_view text);

// Display text: well-formed UTF-8 on every OS, for messages, logs, UI text and name grammars.
// Each maximal ill-formed subpart (POSIX) or unpaired surrogate (Windows) becomes U+FFFD. Lossy:
// never turn it back into a path. Never throws std::system_error (it may throw std::bad_alloc).
[[nodiscard]] std::string pathDisplayText(const std::filesystem::path& path);
}  // namespace dvv
```

A path-text string that must be shown (for example a URI's decoded path) is shown as
`pathDisplayText(pathFromText(text))`. A path-text value written into a format that holds only
UTF-8 (JSON) is checked with `isValidUtf8` by its writer, which reports the name as not storable
instead of storing a different path; today's nlohmann writer already fails such a value with an
exception that rawreel's config writer reports (rawreel@5a48754:src/core/config_writer.cpp:116).

Implementation (`src/utf8.cpp`, plus the private `src/utf8_detail.h`). The platform's form is
picked by overload resolution on `path::string_type` (`std::string` on POSIX, `std::wstring` on
Windows). Every overload is ordinary, non-template code that compiles on every toolchain; there is
no preprocessor `#if` and no `if constexpr` (S-p2):
```cpp
// src/utf8_detail.h (private)
namespace dvv::detail {
// Table 3-7: the length of the well-formed sequence that starts at text[i], or 0 if none does.
[[nodiscard]] std::size_t wellFormedLength(std::string_view text, std::size_t i) noexcept;
// Where wellFormedLength is 0: the length (1 to 3) of the maximal subpart that starts at text[i]
// (Unicode 16.0 §3.9, "U+FFFD Substitution of Maximal Subparts").
[[nodiscard]] std::size_t illFormedLength(std::string_view text, std::size_t i) noexcept;
// Bytes to well-formed UTF-8: each maximal ill-formed subpart becomes U+FFFD.
[[nodiscard]] std::string replaceIllFormed(std::string_view bytes);

enum class BadUnits : std::uint8_t { Replace, Reject };
// UTF-16 to UTF-8. Replace: each unpaired surrogate becomes U+FFFD (never nullopt).
// Reject: nullopt on the first unpaired surrogate. Defined in this header for char16_t and wchar_t.
template <class Unit>
[[nodiscard]] std::optional<std::string> utf8FromUtf16(std::basic_string_view<Unit> text,
                                                       BadUnits bad);
// UTF-8 to UTF-16: each maximal ill-formed subpart becomes U+FFFD.
template <class Unit>
[[nodiscard]] std::basic_string<Unit> utf16FromUtf8(std::string_view text);

// The native forms. Overload resolution on path::string_type picks the platform's pair.
[[nodiscard]] inline std::string textOfNative(const std::string& native) { return native; }
[[nodiscard]] inline std::string textOfNative(const std::wstring& native) {
    return *utf8FromUtf16<wchar_t>(native, BadUnits::Replace);
}
[[nodiscard]] inline std::string displayOfNative(const std::string& native) {
    return replaceIllFormed(native);
}
[[nodiscard]] inline std::string displayOfNative(const std::wstring& native) {
    return *utf8FromUtf16<wchar_t>(native, BadUnits::Replace);
}
inline void nativeOfText(std::string_view text, std::string& native) { native.assign(text); }
inline void nativeOfText(std::string_view text, std::wstring& native) {
    native = utf16FromUtf8<wchar_t>(text);
}
}  // namespace dvv::detail

// src/utf8.cpp
std::string pathText(const std::filesystem::path& path) {
    return detail::textOfNative(path.native());
}
std::string pathDisplayText(const std::filesystem::path& path) {
    return detail::displayOfNative(path.native());
}
std::filesystem::path pathFromText(std::string_view text) {
    std::filesystem::path::string_type native;
    detail::nativeOfText(text, native);
    return std::filesystem::path(std::move(native));
}
```
The `wchar_t` overloads are compiled but unused on POSIX, where `wchar_t` is 32 bits; the
templates write each unit with `static_cast<Unit>`, so they compile for any unit type. They are
exercised on every toolchain through `char16_t` in the tests.

- One validator, `detail::wellFormedLength`, moved from dvv-media's Table 3-7 version
  (dvv-media@134f98b:src/types.cpp:54-99). `isValidUtf8` is "every sequence has a non-zero
  length". dvv-base's own validator (dvv-base@d3bd517:src/utf8_path.h:15-58) is deleted, so two
  validators become one.
- One replacement rule, maximal subparts (spec's call, S-o12): where `wellFormedLength` is 0,
  `illFormedLength` bytes become one U+FFFD. A lead byte that can start no sequence (`80`–`C1`,
  `F5`–`FF`) is a subpart of 1. This is the Unicode-recommended practice that Windows'
  `MultiByteToWideChar`, ICU and the WHATWG decoder follow, so `wideFromUtf8` agrees with the
  system conversion it replaces (§5.2).
- One UTF-16 decoder and one encoder, the two templates above, portable C++ with no OS API.

Edge cases: empty text gives an empty path, and back. Embedded NUL bytes pass through unchanged
(a path may not hold one; that is the caller's concern, as today). Display of the bytes
`"a\xFF" "b"` is `"a\xEF\xBF\xBD" "b"`. A truncated `"\xE6\x97"` gives one U+FFFD (it was two in
dvv-media's per-byte rule). `"\xC0\xAF"` gives two, and `"\xED\xA0\x80"` three (no valid sequence
starts `ED A0`).

Files: `include/dvv/base/utf8.h` and `src/utf8.cpp` are new; `src/utf8_detail.h` is new
(private); `src/utf8_path.h` is deleted. `checkApp` keeps its `isValidUtf8` guard and its error;
its comment changes from "would throw on Windows" to "an app name must be valid UTF-8 on every
OS". The split sink's `try { utf8(...) } catch (const std::system_error&)` around the directory
scan (dvv-base@d3bd517:src/split_file_sink.cpp:163-168) is removed: `pathDisplayText` never
throws `std::system_error`, and a U+FFFD name never matches the ASCII name grammar, so such a
file is skipped as before.

#### 5.1.1 Caller map: every call in the snapshots

The rule: a call whose result is shown, logged or parsed by a name grammar takes
`pathDisplayText`; a call whose result names the file again takes `pathText`; every conversion of
text to a path takes `pathFromText`. Lines are the call lines at the baseline; a file with one ref
lists its other lines by number.

dvv-base (migrates in 0.5.0):

| file | `pathDisplayText` | `pathText` | `pathFromText` |
|---|---|---|---|
| dvv-base@d3bd517:src/split_file_sink.cpp:150 | 150, 165 (the name grammar), 196, 201, 277, 279, 287, 338 | – | 122 (a grammar-built ASCII name) |
| dvv-base@d3bd517:src/paths.cpp:39 | – | – | 39 (app name part), 63 (an environment value) |
| dvv-base@d3bd517:tests/test_paths.cpp:66 | – | 66, 71, 139, 140, 262, 263, 264, 274, 323 (comparisons and values set back into the environment) | – |
| dvv-base@d3bd517:tests/test_split_file_sink.cpp:93 | – | 93 (exact name comparisons) | – |

`environment.cpp` uses only `isValidUtf8` (dvv-base@d3bd517:src/environment.cpp:18-21).

dvv-media (0.2.0, §6): its own calls move to the dvv-base names, and its public `pathFromUtf8` and
`utf8FromPath` stay as deprecated inline forwarders.

| file | `pathDisplayText` | `pathText` | `pathFromText` |
|---|---|---|---|
| dvv-media@134f98b:src/clip_json.cpp:279 | – | – | 279 |
| dvv-media@134f98b:src/source_factory.cpp:50 | – | – | 50, 62 |
| dvv-media@134f98b:src/sources/y4m.cpp:264 | – | – | 264 |
| dvv-media@134f98b:src/sources/raw_file.cpp:331 | – | – | 331 |
| dvv-media@134f98b:include/dvv/media/types.h:96 | – | `utf8FromPath` (:99) forwards here | `pathFromUtf8` (:96) forwards here |

Its tests (test_raw_source.cpp, test_y4m_source.cpp, test_clip_json.cpp, test_source_factory.cpp)
build fixture paths and URIs: `pathText` and `pathFromText`. test_types.cpp tests the forwarders
(§6).

dvv-platform (0.3.0): `displayPath` (dvv-platform@3478d4f:src/file_reader.cpp:118-126) is deleted;
its calls at dvv-platform@3478d4f:src/file_reader.cpp:61 and lines 141, 146, 216, 225, 239 take
`pathDisplayText`. The `widen` copy (dvv-platform@3478d4f:src/dynamic_library_win32.cpp:18) goes:
`DynamicLibrary::open` checks `isValidUtf8` (keeping "the name is not valid UTF-8",
dvv-platform@3478d4f:src/dynamic_library_win32.cpp:76) and calls `wideFromUtf8`. Its tests
(test_file_reader.cpp) compare messages: `pathDisplayText`.

dvv-gpu (0.3.0): the `utf8` copy (dvv-gpu@ce4c7d3:src/pipeline_cache.cpp:51-56) is deleted; its
calls at dvv-gpu@ce4c7d3:src/pipeline_cache.cpp:125 and lines 139, 232, 241, 253 are messages:
`pathDisplayText`. The test copy (dvv-gpu@ce4c7d3:tests/gpu_test_pipeline.cpp:59) goes; its
callers at lines 267 and 286 match log text: `pathDisplayText`.

dvv-components (0.3.0):

| file | `pathDisplayText` | `pathText` | `pathFromText` |
|---|---|---|---|
| dvv-components@db7323d:src/ffmpeg/loader.cpp:249 | 249, 266 | 273 (`DynamicLibrary::open`, which ends in `dlopen`) | – |
| dvv-components@db7323d:src/ffmpeg/checks.cpp:79 | 79 | – | 87 (`DVV_FFMPEG_DIR`) |
| dvv-components@db7323d:src/sources/container_supplier.cpp:636 | – | 636 (a test URI that `avformat_open_input` opens, :132) | – |

Its tests (ffmpeg_main.cpp, ffmpeg_support.h, ffmpeg/ffmpeg_harness.cpp,
ffmpeg/test_ffmpeg_coverage.cpp, ffmpeg/test_ffmpeg_provider.cpp, ffmpeg/test_lavfi_provider.cpp,
oracle/oracle_harness.cpp, test_ffmpeg_checks.cpp) follow the rule: fixture paths and URIs take
`pathText`, expected message text takes `pathDisplayText`.

rawreel (first re-pin release):

| file | `pathDisplayText` | `pathText` | `pathFromText` |
|---|---|---|---|
| rawreel@5a48754:src/core/drop.cpp:9 | – | 9 (the `file:` URI) | – |
| rawreel@5a48754:src/core/settings_claim.cpp:56 | – | 56 (the mutex name's hash key) | – |
| rawreel@5a48754:src/core/vulkan_driver.cpp:35 | – | 35 (`VK_DRIVER_FILES`) | – |
| rawreel@5a48754:src/core/config_writer.cpp:184 | 184 | 314 (the temporary file's name) | 313 |
| rawreel@5a48754:src/core/startup.cpp:42 | 56, 69, 74 (shown lines) | – | 42, 64 |
| rawreel@5a48754:src/core/log_file_switch.cpp:61 | 61, 66 | – | – |
| rawreel@5a48754:src/core/recent.cpp:135 | – | 135, 152 (recent entries, parsed back at :238) | 152, 238 |
| rawreel@5a48754:src/core/config.cpp:40 | 40, 243, 273 | – | 82, 286 |
| rawreel@5a48754:src/core/open_text.cpp:49 | – | – | 49 |
| rawreel@5a48754:src/cli/commands.cpp:291 | – | – | 291 |
| rawreel@5a48754:src/gui/drop_target.cpp:19 | – | – | 19 |
| rawreel@5a48754:src/gui/main_frame.cpp:145 | 145 (wx), 205 (the name grammar), 1503, 1789, 1802 | – | 1120, 1788 |
| rawreel@5a48754:src/gui/native_controls_cocoa.mm:194 | – | 194 (Finder reveal) | – |
| rawreel@5a48754:src/gui/native_controls_gtk.cpp:124 | – | 124 (the folder to open) | – |
| rawreel@5a48754:src/gui/native_controls_stub.cpp:54 | – | 54 (the folder to open) | – |
| rawreel@5a48754:src/gui/settings.cpp:78 | 78 (wx), 374 | 399 (the stored `log.file.folder`) | 212 |
| rawreel@5a48754:src/gui/app.cpp:111 | 111, 124, 131, 373, 444, 469, 477, 488, 509, 533, 556, 598 (the header text), 607, 616, 672, 680, 688, 689, 713, 732 | 364 (the `--config` argument, read back at :355), 492 (the stored `log.file.folder`) | 323, 355 |
| rawreel@5a48754:src/gui/selftest.cpp:2216 | every other call (messages, `seen` text, wx dialog text, name grammar at 2699) | 6266, 6325, 6355 (text typed into the open field), 8177, 8180, 8196, 8373, 8384 (`loadJson` opens the file) | 2255, 3372, 6164, 8570, 8577, 8586, 8626 |

The two wx calls at native_controls_gtk.cpp:124 and native_controls_stub.cpp:54 hand the text to
`wxString::FromUTF8`; for a non-UTF-8 folder that yields an empty string and the launch fails, as
today, instead of opening a different folder. rawreel's tests (test_config_writer.cpp,
test_config.cpp, test_drop.cpp, test_exe_icons.cpp, test_format_choice.cpp,
test_log_file_switch.cpp, test_recent.cpp, test_startup.cpp) follow the rule. The rawreel spec
re-checks each selftest line against the rule.

Approaches considered:
- A path with no UTF-8 form (S-p1; the user's direction, §11 question 1):
  - (a) dvv-media's fixed placeholder "(path not representable as UTF-8)" for the whole path
    (dvv-media@134f98b:src/types.cpp:164-171);
  - (b) `std::optional<std::string>` from one function;
  - (c) one function with U+FFFD per unit on every OS (revision 1);
  - (d) two functions, the round-trip path text and the display text (chosen).

  (c) broke callers that use the result as an identity: on POSIX `path.u8string()` passes
  ill-formed bytes through today (dvv-media@134f98b:src/types.cpp:164-171), so a dropped Linux file
  whose name is not UTF-8 reaches FFmpeg intact through percent-encoding
  (rawreel@5a48754:src/core/drop.cpp:9, dvv-base@d3bd517:src/uri.cpp:221-225,
  dvv-components@db7323d:src/sources/container_supplier.cpp:132), and a library folder reaches
  `dlopen` (dvv-components@db7323d:src/ffmpeg/loader.cpp:273). A U+FFFD there opens nothing, and
  two bad names hash to one key. (a) and (b) give display callers a value they cannot show. (d)
  names the two jobs, and it also fixes today's other half: dvv-media's `pathFromUtf8` replaces
  bad bytes on POSIX (dvv-media@134f98b:src/types.cpp:144-162), so a raw or y4m file with such a
  name cannot be opened from its URI today (dvv-media@134f98b:src/source_factory.cpp:50);
  `pathFromText` keeps the bytes.
- Names: `pathText` / `pathFromText` / `pathDisplayText` (chosen), or keeping `utf8FromPath` for
  one of them. The round-trip form is not UTF-8 on POSIX, so a `utf8…` name would state a false
  contract; and dvv-media's names stay as forwarders, so reusing them in `dvv::` would make two
  same-named functions with different contracts.
- A WTF-8 path text on Windows (lossless for unpaired surrogates) or U+FFFD (chosen). FFmpeg, wx
  and the Win32 UTF-8 entry points reject WTF-8, so the form would open nothing more, and it
  would add a third encoding to the library.
- Replacement per byte (dvv-media today) or per maximal subpart (chosen, spec's call): it is the
  Unicode recommendation, it matches Windows' own conversion, and dvv-media's expectations change
  in 0.2.0 anyway.
- Namespace `dvv::` (chosen), as `dvv::percentEncode` is, or `dvv::text::`. One header with four
  functions does not justify a sub-namespace.
- `wellFormedLength` (the Table 3-7 ranges) or dvv-base's decode-then-check validator: the
  table-driven one (chosen). It is the one already behind dvv-media's never-throw contract, and it
  gives the maximal-subpart rule its definition.

Tests (`tests/test_utf8.cpp`, new, all toolchains unless named; every hex escape ends at a literal
split):
- "isValidUtf8 accepts well-formed text": "", ASCII, 2-, 3- and 4-byte sequences (`"aä日🎞"`).
- "isValidUtf8 rejects ill-formed text": `"\xC0\xAF"`, `"\xE0\x80\x80"`, `"\xED\xA0\x80"`,
  `"\xF4\x90\x80\x80"`, `"\x80"`, `"\xE6\x97"`, `"\xF5\x80\x80\x80"`.
- "display text replaces each maximal subpart with one U+FFFD" (through
  `detail::replaceIllFormed`): `"a\xFF" "b"` → `"a\xEF\xBF\xBD" "b"`; `"\xE6\x97"` → one U+FFFD;
  `"\xF0\x9F\x8E" "a"` → U+FFFD then `a`; `"\xC0\xAF"` → two; `"\xE0\x80\x80"` and
  `"\xED\xA0\x80"` → three; `"\xF4\x90\x80\x80"` and `"\xF5\x80\x80\x80"` → four.
- "the validator and the replacement agree": over the reject list and the accept list,
  `isValidUtf8(x) == (detail::replaceIllFormed(x) == x)`.
- "path text round-trips valid text": for `"aä日🎞"` and a nested `"ü/日.txt"`,
  `pathText(pathFromText(x)) == x`, `pathDisplayText(pathFromText(x)) == x`, and
  `pathFromText(pathText(p)) == p`.
- "path text keeps the bytes of a non-UTF-8 name" (POSIX): for
  `p = std::filesystem::path(std::string("a\xFF" "b"))`, `pathText(p) == std::string("a\xFF" "b")`,
  `pathFromText(pathText(p)) == p`, and `pathDisplayText(p) == "a\xEF\xBF\xBD" "b"`.
- "a non-UTF-8 file name opens through its path text" (Linux; APFS refuses such names): create
  `dir / std::string("x\xFF" ".bin")`; `std::filesystem::exists(pathFromText(*percentDecode(
  percentEncode(pathText(p)))))` is true.
- "a name with no UTF-8 form" (Windows): for
  `p = std::filesystem::path(std::wstring{L'a', static_cast<wchar_t>(0xD800), L'b'})`,
  `pathText(p)` and `pathDisplayText(p)` are both `"a\xEF\xBF\xBD" "b"`, with no throw; and
  `pathFromText("a\xFF" "b").native() == std::wstring{L'a', static_cast<wchar_t>(0xFFFD), L'b'}`.
- "utf16 decoder replaces or rejects unpaired surrogates" (on `char16_t`): a pair gives its 4-byte
  form; a lone high surrogate at the end, a lone low surrogate, and a high surrogate followed by a
  non-low unit each give U+FFFD with `Replace` and nullopt with `Reject`; empty input gives "".
- "utf16 encoder replaces maximal subparts" (on `char16_t`): `u8"aä🎞"` as bytes → `u"aä🎞"`;
  `"a\xE6\x97"` → `u"a�"`; empty → empty.
- The existing test_paths case for an app name `"\xFF"` still fails with "application name must be
  valid UTF-8".

### 5.2 `dvv/base/utf8_win32.h`: the UTF-16 helpers (L3-0003-7)

```cpp
// include/dvv/base/utf8_win32.h — Windows only: wchar_t is UTF-16 there. Include it only in
// Windows code: a file with a Windows suffix, or the `_WIN32` include group of a shared file
// until L3-0004-4 moves that code into a per-OS file.
#pragma once
#if !defined(_WIN32)
#error "dvv/base/utf8_win32.h is Windows only"
#endif
#include <optional>
#include <string>
#include <string_view>
namespace dvv {
// Strict UTF-16 → UTF-8: nullopt for an unpaired surrogate; "" for empty input.
// Never throws std::system_error; may throw std::bad_alloc.
[[nodiscard]] std::optional<std::string> utf8FromWide(std::wstring_view text);
// UTF-8 → UTF-16 with pathFromText's Windows rule: each maximal ill-formed subpart becomes
// U+FFFD; never fails. A caller that must reject bad input checks isValidUtf8 first.
[[nodiscard]] std::wstring wideFromUtf8(std::string_view utf8);
}  // namespace dvv
```

Implementation: `src/utf8_win32.cpp`. `utf8FromWide` is
`detail::utf8FromUtf16<wchar_t>(text, detail::BadUnits::Reject)`. `wideFromUtf8` is
`detail::utf16FromUtf8<wchar_t>(utf8)`. The build adds the file with
`dvv_os_sources(dvv_base OPTIONAL src/utf8)`. dvv-base's copies are deleted:
- `toUtf8` (dvv-base@d3bd517:src/environment.cpp:50-69); `getEnvironment` calls `utf8FromWide`,
  and nullopt still means "not set / not representable";
- `toWide` (dvv-base@d3bd517:src/environment.cpp:29-48); names and values are already valid
  (`validName`/`validValue`), so `wideFromUtf8` gives the same result;
- the lenient conversion in `SystemLogSink::write` (dvv-base@d3bd517:src/system_log_sink.cpp:50-68),
  which becomes `wideFromUtf8(line)`.

environment.cpp and system_log_sink.cpp are shared files; they include the header inside their
existing `#if defined(_WIN32)` include groups (dvv-base@d3bd517:src/environment.cpp:8-12,
dvv-base@d3bd517:src/system_log_sink.cpp:8-17), which the header comment allows until L3-0004-4.

Edge cases:
- An input longer than INT_MAX is no longer an error: the decoder is not the Win32 API.
- `wideFromUtf8` on a line with bad bytes writes one U+FFFD per maximal subpart, as Windows' own
  conversion does.

Approaches considered:
- Name and namespace: `dvv::utf8FromWide` (chosen), or `dvv::win32::utf8FromUtf16`. The `_win32`
  header already says "Windows". A namespace that repeats it adds nothing.
- Strict helper only (the case's shape), with UTF-8→UTF-16 through `pathFromText(x).native()` at
  each call site, or the pair `utf8FromWide`/`wideFromUtf8` (chosen, spec's call). A window title,
  an environment value or a log line is not a path. A named function says what the call does, and
  costs one line on top of the same implementation.
- `win32ErrorMessage` (dvv-platform) stays strict, with its existing "unknown error (error N)"
  fallback, rather than getting a lenient variant (P7: no bool mode). FormatMessageW returns
  well-formed text, and the error code is kept.
- A Win32 API body (the case) or the portable decoder and encoder (chosen). One pair serves the
  path functions and the wide helpers, and it is tested on all three toolchains. After this change
  no `WideCharToMultiByte`/`MultiByteToWideChar` call is left in dvv-base.
- `#error` guard (chosen) or a link error on misuse. The guard names the problem at compile time.
  It is an OS `#if` in a per-OS file, which the gate exempts.

Tests (`tests/test_utf8_win32.cpp`, new, Windows only, added through
`dvv_os_sources(dvv_base_tests OPTIONAL test_utf8)`):
- "utf8FromWide converts": `L""` → `""`; `L"aä日"` → `"a\xC3\xA4\xE6\x97\xA5"`; U+1F39E as a
  pair → its 4 bytes.
- "utf8FromWide rejects unpaired surrogates": `std::wstring{static_cast<wchar_t>(0xD800)}` and
  `std::wstring{L'a', static_cast<wchar_t>(0xDC00)}` give nullopt.
- "wideFromUtf8 converts and replaces": `"aä"` → `L"aä"`; `"a\xFF"` → `L"a\xFFFD"`;
  `"a\xE6\x97"` → `L"a\xFFFD"` (one U+FFFD).
- The existing test_environment cases stay green, plus "a non-ASCII value round-trips through
  set and get".

### 5.3 `dvv/base/local_time.h` (L3-0003-2)

```cpp
// include/dvv/base/local_time.h
#pragma once
#include <chrono>
#include <ctime>
#include <optional>

namespace dvv {
// The local calendar time of `time`, to the second (localtime_s on Windows, localtime_r
// elsewhere); nullopt when the platform cannot convert it (outside its local-time range, e.g.
// before 1970 on Windows).
[[nodiscard]] std::optional<std::tm> localTime(std::chrono::system_clock::time_point time) noexcept;
}  // namespace dvv
```

Implementation: `src/local_time_win32.cpp` and `src/local_time_posix.cpp`. Each converts
`to_time_t(floor<seconds>(time))` and checks the result. They are added with
`dvv_os_sources(dvv_base src/local_time)`, so no OS `#if` remains anywhere. `splitLogFileStem`
(dvv-base@d3bd517:src/split_file_sink.cpp:69-84) calls `dvv::localTime` and keeps its UTC
fallback. That removes the 5-line `#if` at lines 71-75.

Approaches considered:
- `std::optional<std::tm>` (chosen) or `std::tm localTimeOrUtc(time_point)` that applies the
  fallback itself. A bare `std::tm` cannot tell the caller that it holds UTC, so the caller could
  not label or test it. Both users already pick their own fallback form: the split sink formats
  `sys_time` directly, and rawreel builds UTC fields.
- A 5-line commented `#if` in one `local_time.cpp`, or the per-OS pair (chosen). The pair follows
  the rule this release writes, gives `dvv_os_sources` its first user in the train, and leaves
  dvv-base with no OS block for the 0.5.x moves to remove.
- The `std::chrono` time-zone path: out of the gate on Apple libc++.

Tests (`tests/test_local_time.cpp`, new):
- "localTime converts the epoch": `localTime(from_time_t(0))` has a value with `tm_year` 69 or 70.
- "localTime of extreme times does not crash": `time_point::min()` and `time_point::max()` give
  nullopt or a value.
- "localTime before 1970 is nullopt on Windows": `from_time_t(-86400)` is nullopt on Windows
  (`_localtime64_s` rejects negative times) and has a value elsewhere.
- test_split_file_sink.cpp:140-185 (the stem by local time, with a fixed clock) stays green.

### 5.4 `SplitFileSink::setHeader` on `dvv::MoveOnlyFunction` (L3-0008-1, decision 7)

```cpp
// include/dvv/base/log.h, class SplitFileSink
// … Pass nullptr for no header. An empty std::function is not "no header": it is a callable that
// throws std::bad_function_call at the first header, from setHeader itself when the open file
// has no line yet, else from the write that starts the next file.
void setHeader(MoveOnlyFunction<std::string(const std::filesystem::path& previous)> make);
…
MoveOnlyFunction<std::string(const std::filesystem::path&)> make_header_;
```

The body (dvv-base@d3bd517:src/split_file_sink.cpp:215-221) is unchanged: it moves the argument
in, tests it for emptiness, and calls it from the non-const `writeHeader`
(dvv-base@d3bd517:src/split_file_sink.cpp:303-311) under the sink's mutex. log.h includes
`dvv/base/move_only_function.h`. `<functional>` leaves log.h if nothing else in it needs it
(move_only_function.h includes it, dvv-base@d3bd517:include/dvv/base/move_only_function.h:14).

**An empty `std::function` (S-p4, §11 question 2).** Today `make_header_` is a `std::function`
and an empty one means "no header" (dvv-base@d3bd517:src/split_file_sink.cpp:218 and :297). A
`MoveOnlyFunction` built from an empty `std::function` is not empty, as the standard specifies for
`std::move_only_function`; it wraps a callable that throws. So `setHeader(std::function<…>{})`
throws `std::bad_function_call` at the first header. Thrown from a write on the async logger
thread it ends the process. The header comment states it, `nullptr` is the documented "no header",
the CHANGELOG lists it under "Changed" as a behaviour change, and a test pins it. No dependent is
affected: the only caller outside dvv-base's tests passes a lambda
(rawreel@5a48754:src/gui/app.cpp:596).

**The polyfill must treat a null pointer and an empty function as empty (spec's call, a fix in the
same release).** Today `detail::MoveOnlyFunctionBase`'s converting constructor wraps any `F`
(dvv-base@d3bd517:include/dvv/base/move_only_function.h:29-36), including a null function
pointer. The result is non-empty, and calling it calls through a null pointer
(dvv-base@d3bd517:include/dvv/base/move_only_function.h:74-79). `std::move_only_function` gives
no target for a null function pointer, a null member pointer, or a `move_only_function`
specialization with no target. `std::function` (today's parameter type) also treats a null
function pointer as empty, so today `setHeader(static_cast<Fn*>(nullptr))` means "no header".
Without the fix the same call would crash under the sink's mutex on AppleClang, which is the
polyfill's toolchain, and only there. Fix (inside the class template, so `if constexpr` depends
on `F`):
```cpp
template <class Signature> class MoveOnlyFunction;  // declared before the base

template <class T> inline constexpr bool IS_MOVE_ONLY_FUNCTION = false;
template <class S> inline constexpr bool IS_MOVE_ONLY_FUNCTION<MoveOnlyFunction<S>> = true;

// in MoveOnlyFunctionBase, same constraints as today:
template <class F> requires(…)
MoveOnlyFunctionBase(F&& target) {
    using D = std::decay_t<F>;
    // A function reference is never null, and testing one would trip -Waddress under -Werror.
    if constexpr (!std::is_function_v<std::remove_reference_t<F>> &&
                  ((std::is_pointer_v<D> && std::is_function_v<std::remove_pointer_t<D>>) ||
                   std::is_member_pointer_v<D> || IS_MOVE_ONLY_FUNCTION<D>)) {
        if (target == nullptr) {
            return;  // no target, as std::move_only_function
        }
    }
    model_ = std::make_unique<Model<D>>(std::forward<F>(target));
}
```
A `MoveOnlyFunction` of the same signature never reaches this constructor (the move constructor
takes it, as today). `target == nullptr` on a `MoveOnlyFunction` finds the base's hidden
friend through argument-dependent lookup. This matches the standard for the three cases, so
code that already compiles against `std::move_only_function` sees no change.

Tests:
- test_move_only_function.cpp, new cases over the polyfill and the alias:
  - a `TEST_CASE_TEMPLATE` over the same types as the existing ones: "a null function pointer
    makes an empty function", checking `function == nullptr` and `!function`;
  - "a null member pointer makes an empty function": `int (S::*member)() = nullptr;` into
    `MoveOnlyFunction<int(S&)>` is empty;
  - "an empty function of another signature makes an empty function": an empty
    `MoveOnlyFunction<int() noexcept>` moved into `MoveOnlyFunction<int()>` is empty.
- test_split_file_sink.cpp, new:
  - "split sink: setHeader takes a move-only callable": a lambda that captures a
    `std::unique_ptr<std::string>`, whose text is the first line of the new file;
  - "split sink: setHeader takes a std::function": a named, non-empty lvalue, whose text is the
    first line;
  - "split sink: setHeader(nullptr) means no header": the new file starts with no header line;
  - "split sink: a null function pointer means no header";
  - "split sink: an empty std::function throws at the first header": on a sink just opened with
    `SplitOpen::NewFile`, `CHECK_THROWS_AS(sink->setHeader(std::function<std::string(const
    std::filesystem::path&)>{}), std::bad_function_call)`.
- The existing setHeader tests (lines 367-460 and 653) stay green.

Approaches considered: decision 7 settles the type: plain, with no template and no shim. The open
items were the null-pointer gap, which the fix above closes, and the empty `std::function`, which
§11 question 2 records.

### 5.5 Enum forms beside the bool forms (L3-0006-5, dvv-base part)

```cpp
// include/dvv/base/log.h
enum class SourceSuffix : std::uint8_t { Without, With };
// SourceSuffix::With is the one-argument form; Without drops the " [file.cpp:42]" suffix.
[[nodiscard]] std::string formatLogRecord(const LogRecord& record, SourceSuffix suffix);
// Deprecated (CHANGELOG 0.5.0): use the SourceSuffix form.
[[nodiscard]] std::string formatLogRecord(const LogRecord& record, bool with_source);

enum class SplitOpen : std::uint8_t { NewFile, AppendNewest };
// class SplitFileSink:
[[nodiscard]] static Result<std::shared_ptr<SplitFileSink>> open(
    std::filesystem::path folder, std::string prefix, std::uintmax_t max_bytes, SplitOpen mode);
// Deprecated (CHANGELOG 0.5.0): use the SplitOpen form.
[[nodiscard]] static Result<std::shared_ptr<SplitFileSink>> open(
    std::filesystem::path folder, std::string prefix, std::uintmax_t max_bytes, bool append_newest);
```

Both enums put the `false` meaning first, so a value-initialised enum (`SourceSuffix{}`,
`SplitOpen{}`) means what `bool{}` means.

Behaviour: each bool form forwards to its enum form (`true` → `With` / `AppendNewest`). The enum
forms hold the implementation (dvv-base@d3bd517:src/split_file_sink.cpp:140-143 and the test at
:187 become `mode == SplitOpen::AppendNewest`). dvv-base's own call at
dvv-base@d3bd517:src/split_file_sink.cpp:310 passes `SourceSuffix::Without`. The comments in
log.h (dvv-base@d3bd517:include/dvv/base/log.h:42-45, :148-149) and the README text
(dvv-base@d3bd517:README.md:52-63) describe the enum forms.

Overload safety: a scoped enum has no implicit conversion. So `true`, `false`, a `bool`
variable, an `int` and an `std::atomic<bool>::load()` all pick the bool form exactly as today, and
the census call sites (rawreel@5a48754:src/core/log_store.cpp:14,
rawreel@5a48754:src/core/log_file_switch.cpp:71-77, rawreel@5a48754:src/core/log_file_switch.cpp:110)
compile unchanged. Nothing outside dvv-base and rawreel calls either function (L3-0006-5 census).
A braced `{}` flag (`formatLogRecord(r, {})`) becomes ambiguous, since both conversions are
identity conversions; no caller in the snapshots passes one.

Tests:
- test_log.cpp: "formatLogRecord: SourceSuffix::With equals the one-argument form" and
  "formatLogRecord: SourceSuffix::Without drops the location".
- test_split_file_sink.cpp: its 30 `open` calls take the enum arguments. One new case, "split
  sink: the bool open form still appends and creates", keeps the deprecated form covered until
  its removal.

### 5.6 The split-log name parser becomes public (D-0003, F-L1-rawreel-core-cli-16)

Spec's call, beyond the release-train row: D-0003 assigns this step to dvv-base 0.5.0 ("dvv-base
0.5.0 makes the name parser public"). Leaving it out would cost a second dvv-base minor before
rawreel's first re-pin release. §11 hands the row update to the train.

rawreel re-implements the sink's private name grammar
(rawreel@5a48754:src/core/log_file_switch.cpp:79-104 against
dvv-base@d3bd517:src/split_file_sink_detail.h:13-24). rawreel needs ASCII case-insensitive
matching for Windows and macOS file names (rawreel@5a48754:src/gui/main_frame.cpp:205).

```cpp
// include/dvv/base/log.h, beside SplitFileSink (log.h includes <compare>)
// A name SplitFileSink writes: stamp is YYYYMMDDHHMMSS as a number, number the -N suffix (1
// without one). The comparison orders by (stamp, number), which is creation order.
struct SplitLogName {
    std::uint64_t stamp = 0;
    std::uint32_t number = 1;
    friend constexpr auto operator<=>(const SplitLogName&, const SplitLogName&) = default;
};
enum class LetterCase : std::uint8_t { Exact, IgnoreAscii };
// Parses "<prefix>-YYYYMMDD-HHMMSS.log" or "<prefix>-YYYYMMDD-HHMMSS-N.log" (8 and 6 digits; N 1 to
// 9 digits, no leading zero). IgnoreAscii compares the prefix and ".log" without ASCII case;
// SplitFileSink itself always uses Exact.
[[nodiscard]] std::optional<SplitLogName> parseSplitLogName(std::string_view name,
    std::string_view prefix, LetterCase letters = LetterCase::Exact) noexcept;
```

`detail::SplitLogName` and `detail::parseSplitLogName` move to these public names, and the sink
calls the public ones. The sink's `std::tie` comparison
(dvv-base@d3bd517:src/split_file_sink.cpp:179-180) becomes `*name > *newest_name`, so the sink and
rawreel use one ordering. tests/test_split_file_sink.cpp:233 drops its `using dvv::detail::…`. New
tests: "parseSplitLogName ignores ASCII case when asked": `"RAWREEL-20261110-153126.LOG"` with
`IgnoreAscii` has a value, with `Exact` it does not, and digit and suffix rules are unchanged in
both modes; "SplitLogName orders by stamp, then number".

### 5.7 Deprecations (CHANGELOG only) and MemorySink (L3-FIX-1)

| form | why | replacement | removed |
|---|---|---|---|
| `formatLogRecord(record, bool)` | bare bool flag (L3-0006-5) | `SourceSuffix` form | dvv-base 1.0.0 |
| `SplitFileSink::open(…, bool)` | bare bool flag (L3-0006-5) | `SplitOpen` form | dvv-base 1.0.0 |
| `Errc::ShaderCompile` | no dvv library returns it; only its definition, `errcName` and rawreel's exhaustive switch name it (F-L1-dvv-base-1; dvv-base@d3bd517:include/dvv/base/error.h:14-23, rawreel@5a48754:src/cli/output.cpp:157-169) | none (a shader failure is `Internal` or `Unsupported`, as today) | dvv-base 1.0.0 |
| `RotatingFileSink` | no user outside dvv-base's own tests; a second file-rotation policy beside `SplitFileSink` (F-L1-dvv-base-3; dvv-base@d3bd517:include/dvv/base/log.h:96-141) | `SplitFileSink` | dvv-base 1.0.0 |

`Errc::Cancelled` is not deprecated. dvv-media 0.2.0 gives it a producer (L3-0005-1), and
final-report.md's fix list says so.

**MemorySink stays in log.h** (L3-FIX-1, which supersedes revision 1's move to a testing header).
It is in real use: tests in dvv-base, dvv-platform, dvv-gpu, dvv-components and rawreel read
logged lines through it (for example dvv-gpu@ce4c7d3:tests/gpu_fixture.h:49 and
rawreel@5a48754:tests/test_config.cpp:62). An in-memory sink is a general sink, not a test
authority, so moving it would only rename it for every user. Only its stale comment
(dvv-base@d3bd517:include/dvv/base/log.h:85) changes, to "Keeps every line in memory; tests read
what was logged through it", with a CHANGELOG "Fixed" line. No testing header is added, and
S-p12 (a header-only test class) falls away with the move.

**The file-sink tests move to `SplitFileSink` in 0.5.0** (L3-FIX-1), so the 1.0.0 removal deletes
only rotation tests: the warning on disk, the leak-check flush and reopen-appends
(dvv-base@d3bd517:tests/test_log.cpp:317-370), the mark flushed at once
(dvv-base@d3bd517:tests/test_log.cpp:597-611), the stop without start
(dvv-base@d3bd517:tests/test_log_async.cpp:520-530) and the lines on disk after a pass
(dvv-base@d3bd517:tests/test_log_async.cpp:669-688) run against `SplitFileSink` where it does not
already cover the behaviour.

Approaches considered for F-L1-dvv-base-3:
- Keep both file sinks and fix the comment only: rejected. Two rotation policies would stay in the
  public API with no user.
- Move `MemorySink` to the testing headers (revision 1): rejected by L3-FIX-1, see above.

### 5.8 Build, version, install and the package test

- `project(dvv_base VERSION 0.5.0 …)` (dvv-base@d3bd517:CMakeLists.txt:2).
- dvv-deps.txt: `dvv-build 0.5.0` (dvv-base@d3bd517:dvv-deps.txt:2). The workflow's `@v` tag
  follows the line (check-pins).
- `include("${DVV_CMAKE_DIR}/DvvOsSources.cmake")` after DvvCompilerOptions. New sources:
  `src/utf8.cpp` in the source list (dvv-base@d3bd517:CMakeLists.txt:30-43), and
  `dvv_os_sources(dvv_base src/local_time)` and `dvv_os_sources(dvv_base OPTIONAL src/utf8)`
  beside the existing `if(WIN32)` paths block (dvv-base@d3bd517:CMakeLists.txt:51-57, which
  L3-0004-4 converts later).
- The install line (dvv-base@d3bd517:CMakeLists.txt:68-73) adds
  `${DVV_BUILD_CMAKE_DIR}/DvvOsSources.cmake`. The config file
  (dvv-base@d3bd517:cmake/dvv_baseConfig.cmake.in:1-15) already exports `DVV_CMAKE_DIR`; its
  comment names DvvOsSources too.
- tests/CMakeLists.txt adds test_utf8.cpp and test_local_time.cpp, and
  `dvv_os_sources(dvv_base_tests OPTIONAL test_utf8)`.
- The package test (S-o2) covers the installed and the in-tree consumer, which
  dvv-base@d3bd517:tests/package/CMakeLists.txt:7-12 already builds both ways. Its
  `find_package(dvv_base 0.1 REQUIRED)` becomes `0.5`. After the existing checks
  (dvv-base@d3bd517:tests/package/CMakeLists.txt:14-21) it fails unless
  `${DVV_CMAKE_DIR}/DvvOsSources.cmake` exists, includes it, and adds the consumer's per-OS probe
  with `dvv_os_sources(consumer os_probe)` from two new fixture files, `os_probe_posix.cpp` and
  `os_probe_win32.cpp`, each defining `const char* osProbe()`. main.cpp
  (dvv-base@d3bd517:tests/package/main.cpp:1-12) includes `dvv/base/utf8.h`,
  `dvv/base/local_time.h` and `dvv/base/log.h`, and checks `osProbe()`,
  `pathText(pathFromText("a")) == "a"`, `localTime(from_time_t(0)).has_value()` and
  `parseSplitLogName("p-20261110-153126.log", "p").has_value()`, so each new installed header
  compiles standalone in a consumer.
- `os-blocks.txt`: written with `-DMODE=write` after all of the above, in the same commit. The
  deleted `toWide`/`toUtf8` block (dvv-base@d3bd517:src/environment.cpp:28-70) means it starts
  below the R5 count of 7 counted blocks; the 5-line `localtime` block was never counted. The
  commit message lists any file whose count differs from the census.

### 5.9 dvv-base CHANGELOG 0.5.0

```markdown
## [0.5.0] - <date>

One owner for UTF-8, path and UTF-16 text and for local time; named flags; requires dvv-build 0.5.0.

### Added
- `dvv/base/utf8.h`: `isValidUtf8`; `pathText` and `pathFromText`, the round-trip path text (the
  native bytes on POSIX, so a name that is not UTF-8 stays usable); `pathDisplayText`, always
  well-formed UTF-8 for messages and UI (one U+FFFD per maximal ill-formed subpart). None throws
  std::system_error.
- `dvv/base/utf8_win32.h` (Windows only): `utf8FromWide` (strict) and `wideFromUtf8`.
- `dvv/base/local_time.h`: `localTime(time_point)` → `std::optional<std::tm>`.
- `SourceSuffix` and `formatLogRecord(record, SourceSuffix)`; `SplitOpen` and
  `SplitFileSink::open(folder, prefix, max_bytes, SplitOpen)`.
- `SplitLogName` (ordered by stamp, then number), `LetterCase` and
  `parseSplitLogName(name, prefix, letters)`.
- The package installs `DvvOsSources.cmake` next to `DvvCompilerOptions.cmake`.

### Changed
- `SplitFileSink::setHeader` takes a `MoveOnlyFunction<std::string(const std::filesystem::path&)>`.
  Lambdas (also move-only ones), `std::function` objects, function pointers and `nullptr` are
  accepted. Pass `nullptr` for no header. Behaviour change: an empty `std::function` no longer
  means "no header"; it throws `std::bad_function_call` at the first header.
- `MoveOnlyFunction` (the polyfill used where the standard library lacks
  std::move_only_function) has no target when built from a null function pointer, a null member
  pointer or an empty `MoveOnlyFunction` of another signature, as the standard type does.
- Requires dvv-build 0.5.0.

### Deprecated (removed in 1.0.0)
- `formatLogRecord(record, bool)`: use `SourceSuffix`.
- `SplitFileSink::open(…, bool append_newest)`: use `SplitOpen`.
- `Errc::ShaderCompile`: no dvv library returns it.
- `RotatingFileSink`: use `SplitFileSink`.

### Fixed
- `MemorySink`'s comment no longer names a GUI log panel.
```

## 6. Migration notes for dependents

General rule (lockstep): a repository that raises dvv-base to 0.5.0 raises dvv-build to 0.5.0 in
the same commit. Its CI workflow reference moves to `dvv-ci.yml@v0.5.0`, which turns on the
"OS blocks" step and Format on `*.mm`. So the same commit also:
1. runs `cmake -DREPO=. -DMODE=write -P <dvv-build>/tools/os-blocks.cmake` and commits
   `os-blocks.txt`;
2. reformats its `.mm` files with the pinned clang-format 19.1.7;
3. lets Tidy take the database list (nothing to do in the repository; dvv-gpu drops its guard,
   below).

No call site has to change on the day of the pin: every 0.4.1 caller form compiles against 0.5.0.
The migrations below belong to each repository's own release in the train. Each path conversion
moves to the function §5.1.1 names for it.

| repository | when | what it does |
|---|---|---|
| dvv-registry | not before its 1.0.0 | nothing. It may stay on dvv-build 0.4.x and dvv-base 0.4.1; dvv-base 0.5.0 is compatible with its minimum |
| dvv-platform 0.3.0 | train step 4 | deletes `displayPath` and both `widen` copies; calls `dvv::pathDisplayText`, `dvv::wideFromUtf8` (in `test_window_win32.cpp`), and `isValidUtf8` + `wideFromUtf8` in `DynamicLibrary::open` (keeps "the name is not valid UTF-8"); `win32ErrorMessage` uses `utf8FromWide(…).value_or("")`; reformats `src/testing/test_window_cocoa.mm`; baseline about 4 counted blocks |
| dvv-gpu 0.3.0 | train step 5 | deletes its `utf8` copies (dvv-gpu@ce4c7d3:src/pipeline_cache.cpp:51-56 and the test's) and calls `dvv::pathDisplayText`; drops the whole-file `__APPLE__` guard of `tests/present_test_metal.cpp` (dvv-gpu@ce4c7d3:tests/present_test_metal.cpp:6-8 and :116), since the file is no longer in the Linux database; baseline about 2 counted blocks |
| dvv-media 0.2.0 | train step 6 | includes `dvv/base/utf8.h`; deletes `wellFormedLength` and both bodies; `pathFromUtf8` and `utf8FromPath` (dvv-media@134f98b:include/dvv/media/types.h:93-99) become inline forwarders to `dvv::pathFromText` and `dvv::pathText`, CHANGELOG "Deprecated" (removed in dvv-media 1.0.0) and "Changed": on POSIX `pathFromUtf8` keeps bytes that are not UTF-8 instead of replacing them, so such a file opens from its URI; on Windows `utf8FromPath` gives U+FFFD for an unpaired surrogate instead of the whole-path placeholder; its own calls move to the dvv-base names (§5.1.1); test_types.cpp: "pathFromUtf8 replaces invalid UTF-8" (dvv-media@134f98b:tests/test_types.cpp:222) expects the bytes kept on POSIX and maximal-subpart U+FFFD on Windows, and the placeholder case (dvv-media@134f98b:tests/test_types.cpp:236-242) expects `"a\xEF\xBF\xBD" "b"`; README names dvv-base as the owner |
| dvv-components 0.3.0 | train step 7 | moves its calls off the deprecated dvv-media names to the dvv-base names (§5.1.1: `pathText` at loader.cpp:273 and container_supplier.cpp:636, `pathDisplayText` for messages, `pathFromText` for `DVV_FFMPEG_DIR`); raises dvv-build and dvv-base with its other lines; baseline about 1 counted block |
| rawreel, first re-pin | train step 8 | moves every path conversion to the function §5.1.1 names (`pathText` for the drop URI, the settings key, the stored folder, recent entries and the `--config` argument; `pathDisplayText` for messages and wx text; `pathFromText` for every text-to-path); `localTime` calls `dvv::localTime` and, on nullopt, fills `tm` with the UTC fields from `year_month_day`/`hh_mm_ss` (no `#if`; rawreel@5a48754:src/core/log_text.cpp:26-38), with a Windows-only test for a pre-1970 time; `utf8Arguments` calls `dvv::utf8FromWide` and keeps its "argument <i> is not valid Unicode" error, and its `windows.h` include goes; `openLogFile` takes `dvv::SplitOpen` and both `formatLogRecord` callers pass `SourceSuffix`; `isLogFileName` becomes a call of `dvv::parseSplitLogName` with `LetterCase` (the bool parameter goes with L3-0006-6), and the newest-file choice uses `SplitLogName`'s ordering; the "either form" text (rawreel@5a48754:docs/superpowers/specs/2026-10-19-rawreel-design.md:2249-2250) points to dvv-build's "Per-OS code"; three `.mm` files reformatted; baseline written (about 74 counted blocks by the R5 count) |
| rawreel, second re-pin | train step 15 | drops `case dvv::Errc::ShaderCompile` (rawreel@5a48754:src/cli/output.cpp:164) and its two test lines (rawreel@5a48754:tests/test_cli_output.cpp:52-53) when dvv-base 1.0.0 removes the code. They cannot go earlier: the switch is exhaustive, and clang's -Wswitch under -Werror would reject a missing enumerator while it still exists |

Name lookup: dvv-base's new names (`pathText`, `pathFromText`, `pathDisplayText`) differ from
dvv-media's (`pathFromUtf8`, `utf8FromPath`), so no call becomes ambiguous in any window, even
under `using namespace dvv; using namespace dvv::media;`.

## 7. Order of work

1. dvv-build: one pull request (one squashed commit) with §4. Tag `v0.5.0` after the user merges.
2. dvv-base: one pull request with §5, pinned on the `v0.5.0` tag. Tag `v0.5.0` after the merge.
   Inside the branch, cost orders the work: build/pin → utf8 → utf8_win32 → local_time → setHeader
   and the polyfill → enums → name parser → deprecations, file-sink test ports and docs → package
   test → baseline last.
3. The dependents follow the train (§6). None is part of this spec's acceptance.

Per commit, run only the relevant tests: `ctest -R "^dvv_build\."` and `dvv_base_tests` with the
`-tc` filters of the touched files. Run the full suite once at the end, serially.

## 8. Acceptance

- dvv-build: the four `dvv_build.*` tests above pass on the three OS jobs. dvv-ci.yml names no
  per-OS suffix. On the snapshots, the Tidy list for each repository on Linux holds no `_win32`,
  `_msw`, `_macos`, `_metal`, `_cocoa` or `_stub` file, and every `.cpp` it held before that Linux
  compiles.
- `os-blocks.cmake -DMODE=write` on the R0 snapshots gives one row per file with counted blocks.
  The sums of the counts are close to the R5 mechanical block count (L3-0004-1): rawreel 74,
  dvv-base 7, dvv-platform 4, dvv-gpu 2 and dvv-components 1. A sum that differs is explained in
  the adopting commit.
- dvv-base: all tests pass on the three CI toolchains, including ASan, TSan and Tidy.
  `src/utf8_path.h`, `toWide` and `toUtf8` are gone. `grep -rn "MultiByteToWideChar\|WideCharToMultiByte\|localtime_[rs]" src`
  finds only `src/local_time_*.cpp`. `grep -rn "std::function<" include src` finds nothing.
  `grep -rn "u8string" include src` finds nothing (no path conversion outside utf8.cpp's native
  forms). log.h has no new bare `bool` parameter. The package test passes in both modes, so the
  installed package contains `DvvOsSources.cmake` and the new headers compile in a consumer.
  `os-blocks.txt` passes `check`.
- Path text: the Linux test "a non-UTF-8 file name opens through its path text" passes.
- Compatibility: rawreel at 5a48754 (dvv-base line raised to 0.5.0, dvv-build to 0.5.0, baseline
  written, `.mm` files reformatted, and no other change) builds and passes its tests on the three
  toolchains. This proves "every caller form compiles". The same check runs for dvv-components at
  its snapshot.

## 9. Risks

- Format on `*.mm` makes the first lint run red in rawreel and dvv-platform unless the adopting
  commit reformats. §6 puts the reformat in that commit.
- The gate's parser could hide a block through a wrong nesting parse. The os_blocks fixtures cover
  nesting, a group inside a non-OS group, `#elif`, `#ifdef`, spaces after `#`, continuation lines,
  comments, `#define` blocks, `.h` and `.mm` files, stale rows and a malformed baseline.
- A call mapped to the wrong text form: path text shown in wx comes out empty, and display text
  used as an identity names a different file. §5.1.1 maps every call, the header comments state
  the rule, and each dependent's spec review checks its sites against the map.
- `utf8FromPath`'s output through dvv-media's forwarder changes on Windows from the placeholder to
  U+FFFD. No caller compares against the placeholder: the only match outside dvv-media's own test
  is its definition.
- An empty `std::function` passed to `setHeader` now throws (§5.4). No dependent passes one.
- The polyfill fix changes behaviour only for null pointers and empty functions, which today crash
  when called.
- The `string(JSON)` cost on a large database is bounded by the one whole-document parse.

## 10. Backend outlook (P10)

Every item is neutral for stages B and C. No Vulkan type, no backend API. Fix size M (two
repositories, all additive). Residual for stage C: 0.

## 11. Decisions to confirm

The two spec questions from the reviews, with the answers applied at the coordinator's
recommendation (both approved by the user). The user approved both on 2026-11-18.

1. **Linux paths whose names are not UTF-8 (S-p1).** Applied answer: they stay openable through
   the native bytes. A path has two text forms with two clearly named functions: `pathText` (with
   its inverse `pathFromText`), the round-trip form, and `pathDisplayText`, well-formed UTF-8 with
   U+FFFD only there (§5.1). Every call in the snapshots is mapped to one of them (§5.1.1). If the
   user declines, the spec falls back to one display function and states that such files cannot
   be opened.
2. **An empty `std::function` passed to `setHeader` (S-p4, a consequence of decision 7).** Applied
   answer: it throws `std::bad_function_call` at the first header; the header comment and the
   CHANGELOG ("Changed", behaviour change) document it; `nullptr` means "no header"; a test pins
   the throw (§5.4). No dependent passes one.

Settled, not open: R7-d8 (the 1.0.0 window covers all six dvv libraries) is confirmed by the user,
so the forms deprecated in §5.7 are removed in dvv-base 1.0.0.

Hand-over to the release train (the release train is updated to match; this spec
does not): the dvv-base 0.4.1 → 0.5.0 row also gains `pathText`/`pathFromText`/`pathDisplayText`
as the content of utf8.h, `wideFromUtf8` beside the UTF-16→UTF-8 helper, the public split-log
name parser (§5.6), the polyfill fix and the empty-`std::function` behaviour change (§5.4); the
dvv-media 0.2.0 row gains the deprecation of `pathFromUtf8`/`utf8FromPath` (forwarders) and the
dvv-media 1.0.0 row their removal; the dvv-components 0.3.0 and rawreel first re-pin rows gain
the moves of §5.1.1.

Everything else is the spec's call, marked where it is made: §4.1 PRIVATE only, unconditional ARC;
§4.2 per-repo baseline, tests not counted, `#define` lines count as code; §4.3 the database
reader; §4.4 README only, ship in 0.5.0; §5.1 the names, U+FFFD on Windows instead of WTF-8, the
maximal-subpart replacement; §5.2 the `wideFromUtf8` pair and the portable decoder and encoder;
§5.3 the per-OS pair; §5.4 the polyfill fix; §5.6 the public name parser and its ordering.

## 12. Revision

Revision 2 (2026-11-18), after both reviews (C0 I5 M19 in total), the user's "Spec 0.5
questions" and L3-FIX-1:
- §5.1 rewritten for S-p1 and the user's direction: two text forms, `pathText`/`pathFromText`
  (round trip, native bytes on POSIX) and `pathDisplayText` (well-formed, U+FFFD), replacing
  revision 1's single `pathFromUtf8`/`utf8FromPath`; new §5.1.1 maps every call in the snapshots;
  the non-goal on round trips now covers only Windows' unpaired surrogates; new anti-goal against
  display text as an identity; §6, §8 and §9 follow.
- S-p2: the `if constexpr` dispatch replaced by overloads on `path::string_type`; the polyfill fix
  is shown as code whose `if constexpr` depends on the template parameter.
- S-p3: every hex escape split or written as a unit list; §3 states the rule.
- S-o12 (optional part) adopted: one U+FFFD per maximal ill-formed subpart instead of per byte,
  with a UTF-16 encoder template beside the decoder.
- S-o1, S-p9: "rows" and "counted blocks" separated in §4.2, §5.8, §6 and §8.
- S-o2: the package test raises its minimum to 0.5, includes `DvvOsSources.cmake`, builds a per-OS
  probe and compiles the new headers (§5.8).
- S-o3, S-o9, S-o10: free lines defined (only `#include`, `#pragma`, `#error`, `#warning` and
  conditional directives; `#define` counts); baseline parse errors defined; nine os_blocks
  fixtures added (§4.2, §4.5); §9's coverage claim matches the fixtures.
- S-o4, S-p7: the utf8_win32.h comment allows the `_WIN32` include group of a shared file.
- S-o5, S-p6: the polyfill also leaves an empty function of another signature empty; tests for a
  null member pointer and that case.
- S-o6: the dvv-base baseline reason names only the `toWide`/`toUtf8` block.
- S-o7, S-p8: the acceptance grep is `grep -rn "std::function<" include src`.
- S-o8: `ENCODING UTF-8`, the trailing comma, the exact CI command, and tests for a minified
  database and a non-ASCII path.
- S-o11: the dvv-build CHANGELOG names the three queries and the compile-database dependency.
- S-o12, S-p11: `SplitLogName` gets a defaulted `operator<=>`, used by the sink; the split sink's
  dead `try`/`catch` is removed.
- S-p4: the empty-`std::function` case documented in the header, the CHANGELOG ("behaviour
  change") and a test; listed in §11.
- S-p5: `SourceSuffix { Without, With }`; the braced `{}` ambiguity noted.
- S-p10: the train-row additions handed over in §11.
- S-p12 and L3-FIX-1: `MemorySink` stays in log.h with a corrected comment (CHANGELOG "Fixed");
  revision 1's testing header and its deprecation are dropped; the file-sink tests move to
  `SplitFileSink` in 0.5.0; the `ShaderCompile` wording follows L3-FIX-1.
- §11 became "Decisions to confirm"; R7-d8 is recorded as confirmed.

## 13. Revision 3: errata from the plan reviews

These three errata supersede the text they name. Where the sections above differ, the errata win.

### E1: §4.3, the CI Tidy step resolves the tool from the workspace (P-p1), and prints its list (P-p4)

Reason: the Tidy step runs with `working-directory: ${{ inputs.repo }}`
(dvv-build@3ddc842:.github/workflows/dvv-ci.yml:640), and `DVV_BUILD` is relative to the
workspace (`dvv-build` or `<app>/libs/dvv-build`, dvv-ci.yml:45). `"$DVV_BUILD/tools/..."`
therefore resolves to `<repo>/dvv-build/tools/...` and does not exist. Every other
`$DVV_BUILD/tools` call sits in a step without a working directory. The printed list lets §8's
Tidy-list acceptance read the Linux list from the CI log.

Section: §4.3 "CI Tidy step".

Old text:
```bash
cmake "-DDB=$GITHUB_WORKSPACE/$DVV_ROOT/_build/$DVV_REPO/ninja-clang/compile_commands.json" \
      -DREPO=. "-DOUT=$RUNNER_TEMP/tidy-files.txt" -P "$DVV_BUILD/tools/tidy-files.cmake"
```
then `tr '\n' '\0' < "$RUNNER_TEMP/tidy-files.txt" | xargs -0 -r -P "$(nproc)" -n 2 clang-tidy -p
"$GITHUB_WORKSPACE/$DVV_ROOT/_build/$DVV_REPO/ninja-clang" --quiet`, in bash with pipefail.

New text:
```bash
cmake "-DDB=$GITHUB_WORKSPACE/$DVV_ROOT/_build/$DVV_REPO/ninja-clang/compile_commands.json" \
      -DREPO=. "-DOUT=$RUNNER_TEMP/tidy-files.txt" -P "$GITHUB_WORKSPACE/$DVV_BUILD/tools/tidy-files.cmake"
echo "::group::Tidy files"; cat "$RUNNER_TEMP/tidy-files.txt"; echo "::endgroup::"
```
then `tr '\n' '\0' < "$RUNNER_TEMP/tidy-files.txt" | xargs -0 -r -P "$(nproc)" -n 2 clang-tidy -p
"$GITHUB_WORKSPACE/$DVV_ROOT/_build/$DVV_REPO/ninja-clang" --quiet`, in bash with pipefail. The
step runs in `${{ inputs.repo }}`, so every path in it is absolute (`$GITHUB_WORKSPACE/...`) or
relative to the repository (`-DREPO=.`). The list is printed in a collapsed log group, so the
Linux Tidy list of each run can be read back (§8).

### E2: §4.2 and §4.3, both tools require `REPO` to be the top of a git work tree (P-o2)

Reason: a plain folder inside another work tree (the ctest work folder lies in the git-ignored
`libs/_build/` of the rawreel work tree) makes `git -C <folder> ls-files` exit 0 with no output, so
os-blocks reported "OK (0 files ...)" and a gate passed silently. A `REPO` that is a subfolder of a
repository would likewise check a part of it under the wrong relative paths.

Section: §4.2 "Definitions", bullet **Files**.

Old text: "If `git ls-files` fails, the tool fails."

New text: "Before listing, the tool requires `git -C <REPO> rev-parse --show-toplevel` to succeed
and to equal `<REPO>` (both compared after `file(REAL_PATH)`); otherwise it fails with
"`<REPO>` is not the top of a git work tree". If `git ls-files` fails, the tool fails."

Section: §4.3, the paragraph "Reading the database", last sentence.

Old text: "The tool also fails when the database is missing, and when the candidates of step 3 are
not empty but the result is empty (the paths did not match)."

New text: "The tool also fails when `REPO` is not the top of a git work tree (the same check and
message as os-blocks, §4.2), when the database is missing ("compile database not found: <DB>"),
and when the candidates of step 3 are not empty but the result is empty ("the compile database
paths matched none of the tracked sources")."

Section: §4.5, the `dvv_build.os_blocks` list, after the last bullet. New bullet:
"- a plain folder (no repository of its own, inside an ignored part of another work tree) and a
  subfolder of a repository each fail with "is not the top of a git work tree"."

Section: §4.5, the `dvv_build.tidy_files` paragraph. Old text: "A missing database fails. A
database that names none of the candidates fails." New text: "A missing database fails naming the
database. A database that names none of the candidates fails with "matched none of the tracked
sources". A `REPO` that is a subfolder of the repository fails with "is not the top of a git work
tree"."

### E3: §5.3, `localTime` computes the seconds without converting back (P-p2)

Reason: `floor<seconds>(time)` yields a `time_point<system_clock, seconds>`, and `to_time_t` takes
`system_clock::time_point`, so the argument is converted back to the clock's unit (µs on libc++,
ns on libstdc++). For `time_point::min()` that product is below INT64_MIN: signed overflow, which
the ASan presets (`-fsanitize=address;undefined -fno-sanitize-recover=all`,
dvv-build@3ddc842:cmake/DvvCompilerOptions.cmake:64-65) abort on.

Section: §5.3 "Implementation".

Old text: "Each converts `to_time_t(floor<seconds>(time))` and checks the result."

New text: "Each converts `static_cast<std::time_t>(std::chrono::floor<std::chrono::seconds>(time.time_since_epoch()).count())`,
with no conversion back to the clock's unit, so every `time_point` (`min()` and `max()` included)
is converted without undefined behaviour, and checks the result."

Section: §5.3 "Tests", second bullet.

Old text: "\"localTime of extreme times does not crash\": `time_point::min()` and `time_point::max()`
give nullopt or a value."

New text: "\"localTime of extreme times does not crash\": `time_point::min()` and `time_point::max()`
give nullopt or a value, with no undefined behaviour (the case runs under the UBSan presets
`ninja-macos-asan` and `ninja-clang-asan`, which abort on signed overflow)."

### E4: §5.2, the environment pin is the existing round-trip case (P-o11, Task 10 review)

Reason: test_environment.cpp already has "environment values round-trip as UTF-8". It sets,
reads and clears a non-ASCII value on every OS (clearing is `setEnvironment(name, "")`; there is
no remove call), and on Windows it reads the value back with `GetEnvironmentVariableW`. A second
case with another name would test the same path, so the plan adds none and pins the rewrite with
this one.

Section: §5.2 "Tests", last bullet.

Old text: "The existing test_environment cases stay green, plus \"a non-ASCII value round-trips
through set and get\"."

New text: "The existing test_environment cases stay green; \"environment values round-trip as
UTF-8\" is the pin across the rewrite (it sets, reads through `GetEnvironmentVariableW` on Windows,
and clears a non-ASCII value)."
