# rawreel 0.10.1

rawreel is a player for raw and lossless video on the GPU (Vulkan). Version 0.10.1 brings a new
control panel with a timeline, a Log pane and a Settings window, File > Open Recent, an Info
window, the file name in the window title and the status bar, and the rawreel app icon on every
platform. It plays the same sources as 0.10.0.

rawreel 0.10.1 runs on Windows, on Linux (Ubuntu 24.04, X11 or XWayland) and on macOS 27 or newer
on Apple Silicon.

## Control panel

- The one-row text transport bar is now a compact two-row panel. The timeline has ticks, the
  first and last frame numbers, and the frame on screen above the thumb; while you drag or skim,
  a marker shows the frame you started from.
- Icon buttons with tooltips: Open and Format on the left, then first frame, step back, play,
  stop, step forward, last frame and loop in the middle, and an "i" toggle for the Info window at
  the right end. The buttons follow the open stream like the menu.
- Skimming: while paused, hold Option (macOS) or Ctrl (Windows, Linux) over the timeline to
  preview the frame under the pointer; a click moves there, and releasing the key or leaving the
  timeline returns to the current frame.
- On Windows, a click on the timeline's channel jumps to the clicked frame.
- The panel follows the player without polling, so an idle window costs nothing. After a seek
  the timeline never shows an older frame than the one you asked for.

## Window title, status bar and Info window

- The window title names the open file: on macOS the file name with its proxy icon, on Windows
  "name - rawreel", on Linux "name — rawreel"; "rawreel" when nothing is open.
- The status bar's first field shows the stream's name, cut in the middle when it is long; its
  tooltip is the full path (the URI for a source that is not a file).
- The Info window replaces the docked Info pane. It is a small floating window beside the main
  window (View > Info, Ctrl+I or ⌘I, or the "i" button) with Picture (resolution, frame rate,
  format) and Colour (standard, matrix, range, transfer, tone mapping). It opens without taking
  the focus, follows a change of stream and hides while the main window is minimised. Esc and
  Ctrl+I or ⌘I close it, the player keys still reach the player, and Copy copies its rows.

## Open Recent

- File > Open Recent lists the files and URIs opened last, newest first. Choosing one opens it
  again; opening an entry again moves it to the top. A raw file that needed the Format dialog
  asks for its format again.
- An entry is added only after its open succeeds, from every way of opening: Open..., a drop,
  Finder or the Dock, Open URI..., the command line and the Format dialog. An entry whose file is
  missing or not accessible is removed when opening it fails.
- The list keeps 5 entries by default, 1 to 10 in Settings > Files. It is stored in `rawreel.json`
  (`gui.recentSize`, `gui.recent`).

## Log pane, Settings and log files

- The Log pane is hidden by default. View > Show Log (macOS) or View > Log (Windows, Linux) shows
  it; its state is kept in `rawreel.json`. While it is hidden, the status bar counts the warnings
  and errors logged. Lines can be selected and copied; File > Save Log... and Save Selected
  Lines... write them to a file.
- The Settings window (rawreel > Settings… on macOS, File > Settings… on Windows, Edit >
  Preferences… on Linux) has the log level, the log file (on or off, and its folder), the system
  log (off by default), file reads and the size of Open Recent. A level chosen in Settings is
  saved and applies at once; the Log pane's context menu sets a level for this run only. Every
  level change writes a marker line (`===== log level changed: Info -> Debug (...) =====`).
- Log files are named by their creation time, `rawreel-YYYYMMDD-HHMMSS.log`, and split at 4 MiB.
  rawreel never deletes a log file; it logs one warning per run when the log folder passes 1 GiB.
  A `rawreel.log` from an earlier version stays where it is and is not written any more.
