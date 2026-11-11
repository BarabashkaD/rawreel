# Window title, status bar file field and Info window — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The open stream's name in the window title (per OS) and in a new status bar field
whose tooltip shows the full path. A floating Info window with 8 rows replaces the docked Info
pane. It opens from a new "i" toggle button; a new Format button sits next to Open.

**Architecture:**
- The names, the full path, the title text, the Info values and the window placement are wx-free
  functions in `src/core`, unit tested on any OS.
- `MainFrame` applies the title and the status field. A new `InfoWindow` (a `wxFrame` tool
  window owned by `MainFrame`) shows the rows.
- `TransportBar` gets two buttons through its existing `setUp`.
- The docked Info pane, `InfoPanel` and `formatInfo` are removed once the window carries
  everything.

**Tech Stack:** C++23, wxWidgets 3.3.4 (wxOSX, wxMSW, wxGTK3), doctest, the rawreel `--selftest`
scripts, CMake/Ninja.

**Spec:** `docs/superpowers/specs/2026-11-10-window-title-and-info-design.md` (rev 3). § numbers
below are the spec's.

**Branch:** `feat/title-and-info`, stacked on `feat/app-icon` (the icon PR). The spec commit is
cherry-picked onto it. If `feat/app-icon` is rebased before this branch merges, rebase this
branch onto it.

## Global Constraints

