# Video Control Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the one-row text transport bar with the two-row control panel of the spec: a
drawn timeline strip over the native slider, Lucide icon buttons with Open on the left and the
group centred, a scrub marker, and a cancel that seeks back.

**Architecture:** `TransportBar` stays the one owner of the panel's state (CP-D1). Geometry is a
wx-free unit in `rawreel_core` (CP-D2); a `TimelineStrip` child window paints from it; native
track queries live in per-platform `native_controls_*` files. A static library `rawreel_panel`
holds the panel sources so both `rawreel_gui` and the standalone `rawreel_panel_lab` compile the
same classes.

**Tech Stack:** C++23, wxWidgets 3.3.4 (`wxBitmapBundle::FromSVG`, NanoSVG), doctest, CMake
presets (`ninja-macos`; CI: Linux clang/TSan/ASan, Windows MSVC, macOS).

**Spec:** `docs/superpowers/specs/2026-11-09-video-control-panel-design.md` (revision 3). It
builds on `docs/superpowers/specs/2026-11-08-session-state-notifications-design.md` (PR #9).

## Global Constraints

- The native `wxSlider` stays; no custom-drawn slider, no OpenGL, WebView or other UI framework
  (spec §8, brief §9).
- No timer and no polling in product code (spec §11). A posted `CallAfter` is an event, and so is
  the bar's state read on leaving Scrubbing or Cancelled (spec §3). The lab's simulated player may
  use a `wxTimer` (spec §10.1).
- U-EV6: no acknowledgement, echo, counter or timestamp of GUI requests. `streamGeneration` is
  session state (U-CP5).
- Decisions U-CP1 … U-CP6 and CP-D1 … CP-D4 are binding as written in spec §2.
- Initial constants: `MIN_MINOR_TICK_DIP = 4`, `LABEL_FLIP_HYSTERESIS_DIP = 8`,
  `FALLBACK_THUMB_DIP = 20`, buttons `29 × 28` DIP minimum, icons `18 × 18` DIP, strip minimum
  width `400` DIP, marker `2` DIP wide, original-label gap `2` DIP.
- Posted calls use the bar's own `CallAfter` (a `wxEvtHandler` member, dropped by the bar's
  destructor), never `wxTheApp->CallAfter` or another handler.
- Warnings are errors; clang-format 19.1.7 (`tools/requirements.txt`); the repo's clang-tidy
  profile. Sanitizer presets (`DVV_SANITIZE`) do not build `rawreel_gui` or `rawreel_panel`
  (`CMakeLists.txt:113`): ASan and TSan cover the core code only; the GUI is covered by the
  selftests.
- Buttons and the strip never take focus. Bare keys reach the slider only when no command takes
  them.
- Commit identity BarabashkaD; no machine paths, real names or personal e-mail in committed files;
  never bypass the pre-push hook. Branch `docs/video-control-panel` (holds the spec); one squashed
  PR that the user merges. The README states facts only (no generic OS how-tos) and only behaviour
  that a test or a recorded manual check has shown.
- Build and test on this Mac: `cmake --build libs/_build/rawreel/ninja-macos --config Debug
  --target <target>`, then `ctest --preset ninja-macos -R '<regex>' --output-on-failure` (run
  `cmake --workflow --preset ninja-macos` once first if the build tree is missing). GUI programs
  outside CTest need the README's macOS `VK_DRIVER_FILES` and `DVV_FFMPEG_DIR` environment.
