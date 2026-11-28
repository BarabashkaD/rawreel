# rawreel 0.17.2

rawreel is a player for raw and lossless video on the GPU (Vulkan). Version 0.17.2 fixes the width
of the control bar's toggle buttons on macOS and Linux, and builds the Linux package in the image
CI tests in. It plays the same sources as 0.17.1 and behaves the same, except as listed below.

## Fixes

- On macOS and Linux the control bar's toggle buttons (Loop, Alpha, Pipette, Padding, Grid, Info
  and the plane toggles) are as wide as the other icon buttons, as on Windows.

## Changes

- The Linux package is built in the same Ubuntu 24.04 image CI tests in (pinned by digest), so the
  released binary uses the same library versions CI tested.
- Build: dvv-build 0.6.2; the other dvv versions are the same as in 0.17.1 (dvv-base 0.6.0,
  dvv-registry 0.3.0, dvv-platform 0.4.0, dvv-gpu 0.7.0, dvv-media 0.8.0 and dvv-components
  0.9.0); no library change.

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
