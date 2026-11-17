# rawreel 0.11.0

rawreel is a player for raw and lossless video on the GPU (Vulkan). Version 0.11.0 brings view
zoom: Fit to window, Actual size, zoom in and out from 12.5 % to 3200 %, and a window sized to the
clip on every open. The picture can be dragged, scrolled and pinched, and the control bar has zoom
buttons. On Windows, the crash handler copes better with a stack overflow. It plays the same
sources as 0.10.1.

rawreel 0.11.0 runs on Windows, on Linux (Ubuntu 24.04, X11 or XWayland) and on macOS 27 or newer
on Apple Silicon.

## View zoom

- 100 % is one clip pixel per physical screen pixel. On a 2x Retina display a 1920×1080 clip
  covers 960×540 points.
- Fit scales the picture to the viewport and follows the window when it is resized. Any other
  zoom keeps its scale when the window is resized; the picture is centred on an axis where it is
  smaller than the viewport, and never pans past its edge on an axis where it is larger.
- The View menu has five new commands, before Format…, available while a stream is open:

  | Command (macOS) | Command (Windows, Linux) | macOS | Windows, Linux |
  | --- | --- | --- | --- |
  | Zoom to Fit | Fit to window | ⌘9 | Ctrl+0 |
  | Actual Size | Actual size | ⌘0 | Ctrl+1 |
  | Zoom In | Zoom in | ⌘+ | Ctrl++ |
  | Zoom Out | Zoom out | ⌘− | Ctrl+- |
  | Resize Window to Actual Size | Resize window to actual size | ⌘1 | Alt+1 |

- Zoom in and Zoom out move through 12.5, 25, 50, 100, 200, 400, 800, 1600 and 3200 %, around
  the viewport's centre. The fit zoom is an extra stop: a step that crosses it stops there and
  sets Fit. A fit within 1 % of a step counts as that step. The range is 12.5 % to 3200 %; a fit
  outside it widens the range to the fit. Zoom in is disabled at the maximum, Zoom out at the
  minimum.
- Resize window to actual size sizes the window so the viewport is the clip at 100 %, and shows
  the clip at 100 %. For a clip too large for the screen, each side of the window is clamped to
  the screen and the clip is panned to its centre. In a maximised (Windows, Linux) or full-screen
  (macOS) window it acts as Actual size.

## Window size on open

- Every open sizes the window: Open…, Open URI…, a drop, Finder or the Dock (macOS), Open
  Recent, the command line and View > Format….
- A clip that fits on the screen at 100 %: the viewport becomes exactly the clip at 100 %. The
  window grows or shrinks, keeps its top-left corner and moves only as far as it must to stay on
  the screen. The clip shows in Fit, so the picture follows the window when it is resized.
- A clip that does not fit: the window becomes the largest window that keeps the clip's shape and
  fits on the screen, centred on the screen, and the clip shows in Fit.
- "The screen" is the work area (without the menu bar, Dock or taskbar) of the display the window
  is on.
- A clip smaller than the minimum window on both axes shows at 100 %, centred, with background
  around it. A maximised (Windows, Linux) or full-screen (macOS) window keeps its size; the clip
  shows at 100 % when the viewport is larger than the clip on both axes, otherwise in Fit.
- View > Restart GPU and reopen keeps the window and the view. File > Close stream keeps the
  window size and returns the view to Fit.

## Drag, scroll and pinch

- Drag with the left or the middle button to pan a picture larger than the viewport. During the
  drag the cursor is the closed hand on macOS and the move cursor on Windows and Linux.
- The mouse wheel and two-finger scrolling pan, horizontal wheels included. A wheel set to scroll
  by pages moves one viewport page.
- On Windows and Linux, Ctrl+wheel zooms one step per notch at the pointer; partial touchpad
  steps add up to a notch. A Windows precision touchpad sends a pinch as Ctrl+wheel, so it moves
  through the zoom steps.
- A pinch that reaches rawreel as a pinch gesture (a trackpad pinch on macOS) zooms smoothly at
  the fingers.
- On macOS scrolling always pans, with or without ⌘, as in Preview and QuickTime Player. Pinch
  and the keys zoom.

## Zoom buttons

- The control bar has − [zoom ▾] + before the "i" button (the mark is ▼ on Windows).
- − and + run Zoom out and Zoom in as the View menu does, and are disabled when those are. Their
  tooltips name the key.
- The zoom button shows the current zoom ("Fit (37.5 %)", "200 %", "—" without a clip). A click
  opens a menu: Fit to window, Actual size, 25 %, 50 %, 200 %, 400 %, 800 %, 1600 % and Resize
  window to actual size (on macOS: Zoom to Fit, Actual Size and Resize Window to Actual Size), with
  the current view checked. A percentage zooms around the viewport's centre.
