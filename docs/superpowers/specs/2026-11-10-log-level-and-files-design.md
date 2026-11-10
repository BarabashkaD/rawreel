# Log Level, Level Markers, Source and Log Files: Design Addendum

Status: revision 2, 2026-11-10.
- It folds in a builder check and an optimistic and a pessimistic review.
- It extends `docs/superpowers/specs/2026-11-10-log-pane-and-settings-design.md` (revision 2,
  cited as "base §N"). Where they differ, this addendum wins.
- It comes from the user's first hand test of branch `feat/log-pane-settings`. Every decision here
  is the user's unless it is marked as a call (§9).
- It needs dvv-base **0.4.0** (§3, §4, §5): new public API, and 0.3.0 is released.

## 1. Intent

The first hand test raised three points:
- **Log level.** There is no way to choose the log level: the base design listed it as a
  non-goal.
- **Source.** The Source column, and the `[file.cpp:42]` part of every saved or written line, is
  noise for a user. Developers need it only when debugging.
- **File name.** The log file is always `rawreel.log`, while Save Log… names its file with a
  timestamp. The log file should be named when it is created.

### Goals

- G1. A default log level chosen in Settings, saved in `rawreel.json`, in effect at every start.
- G2. A level for the current run only, chosen from the Log list's right-click menu.
- G3. Every level change leaves a marker line that is easy to spot, in the Log pane, the log file
  and the system log (when it is on).
- G4. No source location in the pane, the log file, copy or save, unless a JSON-only key asks
  for it.
- G5. Log files named by their creation time, split by size, never deleted; a warning when they
  grow large.

### Non-goals

- Showing lines that were below the level when they were logged. They were never recorded
  (§2.4).
- Per-sink levels in the GUI.
- Deleting or compressing old log files.
- A level for the system log (it stays at warnings and errors, base §5.2).
- Any change to rawreel-cli's logging.

## 2. The log level

### 2.1 What the level controls

- **The Logger itself is set to Trace once at start** (`Logger::setLevel(Trace)`, base §5.3
  step 1), so it never filters on its own. Its gate is the lowest sink level, so a Debug line is
  only formatted when some sink wants it.
- **One active level drives three sinks** through `Logger::setSinkLevel`: the file switch, the log
  store and the stderr sink.
  - The stderr sink is created in `gui/main.cpp`. `main()` hands it to App, e.g. through
    `App::setArguments(arguments, stderr_sink)`, so App can set its level.
- **The system log sink stays at Warn**, or at Off when the user turned it off.
- **Order of a change:** set the sink levels, then write the marker (§3), both on the GUI thread.
  Another thread's line can land just before or just after the marker; that is accepted.

The values:

| Shown | `log.level` value (any letter case accepted) | dvv level |
|---|---|---|
| Errors | `error` | Error |
| Warnings | `warn` | Warn |
| Info | `info` | Info |
| Debug | `debug` | Debug |
| Trace | `trace` | Trace |

The default is Info. rawreel writes the value in lower case.

### 2.2 Where it is chosen

- **Settings → Log page → Level:** a choice control, first on the page.
  - It shows the **saved** `log.level`, not a run-only level.
  - Changing it saves `log.level`, sets the active level, and ends any run-only level. It applies
    at once on macOS and Linux. On Windows it applies on OK, and only when the control changed:
    OK with the control untouched leaves a run-only level as it is.
- **Log list right-click menu → Level ▸:** a submenu with the five levels as radio items. The
  active level is checked.
  - Choosing one sets the active level **for this run only**. Nothing is saved, and the next start
    uses `log.level` again.
  - The submenu sits after Select All, between separators.
  - A selftest cannot drive a popup menu, which is modal. So `LogPanel::setRunLevel(LogLevel)` is
    the call the menu uses, and a selftest calls it directly. A hook reports the built menu's
    items and which one is checked.
- **A second copy** (not the settings owner, base §6.3) applies a Settings change without saving
  it.
- **At start:** the active level is `log.level`, read with the config before the log file opens.

### 2.3 Lines logged before the level is known

The store collects lines from before the config is read (base §5.3). Those below the active level
are left out when they are written into the first log file. The pane shows them, because the
store took them before the level was set.

### 2.4 Lines below the level are lost

A line below every sink's level is dropped at the call site, and each sink drops lines below its
own level. A dropped line is never stored. So lowering the level (Info → Debug) shows Debug lines
from that moment on, never earlier ones. The README states this as a fact.

## 3. The level marker

### 3.1 The line

At every change of the active level, and once at start, rawreel writes one marker line. The
message text, padded so it stands out:

