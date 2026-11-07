# rawreel 0.10.0

rawreel is a player for raw and lossless video on the GPU (Vulkan). Version 0.10.0 adds macOS on
Apple Silicon: `rawreel.app`, in a `.dmg`, plays everything rawreel 0.9.0 plays on Windows and
Linux (raw, Y4M and lossless container files, `lavfi:` graphs, custom formats, clip descriptions,
colour by standard with PQ and HLG tone mapping), and `rawreel-cli` comes with it.

rawreel 0.10.0 runs on Windows, on Linux (Ubuntu 24.04, X11 or XWayland) and on macOS 27 or newer
on Apple Silicon.

## macOS on Apple Silicon

- `rawreel-gui` is a native Mac app (Cocoa): the application menu has About rawreel and Quit
  rawreel (⌘Q), the usual ⌘ shortcuts work, and files open from Finder (Open With offers rawreel
  for raw, Y4M and video files; or a drop on the Dock icon) as well as by a drop on the window.
  Quit closes the window the same way as its close button.
- The picture is sharp on Retina displays and follows a change of the display scale, for example
  when the window moves between a Retina and a non-Retina display.
- The GPU work runs on the Mac's GPU through Vulkan 1.4 on KosmicKrisp, Mesa's Vulkan driver on
  Metal, as the LunarG Vulkan SDK 1.4.363.1 ships it. The app bundles the driver and the Vulkan
  loader 1.4.365, so nothing else is installed. rawreel uses the bundled driver even when a Vulkan
  SDK (with MoltenVK) is installed; a `VK_DRIVER_FILES` you set yourself is respected.
  `rawreel-cli --version` and the log name the driver (`driver KosmicKrisp ...`).
- FFmpeg: the app bundles custom_ffmpeg 1.0.1 (FFmpeg n8.1.3, LGPL), found inside the app.
- Configuration and log: `~/Library/Application Support/rawreel/` (`rawreel.json`,
  `rawreel.log`); an app bundle is read-only, so the config is never looked up next to the
  executable there.
- The app is signed ad hoc only: it has no Apple Developer ID signature and is not notarized.
- `rawreel-cli` is `rawreel.app/Contents/MacOS/rawreel-cli`. A symbolic link to it (e.g. in
  `/usr/local/bin`) works: it resolves its own path and uses the app's FFmpeg and Vulkan driver.

## Other changes

- The Vulkan line of `rawreel-cli --version` (and of About) names the driver of the device:
  `Vulkan: <device>, API <version>, driver <driver name> <driver info>`. `rawreel-gui` logs one
  line, `Vulkan device: <device>, driver <driver>`, for the device it uses, instead of one line
  per device.
- The `--config` help of `rawreel-gui` and `rawreel-cli` says "instead of the default
  rawreel.json": the default is next to the executable on Windows and Linux, and in Application
  Support for the macOS app.
- The dvv libraries are built and tested on macOS: dvv-build 0.2.0, dvv-base 0.2.0, dvv-registry
  0.1.1, dvv-platform 0.2.0, dvv-gpu 0.2.0, dvv-media 0.1.1 and dvv-components 0.2.0. The macOS
  third-party prefix adds custom_ffmpeg 1.0.1 (Windows and Linux stay on custom_ffmpeg 1.0.0),
  wxWidgets 3.3.4 on Cocoa, the Vulkan loader built from source and KosmicKrisp. The Windows and
  Linux packages are built as in 0.9.0.
- The release no longer has the third-party sources archive of 0.9.0. Instead, each package's
  `SOURCE.txt` points to the tag of the custom_ffmpeg release its FFmpeg libraries come from, as
  their complete source: v1.0.0 for Windows and Linux, v1.0.1 for macOS.

## Downloads

- `rawreel-0.10.0-windows-x64.zip`: the application for Windows. Unzip it anywhere, keep all its
  files together, and start `rawreel-gui.exe`.
- `rawreel-0.10.0-windows-x64-symbols.zip`: debug symbols for crash dumps.
- `rawreel-0.10.0-linux-x64.tar.xz`: the application for Linux x86-64. Extract it anywhere
  (`tar xf rawreel-0.10.0-linux-x64.tar.xz`), keep all its files together, and start
  `./rawreel-gui` in the extracted folder.
- `rawreel-0.10.0-linux-x64-symbols.tar.xz`: debug symbols for core dumps.
- `rawreel-0.10.0-macos-arm64.dmg`: the application for Macs with Apple Silicon. Open it, drag
  `rawreel.app` to Applications (or any folder) and open it.
- `rawreel-0.10.0-macos-arm64-symbols.zip`: debug symbols (`.dSYM`) for macOS crash reports.
- `SHA256SUMS`: the checksums.

To verify a download, compare `(Get-FileHash <file> -Algorithm SHA256).Hash` with its line in
`SHA256SUMS` (or run `sha256sum -c --ignore-missing SHA256SUMS` on Linux and macOS), and check its
build attestation with `gh attestation verify <file> --repo BarabashkaD/rawreel`.

## Requirements

Windows: Windows 10 or newer, 64-bit, with a GPU driver that supports Vulkan 1.4, and the
Microsoft Visual C++ Redistributable (x64) 14.51 or newer, which the zip does not include:
install Microsoft's
latest one from https://aka.ms/vc14/vc_redist.x64.exe. "MSVCP140.dll was not found" at the start
means it is missing or too old.

