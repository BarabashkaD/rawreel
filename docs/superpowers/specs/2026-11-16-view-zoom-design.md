# View zoom — design (simple version)

Status: rev 5 (2026-11-16), after the builder check and the optimistic and pessimistic reviews. Replaces the rev 4 design on `feat/view-zoom-1`
(`2026-11-10-view-zoom-design.md`, 870 lines) and its plan. The user asked on 2026-11-16 to make
the feature as simple as possible and focus on the GUI. Rev 4's decisions still hold where this
document does not change them; everything rev 4 had that is not here is cut (§10).

## 1. Intent

Today rawreel always fits the frame to the viewport. The user wants to:

1. See a new clip at its original size when the screen allows it, with the window sized to the clip.
2. Fit the clip to the window, and go back to Fit at any time.
3. Show the clip at actual size (100 %) inside the current window, and pan when it is larger.
4. Zoom in and out.
5. Resize the window to the clip at actual size on demand.

Reading pixel values (a hover readout or a magnifier, as in YUView and vooya) is an important later
feature. It is not in this design. This design only keeps the one function it will need (§6).

## 2. Decisions

"(user)" marks the user's decisions; "(call)" marks a call made in the design that the user has
not confirmed yet.

1. **100 % is one clip pixel per physical screen pixel** (one pixel of the window's backing store).
   On a 2x Retina display a 1920×1080 clip covers 960×540 points. (user, 2026-11-11)
2. **Two view modes.** *Fit* follows the viewport size. *Zoom* holds a fixed scale and a pan
   position; a window resize keeps the scale and the image point at the viewport's centre.
   Actual size is Zoom at 100 %. (user, 2026-11-11)
3. **Every open sizes the window** (§3). (user, 2026-11-16: "resize on every open")
4. **Zoom steps.** 12.5, 25, 50, 100, 200, 400, 800, 1600, 3200 %. Zoom in and out go to the next
   step strictly above or below the current zoom (also after a pinch left it between steps). The
   fit zoom is an extra stop: a step that crosses it stops there and sets Fit. A fit within 1 % of
   a step counts as that step, so a key press never lands on a fit that looks the same (after an
   open at 100 % the fit is often 100.05 %). The range is min(fit, 12.5 %) to max(fit, 3200 %).
   (user, 2026-11-16, simple version; the 1 % rule is a call)
5. **Pan is clamped.** No background shows past the picture's edge on an axis where the picture is
   larger than the viewport; on a smaller axis the picture is centred and does not move. No
   scrollbars. (user, 2026-11-11)
6. **Readout:** a new status bar field, "Fit (47 %)" in Fit, "200 %" in Zoom, empty without a
   stream. The window's minimum width grows by this field. (user, 2026-11-11)
7. **Keys follow each OS's native viewers** (rev 4 §8.1 has the sources). (user, 2026-11-11)
8. **Menu labels per OS:** macOS uses Preview's names in title case; Windows and Linux use
   sentence case. (user, 2026-11-11)
9. **Sampling is unchanged:** the nearest texel at 100 % and above, today's filter below. The
   shader does not change. (user, 2026-11-11)
10. **No new argument to `openUri`** (user, 2026-11-16). The open is recognised as Open Recent
    already does it (§3.3).
11. **No dvv library change.** Everything is in rawreel.

## 3. Opening a clip

### 3.1 What the user sees

When a new clip opens (Open, Open URI, a drop, Open Recent, the command line, View → Format):

- **The window fits on the screen with the clip at 100 %:** the window resizes so the viewport is
  exactly the clip at 100 %. It grows for a larger clip and shrinks for a smaller one. The window
  keeps its top-left corner and moves only as far as it must to stay on the screen. The readout
  shows "100 %".
- **It does not fit:** the window becomes the largest window that keeps the clip's shape (no
  background bars) and fits on the screen. It is centred on the screen and the clip shows in Fit.
  The readout shows "Fit (63 %)" (the real value). (user, 2026-11-16: keep the clip's shape)

### 3.2 Rules

- "The screen" is the work area (without the menu bar, Dock or taskbar) of the display the window is
  on: `wxDisplay::GetFromWindow`, falling back to display 0, then `GetClientArea()`. The whole
  outer window must fit in it. (call)
- The window's parts around the viewport (title bar, infobar, transport bar, status bar, docked
  panes such as the Log pane) are measured as they are at that moment: outer size minus viewport
  size. (call)
