# dvv-base logger: a queued logger with deferred formatting

Status: design, revision 2 (after an optimistic and a pessimistic review, 2026-11-09). Repository:
dvv-base (`include/dvv/base/log.h`, `src/log.cpp`). Follow-up in rawreel (a separate design): the
Log pane hidden by default, its window sink, the crash wiring (§10).

## 1. Intent

The dvv logger is process infrastructure every dvv library and app uses. Today it is a small
synchronous logger (spec 2026-10-19 §11): each `DVV_LOG_xxx` call formats its message with
`std::format` on the calling thread, takes one process-wide lock and calls every sink inline under
that lock. The rawreel Log pane then copies lines out of a buffer on a 100 ms timer.

That costs the logging thread a full format and a lock on every enabled message, makes every
thread wait for the slowest sink (a file write), and needs the timer the project rules forbid
("no timers or polling"). This redesign makes logging cheap for the caller and moves all text work
to one log thread:

- a call copies its arguments into a bounded queue and returns;
- one ordinary system thread, the log thread, formats and hands each message to the targets
  (sinks) whose level accepts it;
- nothing anywhere wakes up on a timer;
- on a crash, what is still in the queue can be written out with the crash.

## 2. Goals, non-goals, anti-goals

Goals (the user's requirements, 2026-11-09):

1. **One queue any code can log to**, from any thread, holding many messages but bounded to a
   fixed, reasonable size, never all memory.
2. **Writing to the queue as fast as possible**, filtered by level before any work.
3. **Thread safe for many writing threads.**
4. **One consumer, an ordinary system thread**, that takes messages from the queue and spreads them
   over the supported and enabled targets: window, file, system log.
5. **Level filtering on the consumer side too**, per target.
6. **Avoid every operation that can be avoided** along the whole path.
7. **Read-only format strings and stored arguments**: a call site's format string and location are
   static, read-only data; a message carries only its arguments, and only when it will be printed.
8. **Messages that are only outside text** (`DVV_LOG_INFO("{}", text)`) still work, but are
   reported as a warning of non-optimal logger usage.
9. **Crash support**: the queue's messages can be written out on a crash, into the crash dump
   where the system allows it.

Non-goals:

- A binary log file read by an offline tool (NanoLog style). The file stays text.
- Structured or JSON logging, per-logger names, tags, rate limiting, metrics, thread names.
- Deferred formatting of enums and user types (they are formatted on the caller, §5.3).
- Logging from inside a signal handler or another async-signal context.
- Changing the text of the standard line (§5.6) or of any existing message.

Anti-goals (it would be a failure even if it worked):

- Any timer, timed wait or periodic wake-up on the normal path (log thread, sinks or GUI).
- Losing the compile-time check of format strings that `std::format_string` gives today.
- A writing thread blocked behind a slow sink (a file write, a GUI update) in asynchronous mode.
- A third-party dependency in dvv-base (C++23 and the standard library only, plus OS APIs).

## 3. Constraints

- dvv-base: C++23, no dependencies beyond the standard library and the OS (spec 2026-10-19 §3).
- Windows (MSVC), Linux (clang, libstdc++), macOS (Apple clang, libc++; deployment target macOS
  27, so `std::atomic::wait` is available), on x64 and arm64.
- Clean under the existing TSan and ASan CI jobs (`linux-clang-tsan`, `*-asan`).
- Each OS's native mechanism for the system log (project rule: native per OS).
- The caller-side API stays source compatible: all 99 existing macro calls (rawreel `src/` and
  `libs/dvv-*/src`) and the two direct `Logger::write` calls compile unchanged.
- The `Logger` and its sinks stay process infrastructure: never destroyed, exempt from the leak
  check (spec 2026-10-19 §9.4).

## 4. Decided with the user

- Own implementation in dvv-base, borrowing the design of Quill (github.com/odygrd/quill, MIT;
  reviewed at 13.1.0): static per-call-site metadata, arguments encoded as bytes into a queue, one
  background thread that formats, a generated decode function per argument-type list. Not Quill
  itself: it uses fmt rather than `std::format`, checks formats only at run time, its background
  thread wakes every 100 µs by default, and its crash handler is best effort with nothing written
  into a dump.
- Formatting stays `std::format`: checked at compile time at the call, run on the log thread.
- Application code may build strings with `std::format` and log them, in particular for a single
  message outside a time-critical path, through an explicit text call (§5.2).
- The consumer is an ordinary system thread (`std::thread`), not a task on a pool.

## 5. Design

### 5.1 Two modes

- **Synchronous mode**, the state before `start()` and after `stop()`: a call formats its message
  and calls the sinks on the calling thread, under the Logger's lock, before it returns, as today.
  Tests, startup messages and command-line tools (rawreel-cli) use it. It does not use the queue.
- **Asynchronous mode**, between `start()` and `stop()`: a call encodes its message into the queue
  and returns; the log thread formats it and calls the sinks.

Both modes share one encoder and one decode-and-format function (§5.3), so a message prints the
same in both.

```text
any thread                           bounded queue                 log thread (one)
DVV_LOG_INFO(fmt, a, b)
  level gate (1 atomic load) ──no──> return
  encode {header, site*, a, b} ───> [rec][rec][rec]...  ──wake──>  decode and format once,
  wake the log thread if asleep                                    build the line once,
                                                                   sinks whose level accepts it,
                                                                   batchEnd() on every sink
```

### 5.2 The call site

`DVV_LOG_TRACE/DEBUG/INFO/WARN/ERROR(fmt, args...)` and `DVV_LOG(level, fmt, args...)` keep their
form. One call site defines, once, a `static constexpr` site record: the format text, its
`std::source_location`, the decode-and-format function for its argument types (§5.3), and a flag
"the format is exactly `{}`". The level is not in the site (one call in dvv-gpu chooses its level
at run time); it travels in the message.

In this order, a call:

1. checks the level with one relaxed atomic load (`Logger::enabled`, §5.6); when it is off, the
   arguments are not evaluated, as today;
2. passes the format literal to a parameter of type `std::format_string<Args...>`, so a wrong
   argument count or format spec is a compile error, as today; only the text is kept;
3. in synchronous mode formats and writes at once; in asynchronous mode encodes the message into the
   queue (§5.3, §5.4) and wakes the log thread if it sleeps (§5.5).

`DVV_LOG_TEXT(level, text)` logs a string the caller already has (an `Error::describe()`, a
Vulkan validation message, a line from FFmpeg) or built for a one-off message. It goes through the
same level gate, so `DVV_LOG_TEXT(Debug, std::format(...))` does not format when Debug is off. It
expands to `Logger::write(level, text, where)`, which stays public: dvv-gpu (the Vulkan messenger,
`instance.cpp`) and dvv-components (the FFmpeg log callback, `loader.cpp`) already call it with
run-time text behind their own `enabled()` check and keep compiling unchanged. It is never warned
about.

A plain call whose format is exactly `"{}"` works, and the logger adds once per call site, at
Warn, in both modes: `log call at <file>:<line> formats only "{}": use DVV_LOG_TEXT or pass the
arguments`.

### 5.3 Messages and argument encoding

A message in the queue has one of two kinds:

- **Arguments**: the call site's format plus encoded arguments, formatted on the log thread.
- **Text**: the finished message text. Used by `DVV_LOG_TEXT`/`Logger::write` (with the
  `std::source_location` stored in the record, by value: its file name is static data like a
  site's) and by the caller-side fallback below (with the site's location; the site's format is
  not used).

A record's content (the layout is the builder's, §9):

