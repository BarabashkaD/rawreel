# dvv-base Queued Logger Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** dvv-base 0.3.0 with a queued logger: compile-checked `std::format` calls encoded as bytes
into a bounded ring, formatted on one log thread that wakes without timers, per-sink levels, a
system-log sink per OS and a signal-safe crash snapshot; then rawreel moves to it and stays
synchronous.

**Architecture:** A call site's macro checks the level, validates the format at compile time and
either formats at once (synchronous mode, today's behaviour) or encodes a record into a shared
many-writer ring (asynchronous mode). One `std::thread` drains the ring, formats each record with
the decode-and-format function generated for its argument types, and hands the line to the sinks.
A crash handler can copy the unhandled records out with `writeCrashSnapshot`; a tag-driven decoder
turns them back into lines.

**Tech Stack:** C++23, `std::format`, `std::atomic::wait/notify`, doctest, CMake presets from
dvv-build (`ninja-macos` locally; CI adds `linux-clang-tsan`, `*-asan`, MSVC).

**Spec:** `docs/superpowers/specs/2026-11-09-dvv-logger-design.md` (revision 2). Read it first; this
plan cites its sections as §N.

## Global Constraints

- dvv-base: C++23, standard library and OS APIs only, no new dependency (§2 anti-goals, §3).
- No timer, timed wait, sleep or polling anywhere, tests included: waits are `atomic::wait`,
  `flush()` or joins (§2, §8).
- All 99 existing macro calls and the two direct `Logger::write` calls compile unchanged (§3).
- The standard line stays byte-identical to `formatLogRecord`:
  `2026-10-20T12:34:56.789Z INFO  message [file.cpp:42]` (§5.6).
- Synchronous mode (before `start()`, after `stop()`) handles a message before the call returns;
  every existing `test_log.cpp` case passes unchanged (§5.1, §8).
- Ring: `dvv::detail::LOG_RING_BYTES` = 4 MiB; Trace/Debug/Info up to 3/4 of it, Warn/Error all
  of it; one record at most `dvv::detail::LOG_MAX_RECORD_BYTES` = `LOG_RING_BYTES / 16` (§5.3, §5.4).
- Wake-up protocol exactly as §5.5 (32-bit `asleep`, all `seq_cst`, writer `exchange(0)`).
- Versioning: dvv-base 0.3.0 must stay source compatible with 0.2.0 callers (CHANGELOG rules).
- Commit identity BarabashkaD; no machine paths, real names or personal e-mail in committed files;
  every commit message ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Test cycles: per commit run only the focused command below; each full suite runs once, serially
  (dvv-base in Task 7, rawreel in Task 8); never run GUI tests in parallel.
- Push, pull requests, merges and tags are the user's decision: one stop at the end of Task 8.

## Rulings against the spec

- **R1** §6 says `addSink`/`clearSinks` return `Result<void>`. `dvv::Result` is `std::expected`;
  whether a toolchain warns when it is ignored differs (the plan reviewers disagree about MSVC's STL,
  and warnings are errors), and existing callers ignore the result (rawreel: 4 sites). They stay
  `void`, which is source compatible on every toolchain; in asynchronous mode the call is ignored and
  the logger queues one Error line `Logger::addSink ignored: the log thread is running` (or
  `clearSinks`). Cost if wrong: a caller learns of the misuse from the log, not from a return value.
- **R2** §7 leaves the 10 bare-`"{}"` calls in rawreel to the follow-up. Left as they are, they would
  add 10 new Warn lines to rawreel's log and selftests as soon as rawreel moves to 0.3.0. Task 8
  rewrites them as `DVV_LOG_TEXT`. Cost if wrong: a 10-line diff the follow-up would have made anyway.
- **R3** §5.3 puts the decode-and-format function in the site record. The record carries the function
  pointer instead and the site keeps format, location and the `{}` flag: `logCall` is a template on
  the argument types and the site is defined in the macro (§9 leaves generation to the builder).
  Cost: 8 bytes a record.
- **R4** §5.4 says the commit word is the record's start position, which "bytes left from an earlier
  lap can never equal". A stale payload word can equal it in theory (a logged `uint64` of that exact
  value at that exact offset). The commit word is `position ^ LOG_COMMIT_MIX` with
  `LOG_COMMIT_MIX = 0x9E3779B97F4A7C15`, making an accidental match require that exact logged value.
  Cost if wrong: none measurable.
- **R5** §8 "an idle log thread is blocked, not running" cannot be observed without polling or
  OS-specific thread inspection; it is not tested. The wake-up tests and the code review cover it.

## Review Focus

1. A text message longer than the record limit (a Vulkan validation message, an FFmpeg dump): it is
   cut on a code-point boundary with `…[cut]` inside the limit and the line still appears — Task 1 and
   Task 4 tests.
2. Strings that are not valid UTF-8 (FFmpeg lines in Latin-1): the cut never reads past the string,
   never loops, and keeps whole bytes — Task 1 test.
3. A sink that blocks (a file on a slow drive): Info is dropped and counted at 3/4, Warn and Error
   still queue, writers never wait — Task 3 and Task 4 tests.
