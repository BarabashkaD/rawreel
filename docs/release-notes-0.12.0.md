# rawreel 0.12.0

rawreel is a player for raw and lossless video on the GPU (Vulkan). Version 0.12.0 writes a crash
log on every OS and stops busy sources at once when a stream closes. It plays the same sources as
0.11.0.

## Changes

- A crash of rawreel-gui writes its last log lines to `<name>-<date>-<time>-<pid>.dvvlog` in the
  crashes folder of rawreel's data folder, on Windows, macOS and Linux; a crash of rawreel-cli
  writes the same file, which names the crash. Windows also writes the minidump beside it, also
  when the program aborts. The next start shows the earlier crash's lines in the Log. macOS still
  writes its own crash report, and Linux its core dump where enabled.
- Closing a stream, opening another or quitting stops an open or a frame read that is still
  running, instead of waiting for it.
- When no source can open a file, the message names each source that is not available and why,
  for example FFmpeg that was not found.
- A "no source can open" error ends with the FFmpeg status line only when an FFmpeg source would
  have handled the request: a file or a `lavfi:` URI while FFmpeg is missing, and a `lavfi:` URI
  while FFmpeg is present but its lavfi part is off. A URI of any other scheme gets no FFmpeg line.
- The Format… dialog reads size, format and frame rate from a file name that follows
  `<name>_WxH[_format][_fps][_colour]`; for any other name it takes the first WxH, as before. The
  frame rate field accepts up to 9 decimals.
- The size fields accept 1 to 65535.
- The Log lists each loaded module at start.
- A failed save of the settings names the step that failed.
- On Linux, a folder or file name that is not valid UTF-8 shows U+FFFD in messages; it still
  opens.
- Windows: rawreel-gui stops with an error for an argument that is not valid Unicode.
- Windows: the minimum Microsoft Visual C++ Redistributable (x64) is 14.52 (0.11.0: 14.51),
  because the build tools moved to MSVC 14.52.
- Build: dvv-build 0.5.1, dvv-base 0.5.0, dvv-registry 0.2.0, dvv-platform 0.3.0, dvv-gpu 0.3.0,
  dvv-media 0.2.0 and dvv-components 0.3.0.

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