Linux: Ubuntu 24.04 (x86-64) with its current updates (glibc 2.39); an X11 desktop, or a Wayland
desktop with XWayland; GTK 3; the Vulkan loader `libvulkan1`; and a Vulkan 1.4 driver (Mesa from
`noble-updates` or the GPU vendor's driver). Install the system packages with
`sudo apt install libgtk-3-0t64 libsm6 libice6 libvulkan1 mesa-vulkan-drivers`.

macOS: a Mac with Apple Silicon and macOS 27 or newer. The app brings its Vulkan loader, its
Vulkan driver and FFmpeg; nothing else is needed. Intel Macs are not supported.

## FFmpeg in the package

The Windows zip and the Linux tar.xz include the FFmpeg libraries avutil, avcodec, avformat,
avfilter, swscale and swresample of custom_ffmpeg 1.0.0 (FFmpeg n8.1.3, LGPL 2.1 or later) in
`ffmpeg/`; the macOS app includes the same libraries of custom_ffmpeg 1.0.1 (FFmpeg n8.1.3; 1.0.1
lets the macOS libraries find each other in any folder) in
`rawreel.app/Contents/Frameworks/ffmpeg/`.
rawreel loads them at run time and is not linked with them; you may replace them with your own
LGPL shared build of FFmpeg 8.1 or a newer 8.x with the same file names, or delete the folder.
Each library must have the major version rawreel was built with and at least its minor version
(avutil 60.26, avcodec 62.28, avformat 62.12, avfilter 11.14, swscale 9.5, swresample 6.3); an
older one, a GPL build or a non-free build is refused. On macOS, sign each library you put in and
then the app again (`codesign --force --sign - <library>`, then
`codesign --force --sign - rawreel.app`); after deleting the folder, sign only the app. Their
complete source of FFmpeg is the tag of that custom_ffmpeg release, with its submodules and build
scripts:
https://github.com/BarabashkaD/custom_ffmpeg/tree/v1.0.0 for the Windows and Linux packages and
https://github.com/BarabashkaD/custom_ffmpeg/tree/v1.0.1 for the macOS app. The tag does not hold
the source of LAME 3.100 (built into avcodec): that is LAME's own source release,
`lame-3.100.tar.gz`, whose URL and SHA256 `SOURCE.txt` prints. `SOURCE.txt` (in `ffmpeg/`, on
macOS at the top of the `.dmg`) also names the tag. These libraries contain decoders for
codecs such as H.264, HEVC and AAC, which may be covered by patents in some countries; licensing
them is a matter for the user and the distributor. rawreel itself opens only lossless video
codecs. This is not legal advice.

## Known limits

- Only lossless video codecs open (`rawvideo`, `v210`, `v410`, `r210`, `ffv1`, `huffyuv`,
  `ffvhuff`, `utvideo`, `magicyuv`, `png`, `qtrle`); lossy codecs are refused at open.
- A decoder output format outside the pixel-format catalogue is refused; the message names it.
- A damaged frame of a container file (the decoder reports an error) and every frame after it up
  to the next key frame give the error "frame N is damaged" instead of a picture; from the next
  key frame on, the frames are exact again.
- The sample aspect ratio is ignored, so anamorphic files show stretched.
- A step or seek backwards on a stream without usable timestamps or a `lavfi:` graph decodes
  again from the start.
- A `lavfi:` graph with random colours (`gradients` without a fixed seed, or `c=random`) shows
  different frames after a seek back.
- MKV and NUT files are scanned when they open, so large ones open more slowly.
- Very small views of very large frames (below 1:8) can alias.
- FFV1 decoding is CPU-bound, so 4K FFV1 does not reach 60 fps: about 50 fps on a typical 8-core
  desktop CPU for easy content, far less for noisy content. Raw and Y4M files play 4K at 60 fps.
- The zip is not code-signed, so Windows SmartScreen may warn at the first start.
- On Linux, `rawreel-gui` uses X11; on a Wayland desktop it runs through XWayland.
- macOS: Apple Silicon and macOS 27 or newer only. The app is signed ad hoc, not with a Developer
  ID, and not notarized.
- macOS: rawreel pins only the Vulkan driver; Vulkan implicit layers installed system-wide (for
  example by a Vulkan SDK) can still load into it.
- macOS: the release workflow's Mac has no usable GPU, so the GPU, display and GUI checks of the
  macOS package run on a development Mac, not in the release workflow.

## Bug reports

On Windows a crash writes a minidump to `%APPDATA%\rawreel\crashes\`. Attach it and the log to the
report: `%APPDATA%\rawreel\rawreel.log` for `rawreel-gui`, the console output (with its
`rawreel: crash dump <path>` line) for `rawreel-cli`. The maintainer reads the dump with the
symbols zip.

On Linux rawreel writes no crash report; attach a core dump (`ulimit -c unlimited` before the
start) and the log, `~/.local/share/rawreel/rawreel.log` for `rawreel-gui`. The README says where
the core goes. The maintainer reads it with the Linux symbols tar.xz.

On macOS rawreel has no crash handler; the system writes a crash report to
`~/Library/Logs/DiagnosticReports/`. Attach it, the log
(`~/Library/Application Support/rawreel/rawreel.log` for `rawreel-gui`) and the output of
`rawreel-cli --version`. The maintainer reads the report with the macOS symbols zip.

## Licence

rawreel is MIT-licensed. `THIRD_PARTY_NOTICES.txt` in each package lists the licences of the
third-party code it ships. The FFmpeg libraries are LGPL; they include LAME
(`LGPL-2.0-or-later`) and SVT-AV1 (`BSD-3-Clause-Clear`). wxWidgets includes code under the
`libpng-2.0`, `IJG` and `X11` licences. The macOS app also ships the Vulkan loader
(`Apache-2.0 AND MIT`) and KosmicKrisp (Mesa, `MIT` with parts under `BSD-3-Clause`,
`BSD-2-Clause`, `X11` and BLAKE3's `CC0-1.0 OR Apache-2.0 OR Apache-2.0 WITH LLVM-exception`).