4. Pool threads logging while `stop()` runs at exit: every message reaches the sinks once, each
   thread's in order, no sink runs on two threads at once, nothing hangs — Task 4 test.
5. A crash in the middle of a log call (a record reserved, never published): the snapshot stops there,
   decodes everything before it and never crashes the next start — Task 5 tests.

## File Structure (all in `libs/dvv-base` unless stated)

| File | Responsibility |
| --- | --- |
| `include/dvv/base/log.h` | public API: levels, `LogRecord`, `ILogSink`, built-in sinks, `Logger`, macros |
| `include/dvv/base/log_encode.h` (new) | `dvv::detail`: ring constants, argument tags, the deferrable set, encoded size, encode, the generated decode-and-format function, `LogSite`, `logCall` |
| `src/log_ring.h`, `src/log_ring.cpp` (new) | the bounded many-writer byte ring (§5.4) and the private test hooks |
| `src/log.cpp` | `Logger`: gate, sink list, synchronous path, log thread, start/stop/flush, existing sinks |
| `src/log_snapshot.cpp` (new) | `writeCrashSnapshot`, `decodeCrashSnapshot` (§5.8) |
| `src/system_log_sink.cpp` (new) | `SystemLogSink`, `#if _WIN32 / #elif __APPLE__ / #else` (Linux) like `src/paths.cpp` |
| `src/leak_check.cpp` | `finishWithLeakCheck` calls `stop()` |
| `tests/test_log_encode.cpp`, `test_log_ring.cpp`, `test_log_async.cpp`, `test_log_snapshot.cpp` (new) | new tests; `tests/test_log.cpp` keeps today's cases and gains the synchronous ones |
| `tests/compile_fail/log_format.cpp` (new) | compile-fail cases for format checks |
| `tests/bench/log_bench.cpp` (new) | benchmark executable, not a test |
| `README.md`, `CHANGELOG.md`, `CMakeLists.txt` | docs, 0.3.0 |

**Commands** (run from `libs/dvv-base`; the tree `../_build/dvv-base/ninja-macos` is configured):

- focused run (Tasks 1-6, Step 2 and Step 4):
  `cmake --build --preset ninja-macos && ../_build/dvv-base/ninja-macos/tests/Debug/dvv_base_tests -sf="*test_log*"`
  — builds all targets (including the compile-fail control) and runs every `test_log*.cpp` file.
  Expected on success: doctest's summary shows a non-zero number of test cases and 0 failed.
- compile-fail tests: `ctest --preset ninja-macos -R "dvv_base\.compile_fail"` (after the build).
- full dvv-base suite (Task 7 only): `cmake --build --preset ninja-macos && ctest --preset ninja-macos`.

TSan and MSVC run in CI only (`linux-clang-tsan`, `windows-msvc*`), after the push at the end.

**Branch:** in `libs/dvv-base` (a submodule, detached at `v0.2.0`), before Task 1:
`git -C libs/dvv-base fetch origin && git -C libs/dvv-base switch -c feat/queued-logger origin/main`.
dvv-base `origin/main` is two commits past `v0.2.0`; if its pending `chore/dvv-build-0.4.0` merges
first, rebase `feat/queued-logger` onto it before the push.

**Test helpers** shared by the new test files (put them in `tests/log_test_support.h`, new):

- `Colour` — a namespace-scope `enum class Colour { Red };` with a `std::formatter` that prints `red`.
- `AsyncScope` — constructor `Logger::instance().stop(); clearSinks();`; destructor `stop()`,
  `clearSinks()`, `setLevel(Info)`, so a failed check never leaves the log thread running.
- `WaitingSink` — counts lines in `write()` into a vector under its own mutex, and in `batchEnd()`
  stores the count into `std::atomic<std::uint32_t> handled` and calls `handled.notify_all()`;
  `waitFor(n)` loops on `handled.wait(seen)` until `handled >= n`. Added after any other sink, so its
  `batchEnd` runs after theirs.

---

### Task 1: Argument encoding and the decode-and-format function

**Files:**
- Create: `include/dvv/base/log_encode.h`, `tests/test_log_encode.cpp`, `tests/log_test_support.h`
- Modify: `tests/CMakeLists.txt` (add the test file)