- The playback buttons stay centred on the bar.

## Windows

- The crash handler now has 64 KB of stack reserved for a stack overflow on the main thread, and
  it ends the process itself after the dump, with the exception code (0xC00000FD for a stack
  overflow). Before, a stack overflow could end with an access violation, with or without a dump.
- The Loop and "i" toggles are as wide as the other icon buttons, so the playback buttons are
  centred on the bar.

## Other changes

- On Windows, rawreel takes its user data folder from the `APPDATA` environment variable, or
  `%USERPROFILE%\AppData\Roaming` when `APPDATA` is not set or not an absolute path, instead of
  asking Windows for the known folder. In a normal logon both are the same folder, so
  `rawreel.json`, the logs and the crash dumps stay where they were. When `APPDATA` differs from
  the known folder (for example in a process started with `runas /noprofile`), rawreel now uses
  `APPDATA`.
- The dvv libraries move to dvv-build 0.4.2 and dvv-base 0.4.1; the others stay as in 0.10.1. The
  shipped third-party files (FFmpeg, wxWidgets, the Vulkan loader and KosmicKrisp) are the same
  as in 0.10.1.

## Downloads

- `rawreel-0.11.0-windows-x64.zip`: the application for Windows. Unzip it anywhere, keep all its
  files together, and start `rawreel-gui.exe`.
- `rawreel-0.11.0-windows-x64-symbols.zip`: debug symbols for crash dumps.
- `rawreel-0.11.0-linux-x64.tar.xz`: the application for Linux x86-64. Extract it anywhere
  (`tar xf rawreel-0.11.0-linux-x64.tar.xz`), keep all its files together, and start
  `./rawreel-gui` in the extracted folder.
- `rawreel-0.11.0-linux-x64-symbols.tar.xz`: debug symbols for core dumps.
- `rawreel-0.11.0-macos-arm64.dmg`: the application for Macs with Apple Silicon. Open it, drag
  `rawreel.app` to Applications (or any folder) and open it. The app is not notarized, so the
  first start shows "Apple could not verify "rawreel" is free of malware…". To allow it, choose
  **Done**, then System Settings → Privacy & Security → **Open Anyway** (next to "rawreel was
  blocked…") and confirm; macOS remembers the choice for that copy. Or, in Terminal:
  `xattr -dr com.apple.quarantine /Applications/rawreel.app`.
- `rawreel-0.11.0-macos-arm64-symbols.zip`: debug symbols (`.dSYM`) for macOS crash reports.
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
- Views below 12.5 % undersample, so very small views of very large frames can alias.
- A clip smaller than the minimum window on both axes shows at 100 % with background around it;
  smaller on one axis only, it opens in Fit with background bars on the other.
- rawreel does not show pixel values (no readout under the pointer, no magnifier).
- macOS: trackpad scroll steps arrive rounded to whole points, so a very slow two-finger pan can
  stall.
- Linux: a touchpad pinch usually does not reach rawreel on X11; Ctrl+scroll zooms there.
- FFV1 decoding is CPU-bound, so 4K FFV1 does not reach 60 fps: about 50 fps on a typical 8-core
  desktop CPU for easy content, far less for noisy content. Raw and Y4M files play 4K at 60 fps.
- Skimming shows the ghost thumb (the position a click moves to) on macOS only; on Windows and
  Linux the preview works without it.
- On Windows and Linux, while the Info window has the focus, keyboard shortcuts other than Ctrl+I
  do nothing.
- The zip is not code-signed, so Windows SmartScreen may warn at the first start.
- Windows: a stack overflow on a thread other than the main thread can end without a crash dump
  (issue #40).
- On Linux, `rawreel-gui` uses X11; on a Wayland desktop it runs through XWayland.
- macOS: Apple Silicon and macOS 27 or newer only. The app is signed ad hoc, not with a Developer
  ID, and not notarized.
- macOS: rawreel pins only the Vulkan driver; Vulkan implicit layers installed system-wide (for
  example by a Vulkan SDK) can still load into it.
- macOS: the release workflow's Mac has no usable GPU, so the GPU, display and GUI checks of the
  macOS package run on a development Mac, not in the release workflow.

## Bug reports

On Windows a crash writes a minidump to `%APPDATA%\rawreel\crashes\` (a stack overflow outside
the main thread may not; see the known limits). Attach it and the log to the report: the newest
`rawreel-*.log` in `%APPDATA%\rawreel\` for `rawreel-gui`, the console output (with its
`rawreel: crash dump <path>` line) for `rawreel-cli`. The maintainer reads the dump with the
symbols zip.

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
