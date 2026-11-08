# Session State Notifications Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the GUI's 50 ms status polling and the transport bar's 250 ms scroll hold with
overwriting state notifications and a read-your-writes `target`, with no confirmation channel.

**Architecture:** The Session keeps publishing a `SessionStatus` snapshot at the end of every
`pump()`; it now bumps a `revision` only when the snapshot changed and sets a pending flag on every
`StatusSubscription`, calling a subscription's callback only when its flag was clear. `seek()`
records itself as the newest queued seek and patches the snapshot's `target` on the caller's
thread before it posts, so no snapshot is older than the caller's last seek. The GUI subscribes
once, handles each notification on the GUI thread with `CallAfter`, and the transport bar has only
Idle and Scrubbing.

**Tech Stack:** C++23, wxWidgets 3.3, doctest, CMake presets (`ninja-macos`, `ninja-clang`,
`vs2026`), the dvv libraries at their current pins.

**Spec:** `docs/superpowers/specs/2026-11-08-session-state-notifications-design.md`, revision 2
(cited as "spec §N"). Read §5 (architecture today), §6 and §7 before Task 1.

## Global Constraints

- The Session stays wx-free; subscription callbacks run on the Session executor and only post
  (spec §3, §6.2, D10).
- No confirmation of the GUI's requests in any form: no sequence number, processed count or
  returned timestamp (U-EV6).
- No `wxTimer`, `CallLater`, idle loop or time window in `src/gui` to learn the Session's state.
  The selftest runner's `TICK_MS` timer and the Log pane's 100 ms timer stay (spec §3).
- `ISessionEvents` keeps its four events and one listener (spec §6.5).
- `PlayerStatus::position` keeps its meaning (the frame on screen); the slider follows `target`.
- Seeking stays live on every `THUMBTRACK` and every `CHANGED`; the selftest step "slider to 5"
  (`src/gui/selftest.cpp:1650`) keeps passing unchanged (spec §6.6).
- Lock order (spec §12): subscription list mutex, released before any callback; per-subscription
  callback mutex; `status_mutex_` is never held while a callback runs.
- Warnings-as-errors, clang-format 19.1.7, clang-tidy profile; TSan (`linux-clang-tsan` in CI) and
  ASan clean.
- Commit identity BarabashkaD; no machine paths, real names or personal e-mail in committed files;
  never bypass the pre-push hook. Branch `docs/session-state-notifications` (holds the spec); one
  squashed PR that the user merges.
- Build and test on this Mac: `cmake --build libs/_build/rawreel/ninja-macos --config Debug
  --target <target>`, then `ctest --preset ninja-macos -R '<regex>' --output-on-failure` (run
  `cmake --workflow --preset ninja-macos` once first if the build tree is missing).

## Review Focus

