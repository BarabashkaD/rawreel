# Window title, status bar file field and Info window — design

Status: draft, rev 3 (2026-11-10). Part 1: window title and status bar file field (§1–§8).
Part 2: the Info window that replaces the docked Info pane, and the Info and Format buttons on the
control bar (§9–§16). Rev 3 applies the optimistic and pessimistic reviews (2026-11-10).

## 1. Intent

rawreel's main window is always titled "rawreel" (`MainFrame` sets it once and never changes it).
The open file appears only on the Info pane's first line, as the full resolved URI. So the user
cannot see which file is open without the Info pane, and every rawreel window looks the same in the
Windows taskbar and Alt+Tab, the macOS Dock menu, Window menu and Mission Control, and the Linux
window switcher.

This design puts the file name where each OS expects a document's name, and the full path where
the user can reach it on every OS.

## 2. Decisions (user, 2026-11-10)

- The file name goes into the window title, in each OS's own form.
- The file name also goes into a new status bar field; hovering over it shows the full path as a
  native tooltip after the OS's usual hover delay.
- No tooltip on the title bar itself (see §6: no OS supports one natively; on Linux X11 an app
  cannot get mouse events over the title at all).

## 3. Goals, non-goals

Goals:
- The open stream's name in the window title on macOS, Windows and Linux, per OS convention.
- macOS: the native proxy icon and the Cmd-click / right-click path menu on the title.
- A status bar field with the name, whose native tooltip shows the full path, on all three OSes.

Non-goals:
- A tooltip on the title bar, and the full path in the title.
- A title for more than one stream (`MAX_STREAMS` is 1); the rule for several streams is decided
  when a second stream slot exists.
- An "edited" mark in the title (rawreel does not edit the file).
- Actions on the status bar field (click, context menu, Show in Finder/Explorer); possible later.

## 4. The shown name and the full path

Wx-free functions in `src/core` (unit tested), from a stream's `resolvedUri`:

- **Shown name.** A `file:` URI: the file's base name, without the URI's query (`file:clip.yuv?
  fmt=v210&size=...` → `clip.yuv`). Any other URI with a path: the scheme and the path
  (`lavfi:testsrc2=size=1280x720` → `lavfi:testsrc2=size=1280x720`). UTF-8 names stay as they are.
  This one function replaces the name logic of `formatDialogTitle` (format_dialog.cpp) and
  `MainFrame`'s `fileName()` helper (the info bar warning).
- **Full path.** A `file:` URI: the absolute path; a relative path is made absolute against the
  process's working directory, which rawreel never changes (as `uriFolder` in open_text.cpp does).
  The core function takes the base folder and the path style (POSIX or Windows) as arguments, so a
  unit test on any OS covers both styles; the GUI passes the current working directory and the
  native style (backslashes on Windows). Any other URI: the resolved URI as it is (with its query),
  since it has no path.

No stream: no name and no path.

## 5. Window title (per OS)

| OS | Title with a stream | Extra | No stream |
|---|---|---|---|
| macOS | `clip.yuv` (the name only; macOS shows no app name for document windows) | File streams: `SetRepresentedFilename(full path)`, which gives the proxy icon and the Cmd-click / right-click path menu. Non-file streams: an empty string (clears it). | `rawreel`, represented filename cleared |
| Windows | `clip.yuv - rawreel` (document name, hyphen, program name) | — | `rawreel` |
| Linux | `clip.yuv — rawreel` (em dash, the KDE form; GNOME apps often show the name alone, but wx uses the classic title bar, where the app name helps) | — | `rawreel` |

The title changes wherever the Info text changes today (`MainFrame::onStreamsChanged`, next to the
mapping field): open, reopen, a Format change, close. The OS shortens a long title itself; rawreel
does not cut it. `SetRepresentedFilename` is a no-op in wx's base class and implemented on wxOSX,
so the code needs no `#if` for it; it is set for every file stream without an existence check (the
open has just read the file). The title separator is chosen per OS with `#if`.

## 6. Review against OS practice (why not a title-bar tooltip)

