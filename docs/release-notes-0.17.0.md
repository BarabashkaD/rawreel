# rawreel 0.17.0

rawreel is a player for raw and lossless video on the GPU (Vulkan). Version 0.17.0 adds a pixel
inspector: the exact stored values of one pixel, its raw words with their padding bits, and a
check for padding that is not 0. It plays the same sources as 0.16.0 and behaves the same, except
as listed below.

## New

- P, View → Pipette or the control bar's Pipette toggle turns the pipette on; a click on the
  picture pins that image pixel. P or Esc turns it off; a drag still pans. The main picture marks
  the pin in the cursor colour and outlines, dashed, the region the Zoom window shows. View →
  Clear Pin removes the pin; a new clip that does not contain it removes it too.
- Shift+P or View → Live Probe shows the pixel under the cursor instead of the pin, in both windows.
- The Zoom window (View → Zoom Window; on macOS Show Zoom Window / Hide Zoom Window) opens with the
  first pin, without taking the focus. It has rulers, the main window's grid and two layouts:
  Structure (S), one grid of image pixels, chroma blocks outlined with their U and V once; Planes
  (S), one panel per component at the same region and scale. Follow (F) keeps the pin centred;
  Cell (F) shows the largest grid cell that holds the pin. The zoom is a whole number of screen
  pixels per image pixel, 1 to 256; Numbers fit (Actual Size) and Fit cell (Zoom to Fit) set it.
  Arrows move the pin by one pixel, Shift+arrows by one chroma block, C centres on the pin.
- The header, the status bar and a raw-word line per component show the code values and the
  stored words in hex and binary, value and padding bits marked, read from the GPU's copy of the
  shown frame every frame. ⌘C (Ctrl+C on Windows and Linux) or View → Copy Pixel Values copies the
  values and the raw words.
- X, View → Padding Check or the control bar's Padding toggle marks in orange every pixel whose
  samples carry padding that is not 0, and the status bar counts those samples in the shown frame.
  In the Zoom window such cells and padding bits are orange, and the header names the component.
- The Zoom window's place, size, layout and mode and the padding check are saved in
  `rawreel.json` (`gui.zoomWindow.*`, `gui.paddingCheck`). The pin is not saved.
- With the padding check off and the Zoom window closed, the inspector costs nothing.
- A GPU device loss with the Zoom window shown keeps the pin; Restart GPU brings both windows back.

## Changes

- Build: dvv-build 0.6.1, dvv-base 0.6.0, dvv-registry 0.3.0, dvv-platform 0.4.0, dvv-gpu 0.7.0,
  dvv-media 0.8.0 and dvv-components 0.9.0. Requires dvv-gpu 0.7.0, dvv-media 0.8.0,
  dvv-components 0.9.0 and dvv-build 0.6.1.

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