- Selftests: steps go in the `player` scenario of `src/gui/selftest.cpp`. Real input is filtered
  while a selftest runs (`isRealInput`, `selftest.cpp:110`); every synthetic mouse, key or
  activation event is sent with `injectInput`. A step that feeds the bar crafted statuses ends by
  restoring the session's real one: `bar.update(frame.session().status().player,
  frame.session().status().streamGeneration)`.

## Review Focus

1. **Destroying the bar with a posted `leaveCancelled` or `forgetRelease` pending** (closing the
   window right after a drag is released): no call into a dead bar. Pinned in Task 7 (`a pending
   call after the bar is gone`).
2. **A dark-mode switch mid-session**: the icons are re-tinted and the strip re-reads its colours.
   Pinned in Task 4 (`the icons follow a colour change`) and Task 5 (`the strip follows a colour
   change`), both through a colour seam so they fail without the handler.
3. **Resizing between the 400 DIP minimum and a wide window**: the button group stays centred on
   the whole panel, Open stays left, and the labels follow the new track. Pinned in Task 4 (`the
   group is centred`) and Task 5 (`labels follow a resize`).
4. **Degenerate streams** (1 frame, unbounded, longer than `INT_MAX`, no position yet): the strip
   neither divides by zero nor shows a frame that does not exist. Pinned in Task 2 (unit) and
   Task 5 (`degenerate statuses`).
5. **Play pressed during a drag, then a GTK-style release**: the bar returns to the Idle view and
   playback keeps running (no trailing seek pauses it). Pinned in Task 6 (`play during a drag`).

---

### Task 1: `SessionStatus::streamGeneration`

**Files:**
- Modify: `src/core/session_types.h` (`SessionStatus`, `sameState`)
- Modify: `src/core/session.cpp` (`publishStatus`, near `status.streams = …`, ~line 809)
- Test: `tests/test_session_types.cpp`, `tests/test_session.cpp` (suite `session`)

**Interfaces:**
- Produces: `std::uint64_t SessionStatus::streamGeneration = 0;` — part of `sameState`. Published
  as the Session's `stream_set_`, which is bumped wherever the stream set is replaced or dropped
  (`streamsReplaced`, `closeStreams`, the device-loss release, shutdown) and never by a seek, a
  step or play/pause.

- [ ] **Step 1: Write the failing tests**
  - `test_session_types.cpp`, `TEST_CASE("sameState sees streamGeneration")`: two default
    `SessionStatus`, `b.streamGeneration = 1` → `CHECK_FALSE(sameState(a, b))`.
  - `test_session.cpp`, `TEST_CASE("streamGeneration changes with the stream set only")`, with
    the fixture's open and wait helpers used by the neighbouring tests and a synthetic 40-frame
    URI: after the open, `g1 = status().streamGeneration`, `CHECK(g1 >= 1)`; after `seek(5)`, a
    step and play/pause (each waited for), `CHECK(streamGeneration == g1)`; after reopening the
    same URI, `g2 > g1`; after `closeStreams`, `g3 > g2`.
- [ ] **Step 2: Run to verify they fail**
  Run: `cmake --build libs/_build/rawreel/ninja-macos --config Debug --target rawreel_core_tests`
  Expected: compile error, no member `streamGeneration`.
- [ ] **Step 3: Implement** the field, `status.streamGeneration = stream_set_;` in
  `publishStatus`, and the term in `sameState`.
- [ ] **Step 4: Run**
  Run: `ctest --preset ninja-macos -R 'rawreel\.core_(unit|session)$' --output-on-failure`
  Expected: PASS.
- [ ] **Step 5: Commit** — `feat(core): SessionStatus::streamGeneration`

### Task 2: Timeline geometry (wx-free)

**Files:**
- Create: `src/core/timeline.h`, `src/core/timeline.cpp`; Modify: `CMakeLists.txt` (add to
  `rawreel_core`)
- Test: `tests/test_timeline.cpp`; Modify: `tests/CMakeLists.txt` (add to `rawreel_core_tests`)

**Interfaces:**
- Produces (namespace `rawreel`):

```cpp
struct TickInterval { std::int64_t minor = 0; std::int64_t major = 0; };  // minor 0: no ticks
// Smallest minor in 1, 2, 5 × 10^n with minor × (track_px / (last - first)) >= min_minor_px;
// major = 10^(n+1) for minor = m × 10^n. {0, 0} when last <= first or track_px <= 0.
[[nodiscard]] TickInterval chooseTicks(std::int64_t first, std::int64_t last, double track_px,
                                       double min_minor_px) noexcept;
struct Tick { std::int64_t frame; bool major; };
// Multiples of interval.minor in [first, last]; major when a multiple of interval.major.
[[nodiscard]] std::vector<Tick> ticks(std::int64_t first, std::int64_t last, TickInterval interval);
// travel_start + (value - min) / (max - min) × (travel_end - travel_start); travel_start if max <= min.
[[nodiscard]] double thumbX(std::int64_t value, std::int64_t min, std::int64_t max,
                            double travel_start, double travel_end) noexcept;

enum class Side : std::uint8_t { Left, Right };
struct LabelIn { double anchor; double width; };  // anchor: the x the label belongs to
struct TimelineLabelsIn {
    double strip_width = 0;
    double gap = 0;         // between the marker line and the original label
    double hysteresis = 0;  // LABEL_FLIP_HYSTERESIS_DIP in pixels
    std::optional<LabelIn> current, original, first, last;  // original.anchor: the marker's x
    std::optional<Side> previous_side;                      // the original label's last side
};
struct TimelineLabelsOut {
    std::optional<double> current, original, first, last;  // left edges; nullopt: hidden
    std::optional<Side> original_side;                     // set whenever `original` was given
};
[[nodiscard]] TimelineLabelsOut placeLabels(const TimelineLabelsIn& in);
```

`placeLabels` follows spec §5.2 in order:
1. Current: centred on its anchor, clamped into `[0, strip_width]` (left 0 if wider than the
   strip).
2. Original's preferred side: Left when `current.anchor > marker + hysteresis`, Right when
   `current.anchor < marker - hysteresis`, else `previous_side`, else Left if
   `current.anchor >= marker`; with no current label, `previous_side`, else Left.
   `original_side` is always this preferred side, whether the label is then placed there, on the
   other side or hidden. Left puts its right edge at `marker - gap`, Right its left edge at
   `marker + gap`. Placed on the preferred side if inside the strip and not overlapping the current
   box, else on the other side under the same test, else hidden.
3. First and last: centred on their anchors, clamped, hidden when they overlap the current or the
   placed original box. Boxes that only touch do not overlap.

The caller (Task 5) clears its stored side when the marker disappears.

- [ ] **Step 1: Write the failing tests** (doctest, `tests/test_timeline.cpp`)
  - `chooseTicks`: `(0, 99, 400, 4)` → `{1, 10}`; `(0, 9, 400, 4)` → `{1, 10}`;
    `(0, 4566, 400, 4)` → `{50, 100}`; `(0, 99999, 2000, 4)` → `{200, 1000}`;
    `(0, 99999, 400, 4)` → `{1000, 10000}`; `(0, 0, 400, 4)` → `{0, 0}`; `(0, 99, 0, 4)` →
    `{0, 0}`.
  - `ticks(0, 25, {5, 10})` → frames `0, 5, 10, 15, 20, 25`, major exactly at `0, 10, 20`;
    `ticks(0, 0, {0, 0})` empty.
  - `thumbX`: `(0, 0, 100, 10, 410) == 10`, `(50, …) == 210`, `(100, …) == 410`;
    `(1, 0, 1, 10, 410) == 410`; `(0, 0, 0, 10, 410) == 10`.
  - `placeLabels`, strip 400, gap 2, hysteresis 8:
    - clamping: current `{5, 40}` → left 0; current `{395, 40}` → left 360.
    - current hides an end label: first `{10, 10}`, current `{12, 40}` → `first` nullopt; last
      `{390, 30}` → left 370.
    - marker at the start: original `{10, 30}`, current `{13, 40}`, no previous side →
      `original` nullopt, `original_side == Left`, `current == 0`.
    - preferred side: original `{100, 30}`, current `{140, 40}` (box 120–160) → Left, 68–98 →
      `original == 68`, `original_side == Left`.
    - fallback side: original `{20, 30}`, current `{200, 40}` (box 180–220) → preferred Left
      (−12–18) is outside the strip → Right, 22–52 → `original == 22`, `original_side == Left`.
    - fallback at the right edge: original `{385, 30}`, current `{340, 40}` (box 320–360) →
      preferred Right (387–417) is outside → Left, 353–383 → `original == 353`,
      `original_side == Right`.
    - original over an end label: original `{300, 40}`, current `{100, 40}`, last `{340, 40}` →
      `original == 302`, `last` nullopt.
    - no current: original `{200, 30}`, previous Right → `original == 202`, side Right.
    - hysteresis, marker 200: previous Left with current at 205 → Left; previous Left with
      current at 195 → Left; previous Left with current at 191 → Right; previous Right with current
      at 209 → Left.
- [ ] **Step 2: Run to verify they fail** — build `rawreel_core_tests`; Expected: compile error,
  `core/timeline.h` not found.
- [ ] **Step 3: Implement** `src/core/timeline.cpp`.
- [ ] **Step 4: Run** `ctest --preset ninja-macos -R 'rawreel\.core_unit$' --output-on-failure`,
  then `cmake --workflow --preset ninja-macos-asan`. Expected: PASS.
- [ ] **Step 5: Commit** — `feat(core): timeline ticks, thumb x and label placement`

### Task 3: `rawreel_panel` library, native slider queries and the lab

**Files:**
- Create: `src/gui/native_controls.h`, `src/gui/native_controls_msw.cpp`,
  `src/gui/native_controls_gtk.cpp`, `src/gui/native_controls_cocoa.mm`,
  `src/gui/native_controls_stub.cpp`
- Create: `tools/panel_lab/panel_lab.cpp`, `tools/panel_lab/panel_lab.rc` (Windows: the same
  content as `src/gui/rawreel-gui.rc`, i.e. `wx/msw/wx.rc` with the manifest)
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt` (`rawreel.panel_lab.report`)