**Interfaces:**
- Produces (namespace `dvv::detail`):
  - `inline constexpr std::size_t LOG_RING_BYTES = std::size_t{4} << 20U;`
    `inline constexpr std::size_t LOG_MAX_RECORD_BYTES = LOG_RING_BYTES / 16;`
  - `enum class LogArgTag : std::uint8_t { Bool, Char, I8, U8, I16, U16, I32, U32, I64, U64, F32, F64, F80, String, Pointer };`
    (`F80` = `long double`, stored as its `sizeof` bytes).
  - `template <class T> inline constexpr bool LOG_DEFERRABLE` — true exactly for (after
    `std::remove_cvref_t`): `bool`, `char`, `signed char`, `unsigned char`, the other integer types,
    `float`, `double`, `long double`, `std::string`, `std::string_view`, `const char*`, `char*`,
    `char[N]`, `const void*`, `void*`, `std::nullptr_t`. Not: `char8_t`, `wchar_t`, enums, classes.
  - `template <class... Args> inline constexpr bool LOG_ALL_DEFERRABLE`.
  - `template <class... Args> std::size_t encodedSize(std::size_t budget, const Args&... args) noexcept;`
  - `template <class... Args> std::byte* encodeArguments(std::byte* out, std::size_t budget, const Args&... args) noexcept;`
    Per argument: one tag byte, then the value (strings: a `std::uint32_t` length then the bytes, no
    NUL). `budget` is the argument bytes allowed, **the `…[cut]` marker included**: when the strings
    do not fit, the longest are cut so the total fits; a cut string ends at a UTF-8 code-point boundary
    followed by `…[cut]`. A null `const char*` is stored as `(null)`. A `char[N]` is the text up to its
    first NUL within N, else all N. Values are written with `memcpy` (no alignment assumed).
  - `using LogFormatFn = void (*)(const std::byte* arguments, std::string_view format, std::string& out);`
  - `template <class... Args> void decodeAndFormat(const std::byte* arguments, std::string_view format, std::string& out);`
    Decodes into locals of the exact stored types (strings as `std::string_view`), then
    `std::vformat_to(std::back_inserter(out), format, std::make_format_args(locals...))` in the same frame.
  - `std::size_t utf8CutPoint(std::string_view text, std::size_t limit) noexcept;` — if
    `limit >= text.size()` returns `text.size()`; else the largest `n <= limit` that does not split a
    multi-byte sequence; invalid bytes count as single bytes; never reads past `text`.

- [ ] **Step 1: Write the failing tests** in `tests/test_log_encode.cpp`:

```cpp
// helper: encode into a buffer, decode with the same types, compare with std::format.
template <class... Args>
std::string roundTrip(std::format_string<Args...> fmt, const Args&... args);

TEST_CASE("encode: every deferrable type prints as std::format does") {
    CHECK(roundTrip("{} {} {}", true, 'x', static_cast<signed char>(-3)) == "true x -3");
    CHECK(roundTrip("{:>8}|{:x}|{:.3f}", std::string("ab"), 255U, 2.0) == "      ab|ff|2.000");
    CHECK(roundTrip("{} {}", std::int64_t{-1}, std::uint64_t{18446744073709551615ULL}) ==
          "-1 18446744073709551615");
    const char array[] = "arr";
    char buffer[8] = "ab";
    CHECK(roundTrip("{} {} {}", array, buffer, std::string_view("sv")) == "arr ab sv");
    const char* null_text = nullptr;
    CHECK(roundTrip("{}", null_text) == "(null)");
    int value = 0;
    const void* address = &value;
    CHECK(roundTrip("{}", address) == std::format("{}", address));
    CHECK(roundTrip("{} {}", 1.5F, 2.25L) == "1.5 2.25");
}
TEST_CASE("encode: enums, classes and paths are not deferrable") {
    CHECK_FALSE(dvv::detail::LOG_DEFERRABLE<Colour>);
    CHECK_FALSE(dvv::detail::LOG_DEFERRABLE<std::filesystem::path>);
    CHECK(dvv::detail::LOG_DEFERRABLE<char[4]>);
    CHECK(dvv::detail::LOG_DEFERRABLE<const std::string&>);
}
TEST_CASE("encode: a string over the budget is cut on a code point, marker inside the budget") {
    const std::string text = std::string(100, 'a') + "日本";   // 3-byte code points
    // budget = tag + length + 101 text bytes + the 8-byte marker: the cut must not split 日
    CHECK(encodeDecodeWithBudget(text, 1 + 4 + 101 + 8) == std::string(100, 'a') + "…[cut]");
}
TEST_CASE("encode: invalid UTF-8 is cut by bytes without reading past it") {
    const std::string latin1 = "caf\xE9 \xE9\xE9\xE9";
    CHECK(dvv::detail::utf8CutPoint(latin1, 5) == 5);
    CHECK(dvv::detail::utf8CutPoint(latin1, 100) == latin1.size());
    CHECK(dvv::detail::utf8CutPoint("\xF0\x9F\x8E\x9E", 2) == 0);   // never splits a sequence
}
```

- [ ] **Step 2: Focused run** — Expected: build error, `log_encode.h` not found.
- [ ] **Step 3: Implement `include/dvv/base/log_encode.h`** (header-only) and `tests/log_test_support.h`.
- [ ] **Step 4: Focused run** — Expected: all pass, non-zero test cases.
- [ ] **Step 5: Commit** — `git commit -m "feat(log): argument encoding and the decode-and-format function"`

### Task 2: Call sites, the level gate and synchronous mode on the encoder

**Files:**
- Modify: `include/dvv/base/log.h`, `src/log.cpp`, `tests/CMakeLists.txt`, `tests/test_log.cpp`
- Create: `tests/compile_fail/log_format.cpp`