1. Closing the window during playback or during a slow seek: no crash, no hang, no callback after
   a token's destructor. Pinned in Task 2 (`destroying a token while publishes run`, `take on an
   empty token`), Task 5 (`the delay wakes at stop`) and Task 7 (selftest `close during a slow
   seek`).
2. Seeks queued behind a busy executor followed by other commands (a step, Home, Play): snapshots
   must never show an older target than the last seek, and the other command's effect must appear
   in order. Pinned in Task 3 (`a step after queued seeks`).
3. The stream changes while Scrubbing (Ctrl+W, a drop, a reopen after device loss): the bar must
   reset to Idle. Pinned in Task 7 (`stream change resets the scrub`).
4. Slider input that is not a drag (a Windows track click sends `PAGEUP/PAGEDOWN` then
   `CHANGED`; the wheel and keys send `CHANGED` on Windows and GTK; a GTK track click sends
   `THUMBTRACK` then `THUMBRELEASE`; macOS keyboard input sends `THUMBTRACK` with no release): it
   must still seek and must not leave the bar Scrubbing. Pinned in Task 7 (`changed alone seeks
   and stays idle`, `a lone release does nothing`, the mouse-button backstop in `held drag`).
5. Unbounded streams and no stream: the slider stays disabled, `seek()` patches nothing, no scrub
   starts. Pinned in Task 3 (`no patch without a stream`) and Task 7 (`no scrub on a disabled
   slider`).

---

### Task 1: The snapshot gains `target` and `revision`; publish only on change

**Files:**
- Modify: `src/core/player.h:27-33`, `src/core/player.cpp:310-327`
- Modify: `src/core/session_types.h:45-53`, `src/core/session.cpp:697-724` (`publishStatus`)
- Test: `tests/test_player.cpp`, `tests/test_session.cpp`

**Interfaces:**
- Produces: `PlayerStatus::target` (`std::int64_t`, default -1) = `PlayerLogic::target()`;
  `SessionStatus::revision` (`std::uint64_t`, default 0, 1 after the first publish); defaulted
  `operator==` on `PlayerStatus`; `bool sameState(const SessionStatus&, const SessionStatus&)`
  (every field except `revision`) in `session_types.h`. `publishStatus()` returns `bool` (true when
  the stored snapshot changed), used by Task 2.

- [ ] **Step 1: Write the failing tests**

`tests/test_player.cpp`:
```cpp
TEST_CASE("player: status target is the pending seek, else the position") {
    // reset(length 100), ready(0)+takeShow; seek(50) with 50 not ready:
    CHECK(player.status(now).position == 0);
    CHECK(player.status(now).target == 50);
    // ready(50) + takeShow:
    CHECK(player.status(now).position == 50);
    CHECK(player.status(now).target == 50);
}
```
`tests/test_session.cpp` (suite `session`):
```cpp
TEST_CASE("session: target shows a seek before its frame") {
    // open slow:gradient?frames=40&index=30&gate=t1 (closeGate("t1") first), settle(1), seek(30)
    REQUIRE(f.waitStatus([](const SessionStatus& s) { return s.player.target == 30; }));
    CHECK(f.session->status().player.position != 30);
    // releaseGate("t1")
    REQUIRE(f.waitPosition(30));
    CHECK(f.session->status().player.target == 30);
}
TEST_CASE("session: revision changes only with the snapshot") {
    // settle(1); r = status().revision; CHECK(r >= 1)
    // setLoop(false) (already off): then setLoop(true) and waitStatus(loop): the loop command ran
    // after the no-op one (FIFO), so the no-op one is done:
    CHECK(f.session->status().revision == r + 1);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build libs/_build/rawreel/ninja-macos --config Debug --target rawreel_core_tests`
Expected: compile errors on `target`, `revision`.

- [ ] **Step 3: Implement** the fields, `operator==`, `sameState`, and `publishStatus()`: build the
  new snapshot outside the lock as today, then, inside the `status_mutex_` section, set
  `player.target` (from `player_->target()`, -1 without a player; Task 3 extends this choice and it
  must stay inside the lock), compare with `sameState`, store with `revision + 1` only on a
  difference, and return whether it stored.

- [ ] **Step 4: Run them to verify they pass**

Run: `ctest --preset ninja-macos -R 'rawreel\.core_(unit|session)$' --output-on-failure`
Expected: PASS, every existing core test included.

- [ ] **Step 5: Commit**

```bash
git add src/core/player.h src/core/player.cpp src/core/session_types.h src/core/session.cpp tests/test_player.cpp tests/test_session.cpp
git commit -m "feat(core): status target and revision, published only on change"
```

### Task 2: Status subscriptions

**Files:**
- Modify: `src/core/session.h`, `src/core/session.cpp` (subscription list in `SessionCore`;
  fan-out after `publishStatus()` returned true, outside `status_mutex_`)
- Create: `tests/test_session_notify.cpp` (doctest suite `session`)
- Modify: `tests/CMakeLists.txt:36-42` (add the file to `rawreel_core_tests`)

**Interfaces:**
- Consumes: Task 1's `bool publishStatus()`.
- Produces (`src/core/session.h`):
```cpp
class StatusSubscription {  // move-only; default-constructed = empty
public:
    StatusSubscription() noexcept = default;
    StatusSubscription(StatusSubscription&&) noexcept;
    StatusSubscription& operator=(StatusSubscription&&) noexcept;  // unsubscribes the old one
    ~StatusSubscription();  // spec §6.2 binding; never from inside its own callback
    [[nodiscard]] SessionStatus take();  // clear pending, then read; default when empty or gone
    [[nodiscard]] explicit operator bool() const noexcept;
};
// Session: any thread. `changed` runs on the Session executor, carries nothing, only posts.
[[nodiscard]] StatusSubscription subscribeStatus(dvv::MoveOnlyFunction<void()> changed);
```
The token holds a `weak_ptr` to `SessionCore`.

Test helper (in `test_session_notify.cpp`): `CallCounter` = an atomic count plus a mutex and a
condition variable; `bool waitCount(std::size_t n, std::chrono::milliseconds timeout)`. Counts are
always awaited, never read right after a `waitPosition` (the snapshot is stored before the fan-out
runs).

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("notify: one callback per burst until take") {
    // subscribe; seek(5); seek(6); seek(7); waitPosition(7)
    REQUIRE(counter.waitCount(1, WAIT_TIMEOUT));
    // let the executor go idle: settle(0); then 50 ms
    CHECK(counter.count() == 1);
    (void)sub.take(); f.session->seek(8);
    REQUIRE(counter.waitCount(2, WAIT_TIMEOUT));
}
TEST_CASE("notify: take clears before it reads") {
    // after take() returned target 7, seek(9): waitCount(2); sub.take().player.target == 9
}
TEST_CASE("notify: an unchanged snapshot calls nothing") {
    // settle(1); take(); counter reset; setLoop(false) twice (already off: no change); then a
    // FIFO barrier: a second subscription `barrier`, setLoop(true), barrier.waitCount(1) (the
    // loop command ran after the no-op ones). The counter saw that change exactly once:
    CHECK(counter.count() == 1);
}
TEST_CASE("notify: subscribers are independent") {
    // a, b; seek(5); waitCount both 1; a.take(); seek(6); a.waitCount(2)
    CHECK(b.count() == 1);  // b still pending
}
TEST_CASE("notify: destroying a token while publishes run") {
    // RealTime fixture, play() on a 40-frame loop; 500 iterations: per iteration a fresh
    // shared `Probe{std::atomic<bool> destroyed, violated}`; subscribe with a callback that only
    // sets probe->violated if probe->destroyed is set (no FAIL/CHECK on the executor thread);
    // the test thread calls take() in a loop for 0-2 ms (re-arming, so the callback really races
    // the destructor), destroys the token, sets probe->destroyed. After the loop, on the test
    // thread: CHECK_FALSE(any probe violated).
}
TEST_CASE("notify: a token that outlives the session") {
    // subscribe; f.shutdown(); f.session.reset(); CHECK(sub.take().revision == 0); token dtor ok
}
TEST_CASE("notify: take on an empty token") {
    StatusSubscription empty;
    CHECK_FALSE(empty);
    CHECK(empty.take().revision == 0);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build libs/_build/rawreel/ninja-macos --config Debug --target rawreel_core_tests`
Expected: compile errors on `subscribeStatus` / `StatusSubscription`.

- [ ] **Step 3: Implement** a per-subscription shared state `{std::atomic<bool> pending;
  std::mutex callback_mutex; MoveOnlyFunction<void()> changed; bool removed;}`. Fan-out: copy the
  list of states under the list mutex, release it, then for each `if (!pending.exchange(true))` lock
  its `callback_mutex`, skip if `removed`, call. Destructor: remove from the list under the list
  mutex, release it, lock `callback_mutex`, set `removed`.

- [ ] **Step 4: Run them to verify they pass, also under ASan**

Run: `ctest --preset ninja-macos -R 'rawreel\.core_session$' --repeat until-fail:20 --output-on-failure`, then `cmake --workflow --preset ninja-macos-asan`.
Expected: PASS on every repeat and under ASan.

- [ ] **Step 5: Commit**

```bash
git add src/core/session.h src/core/session.cpp tests/test_session_notify.cpp tests/CMakeLists.txt
git commit -m "feat(core): status subscriptions with an overwriting notification"
```

### Task 3: Read-your-writes `target` for seeks (spec §6.3)

**Files:**
- Modify: `src/core/session.h:117` (`seek` comment), `src/core/session.cpp` (`Session::seek`,
  `SessionCore` queued-seek fields under `status_mutex_`, the seek command body,
  `publishStatus()`)
- Modify: `tests/session_fixture.h`, `tests/session_fixture.cpp` (the `ExecutorHold` helper)
- Test: `tests/test_session_notify.cpp`

**Interfaces:**
- Consumes: Task 1's `target`, `revision`; Task 2's `subscribeStatus`.
- Produces: no new public API. Behaviour (spec §6.3):
  - `Session::seek(index)`, all under `status_mutex_`: post the seek's own lambda first; then, if
    the stored snapshot has `streams > 0` and a bounded `length > 0`: `queued_seek_ =
    clamp(index, 0, length − 1)`, `++queued_seeks_`, and, if `status_.player.target` differs,
    set it and `++status_.revision`. The lambda captures `bool counted`.
  - The seek's lambda is posted directly on the executor (not through `Session::command`, whose
    `stopping()` check comes first): under `status_mutex_` it does `if (counted) --queued_seeks_`,
    then `if (core->stopping()) return;`, then `player_->seek(index)` (when a player exists) and
    `pump()`.
  - `publishStatus()`, inside its locked section: `target = (queued_seeks_ > 0 && player_ &&
    !stopping_) ? queued_seek_ : (player_ ? player_->target() : -1)`; then the compare and store of
    Task 1; it returns true (fan out) when it stored **or** when `status_.revision !=
    notified_revision_`, and sets `notified_revision_ = status_.revision` in that case.