| Field | Content |
| --- | --- |
| commit word | 64-bit: the record's start position once published, else anything else (§5.4) |
| size, kind, level, argument count | |
| time | `std::chrono::system_clock::now()` at the call |
| site | address of the call site's static record (arguments kind, and text kind from the fallback) |
| location | `std::source_location` (text kind from `Logger::write` only) |
| per argument | a one-byte type tag, then the value |

The deferred types are a closed set:

| Type | Stored as | Tag |
| --- | --- | --- |
| `bool`, `char` | the value | bool, char |
| `signed char`, `unsigned char`, other integers, floating point | the value's bytes | kind and width |
| `std::string`, `std::string_view`, `const char*`, `char*`, `char[N]` | length and characters, copied | string |
| `const void*`, `void*`, `std::nullptr_t` | the address | pointer |

Rules:

- Strings are always copied: a view may be gone before the log thread runs. A null `const char*`
  is stored as `(null)`. Character arrays and `char*` are strings, never pointers.
- **A message with any other argument type** (an enum, a class such as dvv-gpu's `DeviceId`, a
  `std::filesystem::path`) is formatted on the calling thread with `std::format`, today's
  behaviour, and stored as a text record. It is not warned about.
- One record is at most 1/16 of the queue. A longer string argument is cut on a UTF-8 code-point
  boundary to fit and ends with `…[cut]`.