**CMake** (inside the existing `if(NOT DVV_SANITIZE)` block):

```cmake
find_package(wxWidgets ...)            # already there; everything below comes after it
if(APPLE)
    enable_language(OBJCXX)            # moved up from the rawreel_gui APPLE branch
endif()
if(UNIX AND NOT APPLE)
    pkg_check_modules(GTK3 ...)        # moved up from the rawreel_gui Linux branch
endif()
add_library(rawreel_panel STATIC src/gui/transport_bar.cpp)
# + native_controls_msw.cpp (WIN32) | native_controls_gtk.cpp (UNIX AND NOT APPLE, links
#   PkgConfig::GTK3) | native_controls_cocoa.mm (APPLE, COMPILE_OPTIONS -fobjc-arc) | the stub
target_link_libraries(rawreel_panel PUBLIC rawreel_core wx::core dvv::base)
dvv_set_compiler_options(rawreel_panel)
# rawreel_gui: drop transport_bar.cpp from its sources, link rawreel_panel.
add_executable(rawreel_panel_lab tools/panel_lab/panel_lab.cpp)  # console subsystem
# Windows: + tools/panel_lab/panel_lab.rc, /MANIFEST:NO; Linux: + src/gui/gui_platform_gtk.cpp
target_link_libraries(rawreel_panel_lab PRIVATE rawreel_panel)
dvv_set_compiler_options(rawreel_panel_lab)
# No install() rule: the lab is never packaged.
```

**Interfaces:**
- Produces (`src/gui/native_controls.h`):

```cpp
struct SliderTrack { int travel_start; int travel_end; int thumb_width; };  // wx client px
// The thumb centre's travel limits from the native control (spec §5.3), in the slider's wx client
// coordinates: Windows TBM_GETCHANNELRECT + TBM_GETTHUMBRECT; GTK gtk_range_get_range_rect +
// gtk_range_get_slider_range on wxSlider::m_scale, converted from the GtkScale to the wx widget;
// macOS the cell's barRectFlipped: + knobRectFlipped:, minus the NSView's alignment-rect inset that
// wxOSX applies. nullopt: no query on this platform.
[[nodiscard]] std::optional<SliderTrack> sliderTrack(const wxSlider& slider);
// The native thumb's centre at the slider's current value, same coordinates. Lab and tests only.
[[nodiscard]] std::optional<int> nativeThumbCentre(const wxSlider& slider);
```

- Produces: targets `rawreel_panel` (later tasks add their panel sources to it) and
  `rawreel_panel_lab`, CTest `rawreel.panel_lab.report`.
- The lab: `rawreel-panel-lab [--frames N] [--fps F] [--seek-delay MS] [--report]`, defaults
  `--frames 4567 --fps 25 --seek-delay 0`. Windows `wmain` + `wxEntry`; Linux calls
  `prepareX11StartUp()` first and returns `skipExitCode()` when it reports a problem (as
  `src/gui/main.cpp:45-53`). A frame holds one `TransportBar`; a simulated player (lab-only
  `wxTimer`) answers `execute` (PlayPause, Stop, First, Last, StepBack, StepForward, Loop) and
  `seek` (target at once, position after the delay) and calls `update()` on each change. Above the
  slider the lab draws a 1 px line at the computed thumb x (removed when the strip exists, Task 5),
  for the manual check.
