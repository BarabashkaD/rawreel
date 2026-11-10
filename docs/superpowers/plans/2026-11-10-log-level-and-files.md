# Log Level, Markers, Source and Log Files Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:**
- A saved default log level (Settings) and a run-only level (Log list menu).
- A padded marker at each change in the pane, the file and the system log.
- No source location unless `log.source` is set.
- Log files named at creation, split by size, never deleted, with a 1 GiB warning.

**Architecture:** Two parts.

Part A (dvv-base 0.4.0) adds:
- `LogRecord::mark` and `Logger::mark`, in synchronous mode, asynchronous mode and the crash
  snapshot;
- `formatLogRecord(record, with_source)`;
- `SplitFileSink`.

Part B (rawreel, branch `feat/log-pane-settings`):
- the config keys and the core text;
- the log store and the file switch;
- the App level control;
- the Settings, menu and list changes;
- the README.

The final task moves rawreel to the released dvv-base 0.4.0.

**Tech Stack:** C++23, dvv-base logger (ring, snapshot), wxWidgets 3.3, doctest.

**Spec:** `docs/superpowers/specs/2026-11-10-log-level-and-files-design.md` (revision 2, cited as
"§N"). It extends `docs/superpowers/specs/2026-11-10-log-pane-and-settings-design.md` ("base §N").

## Global Constraints

- **dvv-base is a library with other users.**
  - Existing APIs and behaviour stay: `RotatingFileSink` and the one-argument `formatLogRecord`
    are unchanged.
  - The snapshot magic stays `DVVLOGS1`.
  - The version becomes 0.4.0, with CHANGELOG entries.
- **Where the two parts are committed:**
  - **Part A** in the `libs/dvv-base` submodule, on a new branch `feat/log-mark-split` from
    `v0.3.0` (efb55c7).
  - **Part B** in rawreel on `feat/log-pane-settings`.
  - rawreel builds against the submodule's working checkout until Task B6.
- **No logging inside sinks, the store's write, or the GUI refresh path.** No sink calls back into
  the Logger.
- **No timers or polling**, tests included. `src/core` stays wx-free.
- **Selftests:**
  - never show native dialogs or popup menus (hooks and seams only);
  - never depend on the OS focus;
  - never touch the user's files;
  - call `Logger::flush()` before reading a log file.
- **Path ↔ text:** only through `dvv::media::utf8FromPath` and `pathFromUtf8` (rawreel), or the
  dvv-base equivalent. Never `path::string()` into text.
- **Build:**
  - clang-format 19.1.7 (`$HOME/.claude/venvs/dvv-build-tools/bin/clang-format`);
  - no warnings (MSVC /W4 /WX in CI; avoid C4996 CRT functions);
  - clang-tidy naming: private members `lower_case_`, methods `camelBack`, constants
    `UPPER_CASE`.
- **Commits:**
  - identity BarabashkaD / 6252647+BarabashkaD@users.noreply.github.com;
  - a `Co-Authored-By` trailer;
  - no machine paths, real names or e-mail in committed files.
- **Tests per task:** only the task's tests. The full suites run serially at the end; GUI tests
  never run in parallel.
- **Commands:**
  - dvv-base tests:
    `cmake --build libs/_build/dvv-base/ninja-macos --target dvv_base_tests && ctest --test-dir libs/_build/dvv-base/ninja-macos -R dvv_base.unit --output-on-failure`
    (if that tree does not build the submodule checkout, configure it with the repo's
    `ninja-macos` preset first);
  - rawreel core run, smoke selftest and build: as in the previous plan
    (`libs/_build/rawreel/ninja-macos`, Release).

## Review Focus

1. **A worker thread logs while the level changes.** No crash, the marker is present, and every
   line at or above the new level after the marker is kept. Pinned by "mark keeps order with
   records" (A2) and the selftest "level change reaches the file" (B4).
2. **Two rawreel processes on one log folder.** They never write the same file. Pinned by
   "split sink: exclusive creation skips a taken name" and "open(false) always creates" (A3), and
   by the append rule in B2.
3. **A log folder on a stalled or vanished volume.** No retry on every line. Pinned by "split
   sink: back-off after a failed creation" (A3).
4. **A hand-written `rawreel-old.log` or `rawreel-20261110-1.log` in the folder.** Never appended
   to. Pinned by "split sink: non-matching names are ignored" (A3).