**The decode-and-format function**, one generated per argument-type list and named by the site:
`void(const std::byte* arguments, std::string_view format, std::string& out)`. It decodes the
arguments into locals of their exact types (strings as `std::string_view` into the record) and
calls `std::vformat_to(std::back_inserter(out), format, std::make_format_args(locals...))` in the
same frame, so no `format_args` outlives the values it refers to. The format text was checked at
compile time against the same types, so formatting cannot fail on a format error.

### 5.4 The queue

One bounded ring of bytes shared by all writing threads (many writers, one reader), of a fixed
4 MiB (about 50,000 typical messages), allocated by the first `start()` and kept for the life of
the process.

- A writer reserves a record's bytes with an atomic compare-and-swap on the write position, checked
  against the release position (§5.5), copies the record, then publishes it by storing the record's
  64-bit start position (it counts every byte ever reserved, so it is unique per lap) into the
  commit word, with release ordering. No lock, no allocation, no system call on the normal path.
- A reader accepts a record at position P only when its commit word equals P. Bytes left from an
  earlier lap can never equal P, so a claimed but unfinished record is never taken for a finished
  one.
- Records keep the order of their reservations, so the output order is the queue order.
- A record never wraps: when it does not fit before the ring's end, the writer fills the end with a
  padding record (published the same way) and starts at the beginning.
- The log thread reads records in order and stops at the first one not yet published.

When the queue is full:

- Trace, Debug and Info may fill the ring only up to 3/4 of its size; Warn and Error may use all of
  it. So a burst of chatter never takes the space a warning needs.
- A message that does not fit is dropped and counted per level. A writer never waits for space.
- When space is back, the log thread writes one Warn line before the next record:
  `N log messages dropped (E error, W warn, I info, D debug, T trace)`, the non-zero counts only.

One shared ring rather than Quill's one queue per thread: per-thread queues avoid the writers'
shared atomic, but need thread registration, queues left behind by finished threads and a merge by
time; dvv's message rate (a few per second, bursts of a few hundred) does not need them. They stay
a later option behind the same API if a benchmark ever shows contention.

### 5.5 The log thread

One `std::thread`, named `dvv-log` where the OS allows, started by `start()` and joined by `stop()`.

**Waking, without timers.** A 32-bit `std::atomic<std::uint32_t> asleep`, all accesses
`seq_cst`:

- The log thread, with nothing to read: `asleep.store(1)`; check once more for a published record at
  the read position; if there is one, `asleep.store(0)` and go on; else `asleep.wait(1)`.
- A writer, after publishing its record: `if (asleep.exchange(0) == 1) asleep.notify_one();`.
- `flush()` and `stop()` wake it the same way after posting their request.

The writer changes the waited-on value, so `wait(1)` returns, and the `seq_cst` pairing (store of
the commit word, then exchange on `asleep`; store of `asleep`, then load of the commit word) rules
out both sides reading stale values. A burst of messages costs one wake, and an idle process has no
wake-ups. This order is required, not a builder's choice: TSan cannot detect a violation.

**One pass**, repeated while records are published:

1. Take the records from the read position up to the first unpublished one.
2. For each record: if no sink accepts its level, skip it. Else get the message (call the site's
   decode-and-format function into a buffer the thread keeps between records, or take the text),
   build the standard line once (§5.6), and call `write` on every sink whose level accepts it.
3. Call `batchEnd()` on every sink, then store the new release position (release ordering), giving
   the bytes back to the writers. Bytes are released only after the sinks' `batchEnd()`, so a crash
   snapshot (§5.8) covers everything a sink might not yet have made durable; it may repeat lines of
   the pass in progress (a duplicate is better than a lost line).

**Errors on the log thread.** Every call into a sink and every format runs inside `try`/`catch (...)`.
A throwing sink call loses that one line for that sink; the first time a sink throws, the log
thread queues one Warn line `a log sink failed: <what>` (once per sink). An allocation failure of
the format buffer drops the message and counts it as dropped. Nothing propagates out of the thread.

Also on the log thread: the dropped-messages line (§5.4) and the once-per-site bare-`"{}"` warning
(§5.2).

### 5.6 Sinks

```cpp
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void write(const LogRecord& record, std::string_view line) = 0;
    virtual void batchEnd() {}  // asynchronous mode: after every pass that called write()
    virtual void flush() {}
};
```

- `LogRecord` is unchanged: `level`, `message` (the formatted message), `where`
  (`std::source_location`), `time`.