**Interfaces:**
- Consumes: Task 1.
- Produces:
  - `struct dvv::detail::LogSite { std::string_view format; std::source_location where; bool bareBraces; };`
    `bareBraces` is `format == "{}"`, computed `constexpr`.
  - `template <class... Args> void dvv::detail::logCall(LogLevel level, const LogSite& site, std::atomic<bool>& bareWarned, std::format_string<Args...> fmt, const Args&... args);`
    `if constexpr (LOG_ALL_DEFERRABLE<Args...>)` → an Arguments record with
    `&decodeAndFormat<std::remove_cvref_t<Args>...>` (R3); else
    `std::vformat(fmt.get(), std::make_format_args(args...))` and a Text record with `site.where`.
  - Macros: `DVV_LOG(level, fmt, ...)` (`__VA_OPT__`, `/Zc:preprocessor` is set on MSVC) defines a
    `static constexpr ::dvv::detail::LogSite` from the literal `fmt` and
    `std::source_location::current()`, and **always** a `static constinit std::atomic<bool>` flag
    passed as `bareWarned` (no conditional code in the macro; MSVC `/W4 /WX` rejects a constant `if`,
    C4127). The level gate (`Logger::instance().enabled(level)`) comes first; arguments are evaluated
    only after it. `DVV_LOG_TRACE..ERROR(fmt, ...)` as today. `DVV_LOG_TEXT(level, text)` →
    `if (enabled(level)) Logger::instance().write(level, text)`.
  - `void Logger::addSink(std::shared_ptr<ILogSink> sink, LogLevel level = LogLevel::Trace);`
  - `void Logger::setSinkLevel(const ILogSink& sink, LogLevel level);`
  - `void Logger::clearSinks();` (R1)
  - `bool Logger::enabled(LogLevel level) const noexcept;` — `level != Off && level >= level() &&
    level >= the smallest sink level` (Off with no sinks), read from one atomic gate value that
    `addSink`, `setSinkLevel`, `clearSinks` and `setLevel` recompute.
  - Internal (`src/log.cpp`): `Logger::submit(...)`, the one entry every call reaches. In this task it
    always takes the synchronous path: encode into a `thread_local` scratch buffer (never the ring),
    decode-and-format, `formatLogRecord`, call the accepting sinks under `mutex_`, before returning.
  - Bare `"{}"` warning, synchronous path: when `bareWarned.exchange(true)` returns `false` and
    `site.bareBraces` is true, after the message's own sinks the same locked pass writes one Warn
    record with the site's location: `log call at <file>:<line> formats only "{}": use DVV_LOG_TEXT or pass the arguments`
    (`<file>` = `sourceFileName(site.where)`); no recursive lock.

- [ ] **Step 1: Write the failing tests** (new cases in `tests/test_log.cpp`; existing ones untouched):

```cpp
TEST_CASE("sync: a disabled level does not evaluate its arguments") {
    LoggerScope scope; auto sink = std::make_shared<dvv::MemorySink>();
    dvv::Logger::instance().addSink(sink, dvv::LogLevel::Info);
    int evaluated = 0;
    DVV_LOG_DEBUG("n {}", ++evaluated);
    CHECK(evaluated == 0);
}
TEST_CASE("sync: the gate is closed without sinks and follows sink levels") {
    LoggerScope scope; auto& logger = dvv::Logger::instance();
    logger.setLevel(dvv::LogLevel::Trace);
    CHECK_FALSE(logger.enabled(dvv::LogLevel::Error));          // no sinks
    auto sink = std::make_shared<dvv::MemorySink>();
    logger.addSink(sink, dvv::LogLevel::Warn);
    CHECK_FALSE(logger.enabled(dvv::LogLevel::Info));
    logger.setSinkLevel(*sink, dvv::LogLevel::Debug);
    CHECK(logger.enabled(dvv::LogLevel::Debug));
    CHECK(logger.level() == dvv::LogLevel::Trace);               // sink changes leave level() alone
    CHECK_FALSE(logger.enabled(dvv::LogLevel::Off));
}
TEST_CASE("sync: per-sink levels") {
    // sink A at Trace, sink B at Warn; one Info and one Warn call: A has 2 lines, B has 1.
}
TEST_CASE("sync: a message is in the sink before the call returns") {
    // 8 threads log at once; each checks its own line is in the MemorySink right after its call.
}
TEST_CASE("sync: deferred, caller-formatted and text messages print as std::format") {
    // DVV_LOG_INFO("{} {:>4}", 7, "ab") -> "7   ab"; DVV_LOG_INFO("colour {}", Colour::Red) ->
    // "colour red"; DVV_LOG_TEXT(Warn, "plain") -> "plain"; each line == dvv::formatLogRecord(record).
}
TEST_CASE("sync: bare {} warns once per call site, DVV_LOG_TEXT never") {
    // a loop calling DVV_LOG_INFO("{}", text) 3 times: 3 messages + exactly 1 line containing
    // "formats only \"{}\"" and "test_log.cpp"; DVV_LOG_TEXT twice: no warning line.
}
TEST_CASE("sync: Logger::write keeps its run-time location") {
    // logger.write(Info, "x", where) with where = std::source_location::current() on a known line ->
    // the line ends with "[test_log.cpp:<that line>]".
}
```

  And `tests/compile_fail/log_format.cpp` with `DVV_COMPILE_FAIL_CASE` 0 (control, valid
  `DVV_LOG_INFO("{} {}", 1, 2)`), 1 (`DVV_LOG_INFO("{} {}", 1)`), 2 (`DVV_LOG_INFO("{:d}", "text")`),
  registered like `direct_construction.cpp`: case 0 as the default-built target
  `dvv_base_log_format_ok`; cases 1 and 2 as the tests `dvv_base.compile_fail.log_format.1/2`, pattern
  `C7595` on MSVC and `constant expression` otherwise.