- `src/core` stays wx-free. No timers or polling, tests included: GUI effects are checked in a
  later event-loop turn (`CallAfter`, the selftest's next step).
- Native per OS. The `#if` choices the spec fixes:
  - title form (§5);
  - label alignment: right on macOS, left elsewhere (§11);
  - label colour: macOS `NSColor.secondaryLabelColor`, GTK `dim-label`, Windows the normal window
    text (§11; ruling: the spec's first choice, as File Properties);
  - `wxToolTip::SetMaxWidth` on Windows only (§7).
- Path ↔ text only through `dvv::media::utf8FromPath` / `pathFromUtf8`; never `path::string()`.
- Selftests:
  - never show native dialogs or popup menus (hooks only);
  - never depend on the real OS focus (`setFocusForTest`, key events sent to the window);
  - never touch the user's files;
  - check `IsShown()`, never on-screen visibility.
- Build:
  - clang-format 19.1.7 (`$HOME/.claude/venvs/dvv-build-tools/bin/clang-format`);
  - no warnings (MSVC /W4 /WX and GCC/Clang -Werror in CI; `-Wswitch` makes every `switch` on
    `Command` and `Icon` list the new values);
  - clang-tidy naming: private members `lower_case_`, methods `camelBack`, constants
    `UPPER_CASE`.
- Commits:
  - identity BarabashkaD / 6252647+BarabashkaD@users.noreply.github.com;
  - a `Co-Authored-By` trailer;
  - no machine paths, real names or e-mail in committed files;
  - never `--no-verify` (the pre-push privacy lint runs).
- Tests per task: only the task's tests (named in each task). The full suites run once, serially,
  in Task 7. GUI tests never run in parallel. `--selftest about` is not run (About does not
  change).
- Build tree: `libs/_build/rawreel/ninja-macos`. Commands:
  - `B` = `cmake --build libs/_build/rawreel/ninja-macos --config Debug`
  - core tests:
    `$B --target rawreel_core_tests && libs/_build/rawreel/ninja-macos/tests/rawreel_core_tests --test-case='<pattern>'`
  - smoke:
    `$B --target rawreel_gui && ctest --test-dir libs/_build/rawreel/ninja-macos -R 'rawreel.gui_selftest.smoke$' --output-on-failure`
  - player:
    `ctest --test-dir libs/_build/rawreel/ninja-macos -R 'rawreel.gui_selftest.player$' --output-on-failure`
- Text is UTF-8 in `std::string`. "×" is U+00D7, "–" U+2013, "→" U+2192, "·" U+00B7, "—" U+2014.
  Write them as `\u` escapes or UTF-8 byte escapes, as `status_text.cpp` does.
- README and comments state facts only.

## Review Focus

1. **A long or non-ASCII file name** (a 120-character UTF-8 name at the window's minimum width).
   - The file field cuts in the middle and keeps the extension.
   - The tooltip shows the whole path.
   - rawreel does not cut the title.
   - Pinned by Task 1 "stream name keeps UTF-8" and Task 3's "long file name" step.
2. **No room right of the main window** (maximised, near the right edge, a second display at
   negative coordinates). The Info window goes inside the right edge, on the same display.
   Pinned by Task 4's `infoWindowPlace` unit tests.
3. **Minimise, restore and quit with the Info window shown.**
   - It hides with the main window and returns without taking the focus.
   - Quitting destroys it without a crash.
   - Pinned by Task 4's iconize step. Task 5 leaves the window shown, so the scenario's own close
     covers the quit.
4. **Keys while the Info window has the focus.**
   - Space plays.
   - Esc and Ctrl/Cmd+I close it.
   - Ctrl+C copies (Windows/Linux).
   - ⌘W closes it, not the stream (macOS).
   - Pinned by Task 5's key steps.
5. **Stream changes while it is open** (open, Format change, close). The rows follow, "No stream"
   shows after close, and the window's size never changes. Pinned by Task 4's rows step (size
   compared across the changes).

---

### Task 1: Stream name, full path and window title (core)

**Files:**
- Create: `src/core/stream_name.h`, `src/core/stream_name.cpp`, `tests/test_stream_name.cpp`
- Modify:
  - `CMakeLists.txt` (core sources, next to `src/core/open_text.cpp`);
  - `tests/CMakeLists.txt` (`rawreel_core_tests` sources);
  - `src/gui/format_dialog.cpp:167-181` (`formatDialogTitle`);
  - `src/gui/main_frame.cpp:122-129` (`fileName`, removed).

**Interfaces:**
- Produces (namespace `rawreel`):
  - `enum class PathStyle : std::uint8_t { Posix, Windows };`
  - `inline constexpr PathStyle NATIVE_PATH_STYLE` (Windows under `_WIN32`, else Posix)
  - `enum class TitleStyle : std::uint8_t { MacOs, Windows, Linux };`
  - `inline constexpr TitleStyle NATIVE_TITLE_STYLE` (by `__APPLE__`, `_WIN32`, else Linux)
  - `[[nodiscard]] std::string streamName(std::string_view resolved_uri);`
  - `[[nodiscard]] std::string streamFullPath(std::string_view resolved_uri, std::string_view base_folder, PathStyle style);`
  - `[[nodiscard]] std::string windowTitle(std::string_view name, TitleStyle style);`

- [ ] **Step 1: Write the failing tests** in `tests/test_stream_name.cpp` (doctest, the
  `test_info_text.cpp` style). Each `CHECK` below is one assertion.

```cpp
TEST_CASE("stream name: a file URI gives the base name without the query") {
    CHECK(streamName("file:clip.yuv?fmt=v210&size=1920x1080") == "clip.yuv");
    CHECK(streamName("file:/data/in/clip.yuv") == "clip.yuv");
    CHECK(streamName("file:X:\\data\\clip.yuv") == "clip.yuv");
}
TEST_CASE("stream name keeps UTF-8") {
    CHECK(streamName("file:/data/\xD0\xBA\xD0\xB0\xD0\xB4\xD1\x80.yuv") == "\xD0\xBA\xD0\xB0\xD0\xB4\xD1\x80.yuv");
}
TEST_CASE("stream name: another URI gives the scheme and the path") {
    CHECK(streamName("lavfi:testsrc2=size=1280x720") == "lavfi:testsrc2=size=1280x720");
    CHECK(streamName("synthetic:gradient?fmt=yuv420p&size=64x48") == "synthetic:gradient");
}
TEST_CASE("stream name: empty and unparsable") {
    CHECK(streamName("") == "");
    CHECK(streamName("not a uri") == "not a uri");  // the text as it is (as fileName did)
}
TEST_CASE("full path: POSIX") {
    CHECK(streamFullPath("file:clip.yuv?fmt=v210", "/data/u/v", PathStyle::Posix) == "/data/u/v/clip.yuv");
    CHECK(streamFullPath("file:../in/./clip.yuv", "/data/u/v", PathStyle::Posix) == "/data/u/in/clip.yuv");
    CHECK(streamFullPath("file:/abs/clip.yuv", "/data/u", PathStyle::Posix) == "/abs/clip.yuv");
}
TEST_CASE("full path: Windows") {
    CHECK(streamFullPath("file:clip.yuv", "X:\\work\\v", PathStyle::Windows) == "X:\\work\\v\\clip.yuv");
    CHECK(streamFullPath("file:X:/in/clip.yuv", "X:\\u", PathStyle::Windows) == "X:\\in\\clip.yuv");
    CHECK(streamFullPath("file:\\\\server\\share\\clip.yuv", "X:\\u", PathStyle::Windows) == "\\\\server\\share\\clip.yuv");
}
TEST_CASE("full path: another URI is the resolved URI with its query") {
    CHECK(streamFullPath("lavfi:testsrc2=size=1280x720", "/x", PathStyle::Posix) == "lavfi:testsrc2=size=1280x720");
    CHECK(streamFullPath("synthetic:gradient?fmt=yuv420p", "/x", PathStyle::Posix) == "synthetic:gradient?fmt=yuv420p");
    CHECK(streamFullPath("", "/x", PathStyle::Posix) == "");
}
TEST_CASE("window title per OS") {
    CHECK(windowTitle("clip.yuv", TitleStyle::MacOs) == "clip.yuv");
    CHECK(windowTitle("clip.yuv", TitleStyle::Windows) == "clip.yuv - rawreel");
    CHECK(windowTitle("clip.yuv", TitleStyle::Linux) == "clip.yuv \u2014 rawreel");  // as UTF-8
    for (auto style : {TitleStyle::MacOs, TitleStyle::Windows, TitleStyle::Linux}) {
        CHECK(windowTitle("", style) == "rawreel");
    }
}
```

- [ ] **Step 2: Run them and watch them fail.**
  Run: core tests with `--test-case='stream name*,full path*,window title*'`.
  Expected: build error, because `core/stream_name.h` does not exist.

- [ ] **Step 3: Implement the three functions** in `src/core/stream_name.cpp` with
  `dvv::parseUri`.
  - Name: the base name is after the last `/` or `\`.
  - Full path: pure string work, no `std::filesystem`, so both styles work on any OS.
    - Absolute in POSIX style: a leading `/`. In Windows style: `X:\`, `X:/`, or a leading `\\`
      or `//`.
    - A relative path is joined to `base_folder`.
    - `.` and `..` segments are resolved lexically.
    - Windows style writes backslashes.
  - Header comment: the spec's §4 rules in one paragraph.

- [ ] **Step 4: Replace the two old name helpers.**
  - `formatDialogTitle` takes `streamName(uri)` as its name. Its `TITLE_MAX` cut and its
    "Format of the stream - " prefix stay.
  - `MainFrame`'s `fileName()` is deleted; its one caller (the trailing-bytes warning in
    `onStreamsChanged`) uses `streamName`.

- [ ] **Step 5: Run them and watch them pass**, then the format dialog's existing tests.
  Run:
  - core tests `--test-case='stream name*,full path*,window title*'`;
  - the smoke selftest (it shows the Format dialog title).
  Expected: all pass.

- [ ] **Step 6: Commit.**
  `feat(core): stream name, full path and window title per OS; one name rule for the Format dialog and the infobar`

---

### Task 2: Info values and the "i" hint (core)

**Files:**
- Modify: `src/core/info_text.h`, `src/core/info_text.cpp`, `tests/test_info_text.cpp`.
  `formatInfo` stays until Task 5 removes it.

**Interfaces:**
- Produces:

```cpp
struct InfoRow {
    std::string_view label;  // "Resolution", "Frame rate", "Format", "Standard", "Matrix",
                             // "Range", "Transfer", "Tone mapping" (this order)
    std::string value;
};
inline constexpr std::size_t INFO_PICTURE_ROWS = 3;  // rows 0..2 under "Picture", 3..7 "Colour"
[[nodiscard]] std::vector<InfoRow> infoRows(const StreamInfo& stream);  // always 8 rows
// "1920 × 1080 · 50 fps · v210 · BT.709 limited" (§13)
[[nodiscard]] std::string infoHint(const StreamInfo& stream);
// Rows as "Label: value" lines joined with '\n'; nullptr: "No stream" (§10 Copy)
[[nodiscard]] std::string infoCopyText(const StreamInfo* stream);
// The fixed samples that size the value column (§11 Size)
[[nodiscard]] std::span<const std::string_view> infoValueSamples() noexcept;
```

- [ ] **Step 1: Write the failing tests** in `tests/test_info_text.cpp`.
  - Reuse its `stream()` helper, extended with a format name and fps arguments.
  - Add a helper `row(stream, "Range")` that returns the value.
  - One `TEST_CASE` per row family, named `info row: ...`, with these exact values (§11 table):
    - **Resolution:** 1920x1080 → `1920 × 1080`.
    - **Frame rate:**
      - {50,1} → `50 fps`;
      - {100,2} → `50 fps`;
      - {30000,1001} → `29.97 fps`;
      - {24000,1001} → `23.976 fps`;
      - {0,0} → `Unspecified` (ruling: a rate with `den == 0`);
      - an unbounded stream (no `frameCount`) still shows its rate.
    - **Format:**
      - `yuv420p` → `YUV 4:2:0 8-bit (yuv420p)`;
      - `v210` → `YUV 4:2:2 10-bit (v210)`;
      - `yuv444p` → `YUV 4:4:4 8-bit (yuv444p)`;
      - `rgba` → `RGB 8-bit + alpha (rgba)`;
      - `gray16le` → `Gray 16-bit (gray16le)`;
      - `yuv411p` → `yuv411p`;
      - `rgb565le` → `rgb565le`.
    - **Standard:**
      - a stream whose URI has `colour=bt709` → `BT.709`, even when the stream's own standard
        differs (precedence);
      - a BT.2100 PQ stream → `BT.2100 PQ`;
      - no matching standard → `None`.
    - **Matrix:**
      - Bt709 → `BT.709`; Bt601 → `BT.601`; Bt2020Ncl → `BT.2020`; Unspecified → `Unspecified`;
      - Custom kr 0.299, kb 0.114 → `Custom (Kr 0.2990, Kb 0.1140)`;
      - an `rgba` and a `gray` stream → `Not used`;
      - `ColourMatrix::Rgb` on a YUV stream → `Identity` (ruling: not in the spec).
    - **Range:**
      - limited 8-bit → `Limited (16–235)`;
      - limited 10-bit → `Limited (64–940)`;
      - limited 16-bit → `Limited (4096–60160)`;
      - full 10-bit → `Full (0–1023)`;
      - full 8-bit → `Full (0–255)`;
      - Unspecified → `Unspecified`;
      - the depth is the Y component's bits for YUV and Gray, the first component's for RGB.
    - **Transfer:** `BT.1886`, `sRGB`, `PQ`, `HLG`, `Linear`, `Unspecified`.
    - **Tone mapping:** PQ and HLG → `BT.2390, 1000 → 100 nits`; Bt1886 and Srgb → `None (SDR)`.
    - **Hint:**
      - v210 1920x1080 at 50/1, BT.709 limited → `1920 × 1080 · 50 fps · v210 · BT.709 limited`;
      - standard None with full range → `... · full` (the range word alone);
      - Unspecified range → the standard alone.
    - **Copy:**
      - `infoCopyText(nullptr) == "No stream"`;
      - for a stream: 8 lines, the first `Resolution: 64 × 48`.
    - **Samples:** `infoValueSamples()` holds `BT.2390, 1000 → 100 nits`,
      `YUV 4:2:2 10-bit (yuv422p10le)` and `Custom (Kr 0.2990, Kb 0.1140)`.

- [ ] **Step 2: Run them and watch them fail.**
  Run: core tests `--test-case='info row*,info hint*,info copy*,info samples*'`.
  Expected: build error (`infoRows` undeclared).

- [ ] **Step 3: Implement** in `info_text.cpp`.
  - The standard reuses the precedence code of today's `colourLines` (URI `colour=` preset, else
    `streamColourBase(stream).standard`, label from `colourPresetInfo(...).label`). Factor it into
    one function used by both until Task 5.
  - Format: from `dvv::media::descriptor(format)`.
    - The chroma comes from the U component's `log2SubX/log2SubY`: (0,0) → 4:4:4, (1,0) → 4:2:2,
      (1,1) → 4:2:0. Anything else gives the name alone.
    - "N-bit" needs every non-A component to have the same `bits`; otherwise the name alone.
    - "+ alpha" is added when an A component exists.
  - Range limits at depth b: limited `16·2^b/256 – 235·2^b/256`, full `0 – 2^b−1`.
  - Fractional rate: `{:.3f}` with trailing zeros (and a trailing `.`) removed.

- [ ] **Step 4: Run them and watch them pass**, plus the old `formatInfo` cases.
  Run: core tests `--test-case='info*,format info*'`, or the file's full case list.
  Expected: PASS.

- [ ] **Step 5: Commit.**
  `feat(core): Info window values and the Info button's summary line`

---

### Task 3: Window title and status bar file field (GUI, Part 1)

**Files:**
- Modify: `src/gui/main_frame.h`, `src/gui/main_frame.cpp`;
- `src/gui/native_controls.h`, `src/gui/native_controls_cocoa.mm`;
- `src/gui/selftest.cpp`;
- spec §7 (one line: the measured minimum width).

**Interfaces:**
- Consumes: Task 1 `streamName`, `streamFullPath`, `windowTitle`, `NATIVE_*`.
- Produces (public on `MainFrame`):

```cpp
static constexpr std::size_t FILE_FIELD = 0, FIRST_TICK_FIELD = 1, MAPPING_FIELD = 4,
                             LOG_NOTICE_FIELD = 5, STATUS_FIELD_COUNT = 6;
[[nodiscard]] std::array<wxString, STATUS_FIELD_COUNT> statusTexts() const;  // was 5
[[nodiscard]] std::optional<int> statusFieldAt(const wxPoint& point) const;  // bar client coords
[[nodiscard]] wxString statusTooltipFor(std::optional<int> field) const;     // "" = none
[[nodiscard]] const wxString& streamPath() const noexcept;  // streamFullPath(...) or ""
```

  - In `native_controls.h`, under `#if defined(__WXOSX__)`:
    `[[nodiscard]] wxString representedFilename(wxTopLevelWindow& window);`
    (the NSWindow's `representedFilename`).

- [ ] **Step 1: Write the failing selftest steps** in `src/gui/selftest.cpp`.
  - **smoke**, right after `openStreamStep` (a `synthetic:` stream), the step
    `"title and file field"`:
    - `GetTitle()` equals `windowTitle(streamName(uri), NATIVE_TITLE_STYLE)`;
    - `statusTexts()[FILE_FIELD]` equals `streamName(uri)`;
    - the point at the centre of `GetFieldRect(FILE_FIELD)` gives
      `statusTooltipFor(statusFieldAt(p)) == streamPath() == uri`;
    - macOS: `representedFilename(frame).empty()`.
  - **player**, right after `"drop two files"`: `"title of a file"`:
    - the name is `first_32x16_gray.yuv`;
    - `streamPath()` is that file's absolute path through `utf8FromPath`;
    - the tooltip over field 0 is that path;
    - macOS: `representedFilename` is that path.
  - **player**, `"long file name"`:
    - write a 32x16 gray file named
      `std::string(100, 'a') + "\xD0\xBA\xD0\xB0\xD0\xB4\xD1\x80_32x16_gray.yuv"` into the
      scenario's temporary folder and open it (`openFiles`);
    - set the client width to `GetMinClientSize().x`;
    - in a later step: the field text differs from the name, contains `…` (U+2026), and ends
      with `_gray.yuv`;
    - the tooltip over field 0 is the full path.
  - **player**, `"title after close"`: execute `CloseStreams`; in the next turn:
    - the title is `rawreel`;
    - the file field is empty;
    - macOS: the represented filename is empty;
    - `statusTooltipFor(statusFieldAt(centre of field 0))` is empty.
  - **smoke**, `"cut field tooltip"`:
    - set the client width to the minimum;
    - for a field `n` in 1..3 with `GetStatusBar()->GetField(n).IsEllipsized()`, its tooltip
      equals its full text, and an uncut field gives `""`;
    - the step passes vacuously, and logs "no cut field", when none is cut at the minimum.
  - Replace every literal status index in `selftest.cpp` with the constants: `statusTexts()[4]`
    becomes `[LOG_NOTICE_FIELD]`, `[3]` becomes `[MAPPING_FIELD]`, `[0]` becomes
    `[FIRST_TICK_FIELD]`.

- [ ] **Step 2: Run the smoke and player selftests and watch them fail.**
  Expected: the build fails (`FILE_FIELD`, `statusFieldAt` undeclared).

- [ ] **Step 3: Implement in `MainFrame`.**
  - **`buildStatusBar`.**
    - Create the bar with `CreateStatusBar(STATUS_FIELD_COUNT, wxSTB_DEFAULT_STYLE & ~wxSTB_SHOW_TIPS)`.
    - Widths `{-3, -3, -2, -2, mapping, notice}`.
    - Measure the two fixed widths in one helper `statusFixedWidth()`; `applyLayoutLimits` uses it
      too.
    - `#if defined(__WXMSW__)`: call `wxToolTip::SetMaxWidth(FromDIP(800))`.
    - Bind on the bar:
      - `wxEVT_MOTION`: when `statusFieldAt` changes from the last field, `UnsetToolTip()` and
        then `SetToolTip(statusTooltipFor(field))` when it is not empty;
      - `wxEVT_LEAVE_WINDOW`: unset;
      - `wxEVT_SIZE`: `Skip()`, then `CallAfter` re-cuts field 0 after wx has laid out the
        fields.
  - **The tick, the mapping and the notice.** `onStatusChanged` writes field
    `FIRST_TICK_FIELD + i`. The mapping and the notice use `MAPPING_FIELD` and
    `LOG_NOTICE_FIELD`. `onStatusBarLeftUp` uses `statusFieldAt(...) == LOG_NOTICE_FIELD`.
  - **`showStreamName()`**, a new private method called from `onStreamsChanged` next to the
    mapping field:
    - stores the name and the path (`streamFullPath(uri, utf8FromPath(std::filesystem::current_path()), NATIVE_PATH_STYLE)`);
    - calls `SetTitle(windowTitle(name, NATIVE_TITLE_STYLE))`;
    - calls `SetRepresentedFilename(path)` for a `file:` stream, `""` otherwise;
    - writes field 0 through `cutFileField()`.
  - **`cutFileField()`:**
    `wxControl::Ellipsize(name, dc, wxELLIPSIZE_MIDDLE, rect.width - FromDIP(8))` against
    `GetFieldRect(FILE_FIELD)`. `status_texts_[FILE_FIELD]` keeps the shown (cut) text.
  - **`applyLayoutLimits`:** `minimum.x = max(minimum.x, statusFixedWidth() + FromDIP(120))`.
    - Record the figure at 100 % on macOS in spec §7 ("measured: N DIP"), replacing "the builder
      measures ... records the resulting figure here".
    - Ruling: the spec asks for that line; it is the only spec edit in this plan.
  - **`representedFilename`** in `native_controls_cocoa.mm`: read the NSWindow's
    `representedFilename` (the `wxTopLevelWindow`'s `GetWXWindow()`).

- [ ] **Step 4: Run the smoke and player selftests.** Expected: both pass. Read the new steps'
  `seen` lines in the output.

- [ ] **Step 5: Commit.**
  `feat(gui): file name in the window title and in a status bar field with the full path as its tooltip`

---

### Task 4: The Info window and View > Info

**Files:**
- Create: `src/gui/info_window.h`, `src/gui/info_window.cpp`
- Modify:
  - `CMakeLists.txt` (GUI sources);
  - `src/core/commands.h`, `src/core/commands.cpp`, `tests/test_commands.cpp`;
  - `src/core/layout.h`, `src/core/layout.cpp`, `tests/test_layout.cpp`;
  - `src/gui/native_controls.h` and its `_cocoa.mm`, `_gtk.cpp`, `_msw.cpp`, `_stub.cpp`;
  - `src/gui/main_frame.h`, `src/gui/main_frame.cpp`, `src/gui/selftest.cpp`.

**Interfaces:**
- Consumes: Task 2 `infoRows`, `INFO_PICTURE_ROWS`, `infoValueSamples`.
- Produces:
  - `Command::InfoWindow` right after `Command::LogPane`, with the table row
    `{Command::InfoWindow, "info_window", "View", "Info", "Show or hide the Info window", ctrlKey('I'), ACC, true}`.
  - In `core/layout.h`:

```cpp
struct ScreenRect { int x = 0, y = 0, width = 0, height = 0; bool operator==(const ScreenRect&) const = default; };
// §10 Place: right of `frame` (top level with frame.y, left = frame right + gap) when that fits
// in `work_area`; else inside the frame's right edge: x = min(frame right, work right) - gap -
// size.width, y = client.y + gap; clamped into `work_area`.
[[nodiscard]] ScreenRect infoWindowPlace(ScreenRect frame, ScreenRect client, LayoutSize size,
                                         ScreenRect work_area, int gap) noexcept;
```

  - In `native_controls.h`: `void setSecondaryText(wxWindow& label);`
    - macOS: `secondaryLabelColor`, resolved for the window's effective appearance;
    - GTK: add the `dim-label` style class;
    - Windows and the stub: nothing.
  - `class InfoWindow final : public wxFrame` with:
    - `InfoWindow(MainFrame& owner)`;
    - `void showFor(const StreamInfo* stream)` (updates the rows or "No stream");
    - `void toggle()`;
    - `void hideKeepingPlace()`;
    - `void restoreAfterIconize()`;
    - `[[nodiscard]] std::vector<std::pair<std::string, std::string>> rowsForTest() const` (empty
      while "No stream" shows);
    - `[[nodiscard]] wxRect requestedRectForTest() const`.
  - On `MainFrame`:
    - `[[nodiscard]] bool infoShown() const;`
    - `void setInfoShown(bool shown);`
    - `[[nodiscard]] InfoWindow* infoWindow() const noexcept;` (null before the first show).

- [ ] **Step 1: Write the failing unit tests.**
  - `test_commands.cpp`:
    - `commandTable()[size_t(Command::InfoWindow)]` has id `info_window`, menu `View`, label
      `Info`, key Ctrl+I, route Accelerator, checkable;
    - it is the entry after `LogPane`.
  - `test_layout.cpp`, `TEST_CASE("info window place: ...")`, with frame `{100,100,800,600}`,
    client `{100,128,800,572}`, size `{240,260}`, gap 8:
    - work area `{0,0,1920,1080}` → `{908,100,240,260}`;
    - work area `{0,0,1000,1080}` (no room) → `{652,136,240,260}`;
    - maximised: frame equals the work area `{0,25,1440,875}`, client `{0,53,1440,847}` →
      `{1192,61,240,260}`;
    - a display left of the primary (work `{-1920,0,1920,1080}`, frame
      `{-1500,100,800,600}`, client `{-1500,128,800,572}`) → `{-692,100,240,260}`;
    - the same display, frame `{-1000,100,800,600}`, client `{-1000,128,800,572}` (no room) →
      `{-448,136,240,260}`;
    - the result always lies inside the work area.

- [ ] **Step 2: Run them and watch them fail.**
  Run: core tests `--test-case='*command*,info window place*'`.
  Expected: build error (`Command::InfoWindow`, `infoWindowPlace`).

- [ ] **Step 3: Implement the command and the placement.**
  - Add `InfoWindow` to `Command`. `COMMAND_COUNT` still follows `About`.
  - Add the table row.
  - Implement `infoWindowPlace`.
  - Every `switch` on `Command` (`isAvailable`: always true; `execute`: `setInfoShown(!infoShown())`)
    lists it.
  - In `buildMenus`, `Command::InfoWindow` follows the `LogPane` pattern:
    - macOS: `infoItemTitle(shown)` returns "Show Info" / "Hide Info", and `onUpdateUi` keeps it;
    - Windows and Linux: a check item.

- [ ] **Step 4: Implement `InfoWindow`** (§10, §11).
  - **Frame style:** `wxCAPTION | wxCLOSE_BOX | wxFRAME_TOOL_WINDOW | wxFRAME_FLOAT_ON_PARENT`,
    parent the `MainFrame`, title `Info`. Call `EnableFullScreenView(false)`.
  - **Content.** One `wxPanel` that accepts the focus (`SetCanFocus(true)`, no visible ring). It
    holds a `wxSimplebook`:
    - page 0: two groups. Each has a bold heading ("Picture", "Colour") over a
      `wxFlexGridSizer(2)`.
      - The value column's width is the widest `infoValueSamples()` text (`GetTextExtent`). The
        label column is as wide as the widest of the 8 labels, in both groups.
      - Column gap `FromDIP(12)`; one line between the groups; `wxSizerFlags().Border()` margins.
      - Labels use `wxALIGN_RIGHT` under `__WXOSX__`, left elsewhere.
      - `setSecondaryText(label)` on each label.
      - Each value's accessible name is `"<Label>, <value>"`, via `setAccessibleName`.
    - page 1: "No stream", centred, `setSecondaryText`.
  - **Size:** `Fit()` once; the size is fixed after that. It fits again only on
    `wxEVT_DPI_CHANGED` and `wxEVT_SYS_COLOUR_CHANGED` (font change), keeping the top-left corner.
    On `wxEVT_SYS_COLOUR_CHANGED`, call `setSecondaryText` again on every label.
  - **Show:**
    - use the remembered place if `wxDisplay::GetFromPoint(place.GetTopLeft()) != wxNOT_FOUND`;
    - otherwise `infoWindowPlace(...)`, with the work area of
      `wxDisplay(wxDisplay::GetFromWindow(&owner)).GetClientArea()`, the gap `FromDIP(8)`, and
      `client` = the owner's client area in screen coordinates;
    - store that rectangle for `requestedRectForTest()`;
    - `SetPosition`, then `ShowWithoutActivating()`.
  - **Close box:** `wxEVT_CLOSE_WINDOW` → `Veto()` when `CanVeto()`, then hide, remembering the
    place. When it cannot veto (the owner is closing), `Destroy()`.
- [ ] **Step 5: Wire it into `MainFrame`.**
  - The `InfoWindow` is created on the first show and owned through wx (a child TLW of the
    frame). `onClose` destroys it before the Session goes.
  - `onStreamsChanged` calls `info_window_->showFor(streams_.empty() ? nullptr : &streams_.front())`
    when the window exists.
  - `onIconize`: an iconized frame hides the shown window. Restore shows it again with
    `ShowWithoutActivating()`. Keep a flag `info_hidden_by_iconize_`.
  - The docked Info pane stays for now (Task 5 removes it).

- [ ] **Step 6: Write the selftest steps** (smoke, after `panesStep`). Each one checks in the
  next turn.
  - `"info window opens"`:
    - before the step, remember whether the frame is active;
    - `execute(Command::InfoWindow)`;
    - then: `infoShown()` and `infoWindow()->IsShown()`;
    - Linux: `requestedRectForTest()` equals `infoWindowPlace(...)` computed again;
    - macOS and Windows: `GetScreenRect()` equals the request;
    - `FindFocus()` is not inside the Info window;
    - if the frame was active, it is still active.
  - `"info window rows"`:
    - `rowsForTest()` has 8 entries;
    - `Resolution` is `64 × 48` for the smoke stream, and `Format` matches `infoRows` of
      `frame.streams().front()`;
    - remember `GetSize()`.
  - `"info window after close"`:
    - execute `CloseStreams`;
    - `rowsForTest()` is empty (No stream page) and the size is unchanged;
    - reopen the smoke stream through the same helper `openStreamStep` uses, and check the size
      is still unchanged.
  - `"info window with the main window minimised"`:
    - Windows and macOS: `Iconize(true)`;
    - X11: `Hide()`, as `hideStep` does (`IsIconized` is not testable there; check
      `!IsShown()` of the Info window after `onIconize` or the hide);
    - then restore: shown again, focus not inside.
  - `"view info closes"`: `execute(Command::InfoWindow)` → `!infoShown()`.

- [ ] **Step 7: Run them and watch them pass.**
  Run:
  - core tests `--test-case='*command*,info window place*'`;
  - the smoke selftest.
  Expected: PASS.

- [ ] **Step 8: Commit.**
  `feat(gui): Info window (View > Info, Ctrl/Cmd+I) beside the main window`

---

### Task 5: Info window keys, ⌘W and Copy; the docked Info pane removed

**Files:**
- Modify: `src/gui/info_window.{h,cpp}`, `src/gui/main_frame.{h,cpp}`,
  `src/core/info_text.{h,cpp}`, `tests/test_info_text.cpp`, `src/gui/selftest.cpp`,
  `CMakeLists.txt`
- Delete: `src/gui/info_panel.h`, `src/gui/info_panel.cpp`

**Interfaces:**
- Consumes: Task 4 `InfoWindow`, `infoShown`, `setInfoShown`; Task 2 `infoCopyText`.
- Produces: `MainFrame::infoInFront() const -> wxWindow*`, on every OS. Null unless the focus is
  inside the Info window, or the window is shown and `IsActive()`.

- [ ] **Step 1: Write the failing selftest steps** (smoke). They replace `infoCopyStep` in
  `addEditSteps`.
  - `"info window keys"`:
    - open the window;
    - send a `wxEVT_CHAR_HOOK` key event for Space to the Info window's panel (with
      `setFocusForTest(panel)`);
    - next turn: playback started (`session().status().player.playing`);
    - Space again pauses.
  - `"info window Esc"`: an Esc char-hook event → `!infoShown()`.
  - `"info window Ctrl+I"`: reopen; a Ctrl+I char-hook event (`ControlDown`, so Cmd on macOS)
    → closed.
  - `"info window copy"`:
    - reopen and set the focus to the panel;
    - Windows/Linux: a Ctrl+C char-hook event;
    - macOS: `execute` the Edit menu's `wxID_COPY` through `onEditMenu` (a `wxCommandEvent` sent
      to the frame);
    - the clipboard hook gets `infoCopyText(&frame.streams().front())`.
  - macOS only, `"⌘W closes the Info window"`:
    - with the focus in the panel, `isAvailable(CloseStreams)` is true;
    - `execute(CloseStreams)` closes the window, and the stream count is unchanged;
    - repeat after closing the stream: `CloseStreams` is still available with the window in
      front, and closes it.
  - `closeKeyOtherWindowStep`: its comment's stand-in no longer names "a floating Info pane"
    ("a floating Log pane").
  - The last step leaves the Info window shown, so the scenario's own close covers quitting with
    it open.

- [ ] **Step 2: Run the smoke selftest and watch it fail.** Expected: the build fails, or the
  new steps fail (no char hook, no `infoInFront`).

- [ ] **Step 3: Implement the keys** in `InfoWindow` (§10).
  - `wxEVT_CHAR_HOOK` on the window:
    - Esc → hide;
    - Ctrl/Cmd+I → `owner.execute(Command::InfoWindow, CommandSource::Key)`;
    - Windows/Linux Ctrl+C → `setClipboardText(infoCopyText(...))`;
    - anything else → `commandForKey(normaliseKey(...), false)`, then `owner.execute(command, Key)`
      when one matches, else `Skip()`.
  - `MainFrame`:
    - `isAvailable(CloseStreams)` on macOS is
      `hasStream() || settingsInFront() || infoInFront()`;
    - `execute(CloseStreams)` hides the Info window first when it is in front;
    - `onEditUpdateUi`/`onEditMenu` handle `wxID_COPY` with `infoInFront()` (the copy text),
      before `runEdit`.

- [ ] **Step 4: Remove the docked pane** (§10 last bullet).
  - Delete the AUI pane `"info"`, `InfoPanel` (files and the CMake line),
    `MainFrame::infoPanel()`, `info_`, `INFO_MIN_WIDTH_DIP`, `formatInfo` and its private helpers
    that nothing uses any more, and the `formatInfo` test cases.
  - Update `main_frame.h`'s header comment, and the `settingsInFront` comment ("a floating AUI
    pane (Log)").
  - Rewrite the selftest users:
    - `infoHas(frame, field)` → checks on `frame.streams().front().colour` (matrix, range,
      transfer) and on `infoRows` (standard). The two D5R §4.5 checks keep their meaning: BT.601
      (PAL) full range → `Standard == "BT.601 (PAL)"`, `Matrix == "BT.601"`,
      `Range` starts with `Full`; PQ → `Transfer == "PQ"`, `Standard == "BT.2100 PQ"`.
    - The "range limited" check after Format: `Range` starts with `Limited`.
    - `panesStep`: no Info pane; the Log pane is hidden and the player is the centre.
    - `movedPaneStep` is rewritten with the Log pane (ruling: the user can still dock Log on any
      side, so DockManager's two-direction code stays):
      - show the Log pane, dock it `Right().Row(0)`, and shrink to `100x80` around the viewport;
      - `back` docks it `Bottom().Row(0)` and hides it again;
      - the checks are unchanged (viewport ≥ 320x240 DIP, presented extent = client).

- [ ] **Step 5: Run the core Info tests and the smoke and player selftests.**
  Run:
  - core tests `--test-case='info*'`;
  - smoke;
  - player.
  Expected: all pass. `grep -rn "InfoPanel\|formatInfo\|infoPanel" src tests` prints nothing.

- [ ] **Step 6: Commit.**
  `feat(gui): Info window keys, Cmd+W and Copy; the docked Info pane is removed`

---

### Task 6: Format and "i" buttons on the control bar

**Files:**
- Modify:
  - `src/gui/icons.h`, `src/gui/icons.cpp`;
  - `src/gui/icons/lucide_icons.h`;
  - `tests/test_svg_colour.cpp`;
  - `src/gui/transport_bar.{h,cpp}`;
  - `src/gui/main_frame.cpp`, `src/gui/selftest.cpp`.

**Interfaces:**
- Consumes: Task 2 `infoHint`; Task 4 `Command::InfoWindow`, `infoShown`.
- Produces:
  - `Icon::SlidersHorizontal` and `Icon::Info`, appended after `Repeat`; `ICON_COUNT` follows
    `Info`.
  - On `TransportBar`:
    - `void setInfoShown(bool shown);` (the "i" pressed state);
    - `void setInfoHint(const std::string& hint);` (`""` = none);
    - `[[nodiscard]] wxAnyButton& buttonForTest(Command command);`
    - `groupRectForTest()` takes the group buttons by command (First, StepBack, PlayPause, Stop,
      StepForward, Last, Loop).

- [ ] **Step 1: Write the failing tests.**
  - `test_svg_colour.cpp`: add `lucide::SLIDERS_HORIZONTAL` and `lucide::INFO` to its list.
  - Selftest steps (smoke):
    - `"format button"`:
      - with a stream, `buttonForTest(Command::Format)` is enabled;
      - a click (a `wxEVT_BUTTON` sent to it) opens the Format dialog through
        `dialogHooks().format`, and `formatDialogsShown()` rises by 1;
      - after `CloseStreams` it is disabled.
    - `"info button"`:
      - `buttonForTest(Command::InfoWindow)` is always enabled;
      - a `wxEVT_TOGGLEBUTTON` shows the window, and its value is true;
      - a second one closes it, and the value is false;
      - View > Info sets the value too.
    - `"info button tooltip"`:
      - with the smoke stream, the tooltip is
        `tooltipText(info, CMD_FOR_CTRL) + "\n" + infoHint(stream)`;
      - with no stream, the first line alone.
    - `"the group is centred"` (existing): Open's x is unchanged, and the group stays centred.
      Use 400 DIP, or the bar's `GetEffectiveMinSize().x` when that is larger (the step logs the
      width used).

- [ ] **Step 2: Run them and watch them fail.**
  Run:
  - core tests `--test-case='*svg*'`;
  - smoke.
  Expected: build errors (`SLIDERS_HORIZONTAL`, `buttonForTest`).

- [ ] **Step 3: Add the icons.**
  - `sliders-horizontal.svg` and `info.svg` from `https://unpkg.com/lucide-static@1.70.0/icons/`,
    verbatim, in the header's existing form (licence comment line included).
  - `icons.cpp` maps the new enum values.

- [ ] **Step 4: Add the buttons to the bar** (§12).
  - **Format:** `addButton(*row, Command::Format, Icon::SlidersHorizontal)` right after Open.
  - **i:** a `wxBitmapToggleButton` with `Icon::Info` after the right stretch spacer, bound to
    `execute_(Command::InfoWindow)`, set up through `setUp`.
  - **`setUp` / `showStatus`:** `Command::InfoWindow` is exempt from the stream rule, like Open.
    `Command::Format` follows the stream rule.
  - **`syncSpacer`:** the right spacer's width = (Open's and Format's widths plus their borders)
    minus ("i"'s width plus its border), at least 0.
    - The two sides are then equal, so the group stays centred.
    - The icon and DPI rebuilds call it as before.
  - **Accessible names:** "Format" and "Info", via `setUp`'s existing label rule.
  - **`MainFrame`:**
    - `setInfoShown` and the Info window's own hide (close box, Esc) call
      `transportBar().setInfoShown(...)`;
    - `onStreamsChanged` calls `setInfoHint(streams_.empty() ? "" : infoHint(streams_.front()))`;
    - `setInfoHint` sets the tooltip to the command's tooltip, plus `"\n" + hint` when the hint is
      not empty.

- [ ] **Step 5: Run them and watch them pass.**
  Run:
  - core tests `--test-case='*svg*'`;
  - smoke;
  - player (it uses the transport bar).
  Expected: PASS.

- [ ] **Step 6: Commit.**
  `feat(gui): Format and Info buttons on the control bar`

---

### Task 7: README and the full verification

**Files:**
- Modify: `README.md`, at about lines 202, 222, 278, 360 and 699 (`grep -n "Info pane" README.md`).

- [ ] **Step 1: Update the README** (§16). State facts only:
  - **The Info window:** its 8 rows; it is opened by View > Info, Ctrl+I (Cmd+I) or the "i"
    button; it sits beside the main window.
  - **The window title and the status bar file field** with its full-path tooltip.
  - **The Format button.**
  - **Line 278:** "as in the Info pane" → the Format dialog.
  - **Line 360:** the Edit menu acts on the Info window (Copy).
  - **Line 699:** the read mode shows in `rawreel-cli info` and in the log.
  - `grep -n "Info pane" README.md` prints nothing.

- [ ] **Step 2: The full build and the suites, serially.**
  Run:
  `$B 2>&1 | tee build.log; grep -c "warning:" build.log` (expected: 0), then
  `DVV_REQUIRE_GPU=1 DVV_REQUIRE_FFMPEG=1 ctest --test-dir libs/_build/rawreel/ninja-macos -j1 --output-on-failure`.
  Expected: every test passes; the count is the earlier 30 plus the new `test_stream_name` cases,
  which run inside `rawreel.core_unit`.

- [ ] **Step 3: clang-format and clang-tidy** on the changed files. Expected: no diff, no
  findings.

- [ ] **Step 4: Commit.**
  `docs: README for the Info window, the window title, the file field and the new buttons`

- [ ] **Step 5: Manual checks for the user** (§8, §15). List them in the final message; they are
  not run by the agent.
  - The hover tooltips on each OS: the file field after the OS's delay, then a cut field with its
    own delay.
  - macOS:
    - the proxy icon and Cmd-click on the title;
    - the Info window over a full-screen main window;
    - the panel hides while rawreel is inactive.
  - The label colours in light and dark appearance, on each OS.
  - Windows: a long path stays on one line in the tooltip.
