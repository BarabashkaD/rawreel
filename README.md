# rawreel

rawreel is a raw video player. This repository holds two programs: `rawreel-gui`, the
application, and `rawreel-cli`, a console tool that describes, dumps and benchmarks a source. They
are built on the dvv libraries, which this repository pins as git submodules in `libs/`:

| Repository | Content |
|---|---|
| [dvv-build](https://github.com/BarabashkaD/dvv-build) | presets, bootstrap, third-party superbuild and licence gate, privacy lint, CI workflow |
| [dvv-base](https://github.com/BarabashkaD/dvv-base) | errors, tasks and executors, logger, lifetime registry and leak check, JSON, URIs, paths |
| [dvv-registry](https://github.com/BarabashkaD/dvv-registry) | component registry, capabilities, parameter schemas, component kind check |
| [dvv-platform](https://github.com/BarabashkaD/dvv-platform) | native windows, Vulkan surfaces, dynamic libraries, `FileReader` |
| [dvv-gpu](https://github.com/BarabashkaD/dvv-gpu) | Vulkan 1.4 core: devices, buffers and images, submission, Slang shaders, compute pipelines, presentation |
| [dvv-media](https://github.com/BarabashkaD/dvv-media) | pixel-format catalogue, custom JSON formats, frames and the frame store, synthetic, raw and Y4M sources |
| [dvv-components](https://github.com/BarabashkaD/dvv-components) | `dvv_add_component()`, the FFmpeg run-time binding, the `ffmpeg` and `lavfi` sources |

## Packages

A release has these files (`<v>` is the version, e.g. `0.10.0`):

- `rawreel-<v>-windows-x64.zip`: the application. Unzip it anywhere and start `rawreel-gui.exe`;
  nothing is installed. The zip is not code-signed, so Windows SmartScreen may warn at the first
  start.
- `rawreel-<v>-windows-x64-symbols.zip`: the debug symbols (`rawreel-gui.pdb`, `rawreel-cli.pdb`)
  for crash dumps (see [Bug reports](#bug-reports)); not needed to run rawreel.
- `rawreel-<v>-linux-x64.tar.xz`: the application for Linux x86-64. Extract it anywhere
  (`tar xf rawreel-<v>-linux-x64.tar.xz`) and start `./rawreel-gui` in the extracted folder;
  nothing is installed.
- `rawreel-<v>-linux-x64-symbols.tar.xz`: the debug symbols (`rawreel-gui.debug`,
  `rawreel-cli.debug`) for core dumps (see [Bug reports](#bug-reports)); not needed to run rawreel.
- `rawreel-<v>-macos-arm64.dmg`: the application for Macs with Apple Silicon, `rawreel.app`
  (see [macOS](#macos) below).
- `rawreel-<v>-macos-arm64-symbols.zip`: the debug symbols (`rawreel-gui.dSYM`,
  `rawreel-cli.dSYM`) for crash reports (see [Bug reports](#bug-reports)); not needed to run
  rawreel.
- `SHA256SUMS`: the checksums of the files above (see [Verifying downloads](#verifying-downloads)).

The zip holds one folder, `rawreel-<v>/`, with `rawreel-gui.exe`, `rawreel-cli.exe`, `README.md`,
`LICENSE`, `THIRD_PARTY_NOTICES.txt` and `ffmpeg/` (the six FFmpeg libraries, their `licenses/`,
`build-info.txt` and `SOURCE.txt`). The Linux tar.xz holds the same folder with `rawreel-gui` and
`rawreel-cli` instead of the `.exe` files. Keep all files of a package together. It has no
`rawreel.json`; the built-in defaults apply (see [Configuration](#configuration)).

**Requirements (Windows):** Windows 10 or newer, 64-bit, with a GPU driver that supports Vulkan
1.4, and the Microsoft Visual C++ Redistributable (x64) 14.51 or newer, which the zip does not
include: install Microsoft's latest one from https://aka.ms/vc14/vc_redist.x64.exe. "MSVCP140.dll
was not found" at the start means it is missing or too old.

**Requirements (Linux):** Ubuntu 24.04 (x86-64) with its current updates (glibc 2.39); an X11
desktop, or a Wayland desktop with XWayland (Ubuntu's default);
GTK 3; the Vulkan loader `libvulkan1`; and a Vulkan 1.4 driver: Mesa from Ubuntu's updates
(`noble-updates`; the Mesa of the 24.04 release is too old) or the GPU vendor's driver. Install
the system packages rawreel needs with:

```sh
sudo apt install libgtk-3-0t64 libsm6 libice6 libvulkan1 mesa-vulkan-drivers
```

The tar.xz ships no C++ runtime and no Vulkan loader; it uses the system's. `rawreel-gui` uses
X11: on a Wayland desktop it runs through XWayland, so `DISPLAY` must be set (as it is in a
desktop session, and in WSL on Windows 11 with WSLg). Without a display it exits with 1 and "no
X11 display (DISPLAY is not set); rawreel-gui needs X11 or XWayland".

### macOS

**Requirements (macOS):** a Mac with Apple Silicon and macOS 27 or newer. Nothing else is needed:
the app brings its own Vulkan loader, its Vulkan driver (KosmicKrisp, Mesa's Vulkan driver on
Metal, as the LunarG Vulkan SDK ships it) and FFmpeg. Intel Macs are not supported.

The `.dmg` holds `rawreel.app`, `README.md`, `LICENSE`, `THIRD_PARTY_NOTICES.txt` and
`SOURCE.txt` (about the bundled FFmpeg). In the app, `Contents/MacOS/` has `rawreel-gui` (the
application) and `rawreel-cli`; `Contents/Frameworks/` has the Vulkan loader
(`libvulkan.1.dylib`), the KosmicKrisp driver and `ffmpeg/` (the six FFmpeg libraries);
`Contents/Resources/` has the driver's manifest and FFmpeg's `licenses/` and `build-info.txt`.
Open the `.dmg` and drag `rawreel.app` to `Applications` (or any folder); it also runs from the
opened `.dmg`. Nothing else is installed. The app has no `rawreel.json`; it keeps its
configuration and log in `~/Library/Application Support/rawreel/` (see
[Configuration](#configuration)).

The app is signed ad hoc only: it has no Apple Developer ID signature and is not notarized.

**rawreel-cli** is inside the app, `rawreel.app/Contents/MacOS/rawreel-cli`. A symbolic link to it
works: it resolves its own path, so it uses the app's FFmpeg, Vulkan loader and driver. A copy
outside the app does not find them.

## FFmpeg in the package

The Windows zip and the Linux tar.xz include the FFmpeg libraries avutil, avcodec, avformat,
avfilter, swscale and swresample of custom_ffmpeg 1.0.0 (FFmpeg n8.1.3, LGPL 2.1 or later) in
`ffmpeg/` (on Linux named `libavutil.so.60`, `libavcodec.so.62`, …). The macOS app includes the
same six libraries of custom_ffmpeg 1.0.1 (also FFmpeg n8.1.3; 1.0.1 lets the macOS libraries
find each other in any folder) in `rawreel.app/Contents/Frameworks/ffmpeg/`
(`libavutil.60.dylib`, `libavcodec.62.dylib`, …), with their `licenses/` and `build-info.txt` in
`rawreel.app/Contents/Resources/ffmpeg/`. rawreel loads them at run
time and is not linked with them; you may replace them with your own LGPL shared build of FFmpeg
8.1 or a newer 8.x with the same file names, or delete the folder. Each library must have
the major version rawreel was built with and at least its minor version (avutil 60.26, avcodec
62.28, avformat 62.12, avfilter 11.14, swscale 9.5, swresample 6.3); an older one, a GPL build or
a non-free build is refused. The complete source of FFmpeg is the tag of that custom_ffmpeg
release in its repository, with its submodules and build scripts:
https://github.com/BarabashkaD/custom_ffmpeg/tree/v1.0.0 for the Windows and Linux packages,
https://github.com/BarabashkaD/custom_ffmpeg/tree/v1.0.1 for the macOS app. The tag does not
hold the source of LAME 3.100, which is built into avcodec: that is LAME's own source release,
`lame-3.100.tar.gz`, whose URL (and SHA256, pinned in the tag's `versions.cmake`) the package's
`SOURCE.txt` prints. `SOURCE.txt` (in `ffmpeg/`, on macOS at the top of the `.dmg`) also names the
tag of its package. These
libraries contain decoders for codecs such as H.264, HEVC and AAC, which may be covered by
patents in some countries; licensing
them is a matter for the user and the distributor. rawreel itself opens only lossless video
codecs. This is not legal advice.

**Replacing FFmpeg on macOS.** The app's signature covers its libraries, and an unsigned library
does not load on Apple Silicon. After replacing libraries in
`rawreel.app/Contents/Frameworks/ffmpeg/`, sign each new library first and then the app (ad hoc,
as the release does), in the folder that holds `rawreel.app`:

```sh
codesign --force --sign - rawreel.app/Contents/Frameworks/ffmpeg/libavcodec.62.dylib  # each one
codesign --force --sign - rawreel.app
```

`codesign` refuses to sign the app while one of its libraries is unsigned. After deleting the
`ffmpeg/` folder, sign only the app.

**A package without FFmpeg.** A package built on a third-party prefix configured with
`-DDVV_BUNDLE_FFMPEG=OFF` (bootstrap `-BundleFfmpeg OFF`, or `--bundle-ffmpeg OFF` on Linux; the
default is `ON`) has no `ffmpeg/` folder and no `SOURCE.txt`. To open container files and
`lavfi:` graphs with it, put an LGPL shared build of FFmpeg 8.1 or a newer 8.x (`avutil-60.dll`,
`avcodec-62.dll`, `avformat-62.dll`, `avfilter-11.dll`, `swscale-9.dll`, `swresample-6.dll`; on
Linux `libavutil.so.60`, `libavcodec.so.62`, `libavformat.so.62`, `libavfilter.so.11`,
`libswscale.so.9`, `libswresample.so.6`; same version rule) into `ffmpeg/` next to `rawreel-gui`,
or set `DVV_FFMPEG_DIR` to its absolute path. On macOS the libraries are `libavutil.60.dylib`,
`libavcodec.62.dylib`, `libavformat.62.dylib`, `libavfilter.11.dylib`, `libswscale.9.dylib` and
`libswresample.6.dylib`, and the folder is `rawreel.app/Contents/Frameworks/ffmpeg/` (signed as
above).

## FFmpeg sources

With FFmpeg present (see "Finding FFmpeg" below), rawreel also opens container files and FFmpeg
filter graphs. Raw and Y4M files use their own readers.

**Container files (`file:`).** Any container FFmpeg demuxes (AVI, MKV, MOV, MP4, NUT, …) with a
lossless video codec: `rawvideo`, `v210`, `v410`, `r210`, `ffv1`, `huffyuv`, `ffvhuff`,
`utvideo`, `magicyuv`, `png` or `qtrle`. Any other codec, e.g. H.264, HEVC, MPEG-1/2/4 or another
lossy codec, is refused at open with "codec <name> is not supported (lossless codecs only)".

- rawreel plays FFmpeg's best video stream of the file (cover art never counts).
- MKV and NUT files have no complete frame index, so rawreel scans them when they open: large
  files open more slowly (closing rawreel stops the scan).
- A stream without usable timestamps (a packet without one, or two with the same) is
  `Sequential`: it plays forwards normally, but a step or seek backwards decodes again from the
  start of the file and is slow.
- A damaged frame (the decoder reports an error, e.g. a slice CRC mismatch in an FFV1 file) is
  never shown: its request fails with "frame N is damaged (the decoder reported an error)", and
  every following frame up to the next key frame with "frame M is damaged (it depends on damaged
  frame N)". From the next key frame on, the frames are exact again.
- The colour keys on the URI (see [Source URIs](#source-uris)) override the container's colour,
  e.g. `file:clip.mkv?colour=bt601-pal`. `fmt`, `size` and `fps` are accepted only when they
  equal the stream's.

**Filter graphs (`lavfi:`).** Everything after `lavfi:` is an FFmpeg filter graph, e.g.
`lavfi:testsrc2=s=1280x720:r=25,format=yuv420p` or
`lavfi:testsrc2=s=640x360:r=25,format=yuv420p10le,setparams=colorspace=bt709:range=pc`. A graph
may use only these 31 generators and simple filters: `testsrc`, `testsrc2`, `smptebars`,
`smptehdbars`, `pal75bars`, `pal100bars`, `rgbtestsrc`, `yuvtestsrc`, `colorchart`,
`colorspectrum`, `color`, `gradients`, `allrgb`, `allyuv`, `nullsrc`, `mandelbrot`, `format`,
`scale`, `setparams`, `setsar`, `setdar`, `crop`, `pad`, `hflip`, `vflip`, `transpose`, `null`,
`loop`, `tpad`, `drawbox` and `drawgrid`. Any other filter is refused with "lavfi: filter <name>
is not supported"; for a filter that reads files or URLs (`movie`, `amovie`, `subtitles`,
`drawtext`, `sendcmd`, …) the text goes on with "; open files with file: instead".

- A graph is one chain with one output (a filter with a second input is refused).
- A `lavfi:` URI takes no query; set the colour in the graph with `setparams`. View → Format…
  does not apply to a `lavfi:` stream.
- A `gradients` source with random colours (no fixed seed), or a random colour (`c=random`), shows
  different frames after a seek back.

**Finding FFmpeg.** rawreel loads the six libraries of an LGPL shared FFmpeg build (`avutil`,
`swresample`, `swscale`, `avcodec`, `avformat`, `avfilter`) from the first of:

1. `DVV_FFMPEG_DIR`, an absolute folder. When set it is final: rawreel looks nowhere else. An
   empty value counts as unset.
2. `ffmpeg/` next to the executable.
3. `../lib/ffmpeg/` from the executable's folder.
4. macOS only: `../Frameworks/ffmpeg/` from the executable's folder (inside `rawreel.app`).

The first folder of 2 to 4 that holds the avutil library is final too, so a broken bundled
FFmpeg is reported, not skipped. A GPL or non-free build, a different major version, or an older
minor one than rawreel was built with, is refused. Without FFmpeg the raw, Y4M and synthetic
sources still work. The last line of `rawreel-cli --version` shows the FFmpeg status (the About
box of `rawreel-gui` shows its version and licence as links): `FFmpeg <version> (<licence>) from https://github.com/BarabashkaD/custom_ffmpeg`
for the bundled FFmpeg, or `... from DVV_FFMPEG_DIR` when the variable is set (never a folder),
with ", lavfi not available" when avfilter is missing, or `FFmpeg: not available (<reason>)`
(About shows "details in the log" instead of the reason). The reason, in the log too, writes the
program's folder as `<app>`, the variable's folder as `$DVV_FFMPEG_DIR` and the home folder as `~`.

## rawreel-gui

`rawreel-gui` plays one video stream. The window has the viewport with the control panel under it,
an Info pane, a Log pane, a status bar (frame and length, target / actual fps, the pipeline's
bottleneck, and the colour mapping, e.g. "SDR, no tone mapping" or "PQ → SDR tone mapping (1000 →
100 nits)") and an infobar for warnings and errors. The log also goes to log files
(`rawreel-YYYYMMDD-HHMMSS.log`, see [Log files](#log-files)) in the user's data folder
(`%APPDATA%\rawreel\` on Windows, `~/.local/share/rawreel/` on Linux, or `$XDG_DATA_HOME/rawreel/`
when that is set, `~/Library/Application Support/rawreel/` on macOS).

The control panel has two rows. The upper row is a timeline strip over the slider: ticks, the first
and last frame numbers, and above the thumb the number of the frame on screen. The lower row has
Open on the left and, centred, First, Step back, Play/Pause, Stop, Step forward, Last and Loop,
with icons and tooltips. While you drag the slider, a marker shows the frame where the drag
started.

While the video is paused, holding ⌥ Option (macOS) or Ctrl (Windows, Linux) and moving over the
slider skims: the thumb, the frame number and the picture follow the pointer, and the marker shows
the frame where skimming started. A click on the slider jumps to the clicked frame and keeps it.
Without a click, releasing the key, moving off the slider or the window losing focus returns to
the start frame.

The Info pane shows the stream: URI, format, size, frame rate, frame count, colour and, for files,
the read mode (e.g. "reads: unbuffered"). The colour names the standard ("no standard" when the
file's colour fits none) and where it comes from: "(default)" for rawreel's defaults, "(from
file)", "(from the file name)", "(from clip.yuv.json)" for a clip description, "(from the graph)"
or "(auto)". A second line gives the matrix ("not used" for RGB and gray), range, curve and
primaries; a `hint:` line flags a standard that does not fit the size. PQ and HLG streams add the
line "tone-mapped (BT.2390, 1000 → 100 nits)". Notes from the source (e.g. how the file's colour
tags were read) go to the Log pane when a stream opens.

```text
Usage: rawreel-gui [--appearance system|light|dark] [--gpu N] [--selftest SCENARIO]
                   [--inflight K] [--config FILE] [--debug-seek-delay MS]
                   [--debug-transport] [URI]

  --appearance  light or dark windows, or the system setting (default)
  --gpu N       use GPU N (the index dvv-gpu-info lists) instead of the best one
  --selftest S  run a scripted check and exit: smoke, player, lost, perf or about
  --inflight K  frames in flight for --selftest perf (default 4)
  --config F    read config file F instead of the default rawreel.json
  --debug-seek-delay MS  developer: every frame takes MS ms longer to load
  --debug-transport  developer: log the transport bar's notifications
  --help        print this text
  URI           a source to open (a path, file:..., synthetic:...)
```

Run the examples from the unzipped rawreel folder (in a development build, from `$bin`, see
[Building and testing](#building-and-testing-windows)). For example (quote a URI that contains `&`
in PowerShell):

```powershell
& .\rawreel-gui.exe --appearance dark 'synthetic:gradient?fmt=yuv420p10le&size=1920x1080&frames=250'
```

On Linux, in the extracted folder (in a development build, from `$bin`, see
[Building and testing (Linux)](#building-and-testing-linux)):

```sh
./rawreel-gui 'synthetic:gradient?fmt=yuv420p10le&size=1920x1080&frames=250'
```

On Linux, `--appearance dark` does not darken the windows yet; they follow the GTK theme.

**Opening.** File → Open… (Ctrl+O), File → Open URI… (Ctrl+U), a URI on the command line (at most
one) or a file dropped on the window opens one stream; it replaces the stream that was open. When
several files are dropped, the first opens and the Log pane says "N files ignored: one stream at
a time". A file whose format the name does not give (e.g. `clip.yuv`), or an ambiguous one, opens
the format dialog, filled with the best guess, else the last choice, else 1920×1080 `yuv420p`
25 fps. The dialog sets:

- the pixel format: the catalogue, or a custom format loaded with Load JSON… (see the dvv-media
  README);
- the size: a preset such as `1920x1080 (1080p)`, or Custom;
- the frame rate: a preset from 23.976 to 120, or typed as `N` or `N/D` (29.97 and the other NTSC
  rates mean N×1000/1001);
- the colour: one of five standards, BT.601 (NTSC), BT.601 (PAL), BT.709, BT.2020 and BT.2100
  (with the curve PQ or HLG), marked "(default)" when nothing gives one (BT.709 at any size) or
  with where it comes from, as in the Info pane; "From file (no standard)" when the file's colour
  fits no standard. The range is Limited or Full on every standard; Full on BT.601, BT.709 or
  BT.2020 is marked "(not standard)", and the range follows the format until you pick one. A line
  below shows the matrix and primaries in use (with "From file (no standard)" also the range and
  curve). RGB and gray formats use no matrix. Primaries are shown, not converted. A grey hint flags a standard that does not fit the size, e.g. "SD
  size: BT.601 is common for SD".
- Auto detect (off by default, kept until rawreel-gui closes): an untagged clip gets a standard
  guessed from its height (up to 480 lines BT.601 (NTSC), up to 576 BT.601 (PAL), else BT.709),
  marked "(auto)".

Load JSON… also reads a [clip description](#clip-descriptions) and fills the parts it has. OK stays
disabled while the choice is invalid. View → Format… opens the same dialog for the open stream and
reopens it with the new choice at the current frame. On Y4M and container streams the format, size
and frame rate come from the file and show as read-only "(from file)" rows; only the colour can be
changed there, and Load JSON… takes only a clip description's colour. A file with "N trailing
bytes ignored" shows the warning in the infobar with a Format… button.

**Playback** shows every frame in order (it waits for a slow frame instead of skipping it) and
always fits the frame to the viewport: scaled up or down, centred, aspect ratio kept.

**4K at 60 fps.** Uncompressed 4K 10-bit 4:2:0 (`yuv420p10le`) at 60 fps is about 1.5 GB/s. The
file must sit on a drive that sustains that, i.e. an NVMe SSD, read unbuffered (the default); a
SATA SSD or a hard disk cannot, and a buffered read (e.g. on a network drive) may not. Playback
then stalls; it never skips a frame.

**Hotkeys** (bare keys act while no text field has the focus):

| Key | Command | Key | Command |
| --- | --- | --- | --- |
| Ctrl+O | File → Open… | Space | Play/Pause |
| Ctrl+U | File → Open URI… | Left / Right | Step back / forward |
| Ctrl+W | File → Close stream | Shift+Left / Shift+Right | 10 frames back / forward |
| Ctrl+Q | File → Quit | Home / End | First / last frame |
| | | L | Loop on or off |

Playback → Stop, View → Format…, View → Restart GPU and reopen, Help → GPU info and Help →
Diagnostics have no key.

**GPU failures.** When the GPU device is lost (or a presenter fails), playback pauses, the viewport
shows the background and the infobar says "GPU device lost: … Restart the GPU to continue." with a
Restart GPU button (also View → Restart GPU and reopen). It builds a new GPU context and reopens the stream
at the frame it showed, paused. Use the menu item after a GPU error; it also works without one.
Nothing is retried automatically. A GPU error at start (no Vulkan loader, no usable
device) shows a dialog and exits with 4.

**Selftests.** `--selftest <scenario> [URI]` drives the real window through its commands, each
step with a 5 s deadline, closes the window and exits with 6 naming a failed step:

- `smoke`: resizes, minimise and restore, the panes, menus, status fields, infobar and Help
  items, with no stream and then with one.
- `about` (opt-in, not in CTest): Help → About, its layout and links; run it when About changes.
- `player` (needs a URI): play, pause, step, Home/End, the slider, loop, resize, View → Format…
  and file drops.
- `lost` (needs a URI): an injected device loss, Reopen, play, a second loss.
- `perf` (needs a URI; manual): maximises the window, plays 600 frames, seeks to 20 random frames
  and prints one JSON line with `targetFps`, `actualFps`, `stalls`, `bottleneck`, `frames`,
  `presented`, `overwritten`, `p99TickLatenessMs`, `inflight`, `seekMedianMs` and `seekMaxMs`.
  `--inflight K` sets the frames in flight (default 4). It fails only on a step deadline.

**Exit codes** (`--help` lists them): 0 success; 1 bad arguments (e.g. a second URI: "at most one
URI"), or the GUI could not start; 4 GPU error (no Vulkan loader, no usable device, a presenter
failure in a selftest); 6 a `--selftest` check failed; 77 (`--selftest` only) no usable device or
no display, unless `DVV_REQUIRE_GPU=1` (then 4).

### Log pane

The Log pane is hidden at start. It shows the records of this run, the last 5000 of them. The list
has the columns Time (local, `HH:MM:SS.mmm`), Level, Message (a message of several lines shows its
first line followed by " …"); the Source column (`file.cpp:42`) is there only when `log.source`
is `true` (see [Configuration](#configuration)). Several rows can be selected. While the
list has the focus it keeps every key (arrows, Page Up/Down, Home, End, Shift to extend the
selection); the playback keys work again once the focus is elsewhere.

- **Show and hide:** on macOS, View → Show Log / Hide Log; on Windows and Linux, View → Log, a
  check item. The pane's caption has a close button. Help → Diagnostics shows the pane. A warning
  or an error never opens it by itself.
- **Status bar notice:** the status bar's last field counts the warnings and errors logged since
  the pane was last shown, for example "1 warning in the log" or "2 errors, 3 warnings in the log".
  It is empty while the pane is shown and when there are none. A click on it shows the pane.
- **Remembered:** the choice is kept in `rawreel.json` as `gui.showLogPane` and is saved when the
  window closes, only if it differs from its value at start.
- **Edit menu:** Undo, Redo, Cut, Copy, Paste, Delete and Select All act on the focused control:
  a text field, the Info pane (Copy, Select All) or the Log list (Copy of the selected rows,
  Select All). Delete has no key.
- **Copy and save:** File → Save Log… saves every record the pane holds; File → Save Selected
  Lines… saves the selected rows and is disabled without a selection. The Log list's context menu
  has Copy, Select All, Level ▸, Save Selected Lines… and Save Log…. Copy and save write one
  record per line in the format of the log files (`2026-10-20T12:34:56.789Z INFO  message`, UTC;
  with ` [file.cpp:42]` at the end when `log.source` is `true`; a multi-line message keeps its
  lines), UTF-8, with `\n` line ends. The save dialog suggests `rawreel-log-YYYYMMDD-HHMMSS.txt`.
  A target that is the log file in use, or a file in the log folder in use whose name is that of a
  log file (`rawreel-YYYYMMDD-HHMMSS.log`, with an optional `-N` before `.log`; names compare
  without regard to case on Windows and macOS), is refused.
- **Level:** the active log level decides which records are recorded. The list's context menu has
  Level ▸ with Errors, Warnings, Info, Debug and Trace, the active one checked. A level chosen
  there holds for this run only and is not saved; the next start uses the saved level. The saved
  level is chosen in Settings (Log page, Level).
- **Below the level:** a record below the active level is not recorded: not in the Log pane, not
  in the log file, not on stderr. Lowering the level (Info to Debug) shows Debug records from then
  on, never earlier ones. The system log keeps its own rule (warnings and errors, when it is on).
  Records from before the config was read are all in the pane; the log file gets those at or above
  the level.
- **Level markers:** rawreel records a marker line at every level, so a log shows its level:
  `===== log level: Info (settings) =====` once at start (`(default)` when no level is saved or
  the config was not used), `===== log level changed: Info -> Debug (settings) =====` when the
  level changes (`(this window only)` for a Settings change in a second copy,
  `(this run, Log pane menu)` for the menu), and a `===== log level: ... =====` line after every
  log file opens. Choosing the level that is already active writes nothing. Markers reach the
  Log pane, the log file, stderr and the system log when it is on.

### Settings window

| | macOS | Windows | Linux |
| --- | --- | --- | --- |
| Menu item | rawreel → Settings… (⌘,) | File → Settings… (Ctrl+,) | Edit → Preferences… (Ctrl+,) |
| Window | toolbar with the pages | dialog with tabs | dialog with tabs |
| Applies | each change at once | OK; Cancel applies nothing | each change at once |

The pages and their controls:

- **Log:** Level (`log.level`: Errors, Warnings, Info, Debug or Trace; it shows the saved level,
  not a level chosen for this run in the Log list's menu; changing it saves it and makes it the
  active level, which ends a run-only level; on Windows it applies with OK); "Write a log file"
  (`log.file.enabled`); Folder, the folder of the log files, with
  Choose…, Use Default (back to the user data folder) and a button that shows the folder in the
  file manager (Show in Finder, Show in Explorer or Open Folder); and the system log checkbox
  (`log.system.enabled`, off by default): "Send warnings and errors to the system log (Console)"
  on macOS, "… (journal/syslog)" on Linux, "Send warnings and errors to the debugger output" on
  Windows. A new folder takes effect at once; the old file gets the line `log file moves to
  '<new path>'` (at Warnings or Errors that Info line is not recorded) and
  stays where it was. When the saved folder cannot be used, rawreel
  writes to the default folder, logs one warning, and the page says "Not available, writing to
  <folder>".
- **Files:** File reads, Automatic, Unbuffered (no OS cache) or Buffered (OS cache)
  (`io.fileReads`), which applies to files opened after the change and to reopens; and the path
  of the settings file in use.

Only the first `rawreel-gui` process that uses a config file saves settings. A second copy runs
normally: its Settings window says "Another rawreel window owns the settings. Changes here apply
to this window only and are not saved.", and its changes and its Log pane state are not saved.

### Log files

- **Names:** a log file is named by its creation time in local time,
  `rawreel-YYYYMMDD-HHMMSS.log` (for example `rawreel-20261110-153126.log`); a name already taken
  gets `-2`, `-3`, … before `.log`. The lines inside carry UTC times. Because the names are local
  time, a clock set back (a daylight-saving change, a new time zone) can make an older file the
  newest for a while.
- **Which file is written:** at start, and when the log folder changes, the first `rawreel-gui`
  process that uses a config file (the one that saves settings) appends to the newest log file of
  the folder if it is below 4 MiB, else it creates a new file. A second copy always creates its
  own file. The newest file is the one with the latest name, the number after the time breaking a
  tie; a file whose name does not follow the pattern (`rawreel-old.log`) is never touched.
- **Splitting:** a log file is closed at 4 MiB and the next line starts a new file; a line is
  never split across two files. If a new file cannot be created, the current one goes on past
  4 MiB rather than lose lines.
- **First lines:** a file that rawreel creates starts with a line that names its level and the
  file before it, `===== log file: level Info, continues rawreel-20261110-140210.log =====`
  (a file created at start or when the folder changes has no `continues` part; a split names
  the file it follows). After every open, the file then gets the line
  `log file: '<full path>'` (at Warnings or Errors that Info line is not recorded) and the level
  marker. When the folder changes, the last line of the old file is
  `log file moves to '<full path>'` (also an Info line).
- **Never deleted:** rawreel deletes, renames and rotates no log file. A `rawreel.log` and its
  backups from an earlier version stay where they are and are not written any more.
- **Size warning:** at start, when a log folder is opened and after each split, rawreel adds up
  the sizes of the log files in the folder. Over 1 GiB it logs one warning, once per run: `log
  files in '<folder>' use <N> MiB (over 1 GiB); rawreel never deletes them`.

## rawreel-cli

```text
rawreel-cli info <uri> [--json] [--config <file>]
rawreel-cli dump <uri> --out <file> [--frame N] [--count K] [--view original|planes]
                 [--via cpu|gpu|auto] [--json] [--config <file>]
rawreel-cli bench <uri> [--frames N] [--inflight K] [--read-only] [--json] [--config <file>]
rawreel-cli --version [--json]
rawreel-cli --help
```

- `info` describes a source: provider, format, size, frame rate, frame count, frame bytes, colour,
  access, file read mode (`reads`) and warnings.
- `dump` writes `--count` frames (default 1) from frame `--frame` (default 0) to `--out`, one
  after the other. `--view original` (the default) writes the original bytes of each frame;
  `--view planes` writes its unpacked planes (format `<name>@planes`). `--via` defaults to `auto`.
- `bench` reads the first `--frames` frames (default 60; at most the frame count of the source) as
  GPU images in batches of `--inflight` frames (default 4; each batch is awaited before the next
  starts), and prints frames/s, GB/s and the time of each stage. It always uses the GPU. Frames/s
  and GB/s are wall clock; a stage's GB/s is its bytes over its summed run time, so it is not a
  throughput. `--read-only` stops after reading each frame into host memory (only Produce runs),
  so frames/s and GB/s are the file read's wall-clock throughput (`read wall clock:` in the text
  output). A file source keeps one read in flight whatever K is.
- `--config <file>` reads that config file instead of the default `rawreel.json` (see
  [Configuration](#configuration)).
- `--version` prints the version of rawreel, of each dvv library and of wxWidgets, the Vulkan
  device with its API version and driver, and the FFmpeg status line
  ([FFmpeg sources](#ffmpeg-sources)); `--help` prints the usage.

Each option is valid only for the commands above and may be given once. `--frame` is at least 0;
`--count`, `--frames` and `--inflight` are at least 1.

### Source URIs

A `<uri>` is one of:

- a file path whose name carries the size and the format (`clip_1920x1080_v210.yuv`), and
  optionally the colour standard as the last token (`clip_3840x2160_yuv420p10le_50_bt2100-pq.yuv`),
  or a Y4M file (`clip.y4m`);
- `file:<path>?fmt=<format>&size=WxH`;
- `synthetic:<gradient|noise>?fmt=<format>&size=WxH[&frames=N][&seed=S]`, 10 frames unless
  `frames` is given;
- a container file or a `lavfi:` graph (see [FFmpeg sources](#ffmpeg-sources)).

Every form except `lavfi:` also takes `colour=<standard>` (`bt601-ntsc`, `bt601-pal`, `bt709`,
`bt2020`, `bt2100-pq`, `bt2100-hlg`) and `range=full|limited`. `range=` works with every
standard (without it: the range the file signals, else limited for YUV and full for RGB and gray).
Without a standard from the URI, the file or its name, rawreel uses BT.709. A raw file also
takes a custom format as `fmt=@<file.json>` (see the dvv-media README) and a clip description as
`clip=@<file.json>`. In PowerShell, quote a URI that contains `&`. Paths with non-ASCII characters
work for both the source and `--out`.

### Clip descriptions

A clip description is a JSON file with the format, size, frame rate and colour of a raw file;
every part is optional except `"rawreelClip": 1`:

```json
{ "rawreelClip": 1, "format": "yuv420p10le", "size": { "width": 3840, "height": 2160 },
  "frameRate": "60000/1001", "colour": { "standard": "bt2100-pq", "range": "full" } }
```

`format` is a catalogue name or `"@<pixel-format.json>"` (relative to the clip description's
folder); `frameRate` is `N` or `"N/D"`; `colour` is `{"standard": "<colour= token>"}` with an
optional `"range"` (`full` or `limited`, as for `range=`). Unknown keys are errors. rawreel reads it from Load JSON…, from `clip=@<file.json>`, and as the sidecar
`<file>.<ext>.json` next to a raw file (`clip.yuv.json` for `clip.yuv`); the sidecar wins over the
file name, and the Log says so.

### --via

`--via` chooses where `dump` reads the frames:

- `cpu`: no GPU; the frames are read and unpacked on the host.
- `gpu`: upload, unpack on the GPU and readback. There is no fallback: without a usable device
  the run fails with 4.
- `auto`: `gpu` when a usable device exists, else `cpu`. Other GPU setup errors do not fall back;
  they fail with 4.

`--via cpu` and `--via gpu` write the same bytes. The result names the path that was used.

### Example

Dump three frames of a synthetic source into a raw file whose name carries the size and the
format, then describe the file. Start from the unzipped rawreel folder (in a development build,
from `$bin`, see [Building and testing](#building-and-testing-windows)). The later examples use
the same `$cli` and folder.

```powershell
$cli = (Resolve-Path .\rawreel-cli.exe).Path
Set-Location (New-Item -ItemType Directory -Force $env:TEMP/rawreel-cli-example)
& $cli dump 'synthetic:gradient?fmt=v210&size=1920x1080' --count 3 --out clip_1920x1080_v210.yuv
& $cli info clip_1920x1080_v210.yuv
```

On Linux the same in bash:

```sh
cli="$PWD/rawreel-cli"
mkdir -p /tmp/rawreel-cli-example && cd /tmp/rawreel-cli-example
"$cli" dump 'synthetic:gradient?fmt=v210&size=1920x1080' --count 3 --out clip_1920x1080_v210.yuv
"$cli" info clip_1920x1080_v210.yuv
```

The dump prints `wrote 3 frames (16588800 bytes) of v210 to clip_1920x1080_v210.yuv via gpu`
(`via cpu` without a usable device). The info prints:

```text
uri: file:clip_1920x1080_v210.yuv?fmt=v210&size=1920x1080&fps=25
provider: raw
format: v210
size: 1920x1080
fps: 25/1
frames: 3
frame bytes: 5529600
colour: bt709 (default) matrix=bt709 range=limited transfer=bt1886 primaries=bt709
access: random-cheap
reads: unbuffered
```

`reads` is the file read mode in use: `unbuffered`, `buffered`, or `buffered (fallback: <reason>)`
when the volume does not allow unbuffered reads (see [Configuration](#configuration)). The JSON
key is `reader`. The `colour` line shows `no standard (from file)` when the file's colour fits no
standard. A `note:` line follows for each note from the source, e.g. `note: clip.yuv.json describes
clip.yuv` or how a container's colour tags were read (JSON: `notes`).

### Exit codes

| Code | Meaning                                                                     |
| ---- | --------------------------------------------------------------------------- |
| 0    | success                                                                     |
| 1    | bad arguments, frames outside the source, `--frames` above the frame count  |
| 2    | input error: a file, URI or source that cannot be read, or an output error |
| 3    | unsupported or ambiguous source                                             |
| 4    | GPU error, also no usable device for `--via gpu` or `bench`                 |

In detail:

- 1 also covers an argument that is not valid Unicode.
- 2 covers the dvv errors `Io` and `Format`, and `OutOfMemory`, `Internal` and `Cancelled`
  outside the GPU setup. An `Internal` from Upload or GpuUnpack exits with 2 too, and so does a
  URI the parser rejects, even as `Unsupported` (e.g. `file://host/x`), and an unexpected
  exception.
- 3 covers `Unsupported` from opening a source.
- 4 covers `DeviceLost` and `ShaderCompile` anywhere, `Unsupported`, `OutOfMemory`, `Internal`
  and `Cancelled` from the GPU setup, and `OutOfMemory` from reading a frame through a store with
  a GPU context.

A leak check runs at exit and lists leaked objects on stderr; with `DVV_LEAK_CHECK=fail` (the
default in Debug builds) a run that would exit with 0 exits with 1.

### Text and JSON output

Without `--json` the result goes to stdout, and an error goes to stderr in a line that starts
with `error:` (followed by the usage for bad arguments). With `--json` both the result and the
errors are JSON on stdout, except three that stay plain text on stderr: an argument that is not
valid Unicode, an internal error (an unexpected exception) and the leak report. The keys are:

- `info`: `uri` (resolved), `provider`, `format` (the format description: `name`, `model`,
  `components`, `planes`, `widthMultiple`), `width`, `height`, `fps` (`num`, `den`), `frames`
  (`null` when unbounded), `frameBytes`, `colour` (`matrix`, `range`, `transfer`, `primaries`: the
  source's values as it reports them, `unspecified` included; a BT.601 matrix tag also fills in
  the PAL or NTSC primaries), `standard` (the `colour=` token in use, `null` when the colour
  matches no standard), `access` (`randomCheap`, `randomCostly` or `sequential`), `deterministic`,
  `reader` (`null` for a source that is not a file), `warnings` and `notes`.
- `dump`: `command` (`"dump"`), `uri`, `first`, `frames`, `bytes`, `view`, `via` (`cpu` or `gpu`,
  the path used), `format` (of the written data), `out` and `converters` (`name`, `runs`, `bytes`,
  `nanoseconds` each).
- `bench`: `uri`, `device`, `format`, `width`, `height`, `frameBytes`, `frames`, `inflight`,
  `reader` (`none` for a source that is not a file), `readOnly`, `seconds`, `fps`, `gbps` and
  `stages` (`name`, `runs`, `bytes`, `nanoseconds`, `gbps` each; Produce, HostToStaging, Upload,
  GpuUnpack; only Produce with `--read-only`).
- `--version`: `rawreel`, `dvv_base`, `dvv_registry`, `dvv_platform`, `dvv_gpu`, `dvv_media`,
  `dvv_components`, `wxwidgets`, `vulkan` and `ffmpeg` (the FFmpeg status line).

An error is `{"error": {"code", "message"}}`. `code` is `Arguments` for exit 1 and otherwise the
dvv error code (`Io`, `Format`, `Unsupported`, `OutOfMemory`, `DeviceLost`, `ShaderCompile`,
`Cancelled`, `Internal`):

```powershell
& $cli info 'synthetic:noise?fmt=nope&size=2x2' --json    # exit 2
```

```json
{
  "error": {
    "code": "Format",
    "message": "unknown pixel format 'nope'"
  }
}
```

An ambiguous source (exit 3, e.g. a one-byte `clip_33x17.yuv`) has the code `Ambiguous` and an
array `candidates` (`confidence`, `provider`, `why` each, e.g. `"why": "size only; format guessed
as yuv420p"`); pass `?impl=<id>` or more parameters to choose one. Without `--json` the same list
goes to stderr.

## Configuration

`rawreel-gui` and `rawreel-cli` read an optional JSON file, `rawreel.json`. `--config <file>` on
either executable reads another file instead. The default file is:

- on macOS, for an executable inside an app bundle (the shipped `rawreel.app`, and the build
  tree's `rawreel-gui.app`): `~/Library/Application Support/rawreel/rawreel.json`, never a file
  inside the signed bundle;
- otherwise, `rawreel.json` in the executable's folder if that file exists (a portable setup;
  never the working directory), else `rawreel.json` in the user data folder
  (`%APPDATA%\rawreel\` on Windows, `~/.local/share/rawreel/` or `$XDG_DATA_HOME/rawreel/` on
  Linux, `~/Library/Application Support/rawreel/` on macOS), the folder of the default
  log files.

No `rawreel.json` is shipped, so the built-in defaults apply until a file exists. `rawreel-gui`
creates it in the user data folder when a setting is saved.

```json
{
  "io":  { "fileReads": "auto" },
  "gui": { "showLogPane": false },
  "log": {
    "level":  "info",
    "source": false,
    "file":   { "enabled": true, "folder": "" },
    "system": { "enabled": false }
  }
}
```

| Key | Values | Default | Used by |
| --- | --- | --- | --- |
| `io.fileReads` | `auto`, `unbuffered`, `buffered` | `auto` | both executables |
| `gui.showLogPane` | `true`, `false` | `false` | rawreel-gui |
| `log.level` | `error`, `warn`, `info`, `debug`, `trace` (any letter case) | `info` | rawreel-gui |
| `log.source` | `true`, `false` | `false` | rawreel-gui |
| `log.file.enabled` | `true`, `false` | `true` | rawreel-gui |
| `log.file.folder` | an absolute folder path; `""` is the user data folder | `""` | rawreel-gui |
| `log.system.enabled` | `true`, `false` | `false` | rawreel-gui |

`log.level` is the active log level at start (see [Log pane](#log-pane)); Settings writes it.
`log.source` has no control in the GUI, is read at start and is never written by rawreel: when
`true`, the log files, Copy and save end each line with ` [file.cpp:42]` and the Log list has the
Source column; the few lines logged before the config is read have no source.

`io.fileReads` sets how raw and Y4M files are read. FFmpeg container sources read through
FFmpeg's own file I/O (`reads: ffmpeg`) and `lavfi:` graphs read no files (`reads: lavfi`), so the
key does not apply to them.

- `unbuffered`: no OS file cache, sector-aligned reads. A file whose volume does not allow it
  (for example some network drives or an unusual sector size) fails to open.
- `buffered`: through the OS file cache (a file that fits in memory plays again from RAM).
- `auto`: unbuffered when the file's volume passes a check at open (its sector size, an
  unbuffered open and a first aligned read), else buffered for that file with one log line that
  says why.

The mode in use shows as `reads:` in `rawreel-cli info` and in the GUI Info pane.

`rawreel-gui` writes the file when a setting changes (see [Settings window](#settings-window)),
only the keys it sets: it keeps the other keys, the key order, a symbolic link and the file's
permissions. It does not write a file that is not valid, has a bad value or is read-only; the
setting then holds for the session only, with the warning "config '<path>' not saved: <reason>".
`rawreel-cli` never writes the file.

A missing default `rawreel.json` means the defaults, silently. A missing
`--config` file, or a file that cannot be read, is not valid JSON or has a bad value, gives one
warning in the log ("config '<path>' ignored: <reason>; using the defaults") and the defaults. A
bad value in any key, including a `gui` or `log` key that `rawreel-cli` does not use, makes both
executables use the defaults. An older rawreel ignores a file that holds a value only a newer one
knows. Unknown keys are ignored. To try another file:

```powershell
& $cli info <uri> --config <folder>/rawreel.json
```

## Current limits

- Only lossless video codecs open (see [FFmpeg sources](#ffmpeg-sources)); lossy codecs are
  refused at open.
- A decoder output format outside the pixel-format catalogue is `Unsupported`; the message names
  the format ("decoder output <format> is not in the catalogue"; for a `lavfi:` graph, append
  `,format=<a catalogue format>`).
- The sample aspect ratio is ignored, so anamorphic files show stretched in the fit view.
- A step or seek backwards on a `Sequential` stream decodes again from the start of the file.
- MKV and NUT files are scanned when they open, so large ones open more slowly.
- Below 1:8 the fit view undersamples, so very small views of very large frames can alias.
- FFV1 decoding is CPU-bound, so 4K FFV1 does not reach 60 fps: about 50 fps on a typical 8-core
  desktop CPU for easy content, far less for noisy content. Raw and Y4M files play 4K at 60 fps.
- The Windows zip is not code-signed.
- On Linux, `rawreel-gui` uses X11; on a Wayland desktop it runs through XWayland.
- On Linux, a crash writes no crash report; use a core dump (see [Bug reports](#bug-reports)).
- macOS: Apple Silicon and macOS 27 or newer only. The app is not signed with a Developer ID or
  notarized. rawreel pins only the Vulkan driver (the bundled KosmicKrisp); Vulkan implicit
  layers installed system-wide (for example by a Vulkan SDK) can still load into it. The macOS CI
  has no usable Metal device, so it runs the GPU tests on lavapipe (Mesa's software driver, CI
  only); the display tests and the real-GPU check run only on a Mac (see
  [Building and testing (macOS)](#building-and-testing-macos)). A crash writes no rawreel crash
  report; use the system's (see [Bug reports](#bug-reports)).

## Bug reports

When `rawreel-gui` or `rawreel-cli` crashes, it writes a minidump to `%APPDATA%\rawreel\crashes\`
(`<exe>-<yyyymmdd-hhmmss>-<pid>.dmp`). Attach the `.dmp` file and the log to the bug report: for
`rawreel-gui` its newest log file in `%APPDATA%\rawreel\` (`rawreel-*.log`); for `rawreel-cli` its console output,
which includes the `rawreel: crash dump <path>` line. Add the output of `rawreel-cli --version`.
The maintainer reads the dump with the symbols of the same release,
`rawreel-<v>-windows-x64-symbols.zip`. `abort` and `__fastfail` end the process without a dump.

On Linux, rawreel writes no crash report; the system can write a core dump instead. Run
`ulimit -c unlimited` in the shell that starts rawreel. Where the core goes depends on the system
(`cat /proc/sys/kernel/core_pattern`): on Ubuntu, apport usually stores it below
`/var/lib/apport/coredump/`; with systemd-coredump, `coredumpctl` lists it. Attach the
core, the log (the newest `rawreel-*.log` in `~/.local/share/rawreel/` for `rawreel-gui`,
the console output for `rawreel-cli`) and the output of `rawreel-cli --version`. A core holds the
program's memory,
including frames of the open video. The maintainer reads it with the symbols of the same release,
`rawreel-<v>-linux-x64-symbols.tar.xz`, extracted into the package folder
(`tar xf rawreel-<v>-linux-x64-symbols.tar.xz -C rawreel-<v>`), so that gdb finds
`rawreel-gui.debug` next to `rawreel-gui`.

On macOS, rawreel has no crash handler; the system writes a crash report for `rawreel-gui` or
`rawreel-cli` to `~/Library/Logs/DiagnosticReports/`. Attach it, the log
(the newest `rawreel-*.log` in `~/Library/Application Support/rawreel/` for
`rawreel-gui`, the console output for `rawreel-cli`; the log names the Vulkan driver) and
the output of `rawreel-cli --version`, whose
version line includes `driver KosmicKrisp ...`. The maintainer reads the report with the symbols
of the same release, `rawreel-<v>-macos-arm64-symbols.zip` (each `.dSYM` matches its executable
by UUID).

## Verifying downloads

`SHA256SUMS` lists the SHA256 of every release file. In PowerShell, in the download folder:

```powershell
$f = 'rawreel-0.10.0-windows-x64.zip'
$want = (Select-String -Path SHA256SUMS -SimpleMatch $f).Line.Split(' ')[0]
(Get-FileHash $f -Algorithm SHA256).Hash -eq $want    # True
```

On Linux (or wherever coreutils exist), and on macOS: `sha256sum -c --ignore-missing SHA256SUMS`
(or `shasum -a 256 -c --ignore-missing SHA256SUMS`). Every release file also has a build
attestation from the release workflow; with the GitHub CLI:

```powershell
gh attestation verify rawreel-0.10.0-windows-x64.zip --repo BarabashkaD/rawreel
```

```sh
gh attestation verify rawreel-0.10.0-linux-x64.tar.xz --repo BarabashkaD/rawreel
gh attestation verify rawreel-0.10.0-macos-arm64.dmg --repo BarabashkaD/rawreel
```

## Getting the source

```sh
git clone --recursive https://github.com/BarabashkaD/rawreel
```

This also checks out the dvv libraries in `libs/`; after a clone without `--recursive`, run
`git submodule update --init`. Keep the clone folder's name `rawreel`: the build paths below
contain it. On Windows the clone path must be at most 63 characters long (e.g. `X:\src\rawreel`),
because the builds go up to 196 characters deeper and Windows paths end at 259; bootstrap checks
it. A fork resolves the submodules against its own account, so clone from BarabashkaD and add the
fork as a second remote.

To change a dvv library, work in its folder in `libs/` as `libs/dvv-build/README.md` describes
("Changing a library"):

1. Create a branch there (the submodule is detached at its release tag), make the change, and run
   that library's tests and rawreel's.
2. Raise its version and `CHANGELOG.md`, merge to its `main`, tag `v<version>`, push both, and
   wait for its CI to pass.
3. In rawreel, `git add libs/dvv-<name>` and commit "build: pin dvv-<name> <version>". A pin
   change is merged only after a green run of the `ci` workflow that includes every library's
   tests at the pins: a pull request does not run them, so start `ci` by hand on its branch with
   `test_libs` (every push to `main` runs them).
4. Raise the library's line in rawreel's `dvv-deps.txt` only when rawreel needs the new version.
5. A dvv-build change follows the same flow; the `@v<version>` of `dvv-ci.yml` in
   `.github/workflows/` moves with its pin (check-pins checks it).

## Building and testing (Windows)

From the `rawreel` folder, in PowerShell 7, after `libs/dvv-build/bootstrap.ps1 -Toolchain
msvc` (prerequisites and the pip tools: `libs/dvv-build/README.md`, "Prerequisites" and
"Building an application"), configure, build Debug and Release and run all tests:

```powershell
cmake --workflow --preset vs2026
```

The executables are `libs/_build/rawreel/vs2026/<Debug|Release>/rawreel-gui.exe` and
`rawreel-cli.exe`. A build tree has no `ffmpeg/` folder next to them, so point rawreel at
the FFmpeg of the third-party prefix (an absolute path) and, optionally, turn on Vulkan
synchronization validation:

```powershell
$env:DVV_FFMPEG_DIR = (Get-ChildItem -Recurse libs/_third_party/msvc/custom_ffmpeg -Filter 'avutil-*.dll' | Select-Object -First 1).DirectoryName
$env:VK_ADD_LAYER_PATH = (Resolve-Path libs/_third_party/msvc/bin).Path
$env:DVV_VK_VALIDATION = 'sync'
$bin = (Resolve-Path libs/_build/rawreel/vs2026/Release).Path
& "$bin/rawreel-cli.exe" --version
& "$bin/rawreel-gui.exe" 'lavfi:testsrc2=s=1280x720:r=25,format=yuv420p'
```

`--version` ends with the FFmpeg line, which names the FFmpeg version, its licence and the
repository it is built from.

CTest sets these variables itself. The tests (`ctest --preset vs2026`, or
`vs2026-release`) include:

- `rawreel.unit`, `rawreel.core_unit`, `rawreel.core_session`, `rawreel.core_timer`: the
  renderer's CPU reference, the player logic, commands, format choice, arguments, texts, the
  config loader and the headless Session, without a GPU.
- `rawreel.gpu` (label `gpu`): the fit compositor against its CPU reference and the Session on the
  GPU.
- `rawreel.gui_selftest.smoke`, `.smoke_source`, `.smoke_noffmpeg`, `.player` and `.lost` (labels
  `gpu;display;gui`): the selftests above, smoke with `log.source` on, and smoke without FFmpeg;
  `rawreel.gui_fault.presenter` and `.stack`: an injected presenter failure and a failed start exit
  with 4.
- `rawreel.cli_unit` and `rawreel.cli_e2e.*`: the `rawreel-cli` arguments and output forms, and
  the built `rawreel-cli` (`info`, `dump`, exit codes, non-ASCII paths, `dump --via gpu`, `bench`,
  FFmpeg sources and `--version`).
- `rawreel.packaging_scripts`: the package checks and the artifact privacy scan. The packages
  themselves are built and checked by `cmake -DBUNDLE=ON -P packaging/dry-run.cmake`, run in the
  `rawreel` folder; it works in `_dryrun` next to that folder.

GPU tests exit with 77 (skipped) without a usable Vulkan 1.4 device unless `DVV_REQUIRE_GPU=1`.
Under `vs2026-asan`, `rawreel-gui` and its selftests are not built (wxWidgets is not
instrumented).

## Building and testing (Linux)

Ubuntu 24.04 (also in WSL on Windows 11) with Clang 19, libstdc++ 14, CMake and Ninja (see
`libs/dvv-build/README.md`). From the `rawreel` folder, in bash, after
`libs/dvv-build/bootstrap.sh` (it names the apt packages it still needs), configure, build
Debug and Release and run all tests:

```sh
cmake --workflow --preset ninja-clang
```

The executables are `libs/_build/rawreel/ninja-clang/<Debug|Release>/rawreel-gui` and
`rawreel-cli`. As on Windows, point rawreel at the FFmpeg of the third-party prefix and,
optionally, turn on Vulkan synchronization validation:

```sh
export DVV_FFMPEG_DIR="$PWD/libs/_third_party/clang/custom_ffmpeg/linux-x64/shared/lgpl/lib"
export VK_ADD_LAYER_PATH="$PWD/libs/_third_party/clang/share/vulkan/explicit_layer.d"
export DVV_VK_VALIDATION=sync
bin="$PWD/libs/_build/rawreel/ninja-clang/Release"
"$bin/rawreel-cli" --version
"$bin/rawreel-gui" 'lavfi:testsrc2=s=1280x720:r=25,format=yuv420p'
```

The tests (`ctest --preset ninja-clang`, or `ninja-clang-release`) are those of
[Building and testing (Windows)](#building-and-testing-windows), plus
`rawreel.gui_start.no_display` (a start without a display exits as described above). The GUI
tests (label `display`) need an X display: a desktop session, WSLg, or Xvfb
(`xvfb-run -a -s "-screen 0 1920x1080x24" ctest --preset ninja-clang`). Without a GPU, Mesa's
software driver lavapipe works: `export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json`.
`libs/dvv-build/tools/sanity.sh` runs the quick sanity cycle (see `libs/dvv-build/README.md`).
Under `ninja-clang-asan` and `ninja-clang-tsan`, `rawreel-gui` and its selftests are not built.
The Linux packages are built and checked by the same `dry-run.cmake` command as on Windows; on
Linux it works in `/tmp/rawreel-dryrun`.

## Building and testing (macOS)

An Apple Silicon Mac with macOS 27 or newer, Xcode 27 or the Command Line Tools with the macOS 27
SDK (AppleClang 21 or newer), CMake 4.2 or newer and Ninja (`brew install cmake ninja`); the
rest is in `libs/dvv-build/README.md`. From the `rawreel` folder, in a shell, after
`libs/dvv-build/bootstrap.sh` (the toolchain `appleclang` is the default on macOS; it downloads
the pinned LunarG Vulkan SDK, takes the KosmicKrisp driver and the validation layer out
of it, and builds the Vulkan loader and wxWidgets for Cocoa, wxWidgets into
`libs/_third_party/appleclang-wx`; it downloads about 400 MB, the LunarG SDK, and takes a few
minutes on an Apple Silicon Mac), configure, build Debug and Release and run
all tests:

```sh
cmake --workflow --preset ninja-macos
```

The executables are `libs/_build/rawreel/ninja-macos/<Debug|Release>/rawreel-cli` and
`rawreel-gui.app/Contents/MacOS/rawreel-gui`. As on Linux, point rawreel at the FFmpeg of the
third-party prefix and, optionally, turn on Vulkan synchronization validation. A Mac has no system
Vulkan driver, so a build-tree executable also needs `VK_DRIVER_FILES` (the packaged app sets it
itself):

```sh
export DVV_FFMPEG_DIR="$PWD/libs/_third_party/appleclang/custom_ffmpeg/macos-arm64/shared/lgpl/lib"
export VK_DRIVER_FILES="$PWD/libs/_third_party/appleclang/share/vulkan/icd.d/libkosmickrisp_icd.json"
export VK_ADD_LAYER_PATH="$PWD/libs/_third_party/appleclang/share/vulkan/explicit_layer.d"
export DVV_VK_VALIDATION=sync
bin="$PWD/libs/_build/rawreel/ninja-macos/Release"
"$bin/rawreel-cli" --version
"$bin/rawreel-gui.app/Contents/MacOS/rawreel-gui" 'lavfi:testsrc2=s=1280x720:r=25,format=yuv420p'
```

In the build tree, `rawreel-gui.app` reads `rawreel.json` from Application Support like the
shipped app, and only `rawreel-cli` reads it from its own folder (see
[Configuration](#configuration)). The tests (`ctest --preset ninja-macos`, or
`ninja-macos-release`) are those of [Building and testing (Windows)](#building-and-testing-windows)
and [Linux](#building-and-testing-linux). The GPU tests run on the Mac's GPU through KosmicKrisp
(Vulkan 1.4); the GUI tests (label `display`) need a logged-in desktop session.
`DVV_REQUIRE_GPU=1` makes a missing device fail instead of skip: run
`DVV_REQUIRE_GPU=1 ctest --preset ninja-macos` (or `ninja-macos-release`) for that; the workflow
above runs the tests without it. Under `ninja-macos-asan`,
`rawreel-gui` and its selftests are not built. The macOS package, `rawreel-<v>-macos-arm64.dmg`
with `rawreel-<v>-macos-arm64-symbols.zip`, is built and checked (package checks and a smoke
test) by `cmake -DBUNDLE=ON -P packaging/dry-run.cmake`, run in the `rawreel` folder; on macOS it
works in `/tmp/rawreel-dryrun`.

## Licence

MIT (see `LICENSE`). `THIRD_PARTY_NOTICES.txt` in each package lists the licences of the
third-party code it ships. The FFmpeg libraries in `ffmpeg/` are LGPL (see
[FFmpeg in the package](#ffmpeg-in-the-package)); they include LAME (`LGPL-2.0-or-later`) and
SVT-AV1 (`BSD-3-Clause-Clear`). wxWidgets includes code under the `libpng-2.0`, `IJG` and `X11`
licences.