| When | Text |
|---|---|
| start, saved level | `===== log level: Info (settings) =====` |
| start, no saved level (key missing, or config rejected) | `===== log level: Info (default) =====` |
| Settings change | `===== log level changed: Info -> Debug (settings) =====` |
| Settings change in a second copy | `===== log level changed: Info -> Debug (this window only) =====` |
| menu change | `===== log level changed: Info -> Debug (this run, Log pane menu) =====` |

- The "from" level is always the active level before the change.
- The arrow is ASCII `->`, because a Windows console can garble `→`.
- A change to the level already active writes nothing.

### 3.2 `Logger::mark` (dvv-base 0.4.0)

```cpp
// Delivers `text` as one record of level Info, flagged as a mark, to every sink whose level is not
// Off, ignoring the logger's level and the sinks' thresholds. In order with the other records:
// synchronous, under the same lock; asynchronous, through the queue with the priority of Warn, so a
// full ring drops it only when it would drop a warning. Does nothing without sinks. Records the
// call site like a log macro (a defaulted std::source_location argument).
void Logger::mark(std::string_view text,
                  std::source_location where = std::source_location::current());
```

- **`LogRecord` gets a last member, `bool mark = false`.** It is the last member because
  `log.cpp` builds `LogRecord` with positional aggregate initialisation.
- **Asynchronous mode:**
  - the ring's record header gets a flag `LOG_RECORD_MARK` next to the existing flag bit;
  - `handleRecord`'s acceptance test and `writeToSinks`/`emitLine` honour it.
- **Crash snapshot:**
  - bit 1 of the flags byte is documented as "mark";
  - `decodeCrashSnapshot` passes it through and decodes a mark as an Info line;
  - the format magic stays `DVVLOGS1` (old decoders ignore the bit).
- **A sink sees a mark like any record**, plus the flag:
  - `RotatingFileSink` and `SplitFileSink` flush at once on a mark, as on Warn, so a marker is not
    lost in a buffer;
  - `SystemLogSink` writes a mark at the OS's default/notice level: `OS_LOG_TYPE_DEFAULT` on macOS,
    `LOG_NOTICE` on Linux, `OutputDebugStringW` on Windows.
- **rawreel's store never counts a mark** in the status bar notice.
- **dvv-base tests:**
  - a mark reaches a sink at Error and is not delivered to a sink at Off;
  - a plain Info record still does not reach a sink at Warn;
  - order between records is kept in both modes;
  - in asynchronous mode a mark survives a ring that is 3/4 full;
  - a snapshot keeps the mark bit and decodes it.
- **Documented:** in asynchronous mode, a sink's level is read when the queue is drained, so a
  level change also applies to records queued just before it.

## 4. Source location

- **The Log list's Source column is shown only when `log.source` is true.** By default the list
  has Time (local), Level and Message.
- **The log file, copy and save leave out the source by default.** A line reads
  `2026-11-10T13:55:02.114Z INFO  message`, without ` [file.cpp:42]`.
- **JSON-only key `log.source`:**
  - boolean, default `false`, no GUI control;
  - when true, the source comes back in the file, in copy and save, and as the Source column;
  - read at start only, never written by rawreel.
- **dvv-base 0.4.0 gets `formatLogRecord(const LogRecord&, bool with_source)`.** The existing
  one-argument form keeps `with_source = true`, so other dvv users see no change.
- **One line per record, formatted once:**
  - `LogEntry` keeps the record's `std::source_location where` (its strings are static), its
    level, time, message and `mark` flag.
  - It also keeps one `line`, formatted by the store with `log.source`'s value.
  - The file switch writes live records with the same flag (one `formatLogRecord` call).
  - The backfill writes `entry.line` unchanged, so early lines keep their real source.
  - Copy and save use `entry.line`.
  - The `source` string field goes away; the Source column formats `where` when shown.
- **The system log and stderr keep their present format.**

## 5. Log files: named at creation, split by size, never deleted

### 5.1 Names and choosing the file

- **The name** is the creation time in local time: `rawreel-YYYYMMDD-HHMMSS.log`, e.g.
  `rawreel-20261110-153126.log`.
  - A clash gets a suffix `-2`, `-3`, … before `.log`.
  - Files are created exclusively (C++23 `std::ios::noreplace`, i.e. `O_EXCL`), so two processes
    never get the same new file. A clash moves on to the next suffix.
