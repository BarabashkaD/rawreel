# Control Panel Revision 4 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the drag cancel with skimming on the slider while a key is held (⌥ Option on
macOS, Ctrl on Windows and Linux): the thumb and the picture follow the pointer, a click moves
there, releasing the key returns. A click on the slider jumps to the clicked frame everywhere.

**Architecture:** The drag cancel comes out of `TransportBar`. A wx-free `frameAt` (the inverse of
`thumbX`) maps the pointer to a frame. The bar adds a `Skimming` state driven only by the slider's
own mouse events, the window's activation and the skim key's release, which a small
`wxEventFilter` forwards (a `KEY_UP` goes only to the focused window). Nothing outside the slider
reacts to the key. On Windows the bar handles a channel click itself instead of the trackbar's
page step.

**Tech Stack:** C++23, wxWidgets 3.3.4, doctest, CMake presets (`ninja-macos`; CI: Linux
clang/TSan/ASan/tidy, Windows MSVC, macOS).

**Spec:** `docs/superpowers/specs/2026-11-09-video-control-panel-design.md`, revision 4: §2
(U-CP7, U-CP8, U-CP9), §3, §6 (normative), §9, §9a, §10 stage 6, §11, §14.2.

## Global Constraints

- No timer and no polling in product code; every transition is an event of spec §6.2.
- Skim key: on macOS (`__WXOSX__`) `AltDown()` / `WXK_ALT` (⌥ Option; `ControlDown()` is ⌘
  there); elsewhere `ControlDown()` / `WXK_CONTROL`. No toggle key. Paused only.
- Only the slider reacts to the skim key. Keys, menus and buttons keep their native meaning
  (⌥/Ctrl+Space is unbound and does nothing). No drag cancel; Escape unused. No double-click.
- U-EV6: no acknowledgement, echo, counter or timestamp of GUI requests; a skim's return is an
  ordinary `seek()`.
- Posted calls use the bar's own `CallAfter`, never `wxTheApp->CallAfter`.
- Naming per the repo `.clang-tidy` (public members camelBack, private lower_case with `_`,
  parameters and locals lower_case, functions camelBack, constants UPPER_CASE).
- Warnings are errors; clang-format 19.1.7; clang-tidy clean on touched files (compile database
  plus `-isysroot` of the macOS SDK).
- Selftests in the `player` scenario; real input filtered; every synthetic event through
  `injectInput`; crafted statuses restored with
  `BarStatus{.player = s.player, .hasStream = s.streams > 0, .streamGeneration = s.streamGeneration}`.
- Commit identity BarabashkaD; no machine paths or personal data in committed files. Branch
  `docs/video-control-panel`. README: facts only.
- Build and test: `cmake --build libs/_build/rawreel/ninja-macos --config Debug --target <t>`,
  `ctest --preset ninja-macos -R '<regex>' --output-on-failure`; the full suite at each task end.
  Only macOS compiles here; MSW and GTK code is checked by CI after the user's push.

## Review Focus

1. **The skim key held while pressing the thumb** (Windows/GTK send a motion with the button down
   before the first `THUMBTRACK`): no skim may start inside a native drag. Pinned in Task 3
   (`a motion with the button down does not enter`).
2. **The skim key's release with the pointer still**: returns at once, without a motion. Pinned in
   Task 3 (`the key's release returns`), sent to a non-frame window so only the filter can see it.
3. **A scroll event while skimming** (a click whose `LEFT_DOWN` did not reach the bar, ⌥/Ctrl+wheel):
   it keeps the skimmed frame, then is handled as in Idle. Pinned in Task 3 (`a scroll event while
   skimming keeps`).
4. **A Ctrl shortcut that opens a dialog while skimming** (Ctrl+O/U on Windows/Linux): the window
   deactivates and the skim returns. Pinned in Task 3 (`deactivation returns`).
5. **The skim key held in another application while the pointer crosses rawreel's slider**: no
   skim starts (its release would never arrive). Pinned in Task 3 (`no skim while inactive`).
   Destroying the bar with a posted call pending stays pinned by Task 1 (`a pending call after the
   bar is gone`); the destructor's unbinding is checked by review, not by a selftest (synthetic
   events cannot reach a dying bar).

---

### Task 1: Remove the drag cancel