5. **A crash right after a level change.** The marker is in the file. Pinned by "a mark is flushed
   at once" (A3 for `SplitFileSink`, A2 for `RotatingFileSink`).

---

## Part A: dvv-base 0.4.0 (in `libs/dvv-base`, branch `feat/log-mark-split`)

### Task A1: `LogRecord::mark` and `formatLogRecord(record, with_source)`

**Files:** `include/dvv/base/log.h`, `src/log.cpp`, `tests/test_log.cpp`

**Interfaces (produces):**
- `struct LogRecord { …existing…; bool mark = false; }`: a last member, so `log.cpp`'s positional
  aggregate initialisation still compiles.
- `[[nodiscard]] std::string formatLogRecord(const LogRecord& record, bool with_source);`
  - The one-argument form calls it with `true`, and its output is byte-identical to today's.
  - With `false`, the line is `"<time> <LEVEL> <message>"`, without the ` [file:line]` suffix.

- [ ] **Step 1: Write the failing tests.**
  - `"formatLogRecord without source drops the location"`: the same record formatted with `true`
    equals the one-argument form; with `false` it equals that string minus its ` [<file>:<line>]`
    suffix.
  - `"LogRecord::mark defaults to false"`.
- [ ] **Step 2: Run, see it fail.** Expected: no matching overload.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `dvv_base.unit`. Expected: PASS.
- [ ] **Step 5: Commit** `feat(log): LogRecord::mark and formatLogRecord without the source`.

### Task A2: `Logger::mark` (synchronous, asynchronous, snapshot, sinks)

**Files:** `include/dvv/base/log.h`, `src/log.cpp`, `src/log_ring.h`, `src/log_snapshot.cpp`,
`src/system_log_sink.cpp`, `tests/test_log.cpp`, `tests/test_log_async.cpp`,
`tests/test_log_snapshot.cpp`

**Interfaces (produces):**
- `void Logger::mark(std::string_view text, std::source_location where = std::source_location::current());`
  The contract is in §3.2, verbatim:
  - an Info record with `mark = true`;
  - delivered to every sink whose level is not Off, ignoring the logger level and the sink
    thresholds;
  - in order with other records;
  - nothing without sinks.
- `inline constexpr std::uint8_t LOG_RECORD_MARK = 2;` in `log_ring.h`, next to
  `LOG_RECORD_WARN_BARE`.
- The `log.h` snapshot format comment documents flags bit 1 as "mark".

- [ ] **Step 1: Write the failing tests.**
  - `test_log.cpp`:
    - `"mark reaches a sink at Error but not a sink at Off"`;
    - `"a plain Info record does not reach a sink at Warn"` (regression);
    - `"mark ignores the logger level"`: `setLevel(Error)`, then `mark` is still delivered;
    - `"mark keeps order with records"`: `write(Info,"a")`, `mark("m")`, `write(Info,"b")` → a
      MemorySink sees `a, m, b` with `mark` true only on `m`;
    - `"mark with no sinks does nothing"`;
    - `"a mark is flushed at once by RotatingFileSink"`: after `mark`, the file's bytes on disk
      contain the line before any `flush()`.
  - `test_log_async.cpp`:
    - `"mark is delivered in asynchronous mode in order"`;
    - `"mark survives a ring filled past 3/4"`: fill the ring with Info until a plain Info is
      dropped, then `mark` → delivered.
  - `test_log_snapshot.cpp`: `"snapshot keeps the mark bit"`. Queue a mark in asynchronous mode,
    write the snapshot, decode: the line is the marker as an Info line, and the decoded record's
    flags have bit 1.
- [ ] **Step 2: Run, see it fail.**
- [ ] **Step 3: Implement.**
  - `mark` gets its own entry point; it skips the `enabled()` gate.
  - **Synchronous:** it delivers under the Logger mutex through a variant of `deliver` that skips
    the threshold for sinks at a level below Off.
  - **Asynchronous:** `queueRecord` reserves with Warn's priority and sets `LOG_RECORD_MARK`;
    `handleRecord`'s acceptance test and `writeToSinks` honour it. `emitLine` carries
    `record.mark`.
  - `RotatingFileSink::write` flushes on `record.mark`, as it does on Warn.
  - `SystemLogSink` writes marks at `OS_LOG_TYPE_DEFAULT`, `LOG_NOTICE`, or `OutputDebugStringW`.
  - The snapshot decoder passes bit 1 through.
  - Document in `log.h`: in asynchronous mode a sink's level is read at drain time.