- [ ] **Step 2: Focused run** — Expected: build errors (no `addSink(sink, level)`, no `DVV_LOG_TEXT`).
- [ ] **Step 3: Implement** the interfaces above.
- [ ] **Step 4: Focused run, then the compile-fail tests** — Expected: all pass, including every
  pre-existing `test_log.cpp` case; `dvv_base.compile_fail.log_format.1/2` pass.
- [ ] **Step 5: Commit** — `git commit -m "feat(log): call sites, sink levels and the synchronous path on the encoder"`

### Task 3: The bounded ring

**Files:**
- Create: `src/log_ring.h`, `src/log_ring.cpp`, `tests/test_log_ring.cpp`
- Modify: `CMakeLists.txt` (add `src/log_ring.cpp`), `tests/CMakeLists.txt`
  (`target_include_directories(dvv_base_tests PRIVATE ${dvv_base_SOURCE_DIR}/src)`)

**Interfaces:**
- Produces `class dvv::detail::LogRing`:
  - `explicit LogRing(std::size_t bytes = LOG_RING_BYTES);` (tests use small rings; a power of two,
    at least 4 KiB; its record limit is `bytes / 16`)
  - `struct Reservation { std::byte* payload; std::uint64_t position; std::uint32_t size; };`
  - `[[nodiscard]] std::optional<Reservation> reserve(std::uint32_t payloadBytes, bool urgent) noexcept;`
    — CAS on the write position against the release position; `urgent` (Warn/Error) may fill the
    whole ring, else up to 3/4; records 8-byte aligned; never wraps (writes a padding record first);
    `nullopt` when it does not fit or the record exceeds `bytes / 16`.
  - `void publish(const Reservation& reservation) noexcept;` — stores `position ^ LOG_COMMIT_MIX`
    into the record's 64-bit commit word, release ordering (R4).
  - `void publishAsPadding(const Reservation& reservation) noexcept;` — publishes it as a padding
    record that readers skip (tests that hold a reservation use it to unblock the ring).
  - `struct Record { const std::byte* payload; std::uint32_t size; std::uint64_t position; std::uint64_t next; };`
  - `[[nodiscard]] std::optional<Record> read(std::uint64_t position) const noexcept;` — the published
    record at `position` (skipping padding), else `nullopt`.
  - `void release(std::uint64_t position) noexcept;` (release ordering),
    `[[nodiscard]] std::uint64_t releasePosition() const noexcept;`,
    `[[nodiscard]] const std::byte* base() const noexcept; [[nodiscard]] std::size_t bytes() const noexcept;`

- [ ] **Step 1: Write the failing tests** (`tests/test_log_ring.cpp`):

```cpp
TEST_CASE("ring: records come back in reservation order with their bytes") {}
TEST_CASE("ring: an unpublished record stops the reader, then appears once published") {}
TEST_CASE("ring: stale bytes from earlier laps are never read as a record") {
    // 4 KiB ring; record sizes cycling 24, 200, 72, 1000 bytes, which forces padding at the end
    // (check at least one padding record was skipped); reserve/read/publish/read/release for at
    // least 3 laps; before each publish read(position) is nullopt although earlier laps' bytes are
    // there; every payload read back equals what was written.
}
TEST_CASE("ring: limits") {
    // Info stops at 3/4; Warn and Error fill the rest; a record over bytes/16 is refused at any level.
}
TEST_CASE("ring: 8 writer threads, one reader, nothing lost or repeated") {
    // Writers write (thread, sequence) records. After each publish a writer increments `published`
    // and notify_one()s. A writer whose reserve fails waits on `released` (loaded before reserve())
    // with released.wait(seen). The reader waits on published.wait(seen) when read() is nullopt,
    // reads, releases, increments `released` and notify_all()s. Every thread's sequence is complete
    // and in order. No sleep, no spin. Runs under TSan in CI.
}
```

- [ ] **Step 2: Focused run** — Expected: build error (no `log_ring.h`).
- [ ] **Step 3: Implement** `LogRing`.
- [ ] **Step 4: Focused run** — Expected: all pass.
- [ ] **Step 5: Commit** — `git commit -m "feat(log): the bounded many-writer ring"`

### Task 4: The log thread, asynchronous mode, start/stop/flush

