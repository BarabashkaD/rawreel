# rawreel 0.13.0

rawreel is a player for raw and lossless video on the GPU (Vulkan). Version 0.13.0 is a clean-up
release on new versions of the dvv libraries. It plays the same sources as 0.12.0 and behaves the
same, except as listed below.

## Changes

- Save Log writes the log to a temporary file next to the target and then replaces the target, so
  a failed save leaves the old file whole. The target's folder must be writable. A target that is
  a link or a folder is refused. A file with other hard links is replaced by a new file; its other
  names keep the old contents.
- On macOS and Linux, Save Log replaces a read-only target when its folder is writable. The new
  file keeps the permission bits of the file it replaces, but not its ACL or extended attributes.
- On Windows, Save Log keeps the replaced file's attributes and ACL. It cannot replace a file that
  another program keeps open without allowing its deletion: after a few short retries the save
  fails.
- A failed save now reads "replacing '<file>' failed: <reason>".
- The list of `--json` error codes no longer names `ShaderCompile`; rawreel never reported it.
- Build: dvv-build 0.6.0, dvv-base 0.6.0, dvv-registry 0.3.0, dvv-platform 0.4.0, dvv-gpu 0.4.0,
  dvv-media 0.3.0 and dvv-components 0.4.0.

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