- [ ] **Step 4: Run** `dvv_base.unit`. Expected: PASS.
- [ ] **Step 5: Commit** `feat(log): Logger::mark delivers a marker to every sink that is not Off`.

### Task A3: `SplitFileSink`

**Files:** `include/dvv/base/log.h` (or a new `include/dvv/base/split_file_sink.h` included by
it), `src/split_file_sink.cpp`, `CMakeLists.txt`, `tests/test_split_file_sink.cpp`,
`tests/CMakeLists.txt`

**Interfaces (produces):** exactly §5.4, with one change, where the plan rules on the header:

```cpp
// Called under the sink's mutex when a new file is created; returns the header line's message
// (the sink formats it as an Info line, flushed). `previous` is the file it continues, empty for
// the first. Must not log.
void setHeader(std::function<std::string(const std::filesystem::path& previous)> make);
```

Ruling: §5.4's `setHeader(std::string)` cannot name the previous file of a split that happens
under the lock, so the header is made by a callback. The callback text is rawreel's (B3).

- [ ] **Step 1: Write the failing tests** (a temporary folder; drop the sink before reading
  files):
  - `"split sink: names a new file by local creation time"`: one file matching
    `^p-\d{8}-\d{6}\.log$`.
  - `"split sink: exclusive creation skips a taken name"`: pre-create the name the next
    creation would take (inject the clock, or create every `-N` of the current second), then
    open → a fresh `-N`.
  - `"split sink: appends to the newest file under the limit"`: pre-create
    `p-20260101-000000.log` (10 bytes) and `p-20260102-000000.log` (10 bytes) →
    `open(append_newest=true)` writes into the 0102 file.
  - `"split sink: newest order with suffixes"`: `p-…-000000.log`, `p-…-000000-2.log`,
    `p-…-000000-10.log` → the `-10` file is chosen.
  - `"split sink: non-matching names are ignored"`: `p-old.log`, `p-20261110-1.log`, `pp-…` → a
    new file is created, and they are untouched.
  - `"split sink: open(false) always creates"`.
  - `"split sink: splits at the limit without dividing a line"`: `max_bytes` 100; write lines →
    every file ≤ 100 except when a single line is larger; no line divided; `splits()` counts.
  - `"split sink: never splits an empty file"`: one 500-byte line with `max_bytes` 100 → one file.
  - `"split sink: header in each new file, not in an appended one"`: the header callback gets the
    previous path.
  - `"split sink: back-off after a failed creation"`: make the folder read-only after open
    (POSIX), fill past the limit → the current file keeps growing. Count creation attempts
    through a test hook or the file count: no new attempt until `max_bytes / 4` more.
  - `"split sink: nothing renamed or deleted"`: after several splits, every created file still
    exists with its name.
  - `"split sink: totalBytes"`: pre-existing matching files of known size plus written bytes.
  - `"split sink: a mark is flushed at once"`.
- [ ] **Step 2: Run, see it fail.**
- [ ] **Step 3: Implement.**
  - C++23 `std::ios::noreplace` for exclusive creation.
  - Strict name parsing.
  - Back-off like `RotatingFileSink::next_rotation_at_`.
  - `localtime_r` / `localtime_s` (not `std::chrono::current_zone`).
  - Failure notes to `stderr` only.
- [ ] **Step 4: Run** `dvv_base.unit`. Expected: PASS.
- [ ] **Step 5: Commit** `feat(log): SplitFileSink names log files by creation time and splits by size`.

### Task A4: dvv-base 0.4.0 docs and version

**Files:** `CMakeLists.txt` (project version 0.4.0), `CHANGELOG.md` (0.4.0 entries for A1–A3),
`README.md` ("Logging": `mark`, `SplitFileSink`, `formatLogRecord(record, with_source)`)

- [ ] **Step 1:** Update the version, the CHANGELOG and the README as facts.
- [ ] **Step 2:** Run `dvv_base.unit` and the asan preset's tests
  (`libs/_build/dvv-base/ninja-macos-asan`). Expected: PASS.
- [ ] **Step 3: Commit** `docs: dvv-base 0.4.0`.

---

## Part B: rawreel (branch `feat/log-pane-settings`)

### Task B1: Config keys, level text and marker text (core)