- Clip size in window units = clip pixels / the viewport's content scale, rounded up (points on
  macOS, physical pixels on Windows, GTK logical pixels on Linux).
- **Smaller than the minimum window:** the window takes its minimum size and the clip shows at 100 %,
  centred, with background around it. On a 2x display this applies to every clip narrower than about
  1900 pixels, since the minimum window width is about 950 points (status bar fields plus 400 DIP,
  plus the zoom field). (call, follows from §2.6)
- **Maximised (Windows, Linux) or full screen (macOS):** the window stays as it is. The clip shows
  at 100 % if it fits the viewport, otherwise in Fit. (call)
- **Reopen** (also after a lost device) keeps the window and the view. **Close** keeps the window
  size and drops the view back to Fit. (call)
- **View → Format** counts as an open: it sizes the window again, also when the frame size is
  unchanged. (call: the simplest rule, since Format goes through `openUri` like every open)
- If something around the viewport changes size after the window moved (a dock growing back, a
  window manager adjusting the size), nothing corrects it: the view stays 100 % and any small
  overflow can be panned. (call)

### 3.3 How the open is recognised (no new argument)

`openUri` already marks a pending open for Open Recent (`RecentPending::opened`), and
`onStreamsChanged` already matches the arriving stream to it by its requested URI
(`RecentPending::streamsChanged`, `StreamInfo::requestedUri`). The window sizing uses the same
pattern, in its own small member (not `RecentPending`, which clears itself for opens that record no
entry):

- `openUri`, inside its `session != nullptr` branch, stores the pending open's `dvv::formatUri`
  and sets the view request to Fit, so a new clip of the old size never shows the old zoom.
- `onStreamsChanged` applies §3.1 only when the first stream's `requestedUri` equals the stored one,
  then clears it. So with two opens in flight, the first clip's streams do not use up the second
  clip's sizing. Empty stream lists (sent before every open's and every Reopen's streams) leave it.
- The sizing runs at the end of `onStreamsChanged`, after the infobar has been dismissed and any
  warning of the new stream (such as "trailing bytes ignored") shown, so the window is measured
  with the infobar as it will stay.
- An open error, a cancelled Format dialog and Close clear the stored open. Reopen does not go
  through `openUri`, so it never sets it.

Until the GUI hears of the new stream, the renderer draws its first frames in Fit; the window then
resizes. One or two frames in Fit are accepted.

On Linux, a command-line open runs before the window is mapped, when the decorations are not known
yet (`applyLayoutLimits`); the window manager may then clamp the window, and §3.2's last rule
covers the result.

## 4. Commands and keys

Five commands in the View menu, before Format…, available only with a stream:

| Command | macOS label | Windows and Linux label | macOS key | Windows and Linux key |
|---|---|---|---|---|
| Fit | Zoom to Fit | Fit to window | ⌘9 | Ctrl+0 |
| Actual size | Actual Size | Actual size | ⌘0 | Ctrl+1 |
| Zoom in | Zoom In | Zoom in | ⌘+ | Ctrl++ |
| Zoom out | Zoom Out | Zoom out | ⌘− | Ctrl+− |
| Resize window to actual size | Resize Window to Actual Size | Resize window to actual size | ⌘1 | Alt+1 |