- `rawreel.json`: a `rawreel.json` next to the executable is still used when it exists (a
  portable setup); otherwise the file is in the user data folder (`%APPDATA%\rawreel\` on
  Windows, `~/.local/share/rawreel/` on Linux, `~/Library/Application Support/rawreel/` on macOS),
  where `rawreel-gui` creates it when a setting is saved or an open changes Open Recent. Saving
  keeps the other keys, their order and the file's permissions. Only the first running
  `rawreel-gui` saves settings.

## App icon

- rawreel has its own icon, an owl on a film reel, on every platform: the macOS app, the Windows
  `rawreel-gui.exe` and `rawreel-cli.exe` and their windows, and the Linux windows.
- Help > About shows the owl logo.

## Other changes

- The macOS app keeps its bundle identifier, `io.github.barabashkad.rawreel`. Builds from source
  now use `io.github.barabashkad.rawreel.dev`, so Spotlight and Open With never start a build
  from source in place of the installed app.
- The dvv libraries move to dvv-build 0.4.0, dvv-base 0.4.0 and dvv-gpu 0.2.1; the others stay as
  in 0.10.0. The shipped third-party files (FFmpeg, wxWidgets, the Vulkan loader and KosmicKrisp)
  are the same as in 0.10.0.

## Downloads

- `rawreel-0.10.1-windows-x64.zip`: the application for Windows. Unzip it anywhere, keep all its
  files together, and start `rawreel-gui.exe`.
- `rawreel-0.10.1-windows-x64-symbols.zip`: debug symbols for crash dumps.
- `rawreel-0.10.1-linux-x64.tar.xz`: the application for Linux x86-64. Extract it anywhere
  (`tar xf rawreel-0.10.1-linux-x64.tar.xz`), keep all its files together, and start
  `./rawreel-gui` in the extracted folder.
- `rawreel-0.10.1-linux-x64-symbols.tar.xz`: debug symbols for core dumps.
- `rawreel-0.10.1-macos-arm64.dmg`: the application for Macs with Apple Silicon. Open it, drag
  `rawreel.app` to Applications (or any folder) and open it. The app is not notarized, so the
  first start shows "Apple could not verify "rawreel" is free of malware…". To allow it, choose
  **Done**, then System Settings → Privacy & Security → **Open Anyway** (next to "rawreel was
  blocked…") and confirm; macOS remembers the choice for that copy. Or, in Terminal:
  `xattr -dr com.apple.quarantine /Applications/rawreel.app`.
- `rawreel-0.10.1-macos-arm64-symbols.zip`: debug symbols (`.dSYM`) for macOS crash reports.
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
- Skimming shows the ghost thumb (the position a click moves to) on macOS only; on Windows and
  Linux the preview works without it.
- On Windows and Linux, while the Info window has the focus, keyboard shortcuts other than Ctrl+I
  do nothing.
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
report: the newest `rawreel-*.log` in `%APPDATA%\rawreel\` for `rawreel-gui`, the console output
(with its `rawreel: crash dump <path>` line) for `rawreel-cli`. The maintainer reads the dump with
the symbols zip.

On Linux rawreel writes no crash report; attach a core dump (`ulimit -c unlimited` before the
start) and the log, the newest `rawreel-*.log` in `~/.local/share/rawreel/` for `rawreel-gui`.
The README says where the core goes. The maintainer reads it with the Linux symbols tar.xz.

On macOS rawreel has no crash handler; the system writes a crash report to
`~/Library/Logs/DiagnosticReports/`. Attach it, the log (the newest `rawreel-*.log` in
`~/Library/Application Support/rawreel/` for `rawreel-gui`) and the output of
`rawreel-cli --version`. The maintainer reads the report with the macOS symbols zip.

## Licence

rawreel is MIT-licensed. `THIRD_PARTY_NOTICES.txt` in each package lists the licences of the
third-party code it ships. The FFmpeg libraries are LGPL; they include LAME
(`LGPL-2.0-or-later`) and SVT-AV1 (`BSD-3-Clause-Clear`). wxWidgets includes code under the
`libpng-2.0`, `IJG` and `X11` licences. The control panel's icons are Lucide's (`ISC AND MIT`),
compiled into `rawreel-gui`. The macOS app also ships the Vulkan loader (`Apache-2.0 AND MIT`)
and KosmicKrisp (Mesa, `MIT` with parts under `BSD-3-Clause`, `BSD-2-Clause`, `X11` and BLAKE3's
`CC0-1.0 OR Apache-2.0 OR Apache-2.0 WITH LLVM-exception`).
