# rawreel 0.16.0

rawreel is a player for raw and lossless video on the GPU (Vulkan). Version 0.16.0 can hide each
of a clip's three colour components (planes). It plays the same sources as 0.15.0 and behaves the
same, except as listed below.

## New

- 1, 2 and 3, three plane buttons on the control bar (left of Alpha) and three View menu items
  show and hide the first, second and third colour component, in any combination, during playback
  and in pause. The buttons show the clip's letters: Y, U and V for a YUV or gray clip, R, G and B
  for an RGB clip. The menu items are titled the same way: Y Plane, U Plane, V Plane on macOS, Y
  plane, U plane, V plane on Windows and Linux. They are check items on every OS, macOS included.
- The View menu has, after Grid and Rulers…, the three plane items, Hidden U and V at 0, then
  Alpha. The picture's context menu has the three plane items between Rulers and Alpha.
- A hidden component's code value is replaced before the colour matrix: Y, R, G and B by 0; U and
  V by the mid code 2^(bits−1) (128 at 8 bits), or by 0 while View → Hidden U and V at 0 is
  checked. The picture shows what the matrix makes of the rest:
  - U only or V only (Y hidden) is mostly black: Y at 0 lies below black, so only pixels whose
    chroma pushes a channel above 0 light up.
  - Y only with U and V at 0 is green; with U and V at the mid code it is the grey luma picture.
  - An RGB clip's hidden component is 0. A YUV clip that carries RGB (its colour matrix is RGB)
    shows the letters G, B and R, the order of its planes, and hides each of them to 0.
  - All planes hidden is black (with Alpha on, black over the checkerboard where the clip is
    opaque).
  - With every plane shown, the picture is the same as in 0.15.0.
- Without a clip the plane items and buttons are disabled; for a gray clip U and V are disabled.
  Hidden U and V at 0 is enabled only for a YUV clip with chroma (not gray, not RGB in YUV).
- All planes are shown and Hidden U and V at 0 is off at every start; neither is saved in
  `rawreel.json`. Opening another clip keeps them, by position.

## Changes

- Build: dvv-build 0.6.0, dvv-base 0.6.0, dvv-registry 0.3.0, dvv-platform 0.4.0, dvv-gpu 0.6.0,
  dvv-media 0.7.0 and dvv-components 0.8.0. Requires dvv-media 0.7.0 and dvv-components 0.8.0.
- The playback buttons are centred on the control bar while they fit between its sides; in a
  narrower window they move toward the narrower side just enough not to overlap it.
- The Log pane docked at a side of the window is at least 120 pt wide.

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
