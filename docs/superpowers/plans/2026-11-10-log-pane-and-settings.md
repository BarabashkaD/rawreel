# Log Pane and Settings Window Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:**
- The Log pane is hidden by default, fed without a timer, and shows selectable rows that can be
  copied and saved.
- A native Settings window edits the log file, the system log and the file reads mode.
- Everything is kept in `rawreel.json`, written by the first rawreel-gui process only.

**Architecture:** The logic lives in wx-free units in `src/core`, each unit tested:
- a log store (the pane's Logger sink);
- a GUI-side mirror with selection mapping;
- the log text functions;
- a log file switch sink;
- the config keys and its location rule;
- a safe merging writer and the settings claim.

`src/gui` only wires them:
- the pane, with two virtual list adapters (one per OS family);
- the Edit menu dispatch;
- the Settings window (`wxPreferencesEditor`).

**Tech Stack:** C++23, wxWidgets 3.3 (core, base, aui), dvv-base 0.3.0 logger, nlohmann JSON
(through `dvv::json`, plus `nlohmann::ordered_json` in the writer), doctest.

**Spec:** `docs/superpowers/specs/2026-11-10-log-pane-and-settings-design.md` (revision 2, cited as
"spec §N").

**Reviews:** an optimistic and a pessimistic review of the first draft of this plan are folded in.

## Global Constraints

- **No timers or polling** anywhere, tests included; waits use events or subscriptions.
- **`src/core` stays wx-free.** Every unit marked "core" builds into `rawreel_core` and is tested in
  `rawreel_core_tests`.
- **Locks and logging (spec §3.3):**
  - the store's `write()` and the notify function never log;
  - the GUI refresh path (`MainFrame::onLogChanged`, the list adapters' batch code) never logs;
  - the lock order is Logger → store and Logger → file switch.
- **Sinks** are added once and never removed; there is no `clearSinks()` in rawreel.
- **The config** is written only through `App::saveSettings` (Task 8), which uses the core writer
  (Task 5) and refuses when this process is not the settings owner. rawreel-cli never writes.
- **Selftests:**
  - Never a dialog while the main window is closing.
  - In a `--selftest` run, a native dialog (`wxFileDialog`, `wxDirDialog`, `wxMessageDialog`) is
    **never shown**: its `showModal` calls the hook (which fills in the result) and returns
    `wxID_OK`. Without a hook it logs one warning and returns `wxID_CANCEL`.
  - Clipboard writes and file-manager hand-offs go to hooks in a selftest.
  - Steps never depend on the OS keyboard focus. Code that acts on "the focused window" takes it
    as a parameter, and the frame supplies `focusedWindow()`, which a selftest can override.
- **Tests never touch the developer's user data folder:** from Task 3 on, every CTest test runs with
  `HOME`, `XDG_DATA_HOME` and `APPDATA` set to `<build>/test-home` (spec §7.1).
- **Format** with clang-format 19.1.7; the build has no warnings (MSVC builds warnings-as-errors in
  CI).
- **Commits:** identity BarabashkaD / 6252647+BarabashkaD@users.noreply.github.com; no machine
  paths, real names or e-mail in committed files; each commit ends with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- **Tests per task:** only the tests the task names. The full suite runs once, serially, in
  Task 12; GUI selftests never run in parallel.
- **Platform-only code:**
  - Keep code that only Windows or only Linux compiles minimal, behind `#if`.
  - Prefer wx code that compiles on every OS (the `wxListCtrl` adapter is built and tested on
    macOS too).
  - List the parts that are not compiled locally in each task report. CI builds them only after
    the user approves a push.
- **Build tree:** `libs/_build/rawreel/ninja-macos`, configuration Release. The core test run is
  `cmake --build libs/_build/rawreel/ninja-macos --config Release --target rawreel_core_tests && ctest --test-dir libs/_build/rawreel/ninja-macos -C Release -R rawreel.core_unit --output-on-failure`
  (later called "the core run").

## Review Focus

1. Records logged by a worker thread while the log folder changes: each lands in exactly one file,
   none lost. Pinned by "log file switch: no record lost during replace" (Task 4).
2. The config file edited by hand while the app runs, then a setting saved: the hand edit
   survives. Pinned by "config writer: re-reads the file" (Task 5).
3. A symlinked or read-only `rawreel.json`: the link stays a link, a read-only file is refused.
   Pinned by "config writer: keeps a symlink" and "config writer: refuses a read-only file"
   (Task 5).
4. The user has selected rows and scrolled up while records stream into a full list: the selection
   and the top row stay on the same records. Pinned by "log mirror: rows after a batch" (Task 2)
   and the selftest step "log list keeps its place" on both adapters (Task 7).
5. A second rawreel-gui changes settings: the owner's file is untouched. Pinned by "settings claim:
   a second claim fails" (Task 5) and the owner guard in `App::saveSettings` (Task 8). The
   reviewer checks that no other code calls the writer.

---

### Task 1: The log store (core)

**Files:**
- Create: `src/core/log_store.h`, `src/core/log_store.cpp`, `tests/test_log_store.cpp`
- Modify: `CMakeLists.txt` (core sources, after `src/core/layout.cpp`), `tests/CMakeLists.txt`
  (`rawreel_core_tests` sources)

**Interfaces:**
- Produces (`namespace rawreel`):

```cpp
inline constexpr std::size_t LOG_STORE_RECORDS = 5000;

struct LogEntry {
    std::uint64_t sequence = 0;  // from 0, in write order, for the whole run
    dvv::LogLevel level = dvv::LogLevel::Info;
    std::chrono::system_clock::time_point time;
    std::string message;
    std::string source;  // "file.cpp:42": the file name without folders, as formatLogRecord shows it
    std::string line;    // the formatted line write() received
};
struct LogCounts {
    std::uint64_t warnings = 0;
    std::uint64_t errors = 0;
    bool operator==(const LogCounts&) const = default;
};
struct LogBatch {
    std::vector<LogEntry> entries;  // sequence >= since, oldest first
    LogCounts counts;               // totals so far
    bool gap = false;               // records from `since` on were already dropped
    std::uint64_t next = 0;         // the sequence the next record will get
};
class LogStore final : public dvv::ILogSink {
public:
    void write(const dvv::LogRecord& record, std::string_view line) override;
    void setNotify(std::function<void()> notify);         // empty: none
    [[nodiscard]] LogBatch take(std::uint64_t since);     // also clears pending, in one lock hold
    [[nodiscard]] std::vector<LogEntry> entries() const;  // every kept record
    [[nodiscard]] std::uint64_t next() const;
};
```

- [ ] **Step 1: Write the failing tests** in `tests/test_log_store.cpp` (`TEST_SUITE("core")`).
  Use a helper `put(store, level, message)` that builds a `dvv::LogRecord` and passes
  `formatLogRecord(record)` as the line.
  - `"log store: order and sequence numbers"`: put "a", "b" → `take(0)`: sequences 0, 1, messages
    "a", "b", `next == 2`, `!gap`; `take(1)` holds only "b"; `take(2)` is empty.
  - `"log store: keeps at most 5000 records"`: 5003 puts → `take(0)`: 5000 entries, front sequence
    3, `gap`; `take(4000)`: 1003 entries, `!gap`.
  - `"log store: counts warnings and errors"`: Trace, Debug, Info, Warn, Warn, Error →
    `take(0).counts == LogCounts{.warnings = 2, .errors = 1}`.
  - `"log store: notifies once until take"`: 3 puts → 1 notify; `take`; 1 put → 2 notifies.
  - `"log store: a notify set after records"`: Warn, Info with no notify; `setNotify(counter)` →
    0 calls; `take(0).counts.warnings == 1`; 1 put → 1 call.
  - `"log store: an empty notify stops notifications"`: notify set, put, `take`, `setNotify({})`,
    put → still 1 call.
  - `"log store: source and line"`: an entry's `source` is `"test_log_store.cpp:<line>"` and its
    `line` equals `formatLogRecord` of the record.
  - `"log store: writes from 4 threads"`: 4 `std::jthread` × 1000 puts → `next() == 4000`, at least
    one notify.
- [ ] **Step 2: Run, see it fail** (the build part of the core run). Expected: compile error,
  `core/log_store.h` not found.
- [ ] **Step 3: Implement.**
  - One `std::mutex` guards the `std::deque<LogEntry>`, `next`, the counts, `pending` and the
    notify function.
  - `pending` turns true only when a notify function is set and called (under the lock).
- [ ] **Step 4: Run** the core run. Expected: PASS.
- [ ] **Step 5: Commit** `feat(core): LogStore, the Log pane's sink with take() and a pending flag`.

### Task 2: The mirror, row mapping and log text (core)

**Files:**
- Create: `src/core/log_mirror.h`, `src/core/log_mirror.cpp`, `src/core/log_text.h`,
  `src/core/log_text.cpp`, `tests/test_log_mirror.cpp`, `tests/test_log_text.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `LogEntry`, `LogBatch`, `LOG_STORE_RECORDS` (Task 1).
- Produces (`core/log_mirror.h`):

```cpp
struct MirrorChange { std::size_t dropped = 0; std::size_t appended = 0; bool reset = false; };
class LogMirror {
public:
    MirrorChange apply(LogBatch batch);  // reset when batch.gap; keeps at most LOG_STORE_RECORDS
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const LogEntry& at(std::size_t row) const;
    [[nodiscard]] std::optional<std::size_t> rowOf(std::uint64_t sequence) const noexcept;
    [[nodiscard]] std::uint64_t next() const noexcept;  // what to pass to LogStore::take
    [[nodiscard]] const std::deque<LogEntry>& entries() const noexcept;
    // The entries with sequences in [first, last], oldest first (snapshots for copy and save).
    [[nodiscard]] std::vector<LogEntry> range(std::uint64_t first, std::uint64_t last) const;
};
// Where the list stood before a batch, by sequence number (spec §4.1).
struct ListPlace {
    std::vector<std::uint64_t> selected;
    std::optional<std::uint64_t> anchor, focus, top;
    bool tail = false;  // the last row was visible
};
struct ListRows {
    std::vector<std::size_t> selected;  // ascending
    std::optional<std::size_t> anchor, focus, top;
    bool tail = false;  // show the last row
};
[[nodiscard]] ListRows rowsAfterBatch(const ListPlace& before, const LogMirror& mirror);
```

- Produces (`core/log_text.h`):

```cpp
// "" | "1 warning in the log" | "2 warnings in the log" | "1 error in the log" |
// "2 errors, 3 warnings in the log"
[[nodiscard]] std::string logNoticeText(std::uint64_t errors, std::uint64_t warnings);
// Each entry's `line`, then '\n' (a line that already ends in '\n' gets none); "" for none.
// A template in the header over any input range of LogEntry (a vector, the mirror's deque).
template <std::ranges::input_range R>
    requires std::same_as<std::ranges::range_value_t<R>, LogEntry>
[[nodiscard]] std::string logRecordsText(const R& entries);
struct LocalTime { std::tm tm{}; int millis = 0; };
[[nodiscard]] LocalTime localTime(std::chrono::system_clock::time_point time);  // localtime_r/_s
[[nodiscard]] std::string logRowTime(const LocalTime& time);       // "HH:MM:SS.mmm"
[[nodiscard]] std::string logRowMessage(std::string_view message);  // first line + " …" if more
[[nodiscard]] std::string logSaveFileName(const LocalTime& time);  // "rawreel-log-YYYYMMDD-HHMMSS.txt"
```

- [ ] **Step 1: Write the failing tests.**
  - `test_log_mirror.cpp`:
    - `"log mirror: appends and drops"`: apply batches of 4000 and then 2000 entries (sequences
      contiguous) → the second returns `{.dropped = 1000, .appended = 2000, .reset = false}`;
      `size() == 5000`; `at(0).sequence == 1000`; `rowOf(999)` is empty; `rowOf(1000) == 0`.
    - `"log mirror: a gap resets"`: a batch with `gap = true` → `reset`, and the size equals the
      batch's.
    - `"log mirror: rows after a batch"`: from a mirror at sequences 0..4999, the place
      `{selected {10, 2000, 4999}, anchor 10, focus 2000, top 1500, tail false}`, then a batch of
      100 → `selected {1900, 4899}`, `anchor` empty, `focus 1900`, `top 1400`, `!tail`.
    - `"log mirror: tail stays at the end"`: `tail` true → `tail` true; `top` is empty.
    - `"log mirror: top dropped"`: `top` 50, then a batch of 100 → `top == 0`.
    - `"log mirror: range"`: sequences 10..20 of a mirror holding 0..99 → 11 entries; a range
      partly dropped → only the kept ones.
  - `test_log_text.cpp`:
    - `"log text: notice"`: the five values above.
    - `"log text: records"`:
      - lines "a" and "b\nc" → `"a\nb\nc\n"`;
      - a line "d\n" → `"d\n"`;
      - none → `""`;
      - the same entries in a `std::deque` → the same text.
    - `"log text: row message"`: "one" → "one"; "one\ntwo" → "one …".
    - `"log text: row time and file name"`: a `LocalTime` of 2026-11-10 09:05:03.007 →
      `"09:05:03.007"` and `"rawreel-log-20261110-090503.txt"`.
- [ ] **Step 2: Run, see it fail.** Expected: missing headers.
- [ ] **Step 3: Implement.** `rowsAfterBatch` maps each sequence through `mirror.rowOf`. A dropped
  `top` maps to row 0, and other dropped sequences leave the result.
- [ ] **Step 4: Run** the core run. Expected: PASS.
- [ ] **Step 5: Commit** `feat(core): log mirror with row mapping, and the log text functions`.

### Task 3: Config keys, the default location, and tests isolated from the user data folder

**Files:**
- Modify: `src/core/config.h`, `src/core/config.cpp`, `tests/test_config.cpp`,
  `tests/CMakeLists.txt` (the test environment)

**Interfaces:**
- Produces:

```cpp
struct GuiConfig { bool showLogPane = false; };
struct LogFileConfig { bool enabled = true; std::filesystem::path folder; };  // empty: default
struct SystemLogConfig { bool enabled = false; };
struct LogConfig { LogFileConfig file; SystemLogConfig system; };
struct Config { IoConfig io; GuiConfig gui; LogConfig log; };

// spec §6.1. `exists` decides whether the portable file next to the executable exists.
[[nodiscard]] dvv::Result<std::filesystem::path> defaultConfigPath(
    const std::filesystem::path& executable, bool macos,
    const std::function<dvv::Result<std::filesystem::path>()>& user_data_dir,
    const std::function<bool(const std::filesystem::path&)>& exists);
```

  `loadConfig(const std::optional<std::string>&)` makes a `--config` path absolute (against the
  working folder) before it loads it, and `LoadedConfig::path` holds that absolute path.

- [ ] **Step 1: Write the failing tests** (in `tests/test_config.cpp`):
  - `"config: parse"` gains:
    - `{"gui":{"showLogPane":true}}` → true;
    - `{"log":{"file":{"enabled":false,"folder":"<an absolute temp path>"},"system":{"enabled":true}}}`
      → those values;
    - `{"log":{"file":{"folder":""}}}` → accepted, with an empty folder (the default; Use Default
      writes this);
    - `{}` → the defaults (`showLogPane` false, file enabled, folder empty, system false).
  - `"config: invalid text and values"` gains these `Errc::Format` messages:
    - `{"gui":1}` → `gui: expected an object`;
    - `{"gui":{"showLogPane":"yes"}}` → `gui.showLogPane: expected true or false, not "yes"`;
    - `{"log":{"file":{"folder":"rel/dir"}}}` → `log.file.folder: expected an absolute folder or "", not "rel/dir"`;
    - `{"log":{"system":{"enabled":0}}}` → `log.system.enabled: expected true or false, not 0`.
  - **Rewrite** the existing tests "the default file of an executable in an app bundle" and "the
    default file is rawreel.json next to the executable" for the 4-argument form and the new rule:
    - in a bundle → the user data dir, `exists` not asked;
    - outside a bundle with the portable file → next to the executable, the user data function not
      called;
    - outside without it → the user data dir.
  - `"config: a relative --config is made absolute"`: `loadConfig(std::string("rel.json"))` →
    `path.is_absolute()`.
  - Test environment: in `tests/CMakeLists.txt`, every test gets `HOME`, `XDG_DATA_HOME` and
    `APPDATA` set to `${CMAKE_BINARY_DIR}/test-home`, through the list already passed as
    `ENVIRONMENT_MODIFICATION` (`DVV_TEST_ENVIRONMENT`) or one added next to it. Create the folder
    at configure time.
- [ ] **Step 2: Run, see it fail** (the build part of the core run). Expected: compile errors.
- [ ] **Step 3: Implement.**
  - Update the header comment (the keys and the location rule).
  - `defaultConfigPath()` (no arguments) passes `std::filesystem::exists`.
  - The load info line becomes:
    `config '<path>': io.fileReads <m>, gui.showLogPane <b>, log.file <on|off> '<folder or default>', log.system <on|off>`.
- [ ] **Step 4: Run** the core run, `rawreel.cli_unit` and `rawreel.cli_e2e.info`. The default case
  must now read no file from the developer's home: check that `<build>/test-home` is the only user
  data folder touched. Expected: PASS.
- [ ] **Step 5: Commit** `feat(core): gui and log keys in rawreel.json, portable-or-user-data location; tests use a build-tree home`.

### Task 4: The log file switch (core)

**Files:**
- Create: `src/core/log_file_switch.h`, `src/core/log_file_switch.cpp`,
  `tests/test_log_file_switch.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`, `src/gui/app.cpp` (use the moved constants)

**Interfaces:**
- Consumes: `LogEntry` (Task 1).
- Produces:

```cpp
inline constexpr std::uintmax_t LOG_FILE_BYTES = std::uintmax_t{4} << 20U;
inline constexpr int LOG_FILE_BACKUPS = 3;
inline constexpr std::string_view LOG_FILE_NAME = "rawreel.log";

enum class FolderCreation : std::uint8_t {
    LastOnly,     // a chosen folder: only the last component, only when its parent exists
    WithParents,  // the default (user data) folder, as today
};
[[nodiscard]] dvv::Result<void> prepareLogFolder(const std::filesystem::path& folder,
                                                 FolderCreation creation);
// prepareLogFolder, then RotatingFileSink::open(folder / LOG_FILE_NAME, LOG_FILE_BYTES, LOG_FILE_BACKUPS).
[[nodiscard]] dvv::Result<std::shared_ptr<dvv::RotatingFileSink>> openLogFile(
    const std::filesystem::path& folder, FolderCreation creation);

class LogFileSwitch final : public dvv::ILogSink {
public:
    void write(const dvv::LogRecord& record, std::string_view line) override;  // to the file, if any
    void batchEnd() override;
    void flush() override;
    // Under the switch's mutex: writes `backfill`'s lines into `file` (when non-null), then swaps it
    // in. Returns the old sink, which the caller drops outside any lock. Null: no log file.
    [[nodiscard]] std::shared_ptr<dvv::ILogSink> replace(std::shared_ptr<dvv::ILogSink> file,
                                                         std::span<const LogEntry> backfill = {});
};
```

- [ ] **Step 1: Write the failing tests** (a temporary folder, as in `tests/test_config.cpp`).
  Every test drops the returned sinks and the switch's last sink (`replace(nullptr)`) before it
  reads a file, because Info lines stay buffered until then.
  - `"log file switch: prepare creates the last folder only"`: `<tmp>/a` exists →
    `prepareLogFolder(<tmp>/a/b, LastOnly)` succeeds; `prepareLogFolder(<tmp>/x/y, LastOnly)` fails
    and `<tmp>/x` does not exist; `prepareLogFolder(<tmp>/p/q, WithParents)` succeeds.
  - `"log file switch: writes go to the current file"`: open in `<tmp>/one`, `replace`, write "r1";
    replace with a file in `<tmp>/two`, write "r2" → `one/rawreel.log` holds r1 only, `two/` r2
    only.
  - `"log file switch: no file, no write"`: `replace(nullptr)`, write → nothing thrown.
  - `"log file switch: backfill"`: `replace(file, {two entries})` → the file starts with their
    lines.
  - `"log file switch: no record lost during replace"`: a thread calls `write()` 2000 times while
    the main thread replaces the file 20 times, each with a new folder `<tmp>/f0`…`<tmp>/f19` →
    the lines in all files total 2000, each record exactly once.
- [ ] **Step 2: Run, see it fail.**
- [ ] **Step 3: Implement.** One mutex guards the held sink. `app.cpp` uses the moved constants.
- [ ] **Step 4: Run** the core run. Expected: PASS.
- [ ] **Step 5: Commit** `feat(core): LogFileSwitch, a log file sink that can change files`.

### Task 5: The config writer and the settings claim (core)

**Files:**
- Create: `src/core/config_writer.h`, `src/core/config_writer.cpp`, `src/core/settings_claim.h`,
  `src/core/settings_claim.cpp`, `tests/test_config_writer.cpp`, `tests/test_settings_claim.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `parseConfig` (Task 3).
- Produces:

```cpp
struct ConfigValue {
    std::string_view key;                   // dotted, e.g. "gui.showLogPane", "log.file.folder"
    std::variant<bool, std::string> value;  // strings are UTF-8
};
// spec §6.4. Never logs, never retries. Errc::Format with the loader's reason when parseConfig
// rejects the ORIGINAL text (the file is left unchanged); "read-only" when the permission bits
// grant no write; an I/O error otherwise.
[[nodiscard]] dvv::Result<void> saveConfigValues(const std::filesystem::path& path,
                                                 std::span<const ConfigValue> values);

// spec §6.3: an exclusive claim of a config file for this process, released by the destructor or
// by the process's end. Null when another claim (in any process, this one included) holds it.
class SettingsClaim {
public:
    [[nodiscard]] static std::unique_ptr<SettingsClaim> tryClaim(const std::filesystem::path& config);
    ~SettingsClaim();
    // POSIX: an exclusive non-blocking flock on "<config>.lock" (an empty file next to the config).
    // Windows: a named mutex "Local\\rawreel-settings-<16 hex of FNV-1a 64 over the UTF-8 of
    // weakly_canonical(config)>".
};
```

- [ ] **Step 1: Write the failing tests:**
  - `test_config_writer.cpp`:
    - `"config writer: creates the file"`: the folder `<tmp>/u` exists and the file is missing →
      save `gui.showLogPane = true` → it parses to that; no `*.tmp` is left in `<tmp>/u`.
    - `"config writer: creates the last folder"`: `<tmp>/v` exists, the file is `<tmp>/v/w/rawreel.json`
      → the save succeeds; with `<tmp>/x/y/rawreel.json` (no `<tmp>/x`) → it fails.
    - `"config writer: keeps other keys and the key order"`: the file
      `{"zeta":{"x":1},"io":{"fileReads":"buffered"},"gui":{"other":2}}`, save `gui.showLogPane =
      false` → keys at the top in the order `zeta`, `io`, `gui`; `zeta.x == 1`;
      `io.fileReads == "buffered"`; `gui.other == 2`; two-space indent, a final newline.
    - `"config writer: re-reads the file"`: load the file, change it on disk, save another key →
      the on-disk change is in the result.
    - `"config writer: leaves an invalid file alone"`: the files `{not json`, `[1]` and
      `{"gui":{"showLogPane":"yes"}}`, each saved with the value `gui.showLogPane = true` (which
      would fix the third) → `Errc::Format`, and the bytes are unchanged.
    - `"config writer: refuses a read-only file"`: permission bits without any write bit → an
      error whose message contains "read-only", file unchanged. The check reads the permission
      bits, so it also holds when the test runs as root.
    - `"config writer: keeps a symlink"` (POSIX only): `link → real.json` → after the save, `link`
      is still a symlink and `real.json` holds the value.
    - `"config writer: keeps the permission bits"` (POSIX only): 0660 → 0660 after the save
      (0660 differs from what a umask of 022 would give).
    - `"config writer: a string value"`: save `log.file.folder = "<abs>"` → the parsed folder
      equals it.
  - `test_settings_claim.cpp`:
    - `"settings claim: a second claim fails"`: `tryClaim(p)` → non-null; `tryClaim(p)` again →
      null.
    - `"settings claim: released by the destructor"`: reset the first → `tryClaim(p)` → non-null.
    - `"settings claim: different files"`: claims on two different paths → both non-null.
    - `"settings claim: one file, two spellings"` (POSIX): `p` and `<dir>/./name` → the second is
      null.
- [ ] **Step 2: Run, see it fail.**
- [ ] **Step 3: Implement** spec §6.4 steps 1–5:
  - `parseConfig(original)` must succeed before anything else;
  - merge with `nlohmann::ordered_json`, then `parseConfig(merged)` as a final check;
  - the temporary file is `<name>.<pid>.<random>.tmp`, created exclusively (`O_EXCL` or
    `CREATE_NEW`), with the target's mode bits on POSIX;
  - `fsync` (`F_FULLFSYNC` on macOS);
  - replace with `rename` on POSIX, `ReplaceFileW` on Windows (`MoveFileExW` when the target does
    not exist);
  - a missing folder: only the last component is created.

  The claim is `flock(LOCK_EX | LOCK_NB)` on POSIX, so a second open in the same process conflicts
  too. On Windows it is `CreateMutexW` plus `GetLastError() == ERROR_ALREADY_EXISTS` for "held".
- [ ] **Step 4: Run** the core run. Expected: PASS.
- [ ] **Step 5: Commit** `feat(core): config writer that merges into rawreel.json safely, and the settings claim`.

### Task 6: Sinks, start and exit order, settings owner, selftest isolation (GUI)

**Files:**
- Modify: `src/gui/app.h`, `src/gui/app.cpp`, `src/gui/main_frame.cpp` (it calls `logSink()`),
  `src/gui/log_panel.h`, `src/gui/log_panel.cpp`, `src/gui/selftest.cpp`

**Interfaces:**
- Consumes: Tasks 1–5.
- Produces (`App`):
  - `const std::shared_ptr<LogStore>& logStore() const noexcept` (replaces `logSink()`);
  - `LogFileSwitch& logFileSwitch() noexcept`;
  - `bool systemLogEnabled() const noexcept`, and `void useSystemLog(bool on)` (which calls
    `setSinkLevel(*system_sink, on ? Warn : Off)` and sets the flag);
  - `const std::filesystem::path& configPath() const noexcept`;
  - `const Config& config() const noexcept` (the values in effect for this run);
  - `bool ownsSettings() const noexcept`;
  - `std::filesystem::path logFolderInUse() const` (empty: no log file).
- `LogPanelSink` is gone. For this task only, the pane keeps its `wxTextCtrl` and its existing
  timer, which now calls `store->take(next_)` and appends the entries' lines. Task 7 removes both.

- [ ] **Step 1: Write the failing selftest steps** (smoke scenario):
  - `"settings owner"`:
    - `wxGetApp().ownsSettings()` is true;
    - `SettingsClaim::tryClaim(configPath())` from the step is null (the app holds it).
  - `"selftest config"`:
    - `configPath()` is inside `selftestConfigFolder()`, a folder
      `<temp>/rawreel-selftest-config-<pid>-<random>/`;
    - `logFolderInUse()` is its `logs/`;
    - `systemLogEnabled()` is false.
  - Every existing step that reads `logPanel().text()` to prove a line was logged (menus, Open URI
    cancel, format needed, the two-file drop, diagnostics) instead reads `logRecordsText` of the
    `store->entries()` whose sequence is at or above `store->next()` remembered at the step's
    start. This replaces the text-length comparisons.
- [ ] **Step 2: Run** the smoke selftest; see it fail.
  `cmake --build libs/_build/rawreel/ninja-macos --config Release --target rawreel_gui && DVV_REQUIRE_GPU=1 ctest --test-dir libs/_build/rawreel/ninja-macos -C Release -R rawreel.gui_selftest.smoke --output-on-failure`
- [ ] **Step 3: Implement** spec §5.3 "At start" and §7.1.

  `App::OnInit`, right after `CrashHandler::install` and before `setAppearance()`, so its line
  reaches the store:
  1. Add the store, the file switch (empty) and `SystemLogSink("rawreel")` at level `Off` to the
     Logger.
  2. A `--selftest` run without `--config` creates `<temp>/rawreel-selftest-config-<pid>-<random>/`
     (16 hex digits; created exclusively, never an existing path, 0700 on POSIX) and its `logs/`
     the same way. It writes the scratch `rawreel.json` with `saveConfigValues`
     (`log.file.folder` = `logs/`, `log.system.enabled` false), and uses it as the config
     argument.
  3. `loadConfig`.
  4. `useSystemLog(config.log.system.enabled)`.
  5. When `log.file.enabled`, open the log file:
     - the saved folder with `LastOnly`;
     - when it is empty, or when it fails, the default `userDataDir("rawreel")` with `WithParents`,
       logging the fallback warning first when a saved folder failed;
     - except with the selftest's own config: there a failed `logs/` means a warning and no log
       file, never the default folder;
     - then `replace(file, store->entries())`, one call.
  6. `settings_claim_ = SettingsClaim::tryClaim(configPath())`.

  If `OnInit` returns false after step 2, it removes the selftest config folder before returning
  (wx calls no `OnExit` then).

  `App::OnExit`, in this order:
  1. the selftest close-time check (Task 8 adds it);
  2. the existing exit-code and validation code;
  3. `dvv::Logger::instance().flush()`;
  4. `replace(nullptr)` on the switch, at the very end, so the session-shutdown lines still reach
     `rawreel.log`;
  5. a selftest run then removes its config folder (failure: one stderr line, no exit code
     change).

  `Session` creation still passes `config().io.fileReads`.
- [ ] **Step 4: Run** the smoke selftest. Expected: PASS. (`rawreel.gui_start.no_display` is
  Linux-only and runs in Task 12 only where available.)
- [ ] **Step 5: Commit** `feat(gui): sinks added once, config before the log file, settings owner, isolated selftest config`.

### Task 7: The timer-free Log list, two adapters (GUI)

**Files:**
- Create:
  - `src/gui/log_list.h` (a `wxPanel`-based interface);
  - `src/gui/log_list_dataview.cpp` (`wxDataViewCtrl`);
  - `src/gui/log_list_listctrl.cpp` (`wxListCtrl`; only `LVM_SETSELECTIONMARK` is under
    `#if defined(__WXMSW__)`).
- Modify: `src/gui/log_panel.h`, `src/gui/log_panel.cpp`, `src/gui/main_frame.h`,
  `src/gui/main_frame.cpp`, `src/gui/selftest.cpp`, `CMakeLists.txt` (both adapter files in
  `rawreel_gui` on every OS)

**Interfaces:**
- Consumes: `LogMirror`, `ListPlace`, `ListRows`, `rowsAfterBatch`, `logRowTime`, `logRowMessage`,
  `localTime` (Task 2); `LogStore::take` (Task 1).
- Produces:
  - `enum class LogListKind : std::uint8_t { ListCtrl, DataView };`
  - `LogListKind nativeLogListKind()`: `ListCtrl` on Windows, `DataView` elsewhere.
  - `class LogList : public wxPanel`, with:
    - `static LogList* create(wxWindow* parent, const LogMirror& mirror, LogListKind kind)`;
    - `virtual void rebuild()`;
    - `virtual void applyBatch(const ListRows& rows)` (count updated from the mirror, then the
      rows restored);
    - `[[nodiscard]] virtual ListPlace place() const`;
    - `[[nodiscard]] virtual std::vector<std::uint64_t> selectedSequences() const`;
    - `virtual void selectRows(std::span<const std::size_t> rows)` (selftests);
    - `virtual void scrollToTop(std::size_t row)` (selftests);
    - `virtual void selectAll()`;
    - `[[nodiscard]] virtual wxWindow* control() const` (the focusable child).
  - Columns, in order: "Time (local)", "Level", "Message", "Source". Cell callbacks
    (`OnGetItemText`, `GetValueByRow`) bounds-check the row against `mirror.size()` and return ""
    beyond it.
  - `LogPanel(wxWindow* parent, LogListKind kind = nativeLogListKind())`, with:
    - `void refresh(LogBatch batch, bool shown)`: takes `place()` only when shown, applies to the
      mirror, and calls `applyBatch(rowsAfterBatch(...))` only when shown;
    - `const LogMirror& mirror() const`;
    - `LogList& list()`;
    - `void recreateList(LogListKind kind)` (selftests: run the same steps on the other adapter).
  - `MainFrame::onLogChanged()` (private): `log_->refresh(store->take(mirror.next()), shown)`.

- [ ] **Step 1: Write the failing selftest steps** (the pane is still shown at start in this
  task):
  - `"log list fills"`: the list's row count equals `mirror().size()` and is > 0; row 0's Level
    cell is the level name of `mirror().at(0)`.
  - `"log list keeps its place"`, run twice, once per `LogListKind` via `recreateList`:
    - start:
      - log Info lines until `mirror().size() == 5000`;
      - `selectRows({10, 20})`;
      - `scrollToTop(5)`;
      - remember those sequences and `at(0).sequence`;
      - log 50 more Info lines;
    - passes when:
      - `at(0).sequence` moved forward by 50 or more;
      - `selectedSequences()` equals the two remembered sequences;
      - the top row's sequence is the remembered one.
- [ ] **Step 2: Run** the smoke selftest; see it fail.
- [ ] **Step 3: Implement** spec §3.2 and §4.1:
  - no `wxTimer`, and `LOG_PANEL_TICK_MS` is gone;
  - at the end of the `MainFrame` constructor (after the status bar exists), set
    `store->setNotify([this] { CallAfter([this] { onLogChanged(); }); })` and call
    `onLogChanged()` once;
  - `~MainFrame` calls `setNotify({})` first.

  The two adapters:
  - `wxListCtrl` with `wxLC_REPORT | wxLC_VIRTUAL`: `SetItemCount`, then the selection set from
    `ListRows`, plus `LVM_SETSELECTIONMARK` on Windows.
  - `wxDataViewCtrl` with a `wxDataViewVirtualListModel`: `Reset(size)`, then the selection,
    focus (`SetCurrentItem`) and top (`EnsureVisible`) set from `ListRows`.

  Rows are never deleted one by one.
- [ ] **Step 4: Run** the smoke selftest and `rawreel.panel_lab.report`. Expected: PASS.
- [ ] **Step 5: Commit** `feat(gui): Log pane as a virtual list (wxListCtrl/wxDataViewCtrl), fed without a timer`.

### Task 8: Hidden by default, View → Log, the notice, saving at close (GUI + core command)

**Files:**
- Modify: `src/core/commands.h`, `src/core/commands.cpp`, `tests/test_commands.cpp`,
  `src/gui/main_frame.h`, `src/gui/main_frame.cpp`, `src/gui/app.h`, `src/gui/app.cpp`,
  `src/gui/selftest.h`, `src/gui/selftest.cpp`

**Interfaces:**
- Produces:
  - `Command::LogPane` after `Command::Reopen`: id `"log_pane"`, menu `"View"`, label `"Log"`
    (`"Show Log"`/`"Hide Log"` on macOS, set by the GUI), help `"Show or hide the Log pane"`, no
    key, `ACC`.
  - `CommandInfo` gains a last member, `bool checkable = false` (true only for `LogPane`, and
    ignored on macOS).
  - `MainFrame` members:
    - `void setLogShown(bool shown)`. Showing it runs `onLogChanged()`, then `list().rebuild()`,
      then `seen_ = <current counts>`.
    - `[[nodiscard]] bool logShown() const`.
    - `[[nodiscard]] wxString logNotice() const` (status field 4; `status_texts_` grows to 5).
    - The private `LogCounts seen_` starts at zero and is set to the counts on every refresh
      while shown. The notice is `logNoticeText(c.errors - seen_.errors, c.warnings -
      seen_.warnings)`.
  - `App` members:
    - `dvv::Result<void> saveSettings(std::span<const ConfigValue> values, bool closing)`:
      - it fails with "not the settings owner" when `!ownsSettings()`;
      - otherwise it calls `saveConfigValues(configPath(), values)`;
      - on failure it logs `config '<path>' not saved: <reason>`, and the first failure of the
        run also shows a message through `showModal(wxMessageDialog&)`, unless `closing`.
    - `std::optional<bool> savedLogPane()`: set by the close save.
- [ ] **Step 1: Write the failing tests:**
  - `test_commands.cpp`:
    - `COMMAND_COUNT == 19`;
    - `"commands: View -> Log"`: the menu, id, label, `checkable`, and no key;
    - no other command is checkable;
    - `LogPane` is in the existing no-key list.
  - Selftest steps:
    - `panesStep` passes when the Log pane is hidden.
    - `"log notice"`: `setLogShown(true)`, `setLogShown(false)`, then one `DVV_LOG_WARN` → passes
      when `logNotice()` contains "warning" and ends with "in the log".
    - `"log pane shows"`: `execute(Command::LogPane)` → passes when:
      - the pane is shown;
      - after `GetMenuBar()->UpdateMenus()`, the item is checked (Windows and Linux) or titled
        "Hide Log" (macOS);
      - the notice is empty;
      - the list's row count equals `mirror().size()` (rebuilt on show);
      - the player panel is at least its minimum size.
    - `"log pane close button"`: remember `aui().GetPane("log").rect.height`, then
      `aui().ClosePane(...)` and `Update()` → passes when the pane is hidden and the item is
      unchecked or titled "Show Log".
    - `"log pane height kept"`: show again → the pane's height equals the remembered one.
    - `"log pane after a burst"`: hidden, 5010 Info lines, then shown → passes when:
      - the list has 5000 rows;
      - the last row's message is "selftest burst 5009";
      - no row's message is "selftest burst 9".
    - Task 7's steps now show the pane first.
    - `diagnosticsStep` also requires the pane to be shown.
    - The smoke scenario's last step shows the pane, so the close save writes `true`.
- [ ] **Step 2: Run, see it fail** (the core run, then the smoke selftest).
- [ ] **Step 3: Implement** spec §2:
  - the pane has `.CloseButton(true)`, and is shown at start iff `config().gui.showLogPane` and the
    run is not a selftest;
  - the check item or title follows the pane through `wxEVT_UPDATE_UI`;
  - the fifth status field is as wide as `"99 errors, 99 warnings in the log"` plus `FromDIP(24)`,
    with a left-up hit test that shows the pane;
  - `Diagnostics` then `setLogShown(true)`;
  - the pane's height is kept across hide and show (its pane info `BestSize` is set from the last
    rect before hiding).

  At close (`onClose`): when `logShown() != config().gui.showLogPane`, call
  `saveSettings({{"gui.showLogPane", logShown()}}, /*closing=*/true)`, and on success set
  `savedLogPane`.

  `OnExit` (step 1 of Task 6's order), selftest runs only: when `savedLogPane()` is set, re-read
  the scratch file. A mismatch prints one stderr line and records exit code 1 through the same
  path the existing failures use, before the validation block. Nothing is checked when nothing was
  saved (the lost and player scenarios).
- [ ] **Step 4: Run** the core run and the smoke selftest. Expected: PASS.
- [ ] **Step 5: Commit** `feat(gui): Log pane hidden by default, View -> Log, status bar notice, state saved at close`.

### Task 9: Edit menu, keys, copy and save (GUI + core commands)

**Files:**
- Modify: `src/core/commands.h`, `src/core/commands.cpp`, `tests/test_commands.cpp`,
  `src/gui/main_frame.h`, `src/gui/main_frame.cpp`, `src/gui/log_panel.cpp`,
  `src/gui/selftest.h`, `src/gui/selftest.cpp`
- Create: `src/gui/edit_dispatch.h`, `src/gui/edit_dispatch.cpp`

**Interfaces:**
- Produces:
  - `Command::SaveLog` (`"save_log"`, File, `"Save Log..."`, `"Save the lines of the Log pane to a
    file"`, no key) and `Command::SaveSelectedLines` (`"save_selected_lines"`, File, `"Save
    Selected Lines..."`, `"Save the selected lines of the Log pane to a file"`, no key). Both go
    after `Command::OpenUri`, so File reads Open…, Open URI…, Save Log…, Save Selected Lines…,
    Close stream, Quit. `COMMAND_COUNT` becomes 21.
  - `commandForKey(const KeyChord&, bool focus_keeps_keys)`: the parameter is renamed, with the
    same behaviour.
  - `MainFrame` members:
    - `wxWindow* focusedWindow() const` returns the override when set, else `wxWindow::FindFocus()`;
    - `void setFocusForTest(wxWindow* window)` (null clears it).

    Every focus decision below uses `focusedWindow()`.
  - `edit_dispatch.h`:
    - `void buildEditMenu(wxMenuBar& bar)`: inserted at index 1, with the items and per-OS keys of
      spec §4.2;
    - `bool runEdit(int id, wxWindow* focus, LogPanel& log)` and
      `bool canEdit(int id, wxWindow* focus, const LogPanel& log)`: they call the action directly
      (spec §4.2);
    - `bool focusKeepsKeys(wxWindow* focus, const wxWindow* log_pane)`: an editable `wxTextEntry`,
      or a window whose parent chain contains `log_pane`.

    The frame binds `wxEVT_MENU` and `wxEVT_UPDATE_UI` for `wxID_UNDO`, `wxID_REDO`, `wxID_CUT`,
    `wxID_COPY`, `wxID_PASTE`, `wxID_CLEAR` and `wxID_SELECTALL`. On macOS these events arrive
    only when the native responder chain did not handle the item.
  - `DialogHooks` gains:
    - `dvv::MoveOnlyFunction<void(wxFileDialog&)> saveLog`;
    - `dvv::MoveOnlyFunction<void(std::string)> clipboard`;
    - `dvv::MoveOnlyFunction<void(wxMessageDialog&)> message`.

    New functions:
    - `int showModal(wxFileDialog&)` and `int showModal(wxMessageDialog&)`: never shown in a
      selftest (see Global Constraints);
    - `void setClipboardText(std::string)`: to the hook in a selftest, else to `wxTheClipboard`.
- [ ] **Step 1: Write the failing tests:**
  - `test_commands.cpp`: the two entries, the File order above, and `COMMAND_COUNT == 21`.
  - Selftest steps (the Log pane shown):
    - `menusStep` expects File, Edit, Playback, View, Help.
    - `"log list keeps keys"`: `setFocusForTest(list.control())`, then a Space char-hook event →
      playback state unchanged.
    - `"log copy"`: `selectAll()`, remember the mirror's first and last sequence, run `wxID_COPY`
      → the clipboard hook's text equals `logRecordsText(mirror().range(first, last))`.
    - `"info copy"`: `setFocusForTest(<the Info pane's text control>)`, run `wxID_SELECTALL` and
      `wxID_COPY` → the hook gets the Info text.
    - `"save log"`: a `saveLog` hook sets the path `<selftest config folder>/saved.txt` → the file
      equals `logRecordsText` of the mirror range remembered when the command ran.
    - `"save log remembers the folder"`: a second save → the hook sees `GetDirectory()` equal to
      the selftest config folder.
    - `"save selected lines"`: two rows selected → the file holds exactly those two records.
    - `"save over the live log"`: the hook chooses `logFolderInUse()/rawreel.log` → the `message`
      hook fires with text naming `rawreel.log`, and the log file's size did not shrink.
    - `"save log write failure"`: the hook chooses a path in a missing folder → the `message`
      hook fires, and a warning record with "not saved" or the path appears in the store.
- [ ] **Step 2: Run, see it fail.**
- [ ] **Step 3: Implement** spec §4.2–§4.4:
  - the list's context menu;
  - the snapshot (a sequence range) taken before the dialog;
  - the macOS file type and the Windows/Linux filters;
  - `equivalent()` against the live log and its backups;
  - `wxTheClipboard->Flush()` in `OnExit` on GTK;
  - `onCharHook` uses `focusKeepsKeys(focusedWindow(), log_)`.

  **macOS Undo/Redo** (spec §4.2): find out whether wx's forwarding reaches the text field's undo
  manager. If not, add a Cocoa helper in `native_controls_cocoa.mm` that sends `undo:`/`redo:` to
  the key window's first responder, and use it for `wxID_UNDO`/`wxID_REDO` on macOS. Record which
  in the task report.
- [ ] **Step 4: Run** the core run and the smoke selftest. Expected: PASS.
- [ ] **Step 5: Commit** `feat(gui): Edit menu, Log list copy and save, the list keeps its keys`.

### Task 10: The Settings window (GUI)

**Files:**
- Create: `src/gui/settings.h`, `src/gui/settings.cpp`
- Modify:
  - `src/core/commands.h`, `src/core/commands.cpp`, `tests/test_commands.cpp`;
  - `src/core/session.h`, `src/core/session.cpp`, `tests/test_session.cpp`;
  - `src/gui/app.h`, `src/gui/app.cpp`, `src/gui/main_frame.h`, `src/gui/main_frame.cpp`;
  - `src/gui/native_controls.h`, `src/gui/native_controls_cocoa.mm`,
    `src/gui/native_controls_msw.cpp`, `src/gui/native_controls_gtk.cpp`,
    `src/gui/native_controls_stub.cpp`;
  - `src/gui/selftest.h`, `src/gui/selftest.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `Command::Settings`:
    - id `"settings"`, menu File, label `"Settings..."` (`"Preferences..."` on Linux), help
      `"Change the settings of rawreel"`, `ctrlKey(',')`, `ACC`;
    - placed after `CloseStreams`, before `Quit`;
    - `menuId(Command::Settings) == wxID_PREFERENCES`, and `commandOfMenuId(wxID_PREFERENCES) ==
      Settings`, with the id bound to `onMenu` like About and Quit;
    - on Linux, `buildMenus` builds the Edit menu first and appends the item to its end after a
      separator;
    - `COMMAND_COUNT` becomes 22.
  - `Session` members: `void setFileReads(dvv::platform::FileReadMode mode) noexcept` and
    `[[nodiscard]] dvv::platform::FileReadMode fileReads() const noexcept`, an atomic read at every
    open.
  - `App` members:
    - `[[nodiscard]] dvv::Result<std::shared_ptr<dvv::RotatingFileSink>> prepareLogFile(const
      std::filesystem::path& folder)` (`LastOnly`; the default folder `WithParents`);
    - `void useLogFile(std::shared_ptr<dvv::RotatingFileSink> sink, std::filesystem::path saved)`:
      logs the "moves to" line, then `replace`;
    - `void stopLogFile()`;
    - `void useFileReads(FileReadMode)`;
    - all saves go through `saveSettings` (Task 8).
  - `class SettingsWindow`, owned by `MainFrame` as a `std::unique_ptr`:
    - `void show(wxWindow* parent)`, `void dismiss()`;
    - `LogSettingsPage& logPage()` and `FilesSettingsPage& filesPage()` (null-safe: they are
      created on demand);
    - actions on the Log page: `chooseFolder()`, `useDefault()`, `[[nodiscard]] bool
      useDefaultEnabled() const`, `[[nodiscard]] bool revealEnabled() const`,
      `[[nodiscard]] wxString folderText() const`, `[[nodiscard]] wxString folderHint() const`,
      `setSystemLog(bool)`;
    - actions on the Files page: `setFileReads(FileReadMode)`,
      `[[nodiscard]] wxString configPathText() const`;
    - `void pressOk()` (Windows: sends `wxEVT_BUTTON` `wxID_OK` to the dialog, which runs
      `Validate()` and `TransferDataFromWindow()`; elsewhere it does nothing, because changes
      apply at once).
  - `wxBitmapBundle settingsPageIcon(std::string_view sf_symbol)` (native_controls; empty off
    macOS).
  - `void revealInFileManager(const std::filesystem::path& file_or_folder)` (native_controls:
    `activateFileViewerSelectingURLs`, `explorer /select,`, and `wxLaunchDefaultApplication` on
    the folder on GTK).
  - `DialogHooks` gains:
    - `dvv::MoveOnlyFunction<void(wxDirDialog&)> chooseFolder` (never shown in a selftest);
    - `dvv::MoveOnlyFunction<void(std::filesystem::path)> reveal`;
    - `dvv::MoveOnlyFunction<void(SettingsWindow&)> settings`. On Windows this hook is queued
      with `CallAfter` before the modal `Show()` and runs inside its loop; the step's action ends
      with `pressOk()`.
- [ ] **Step 1: Write the failing tests:**
  - `test_commands.cpp`: Settings' entry (the Linux label under `#if defined(__linux__)`), and
    `COMMAND_COUNT == 22`.
  - `test_session.cpp`, `"session: setFileReads changes the mode of later opens"`:
    - `SessionFixture(Buffered)`, open the existing test's file → reader `"buffered"`;
    - `setFileReads(Unbuffered)` → `fileReads() == Unbuffered`;
    - close and open again → the reader is `"unbuffered"`, or, where the temporary volume refuses
      unbuffered reads, the open fails with an error that names unbuffered reads. The test
      accepts either and checks it is not `"buffered"`.
  - Selftest steps. Each opens Settings by sending `wxEVT_MENU` with `wxID_PREFERENCES` to the
    frame, does one action through the `settings` hook, and calls `pressOk()`:
    - `"settings opens"`:
      - both pages exist;
      - `configPathText()` shows `configPath()`;
      - `useDefaultEnabled()` is false;
      - `folderText()` shows the selftest `logs/` folder.
    - `"settings log folder"`: `chooseFolder()` with a `chooseFolder` hook set to `logs2/` of the
      selftest config folder → passes when:
      - `logFolderInUse()` is `logs2`;
      - `logs2/rawreel.log` exists;
      - the old file contains a line "log file moves to" naming `logs2`;
      - the scratch file has `log.file.folder`;
      - `useDefaultEnabled()` is true.
    - `"settings folder failure"`: the hook returns a folder whose parent does not exist → passes
      when:
      - the `message` hook fires;
      - `logFolderInUse()` is unchanged;
      - `folderText()` is unchanged.
    - `"settings reveal"`: the `reveal` hook gets `logFolderInUse()/rawreel.log`;
      `revealEnabled()` is true.
    - `"settings system log"`: on, then off → passes when `systemLogEnabled()` is true, then
      false, and the scratch file has `log.system.enabled` false.
    - `"settings file reads"`: Buffered → passes when `session().fileReads() == Buffered` and the
      scratch file has `io.fileReads` `"buffered"`.
    - `"settings close key"` (macOS only): with `setFocusForTest(<a control in the Settings
      window>)`, `execute(Command::CloseStreams)` → passes when the Settings window is closed and
      the stream is still open.
- [ ] **Step 2: Run, see it fail** (the core run, `rawreel.core_session`, the smoke selftest).
- [ ] **Step 3: Implement** spec §5:
  - the per-OS titles;
  - the lifecycle:
    - `dismiss()` in `MainFrame::onClose`, before `shutdownSession()`;
    - page handlers do nothing while the frame is closing;
    - `TransferDataToWindow`, and a reload on `wxEVT_SHOW` on GTK;
  - Windows `Validate()` (prepare a changed folder) and `TransferDataFromWindow()` (apply all, then
    one `saveSettings`);
  - the second-copy line on each page when `!ownsSettings()`;
  - the "Not available" line;
  - the default folder as the field's hint (`folderHint()`);
  - Show in Finder/Explorer disabled when the folder does not exist;
  - on macOS and Linux, the field keeps its old value after a failure;
  - Use Default with `equivalent()`;
  - SF Symbols `doc.text` and `folder`;
  - macOS ⌘W: in the CloseStreams path, if `wxGetTopLevelParent(focusedWindow())` is not the main
    frame, close that window instead.
- [ ] **Step 4: Run** the core run, `rawreel.core_session` and the smoke selftest. Expected: PASS.
- [ ] **Step 5: Commit** `feat(gui): Settings window for the log file, the system log and file reads`.

### Task 11: README

**Files:** Modify `README.md` (the rawreel-gui section near "an Info pane, a Log pane", and
"## Configuration").

- [ ] **Step 1:** Write the README text listed in spec §8, as facts:
  - the location rule;
  - the key table of spec §6.2;
  - the writer's behaviour, the "not saved" warning, the bad-value rule for both executables and
    the newer-value fact;
  - the Settings window per OS;
  - the second-copy rule;
  - the Edit menu;
  - Save Log / Save Selected Lines;
  - the notice.
- [ ] **Step 2:** Check that no example has a machine path (a home folder on macOS, Linux or
  Windows) or a real name → nothing new.
- [ ] **Step 3: Commit** `docs: README for the Log pane, the Settings window and the config file`.

### Task 12: Full build, full suite, manual check, CI

- [ ] **Step 1: The full build:** `cmake --build libs/_build/rawreel/ninja-macos --config Release`.
  Expected: no warnings.
- [ ] **Step 2: The full suite**, serially:
  `DVV_REQUIRE_GPU=1 DVV_REQUIRE_FFMPEG=1 ctest --test-dir libs/_build/rawreel/ninja-macos -C Release -j1 --output-on-failure`
  Expected: every test passes, and the developer's `~/Library/Application Support/rawreel/` is
  untouched (its modification time is the same before and after).
- [ ] **Step 3: Check by hand** on macOS (the run command with `DVV_FFMPEG_DIR`, `VK_DRIVER_FILES`
  and `VK_ADD_LAYER_PATH`):
  - the pane is hidden at start, and View → Show Log / Hide Log works;
  - row selection with Shift and ⌘, ⌘C, ⌘A, and the context menu;
  - Save Log…;
  - ⌘, opens Settings; changing the folder and Show in Finder;
  - ⌘V, ⌘Z and ⇧⌘Z in Open URI…;
  - quit and restart: the pane state and settings are kept in
    `~/Library/Application Support/rawreel/rawreel.json`;
  - a second copy (`open -n`) shows the "Another rawreel window" line.
- [ ] **Step 4: Ask the user** before pushing the branch. Windows and Linux code builds only in CI.
