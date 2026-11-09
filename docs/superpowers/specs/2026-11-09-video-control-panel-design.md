# Video control panel — design

Status: revision 5 (§13): no drag cancel on any platform; skimming with a held key (§6.4), in which
the real thumb stays and a ghost thumb shows the future position; a click on the slider jumps to
the clicked frame on every platform (§6.5). Rewrites the "Native Video
Playback Control Panel" brief against the rawreel code as it is after the state-notification work
(`2026-11-08-session-state-notifications-design.md`, PR #9). The brief's general rules stay; where
it said "do not assume the playback architecture", this document names the architecture, because
it now exists and the panel must fit it.

## 1. Goal

Replace today's one-row transport bar (`src/gui/transport_bar.*`: text buttons `|<  <  Play  >  >|
Loop`, a slider, a "frame N / M" label on the right) with a compact two-row control panel:

- a timeline row: the native slider, adaptive tick marks, the first and last frame numbers, the
  current frame number above the thumb, and a temporary marker at the original frame while the
  user drags or skims;
- skimming: while paused, holding ⌥ Option (macOS) or Ctrl (Windows, Linux) and moving the pointer
  over the slider previews the frame under it; a click moves there, releasing the key returns;
- a transport row: Open on the left, the playback buttons centred on the full panel width, all
  icon-only (Lucide SVG).

C++23 (`cxx_std_23`, dvv-build) and wxWidgets 3.3.4. Platforms: Windows, Linux (GTK 3), macOS.

## 2. Decisions

| Id | Decision | By |
|---|---|---|
| U-CP1 | *Superseded by U-CP7 (revision 4).* Was: a drag cancelled by Escape or deactivation seeks back. Now a drag has **no cancel** on any platform, and Escape is unbound again. | user |
| U-CP2 | The number above the thumb shows the **frame on screen** (`PlayerStatus::position`) while Idle, and the thumb's value while Scrubbing or Skimming. The right-hand "frame N / M" label is removed; the last frame number at the slider's end replaces its "/ M". | user |
| U-CP3 | The panel's minimum width is **400 DIP**. | user |
| U-CP4 | *Superseded by U-CP7.* Was: no cancel where a platform cannot deliver it in time. Measured on macOS (§14.2): Escape is not delivered during a native drag. | user |
| U-CP5 | `SessionStatus` gains **`streamGeneration`**, a counter bumped on every stream-set change. It is session state, not an acknowledgement of a GUI request, so U-EV6 holds. | user |
| U-CP6 | *Superseded.* Was: a cancel leaves playback paused. Skimming only runs while paused (U-CP7), so returning from a skim leaves playback paused too. | user |
| U-CP7 | **Skimming** (§6.4), the only way to look at frames and return. Paused only. Held key: **⌥ Option on macOS, Ctrl on Windows and Linux** (the native "alternate mode" key of each platform; no toggle key). Only the **slider** reacts to it. While held and the pointer moves over the slider, the **real thumb and the filled bar stay** at the current frame, a **ghost thumb** (the knob drawn grey at 50 % opacity, on the slider) follows the pointer to show the future position, the **picture previews** that frame and the frame number follows the ghost (revision 5, validated on macOS). A **click moves** there for real (the real thumb jumps to the ghost); **releasing the key**, moving off the slider, or the window losing activation **returns** to the frame where skimming started. Nothing else changes: keys keep their native meaning (Space plays; Ctrl+Space or ⌥+Space is undefined and does nothing). To play from a skimmed frame: click, release the key, Space. | user |
| U-CP8 | A **click on the slider jumps** to the clicked frame on every platform (§6.5). macOS and GTK do so natively; on Windows the bar handles a click on the channel itself instead of the trackbar's page step. | user |
| U-CP9 | **No double-click** action on the slider. Playback starts only with Play or Space. | user |
| U-EV6 | Unchanged from the notification design: no acknowledgement, echo, counter or timestamp of GUI requests. A skim's return is an ordinary `seek()`. | user (earlier) |
| CP-D1 | The panel is the existing `TransportBar`, extended; no second class owns the same state. A child window `TimelineStrip` draws ticks, numbers and the marker. | Claude's call |
| CP-D2 | All geometry math (tick intervals, label placement, `thumbX`) lives in a wx-free unit in `src/core/` and is unit-tested, like `core/layout.h` with `tests/test_layout.cpp`. The wx code only measures, queries the native track and paints. | Claude's call |
| CP-D3 | Ticks are drawn by `TimelineStrip` on every platform; native slider ticks are not used (§4). | Claude's call |
| CP-D4 | Every button except Open is disabled while there is no stream; Last also while the length is unknown (as the menu). The slider is disabled with no stream or an unbounded length (today's rule). | Claude's call |

## 3. Architecture it builds on (fixed, not to be redesigned)

- **Commands.** Buttons call `execute(Command)`. The commands exist in `src/core/commands.cpp`:
  `Open`, `First`, `StepBack`, `PlayPause`, `Stop` ("Stop playback and go to the first frame"),
  `StepForward`, `Last`, `Loop`.
- **Keys.** Bare keys go through `MainFrame::onCharHook` (`main_frame.cpp:657`). Only keys bound
  to a command are consumed; every other key (Up, Down, PageUp, PageDown, Tab, Escape today) is
  passed on with `Skip()` and reaches the focused window, which can be the slider (a click
  focuses it on Windows and GTK). Buttons never take focus. Escape is unbound (`commands.h`
  reserves it for a later full-screen key); the panel does not use it (revision 4).
- **The skim key** (U-CP7) is read from the event's own modifier state, never by polling:
  `wxMouseEvent`/`wxKeyEvent::AltDown()` on macOS (⌥ Option; there `ControlDown()` is ⌘ Command
  and `RawControlDown()` is the physical Control key), `ControlDown()` on Windows and Linux. A skim
  starts on a slider motion that carries the modifier; its release is the key's `wxEVT_KEY_UP`
  (`WXK_ALT` on macOS, `WXK_CONTROL` elsewhere), which goes only to the focused window, so a small
  `wxEventFilter` forwards it to the bar.
- **State in.** `MainFrame::onStatusChanged` calls `TransportBar::update(const PlayerStatus&)`
  once per new status revision (no timers, no polling). `status.target` is where the thumb goes in
  Idle (read-your-writes); `status.position` is the frame on screen. With U-CP5 the signature
  becomes `update(const PlayerStatus&, std::uint64_t streamGeneration)`.
- **State read on an event.** The bar also gets a callback that reads the session's current
  status (what `onStatusChanged` would read now; a default status with no stream). It is called
  only when the bar leaves Scrubbing or Skimming (§6.1), never on a timer. Because
  `Session::seek` patches `target` before it returns, that read already names the bar's own last
  seek, while the notification for it may still be queued. A read of state is not an
  acknowledgement (U-EV6).
- **Seeks out.** `seek(std::int64_t)` (`Session::seek`), live during a drag, never the same value
  twice in one interaction. `acceptSeek` patches `target` under the lock and the command FIFO keeps
  queued drag seeks before a later seek, so a seek back is reflected in `target` at once.
- **Scrub states** (PR #9 §6.6): `Idle`, `Scrubbing`. Revision 4 adds `Skimming` (§6) and drops
  revision 3's `Cancelled`.

## 4. What the wx 3.3.4 sources say (checked, then re-checked by both reviews)

| Topic | Windows | GTK 3 | macOS |
|---|---|---|---|
| Native ticks | `SetTickFreq`, `SetTick` | `SetTick` per mark (`gtk_scale_add_mark`); `SetTickFreq` loops over it | only `wxSL_AUTOTICKS` at creation: ≤ 20 evenly spaced marks, not updated by `SetRange`; `SetTickFreq`/`SetTick` are empty |
| Major vs minor tick length | no | no | no |
| Thumb width | `TBM_GETTHUMBRECT` (`GetThumbLength()` is the thumb's **height** on a horizontal trackbar, `msw/slider.cpp:796`) | `gtk_range_get_slider_range()` on `wxSlider::m_scale` (public member) | the cell's `knobRectFlipped:` |
| Track start/end | `TBM_GETCHANNELRECT` | `gtk_range_get_range_rect()` | the cell's `barRectFlipped:` |
| End of a mouse drag | `THUMBRELEASE`, then `CHANGED` (`msw/slider.cpp:293-312`) | `THUMBRELEASE`, then `CHANGED` in the same call (`gtk/slider.cpp:41-58`, `:231`) | `THUMBRELEASE` after the native tracking loop returns (`slider_osx.cpp:351`); no `CHANGED` |
| Key or wheel on a focused slider | `CHANGED` (and line/page events) | line/page events, then `CHANGED` | `THUMBTRACK` with no release (`cocoa/slider.mm:58-63`) |
| SVG | `wxUSE_NANOSVG 1`: `wxBitmapBundle::FromSVG` works and copies its input | same | same |
| `currentColor` in SVG | NanoSVG parses it as grey (128,128,128) (`nanosvg.h:1480-1503`): substitute before `FromSVG` | same | same |
| Accessible names | `SetName` only stores `m_windowName` on every port (`window.h:220`); `wxAccessible` needs `wxUSE_ACCESSIBILITY 1` (check the Windows build) | native ATK call needed | native call needed; `wxUSE_ACCESSIBILITY 0` |

Consequences: ticks and the label above the thumb are drawn by us on every platform; the thumb's x
comes from the native track and thumb rects (§5.3); the trailing `CHANGED` of the Windows/GTK
release pair must not seek again (§6.2); keyboard and wheel input must not look like a drag.

| Topic (revision 4) | Windows | GTK 3 | macOS |
|---|---|---|---|
| Click on the track | the trackbar pages (`SB_PAGEUP/PAGEDOWN`, then `CHANGED`) | warps to the click point (`gtk-primary-button-warps-slider`, default on); Shift+click pages (`gtkrange.c`) | jumps to the click point (NSSlider) |
| Pointer moves over the slider with no button down | `wxEVT_MOTION`, `ENTER`/`LEAVE` (a native window gets them) | to confirm (the spike, §10) | to confirm: wxOSX tracking areas on the NSSlider (the spike, §10) |

PR #9 §6.6 says "a GTK track click sends `THUMBTRACK` then `THUMBRELEASE`"; it is `THUMBTRACK`,
`THUMBRELEASE`, `CHANGED`. The PR #9 code is not affected (the trailing `CHANGED` carries the last
sent value and does not seek); that sentence is corrected with this document.

## 5. Layout

### 5.1 Rows

```text
┌────────────────────────────────────────────────────────────────────────┐
│ 0                    1234                                        4567   │  TimelineStrip: numbers
│ ┃    ╷    ╷    ╷    ╷    ┃    ╷    ╷    ╷    ╷    ┃    ╷    ╷    ╷    ┃   │  TimelineStrip: ticks
│ ═══════════════════════════(●)══════════════════════════════════════   │  native wxSlider
│ [📂]            [⏮] [⏴|] [▶] [■] [|⏵] [⏭] [🔁]                  [   ]   │  transport row
└────────────────────────────────────────────────────────────────────────┘
```

- Vertical sizer: `TimelineStrip` (expand), `wxSlider` (expand), transport row. No borders,
  captions or padding beyond the platform's minimum; no row wrapping.
- Transport row: `[Open] stretch [group] stretch [spacer]`. The spacer has Open's width, so the
  group is centred on the whole panel width at every size. Its width is re-set from Open's best
  size on `wxEVT_DPI_CHANGED` and `wxEVT_SYS_COLOUR_CHANGED`.
- Every button gets the same minimum size, `FromDIP(wxSize(29, 28))`, or the native minimum if
  larger. Icons 18 × 18 DIP.
- **Minimum size (U-CP3):** the 400 DIP is `TimelineStrip`'s minimum width
  (`SetMinSize(FromDIP(wxSize(400, h)))`), never `SetMinSize` on the bar window: for a window with
  a sizer, wx computes the best size from the sizer (`wincmn.cpp:709-711`), and
  `PlayerPanel::minimumSize()` reads the bar's `GetBestSize()`. The bar's best width is then
  max(400 DIP, its content). Today's bar reserves "frame 9999999 / 9999999", so its best width may
  already exceed 400 DIP and the frame's minimum width may shrink; the minimum **height** grows by
  the strip and the second row. Stage 1 records the old and new best sizes. The existing selftests
  "shrink stops at the minimum" (`selftest.cpp:442-475`) cover the mechanism.

### 5.2 Timeline strip

- **Ticks** sit at multiples of the minor interval from frame 0, over the slider's range; no extra
  tick at the last frame (its end number marks it). The minor interval is the smallest
  `1, 2, 5 × 10^n` value whose spacing is at least `MIN_MINOR_TICK_DIP` (initial value 4, tuned in
  Stage 1). Major ticks are at multiples of the next power of ten above the minor interval (minor
  1, 2 or 5 → major 10; minor 10, 20 or 50 → major 100) and are visibly longer.
- **Font:** the UI font at its normal size; no shrinking. On macOS the system monospaced-digit
  font may be used (through `wxFont(NSFont*)` in an `.mm` file). The strip's height is fixed from
  that font and recomputed only on DPI or font changes, so it never changes during a resize.
  Overlap tests use the measured text width.
- **Labels:** first (frame 0) at the track's start, last at its end, current over the thumb,
  original over the marker while Scrubbing or Skimming.
- **Placement algorithm** (wx-free, CP-D2; this order is normative):
  1. Place the current label centred on its x, clamped inside the strip.
  2. While Scrubbing, place the original label on the side of the marker away from the thumb if
     it fits inside the strip without overlapping the current label; otherwise on the other side if
     that fits; otherwise hide its number (the marker line is always drawn). The side flips only
     after the thumb has crossed the marker by more than `LABEL_FLIP_HYSTERESIS_DIP` (initial 8),
     so it does not flicker while the thumb hovers over the marker.
  3. Place the first and last labels at their ends, clamped. Hide either one if it overlaps the
     current or the original label.
  Priority: current > original > first/last.
- **Colours:** background = the parent's background colour (`wxBG_STYLE_PAINT` means the strip
  clears it itself); text and major ticks `wxSYS_COLOUR_BTNTEXT`; minor ticks the same colour
  blended halfway to the background; the marker `wxSYS_COLOUR_HIGHLIGHT`, 2 DIP wide, spanning the
  number and tick rows so it never reads as a major tick. All re-read on
  `wxEVT_SYS_COLOUR_CHANGED`.
- **Degenerate cases:**
  - No stream or unbounded length: the strip is empty and the slider disabled (today's rule).
  - `length == 1`: the slider keeps today's range 0..1; the strip shows "0" at the start, no
    ticks and no last label.
  - `position == -1`: no current label.
  - `length - 1 > INT_MAX`: the slider is clamped (today's rule); the last label and the ticks
    follow the slider's maximum, not `length - 1`.

### 5.3 Thumb position

`thumbX(value) = trackStart + (value - min) / (max - min) × (trackEnd - trackStart)`, where `min`
and `max` are the slider's `GetMin()`/`GetMax()` (so `length == 1` never divides by zero), and
`trackStart`/`trackEnd` are the thumb centre's travel limits in strip coordinates.

Native queries come first, in the platform source files (`viewport_cocoa.mm` style for macOS,
`gui_platform_gtk.cpp`, an MSW file): Windows `TBM_GETCHANNELRECT` + `TBM_GETTHUMBRECT`; GTK
`gtk_range_get_range_rect()` + `gtk_range_get_slider_range()`; macOS the cell's `barRectFlipped:`
+ `knobRectFlipped:`. The geometry unit takes `trackStart`/`trackEnd` as inputs. Measured constants
are a fallback only where a query is missing or wrong (Stage 1 decides), expressed in DIP through
`FromDIP` (a no-op on GTK and macOS, which use logical pixels). If Stage 1 shows even the native
query is more than about 2 px off, accept the offset and keep the native slider (the brief's rule).

## 6. Scrubbing and skimming

### 6.1 State the bar keeps

- `scrub_`: `Idle`, `Scrubbing` (a native drag) or `Skimming` (U-CP7).
- `last_status_`: the last `BarStatus` given to `update()` or taken by a state read.
- `idle_value_`: the slider's value as last set by an Idle `update()` or by an Idle seek
  (`CHANGED`, a buttonless `THUMBTRACK`, or a Windows channel click, §6.5), whichever came later.
  It is the read-your-writes `target`, not `position`.
- `original_`: the frame where the current drag or skim started (copied from `idle_value_`).
- `last_sent_`: as in PR #9.
- `release_value_`: the slider value of the release that ended the last drag, kept until a posted
  `CallAfter(forgetRelease)` runs; empty otherwise.

"Apply the Idle view" means: take a status — on leaving Scrubbing or Skimming a fresh one from the
state read (§3), stored as `last_status_`; on an `update()` the one given — set the slider to its
`target` clamped to the range (only if it differs), set `idle_value_` to it, forget `last_sent_`,
and repaint the strip with the current label showing `position` at `thumbX(target)`. Every entry
into `Idle` applies the Idle view, so the thumb and the label never wait for a later notification,
and the thumb never jumps back to a target older than the bar's own last seek.

**The skim area is the slider** (U-CP7): only the slider's own mouse events enter, move and end a
skim; the strip above it only draws.

`skimKeyDown(state)` is `state.AltDown()` on macOS and `state.ControlDown()` on Windows and Linux
(§3). `frameAt(x)` is the inverse of `thumbX` (§5.3): the slider value nearest to `x`, clamped to
the slider's range (wx-free, `core/timeline`). The event's x is first converted to the wx client
coordinates `sliderTrack` uses (`sliderEventX`: on macOS a mouse event's x is in the NSView's
bounds, which differ from wx client coordinates by the alignment inset; elsewhere unchanged).

A skim starts only while the frame is **active** (the last `wxEVT_ACTIVATE` said so): a motion
over an inactive rawreel window with the key held in another application never starts one, since
that key's release would go to the other application.

### 6.2 Events

| State | Event | Action | Next |
|---|---|---|---|
| Idle | `THUMBTRACK`, left button down (probe, PR #9 C7) | `original_ = idle_value_`; seek (no duplicate); show the marker | Scrubbing |
| Idle | `THUMBTRACK`, left button up (macOS key or wheel) | if `value ≠ idle_value_`: seek, `idle_value_ = value`; no marker | Idle |
| Idle | `CHANGED` with `value == release_value_` (the tail of a GTK release pair) | ignored | Idle |
| Idle | `CHANGED` otherwise (wheel, keys, the tail of a Windows release pair) | if `value ≠ idle_value_`: seek, `idle_value_ = value` | Idle |
| Idle | `THUMBRELEASE` | nothing | Idle |
| Idle | Windows: `wxEVT_LEFT_DOWN` on the slider's channel, outside the thumb (§6.5) | jump: `SetValue(frameAt(x))`, seek, `idle_value_ = value`; do not `Skip()` | Idle |
| Idle | `wxEVT_MOTION` on the slider with `skimKeyDown`, the left button up, the frame active, paused, a stream and an enabled slider | `original_ = last_sent_ = idle_value_`; show the marker; skim to `frameAt(x)` (below) | Skimming |
| Scrubbing | `THUMBTRACK` | seek (no duplicate) | Scrubbing |
| Scrubbing | `THUMBRELEASE` or `CHANGED` | seek if `CHANGED` and new; `release_value_ = value` and post `forgetRelease`; remove the marker; apply the Idle view | Idle |
| Scrubbing | `update()` with the left button up (backstop) | remove the marker; apply the Idle view | Idle |
| Skimming | `wxEVT_MOTION` on the slider with `skimKeyDown` | skim to `frameAt(x)`: if it differs from `last_sent_`, seek (the real thumb is **not** moved); show the ghost thumb at `thumbX(frame)` (§6.4) | Skimming |
| Skimming | `wxEVT_LEFT_DOWN` on the slider | **keep**: remove the marker, apply the fresh Idle view; then the Idle rule for a press (macOS, GTK: `Skip()`, the native press jumps to the same point and a drag may follow; Windows: the channel jump of §6.5) | Idle |
| Skimming | any scroll event (`THUMBTRACK`, `CHANGED`, …; e.g. a click whose `LEFT_DOWN` did not reach the bar, or ⌥/Ctrl+wheel) | keep, as the click; then the Idle row for that event | per that row |
| Skimming | the skim key's `KEY_UP` (anywhere: a `wxEventFilter`, §3); a slider motion without `skimKeyDown`; the slider's `wxEVT_LEAVE_WINDOW`; `wxEVT_ACTIVATE(false)` | **return**: seek to `original_` unless a fresh state read shows `target == original_`; the real thumb is already at `original_`; remove the ghost and the marker; apply the Idle view | Idle |
| Skimming | `update()` showing playback started | keep, as the click | Idle |
| any | `update()` with a new `streamGeneration`, a range change, or no stream | remove the marker; forget `last_sent_`; no seek back; apply the Idle view | Idle |
| Scrubbing, Skimming | `update()` otherwise | store `last_status_`; never touch the slider; repaint the strip's labels | unchanged |

Notes:

- In Idle, "no duplicate" compares with `idle_value_`, not `last_sent_` (which the Idle view
  forgets).
- The release pair after a normal drag: GTK reads the value once and sends `THUMBRELEASE` and
  `CHANGED` with it (`gtk/slider.cpp:41-58`); its `CHANGED` matches `release_value_` and is ignored,
  even when the fresh status names another target (playback started during the drag: `target`
  follows `position`), so it never pauses playback with a seek. Windows reads `TBM_GETPOS` again for
  the `CHANGED` (`msw/slider.cpp:316`), so it carries the value the Idle view just set, which
  equals `idle_value_`. `forgetRelease` is posted with the bar's own `CallAfter` (a `wxEvtHandler`
  member: the bar's destructor drops it) and runs after the pair; a later real `CHANGED` at the
  same value then seeks as usual.
- While Skimming no native drag runs, so the bar may `SetValue` the slider freely, and every key,
  activation and pointer event arrives in order on all three platforms (unlike a native drag on
  macOS, §14.2).
- After a stream change in the middle of a native drag, the next `THUMBTRACK` of that drag starts
  a new scrub in the new stream, with `original_` = the new stream's Idle value.

### 6.3 What the panel shows

| State | Native thumb | Current label | Marker and original label |
|---|---|---|---|
| Idle | at `target` | `position`, at `thumbX(target)` | none |
| Scrubbing | under the pointer (native) | the thumb's value, at the thumb | at `original_`, its number placed by §5.2 |
| Skimming | stays at `original_` (with the filled bar); the **ghost thumb** at `thumbX(skimmed frame)` | the skimmed frame, at the ghost | at `original_` (where the real thumb is), as while Scrubbing |

Consequence of U-CP2, expected and not a bug: over a slow source, on release the number goes from
the thumb's value back to the old `position` and then to the new one when that frame is shown; in
Idle it can sit above a thumb that is already at the target.

### 6.4 Skimming (U-CP7)

- Paused only: with playback running, the skim key does nothing.
- The marker shows where skimming started, so the way back is always visible.
- Only the slider takes part; the skim key changes nothing anywhere else (keys, menus, buttons
  keep their native meaning; Space while the key is held is ⌥/Ctrl+Space, which is not bound).
- **Keep** the skimmed frame: a click on the slider (it moves there for real: the keep's Idle view
  sets the real thumb to the skimmed `target` and the ghost goes).
- **The ghost thumb** (revision 5) is the slider's own knob shape drawn grey at 50 % opacity on
  the native slider, at `thumbX(skimmed frame)`, removed on every exit from Skimming. It is drawn
  natively per platform in the `native_controls_*` files, as
  `void showSliderGhost(wxSlider& slider, std::optional<int> x)` (wx client x; nullopt removes it):
  macOS a `CALayer` sublayer of the NSSlider's layer sized and placed from the cell's knob rect
  (validated on macOS 27: the look the user asked for). Not drawn yet on GTK and Windows (their
  `showSliderGhost` is empty; skimming there still previews and moves the frame number): planned
  as GTK a draw handler on `m_scale` that paints a translucent knob with cairo after GTK draws the
  scale, and Windows the trackbar's `NM_CUSTOMDRAW` post-paint stage painting a translucent thumb
  at the ghost x, each to be built and checked on its platform. The brief's "no ghost thumb" rule
  is overridden here by the user.
- **Return** to `original_`: releasing the skim key, moving off the slider, or the window losing
  activation (the key's release may then go to another application, and a dialog opened by a
  Ctrl shortcut deactivates the window). The return is one ordinary `seek(original_)` (U-EV6),
  sent unless a fresh state read shows `target == original_`.
- A skim starts only on a slider motion, never on the key's press alone, and only with the left
  button up (so never inside a native drag).
- The key's release is taken from its `KEY_UP` alone (GTK reports the modifier state from before
  the event, so the event's own flags are not read).
- No toggle key, no timer: every transition is one of the events in §6.2.
- The native drag (Scrubbing) has no cancel on any platform (U-CP1 superseded). Escape is not used.

### 6.5 Click to jump (U-CP8)

- macOS (NSSlider) and GTK (`gtk-primary-button-warps-slider`) jump to the clicked point
  natively; the bar adds nothing. GTK's native Shift+click page step stays.
- Windows: the trackbar pages on a channel click. The bar binds `wxEVT_LEFT_DOWN` on the slider;
  when the press is outside the thumb (`TBM_GETTHUMBRECT`, `native_controls_msw.cpp`), it jumps
  (§6.2 row) and does not `Skip()`, so the native page step never happens. A press on the thumb
  is `Skip()`ped and starts the native drag as before.
- No double-click action (U-CP9).

## 7. Transport buttons

| Command | Lucide icon | Control |
|---|---|---|
| Open | `folder-open` | `wxBitmapButton`, left, always enabled |
| First | `skip-back` | `wxBitmapButton` |
| StepBack | `step-back` | `wxBitmapButton` |
| PlayPause | `play` / `pause` | `wxBitmapButton`; the bitmap is set only when `status.playing` changes |
| Stop | `square` | `wxBitmapButton` |
| StepForward | `step-forward` | `wxBitmapButton` |
| Last | `skip-forward` | `wxBitmapButton` |
| Loop | `repeat` | `wxBitmapToggleButton`, value from `status.loop` (set only on change) |

- No captions. Buttons never take focus. CP-D4: all but Open are disabled with no stream;
  disabled looks are the platform's (wx derives disabled bitmaps).
- **Tooltips:** `std::string tooltipText(const CommandInfo&)` in `core/commands`, unit-tested:
  "<description> (<key>)", or the description alone for a command without a key. On macOS an
  Accelerator-routed Ctrl key is shown as Cmd ("Open a raw video file (Cmd+O)").
- **Accessible names** are set natively with the command's label: macOS
  `setAccessibilityLabel:` on the button's `NSView` (an `.mm` file); GTK
  `atk_object_set_name(gtk_widget_get_accessible(...))` in `gui_platform_gtk.cpp`; Windows through
  `wxAccessible` if the Windows build has `wxUSE_ACCESSIBILITY 1`, else a documented gap.
  `SetName` is set too, for selftests only.
- **Icons:** `src/gui/icons/lucide_icons.h`, checked in: one raw string literal per icon, the
  Lucide version in a comment. `src/gui/icons/LICENSE` is Lucide's licence file verbatim (ISC,
  plus the MIT notice of the Feather icons it derives from). Before `FromSVG`, the code replaces
  `currentColor` (in `stroke` and `fill`) with the system button-text colour, and rebuilds every
  bundle on `wxEVT_SYS_COLOUR_CHANGED` (dark mode).
- **Notices:** `packaging.cmake` appends a notices entry for Lucide, in dvv-build's
  `write-notices.cmake` format (separator, `lucide <version>`, `Licence: ISC AND MIT`, the source
  URL, the `--- LICENSE ---` block from `src/gui/icons/LICENSE`) to the stripped notices file,
  and appends `lucide` to `RAWREEL_SHIPPED`, so `check-package.cmake`'s exact match still passes
  and About → Third-party notices lists it.

## 8. Rendering

`TimelineStrip` paints with `wxAutoBufferedPaintDC`, background style `wxBG_STYLE_PAINT`, and
refreshes only when something it draws changed (range, width, DPI, colours, a label's text or x,
the marker). No child controls are created or moved per update. Layout is recomputed on size and
DPI changes. If Stage 1 shows the strip does not repaint during the macOS tracking loop, the
`THUMBTRACK` handler calls `Refresh()` and `Update()` on it. No OpenGL, WebView or other UI
framework.

## 9. Tests

- **Unit, wx-free** (`tests/`):
  - tick intervals: short, long and five-digit ranges at 400 and 2000 DIP; `length == 1`;
  - placement: clamping at both edges; the current label hiding an end label; the marker at frame 0
    with the thumb at 3 (the original label's number hidden, the line kept); the original label
    overlapping an end label; the side flip with hysteresis as the thumb crosses the marker;
  - `thumbX` at min, middle and max, and for the 0..1 range; `frameAt` as its inverse (rounding to
    the nearest frame, clamping outside the travel, the 0..1 range);
  - `tooltipText` with and without a key, and the macOS Cmd form.
- **Session** (`tests/test_session*.cpp`): `streamGeneration` changes on open, reopen of the same
  clip and close, and not on a seek or a step.
- **Selftest** (`--selftest player`); the release helper reads the slider's value once and sends
  `THUMBRELEASE` then `CHANGED` with it, as GTK does (the harder case); tracks and the pair are sent
  in one action, so the notification for the last seek is still queued at the release.
  - a quick drag 10 → 35 released at once: the thumb never shows 10 or 20 after the release, and
    the seeks are exactly `20, 35`;
  - Space during a drag, then the release pair: playback keeps running; Observables: a read-only strip model (marker frame,
  label texts and x, from the placement result) and the Play/Pause icon id — no bitmap comparison.
  - marker shown during a held drag, gone after release;
  - Escape during a drag does nothing (revision 4: no drag cancel);
  - a `THUMBTRACK` with the button up (probe) seeks and stays Idle with no marker;
  - skimming (synthetic `wxMouseEvent`/`wxKeyEvent` with the platform's skim modifier, through
    `injectInput`): motion with the key enters Skimming, shows the marker at the start frame and
    seeks to `frameAt(x)`; the real slider stays at the start frame while the ghost (a test seam
    reporting the ghost's x, or none) is at `thumbX(frameAt(x))`; every exit removes the ghost; a
    keep moves the real slider to the skimmed frame; further motion seeks without duplicates; the key's release, a motion
    without the key, leaving the slider and `wxActivateEvent(false)` each return to the start
    frame; a click keeps the frame; a motion with the left button down does not enter; with
    playback running the key does nothing; a stream change while Skimming resets without a seek
    back;
  - Windows only: a left-down on the channel jumps to `frameAt(x)` with no page step; a left-down on
    the thumb starts the native drag;
  - with `--debug-seek-delay`, the Idle label shows `position` while `target` differs;
  - a stream change during a drag does not seek back, both for a clip of another length and for a
    reopen of the same clip (`streamGeneration`);
  - the Play/Pause icon id follows `status.playing`; all buttons but Open are disabled with no
    stream.
- **Manual** (written after Stage 1): drag both ways, endpoints, five-digit labels, resize around
  400 DIP, a DPI change, dark mode, skimming with the platform's key, click to jump, keyboard and
  wheel on a focused slider, Loop's on state, per platform.

## 9a. Manual scenarios

Run on each platform (Windows, Linux, macOS), in light and dark mode. The lab is
`rawreel-panel-lab` (a fake player, no media needed); the app is `rawreel-gui` with a clip or a
`synthetic:` source. Native mouse drags and screen readers are not covered by the selftests.

| Scenario | Command | Expected |
|---|---|---|
| Drag both ways | `rawreel-panel-lab` | While held, the marker stays at the start frame and the current number follows the thumb; on release the marker goes and the number shows the frame on screen. Left to right and right to left alike. |
| Endpoints | `rawreel-panel-lab` | Drag to the far left and the far right: the first and last numbers stay readable; the current number stays inside the strip and hides the end number it covers. |
| Five-digit labels | `rawreel-panel-lab --frames 99999` | Ticks thin out; no two numbers overlap; the last number `99998` fits. |
| Resize around 400 DIP | `rawreel-panel-lab` | The window cannot shrink below the bar's best width (400 DIP); at and near that width the strip and the buttons stay intact. |
| DPI change | `rawreel-panel-lab` | Move the window between monitors of different scale: icons, ticks and the number stay crisp and aligned with the thumb. |
| Dark mode | `rawreel-gui --appearance dark <clip>`, then the system switch | The strip, ticks, numbers and icons follow the colours; Loop's on state is visible in both modes. |
| Skimming | `rawreel-panel-lab --seek-delay 200`, paused | Hold ⌥ Option (macOS) or Ctrl (Windows, Linux) and move over the slider: the real thumb and the filled bar stay, a grey 50 % ghost thumb follows the pointer, the number follows the ghost and the picture previews. Release the key: the ghost goes, back to the start frame. Again, then click: the real thumb jumps to the ghost and stays. |
| Skimming ends off the slider | the same | While skimming, move the pointer off the slider (up to the numbers or down to the buttons): back to the start frame. |
| Click to jump | `rawreel-panel-lab` | Click the slider away from the thumb: it jumps to the clicked frame (Windows too; no page step). On GTK, Shift+click pages (native). On Windows, also a quick double click on the channel (the second press arrives as a double-click event, which the bar does not handle). |
| Keyboard and wheel | `rawreel-panel-lab` | On the focused slider, arrows, Home, End, Page keys and the wheel seek and never show the marker. |
| Loop's on state | `rawreel-panel-lab` | Click Loop: it shows as on (icon and button state, readable contrast); click again: off. |
| Label over the thumb | `rawreel-panel-lab --frames 2`, then the default | At min, middle and max the number is centred over the drawn thumb to within 2 px. |
| Skim thumb under the pointer | `rawreel-panel-lab --debug-transport`, paused | Skim at both ends and the middle: the trace's pointer x and `nativeThumbCentre` differ by at most 2 px. |
| Click on the skimmed thumb | the same | Skim, then click without moving: the trace shows the slider's left-down and the frame stays (GTK especially, §14.2). |

## 10. Stages

1. **Feasibility lab.** `rawreel-panel-lab`, a standalone `wxApp` target, not packaged and not
   linked into `rawreel-gui`. It compiles the real `TransportBar`, `TimelineStrip` and geometry
   sources and feeds `update()` synthetic statuses from a lab-only timer (the no-timer rule binds
   the product, not the lab's fake player), with fake `execute` and `seek` callbacks (optionally
   delayed). Its code becomes the deliverable of Stages 2–4, and the lab stays as the tool for the
   manual checks. It measures:
   - the native track and thumb queries per platform and DPI, against the drawn thumb;
   - Escape during a native drag (revision 3; measured on macOS, §14.2, and no longer needed in
     revision 4);
   - that `wxEVT_LEFT_DOWN` reaches the bar on a native slider;
   - VoiceOver, Narrator and Orca names on an icon-only button;
   - Loop's on state and icon contrast on macOS (image buttons get `NSRegularSquareBezelStyle` or
     `NSRoundedBezelStyle`, `cocoa/button.mm:139-176`);
   - the strip repainting during the macOS tracking loop;
   - the old and new best sizes of the bar.
   Output: a findings section appended to this document and the chosen constants.
2. **Layout and icons** in `TransportBar`: two rows, Open left, centred group, Lucide bitmaps,
   tooltips, accessible names, Stop and Open buttons, the right-hand label removed, notices entry.
3. **Timeline strip**: ticks, end numbers, current number (U-CP2), colours, degenerate cases.
4. **Scrubbing**: `streamGeneration` in the Session, the state table of §6.2, marker, original
   number (revision 3 also built the drag cancel, which stage 6 removes).
5. **Verification**: selftests, the manual scenarios, CI on all platforms.
6. **Revision 4**, on the same branch:
   1. Spike on macOS in the lab: `wxEVT_MOTION`, `ENTER_WINDOW`, `LEAVE_WINDOW` reach the bar from
      the native slider with no button down, and `KEY_DOWN`/`KEY_UP` of the skim key reach the
      frame; the same on GTK and Windows by hand (`--debug-transport`). If the native slider gives
      no motion on a platform, skimming is absent there (documented), never with a timer.
   2. Remove the drag cancel (`Cancelled`, `cancelScrub`, `leaveCancelled`, Escape in
      `onCharHook`, the activation exit) and its selftests; keep `wxEVT_ACTIVATE` for the skim
      return.
   3. `frameAt` in `core/timeline` (unit-tested); Skimming in `TransportBar` (§6.2, §6.4) on the
      slider only, with a `wxEventFilter` for the key's release; selftests.
   4. Windows channel click (§6.5).

## 11. Acceptance

The brief's criteria (its section 13) hold, plus:

- A drag has no cancel; Escape does nothing during or outside a drag.
- While paused, holding the platform's skim key (⌥ Option on macOS, Ctrl on Windows and Linux)
  over the slider previews frames; releasing it, leaving the slider or losing activation returns
  to the start frame; a click moves to the skimmed frame. Nothing else reacts to the key.
- A click on the slider away from the thumb jumps to the clicked frame on every platform.
- No double-click action; playback starts with Play or Space only.
- In Idle the label above the thumb shows the frame on screen.
- Keyboard and wheel input on a focused slider never shows the marker.
- The bar's minimum width is at least 400 DIP; the frame's minimum follows `GetBestSize()`.
- The panel adds no timer and no polling.

## 12. Left to the builder

Exact pixel thresholds after Stage 1 (`MIN_MINOR_TICK_DIP`, `LABEL_FLIP_HYSTERESIS_DIP`), the
lab's simulated-player details, and which platform file hosts each native query.

## 13. Review record

Revision 5 (user, after trying revision 4 on macOS): during a skim the real thumb and the filled
bar stay at the current frame and a ghost thumb (grey, 50 %) on the slider shows the future
position, while the picture still previews; a click moves the real thumb there. The mode is the
original drag interaction, now entered with the held key instead of a mouse press (Escape could
not cancel a native drag on macOS). A throwaway macOS spike (a `CALayer` ghost on the NSSlider)
was validated by the user on macOS 27.

Revision 4 (user decisions after the macOS Escape check, §14.2): the native drag on macOS holds
Escape until the release, so a drag cancel cannot work there. Following the per-platform-native
principle, the user replaced the cancel with skimming on a held key — ⌥ Option on macOS, Ctrl on
Windows and Linux, chosen as each platform's native "alternate mode" key (Shift conflicts with
GTK's Shift+click page step; Alt alone opens the Windows menu bar; Control-click is a right-click
on macOS) — removed every drag cancel (U-CP1, U-CP4, U-CP6 superseded), made a click on the slider
jump on every platform (U-CP8), and ruled out a double-click action (U-CP9). The leave, activation
and step-key rules of §6.4 are Claude's calls within those decisions.

Revision 3 (after the plan reviews): the release that ends a drag used the last delivered status,
which can be older than the bar's own last seek (the thumb jumped back), and GTK's trailing
`CHANGED` then seeked again and paused a playback started during the drag. Fixed by the state
read on leaving Scrubbing or Cancelled (§3, §6.1) and `release_value_` (§6.2).

Revision 1 was reviewed by two critics (optimistic: accept with fixes, C1 I5 M5 S7; pessimistic:
reject, C1 I12 M13). Revision 2 addresses all findings:

- The shared Critical — the trailing `CHANGED` of the Windows/GTK release pair undid the cancel —
  is fixed by the posted `leaveCancelled` (§6.2), and the §4 GTK cell is corrected.
- Stored status and "apply the Idle view" on every entry into Idle (§6.1); the Cancelled view
  (§6.3); exits from Cancelled on left-down and activation (§6.2).
- Buttonless `THUMBTRACK` stays Idle (§6.2); §3 corrected on which keys are consumed.
- The ordered placement algorithm with priority and hysteresis (§5.2).
- Native track and thumb queries first; Windows thumb width from `TBM_GETTHUMBRECT` (§4, §5.3).
- `streamGeneration` (U-CP5) for same-length stream changes (§6.2, §9).
- macOS cancel limitation (U-CP4) and the Windows activation order (§6.5); paused after a cancel
  (U-CP6).
- Minimum size on the strip, not the bar window; height growth stated (§5.1).
- Accessible names set natively; `SetName` for selftests only (§7).
- Notices entry through `packaging.cmake` and `RAWREEL_SHIPPED` (§7).
- `tooltipText` and the macOS Cmd form (§7); Play/Pause and Loop set only on change; CP-D4.
- Colours, the marker's look, degenerate ranges, a fixed strip height, no font shrink (§5.2).
- The lab compiles the real classes; `currentColor` answered from the NanoSVG source (§4, §10).
- Tests and observables for each of the above (§9).

## 14. Findings

### 14.1 Native slider travel (Stage 1, automatic part)

`rawreel-panel-lab --report` (CTest `rawreel.panel_lab.report`) asks the native slider for its
track and thumb and compares the thumb centre with the formula
`travel_start + (value - min) * (travel_end - travel_start) / (max - min)`, where the travel is the
track with half a thumb removed at each end. It fails above 2 px error.

macOS (Debug, Retina, wxMac, wx 3.3.4): the track starts 10 px from the slider's left edge and ends
10 px from its right edge; the thumb is 20 px wide, the same as the fallback. The error is 0 px at
the minimum, middle and maximum value, in both stages. The conversion from the cell's rect to wx
client coordinates subtracts the alignment inset. No measured constant was needed.

| Stage | Before: one-row bar (Task 3) | After: two-row panel |
|---|---|---|
| initial (client 800 DIP) | track 10..303, best 637x32 DIP | track 10..786, best 400x88 DIP |
| slider 400 DIP | track 10..390, best 637x32 DIP | track 10..390, best 400x88 DIP |

Verbatim output of the final branch:

```
port wxMac dpi scale 2.00
stage initial size: client 800x88 DIP, slider width 796 px
track start 10 end 786 thumb 20
value 0 native 10 computed 10 error 0
value 2283 native 398 computed 398 error 0
value 4566 native 786 computed 786 error 0
best 400x88 DIP
stage slider 400 DIP: client 404x88 DIP, slider width 400 px
track start 10 end 390 thumb 20
value 0 native 10 computed 10 error 0
value 2283 native 200 computed 200 error 0
value 4566 native 390 computed 390 error 0
best 400x88 DIP
```

`rawreel.panel_lab.report` on Windows: not run (CI). On Linux: not run (CI).

### 14.2 Manual checks

| Item | Windows | Linux (GTK) | macOS |
|---|---|---|---|
| Current label over the drawn thumb at min, middle, max, at 100 % and a higher DPI | manual, not run | manual, not run | manual, not run |
| Escape during a native drag | not needed (revision 4) | not needed (revision 4) | measured: **no cancel** (see below); led to revision 4 |
| Pointer motion and enter/leave from the native slider with no button down (§10 stage 6) | spike, not run | spike, not run | measured: **yes** (see below) |
| Skim key `KEY_DOWN`/`KEY_UP` reach the frame | spike, not run | spike, not run | measured: **yes** (see below) |
| `wxEVT_LEFT_DOWN` reaches the bar on a native slider (a click on the skimmed thumb, §9a) | manual, not run | manual, not run (see below) | manual, not run |
| Skim thumb within 2 px of the pointer (§9a) | manual, not run | manual, not run | manual, not run |
| Screen-reader names of the icon buttons | Narrator: manual, not run | Orca: manual, not run | VoiceOver: manual, not run |
| Loop's on state and icon contrast | not applicable | not applicable | manual, not run |
| The strip repaints during the macOS tracking loop | not applicable | not applicable | manual, not run |

macOS Escape, measured on 2026-11-09 (macOS 27.0.1, Apple Silicon, Debug build of `77458c0`,
`rawreel-gui --debug-transport` on a 4567-frame synthetic clip): Escape pressed while holding the
thumb did not cancel the drag. The log shows six drags, each `Idle -> Scrubbing -> Idle`, and no
`Scrubbing -> Cancelled`: the key reaches the frame only after the native tracking loop has ended
the drag, when the bar is Idle and Escape does nothing. So macOS cannot have an Escape cancel of
a native drag. Revision 4 drops the drag cancel on every platform and adds skimming (U-CP7).

macOS skimming spike, measured on 2026-11-09 (same machine; a throwaway `wxEventFilter` in the
lab, not committed, logging mouse and modifier events on the slider and the strip):

- The native NSSlider sends `wxEVT_MOTION` with no button down (742 events in the session, none
  with the left button down), and `ENTER_WINDOW`/`LEAVE_WINDOW` on entry and exit; the strip
  does the same (249 motions).
- Moving between the strip and the slider gives `LEAVE` of one (y just outside its edge, e.g.
  `y -2`) then `ENTER` of the other. (Skimming uses the slider only, so the strip's events are
  not used.)
- ⌥ Option gives `KEY_DOWN`/`KEY_UP` with `WXK_ALT`, with the pointer still; every mouse event
  carries `AltDown()` while it is held (224 motions with it). ⌘ Command reports as `WXK_CONTROL`
  and `ControlDown()`, as §3 says.

So on macOS skimming on the slider works from events alone. Windows and GTK are still to be
checked by hand with `--debug-transport`. The selftests cover skimming's state machine on every
platform with synthetic events; native event delivery is measured on macOS only.

Known GTK points to check by hand: GTK3 wraps the `GtkScale` in a `GtkBox` that wx connects its
mouse handlers to, so a click on the skimmed thumb (no value change, so no scroll event) must be
seen to reach the bar as a left-down; if it does not, `native_controls_gtk.cpp` connects
`button-press-event` on `m_scale` and forwards it, as wx does for `wxChoice`. The slider's
`LEAVE_WINDOW` on GTK comes from the next motion in another wx window, so leaving the frame
straight from the slider is noticed late (until the key's release or the next motion).
