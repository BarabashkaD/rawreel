# rawreel 0.15.0

rawreel is a player for raw and lossless video on the GPU (Vulkan). Version 0.15.0 shows the
alpha of a clip whose pixel format has an alpha component, over a checkerboard. It plays the same
sources as 0.14.0 and behaves the same, except as listed below.

## New

- A, View → Alpha or the Alpha button on the control bar (left of Zoom out) turns the alpha on and
  off. View → Alpha is a check item with a check mark on every OS, macOS included; the picture's
  context menu has the same item, between Rulers and Grid and Rulers….
- With the alpha on, the picture is blended by its alpha over a checkerboard of 8 pt squares, in
  light or dark greys by the appearance. The squares keep their size on the screen at every zoom,
  and the checkerboard starts at the picture's top-left corner, so it moves with the picture when
  you pan. With the alpha off, the alpha is ignored and the picture is opaque, as in 0.14.0.
- The alpha is off at every start and is not saved in `rawreel.json`; opening another clip keeps it
  on or off. For a clip without alpha the menu item and the button are disabled and A does
  nothing.
- The catalogue formats with alpha are rgba, bgra, argb and rgba64le. A custom format (Load JSON…
  or `fmt=@<file.json>`) has alpha when it has an A component.

## Changes

- Build: dvv-build 0.6.0, dvv-base 0.6.0, dvv-registry 0.3.0, dvv-platform 0.4.0, dvv-gpu 0.6.0,
  dvv-media 0.6.0 and dvv-components 0.7.0. Requires dvv-media 0.6.0 and dvv-components 0.7.0.

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
