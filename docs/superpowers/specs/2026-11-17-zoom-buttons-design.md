# Zoom buttons on the control bar — design

Status: approved (2026-11-17), after a builder check and an optimistic and a pessimistic review. Builds on the view zoom design
(`2026-11-16-view-zoom-design.md`, PRs #32 and #34) and lands after them.

## 1. Intent

The view zoom commands (Fit, Actual size, Zoom in, Zoom out, Resize window to actual size) are
reachable through the View menu, keys, the mouse and the trackpad. The user wants them one click
away on screen, as viewers usually offer, instead of going through the menu. The user chose the
layout from mockups (option B, 2026-11-17):

```
[open] [format]        [|<] [<|] [>] [■] [|>] [>|] [loop]        [−] [ 200 % ▾ ] [+]  [i]
```

## 2. Decisions (user, 2026-11-17: option B and every guess below agreed)

1. **Place.** On the control bar's right side, before the "i" button: a Zoom out button, a zoom
   button that shows the current zoom, a Zoom in button.
2. **The zoom button** shows the current zoom with the readout's text ("Fit (37.5 %)",
   "200 %"; `zoomText`) and a drop-down mark. A click opens a menu:
   - Fit to window, Actual size;
   - a separator; 25 %, 50 %, 200 %, 400 %, 800 %, 1600 %;
   - a separator; Resize window to actual size.
   The entry for the current view is checked: in Fit only Fit to window (also when the fit zoom
   happens to equal a percentage); at 100 % Actual size; otherwise the percentage the zoom equals,
   if any (at 12.5 %, 3200 % or between steps after a pinch, nothing is checked; the button still
   shows the value). Fit, Actual size and Resize show their keys, using each OS's
   labels and keys from the command table. A percentage zooms around the viewport's centre, like
   Zoom in and Zoom out from the menu.
3. **The status bar's zoom field is removed**; the zoom button replaces it. The window's minimum
   width therefore no longer includes that field.
4. **− and +** run Zoom out and Zoom in exactly as the View menu items do (one zoom step, with Fit
   as a stop on the way, not a floor), disabled exactly when those menu items are
   (`MainFrame::isAvailable`); tooltips name the command and its key, as the bar's other buttons do.
5. **Without a clip** all three are disabled and the zoom button shows "—".
6. **Native per OS**: the bar's existing button style on each OS; the menu is the OS's native
   pop-up menu.
7. **Not included:** typing a zoom, a zoom slider, a separate button for Resize window to actual
   size (it is in the menu).

## 3. Calls made in this design

- **The zoom button** is a plain text button (`wxButton`) whose label is the zoom text followed by
  " ▾"; a click opens a pop-up menu (`PopupMenu`) under it. It takes no keyboard focus, like the
  bar's other buttons. It has no command of its own, so it is not one of the bar's icon buttons
  (`buttons_`); its width is measured with its own font (also again on a DPI change) for the text
  "Fit (8888.8 %) ▾", so the bar does not shift as the zoom changes. A longer text (a fit above
  8888.8 %, only for a tiny clip in a large window) is shortened with "…".
- **− and +** are ordinary bar buttons (`addButton` with `Command::ZoomOut` / `ZoomIn`), so the
  tooltip, the accessible name and the icon tinting come from the existing code.
- **Icons:** Lucide `zoom-out` and `zoom-in` (magnifier with − and +), added to the embedded Lucide
  set (`src/gui/icons`).
- **The playback group stays centred** on the bar: with the zoom group on the right, the right side
  is wider than Open and Format, so the left side gets the padding (today `syncSpacer` pads the
  right side next to "i"; it pads whichever side is narrower).
- **The window's minimum width does not grow**: the bar's minimum grows by the zoom group (twice
  over, because of the centring), the status bar's shrinks by the removed field; a rough estimate
  on GTK is about 840 DIP for the bar against about 890 DIP for the status bar. A selftest asserts
  that the bar's minimum width is not larger than the status bar's. If that fails on an OS, the
  centring padding goes first (the playback group then sits slightly left of centre).
- **The percentages** are the zoom steps (`ZOOM_STEPS`) between 25 % and 1600 %, without 100 %
  (Actual size covers it). Every percentage is always offered, also one below the current fit (the
  picture then shows smaller than the window, as Zoom out below Fit already allows).

## 4. Behaviour details

- **One update path.** The bar gets one call that sets the zoom button's text and the enable state
  of the three controls (zoom text, can zoom out, can zoom in, available; the values from
  `MainFrame::isAvailable`). `MainFrame::showZoom` calls it; `showZoom` already runs on every view
  change, on every viewport resize and on stream changes, and it is also called from the session
  status handler, since `isAvailable` depends on the session's stream state. The bar's status update
  (`TransportBar::showStatus`, which enables the icon buttons from "has a stream" on every status
  notification) leaves these three controls alone, so it never re-enables + at the limit.
- **The menu is built by a function** (the current view in, the `wxMenu` with its checked entry
  out), and its entries run through the same paths as the View menu (Fit, Actual size and Resize
  through `MainFrame::applyViewCommand` / `execute`; a percentage through `zoomAround` at the
  viewport's centre).
- **Status bar fields:** removing the zoom field renumbers the fixed fields (mapping, Log notice)
  and their widths; the selftest steps that read the zoom from the status bar read it from the zoom
  button instead.
- Accessibility: each control has an accessible name (Zoom out, Zoom, Zoom in); the zoom button's
  name includes the current zoom.

## 5. Tests

- Selftests (synthetic events, as the existing bar and view steps):
  - after Actual size the zoom button reads "100 %"; in Fit it starts with "Fit (";
  - the − and + buttons zoom one step and are disabled at the limits;
  - + stays disabled at 3200 % across a status notification (playback running);
  - the menu is checked through the function that builds it (a pop-up menu cannot be driven by a
    selftest: it runs a modal loop): Fit checked in Fit, Actual size at 100 %, 400 % at 400 %,
    nothing at 3200 %; each entry's id sent as a `wxEVT_MENU` without showing the menu sets the
    expected view (Fit, Actual size, 400 %, Resize window to actual size);
  - without a clip the three are disabled and the button reads "—";
  - the status bar has no zoom field, and the bar's minimum width is not larger than the status
    bar's.
- Existing steps that read the zoom from the status bar read it from the zoom button instead.

## 6. Docs

README: the control bar's description gains the zoom buttons; the status bar list loses the zoom
field.