- `line` is the standard line, byte-identical to today's `formatLogRecord`:
  `2026-10-20T12:34:56.789Z INFO  message [file.cpp:42]`. It is built only if some sink is called.
- `Logger::addSink(sink, level = Trace)` adds a sink with its own minimum level; `clearSinks()`
  removes all. **Both are allowed only in synchronous mode** (an `Internal` error result in
  asynchronous mode), so the log thread reads the sink list without a lock. After `clearSinks()`
  returns, no removed sink is called again.
- `Logger::setSinkLevel(sink, level)` changes a sink's level at any time (an atomic per entry),
  e.g. the window shown or hidden.
- `setLevel()` and `level()` keep today's meaning: the process-wide minimum the program sets
  (rawreel-cli's Warn). Tests that save and restore `level()` keep working.
- **The writer's level gate**, `enabled(level)`: `level != Off`, `level >= level()`, and
  `level >=` the smallest sink level, which is Off when no sink is registered (such a message went
  nowhere anyway). With every sink at Info, a Debug call costs one atomic load. `addSink`,
  `setSinkLevel`, `clearSinks` and `setLevel` update the gate.
- Sinks are called one at a time: on the log thread in asynchronous mode, under the Logger's lock in
  synchronous mode. A sink needs a lock only for data it shares with other threads. A sink must not
  log, call `flush()`, `addSink()`, `start()` or `stop()` (rule kept).

Built-in sinks, in dvv-base:

| Sink | Behaviour |
| --- | --- |
| `RotatingFileSink` | as today, including "Warn and above flushed at once, Info buffered"; in asynchronous mode it also flushes in `batchEnd()`, so what the log thread has handled is on disk when a crash comes |
| `StderrSink` | as today |
| `MemorySink` | as today (tests) |
| `SystemLogSink` | the OS log, below |

`SystemLogSink(identity)`, one implementation per OS. It owns its `identity` string (`openlog`
keeps the pointer), and never passes the message as a format:

- **macOS**: unified logging, `os_log_create(subsystem, "log")` and
  `os_log_with_type(log, type, "%{public}.*s", size, data)`. Error → `OS_LOG_TYPE_ERROR`, Warn →
  `OS_LOG_TYPE_DEFAULT`, Info → `OS_LOG_TYPE_INFO`, Debug and Trace → `OS_LOG_TYPE_DEBUG`. Seen in
  Console.app and `log stream`.
- **Linux**: `syslog(priority, "%.*s", size, data)` after `openlog(identity, LOG_PID, LOG_USER)`;
  journald collects it where it runs. Error → `LOG_ERR`, Warn → `LOG_WARNING`, Info → `LOG_INFO`,
  Debug and Trace → `LOG_DEBUG`. No libsystemd dependency.
- **Windows**: `OutputDebugStringW` with the standard line (seen in a debugger or DebugView). Not
  the Event Log: it needs an event source registered by an installer, and rawreel ships as a zip.

The window target is not in dvv-base: the app writes a sink whose `write` appends to its own
bounded history and whose `batchEnd` posts one event to the GUI thread when none is pending (§10).

### 5.7 Start, stop, flush

`start()`, `stop()` and `flush()` are serialized by one control mutex.

- `start()`: allocates the queue on first use, starts the log thread and switches to asynchronous
  mode. A second `start()` does nothing.
- **A writer in asynchronous mode** increments an in-flight count before it reads the mode and
  decrements it after publishing (or dropping), so `stop()` can know that no writer is still inside
  the asynchronous path.
- `stop()`: switches the mode to synchronous (new calls now format at once), waits with
  `atomic::wait` until the in-flight count is 0, wakes the log thread with a stop request, joins it
  (the thread first handles every published record), then handles on the calling thread, under the
  Logger's lock, any record published after the thread's last pass, and flushes every sink. A second
  `stop()` does nothing.
- `flush()`: returns when everything logged before the call has been handed to every sink and each
  sink's `flush()` has run. In asynchronous mode it posts a request and waits with `atomic::wait`
  for the log thread to pass it; in synchronous mode it flushes the sinks. It re-checks the mode
  under the control mutex, so it never waits for a thread `stop()` already joined. Called from the
  log thread (from a sink) it is an error and returns at once.
- **In asynchronous mode `flush()` is not for crash handling**: if the crash happened on the log
  thread, or holds memory it needs, `flush()` would wait for ever. A crash handler uses
  `writeCrashSnapshot` (§5.8). An app that installs a crash handler which calls `flush()` (rawreel's
  Windows minidump handler does today) must change it before it calls `start()` (§10).
- `finishWithLeakCheck` calls `stop()` (today it calls `flush()`).

### 5.8 Crash snapshot

dvv-base gives the crash handler three things; the handlers stay in the app (§10).

- `Logger::writeCrashSnapshot(CrashWriter writer, void* context)`: writes, through `writer` (a
  function pointer, e.g. one calling `write(2)` on a file descriptor or copying into a buffer), the
  published records from the release position up to the first unpublished one, the dropped counts,
  and a table of the call sites those records name (format text, file, line) plus the file names of
  text records. It is **async-signal-safe**: no allocation, no lock, no formatting.
  - The other threads keep running while it reads, so every size and length is checked against the
    ring and the record before use, and each record is copied first and kept only if the release
    position has not passed it meanwhile.
  - The site table is built in a fixed buffer reserved by `start()`. The first caller claims it with
    an `atomic_flag`; a later or concurrent caller returns at once without writing.
  - Records naming sites beyond the table's capacity decode as `<unknown call site>`.
- `Logger::crashSnapshotCapacity()`: a constant, the most bytes a snapshot can take, for a crash
  handler that reserves its buffer at install time.
- `dvv::decodeCrashSnapshot(std::span<const std::byte>)`: turns a snapshot back into standard lines,
  using the tags (§5.3), each ending in ` (from crash snapshot)`. It checks every bound, never
  throws, accepts a snapshot cut short, and if one record fails to decode or format it emits that
  record's raw format text and values instead.

The snapshot holds the format texts it needs, so it can be decoded without the program or its
symbols.

### 5.9 Rules

- A library with log calls must not be unloaded while its records may be in the queue: the site and
  decode-function addresses point into it. `flush()` before unloading satisfies the rule. (rawreel
  loads only FFmpeg and the Vulkan loader at run time, and neither logs through dvv.)
- A sink must not block for long: every later message waits for it (writers do not; they keep
  queueing until the queue is full and then drop).
- No logging from a signal handler; `writeCrashSnapshot` is the only signal-safe entry.
- An app that calls `start()` must call `stop()` (or `finishWithLeakCheck`) on every normal exit
  path; a path that skips it loses what is still queued.
- No `fork()` in asynchronous mode: the child would have no log thread. (No dvv code forks.)

## 6. API summary

```cpp
namespace dvv {
enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warn, Error, Off };   // unchanged
struct LogRecord { LogLevel level; std::string_view message; std::source_location where;
                   std::chrono::system_clock::time_point time; };            // unchanged
using CrashWriter = void (*)(void* context, const void* data, std::size_t size);

class Logger {
public:
    static Logger& instance() noexcept;
    Result<void> addSink(std::shared_ptr<ILogSink> sink, LogLevel level = LogLevel::Trace);
    void setSinkLevel(const ILogSink& sink, LogLevel level);
    Result<void> clearSinks();
    void setLevel(LogLevel level) noexcept;   [[nodiscard]] LogLevel level() const noexcept;
    [[nodiscard]] bool enabled(LogLevel level) const noexcept;  // the writer's gate (§5.6)
    void write(LogLevel level, std::string_view text,            // DVV_LOG_TEXT; unchanged
               std::source_location where = std::source_location::current());
    void start();
    void stop();
    void flush();
    void writeCrashSnapshot(CrashWriter writer, void* context) const noexcept;
    [[nodiscard]] static constexpr std::size_t crashSnapshotCapacity() noexcept;
};
std::vector<std::string> decodeCrashSnapshot(std::span<const std::byte> snapshot) noexcept;
}
// DVV_LOG(level, fmt, ...), DVV_LOG_TRACE..ERROR(fmt, ...), DVV_LOG_TEXT(level, text)
```

The names are the builder's to adjust; the behaviour is not. (`addSink` and `clearSinks` returning
a `Result` is a source change only for callers that ignore it, which still compile.)