**Files:** `src/core/config.{h,cpp}`, `tests/test_config.cpp`; create `src/core/log_level.{h,cpp}`,
`tests/test_log_level.cpp`; `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces (produces):**

```cpp
// core/config.h
struct LogConfig { LogFileConfig file; SystemLogConfig system; dvv::LogLevel level = dvv::LogLevel::Info;
                   bool levelSaved = false;   // log.level present in the file
                   bool source = false; };
// core/log_level.h
[[nodiscard]] std::string_view levelShownName(dvv::LogLevel level) noexcept;  // "Errors"… §2.1
[[nodiscard]] std::string_view levelConfigValue(dvv::LogLevel level) noexcept; // "error"…
[[nodiscard]] std::optional<dvv::LogLevel> parseLevel(std::string_view text) noexcept;  // any case
enum class LevelSource : std::uint8_t { Settings, Default, ThisWindow, Menu };
[[nodiscard]] std::string levelStartMarker(dvv::LogLevel level, bool saved);
[[nodiscard]] std::string levelChangeMarker(dvv::LogLevel from, dvv::LogLevel to, LevelSource source);
[[nodiscard]] std::string logFileHeader(dvv::LogLevel level, std::string_view previous_name);
[[nodiscard]] std::string logSizeWarning(std::string_view folder_utf8, std::uintmax_t bytes);
inline constexpr std::uintmax_t LOG_SIZE_WARNING_BYTES = std::uintmax_t{1} << 30U;
```

- [ ] **Step 1: Write the failing tests.**
  - `test_config.cpp`:
    - `{"log":{"level":"Debug","source":true}}` → Debug, `levelSaved`, `source`;
    - `{}` → Info, `!levelSaved`, `!source`;
    - `{"log":{"level":"verbose"}}` → `Errc::Format` with
      `log.level: expected "error", "warn", "info", "debug" or "trace", not "verbose"`;
    - `{"log":{"source":1}}` → `log.source: expected true or false, not 1`;
    - the info line contains `log.level debug`.
  - `test_log_level.cpp`:
    - every value round-trips through `levelConfigValue` and `parseLevel`;
    - `parseLevel("WARN")` is Warn;
    - `levelStartMarker(Info, true)` == `"===== log level: Info (settings) ====="`;
    - `levelStartMarker(Info, false)` == `"===== log level: Info (default) ====="`;
    - `levelChangeMarker` gives the four texts of §3.1 exactly;
    - `logFileHeader(Info, "")` == `"===== log file: level Info ====="`;
    - `logFileHeader(Info, "rawreel-20261110-140210.log")` ==
      `"===== log file: level Info, continues rawreel-20261110-140210.log ====="`;
    - `logSizeWarning("/x", 1536 MiB)` ==
      `"log files in '/x' use 1536 MiB (over 1 GiB); rawreel never deletes them"`.
- [ ] **Step 2: Run, see it fail.**
- [ ] **Step 3: Implement.** The parser accepts any case; the writer writes lower case. The
  config writer validates both keys through `parseConfig`.
- [ ] **Step 4: Run** the core run. Expected: PASS.
- [ ] **Step 5: Commit** `feat(core): log.level and log.source keys, level names, marker and header texts`.

### Task B2: Store lines, marks, and the split file switch (core)

**Files:** `src/core/log_store.{h,cpp}`, `src/core/log_text.{h,cpp}`,
`src/core/log_file_switch.{h,cpp}`, `tests/test_log_store.cpp`, `tests/test_log_text.cpp`,
`tests/test_log_file_switch.cpp`

**Interfaces (produces):**
- `LogEntry` drops `source`. It keeps `std::source_location where` and `bool mark`, and `line` is
  formatted by the store with its `with_source` flag.
- `LogStore`:
  - `void setWithSource(bool)`, set at start before any record;
  - a mark is never counted in `LogCounts`.
- `std::string logSourceText(const LogEntry&)` returns `"file.cpp:42"`, for the optional column.
- `LogFileSwitch`:
  - `void setWithSource(bool)`: live records are written as `formatLogRecord(record, with_source)`;
  - `replace(file, backfill)` writes each `entry.line` unchanged.
- `openLogFile(folder, creation, bool append_newest)` returns
  `Result<std::shared_ptr<dvv::SplitFileSink>>` (prefix `"rawreel"`, `LOG_FILE_BYTES`).
  `LOG_FILE_NAME` and `LOG_FILE_BACKUPS` are removed.
- `[[nodiscard]] bool isLogFileName(std::string_view name, bool case_insensitive);`: the §5.1
  pattern, prefix `rawreel`.

- [ ] **Step 1: Write the failing tests.**
  - `"log store: lines without source by default"`: `line` equals
    `formatLogRecord(record, false)`. With `setWithSource(true)` it equals the source form.
  - `"log store: marks are not counted"`.
  - `"log file switch: backfill keeps each line's source"`: entries formatted with source → the
    file lines equal `entry.line`.
  - `"log file switch: live records without source"`.
  - `"log file switch: files are timestamped"`: the file in the folder matches the pattern.
  - `"log file name: pattern"`:
    - `rawreel-20261110-153126.log` and `rawreel-20261110-153126-2.log` → true;
    - `rawreel.log`, `rawreel-old.log` and `rawreel-20261110-1.log` → false;
    - `RawReel-20261110-153126.LOG` → true only when case-insensitive.
- [ ] **Step 2: Run, see it fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** the core run. `rawreel_gui` must still build: adjust its uses of
  `LOG_FILE_NAME`, `currentFile()` and `isLiveLogFile` minimally (B3/B4 finish them). Expected:
  PASS.
- [ ] **Step 5: Commit** `feat(core): store lines without source, marks not counted, timestamped split log files`.

### Task B3: The App level control, markers, header and size warning (GUI)

**Files:** `src/gui/main.cpp`, `src/gui/app.{h,cpp}`, `src/gui/main_frame.{h,cpp}`,
`src/gui/selftest.cpp`

**Interfaces (produces):**
- `App::setArguments(GuiArguments, std::shared_ptr<dvv::StderrSink>)`: main.cpp hands over the
  stderr sink.
- `App` members:
  - `dvv::LogLevel activeLevel() const`;
  - `dvv::LogLevel savedLevel() const` (`log.level`);
  - `void setActiveLevel(dvv::LogLevel level, LevelSource source)`, which sets the sink levels of
    the store, the file switch and stderr, then `Logger::mark(levelChangeMarker(…))`, and does
    nothing for the same level;
  - `std::filesystem::path currentLogFile() const`.
- At start (base §5.3 order):
  1. `Logger::setLevel(Trace)`;
  2. store/switch `setWithSource(config.log.source)`;
  3. sink levels = `config.log.level`;
  4. the first file opened with `append_newest = ownsSettings()`, its header callback
     `logFileHeader(activeLevel, previous.filename())`, and the backfill filtered to entries at or
     above the active level (§2.3);
  5. `log file: '<path>'`;
  6. `mark(levelStartMarker(level, levelSaved))`.
- The "log file moves to" line names the new `currentFile()`.
- The size warning:
  - at start, and in `MainFrame::onLogChanged` when `switch.splits()` changed since the last
    check, compare `totalBytes()` with `LOG_SIZE_WARNING_BYTES`;
  - the first time over in a run, `DVV_LOG_WARN(logSizeWarning(...))` on the GUI thread, outside
    the refresh path's no-logging rule: post it with `CallAfter`.

- [ ] **Step 1: Write the failing selftest steps** (smoke):
  - `"log level start marker"`: the store has `levelStartMarker(Info, …)`, and the current file
    (after `Logger::flush()`) starts with the header and contains `log file: '` and the marker.
  - `"log level debug reaches the file"`:
    `setActiveLevel(Debug, Settings)`, then `DVV_LOG_DEBUG("selftest debug line")` → it is in the
    store and the current file; a change marker is in both.
  - `"log level with system log off"`: with `useSystemLog(false)`, `setActiveLevel(Warn, Menu)` →
    the marker is in the store and the file, and `systemLogEnabled()` stays false.
  - `"log file is timestamped"`: `currentLogFile().filename()` matches `isLogFileName`.