- `--report` is an event-driven sequence: each stage changes the size or the slider value, then
  waits for the slider's next `wxEVT_PAINT` (or two `CallAfter` hops on platforms where no paint
  arrives), bounded at 5 s (exit 1 with "timeout at <stage>"). Stages: initial size, then a 400 DIP
  client width; at each, `track start <s> end <e> thumb <w>`, then for min, middle and max
  `value <v> native <x> computed <y> error <d>`, then `best <w>x<h> DIP` of the bar. Exit 1 if any
  `|error| > 2` px or `sliderTrack` is nullopt on Windows, GTK or macOS; otherwise 0. The check
  proves the travel formula, not where a drawn label sits (that is the manual check, Task 8).

- [ ] **Step 1: Write the failing test** — register `rawreel.panel_lab.report` (`COMMAND
  rawreel_panel_lab --report`, `LABELS "display;gui"`, `SKIP_RETURN_CODE 77`, `TIMEOUT 60`,
  `ENVIRONMENT "NO_AT_BRIDGE=1"`, `FAIL_REGULAR_EXPRESSION "${gui_x_error}"`) inside the existing
  `if(TARGET rawreel_gui)` block of `tests/CMakeLists.txt`.
- [ ] **Step 2: Run to verify it fails** — `ctest --preset ninja-macos -R 'panel_lab'`;
  Expected: not runnable (no `rawreel_panel_lab` target).
- [ ] **Step 3: Implement** the CMake split (no behaviour change to `rawreel_gui`), the native
  queries, and the lab. If a platform's formula is off by more than 2 px, fix that platform's
  formula. If no formula (including a measured DIP constant, spec §5.3) reaches 2 px, stop and
  report BLOCKED with the report output: the user decides.
- [ ] **Step 4: Run** — `ctest --preset ninja-macos -R 'rawreel\.(panel_lab|gui_)' --output-on-failure`;
  Expected: PASS on macOS. Put the `--report` output in the task report (Task 8 copies it to spec
  §14). The Windows and Linux results come from the branch's CI run.
- [ ] **Step 5: Commit** — `feat(gui): native slider track queries and the panel lab`

### Task 4: Icon buttons, two rows, tooltips, accessible names, notices