**Files:**
- Modify: `include/dvv/base/log.h`, `src/log.cpp`, `src/log_ring.h`, `src/leak_check.cpp`, `tests/CMakeLists.txt`
- Create: `tests/test_log_async.cpp`

**Interfaces:**
- Consumes: `LogRing` (Task 3); `submit`, `logCall` and the record content (Task 2).
- Produces:
  - `void Logger::start(); void Logger::stop(); void Logger::flush();` (§5.7), serialized by a control
    mutex. `start()` allocates the `LogRing` on first use; a second `start()` does nothing.
  - **`stop()`** takes `mutex_` before it switches the mode to synchronous and holds it until its
    final drain and sink flush: synchronous writers wait behind it, so no sink ever runs on two
    threads at once and one thread's lines keep their order. Then it waits (`atomic::wait`) until the
    in-flight writer count is 0, wakes the log thread with a stop request, joins it, handles any
    record left in the ring on its own thread, and flushes every sink. The log thread never takes
    `mutex_`. **`stop()` always flushes the sinks**, also when the logger was never started (so
    `finishWithLeakCheck` keeps flushing). A second `stop()` only flushes.
  - `flush()`: asynchronous → posts a request, wakes the log thread, waits on an atomic until the
    thread has passed it and flushed the sinks; synchronous → flushes the sinks; re-checks the mode
    under the control mutex; called on the log thread (from a sink) it returns at once.
  - `virtual void ILogSink::batchEnd() {}`
  - `submit`, asynchronous path: increment the in-flight count, re-read the mode (synchronous →
    decrement, take the synchronous path), `reserve` (`urgent` for Warn/Error), encode the record
    (kind Arguments/Text, level, a "warn bare {}" flag set when `bareWarned.exchange(true)` returned
    false, argument count, `system_clock` time, site or location, `LogFormatFn`), `publish`, wake per
    §5.5, decrement (and `notify_all` when it reaches 0). A refused `reserve` increments a per-level
    dropped counter. The log thread emits the bare-`"{}"` Warn right after a record carrying the flag.
  - Log thread `dvv-log` (`pthread_setname_np` on Apple and Linux, `SetThreadDescription` on Windows):
    the §5.5 pass — skip records no sink accepts, format into a kept `std::string`, `formatLogRecord`,
    `write` to accepting sinks, `batchEnd()` on all, then `release`. Before a pass's first record, if
    dropped counters are non-zero, emit `N log messages dropped (E error, W warn, I info, D debug, T trace)`
    (non-zero parts only, `N` their sum) and reset them.
  - Errors: each sink call and each format in `try`/`catch (...)`; the first failure per sink queues
    `a log sink failed: <what>` (Warn, once per sink); a format allocation failure counts as a drop.
  - `addSink`/`clearSinks` while asynchronous: ignored with the R1 Error line.
  - `RotatingFileSink::batchEnd()` flushes its stream (`write` keeps "Warn and above at once").
  - `finishWithLeakCheck` calls `Logger::instance().stop()` where it called `flush()`.
  - Private test hooks in `src/log_ring.h`: `LogRing& loggerRingForTest();` (the Logger's ring, valid
    after `start()`) and `std::uint64_t droppedForTest(LogLevel level);`.

- [ ] **Step 1: Write the failing tests** (`tests/test_log_async.cpp`, each with an `AsyncScope`):

```cpp
TEST_CASE("async: messages reach the sinks in order, lines identical to synchronous mode") {
    // start(); 1000 DVV_LOG_INFO("n {}", i) from one thread; waitFor(1000); lines in order and each
    // equals formatLogRecord of its record; also one "{} {:>4}", one "colour {}" with Colour::Red and
    // one Logger::write with a run-time location print as in the Task 2 sync cases.
}
TEST_CASE("async: bursts with idle gaps from 8 threads lose no wake-up") {
    // 50 rounds: 8 threads each log 20 messages, then all wait (atomic barrier) until the sink has the
    // round's 160 (waitFor); a lost wake-up hangs -> ctest TIMEOUT. No flush().
}
TEST_CASE("async: bare {} warns once per call site") {
    // start(); one bare call site called twice; flush(); exactly 1 warning line, after the first message.
}
TEST_CASE("async: flush returns after earlier messages are in every sink and flushed") {}
TEST_CASE("async: a blocked sink makes Info drop at 3/4 while Warn still queues") {
    // a sink that blocks in write() on an atomic gate; log Info until droppedForTest(Info) > 0 (checked
    // once per call, no waiting); then one Warn; open the gate; flush(); the Warn line is present and
    // "log messages dropped (" with exactly droppedForTest's Info count precedes it.
}
TEST_CASE("async: pool threads logging during stop lose nothing and keep their order") {
    // sink without its own lock (a plain vector) so a concurrent call would be a TSan race; 8 threads
    // log 2000 Warn each (about 1 MB, below the ring); the main thread calls stop() midway; join; every
    // (thread, seq) is present exactly once and each thread's sequence is in order;
    // droppedForTest(Warn) == 0; afterwards a call is handled synchronously.
}
TEST_CASE("async: flush concurrent with stop returns") {}
TEST_CASE("async: start and stop twice are harmless; addSink while running is ignored and logged") {}
TEST_CASE("async: stop without start still flushes the file sink") {}
TEST_CASE("async: a throwing sink loses only its own line and is reported once") {}
TEST_CASE("async: flush called from a sink returns at once") {}
TEST_CASE("async: a text message over the record limit is cut and still appears") {}
TEST_CASE("async: the file sink has the lines on disk after a pass without flush") {
    // RotatingFileSink added before a WaitingSink; waitFor(n) returns after the WaitingSink's
    // batchEnd, which runs after the file sink's; then read the file: all n lines.
}
```