## 7. Migration

Scope of this design: dvv-base 0.3.0 only. No other dvv library changes; rawreel only moves to
dvv-base 0.3.0 and stays in synchronous mode (it does not call `start()`) until the rawreel
follow-up (§10), so its crash handler's `flush()` stays safe.

- **Callers**: unchanged. The two direct `Logger::write` callers (dvv-gpu, dvv-components) keep
  compiling (§5.2). The 10 bare-`"{}"` calls in rawreel's `src/` keep working and now give the
  once-per-site warning; rewriting them is part of §10.
- **Sinks**: the `write(record, line)` signature is kept. What changes: `batchEnd()` is new, and in
  asynchronous mode `write` runs on the log thread instead of on the logging thread under the lock.
  rawreel's `LogPanelSink` keeps working unchanged until the rawreel step replaces it.
- **Tests** that log and then read a `MemorySink` keep working: synchronous mode handles a message
  before the call returns. Tests of asynchronous mode call `flush()` before reading.
- **Release**: dvv-base 0.3.0 (new API: `start`, `stop`, sink levels, `DVV_LOG_TEXT`, crash snapshot;
  changed threading of sinks), with a CHANGELOG entry; rawreel moves to it in `dvv-deps.txt`.

## 8. Tests (dvv-base)

All waits in tests are on atomics or `flush()`, never sleeps or polling; a failure shows as a hang
that the ctest timeout reports.