**Files:**
- Create: `src/gui/icons/lucide_icons.h` (first line `// lucide-static <version>`; one
  `inline constexpr std::string_view` per icon as a raw string with the `R"svg(` … `)svg"`
  delimiter, the files of that release verbatim), `src/gui/icons/LICENSE` (that release's
  `LICENSE`, verbatim: ISC plus Feather's MIT notice)
- Create: `src/core/svg_colour.h/.cpp`, `src/gui/icons.h/.cpp`
- Modify: `CMakeLists.txt` (`svg_colour.cpp` → `rawreel_core`, `icons.cpp` → `rawreel_panel`),
  `tests/CMakeLists.txt` (`test_svg_colour.cpp` → `rawreel_core_tests`)
- Modify: `src/core/commands.h/.cpp` (`tooltipText`), `src/gui/native_controls.h` and its
  platform files (`setAccessibleName`), `src/gui/transport_bar.h/.cpp`, `src/gui/selftest.cpp`
- Modify: `packaging/check-package.cmake` (new function next to `rawreel_strip_msvc_notice`),
  `packaging/packaging.cmake` (after the strip, ~line 180; `RAWREEL_SHIPPED`, ~line 346)
- Test: `tests/test_commands.cpp`, `tests/test_svg_colour.cpp`,
  `tests/packaging/test_packaging_scripts.cmake` (CTest `rawreel.packaging_scripts`)

**Interfaces:**
- Produces:
  - `std::string tooltipText(const CommandInfo& info, bool cmd_for_ctrl);` — "<help> (<keyText>)",
    or `help` alone without a key; with `cmd_for_ctrl` an Accelerator-routed chord's "Ctrl+" reads
    "Cmd+". The GUI passes `true` on macOS.
  - `std::string replaceCurrentColor(std::string_view svg, std::string_view colour);` — every
    `currentColor` replaced by `colour` (for example `#1d1d1f`).
  - `enum class Icon : std::uint8_t { FolderOpen, SkipBack, StepBack, Play, Pause, Square,
    StepForward, SkipForward, Repeat };` and `wxBitmapBundle iconBundle(Icon icon, const wxColour&
    colour);` (18 × 18 DIP, `wxBitmapBundle::FromSVG` on the replaced text).
  - `void setAccessibleName(wxWindow& window, const wxString& name);` — macOS
    `setAccessibilityLabel:`; GTK `atk_object_set_name`; Windows a `wxAccessible` subclass
    returning the name (`wxUSE_ACCESSIBILITY` is 1 in the MSW build); stub nothing.
  - `TransportBar`: `void setSystemColourForTest(dvv::MoveOnlyFunction<wxColour(wxSystemColour)>
    source);` (empty: `wxSystemSettings::GetColour`; Task 5's strip reads the same source),
    `[[nodiscard]] Icon playIconForTest() const;`, `[[nodiscard]] wxColour iconTintForTest() const;`,
    `[[nodiscard]] int iconRebuildsForTest() const;`, `[[nodiscard]] wxRect groupRectForTest()
    const;` (the union of First … Loop), `[[nodiscard]] wxWindow& openButtonForTest();`.
  - CMake: `rawreel_append_notice(FILE <notices> NAME <name> VERSION <v> SPDX <id> URL <url>
    LICENCE <file>)` — appends one entry in `write-notices.cmake`'s format: a 62-`=` separator,
    `<name> <version>`, `Licence: <id>`, `Source: <url>`, a blank line, `--- LICENSE ---`, a blank
    line, the file's text with trailing whitespace removed.
- Layout (spec §5.1): vertical sizer with the slider row, then `[Open] stretch [group] stretch
  [spacer]`. The spacer's width is Open's `GetEffectiveMinSize().x` plus the border Open gets,
  recomputed on `wxEVT_DPI_CHANGED`, `wxEVT_SYS_COLOUR_CHANGED` and whenever Open's min size
  changes. Every button's min size is `FromDIP(wxSize(29, 28))` or the native minimum if larger.
  Buttons follow the spec §7 table; Loop is a `wxBitmapToggleButton`. The Play/Pause bitmap and
  Loop's value are set only on change. CP-D4: every button but Open is disabled while the bar has
  no stream (the condition that disables the slider). Tooltips from `tooltipText`;
  `setAccessibleName` with the command's `label` minus a trailing `...` ("Open"), `SetName` with
  the full label. On `wxEVT_SYS_COLOUR_CHANGED` every bundle is rebuilt with the colour source's
  `wxSYS_COLOUR_BTNTEXT`. The right-hand frame label stays until Task 5.
- Notices: `packaging.cmake` calls `rawreel_append_notice` with `NAME lucide`, the version from
  `lucide_icons.h`'s first line (`file(STRINGS … REGEX "^// lucide-static ")`),
  `SPDX "ISC AND MIT"`, `URL https://github.com/lucide-icons/lucide`,
  `LICENCE src/gui/icons/LICENSE`; adds both icon files to `CMAKE_CONFIGURE_DEPENDS`; sets
  `RAWREEL_SHIPPED` to `${DVV_SHIPPED_THIRD_PARTY};lucide`.

- [ ] **Step 1: Write the failing tests**
  - `test_commands.cpp`: `tooltipText(PlayPause, false) == "Start or pause playback (Space)"`;
    `tooltipText(Stop, false) == "Stop playback and go to the first frame"`;
    `tooltipText(Open, false) == "Open a raw video file (Ctrl+O)"`;
    `tooltipText(Open, true) == "Open a raw video file (Cmd+O)"`;
    `tooltipText(StepBack, true) == "Show the previous frame (Left)"`.
  - `test_svg_colour.cpp`: `replaceCurrentColor("<path stroke=\"currentColor\" fill=\"currentColor\"/>",
    "#ffffff")` has no `currentColor` and two `#ffffff`; for every icon in `lucide_icons.h` the
    replaced text contains no `currentColor`.
  - `test_packaging_scripts.cmake`: append an entry to a two-entry notices file written by the
    test, then parse it with `_rawreel_notices_parse`: the entries are the two names plus
    `lucide`, and the licence block holds the test licence text.
  - Selftest steps, after `stream change resets the scrub` (~`selftest.cpp:2749`, where
    `reopen_clip` exists): `the icons follow playback` (Play → `Pause` icon, pause → `Play`);
    `buttons wait for a stream` (after Close stream every button but Open is disabled; after
    reopening all are enabled); `the group is centred` (`bar.SetSize(FromDIP(400),
    bar.GetSize().y); bar.Layout();` then, in the same action, `|groupRect.centre.x − bar width /
    2| <= 1` and Open's x at the left border; again at 1000 DIP; then restore the frame's layout);
    `the icons follow a colour change` (colour source returns `#ff00ff` for `BTNTEXT`;
    `bar.SendSysColourChangedEvents()` → `iconTintForTest() == #ff00ff`, `iconRebuildsForTest()`
    grew, no bitmap null; then clear the source and send again).
- [ ] **Step 2: Run to verify they fail** — build `rawreel_core_tests` and `rawreel_gui`, and
  `ctest -R 'rawreel\.packaging_scripts$'`; Expected: compile errors on `tooltipText`,
  `replaceCurrentColor`, the seams; the packaging test fails on the unknown command.
- [ ] **Step 3: Implement.** Use the latest `lucide-static` release at implementation time and
  record its version in the header: `https://unpkg.com/lucide-static@<version>/icons/<name>.svg`
  and `…/LICENSE`. If an icon of the spec's table does not exist in that release, or the network
  is unavailable, stop and report BLOCKED; do not substitute.
- [ ] **Step 4: Run** — `ctest --preset ninja-macos -R 'rawreel\.(core_unit|gui_|panel_lab|packaging_scripts)' --output-on-failure`,
  then re-configure (`cmake --preset ninja-macos`) and
  `grep -n "^lucide " libs/_build/rawreel/ninja-macos/packaging/THIRD_PARTY_NOTICES.txt`.
  Expected: PASS, including the existing `shrink stops at the minimum` steps; one
  `lucide <version>` line.
- [ ] **Step 5: Commit** — `feat(gui): Lucide icon buttons, Open and Stop, centred group`

### Task 5: Timeline strip

**Files:**
- Create: `src/gui/timeline_strip.h`, `src/gui/timeline_strip.cpp`; Modify: `CMakeLists.txt`
  (→ `rawreel_panel`)
- Modify: `src/gui/transport_bar.h/.cpp` (owns the strip; the right-hand label goes),
  `src/gui/selftest.cpp`, `tools/panel_lab/panel_lab.cpp` (the strip replaces the 1 px line)

**Interfaces:**
- Consumes: Task 2 (`chooseTicks`, `ticks`, `thumbX`, `placeLabels`), Task 3 (`sliderTrack`),
  Task 4 (the bar's colour source).
- Produces:

```cpp
struct TimelineView {
    std::optional<std::int64_t> last_frame;  // min(length - 1, INT_MAX); nullopt: empty strip
    std::int64_t slider_min = 0, slider_max = 1;
    std::optional<std::int64_t> current_anchor;  // frame the current label sits over
    std::optional<std::int64_t> current_text;    // number it shows; nullopt: none
    std::optional<std::int64_t> marker;          // original frame while Scrubbing
    bool operator==(const TimelineView&) const = default;
};
struct TimelineLabel {
    std::string role;  // "current", "original", "first", "last"
    std::string text;
    int x;             // left edge, strip px
    int anchor;        // the x it belongs to, strip px
    int width;
};
struct TimelineModel { std::vector<TimelineLabel> labels; std::optional<std::int64_t> marker; };
class TimelineStrip final : public wxWindow {
public:
    TimelineStrip(wxWindow* parent, const wxSlider& slider,
                  dvv::MoveOnlyFunction<wxColour(wxSystemColour)> colour_source);
    void setView(const TimelineView& view);  // Refresh() only when the view changed
    [[nodiscard]] TimelineModel modelForTest();  // computes the layout like a paint would
    [[nodiscard]] std::pair<double, double> travelForTest();  // strip px
    [[nodiscard]] wxColour textColourForTest() const;
    bool AcceptsFocus() const override { return false; }
    bool AcceptsFocusFromKeyboard() const override { return false; }
};
```

- Rules (spec §5.2, §5.3, §8): `wxBG_STYLE_PAINT`, `wxAutoBufferedPaintDC`. Min size
  `FromDIP(wxSize(400, h))` with `h` from the UI font's height plus the tick rows, fixed except on
  DPI or font change. The track-dependent layout is computed when painting (and in
  `modelForTest`), never in the strip's own size handler: size, DPI, the slider's `wxEVT_SIZE`
  (bound with `Skip()`) and `setView` only `Refresh()`. Travel: `sliderTrack` plus the slider's x
  minus the strip's x; on nullopt, `t / 2` and `width − t / 2` with `t =
  FromDIP(FALLBACK_THUMB_DIP)`. `chooseTicks(0, last_frame, travel_end − travel_start,
  FromDIP(MIN_MINOR_TICK_DIP))`; minor ticks at most half the major length. Labels from
  `placeLabels`; first "0"; last `last_frame` only when `last_frame > 0`; the stored
  `previous_side` is cleared when `marker` becomes nullopt. Colours and the marker as spec §5.2,
  re-read from the colour source on `wxEVT_SYS_COLOUR_CHANGED`. In this task `TransportBar` sets
  `current_anchor = target`, `current_text = position` (nullopt when `position < 0`) and no
  marker; Task 6 adds the Scrubbing and Cancelled views.

- [ ] **Step 1: Write the failing tests** (selftest `player` scenario)
  - `the strip shows the frame on screen`: with the `ProduceDelay` at 500 ms, `seek(30)` from 10;
    while `target == 30` and `position == 10`, the `current` label's text is `"10"` and
    `|anchor − round(thumbX(30, min, max, travelForTest()))| <= 1`.
  - `labels follow a resize`: `bar.SetSize(FromDIP(400), …)`, `Layout()`, then `FromDIP(1000)`:
    each time the `first` anchor equals `travelForTest().first` and the `last` anchor
    `travelForTest().second` (±1 px), and both moved between the two sizes.
  - `degenerate statuses` (crafted `update()` calls, then restore): default status → no labels;
    `length` nullopt with `position == target == 5` → no labels; `length == 1` → only `first`
    ("0"); `position == -1` → no `current`; `length == 3'000'000'000` → `last` reads
    `"2147483647"`.
  - `the strip follows a colour change`: colour source `#ff00ff`,
    `bar.SendSysColourChangedEvents()` → `textColourForTest() == #ff00ff`; clear and send again.
- [ ] **Step 2: Run to verify they fail** — build `rawreel_gui`; Expected: compile error on
  `TimelineStrip`.
- [ ] **Step 3: Implement**; remove the right-hand label.
- [ ] **Step 4: Run** — `ctest --preset ninja-macos -R 'rawreel\.(gui_|panel_lab)' --output-on-failure`;
  Expected: PASS, including `shrink stops at the minimum`.
- [ ] **Step 5: Commit** — `feat(gui): timeline strip with ticks and frame numbers`

### Task 6: Scrub states, the Idle view, the release and the marker

**Files:**
- Modify: `src/gui/transport_bar.h/.cpp`, `src/gui/player_panel.h/.cpp` (passes the state
  read), `src/gui/main_frame.cpp` (`onStatusChanged` passes `streamGeneration`; provides the state
  read), `src/gui/selftest.cpp` (new steps; every direct `bar.update(...)` call — `held drag on a
  slow source` and Task 5's — gains the generation), `tools/panel_lab/panel_lab.cpp`

**Interfaces:**
- Consumes: Task 1 (`SessionStatus::streamGeneration`), Task 5 (`TimelineView`).
- Produces:
  - `struct BarStatus { PlayerStatus player; std::uint64_t stream_generation = 0; };` in
    `transport_bar.h`.
  - Constructor `TransportBar(wxWindow* parent, dvv::MoveOnlyFunction<void(Command)> execute,
    dvv::MoveOnlyFunction<void(std::int64_t)> seek, dvv::MoveOnlyFunction<BarStatus()>
    read_status);` — `MainFrame` reads the session's current status (a default `BarStatus` with no
    stream, as `onStatusChanged` does); `PlayerPanel` forwards it.
  - `void TransportBar::update(const PlayerStatus& status, std::uint64_t stream_generation);`
  - Members per spec §6.1: `last_status_`, `last_generation_`, `idle_value_`, `original_`,
    `release_value_`; private `applyIdleView(bool fresh)` (fresh: take `read_status()`) and
    `forgetRelease()` (posted with the bar's `CallAfter`).
  - Seams: `void setSeekObserverForTest(dvv::MoveOnlyFunction<void(std::int64_t)> observer);`
    (called with every value the bar passes to `seek`, after it does);
    `[[nodiscard]] std::uint64_t streamGenerationForTest() const;`.
  - Selftest helper `sendSliderReleasePair(wxSlider&)`: reads `slider.GetValue()` once, then sends
    THUMBRELEASE and CHANGED with that value (GTK's order and value). `sendSliderRelease`
    (THUMBRELEASE alone, macOS) stays.
- Behaviour: every row of the spec §6.2 table except the `cancelScrub`, Cancelled and
  `leaveCancelled` rows (Task 7). Leaving Scrubbing applies the fresh Idle view. Idle seeks
  compare with `idle_value_`; a CHANGED equal to `release_value_` is ignored until `forgetRelease`
  runs. A THUMBTRACK enters Scrubbing only when the left-button probe says the button is down. The
  strip's view per spec §6.3 (Idle and Scrubbing rows). `--debug-transport` traces every state
  change.

- [ ] **Step 1: Write the failing tests** (selftest `player` scenario, after `a lone release does
  nothing` ~`selftest.cpp:1841`; the probe set by `setLeftButtonProbeForTest`)
  - `marker during a held drag`: probe down, tracks to 20 and 35 from an Idle 10 →
    `modelForTest().marker == 10`, `current` text `"35"`; then the release pair → no marker.
  - `a quick release`: from Idle 10, in one action and with no yield: probe down, tracks to 20 and
    35, the release pair, probe up; check in the same action: Idle, slider 35; after the
    notifications settle: the seek observer saw exactly `20, 35`, and the update observer never saw
    a value other than 35 after the release.
  - `a buttonless track stays idle`: probe up, `sendSliderTrack(…, 12)`; in the same action
    `scrubState() == Idle` and no marker; the player then seeks to 12.
  - `a new generation resets`: probe down, track to 30, then `bar.update(s, g)` and
    `bar.update(s, g + 1)` with the same `s` (`g = streamGenerationForTest()`) → Idle, no marker,
    no seek in the observer after the 30; then restore.
  - `a reopen during a drag resets`: probe down, track to 30, reopen the same clip → Idle, no
    marker, no seek back.
  - `another clip during a drag resets`: probe down, track to 30, drop the MKV clip (another
    length) → Idle, no seek back to the original.
  - `play during a drag`: in one action: probe down, track to 25, Space, the release pair, probe
    up → Idle, slider at the fresh `target`, the player still playing after the notifications
    settle; the seek observer saw no seek after the release.
  - The existing steps (`slider to 5`, `held drag on a slow source`, `changed alone seeks and
    stays idle`, `a lone release does nothing`, `stream change resets the scrub`) keep passing; add
    `probe down` where a step now needs it to enter Scrubbing; `held drag` passes the current
    generation to its crafted `update()` and its release check also asserts the slider is at 55.
- [ ] **Step 2: Run to verify they fail** — build `rawreel_gui`; Expected: compile error on
  `setSeekObserverForTest` and the two-argument `update`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** — `ctest --preset ninja-macos -R 'rawreel\.(gui_|panel_lab)' --output-on-failure`;
  Expected: PASS.
- [ ] **Step 5: Commit** — `feat(gui): scrub marker, Idle view and stream generation`

### Task 7: Cancel (U-CP1, U-CP4, U-CP6)

**Files:**
- Modify: `src/gui/transport_bar.h/.cpp`, `src/gui/main_frame.h/.cpp` (`onCharHook`, a new
  `wxEVT_ACTIVATE` handler), `src/gui/selftest.cpp` (`wxEVT_ACTIVATE` joins `REAL_INPUT_TYPES`;
  `sendKey` returns `injectInput`'s result; new steps), `tools/panel_lab/panel_lab.cpp` (Escape
  and activation wired the same way)

**Interfaces:**
- Produces: `ScrubState::Cancelled`; `[[nodiscard]] bool TransportBar::cancelScrub();` (acts only
  in Scrubbing); `void TransportBar::frameActivated(bool active);` (false → `cancelScrub()`, true →
  leave Cancelled); a `wxEVT_LEFT_DOWN` handler on the slider (with `Skip()`) that leaves
  Cancelled; a private `leaveCancelled()` posted as `CallAfter(&TransportBar::leaveCancelled)` from
  the first THUMBRELEASE or CHANGED in Cancelled, doing nothing unless still Cancelled; leaving
  Cancelled applies the fresh Idle view; `[[nodiscard]] static int leaveCancelledRunsForTest();`.
- `MainFrame::onCharHook`: on Escape, `if (transportBar().cancelScrub()) return;` before the
  command lookup (after the existing `closing_` check). `MainFrame` binds `wxEVT_ACTIVATE` →
  `if (!closing_) transportBar().frameActivated(event.GetActive()); event.Skip();`.
- Behaviour: the Cancelled rows of spec §6.2 and §6.3; the seek back is skipped when
  `last_sent_ == original_`.

- [ ] **Step 1: Write the failing tests** (selftest `player` scenario, paused at 10; the probe
  stays down until after each release pair; events through `injectInput`)
  - `escape seeks back`: tracks to 20 and 40, Escape → Cancelled, no marker, `target == 10`;
    `sendSliderTrack(…, 45)` → no seek; release pair at 45, probe up → the check polls until Idle,
    slider 10, `position == 10`, `current` text `"10"`; the player is paused; the seek observer saw
    `20, 40, 10` only.
  - `deactivation cancels`: track to 30, inject `wxActivateEvent(wxEVT_ACTIVATE, false)` on the
    frame → Cancelled and a seek back to 10.
  - `cancelled ends without a release`: cancel, then an injected `wxEVT_LEFT_DOWN` on the slider
    → Idle; cancel again, then an injected `wxActivateEvent(true)` → Idle; cancel again, then
    `update()` with the probe up → Idle.
  - `a range change while cancelled`: cancel, then a crafted status with another length → Idle,
    no seek; then restore.
  - `keys during a drag`: track to 30, Space, Right, Escape → `target == 10` and paused (U-CP6).
  - `escape outside a drag`: in Idle, `sendKey(frame, WXK_ESCAPE)` returns false and the seek
    observer saw nothing.
  - `a pending call after the bar is gone`: a temporary `wxFrame` holding a second `TransportBar`
    with no-op callbacks and the probe down: track, `cancelScrub()`, the release pair, then
    `bar->Destroy()` (a child: deleted at once) in the same action; yield twice; then destroy the
    frame → `leaveCancelledRunsForTest()` did not grow.
- [ ] **Step 2: Run to verify they fail** — build `rawreel_gui`; Expected: compile error on
  `cancelScrub`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** — `ctest --preset ninja-macos -R 'rawreel\.(gui_|panel_lab|core_)' --output-on-failure`;
  Expected: PASS, including the About and dialog steps (the activation filter must not break them).
- [ ] **Step 5: Commit** — `feat(gui): cancel a drag with Escape or deactivation`

### Task 8: Findings, manual scenarios and the README

**Files:**
- Modify: `README.md` (~line 199, the window description), 
  `docs/superpowers/specs/2026-10-19-rawreel-design.md` (a one-line superseded note at the
  transport-bar description, ~line 1141, pointing to the new spec),
  `docs/superpowers/specs/2026-11-09-video-control-panel-design.md` (new §9a and §14)

- [ ] **Step 1: Re-run the lab** — `rawreel-panel-lab --report` on the final branch (with the
  README's macOS environment) for the "after" best size.
- [ ] **Step 2: Write §14 Findings**: the Task 3 and Step 1 `--report` output for macOS (the
  travel formula and the before/after best sizes), the CI results of `rawreel.panel_lab.report` on
  Windows and Linux, and a table of the items marked "manual, not run": the current label over the
  drawn thumb at min, middle and max per platform and DPI; Escape and deactivation during a native
  drag and their order (spec §6.5); `wxEVT_LEFT_DOWN` on a native slider; VoiceOver, Narrator and
  Orca names; Loop's on state and icon contrast on macOS; the strip repainting during the macOS
  tracking loop.
- [ ] **Step 3: Write §9a Manual scenarios**, each with the lab or app command line and the
  expected result: drag both ways; endpoints; five-digit labels (`--frames 99999`); resize around
  400 DIP; a DPI change (move between monitors); a dark-mode switch; Escape and a window switch
  during a drag; keyboard and wheel on a focused slider; Loop's on state; the label over the thumb.
- [ ] **Step 4: Update the README and the superseded note.** The README describes the two-row
  panel and says Escape during a drag returns to the frame where the drag started. It says nothing
  about macOS until the manual check is recorded in §14.
- [ ] **Step 5: Verify** — `grep -rn "frame 9999999" src` (no match), the full suite
  `ctest --preset ninja-macos --output-on-failure`, and `cmake --workflow --preset ninja-macos-asan`.
  Expected: no match; PASS.
- [ ] **Step 6: Commit** — `docs: control panel findings, manual scenarios and README`

## Self-review notes

- **Ruling R1 (spec §10 order):** the spec's Stage 1 lab compiles the real panel classes, so the
  human measurements need Tasks 4–7 first. The plan builds the automatic part of Stage 1 first
  (Task 3: the travel formula with a ≤ 2 px self-check that CI runs on all three platforms) and
  moves the manual items to Task 8's checklist, using the spec's initial constants until then.
  Cost if wrong: a constant or a platform formula changes after the user's manual run.
- **Ruling R2:** `streamGeneration` publishes the existing `stream_set_` counter rather than a new
  one: it is already bumped exactly where the stream set is replaced or dropped. Cost if wrong: an
  extra bump site shows as a harmless Idle reset.
- **Ruling R3:** the Lucide notices entry lives in the icons task (Task 4), so no commit ships the
  icons without their licence. Cost if wrong: none (the PR is squashed).
- **Ruling R4 (spec revision 3, approved by the user):** leaving Scrubbing or Cancelled reads the
  session's current status, and a CHANGED equal to the release value is ignored until a posted
  `forgetRelease`. Both reviews found that the last delivered status can be older than the bar's
  own last seek (the thumb jumped back) and that GTK's trailing CHANGED then seeked again and
  paused a playback started during the drag. Cost if wrong: one extra state read per release.
- Spec coverage: §3 → T6; §5.1 → T4; §5.2 → T2, T5; §5.3 → T2, T3, T5; §6.1–6.3 → T6, T7;
  §6.4–6.5 → T7, T8; §7 → T4; §8 → T5; §9 → every task's tests; §10 → T3, T8; §11 → T6, T7, T8.
- Not covered automatically by design: native mouse drags (synthetic scroll events skip the
  native control) and screen readers — Task 8's manual list.
- Plan reviews (optimistic: C0 I8 M13 S3; pessimistic: C1 I11 M9) are addressed in this revision.