- [ ] **Step 2: Focused run** — Expected: build errors (no `start`).
- [ ] **Step 3: Implement** the interfaces above.
- [ ] **Step 4: Focused run** (it includes `test_log.cpp`'s leak-check case, which exercises
  `stop()` in `finishWithLeakCheck`) — Expected: all pass.
- [ ] **Step 5: Commit** — `git commit -m "feat(log): the log thread, asynchronous mode, start/stop/flush"`

### Task 5: Crash snapshot and its decoder

**Files:**
- Create: `src/log_snapshot.cpp`, `tests/test_log_snapshot.cpp`
- Modify: `include/dvv/base/log.h`, `src/log_ring.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces (§5.8, §6):
  - `using CrashWriter = void (*)(void* context, const void* data, std::size_t size);`
  - `void Logger::writeCrashSnapshot(CrashWriter writer, void* context) const noexcept;`
  - `[[nodiscard]] static constexpr std::size_t Logger::crashSnapshotCapacity() noexcept;`
  - `std::vector<std::string> dvv::decodeCrashSnapshot(std::span<const std::byte> snapshot) noexcept;`
    (returns what it decoded so far if an allocation fails).
  - Private test hook in `src/log_ring.h`: `void resetCrashSnapshotForTest();` (clears the
    one-caller flag).
- Snapshot layout (the builder's beyond this): magic `"DVVLOGS1"`, the dropped counters, the copied
  records from `releasePosition()` up to the first unpublished one (each copied, then kept only if
  `releasePosition()` has not passed it), then one site-table entry per distinct site or text-record
  location (format text, file name, line). The table lives in a buffer reserved by `start()`; the
  first caller takes it with an `std::atomic_flag`, later callers return at once. With no ring (never
  started) it writes the magic and zero records.
- The decoder formats from tags (it cannot run program code): it walks the format text, copies
  literal text (`{{` → `{`, `}}` → `}`), and for each replacement field `{[index][:spec]}` formats the
  stored value of that index (automatic or manual numbering) with
  `std::vformat("{:" + spec + "}", std::make_format_args(value))`, `value` rebuilt from its tag. A
  nested `{}` in a spec, an index out of range, an unknown tag or a `std::format_error` makes that
  line `<format text> | <values joined by ", ">`. Every read is bounds-checked; a truncated record
  ends the decode. Each line is the standard line plus ` (from crash snapshot)`.

- [ ] **Step 1: Write the failing tests** (each starts with `AsyncScope` and
  `resetCrashSnapshotForTest()`, except where stated):

```cpp
TEST_CASE("snapshot: unreleased records decode to the lines the sinks would have written") {
    // start(); a sink blocks in write() on an atomic gate; log 5 messages of mixed kinds (numbers with
    // specs, a string, a text record, a caller-formatted Colour); writeCrashSnapshot into a
    // std::vector; decodeCrashSnapshot == all 5, each "<standard line> (from crash snapshot)" (the
    // blocked one is still unreleased, §5.5); open the gate; stop().
}
TEST_CASE("snapshot: an unpublished record ends the snapshot") {
    // start(); log 1; reserve a record on loggerRingForTest() without publishing; log 2 more;
    // snapshot holds only the first; then publishAsPadding(reservation); stop() completes.
}
TEST_CASE("snapshot: writing allocates nothing") {
    // replacement operator new/delete in all forms (plain, sized, aligned, nothrow, arrays) over
    // malloc/free, counting only while a thread_local flag is set; the count stays 0 across the call.
}
TEST_CASE("snapshot: a second call returns at once without writing") {
    // no reset between the two calls; the second writes 0 bytes.
}
TEST_CASE("snapshot: truncated and random input never crashes the decoder") {
    // every prefix of a real snapshot, and 1000 random byte strings from mt19937 seeded with 1.
}
TEST_CASE("snapshot: after stop it holds no records") {}
```

- [ ] **Step 2: Focused run** — Expected: build errors.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Focused run** — Expected: all pass.
- [ ] **Step 5: Commit** — `git commit -m "feat(log): crash snapshot and its decoder"`

### Task 6: SystemLogSink

**Files:**
- Create: `src/system_log_sink.cpp`
- Modify: `include/dvv/base/log.h`, `CMakeLists.txt`, `tests/test_log.cpp`

**Interfaces:**
- Produces `class dvv::SystemLogSink final : public ILogSink { public: explicit SystemLogSink(std::string identity); ~SystemLogSink() override; void write(const LogRecord&, std::string_view line) override; };`
  - macOS: `os_log_create(identity, "log")` once; `os_log_with_type(log, type, "%{public}.*s", static_cast<int>(size), data)`; level map §5.6.
  - Linux: `openlog(identity_.c_str(), LOG_PID, LOG_USER)` once (the sink owns `identity_`);
    `syslog(priority, "%.*s", static_cast<int>(size), data)`; level map §5.6; the destructor calls
    `closelog()`.
  - Windows: `OutputDebugStringW` of the UTF-16 standard line plus `\n`.

- [ ] **Step 1: Write the failing test:** `TEST_CASE("system log sink accepts every level and a message with % signs")`
  — add the sink, log one message per level including `"100% {}"` with an argument, then remove it;
  it must not crash (the OS log is not read back).
- [ ] **Step 2: Focused run** — Expected: build error.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Focused run** — Expected: pass.
- [ ] **Step 5: Commit** — `git commit -m "feat(log): SystemLogSink per OS"`

### Task 7: Benchmark, docs, 0.3.0 and the full dvv-base suite

**Files:**
- Create: `tests/bench/log_bench.cpp` (executable `dvv_base_log_bench`, `EXCLUDE_FROM_ALL`, no `add_test`)
- Modify: `tests/CMakeLists.txt`, `README.md`, `CHANGELOG.md`, `CMakeLists.txt` (`VERSION 0.3.0`)

- [ ] **Step 1: Benchmark.** Asynchronous mode with one sink that only counts; for 1, 4 and 16
  threads: ns per call for a disabled level, `"{} {} {}"` with three integers, and one 40-byte string;
  prints a table. `cmake --build --preset ninja-macos-release --target dvv_base_log_bench`, then run
  `../_build/dvv-base/ninja-macos/tests/Release/dvv_base_log_bench`; keep the table for the pull request.
- [ ] **Step 2: README.md** "Logging" section: the two modes, `start()`/`stop()`/`flush()`,
  `DVV_LOG_TEXT` and the bare-`"{}"` warning, per-sink levels, the deferrable types and the
  caller-side fallback, `SystemLogSink` per OS, the crash snapshot API, the §5.9 rules.
- [ ] **Step 3: CHANGELOG.md** `## [0.3.0] - <date>`: Added (asynchronous mode, `start`, `stop`,
  `flush` semantics, sink levels, `setSinkLevel`, `batchEnd`, `DVV_LOG_TEXT`, `SystemLogSink`, crash
  snapshot); Changed (macros encode and defer formatting; sinks run on the log thread in asynchronous
  mode; `finishWithLeakCheck` stops the log thread; a bare-`"{}"` call warns once; the gate is closed
  without sinks). `CMakeLists.txt`: `VERSION 0.3.0`.
