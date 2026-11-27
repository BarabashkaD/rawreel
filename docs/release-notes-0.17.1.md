# rawreel 0.17.1

rawreel is a player for raw and lossless video on the GPU (Vulkan). Version 0.17.1 fixes the Zoom
window and the pipette in the Planes layout, and changes how far the Zoom window zooms out. It
plays the same sources as 0.17.0 and behaves the same, except as listed below.

## Fixes

- In Planes the main picture's outline shows the region of one panel, and a click in any panel
  pins the pixel under it.
- In Planes the Zoom view centres on (Follow) and frames (Cell) the pixel under the pipette again.
- A layout change or a resize of the Zoom window re-frames the shown pixel in Follow and Cell (a
  hand pan returns to it); so do the rulers and a display scale change. Closing and reopening the
  Zoom window keeps a hand pan.

## Changes

- The Zoom window no longer zooms out past 3 x 3 major grid cells per panel (the major grid
  step, shown or not) and never shows the whole picture. A clip of at most 3 x 3 major cells can
  still be shown whole.
- The Zoom window marks the pixel it shows (the pin, or the probe in Live) in every panel; in a
  chroma panel it marks the sample's cell. Numbers, labels and corner flags stay visible over the
  mark, also over flagged neighbours.
- A clip of another size opens centred on the kept pin.
- Build: the same dvv versions as 0.17.0 (dvv-build 0.6.1, dvv-base 0.6.0, dvv-registry 0.3.0,
  dvv-platform 0.4.0, dvv-gpu 0.7.0, dvv-media 0.8.0 and dvv-components 0.9.0); no library change.

## Requirements

Windows: Windows 10 or newer, 64-bit, with a GPU driver that supports Vulkan 1.4, and the
Microsoft Visual C++ Redistributable (x64) 14.52 or newer, which the zip does not include.
"MSVCP140.dll was not found" at the start means it is missing or too old.

Linux: Ubuntu 24.04 (x86-64) with its current updates (glibc 2.39); an X11 desktop, or a Wayland
desktop with XWayland; GTK 3; the Vulkan loader `libvulkan1`; and a Vulkan 1.4 driver (Mesa from
`noble-updates` or the GPU vendor's driver). Install the system packages with
`sudo apt install libgtk-3-0t64 libsm6 libice6 libvulkan1 mesa-vulkan-drivers`.

macOS: a Mac with Apple Silicon and macOS 27 or newer. The app brings its Vulkan loader, its
Vulkan driver and FFmpeg; nothing else is needed. Intel Macs are not supported.