- Keys and menu items zoom around the viewport's centre.
- Zoom in is disabled at the maximum, Zoom out at the minimum.
- Ctrl++ must work with and without Shift and from the keypad, on US, German and French layouts.
  `normaliseKey` already folds '=', '+' and keypad + into Plus. The builder picks the route
  (accelerator or char hook) that achieves this; rev 4 §8 describes the wxMSW pitfall.
- **Resize window to actual size** sizes the window as in §3.1, first case, also when the clip is
  too large: each axis is then clamped to the screen, and the clip shows at 100 % panned to its
  centre. Maximised or full screen: it acts as Actual size.
- All five share the existing "has a stream" enable test (`MainFrame::isAvailable`).
- No bare keys: bare digits, + and − stay free, since rawreel's bare keys are playback keys.

## 5. Mouse and trackpad

On the viewport, only with a stream:

| Input | All OSes |
|---|---|
| Left- or middle-button drag | pans (mouse capture; `wxEVT_MOUSE_CAPTURE_LOST` handled) |
| Wheel, two-finger scroll | pans when the picture overflows; otherwise nothing |
| Ctrl+wheel (Windows, Linux only) | zooms one step per wheel notch, at the pointer; partial deltas (touchpads) add up until they reach a notch |
| Pinch (`wxEVT_GESTURE_ZOOM`) | zooms smoothly at the gesture position; no snap at the end |

- **macOS has no scroll zoom** (user, 2026-11-16). Pinch and ⌘+/⌘− zoom, as in Preview and
  QuickTime Player; scroll always pans, with or without ⌘. Reason: wxOSX reports trackpad scroll as
  points with a 10-point "notch" and keeps the modifier on momentum events, so one ⌘+flick would
  step 20 or more times. Ctrl+scroll is left alone too: the OS uses it for its accessibility zoom.
- A Windows precision touchpad sends pinch as Ctrl+wheel, so it steps through the zoom steps.
- The drag cursor during a drag only: the closed hand on macOS, the move cursor on Windows, the move
  (fleur) cursor on Linux, with the standard wx cursors.
- A drag in Fit, or with no overflowing axis, does nothing.
- Known limit, accepted for now: wxOSX rounds trackpad scroll deltas to whole points, so a very slow
  two-finger pan on macOS can stall. If the manual check finds it bad, a native fix is a follow-up.
- Not used: double-click (kept for full screen later), Space (Play/Pause), the right button.
- Pinch needs `EnableTouchEvents(wxTOUCH_ZOOM_GESTURE)` on the viewport (wxOSX attaches its
  recognizer only then). `wxEVT_MAGNIFY` is not bound, so a macOS pinch zooms once. On GTK/X11 a
  touchpad pinch usually does not arrive as a gesture; Ctrl+wheel is the way to zoom there.
- The selftest input filter (`isRealInput`, a fixed-size array) already covers wheel, motion and
  middle-button events; it gains `wxEVT_GESTURE_ZOOM` (and its array grows). Capture-lost stays
  out: it is not user input.

## 6. Where the code goes

All of it extends existing files; no new modules.

- **`src/core/view` (wx-free, unit tested; next to `ViewTransform::fit`):**
  - `ViewRequest { mode, zoom, centre, image }`, where `image` is the frame size the request was
    made for.
  - `resolveView(request, viewport, image)`: Fit uses today's `ViewTransform::fit`. Zoom uses
    clamped whole-pixel offsets. A Zoom request made for another frame size resolves to Fit.
  - `imagePointAt(transform, viewport point)`: the clip pixel under a point. The zoom code uses it,
    and so will the future pixel-value readout.
  - `zoomAround`, `panBy`, `zoomLimits`, `nextZoomIn`/`nextZoomOut` (the steps with the fit stop),
    `zoomText`.
- **`src/core/layout` (wx-free, unit tested):** one function that returns the outer window
  rectangle for a wanted viewport size, given the current outer rectangle, viewport size, minimum
  client size and work area. It uses the existing `ScreenRect` and the work-area clamp that
  `infoWindowPlace` already has. It has a "keep shape and fit" variant (§3.1, second case) and an
  "actual size, clamped" variant (§4).
