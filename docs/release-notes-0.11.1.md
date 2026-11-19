# rawreel 0.11.1

rawreel is a player for raw and lossless video on the GPU (Vulkan). Version 0.11.1 has no
user-visible change from 0.11.0 except the Windows requirement below. It plays the same sources
as 0.11.0.

## Changes

- Windows: the minimum Microsoft Visual C++ Redistributable (x64) is 14.52 (0.11.0: 14.51),
  because the build tools moved to MSVC 14.52.
- Build: dvv-build 0.5.0 and dvv-base 0.5.0. No user-visible change.

## Requirements

Windows: Windows 10 or newer, 64-bit, with a GPU driver that supports Vulkan 1.4, and the
Microsoft Visual C++ Redistributable (x64) 14.52 or newer, which the zip does not include.
"MSVCP140.dll was not found" at the start means it is missing or too old.

The requirements for Linux and macOS are the same as in 0.11.0.