- **macOS.** AppKit draws the title bar in private views; there is no API for a title tooltip, and
  Mac apps do not have one. The native way to the full path is the proxy icon (draggable into Finder
  or Terminal) and Cmd-click / right-click on the title, which lists the folders of the path. Apple's
  document windows show the document's name alone.
- **Windows.** The title bar is the window's non-client area, drawn by Windows. A tooltip there
  needs a non-client mouse hook and a hand-made tooltip; standard apps do not do it. Windows'
  guidelines put the document name first, then a hyphen, then the program name. Some apps put the
  full path in the title (Notepad++, Explorer's option), which the OS then cuts in the taskbar.
- **Linux.** On X11 (rawreel runs on X11 or XWayland) the window manager draws the title; the app
  gets no mouse events there. GNOME apps that draw their own header bar show the folder as a
  subtitle; wx uses the classic title bar.
- **Status bar tooltips** are ordinary native tooltips on all three (`NSView` tool tip, the Win32
  tooltip control, `GtkTooltip`) with the OS's own hover delay and look. Windows apps commonly have
  tooltips on status bar fields. Mac and GNOME apps rarely have status bars, but rawreel already has
  one on every OS, and Finder's path bar (View > Show Path Bar) shows a location at the bottom of a
  window in the same way.

## 7. Status bar file field

- **Fields.** A new field 0, before the existing five; every existing index moves up by one:
  position 1, playback rate 2, bottleneck 3, display mapping 4, Log notice 5. All of them become
  named constants (`FILE_FIELD`, `FIRST_TICK_FIELD` = 1 for the status tick's three fields, which
  `MainFrame`'s status update writes as `FIRST_TICK_FIELD + i`, `MAPPING_FIELD`,
  `LOG_NOTICE_FIELD`); `status_texts_` grows from 5 to 6; the widths array becomes
  `{-3 file, -3 position, -2, -2, mapping, notice}`; the Log notice click handler and the selftest's
  literal indices use the constants.
- **Text.** The shown name (§4); empty with no stream. The bar keeps its end-cut style for the
  existing fields; field 0 is cut in the middle by rawreel itself (`wxControl::Ellipsize(...,
  wxELLIPSIZE_MIDDLE)` against the field's width, again on every size change), so the extension
  stays visible. "Field 0 is cut" means its shown text differs from the full name.
- **Minimum width.** Without the Info dock the main window's minimum client width falls to the
  centre minimum (about 360–400 DIP with the two new buttons), less than the status bar's two fixed
  fields (display mapping and Log notice, about 500 DIP). The main window's minimum client width
  becomes at least the fixed fields' width plus 120 DIP for the file field (in `MainFrame`'s
  minimum, next to the centre minimum), so the file field never shrinks to nothing; measured:
  852 DIP on macOS at 100 % (fixed fields 256 + 196 DIP, plus 400 DIP for the four variable
  fields, of which the file field's 3 of 10 shares are the 120 DIP).
- **Tooltip.** wx's built-in `wxSTB_SHOW_TIPS` cannot do this: it shows a field's own text, only
  when that text is cut, and only on Windows and GTK. So the bar is created with
  `wxSTB_DEFAULT_STYLE & ~wxSTB_SHOW_TIPS` (keeping the size grip, the end-cut style and the full
  repaint; wx asserts on a hand-set tooltip only with `wxSTB_SHOW_TIPS`). One helper,
  `statusFieldAt(point)`, finds the field whose `GetFieldRect()` contains the point (as the Log
  notice click handler does; `GetFieldFromPoint` exists only in wx's generic bar, not in the
  native Windows one), and `statusTooltipFor(field)` gives the text: over field 0 the full path
  (§4); over another field its full text when that field is cut (`GetField(n).IsEllipsized()`);
  otherwise none. A mouse-motion handler on the bar applies it: when the field under the pointer
  changes, the tooltip is unset (`UnsetToolTip()`) and then set, so each field gets the OS's own
  hover delay; it is also unset on `wxEVT_LEAVE_WINDOW`. No timer.
- **Windows tooltip width.** wxMSW caps every tooltip at min(400 px, half the display) and wraps at
  spaces; rawreel sets `wxToolTip::SetMaxWidth` to about 800 DIP so a path stays on one line where
  it fits.
- The field updates with the title (§5).

## 8. Tests (Part 1)

- Core unit tests for the shown name and the full path: a `file:` URI with a query, a relative path
  with a given base folder, both path styles (a Windows drive letter and backslashes, a POSIX path)
  on any OS, UTF-8 names, a `lavfi:` URI, an empty or unparsable URI.
- Selftest steps (smoke): after a stream opens, the frame title equals the expected per-OS text and
  the file field shows the name; `statusTooltipFor(statusFieldAt(p))` for a point inside the file
  field is the full path (the step uses the same helpers as the motion handler, not the OS tooltip
  window); after close, the title is "rawreel" and the file field is empty. macOS: the window's
  represented filename is the full path (read back through a native query in
  `native_controls_cocoa.mm`). The steps read titles and fields in a later event-loop turn.
- The existing steps that read status bar fields by index use the named constants.
- Manual check per OS: hovering the file field shows the full path after the OS's delay; moving to
  a cut field shows that field's text with its own delay.

---

# Part 2 — Info window and control bar buttons

## 9. Intent and decisions (user, 2026-11-10)

Today the Info pane is a wxAUI pane docked on the right of the main window with a read-only text
(`formatInfo`): the resolved URI, format and size, frame count, colour standard with its origin,
matrix, range, curve, primaries, size hint, tone mapping, clip description, read mode and
warnings. The user wants only what matters while looking at the picture, in a window of its own:

- **Content:** resolution, frame rate, pixel format, colour standard, matrix, range, transfer
  curve, tone mapping. Nothing else.
- **Form:** a separate (detached) window, not editable, in two columns (label, value) and rows.
  Values left-aligned on every OS; labels aligned per OS (§11).
- **Opening:** a new "i" toggle button at the right end of the control bar. The window opens to the
  right of the main window; it closes with its own close button or a second click on "i".
- **"i" hover hint:** the action with its shortcut, then a one-line summary (§13).
- **Format button:** a new button right of Open that runs View > Format... (the "Format of the
  stream" dialog for the clip's pixel format, size, frame rate and colour).
- **Status bar:** no Info values (§14).

## 10. The Info window

- **Kind.** A modeless tool window owned by the main window: macOS a floating inspector panel
  (like Preview's Tools > Show Inspector), Windows and Linux a tool window with no taskbar entry of
  its own. In wx a `wxFrame` with `wxCAPTION | wxCLOSE_BOX | wxFRAME_TOOL_WINDOW |
  wxFRAME_FLOAT_ON_PARENT` (no resize, minimise or maximise). Title: "Info". On macOS
  `EnableFullScreenView(false)` (FullScreenAuxiliary), so the panel can show over a full-screen
  main window; a no-op elsewhere. On Windows a tool window is not in Alt+Tab; Ctrl+I is the
  keyboard path to it.
- **Command.** `Command::InfoWindow`: `{InfoWindow, "info_window", "View", "Info", "Show or hide the
  Info window", ctrlKey('I'), Accelerator, checkable}`, placed right after `LogPane` (View menu:
  Format..., Restart GPU and reopen, Log, Info). On macOS the item reads "Show Info" / "Hide Info"
  (an `infoItemTitle(shown)` like `logItemTitle`). Always available. The "i" button, the menu item
  and the shortcut toggle the same window; the button's pressed state follows the window's shown
  state, not the session status.
- **Showing.** Opened with `ShowWithoutActivating()` on every port (on GTK a plain `Show()` takes
  the focus), so the main window keeps the keyboard focus and the player's keys keep working.
- **Keys while the Info window is active** (after the user clicks or drags it). Its content is a
  panel that accepts the focus (no visible focus ring), so the focus is inside the window on every
  port (on macOS a window without a focusable view gives no key events). A `wxEVT_CHAR_HOOK` on the
  Info window handles Esc (close), Ctrl/Cmd+I (toggle, i.e. close) and Ctrl+C on Windows and Linux
  (copy, below); every other key goes to the main window's key dispatch (`commandForKey`), so Space,
  the arrows, L and the other player keys act as in the main window, as a native inspector passes
  keys to its document.
- **Cmd+W (macOS).** The Log/Settings design's ⌘W rule (only the Settings window takes ⌘W) extends
  to the Info window: an `infoInFront()` like `settingsInFront()` (the focus inside the Info window,
  or the window shown and `IsActive()`, since `FindFocus()` can be null there on wxOSX).
  `CloseStreams` is available when `hasStream() || settingsInFront() || infoInFront()`, and
  `execute(CloseStreams)` closes the Info window first when it is in front. The selftest
  `closeKeyOtherWindowStep` keeps its rule for other windows (its stand-in comment no longer names
  "a floating Info pane").
- **Copy.** Edit > Copy copies all rows as "Label: value" lines when `infoInFront()`: on macOS
  through the Edit menu (`onEditUpdateUi` enables Copy for it, `onEditMenu` runs it); on Windows and
  Linux through Ctrl+C in the Info window's char hook (the main window's Edit menu activates the main
  window first). The text goes through the same clipboard path as the other copies (the selftest's
  clipboard hook in a selftest). The values are static text, not selectable fields.
- **Place.** On open, its top edge level with the main window's top and its left edge right of the
  main window's right edge (a small gap). When that does not fit on the main window's display
  (`wxDisplay` of the main window: maximised, full screen or near the right edge), it is placed
  inside the main window's right edge, still floating, and not over a modal dialog's area (the
  Format dialog is centred; the Info window then goes to the display's right edge). When it hides,
  its position is remembered for this run; it is used again only when `wxDisplay::GetFromPoint`
  finds a display there, otherwise the place is computed again. Not saved to rawreel.json.
- **Life.** It stays open across stream changes and shows "No stream" with no stream. It closes
  with the main window. While the main window is minimised it is hidden, and it shows again
  (without activating) on restore (`MainFrame::onIconize`). On macOS the panel also hides while
  rawreel is inactive and returns when it is active (native inspector behaviour; `IsShown()` stays
  true).
- **The docked Info pane is removed:** its AUI pane, `InfoPanel`, `INFO_MIN_WIDTH_DIP` and the
  dock-related minimum (the window minimum becomes §7's), and `formatInfo` (replaced by §11's value
  functions). No AUI perspective is saved anywhere, so no stored state changes.

## 11. Content and visual design

```
Picture
  Resolution     1920 × 1080
  Frame rate     50 fps
  Format         YUV 4:2:2 10-bit (v210)
Colour
  Standard       BT.709
  Matrix         BT.709
  Range          Limited (64–940)
  Transfer       BT.1886
  Tone mapping   None (SDR)
```

- **Rows.** Two groups with a small bold heading each: Picture (Resolution, Frame rate, Format) and
  Colour (Standard, Matrix, Range, Transfer, Tone mapping). A `wxFlexGridSizer` of two columns per
  group; the label column as wide as the widest label of both groups, so the values line up.
- **Alignment.** Values left-aligned on every OS. Labels: right-aligned on macOS (as Finder's Get
  Info and System Settings), left-aligned on Windows and Linux.
- **Colours and font.** Values in the normal text colour, the system font. Labels in each OS's
  secondary text colour: macOS `NSColor.secondaryLabelColor` (a `native_controls` query, as
  `settingsPageIcon`; `wxSYS_COLOUR_GRAYTEXT` is the disabled colour there), GTK the `dim-label`
  style class, Windows the normal window text (as File Properties; builder's call between that and
  `wxSYS_COLOUR_GRAYTEXT`). Headings in the system font, bold. Colours are applied again on
  `wxEVT_SYS_COLOUR_CHANGED`, as `TransportBar::onSysColourChanged` does.
- **Spacing.** In DIP from the system font: the OS's standard dialog margins
  (`wxSizerFlags::Border()`), a column gap of about 12 DIP, a group gap of one line.
- **Size.** Fixed: the value column's minimum width is the widest of fixed sample strings
  (`BT.2390, 1000 → 100 nits`, `YUV 4:2:2 10-bit (yuv422p10le)`, `Custom (Kr 0.2990, Kb 0.1140)`),
  so the window never changes size when the stream changes and no value is cut. "No stream" is a
  page of a `wxSimplebook` beside the grid, centred in the secondary text colour, so both pages
  share one size. The window fits again on `wxEVT_DPI_CHANGED` and on a system font change, keeping
  its top-left corner.
- **Accessibility.** Each value's accessible name is "Label, value" (`setAccessibleName`), so
  VoiceOver, Narrator and Orca read each row as one item. The "i" toggle exposes its pressed state
  natively.
- **Values** (wx-free functions in `src/core`, unit tested; they replace `formatInfo`):

| Row | Value | Examples |
|---|---|---|
| Resolution | width × height (U+00D7) | `1920 × 1080` |
| Frame rate | the stream's rate; whole when `num % den == 0`, else up to 3 decimals; unbounded streams show their rate too | `50 fps` (also 100/2), `29.97 fps`, `23.976 fps` |
| Format | from the pixel format descriptor: the model (YUV, RGB, Gray), the chroma for YUV 4:4:4, 4:2:2 and 4:2:0, "N-bit" when all colour components have the same depth, "+ alpha" when there is an alpha component, then the rawreel name in brackets; any other subsampling or mixed depths: the rawreel name alone | `YUV 4:2:0 8-bit (yuv420p)`, `YUV 4:2:2 10-bit (v210)`, `RGB 8-bit + alpha (rgba)`, `Gray 16-bit (gray16le)`, `yuv411p`, `rgb565le` |
| Standard | the effective standard, with today's precedence (the URI's `colour=` preset, else `streamColourBase`); "None" when no standard matches | `BT.709`, `BT.2100 PQ`, `None` |
| Matrix | the matrix; "Not used" for RGB and gray; "Custom (Kr …, Kb …)" with 4 decimals; "Unspecified" | `BT.709`, `BT.601`, `BT.2020`, `Not used`, `Custom (Kr 0.2990, Kb 0.1140)`, `Unspecified` |
| Range | limited or full with its code span at the stream's bit depth (the luma depth for YUV); "Unspecified" | `Limited (16–235)`, `Limited (64–940)`, `Full (0–1023)`, `Unspecified` |
| Transfer | the transfer curve; "Unspecified" | `BT.1886`, `sRGB`, `PQ`, `HLG`, `Linear`, `Unspecified` |
| Tone mapping | "None (SDR)" for SDR curves; the mapping for PQ and HLG (today's text) | `None (SDR)`, `BT.2390, 1000 → 100 nits` |

What the Info pane showed and the window does not: the resolved URI (the title and the status bar
file field carry the name and path, Part 1), frame count, primaries, the origin tags ("from the
file name", "default") and the size hint (both remain in the Format dialog), the clip description,
the read mode (remains in `rawreel-cli info` and the log) and the provider warnings (logged; open
errors still show the info bar).

## 12. Control bar buttons

```
[Open][Format]        |<  <|  ▶  ■  |>  >|  ↻        [i]
```

- **Format** (new, right of Open): Lucide `sliders-horizontal` (not a gear, which would read as the
  Settings window), runs `Command::Format` (`CommandSource::Button`); enabled only with a stream,
  like the menu item and the other transport buttons.
- **i** (new, at the right end of the bar): Lucide `info`, a toggle button like Loop
  (`wxBitmapToggleButton`), always enabled.
- **Setup.** Both go through `TransportBar::setUp`: no focus, `SetName`, the accessible names
  "Format" and "Info", and the tooltip from the command table like every other button
  (`tooltipText`: the command's help and its key, e.g. "Choose the pixel format, size, frame rate
  and colour of the stream"). `setUp` and `showStatus` exempt `Command::InfoWindow` from the
  stream rule, as they exempt Open.
- **Layout.** Today Open sits at the left and a spacer as wide as Open at the right keeps the
  transport group centred. Now the left holds Open and Format, the right holds a spacer and "i";
  the right side's width equals the left side's (`syncSpacer` with the new rule), so the group
  stays centred. `groupRectForTest()` takes the centred group's buttons by command, not by index.
- **Icons.** `Icon::SlidersHorizontal` and `Icon::Info` are added to the Lucide set
  (`src/gui/icons`, `ICON_COUNT` updated) with their licence, like the existing ones.

## 13. The "i" button's hover hint

A native tooltip of two lines: the command's tooltip like every other button, then a summary:

```
Show or hide the Info window (Cmd+I)       (Ctrl+I on Windows and Linux)
1920 × 1080 · 50 fps · v210 · BT.709 limited
```

The second line is the resolution, frame rate, rawreel format name, standard and range, joined with
" · " (from the same core functions as §11); with no stream it is left out. The tooltip changes
when the stream changes. A tooltip cannot align columns, so it does not repeat the window.

## 14. Status bar (review)

No Info values go into the status bar. It holds the new file field (Part 1), position, playback
rate, bottleneck, display mapping and the Log notice; at the window's minimum width (§7) the
variable fields already share what the two fixed fields leave, and seven more values would be cut
at once. The display mapping field already shows the tone mapping, and the status bar's rate is the
measured playback rate, which a second (stream) rate beside it would confuse. The "i" hint (§13)
and the Info window carry the values.

## 15. Tests (Part 2)

- Core unit tests for every value function of §11 and the hint line of §13: each pixel format
  family and the "name alone" cases, alpha, whole and fractional rates (including 100/2), every
  matrix (Custom, Unspecified), range spans at 8, 10 and 16 bits, every curve, PQ and HLG tone
  mapping, RGB and gray ("Not used"), the standard's precedence, no stream. They replace the
  `formatInfo` tests in tests/test_info_text.cpp.
- Selftest steps (smoke), each checking its effect in a later event-loop turn (no timer):
  - "i" opens the window; it is shown (`IsShown()`, not on-screen visibility), the button is pressed,
    and it is placed right of the main window, or inside its right edge when there is no room. On
    Linux the step checks the rectangle rawreel requested (`InfoWindow::requestedRectForTest()`),
    since the window manager may adjust a utility window; on macOS and Windows `GetScreenRect()`
    against that request.
  - The main window keeps the keyboard focus after the window opens (`FindFocus()` not inside the
    Info window, the main frame active when it was active before).
  - A second click on "i" closes it and releases the button; the close box, Esc, Ctrl/Cmd+I and
    View > Info close or toggle it (key events sent to the Info window, not only `setFocusForTest`).
  - With the Info window as the focus, Space reaches the player (playback starts).
  - macOS: ⌘W with the Info window in front closes the Info window, with and without a stream;
    `closeKeyOtherWindowStep` still holds for other windows.
  - Copy: an "info window copy" step replaces `infoCopyStep` (rows as "Label: value" lines through
    the clipboard hook).
  - The window's rows (`InfoWindow::rowsForTest()`) match the open stream and show "No stream"
    after close; minimising the main window hides it and restoring shows it.
  - The Format button opens the Format dialog (through the existing dialog hook) and is disabled
    without a stream; the "i" tooltip text.
- Existing steps that read the Info pane are rewritten: the colour checks (`infoHas`, the
  D5R §4.5 origin and value checks, the "range limited" check after Format) read `frame.streams()`
  (`StreamInfo.colour`) or the window's rows; `panesStep` no longer expects an Info pane;
  `movedPaneStep` (the DockManager's two-dock minimum) is rewritten with the Log pane, or, if only
  one dock can remain, the two-dock code paths are removed together with the test.
- Manual checks: macOS full screen with the Info window; the hover tooltips on each OS; the label
  colours in light and dark appearance on each OS.

## 16. Docs

README: the lines that describe the Info pane (about lines 200, 219, 275, 357 and 696 today) are
updated: the Info window and its rows, the window title and the status bar file field, the Format
and "i" buttons, and that the read mode is shown by `rawreel-cli info` and in the log. Facts only.