- **The newest file** is found by parsing names strictly as `rawreel-YYYYMMDD-HHMMSS(-N)?.log`.
  - It is compared by (timestamp, N), with no suffix counting as 1.
  - A name that does not match is never touched (a user's `rawreel-old.log` is safe).
  - Names are in local time, so a clock set back (DST fall-back, a time-zone change) can make
    "newest" an older file for a while. The README states this. Log lines stay in UTC.
- **At start, or when the log folder changes:**
  - **The settings owner** appends to the newest file when it is below the limit, else creates a
    new one.
  - **A second copy** always creates its own new file. Two processes then never write the same
    file, and their buffered lines cannot interleave.

### 5.2 Splitting, failures and the header

- **The size limit** is 4 MiB (`LOG_FILE_BYTES`). A write that would take the current file over it
  first starts a new file, so a line is never split across files.
  - An empty file is never split, so one line longer than the limit cannot start new files
    forever.
- **The header.** Every file this sink creates starts with an owner-set header line. rawreel sets
  it to the marker style with the active level and the previous file:
  - e.g. `===== log file: level Info, continues rawreel-20261110-140210.log =====`
  - or `===== log file: level Info =====` for the first one.
  - The header is set under the sink's mutex through `SplitFileSink::setHeader(std::string)`. The
    sink formats it as an Info line itself and never calls the Logger.
  - An appended file gets no header; the start marker (§3.1) follows in it.
- **The "log file moves to" line** names the new file's full path (`currentFile()` of the opened
  sink).
- **After every open, the new file's first lines** are the header, then
  `log file: '<full path>'`, then the start marker or the change marker.
- **Failures never block logging threads for long:**
  - **Creating a new file fails** (folder gone, disk full, a stalled network share): the current
    file stays in use, and the limit is exceeded rather than losing lines. The next attempt waits
    until another `max_bytes / 4` has been written. This is the same back-off `RotatingFileSink`
    uses, so a stalled folder is not retried on every line.
  - **Appending to the newest file fails at open** (e.g. another program holds it without write
    sharing on Windows): a new file is created instead.
  - **A write to the current file fails:** the file is closed and a new file is tried with the
    same back-off. While no file is open, lines are dropped, as `RotatingFileSink` does today.
  - Failure notes go to stderr only, never to the Logger. In the GUI, the next start's fallback
    warning (base §5.3) and the size check below are what the user sees.
- **Nothing is deleted.** There are no `.1`/`.2` backups and no renaming. The old `rawreel.log`
  and its backups from earlier versions are left as they are, and rawreel no longer writes them.

### 5.3 The 1 GiB warning

- `SplitFileSink::open()` already scans the folder. It also sums the sizes of the matching
  `rawreel-*.log` files, and `totalBytes()` reports the sum.
- At start, and at each split, rawreel compares the sum with **1 GiB**. The first time in a run
  that it is over, it logs one warning:
  `log files in '<folder>' use <N> MiB (over 1 GiB); rawreel never deletes them`.
  - It shows in the status bar notice like any warning.
  - It is not repeated in that run.
- The check is done by rawreel, after the split is reported back to the GUI thread, never inside
  the sink under the Logger lock.

### 5.4 `SplitFileSink` (dvv-base 0.4.0)

```cpp
// Writes <folder>/<prefix>-YYYYMMDD-HHMMSS[-N].log (local creation time, created exclusively).
// open(append_newest = true) appends to the newest strictly matching file below max_bytes, else
// creates a new file; open(false) always creates one. A write that would pass max_bytes starts a
// new file first, unless the current file is empty. Creation failures back off by max_bytes / 4.
// Never renames or deletes a file. Flushing: Warn and above and marks at once; Info and below
// buffered until the buffer fills, flush(), batchEnd() in asynchronous mode, or destruction.
class SplitFileSink final : public ILogSink {
public:
    [[nodiscard]] static Result<std::shared_ptr<SplitFileSink>> open(
        std::filesystem::path folder, std::string prefix, std::uintmax_t max_bytes,
        bool append_newest);
    void setHeader(std::string header);                       // written first in each new file
    [[nodiscard]] std::filesystem::path currentFile() const;  // thread-safe
    [[nodiscard]] std::uintmax_t totalBytes() const;          // matching files, at open + since
    [[nodiscard]] std::uint64_t splits() const;               // files started since open
    void write(const LogRecord& record, std::string_view line) override;
    void batchEnd() override;
    void flush() override;
};
```

- **`RotatingFileSink` stays unchanged** for other dvv users.
- **dvv-base tests:**
  - a new name per creation, the clash suffix, and exclusive creation (a pre-created name is
    skipped);
  - append to the newest file under the limit, and `open(false)` always creating a new file;
  - the newest-file order, including `-2` vs no suffix and `-10` vs `-2`;
  - non-matching names ignored;
  - a split at the limit with no line divided, and no split of an empty file;
  - the header in each new file but not in an appended one;
  - back-off after a failed creation;
  - nothing renamed or deleted;
  - `totalBytes()` and `splits()`.