- **Work area:** the `GetFromWindow` → display 0 → `GetClientArea()` lookup is already written out
  in `info_window.cpp` (twice) and `selftest.cpp`; one small GUI helper serves those and this
  feature.
- **Renderer:** `CompositeParams` gains the `ViewRequest`. The renderer resolves it against its own
  extent and frame (`compositeConstants` takes the resolved transform). `RecordedInfo` reports the
  resolved zoom, origin and mode for selftests.
- **GUI:** the commands in `commands.cpp`; the handlers and the open sizing in `MainFrame`; the
  pointer handlers in `Viewport`; the status bar field. The field is one more fixed field in
  `fixedFieldWidths`, measured on its widest text; `applyLayoutLimits` already adds the fixed
  fields to the minimum width, so the minimum grows with no extra code. The readout is updated when
  the view changes and, in Fit, when the viewport resizes. Its text has up to one decimal, a
  trailing ".0" dropped ("12.5 %", "Fit (47.3 %)").

## 7. Tests

- **Unit:** `resolveView` (Fit, Zoom, overflow on one or both axes, the edge clamps, a request made
  for another frame size); `imagePointAt` inverting it; `zoomAround` keeping the anchor point;
  `panBy` clamping; the steps with the fit stop and limits; `zoomText`; the window function (fits,
  too large, below the minimum, moved back on screen, keep-shape variant); the command table's keys
  per OS with no two commands on one key.
- **GPU:** one compositor case against the CPU reference at 200 %, panned.
- **Selftests** (synthetic events, no timers). The open-sizing steps use a test-only work area and
  derive the clip size from the minimum client size and the content scale, so they give the same
  result on a 2x Mac and on a small 1x CI screen. Existing steps that assume the 1280×720 start
  window in Fit are updated, since every open now sizes the window.
  - a clip that fits opens at 100 % with the viewport equal to the clip;
  - a clip larger than the test work area opens in Fit with the window inside it;
  - two opens in quick succession size the window for the second clip;
  - Reopen keeps the view;
  - each command from its menu item and one key per OS;
  - Zoom out stops at Fit;
  - a wheel pan, a Ctrl+wheel step at the pointer (Windows, Linux), a Cmd+wheel that pans and
    does not zoom (macOS), a drag, and a synthetic pinch;
  - the readout after each step.
- **Manual, per OS:** the zoom keys on US, German and French layouts; trackpad pan and pinch; open
  sizing on a second display with another scale.

## 8. Docs

README: the view section (Fit, Actual size, zoom 12.5–3200 %, window sizing on open), the keys per
OS, one line on mouse and trackpad, the status bar field, and in "Current limits" that a clip
smaller than the minimum window shows with background around it.

## 9. Split

1. View maths, renderer, commands and keys, readout, open sizing, Resize window to actual size.
2. Mouse and trackpad.

## 10. Cut from rev 4

The macOS native scroll hook and sub-point trackpad pan; all Cmd+scroll zoom on macOS, scroll phases
and momentum handling; snapping after a pinch; pan with the fingers during a pinch; keyboard pan
(Ctrl/Cmd+arrows); Shift+wheel sideways pan; check marks on Fit and Actual size; the macOS extra
`Cmd+=` key; the GTK "grabbing" cursor helper; the Windows DWM visible-frame helper; the extra
window-size correction pass; the GPU precision test at 2400 %; the `ViewOnOpen` argument; five PRs
(now two).

## 11. Non-goals

Pixel values (hover readout, magnifier, values drawn on pixels): a later feature, built on
`imagePointAt`. Also: full screen, a pixel grid, region zoom, a zoom presets menu, saving the zoom,
the sample aspect ratio, animated zoom, scrollbars, macOS smart magnify, and `rawreel-cli` changes.