- Test helper (fixture): `class ExecutorHold { public: explicit ExecutorHold(SessionFixture& f);
  void release(); ~ExecutorHold(); };` — preconditions: a bounded stream is open and loop is off
  (REQUIRE both). It subscribes with a callback that blocks on a latch the first time it runs,
  forces one publish with `setLoop(true)`, and waits for the callback to be entered with
  `WAIT_TIMEOUT` (REQUIRE). `release()` opens the latch. Destructor order: release (if not yet),
  destroy the token (its destructor takes the callback mutex the blocked callback held), then
  `setLoop(false)`. Test only: it breaks the "post only" contract on purpose to queue commands
  behind a busy executor.
- Recorder (tests): a subscription whose callback calls its own `take()` (re-arming; safe here, the
  token outlives every publish of the test) and appends `take().player.target` to a mutex-guarded
  vector; every test that uses it first REQUIREs the record to be non-empty.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("ryw: a seek is the target as soon as seek returns") {
    // open 40 frames, settle(1); ExecutorHold hold(f);
    f.session->seek(7);
    CHECK(f.session->status().player.target == 7);   // the executor has not run it
    hold.release(); REQUIRE(f.waitPosition(7));
}
TEST_CASE("ryw: snapshots while seeks are queued show the newest seek") {
    // recorder (re-arming, above); ExecutorHold hold(f); recorder cleared;
    // seek(5); seek(7); hold.release(); waitPosition(7); settle(1)
    REQUIRE_FALSE(recorded.empty());
    CHECK(std::ranges::all_of(recorded, [](std::int64_t t) { return t == 7; }));
}
TEST_CASE("ryw: a patched seek is notified when the executor snapshot equals it") {
    // slow:gradient?frames=40&index=20&gate=g20 (gate closed); seek(20) (20 now outstanding),
    // seek(0); waitPosition(0); settle(0) is not possible (20 is gated): wait until
    // status().inflight == 1. Subscriber S: take() (pending clear).
    // ExecutorHold hold(f); seek(20) (patch: target 20); S.take() clears again; hold.release()
    // The executor's snapshot after seek(20) equals the patched one (20 already outstanding):
    REQUIRE(S.waitCount(1, WAIT_TIMEOUT));
    CHECK(S.take().player.target == 20);
    // releaseGate("g20"); waitPosition(20)
}
TEST_CASE("ryw: a seek refused during shutdown leaves no queued target") {
    // ExecutorHold hold(f); std::thread t([&] { f.shutdown(); }); sleep 50 ms (its first step is
    // queued behind the hold); seek(5) (patched, counted, queued after the shutdown step);
    // hold.release(); t.join()
    CHECK(f.session->status().player.target == -1);
    CHECK(f.session->status().streams == 0);
}
TEST_CASE("ryw: a step after queued seeks") {
    // ExecutorHold; seek(5); seek(7); step(+1); release; waitPosition(8)
    // recorded targets after the release: only 7 then 8, never 5, never back to 7 after 8
}
TEST_CASE("ryw: the call-time clamp") {
    // 40 frames; ExecutorHold; seek(1000): CHECK(status().player.target == 39); seek(-5): == 0
}
TEST_CASE("ryw: no patch without a stream") {
    // no open; REQUIRE(f.waitStatus([](const SessionStatus& s) {
    //     return s.gpuAvailable && s.revision >= 1; }));  (the first publish is done)
    // r = status().revision; seek(5)
    CHECK(f.session->status().player.target == -1);
    CHECK(f.session->status().revision == r);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build libs/_build/rawreel/ninja-macos --config Debug --target rawreel_core_tests && ctest --preset ninja-macos -R 'rawreel\.core_session$' --output-on-failure`
Expected: FAIL on the immediate-target, queued-seeks, step, clamp and patched-seek-notified tests
(`target` is still the old value while the executor is held; nothing is notified for an equal
snapshot). "No patch without a stream" and "a seek refused during shutdown" pass already: they pin
guards the implementation must keep.

- [ ] **Step 3: Implement** as in Interfaces.

- [ ] **Step 4: Run them to verify they pass**

Run: `ctest --preset ninja-macos -R 'rawreel\.core_(unit|session)$' --repeat until-fail:10 --output-on-failure`
Expected: PASS on every repeat.

- [ ] **Step 5: Commit**

```bash
git add src/core/session.h src/core/session.cpp tests/session_fixture.h tests/session_fixture.cpp tests/test_session_notify.cpp
git commit -m "feat(core): a seek is the published target from the moment it is accepted"
```

### Task 4: Tests wait on the subscription, not by polling (C6)

**Files:**
- Modify: `tests/session_fixture.h:172-176`, `tests/session_fixture.cpp:365-388`

**Interfaces:**
- Consumes: Task 2.
- Produces: `waitStatus`, `waitPosition`, `settle` keep their signatures and timeouts.

- [ ] **Step 1: Replace the 1 ms loop.** Pattern: a local `{std::mutex m; std::condition_variable
  cv; bool woke = false;}` shared with the callback through a `shared_ptr`; the callback locks `m`,
  sets `woke`, notifies. Loop: `if (predicate(sub.take())) return true;` then wait on `cv` for
  `woke` until the deadline, clear `woke` under the lock, repeat. The token is a local declared
  after the shared state and destroyed before the function returns, never while `m` is held.
  Update the header comment.

- [ ] **Step 2: Run the whole core suite**

Run: `ctest --preset ninja-macos -R 'rawreel\.core_' --output-on-failure`
Expected: PASS; total time not longer than the Task 3 run beyond noise.

- [ ] **Step 3: Commit**

```bash
git add tests/session_fixture.h tests/session_fixture.cpp
git commit -m "test(core): waitStatus waits on a status subscription"
```

### Task 5: Developer options `--debug-seek-delay` and `--debug-transport` (C11, C12)

**Files:**
- Modify: `src/core/gui_arguments.h`, `src/core/gui_arguments.cpp`, `tests/test_gui_arguments.cpp`
- Create: `src/core/delayed_source.h`, `src/core/delayed_source.cpp` (+ `CMakeLists.txt` next to
  `status_text.cpp`, line 58)
- Modify: `src/core/session_types.h` (`ProduceDelay`), `src/core/session.h` (`SessionOptions`),
  `src/core/session.cpp:853-868` (`openSource`)
- Modify: `src/gui/app.h`, `src/gui/app.cpp:182-196` and its close path (`stop()` before the
  Session shutdown)
- Modify: `tests/session_fixture.h`, `tests/session_fixture.cpp` (an options hook)
- Modify: `README.md` (usage block, around line 220)
- Test: `tests/test_gui_arguments.cpp`, `tests/test_session.cpp`

**Interfaces:**
- Produces:
  - `GuiArguments::debugSeekDelay` (`std::uint32_t` ms, 0 = off; `--debug-seek-delay MS`, 1 to
    60000) and `GuiArguments::debugTransport` (`bool`; `--debug-transport`, a flag, no value).
    Errors: `"bad number '<v>' for --debug-seek-delay (1 to 60000)"`, `"option
    --debug-seek-delay given twice"`, `"option --debug-transport given twice"`, `"option
    --debug-seek-delay needs a value"`.
  - `USAGE` lines: `  --debug-seek-delay MS  developer: every frame takes MS ms longer to load`
    and `  --debug-transport  developer: log the transport bar's notifications`.
  - In `session_types.h`:
```cpp
class ProduceDelay {  // thread-safe; shared by the GUI and the delayed sources
public:
    void set(std::uint32_t ms) noexcept;
    [[nodiscard]] std::uint32_t get() const noexcept;
    void stop() noexcept;          // wakes every wait now and makes later waits return at once
    void waitFor(std::uint32_t ms);  // returns after ms, or at once / early after stop()
};
```
    `SessionOptions::produceDelay` (`std::shared_ptr<ProduceDelay>`, null: no wrapping).
  - `std::unique_ptr<dvv::media::ISource> delayedSource(std::unique_ptr<dvv::media::ISource>,
    std::shared_ptr<ProduceDelay>, std::shared_ptr<dvv::IExecutor> io)`: `produce` reads `get()`
    once; when it is 0 it forwards directly (no hop, so the perf scenario is unchanged); otherwise
    it hops to `io`, calls `waitFor(ms)`, then forwards. `info()` forwards.
  - `explicit SessionFixture(RealTime, std::function<void(SessionOptions&)> configure)`: applies
    `configure` to `options()` before `Session::create`.
  - `ProduceDelay* App::produceDelay()`: created when `debugSeekDelay > 0` or `--selftest`, set to
    `debugSeekDelay`. `stop()` is the **first statement** of `App::shutdownSession()`
    (`src/gui/app.cpp:332`), before `stopFfmpegSources()` and `bridge_->detach()`, so the Session's
    shutdown drain never waits out a delay.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("gui arguments: --debug-seek-delay") {
    // "--debug-seek-delay 800" -> 800; default 0; "0", "60001", "x" -> the bad-number error;
    // twice -> given twice; last argument -> needs a value
}
TEST_CASE("gui arguments: --debug-transport") {
    // "--debug-transport URI" -> true and uri == URI; twice -> given twice
}
// "gui arguments: the usage text": both new lines present.
TEST_CASE("session: produce delay slows a seek") {
    // SessionFixture f(RealTime{}, [&](SessionOptions& o) { o.produceDelay = delay; });
    // delay->set(300); open 400 frames; settle(1)
    // t0; seek(200); waitPosition(200); CHECK(elapsed >= 300 ms)
    // delay->set(0); settle(0) (inflight == 0: the delayed prefetches are done)
    // t1; seek(350); waitPosition(350); CHECK(elapsed < 300 ms)
}
TEST_CASE("session: the delay wakes at stop") {
    // delay->set(10000); open; seek far; then delay->stop(); f.shutdown() returns within 2 s
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build libs/_build/rawreel/ninja-macos --config Debug --target rawreel_core_tests`
Expected: compile errors on the new members.

- [ ] **Step 3: Implement** the parser (the flag branch before the needs-a-value check),
  `ProduceDelay` (mutex + condition variable + `stopped`), `delayedSource`, the wrap in
  `openSource` on the `std::unique_ptr<ISource>` alternative of `OpenResult` when
  `produceDelay` is set, the fixture hook, the App wiring and the README usage block.

- [ ] **Step 4: Run them to verify they pass**

Run: `ctest --preset ninja-macos -R 'rawreel\.core_(unit|session)$' --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/gui_arguments.h src/core/gui_arguments.cpp src/core/delayed_source.h src/core/delayed_source.cpp src/core/session_types.h src/core/session.h src/core/session.cpp src/gui/app.h src/gui/app.cpp CMakeLists.txt README.md tests/test_gui_arguments.cpp tests/test_session.cpp tests/session_fixture.h tests/session_fixture.cpp
git commit -m "feat: developer options --debug-seek-delay and --debug-transport"
```

### Task 6: The GUI updates on notifications; the status timer goes

**Files:**
- Modify: `src/gui/main_frame.h:155` and members; `src/gui/main_frame.cpp:56`, `:178`, `:259`,
  `:321-322`, `:856-873`, `:956`
- Modify: `src/gui/app.cpp` (after `session_` is set, before `frame->Show()`)
- Modify: `src/gui/selftest.cpp` (two steps after "slider to 5")

**Interfaces:**
- Consumes: Task 2; Task 5's `GuiArguments::debugTransport`.
- Produces:
  - `void MainFrame::startStatusUpdates(Session& session)`: subscribes; the callback posts
    `onStatusChanged()` with `CallAfter` on the frame. It also posts one `onStatusChanged()`
    itself, so the snapshot published before the subscription existed is shown.
  - `void MainFrame::stopStatusUpdates()`: destroys the token; called where `status_timer_.Stop()`
    was (`:259` destructor, `:956`).
  - `void MainFrame::onStatusChanged()`: returns at once when the token is empty or `closing_`
    (as `onStatusTimer` did, `:858`); else `take()`; if its `revision` equals the last one handled,
    return; else what `onStatusTimer` did. Records `last_revision_`.
  - `void MainFrame::setDebugTransport(bool on)`; when on, `onStatusChanged` logs (Info)
    `transport: notification revision {r} position {p} target {t} playing {0|1}`.
  - `[[nodiscard]] std::uint64_t MainFrame::statusNotificationsForTest() const noexcept` (handled,
    non-duplicate notifications) and `[[nodiscard]] std::uint64_t lastRevisionForTest() const
    noexcept`.

- [ ] **Step 1: Write the failing selftest steps**

```cpp
// "paused: no status work": pause; predicate A: status().inflight == 0 &&
// frame.lastRevisionForTest() == status().revision (every queued notification is handled);
// then n0 = counter; predicate B latches `grew` in the step state the first time counter != n0
// and passes only when 1 s (steady clock) has passed with `grew` never set (the default 5 s step
// deadline then fails it if it grew).
// "play updates the bar": n0 = counter; play; predicate: frame.statusTexts()[0] changed and
// counter > n0 within 2 s; then pause.
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build libs/_build/rawreel/ninja-macos --config Debug --target rawreel_gui`
Expected: compile errors on `statusNotificationsForTest` / `lastRevisionForTest`.

- [ ] **Step 3: Implement**: remove `status_timer_`, `STATUS_TICK_MS`, the timer part of
  `buildStatusBar()` and `onStatusTimer`; add the members; App calls `startStatusUpdates` and
  `setDebugTransport`. The transport bar keeps its current `update()` in this task.

- [ ] **Step 4: Run all GUI selftests**

Run: `ctest --preset ninja-macos -R 'rawreel\.gui_' --output-on-failure` and
`grep -rn "wxTimer\|CallLater" src/gui`.
Expected: PASS; grep lists only `selftest.cpp`, `selftest.h` (the runner) and `log_panel.cpp`,
`log_panel.h` (spec §3 non-goal).

- [ ] **Step 5: Commit**

```bash
git add src/gui/main_frame.h src/gui/main_frame.cpp src/gui/app.cpp src/gui/selftest.cpp
git commit -m "feat(gui): status bar and transport bar update on notifications"
```

### Task 7: Transport bar Idle / Scrubbing; the scroll hold goes

**Files:**
- Modify: `src/gui/transport_bar.h`, `src/gui/transport_bar.cpp` (remove `SCROLL_HOLD`,
  `last_scroll_`)
- Modify: `src/gui/main_frame.cpp` (`setDebugTransport` forwards to the bar)
- Modify: `src/gui/selftest.cpp` (`sendSliderChanged`, `sendSliderRelease`, new steps)

**Interfaces:**
- Consumes: Task 3's read-your-writes `target`; Task 5's `App::produceDelay()`; Task 6.
- Produces:
  - `enum class ScrubState : std::uint8_t { Idle, Scrubbing };`
    `[[nodiscard]] ScrubState TransportBar::scrubState() const noexcept;`
  - `void TransportBar::setTrace(bool on)`; trace line (Info): `transport: scrub {from} -> {to}`.
  - Seams: `void setLeftButtonProbeForTest(dvv::MoveOnlyFunction<bool()> probe)` (default
    `wxGetMouseState().LeftIsDown()`, C7); `void setUpdateObserverForTest(
    dvv::MoveOnlyFunction<void(int value)> observer)` (called at the end of every `update()` with
    the slider's value).
  - Selftest helpers next to `sendSliderTrack` (`selftest.cpp:1480`): `sendSliderChanged(wxSlider&,
    int)` (`wxEVT_SCROLL_CHANGED`) and `sendSliderRelease(wxSlider&)`
    (`wxEVT_SCROLL_THUMBRELEASE`).

Behaviour (spec §6.6): every scroll event while `max_ < 0` (disabled slider) is ignored.
Otherwise: `THUMBTRACK` → `Scrubbing`; `THUMBTRACK` and `CHANGED` → `seek(value)` unless `value ==
last_sent_`, then `last_sent_ = value`; `THUMBRELEASE` or `CHANGED` in `Scrubbing` → `Idle`;
`THUMBRELEASE` in `Idle` → nothing; `update()` in `Scrubbing` with the probe false → `Idle`.
`update()`: first, a range change (`max_` changes) → `Idle`, `last_sent_ = -1`; then in
`Scrubbing` leave the value alone; in `Idle` set `last_sent_ = -1` and the value to `target`
(clamped, as today's `position`). The label keeps `position`.

- [ ] **Step 1: Write the failing selftest steps** (the player clip has 60 frames)

```cpp
// After Task 6's steps:
// "held drag on a slow source": produceDelay()->set(500); probe -> true; observer records;
// sendSliderTrack 25, 40, 55; predicate 1: 2 s pass with scrubState() == Scrubbing and every
// value recorded after the 55 was sent == 55; probe -> false; sendSliderRelease;
// predicate 2: status().position == 55, scrubState() == Idle, every recorded value == 55;
// then set(0), reset probe and observer.
// "changed alone seeks and stays idle": sendSliderChanged 20; predicate: position == 20,
// scrubState() == Idle.
// "a lone release does nothing": sendSliderRelease in Idle; predicate: scrubState() == Idle and
// position unchanged after 3 ticks.
// Capture the clip URI once: in the existing "open one stream" step (selftest.cpp:1592), when its
// predicate passes, store stream_uri() in a shared `clip_uri` (later steps may have replaced or
// closed the stream: stream_uri() is "" after a close, selftest.cpp:1585).
// Immediately BEFORE the existing "close stream and clean up" (selftest.cpp:2552), insert:
// "reopen the clip": frame.openUri(0, *clip_uri, {}); predicate: one stream,
// status().player.length == 60, status().player.position == 0.
// "stream change resets the scrub": probe -> true; sendSliderTrack 30; predicate 1:
// scrubState() == Scrubbing; then execute(CloseStreams); predicate 2: scrubState() == Idle and
// the slider is disabled; reset the probe.
// "no scrub on a disabled slider": sendSliderTrack 10; predicate: scrubState() == Idle and no
// stream was opened (frame.streams().empty()).
// The existing "close stream and clean up" then runs unchanged (its CloseStreams is a no-op).
// AFTER it, as the scenario's very last two steps (read what "close stream and clean up" checks
// first; if its checks require being last, the builder rules on the order and ledgers it):
// "reopen the clip for the close": as "reopen the clip".
// "close during a slow seek": produceDelay()->set(600000) (the test API is not bounded by the
// parser's 60000; a missing stop() would then exceed the CTest TIMEOUT of 180 s);
// sendSliderTrack 45; predicate: true at once. The scenario ends and the app closes with the seek
// pending; stop() in App::shutdownSession() must wake the delayed produce.
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build libs/_build/rawreel/ninja-macos --config Debug --target rawreel_gui`
Expected: compile errors on `scrubState`, `setLeftButtonProbeForTest`, `sendSliderRelease`.

- [ ] **Step 3: Implement** the state, the bindings (`wxEVT_SCROLL_THUMBRELEASE` added; no
  `MOUSE_CAPTURE_LOST`, which never reaches a native slider), the trace, the seams.

- [ ] **Step 4: Run all GUI and core tests**

Run: `ctest --preset ninja-macos -R 'rawreel\.(gui_|core_)' --output-on-failure` and
`grep -n "SCROLL_HOLD\|last_scroll_" src/gui/transport_bar.*`.
Expected: PASS ("slider to 5" included); grep finds nothing.

- [ ] **Step 5: Commit**

```bash
git add src/gui/transport_bar.h src/gui/transport_bar.cpp src/gui/main_frame.cpp src/gui/selftest.cpp
git commit -m "feat(gui): explicit scrub state in the transport bar; no scroll hold"
```

### Task 8: Measure, supersede the old text, hand over the manual check

**Files:**
- Modify: `docs/superpowers/specs/2026-10-19-rawreel-design.md:207`, `:1242`, `:2399` (note after
  each "polled every 50 ms": "superseded by `2026-11-08-session-state-notifications-design.md`")
- Modify: `docs/superpowers/plans/2026-10-23-m3-player.md:7154` (the same note on the 250 ms hold)
- Modify: `docs/superpowers/specs/2026-11-08-session-state-notifications-design.md` (status line;
  "Results" under §9a)

- [ ] **Step 1: Measure the notification rate (spec §9, §9a scenario 10).** First add to Task 6's
  trace (still under `--debug-transport`, no timer): `onStatusChanged` counts every entry (before
  the revision check) per steady-clock second, and the first entry in a new second logs
  `transport: rate {n} notifications/s` for the previous one. Then, Release build:
  `"$bin/rawreel-gui.app/Contents/MacOS/rawreel-gui" --debug-transport
  'synthetic:gradient?fmt=yuv420p10le&size=3840x2160&frames=600&fps=60'`, play 10 s, read the ten
  `transport: rate` lines in `rawreel.log`. (The per-notification lines also print with the flag;
  their logging cost is part of the measurement, so note it beside the result.)
  Expected: at most 120 a second on average. If above: add Task 8a in this plan's style (a
  `StatusFilter { All, Visible }` argument to `subscribeStatus`, default `All`; the GUI passes
  `Visible`; a core test that `Visible` ignores an `inflight`-only change and `All` does not)
  before continuing.

- [ ] **Step 2: Write the Results subsection**: the measured rate, the machine, and the list of
  §9a scenarios marked "for the user to run" (they need a person at the mouse; the agent does not
  run them).

- [ ] **Step 3: Add the superseded notes and the status line; run the full workflow**

Run: `cmake --workflow --preset ninja-macos`. Expected: every test passes.

- [ ] **Step 4: Commit**

```bash
git add docs/
git commit -m "docs: notifications measured; supersede the polling text"
```

---

## Self-review notes

- Spec coverage: §6.2 → Task 2; §6.3 → Tasks 1, 3; §6.5 → Global Constraints; §6.6 → Tasks 6, 7;
  §6.7 → Tasks 2, 3, 4, 6, 7; §8 done → Tasks 6, 7, 8; §9 rate → Task 8 (8a conditional); §9a →
  Tasks 5 (options), 8 (handover); C1 → Task 1; C5 → Task 1; C6 → Task 4; C7 → Task 7; C8 → 8a;
  C9 → unchanged player behaviour, visible in Task 7's Idle rule; C10 → Task 3; C11, C12 → Task 5.
- Fixes from the revision-2 plan reviews: patches always fanned out (`notified_revision_`, Task
  3); target chosen inside the locked store and null-player safe (Tasks 1, 3); a refused seek
  still decrements (Task 3); post before patch (Task 3); re-arming recorder and non-empty records
  (Task 3); first publish awaited (Task 3); ExecutorHold preconditions, timeout and destructor
  order (Task 3); stress test without assertions on the executor thread (Task 2); FIFO barrier in
  "unchanged snapshot" (Task 2); `settle(0)` (Task 2); delay without a hop at 0 and `stop()` first
  in `shutdownSession` (Task 5); first snapshot posted, early return when closing, latched idle
  predicate, grep list (Task 6); `last_sent_` forgotten in Idle, disabled slider ignored, final
  steps placed before the clean-up with a captured clip URI, 600 s close delay (Task 7);
  per-second rate line (Task 8); platform event facts (Review Focus 4, spec §6.6).
- Review fixes carried from the revision-1 plan reviews: CHANGED seeks (B2); no list filter (B1
  and the command cases, now impossible by construction); counters awaited, not read
  (Task 2); lost wake-ups (Task 4); fixture options hook and interruptible delay (Task 5); idle
  counter sampled after the last revision is handled (Task 6); `take()` on an empty token
  (Task 2); grep expectation (Task 6); `fps=60` (Task 8); manual check for the user (Task 8); no
  `MOUSE_CAPTURE_LOST` (Task 7).