### 5.5 What changes in rawreel

- **`LogFileSwitch` holds a `SplitFileSink`** (prefix `rawreel`, `append_newest` = owns the
  settings). `LOG_FILE_NAME` and `LOG_FILE_BACKUPS` go away.
- **Show in Finder/Explorer** selects `currentFile()`.
- **Save Log refusal (base §4.4):** a save target is refused when:
  - it is `equivalent` to `currentFile()`; or
  - it is in the log folder in use and its name strictly matches the pattern of §5.1
    (case-insensitive on Windows and macOS).
  - The refusal message names the current file.
- **The selftests' scratch `logs/` folder** gets timestamped files the same way. The steps that
  read "the log file" call `Logger::flush()` first and then use `currentFile()`.
- **The config info line** (config.cpp) adds `log.level <value>`.
- **README:**
  - the bug-report section names "the newest `rawreel-*.log` in the log folder";
  - the log file section states naming, splitting, never deleting, the 1 GiB warning, the
    local-time names and lines below the level being lost.

## 6. Config keys

| Key | Values | Default | Used by | Written by rawreel |
|---|---|---|---|---|
| `log.level` | `error`, `warn`, `info`, `debug`, `trace` (any case) | `info` | rawreel-gui | yes (Settings) |
| `log.source` | `true`, `false` | `false` | rawreel-gui | no (JSON only) |

- Bad values follow base §6.2: one warning, and the defaults for the whole file. Example message:
  `log.level: expected "error", "warn", "info", "debug" or "trace", not "verbose"`.
- The writer's "refuse a file the loader would reject" rule (base §6.4) covers both keys.

## 7. dvv-base 0.4.0 release

Contents:
- `Logger::mark`;
- `LogRecord::mark`;
- the ring and snapshot mark bit;
- `formatLogRecord(record, with_source)`;
- `SplitFileSink`.

Release steps:
- CHANGELOG entries for each;
- the README "Logging" section;
- released like 0.3.0: a dvv-base PR, the user merges, tag `v0.4.0`;
- rawreel moves its submodule and `dvv-deps.txt` to it, and its CI runs with `test_libs`.

## 8. Tests in rawreel

**Core (wx-free):**
- parse of both keys, any case for `log.level`, and the bad values;
- level name ↔ value mapping;
- the marker texts of §3.1;
- the store's `line` with and without source;
- marks not counted in the notice;
- the backfill keeping each line's real source;
- the 1 GiB message text and its once-per-run rule.

**Selftests (smoke):**
- Settings → Level to Debug: a `DVV_LOG_DEBUG` line then reaches the store and the current file
  (this proves the Logger gate), and the scratch file has `log.level`.
- `setRunLevel(Trace)`: Trace reaches them; the scratch file is unchanged; the menu hook shows
  Trace checked.
- Each change writes one marker, found in the store and in the current file. The start marker is
  present. The header is the file's first line.
- A level change with the system log Off reaches the store and the file, and the system sink's
  level stays Off.
- No Source column by default.
- Saved text matches the format without a source: `^<time> <LEVEL> +<message>$`, not a check for
  ` [`, which Vulkan's own `[VUID-…]` text would fail.
- A separate CTest entry runs the smoke selftest with a scratch config that has
  `log.source: true`: the column shows, and saved text and the file end in ` [file:line]`.
- The scratch `logs/` file is named `rawreel-YYYYMMDD-HHMMSS.log`.
- Save Log refuses a matching name in the log folder.

## 9. Calls made by the designer (check these)

- D1. One active level for the file, the pane and stderr; the Logger itself at Trace; the system
  log keeps Warn or Off.
- D2. A marker at start and a header in every new file, so every file shows its level.
- D3. Padding `=====`, the ASCII `->`; "(this run, Log pane menu)", "(this window only)",
  "(default)".
- D4. The Level choice first on the Settings Log page; Level ▸ after Select All.
- D5. `log.source` covers the file, copy, save and the Source column; the system log and stderr
  keep their format.
- D6. Local-time names; a clash gets `-2`, `-3`, …; strict name parsing.
- D7. A failed creation keeps the current file (the limit is exceeded, no lines lost), with
  back-off.
- D8. The old `rawreel.log` files are left alone.
- D9. dvv-base 0.4.0.
- D10. Only the settings owner appends to the newest file; a second copy starts its own.
- D11. Marks get Warn's queue priority, skip sinks at Off, are flushed at once, and are not
  counted as warnings.
- D12. `log.level` accepts any letter case.
- D13. The 1 GiB warning (user's choice of limit): once per run, at start or at a split.