**Files:**
- Modify: `src/gui/transport_bar.h/.cpp` (drop `ScrubState::Cancelled`, `cancelScrub`,
  `leaveCancelled`, `leave_posted_`, `leaveCancelledRunsForTest`; keep `frameActivated` as a hook
  with an unnamed parameter; `onSliderLeftDown` only `Skip()`s; the `update()` button-up backstop
  applies to Scrubbing only)
- Modify: `src/gui/main_frame.cpp` (remove the Escape check in `onCharHook`; keep `onActivate`)
- Modify: `tools/panel_lab/panel_lab.cpp` (remove the Escape lambda; keep the activation lambda)
- Modify: `src/gui/selftest.cpp`

**Interfaces:**
- Produces: `enum class ScrubState : std::uint8_t { Idle, Scrubbing };`;
  `[[nodiscard]] static int forgetReleaseRunsForTest() noexcept;` (every run of
  `forgetRelease`'s body); `void frameActivated(bool /*active*/);`.

- [ ] **Step 1: Change the tests first**
  - Delete the steps `escape seeks back`, `deactivation cancels`, `cancelled ends without a
    release`, `a range change while cancelled`, `keys during a drag`, `a cancel at the original
    frame`, `play at the original frame`, `then cancel`, each with the `idle_at(...)` step just
    before it, and the `activate` helper. Keep `current_label`, `has_marker` and `back_at_10`
    (Task 3 reuses them).
  - `escape outside a drag` becomes `escape does nothing` (preceded by one `idle_at(..., 10)`): in
    Idle `sendKey(frame, WXK_ESCAPE)` returns false and seeks nothing; during a held drag (probe
    down, track to 30) Escape returns false and the bar stays Scrubbing; after the release pair the
    bar is Idle at 30 with seeks `30` only.
  - `a pending call after the bar is gone`: first a positive control on a live temporary bar
    (probe down, track to 20, probe up, the release pair, two yields: `forgetReleaseRunsForTest()`
    grows by 1 — only a drag's release posts it); then a second temporary bar with the same drag
    and release pair, `bar->Destroy()` in the same action, two yields: the counter did not grow.
- [ ] **Step 2: Run to verify they fail** — build `rawreel_gui`; Expected: compile error on
  `forgetReleaseRunsForTest`.
- [ ] **Step 3: Implement** the removal.
- [ ] **Step 4: Run** — the full suite; `grep -rn "ScrubState::Cancelled\|cancelScrub\|leaveCancelled\|leave_posted_" src tools`
  gives no match, and `grep -n "WXK_ESCAPE" src/gui/main_frame.cpp` shows only the
  `static_assert`. Expected: PASS.
- [ ] **Step 5: Commit** — `refactor(gui): no drag cancel (control panel spec revision 4)`

### Task 2: `frameAt`, the inverse of `thumbX`

**Files:** Modify `src/core/timeline.h/.cpp`; Test `tests/test_timeline.cpp`

**Interfaces:**
- Produces: `[[nodiscard]] std::int64_t frameAt(double x, std::int64_t min, std::int64_t max,
  double travel_start, double travel_end) noexcept;` — the fraction
  `(x − travel_start) / (travel_end − travel_start)` clamped to `[0, 1]`, then
  `min + lround(fraction × (max − min))`; `min` when `travel_end <= travel_start` or `max <= min`.

- [ ] **Step 1: Write the failing tests** — `frameAt(10, 0, 100, 10, 410) == 0`; `(210, …) == 50`;
  `(410, …) == 100`; `(211, …) == 50`; `(213, …) == 51`; `(0, …) == 0`; `(500, …) == 100`;
  `(409, 0, 1, 10, 410) == 1`; `(200, 0, 1, 10, 410) == 0`; `(5, 0, 100, 10, 10) == 0`;
  `(200, 7, 7, 10, 410) == 7`; round trips `frameAt(thumbX(v, 0, 100, 10, 410), 0, 100, 10, 410)
  == v` for every `v` in `0..100`, and for `v` in `{0, 1, 2283, 4565, 4566}` over
  `(0, 4566, 10, 786)` (the measured macOS track, spec §14.1).
- [ ] **Step 2: Run to verify they fail** — build `rawreel_core_tests`; Expected: no `frameAt`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** — `ctest --preset ninja-macos -R 'rawreel\.core_unit$' --output-on-failure`.
- [ ] **Step 5: Commit** — `feat(core): frameAt, the inverse of thumbX`

### Task 3: Skimming on the slider

**Files:**
- Create: `src/gui/skim_key.h/.cpp` (`rawreel_panel`, add to `CMakeLists.txt`)
- Modify: `src/gui/transport_bar.h/.cpp`, `src/gui/timeline_strip.h/.cpp` (export the travel
  rule; the header includes `gui/native_controls.h`), `src/gui/native_controls.h` and its four
  platform files (`sliderEventX`), `src/gui/main_frame.h/.cpp`, `tools/panel_lab/panel_lab.cpp`,
  `src/gui/selftest.cpp`

**Interfaces:**
- Consumes: Task 2 `frameAt`; Task 1 `ScrubState`, `frameActivated`, `onSliderLeftDown`.
- Produces:
  - `ScrubState::Skimming`.
  - `skim_key.h`: `[[nodiscard]] bool skimKeyDown(const wxKeyboardState& state) noexcept;`
    (`__WXOSX__`: `AltDown()`, else `ControlDown()`); `[[nodiscard]] bool isSkimKey(int key_code)
    noexcept;` (`WXK_ALT` / `WXK_CONTROL`); `class SkimKeyRelease final : public wxEventFilter` —
    its constructor takes a `dvv::MoveOnlyFunction<void()>` and calls `wxEvtHandler::AddFilter`,
    its destructor `RemoveFilter`; on every `wxEVT_KEY_UP` with `isSkimKey` it calls the function
    (the event's own modifier flags are not read: GTK reports them from before the event); always
    `Event_Skip`. `MainFrame` and the lab each hold one as a member, calling
    `transportBar().skimKeyReleased()`.
  - `SliderTrack sliderTravel(const wxSlider& slider);` in `timeline_strip.h` — `sliderTrack`, else
    the fallback (`FromDIP(FALLBACK_THUMB_DIP)` thumb, half of it, rounded, in from each end),
    slider client px. `TimelineStrip`'s travel, the bar's `frameAt` and the selftest helper use it.
  - `[[nodiscard]] int sliderEventX(const wxSlider& slider, int event_x);` in `native_controls.h`
    — a mouse event's x on the slider in the wx client coordinates of `sliderTrack`: macOS
    `event_x` plus the same bounds-to-wx offset `native_controls_cocoa.mm` applies to the track
    (minus the alignment inset), rounded; MSW, GTK and stub: `event_x`.
  - `TransportBar::skimKeyReleased()`; `frameActivated(bool active)` stores whether the frame is
    active (initially true) and, on false, returns from a skim.
- Behaviour: the Skimming rows of spec §6.2 and §6.4.
  - Bind `wxEVT_MOTION` and `wxEVT_LEAVE_WINDOW` on the slider (always `Skip()`); unbind them and
    the slider's `wxEVT_LEFT_DOWN` in `~TransportBar`.
  - Enter: a slider motion in Idle with `skimKeyDown(event)`, `!event.LeftIsDown()`, the frame
    active, not playing, a stream and the slider enabled; `original_ = last_sent_ = idle_value_`;
    show the marker; then skim to `frameAt(sliderEventX(*slider_, event.GetX()), …sliderTravel…)`.
  - In Skimming: a motion with the key skims (no duplicate seeks); a motion without the key, the
    slider's leave, `skimKeyReleased()` and `frameActivated(false)` return; `LEFT_DOWN` keeps, then
    the Idle press rule; any scroll event keeps, then its Idle row; an `update()` showing playback
    keeps; a stream change resets without a seek back.
  - `--debug-transport` traces each transition, every slider left-down, and on each skim step the
    converted pointer x, the frame and `nativeThumbCentre` after the `SetValue` (the manual checks
    of spec §9a).
  - The selftest filter: `wxEVT_MOTION`, `wxEVT_ENTER_WINDOW` and `wxEVT_LEAVE_WINDOW` join
    `REAL_INPUT_TYPES` (counted, not logged one by one).

- [ ] **Step 1: Write the failing tests** (helpers: `xOfFrame(TransportBar& bar, std::int64_t
  frame)` = `lround(thumbX(frame, slider min, slider max, sliderTravel start, end))` in wx client
  px; `sendSliderMouse(TransportBar& bar, wxEventType type, int x, bool skim_key, bool left_down =
  false)` building a `wxMouseEvent` on the slider at `y = height / 2` with the platform modifier
  set and the x converted back to the event's native shape (the inverse of `sliderEventX`),
  through `injectInput`; `sendSkimKeyUp(wxWindow& target)` injecting a `wxEVT_KEY_UP` with the skim
  key code and the modifier flag **set** (GTK's shape) into `target`; each sequence starts paused
  at 10; the seek observer records seeks from a sequence's first step)
  - One sequence, with no `idle_at` between its steps:
    - `skim enters and previews`: a motion at frame 30 with the key → its `passed()` waits until
      settled at 30 and then checks Skimming (a notification must not end a skim), marker 10,
      seeks `30`, slider 30, current label `"30"`.
    - `skim moves without duplicates`: frame 30 again → no seek; frame 40 → seeks `30 40`.
    - `the key's release returns`: `sendSkimKeyUp(frame.viewport())` with no motion → Idle,
      seeks `30 40 10`, slider 10, no marker.
  - `a motion at the current frame seeks nothing`: a motion with the key at frame 10 → Skimming, no
    seek; the release → Idle, no seek.
  - `a motion without the key returns`, `leaving the slider returns` (a slider leave),
    `deactivation returns` (an injected `wxActivateEvent(false)`): each → Idle at 10 after a seek
    back.
  - `a click keeps the skimmed frame`: skim to 40, an injected slider left-down at 40 → Idle at 40,
    no seek to 10, no marker; then the release does nothing.
  - `a motion with the button down does not enter`: a motion with the key and `left_down = true`
    → Idle, no seek.
  - `a scroll event while skimming keeps`: skim to 40, a buttonless `THUMBTRACK` at 45
    (`sendSliderTrack(slider, 45)` with the probe up) → Idle right after the track, seeks end
    `40 45`; then `sendSkimKeyUp` → no seek to 10 (without the keep the bar would still be
    Skimming and return). The slider is checked at 45 only once settled (the keep's Idle view sets
    it to 40 until the notification).
  - `no skim while inactive`: an injected `wxActivateEvent(false)`, then a motion with the key →
    Idle, no seek; an injected `wxActivateEvent(true)` restores.
  - `skim while playing does nothing`: Space; a later step waits for `playIconForTest() ==
    Icon::Pause`; then a motion with the key → Idle, no seek from the bar; then pause.
  - `the skim key during a drag`: probe down, track to 30, a motion with the key and the button
    down → still Scrubbing; the release pair → Idle at 30.
  - `a stream change while skimming`: skim to 40, `bar.update` with the same status and
    `streamGeneration + 1` → Idle, no seek to 10; then restore.
- [ ] **Step 2: Run to verify they fail** — build `rawreel_gui`; Expected: compile error on
  `ScrubState::Skimming` / `skimKeyReleased`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** — the full suite; mutation checks: drop the filter's call (the release step
  fails), drop the `!LeftIsDown()` condition (the button-down step fails), drop the scroll-keep
  rule (the scroll step fails), drop the active check (the inactive step fails), let the
  `update()` backstop end Skimming (the enter step fails); restore.
- [ ] **Step 5: Commit** — `feat(gui): skimming on the slider with Option (macOS) or Ctrl`

### Task 4: Click to jump on Windows

**Files:** Modify `src/gui/native_controls.h`, `native_controls_msw.cpp` (the `TBM_GETTHUMBRECT`
body), `native_controls_gtk.cpp`, `native_controls_cocoa.mm`, `native_controls_stub.cpp` (each
`sliderThumbRect` returns `std::nullopt`), `src/gui/transport_bar.h/.cpp`, `src/gui/selftest.cpp`

**Interfaces:**
- Produces in `native_controls.h`: `constexpr bool SLIDER_PAGES_ON_TRACK_CLICK` (true under
  `__WXMSW__`); `[[nodiscard]] std::optional<wxRect> sliderThumbRect(const wxSlider& slider);`
  (MSW `TBM_GETTHUMBRECT`, slider client coordinates; other platforms nullopt);
  `TransportBar::setTrackClickPagesForTest(std::optional<bool> pages)` (empty: the constant). The
  thumb's rectangle when nullopt: `thumbWidth` from `sliderTravel`, centred at `thumbX(value)`,
  full height.
- Behaviour (spec §6.5, the §6.2 Idle row): in Idle (also right after a Skimming keep), when pages
  is true and the press is outside the thumb's rectangle: `SetValue(frameAt(sliderEventX(…)))`,
  seek only if it differs from `idle_value_`, `idle_value_ = value`, `showStrip()`, and do not
  `Skip()`. A press on the thumb is `Skip()`ped.

- [ ] **Step 1: Write the failing tests** (`setTrackClickPagesForTest(true)`, cleared at the end)
  - `a channel click jumps`: paused at 10, a left-down at the x of frame 50 → `injectInput`
    returns true, slider 50, seeks `50`.
  - `a thumb click does not jump`: a left-down at the thumb's centre → `injectInput` returns
    false, no seek.
  - `a click while skimming keeps and jumps`: on the real clip, skim to 40, then a left-down at
    `xOfFrame(bar, 50)` with no motion in between (outside the thumb at 40) → `injectInput` true,
    Idle at 50, seeks end `40 50`, no seek to 10, no marker.
  - `if constexpr (!SLIDER_PAGES_ON_TRACK_CLICK)` (macOS, GTK): with the seam cleared, a channel
    left-down is skipped and seeks nothing.
- [ ] **Step 2: Run to verify they fail** — compile error on `setTrackClickPagesForTest`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** — the full suite.
- [ ] **Step 5: Commit** — `feat(gui): a click on the Windows slider jumps to the clicked frame`

### Task 5: Documents

**Files:** `README.md`, the spec §14.2, `docs/superpowers/plans/2026-11-09-video-control-panel.md`

- [ ] **Step 1: README** — while paused, holding ⌥ Option (macOS) or Ctrl (Windows, Linux) and
  moving over the slider previews the frame under the pointer; a click moves there; releasing the
  key or moving off the slider returns; a click on the slider jumps to the clicked frame.
- [ ] **Step 2: Spec** — already updated in the plan's review round (§6.1 activation and
  `sliderEventX`, §9a rows, §10, §14.2); check nothing in §6 or §14 still says skimming works over
  the strip.
- [ ] **Step 3: Old plan** — in `docs/superpowers/plans/2026-11-09-video-control-panel.md`, rename
  the shell invocations at lines 245 and 564 to `rawreel-panel-lab`; leave the CTest `COMMAND`
  (it names the CMake target).
- [ ] **Step 4: Verify** — the full suite.
- [ ] **Step 5: Commit** — `docs: skimming and click to jump in the README`

## Self-review notes

- Spec coverage: U-CP1/4/6 superseded → T1; U-CP7, §6.1–6.4 → T3 (`frameAt` → T2); U-CP8, §6.5 →
  T4; U-CP9 → nothing to build; §9 → T2–T4; §9a, §11 → T5 and the manual run.
- Ruling R1: the key's release reaches the bar through a `wxEventFilter`, because `KEY_UP` goes
  only to the focused window. The filter acts on `KEY_UP` only, so a key pressed in a dialog can
  end a skim but never start one. Cost if wrong: one class.
- Revision of the first draft after the plan reviews and the user's simplification: skimming is on
  the slider only; it starts only on a slider motion with the button up; no command keeps or
  returns a skim (Space while the key is held is an unbound chord); kept from the reviews: the
  scroll-keep rule, the Windows keep-then-jump click, the Scrubbing-only backstop, the shared
  travel rule, real-motion filtering in selftests, GTK's key flags, unbinding in the destructor,
  and `last_sent_` on entry.
- Second review round (optimistic C0 I3 M9, pessimistic C0 I5 M7), all applied: non-MSW
  `sliderThumbRect` bodies; the Task 4 keep-and-jump test on the real clip; the scroll-keep test
  with a buttonless `THUMBTRACK` and a key release; entry only while the frame is active;
  `sliderEventX` for macOS's bounds coordinates plus a 2 px manual check; a left-down trace and a
  GTK check for a click on the skimmed thumb; the positive control's drag; helpers kept from
  Task 1; one sequence for the first three skim steps; `xOfFrame`; the header include; the
  strip repaint after a Windows jump; the Windows guard; the destroyed-while-skimming step dropped
  (it could not fail). Logged only: GTK's late leave, the Windows channel click not focusing the
  slider, the thumb's brief snap after a scroll-keep.