- [ ] **Step 2: Run, see it fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** the smoke selftest, then player, serially. Expected: PASS.
- [ ] **Step 5: Commit** `feat(gui): active log level with markers, timestamped log files and the 1 GiB warning`.

### Task B4: Settings Level, the Log list menu, the Source column, Save refusal (GUI)

**Files:** `src/gui/settings.{h,cpp}`, `src/gui/log_panel.{h,cpp}`, `src/gui/log_list*.cpp`,
`src/gui/log_list.h`, `src/gui/main_frame.cpp`, `src/gui/selftest.{h,cpp}`, `tests/CMakeLists.txt`

**Interfaces (produces):**
- `LogSettingsPage`: `setLevel(dvv::LogLevel)` (selftests) and `[[nodiscard]] dvv::LogLevel
  level() const`.
  - The choice shows `savedLevel()`.
  - A change saves `log.level` through `saveSettings`, then calls
    `setActiveLevel(level, ownsSettings() ? Settings : ThisWindow)`.
  - On Windows this happens on OK, only if the control changed.
- `LogPanel::setRunLevel(dvv::LogLevel)`: the menu's handler, which calls
  `setActiveLevel(level, Menu)`. The context menu gets a `Level ▸` submenu, radio items,
  `activeLevel()` checked, after Select All between separators.