- Synchronous mode: every existing `test_log.cpp` case passes unchanged.
- The standard line is byte-identical to `formatLogRecord`'s for the same record.
- Order: messages from one thread arrive in order; with several threads, in reservation order.
- Level gates: a disabled level does not evaluate its arguments; per-sink levels; the gate follows
  `setSinkLevel`; with no sink it is closed; `Off` never passes; `level()` is unchanged by sink
  changes.
- Encoding round trip for every row of the §5.3 table, in both modes, including format specs
  (`{:>8}`, `{:x}`, `{:.3f}`), `char` versus `signed char`, a `char[N]`, a null `const char*`, a
  string cut on a code-point boundary, and a message with an enum and with a class (formatted on the
  caller, same text as `std::format`).
- Compile-fail test (dvv-build's `run-compile-fail.cmake`): a wrong argument count or spec in
  `DVV_LOG_INFO` does not compile.
- Bare `"{}"`: the warning appears once per call site, in both modes; `DVV_LOG_TEXT` never warns.
- `Logger::write` with run-time text and a run-time location, in both modes and in a crash snapshot.
- The queue after it has wrapped at least twice, with records of varying sizes: no record is lost,
  repeated or taken from stale bytes.
- Full queue: Info stops at 3/4 and Warn still fits; drop counts and the dropped line are exact.
- Wake-up: many threads logging in bursts with idle gaps, under TSan; a sink signals an atomic when
  the last expected message arrives and the test waits on it (no `flush()`), so a lost wake-up
  hangs. An idle log thread is blocked, not running.
- Modes: messages logged while another thread calls `stop()` all reach the sinks; `flush()`
  concurrent with `stop()` returns; `start()`/`stop()` twice are harmless; `addSink` in asynchronous
  mode returns an error.
- A throwing sink: other sinks still get the line, one `a log sink failed` line appears, the
  process lives.
- Crash snapshot: written while the log thread is held in a blocking test sink, decoded back to the
  expected lines; unpublished records are not included; it allocates nothing (counting allocator);
  a second call returns at once; `decodeCrashSnapshot` on truncated and on random bytes returns
  without crashing.
- `SystemLogSink` builds and runs on each OS (a smoke test; the OS log is not read back).
- A benchmark (not a CI gate) reports nanoseconds per call for a disabled level, three numbers and
  one string, on 1, 4 and 16 threads, and is quoted in the pull request.

## 9. Left to the builder

- The record's exact layout, the reservation and padding details and the memory orders other than
  the wake-up protocol's (§5.5), as long as TSan is clean and the §8 tests pass.
- How the site record and the decode-and-format function are generated in the macros.
- The size of the site table reserved for crash snapshots.

## 10. Follow-up in rawreel (not part of this design)

A second design, after this one is in dvv-base:

- The Log pane hidden by default, with View → Log to show it; a window sink fed through
  `batchEnd()` with no timer, replacing `LogPanelSink` and its 100 ms timer.
- A review of the messages themselves (startup chatter, the device reported three times, full
  paths).
- Crash wiring, before rawreel-gui calls `start()`: on Windows the existing minidump handler
  replaces its `DVV_LOG_ERROR` + `flush()` with `writeCrashSnapshot` as a user stream of the dump; on
  macOS and Linux a fatal-signal handler writes the snapshot to a file in the log folder, which the
  next start decodes into `rawreel.log`; the system crash report is still made afterwards.
- rawreel-gui calls `start()` after adding its sinks and `stop()` at exit; rawreel-cli stays
  synchronous.
- `SystemLogSink` at Warn for rawreel-gui, and the 10 bare-`"{}"` calls rewritten.
