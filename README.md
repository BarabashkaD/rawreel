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

A release has these files (`<v>` is the version, e.g. `0.9.0`):

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
- `rawreel-<v>-third-party-sources.tar.xz`: the complete source of the bundled FFmpeg of both
  packages (see [FFmpeg in the package](#ffmpeg-in-the-package)).
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

## FFmpeg in the package

The Windows zip and the Linux tar.xz include the FFmpeg libraries avutil, avcodec, avformat,
avfilter, swscale and swresample of custom_ffmpeg 1.0.0 (FFmpeg n8.1.3, LGPL 2.1 or later) in
`ffmpeg/` (on Linux named `libavutil.so.60`, `libavcodec.so.62`, …). rawreel loads them at run
time and is not linked with them; you may replace them with your own LGPL shared build of FFmpeg
8.1 or a newer 8.x with the same file names, or delete the folder. Each library must have
the major version rawreel was built with and at least its minor version (avutil 60.26, avcodec
62.28, avformat 62.12, avfilter 11.14, swscale 9.5, swresample 6.3); an older one, a GPL build or
a non-free build is refused. Their complete source is the release asset
`rawreel-<v>-third-party-sources.tar.xz` (custom_ffmpeg with its submodules and build scripts, and
LAME 3.100); one archive covers both packages, and `ffmpeg/SOURCE.txt` says the same. These
libraries contain decoders for codecs such as H.264, HEVC and AAC, which may be covered by
patents in some countries; licensing
them is a matter for the user and the distributor. rawreel itself opens only lossless video
codecs. This is not legal advice.

**A package without FFmpeg.** A package built on a third-party prefix configured with
`-DDVV_BUNDLE_FFMPEG=OFF` (bootstrap `-BundleFfmpeg OFF`, or `--bundle-ffmpeg OFF` on Linux; the
default is `ON`) has no `ffmpeg/` folder and no sources archive. To open container files and
`lavfi:` graphs with it, put an LGPL shared build of FFmpeg 8.1 or a newer 8.x (`avutil-60.dll`,
`avcodec-62.dll`, `avformat-62.dll`, `avfilter-11.dll`, `swscale-9.dll`, `swresample-6.dll`; on
Linux `libavutil.so.60`, `libavcodec.so.62`, `libavformat.so.62`, `libavfilter.so.11`,
`libswscale.so.9`, `libswresample.so.6`; same version rule) into `ffmpeg/` next to `rawreel-gui`,
or set `DVV_FFMPEG_DIR` to its absolute path.

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

The first folder of 2 and 3 that holds the avutil library is final too, so a broken bundled
FFmpeg is reported, not skipped. A GPL or non-free build, a different major version, or an older
minor one than rawreel was built with, is refused. Without FFmpeg the raw, Y4M and synthetic
sources still work. The last line of `rawreel-cli --version` shows the FFmpeg status (the About
box of `rawreel-gui` shows its version and licence as links): `FFmpeg <version> (<licence>) from https://github.com/BarabashkaD/custom_ffmpeg`
for the bundled FFmpeg, or `... from DVV_FFMPEG_DIR` when the variable is set (never a folder),
with ", lavfi not available" when avfilter is missing, or `FFmpeg: not available (<reason>)`
(About shows "details in the log" instead of the reason). The reason, in the log too, writes the
program's folder as `<app>`, the variable's folder as `$DVV_FFMPEG_DIR` and the home folder as `~`.

## rawreel-gui

`rawreel-gui` plays one video stream. The window has the viewport with the transport bar under it,
an Info pane, a Log pane, a status bar (frame and length, target / actual fps, the pipeline's
bottleneck, and the colour mapping, e.g. "SDR, no tone mapping" or "PQ → SDR tone mapping (1000 →
100 nits)") and an infobar for warnings and errors. The log also goes to `rawreel.log` in the
user's data folder (`%APPDATA%\rawreel\` on Windows, `~/.local/share/rawreel/` on Linux, or
`$XDG_DATA_HOME/rawreel/` when that is set).

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
                   [--inflight K] [--config FILE] [URI]

  --appearance  light or dark windows, or the system setting (default)
  --gpu N       use GPU N (the index dvv-gpu-info lists) instead of the best one
  --selftest S  run a scripted check and exit: smoke, player, lost or perf
  --inflight K  frames in flight for --selftest perf (default 4)
  --config F    read config file F instead of rawreel.json next to rawreel-gui
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
- `--config <file>` reads that config file instead of `rawreel.json` next to `rawreel-cli` (see
  [Configuration](#configuration)).
- `--version` prints the version of rawreel, of each dvv library and of wxWidgets, the Vulkan
  device and the FFmpeg status line ([FFmpeg sources](#ffmpeg-sources)); `--help` prints the usage.

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

`rawreel-gui` and `rawreel-cli` read an optional JSON file, `rawreel.json`, from the folder of
the executable (never from the working directory). `--config <file>` on either executable reads
another file instead. No `rawreel.json` is shipped, so the built-in defaults apply until you
create one; to keep one next to the executables, unzip or extract rawreel to a folder you can
write to (not under Program Files).

```json
{ "io": { "fileReads": "auto" } }
```

| Key | Values | Default |
| --- | --- | --- |
| `io.fileReads` | `auto`, `unbuffered`, `buffered` | `auto` |

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

A missing `rawreel.json` next to the executable means the defaults, silently. A missing
`--config` file, or a file that cannot be read, is not valid JSON or has a bad value, gives one
warning in the log ("config '<path>' ignored: <reason>; using the defaults") and the defaults.
Unknown keys are ignored. To try another file:

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

## Bug reports

When `rawreel-gui` or `rawreel-cli` crashes, it writes a minidump to `%APPDATA%\rawreel\crashes\`
(`<exe>-<yyyymmdd-hhmmss>-<pid>.dmp`). Attach the `.dmp` file and the log to the bug report: for
`rawreel-gui` its log file `%APPDATA%\rawreel\rawreel.log`; for `rawreel-cli` its console output,
which includes the `rawreel: crash dump <path>` line. Add the output of `rawreel-cli --version`.
The maintainer reads the dump with the symbols of the same release,
`rawreel-<v>-windows-x64-symbols.zip`. `abort` and `__fastfail` end the process without a dump.

On Linux, rawreel writes no crash report; the system can write a core dump instead. Run
`ulimit -c unlimited` in the shell that starts rawreel. Where the core goes depends on the system
(`cat /proc/sys/kernel/core_pattern`): on Ubuntu, apport usually stores it below
`/var/lib/apport/coredump/`; with systemd-coredump, `coredumpctl` lists it. Attach the
core, the log (`~/.local/share/rawreel/rawreel.log` for `rawreel-gui`, the console output for
`rawreel-cli`) and the output of `rawreel-cli --version`. A core holds the program's memory,
including frames of the open video. The maintainer reads it with the symbols of the same release,
`rawreel-<v>-linux-x64-symbols.tar.xz`, extracted into the package folder
(`tar xf rawreel-<v>-linux-x64-symbols.tar.xz -C rawreel-<v>`), so that gdb finds
`rawreel-gui.debug` next to `rawreel-gui`.

## Verifying downloads

`SHA256SUMS` lists the SHA256 of every release file. In PowerShell, in the download folder:

```powershell
$f = 'rawreel-0.9.0-windows-x64.zip'
$want = (Select-String -Path SHA256SUMS -SimpleMatch $f).Line.Split(' ')[0]
(Get-FileHash $f -Algorithm SHA256).Hash -eq $want    # True
```

On Linux (or wherever coreutils exist): `sha256sum -c --ignore-missing SHA256SUMS`. Every release
file also has a build attestation from the release workflow; with the GitHub CLI:

```powershell
gh attestation verify rawreel-0.9.0-windows-x64.zip --repo BarabashkaD/rawreel
```

```sh
gh attestation verify rawreel-0.9.0-linux-x64.tar.xz --repo BarabashkaD/rawreel
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
- `rawreel.gui_selftest.smoke`, `.smoke_noffmpeg`, `.player` and `.lost` (labels
  `gpu;display;gui`): the selftests above, and smoke without FFmpeg; `rawreel.gui_fault.presenter`
  and `.stack`: an injected presenter failure and a failed start exit with 4.
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
`libs/dvv-build/tools/sanity.ps1` (`sanity.sh` on Linux) runs T0, the quick sanity cycle;
`-Small -Downstream` (`--small --downstream` for `sanity.sh`) runs the tests of T1 (T1 also runs
clang-format and the privacy lint; see the test tiers in `libs/dvv-build/README.md`).
Under `ninja-clang-asan` and `ninja-clang-tsan`, `rawreel-gui` and its selftests are not built.
The Linux packages are built and checked by the same `dry-run.cmake` command as on Windows; on
Linux it works in `/tmp/rawreel-dryrun`. With `-DBUNDLE=ON` it also verifies the FFmpeg archive
of the Windows prefix (`libs/_third_party/msvc` of the same clone, or `-DWINDOWS_PREFIX=<dir>`)
and stops before the build when that prefix is not bootstrapped; `-DBUNDLE=OFF` needs only the
Linux prefix.

## Licence

MIT (see `LICENSE`). `THIRD_PARTY_NOTICES.txt` in each package lists the licences of the
third-party code it ships. The FFmpeg libraries in `ffmpeg/` are LGPL (see
[FFmpeg in the package](#ffmpeg-in-the-package)); they include LAME (`LGPL-2.0-or-later`) and
SVT-AV1 (`BSD-3-Clause-Clear`). wxWidgets includes code under the `libpng-2.0`, `IJG` and `X11`
licences.