- `DialogHooks::logMenu` (`MoveOnlyFunction<void(const wxMenu&)>`): in a selftest, the built
  context menu goes to the hook instead of being shown.
- The Source column exists only when `config().log.source`; its text comes from
  `logSourceText(entry)`.
- Save refusal:
  - `equivalent(target, currentLogFile())`; or
  - `equivalent(target.parent_path(), logFolderInUse())` and `isLogFileName(name, WIN32||APPLE)`.
  - The message names the current file.
- A second CTest entry: `rawreel.gui_selftest.smoke_source` runs `--selftest smoke` with
  `--config <build>/test-home/source/rawreel.json`, written at configure time with
  `{"log":{"source":true}}`.

- [ ] **Step 1: Write the failing selftest steps:**
  - `"settings level"`: `logPage().setLevel(Debug)` (with `pressOk()` on Windows) → the scratch
    file has `"level": "debug"`, `activeLevel()` is Debug, and the store has a "(settings)"
    marker.
  - `"menu level"`: `setRunLevel(Trace)` → `activeLevel()` is Trace, `savedLevel()` is unchanged,
    the scratch file has no change, and the marker says "(this run, Log pane menu)". Opening the
    menu through the `logMenu` hook shows Trace checked.
  - `"settings level ends the run level"`: after the menu set Trace, Settings sets Info → the
    marker reads `Trace -> Info (settings)`.
  - `"no source column"`: three columns.
  - `"saved text without source"`: every saved line matches
    `^\d{4}-\d\d-\d\dT[\d:.]+Z [A-Z]+ +.*$`, and the lines from the selftest's own `DVV_LOG_INFO`
    calls do not end in ` [selftest.cpp:<n>]`.
  - `"save over a log file"`: a target named `rawreel-20200101-000000.log` in the log folder is
    refused.
  - In `smoke_source` (the source flag on): the Source column exists; saved lines from the
    selftest end in ` [selftest.cpp:<n>]`.
- [ ] **Step 2: Run, see it fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** the smoke selftest, then `smoke_source`, then player, serially. Expected:
  PASS.
- [ ] **Step 5: Commit** `feat(gui): log level in Settings and the Log menu, Source column only with log.source`.

### Task B5: README

**Files:** `README.md`

- [ ] **Step 1:** As facts:
  - Settings → Level and the menu's Level ▸;
  - lines below the level are not recorded;
  - the marker lines;
  - `log.level` and `log.source` in the key table;
  - log file naming, appending, splitting at 4 MiB, never deleting, local-time names, the 1 GiB
    warning;
  - the bug-report section naming the newest `rawreel-*.log`;
  - remove the `rawreel.log` backup text.
- [ ] **Step 2:** Check that no example has a machine path (a home folder on macOS, Linux or
  Windows) or a real name → nothing new.
- [ ] **Step 3: Commit** `docs: README for the log level, markers and timestamped log files`.

### Task B6: Release dvv-base 0.4.0 and pin it (stops for the user)

- [ ] **Step 1:** Run the full suites serially:
  - `dvv_base.unit` (normal and asan);
  - rawreel `DVV_REQUIRE_GPU=1 DVV_REQUIRE_FFMPEG=1 ctest … -j1`.
- [ ] **Step 2: Stop and ask the user** before:
  - pushing `feat/log-mark-split` and opening the dvv-base PR (the user merges);
  - tagging `v0.4.0` on the merge.
- [ ] **Step 3:** After the tag:
  - move `libs/dvv-base` to `v0.4.0`;
  - set `dvv-deps.txt` to `dvv-base 0.4.0`;
  - commit `chore: move to dvv-base 0.4.0`.
  - rawreel CI must run with `test_libs`, but only after the user approves a push.