- [ ] **Step 4: Full dvv-base suite, once** — Expected: all pass, including the package test.
- [ ] **Step 5: Commit** — `git commit -m "docs(log): README, CHANGELOG and version 0.3.0"`

### Task 8: rawreel on dvv-base 0.3.0 (synchronous), the full rawreel suite, then stop

**Branch (rawreel):** `docs/dvv-logger-design` (it holds the spec and this plan; one squashed PR).

**Files (rawreel):** `libs/dvv-base` (submodule pointer), `dvv-deps.txt` (`dvv-base 0.3.0`), and the
10 bare-`"{}"` call sites (R2): `grep -rnE 'DVV_LOG_[A-Z]+\("\{\}"' src` lists them.

- [ ] **Step 1:** `git add libs/dvv-base` (the submodule's worktree is on `feat/queued-logger`, so
  this records its head) and set `dvv-base 0.3.0` in `dvv-deps.txt` (the branch's CMake version is
  0.3.0, so the configure-time minimum check passes).
- [ ] **Step 2:** Rewrite each bare-`"{}"` call as `DVV_LOG_TEXT(dvv::LogLevel::<same level>, <text>)`.
- [ ] **Step 3:** Build rawreel: `cmake --build --preset ninja-macos` — Expected: no warnings or
  errors; `grep -rnE 'DVV_LOG_[A-Z]+\("\{\}"' src` — Expected: no output.
- [ ] **Step 4: Full rawreel suite, once, serially:**
  `DVV_REQUIRE_GPU=1 DVV_REQUIRE_FFMPEG=1 ctest --preset ninja-macos` — Expected: all pass.
- [ ] **Step 5: Commit** (rawreel) — `git commit -m "chore: move to the dvv-base queued logger (synchronous)"`
- [ ] **Step 6: Stop and ask the user** to:
  1. push dvv-base `feat/queued-logger` and open its pull request (CI: TSan, ASan, MSVC; fix any
     finding in new commits, rerunning only the affected tests);
  2. after it is merged and tagged `v0.3.0`: move the rawreel submodule to the tag (`check-pins`
     needs a released tag on `main`), squash, push and open the rawreel pull request.
