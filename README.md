# rawreel

rawreel is a raw video player. This repository holds two programs on top of the dvv libraries
([dvv-base](https://github.com/BarabashkaD/dvv-base),
[dvv-registry](https://github.com/BarabashkaD/dvv-registry),
[dvv-platform](https://github.com/BarabashkaD/dvv-platform),
[dvv-gpu](https://github.com/BarabashkaD/dvv-gpu) and
[dvv-media](https://github.com/BarabashkaD/dvv-media)): `rawreel-gui`, the application, and
`rawreel-cli`, a console tool that describes, dumps and benchmarks a source. It builds through
[rawreel-workspace](https://github.com/BarabashkaD/rawreel-workspace), which provides the presets,
the third-party dependencies (wxWidgets among them) and the CI workflow.

## Build and run (Windows)

From this folder, in PowerShell 7, after the workspace bootstrap:

```powershell
cmake --workflow --preset vs2026
../_build/rawreel/vs2026/Debug/rawreel-gui.exe     # Release: ../_build/rawreel/vs2026/Release/
../_build/rawreel/vs2026/Debug/rawreel-cli.exe --help
```

The executables are `_build/rawreel/<preset>/<config>/rawreel-gui.exe` and `rawreel-cli.exe` in
the workspace folder. To run them with synchronization validation, point the loader at the layer
of the third-party prefix and choose the mode (CTest does the first for the tests):

```powershell
$env:VK_ADD_LAYER_PATH = (Resolve-Path ../_third_party/msvc/bin).Path
$env:DVV_VK_VALIDATION = 'sync'
```

## rawreel-gui

`rawreel-gui` plays one raw video stream (v1.0: Windows only). The window has the viewport with the
transport bar under it, an Info pane (the stream: URI, format, size, frame rate, frame count and
colour, with "(assumed)" for defaults and "tone-mapped (BT.2390, 1000 → 100 nits)" for PQ and HLG
streams, and for files the read mode, e.g. "reads: unbuffered"), a Log pane, a status bar (frame and length, target / actual fps, the pipeline's
bottleneck) and an infobar for warnings and errors. The log also goes to `rawreel.log` in the
user's data folder.

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

For example (quote a URI that contains `&` in PowerShell):

```powershell
$gui = (Resolve-Path ../_build/rawreel/vs2026/Release/rawreel-gui.exe).Path
& $gui --appearance dark 'synthetic:gradient?fmt=yuv420p10le&size=1920x1080&frames=250'
```

**Opening.** File → Open… (Ctrl+O), File → Open URI… (Ctrl+U), a URI on the command line (at most
one) or a file dropped on the window opens one stream; it replaces the stream that was open. When
several files are dropped, the first opens and the Log pane says "N files ignored: one stream in
v1.0". A file whose format the name does not give (e.g. `clip.yuv`), or an ambiguous one, opens
the format dialog, filled with the best guess, else the last choice, else 1920×1080 `yuv420p`
25 fps. The dialog sets the pixel format (the catalogue, or a custom format loaded with
Load JSON…, see the dvv-media README), size, frame rate and colour: matrix (BT.601, BT.709,
BT.2020 NCL, RGB, or Custom with Kr and Kb), range, transfer (sRGB, BT.1886, linear, PQ, HLG) and
primaries; OK stays disabled while the choice is invalid. View → Format… opens the same dialog
for the open stream and reopens it with the new choice at the current frame. A file with "N
trailing bytes ignored" shows the warning in the infobar with a Format… button.

**Playback** shows every frame in order (it waits for a slow frame instead of skipping it) and
always fits the frame to the viewport: scaled up or down, centred, aspect ratio kept, the
background around it. Known limit: below 1:8 the fit view still undersamples (at most 4 × 4
bilinear taps per screen pixel), so very small views of very large frames can alias.

**Hotkeys** (`commandTable()`; bare keys act while no text field has the focus):

| Key | Command | Key | Command |
| --- | --- | --- | --- |
| Ctrl+O | File → Open… | Space | Play/Pause |
| Ctrl+U | File → Open URI… | Left / Right | Step back / forward |
| Ctrl+W | File → Close stream | Shift+Left / Shift+Right | 10 frames back / forward |
| Ctrl+Q | File → Quit | Home / End | First / last frame |
| | | L | Loop on or off |

Playback → Stop, View → Format…, View → Reopen, Help → GPU info and Help → Diagnostics have no key.

**GPU failures.** When the GPU device is lost (or a presenter fails), playback pauses, the viewport
shows the background and the infobar says "GPU device lost: … Reopen to continue." with a Reopen
button (also View → Reopen). Reopen builds a new GPU context and reopens the stream at the frame it
showed, paused. Nothing is retried automatically. A GPU error at start (no Vulkan loader, no usable
device) shows a dialog and exits with 4.

**Options.** `--appearance dark` (or `light`) sets the window appearance at start; `system`
follows Windows. `--gpu N` picks the GPU by the index `dvv-gpu-info` prints; the default is the
best usable one.

**Selftests.** `--selftest <scenario> [URI]` drives the real window through its commands and
synthesised wx events, each step with a 5 s deadline, and exits with 6 naming the failed step:

- `smoke`: present at the window size, resize to 800×600 and 333×177, minimise and restore, with no
  stream and then with one; the panes, menus, status fields, infobar and Help items.
- `player` (needs a URI): play, pause, step, Home/End, the slider, loop, a resize that keeps the
  fit, View → Format… (BT.601, full range, the frame kept), drops of a named file, of `clip.yuv`
  (through the format dialog) and of two files.
- `lost` (needs a URI): an injected device loss → the infobar with Reopen within 5 s, no hang;
  Reopen presents the kept frame again; play advances; a second loss.
- `perf` (needs a URI; manual, Windows only, not a CTest entry): the 4K60 run (exit criterion E7)
  with longer step deadlines. It maximises the window, waits until the prefetch window is full,
  plays 600 frames, seeks to 20 random frames and prints one JSON line with `targetFps`,
  `actualFps`, `stalls`, `bottleneck`, `frames`, `presented`, `overwritten`, `p99TickLatenessMs`,
  `inflight`, `seekMedianMs` and `seekMaxMs`. `--inflight K` sets the frames in flight (default
  4). It fails only on a step deadline; the E7 rule is applied by hand to the median of three runs
  by `actualFps`: pass at `actualFps` ≥ 59.5, 0 `stalls` and at most 6 `overwritten`. The measured
  numbers are in spec §16.

Every scenario ends by closing the window: no validation message, no presenter failure, no leak.
CTest runs `rawreel.gui_selftest.smoke`, `.player` and `.lost` (labels `gpu;display;gui`).

**Exit codes** (`--help` lists them): 0 success; 1 bad arguments (e.g. a second URI: "at most one
URI"), or the GUI could not start; 4 GPU error (no Vulkan loader, no usable device, a presenter
failure in a selftest); 6 a `--selftest` check failed; 77 (`--selftest` only) no usable device or
no display, unless `DVV_REQUIRE_GPU=1` (then 4).

**Performance (M3.1).** Disk reads follow U-IO2: unbuffered on Windows with a buffered fallback
per file, buffered elsewhere, one read in flight per file source; see
[The config file](#the-config-file). No IOCP. FFmpeg sources and packaging come in M4.

**What 4K60 needs.** Uncompressed 4K 10-bit 4:2:0 (`yuv420p10le`) at 60 fps is about 1.5 GB/s.
The file must sit on a drive that sustains a file read of about 1.5 GB/s or more, i.e. an NVMe
SSD, read unbuffered (the Windows default); a SATA SSD or a hard disk cannot. The buffered
fallback, e.g. on a network drive, may not keep up and drop below 60 fps (playback stalls; it
never skips a frame). On the development machine, one data point: a median of 3 runs of 59.999
fps with 0 stalls, 10 of 12 runs clean, the read stage (the slowest) at 70.7 fps, and upload plus
GPU unpack 4.6 ms per frame, so the margin is thin (spec §16).

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
- `bench` reads the first `--frames` frames (default 60) as GPU images in batches of `--inflight`
  frames (default 4; a batch is awaited as a whole before the next starts, so K is a batch size,
  not a sustained queue depth), and prints frames/s, GB/s and the throughput of each stage. It
  always uses the GPU. `--frames` must not exceed the frame count of the source. Frames/s and GB/s
  are wall clock; a stage's GB/s is bytes over its summed run time, which falls as runs overlap,
  so it is not a throughput. `--read-only` stops after reading each frame into host memory (only
  Produce runs), so frames/s and GB/s are the file read stage's wall-clock throughput
  (`read wall clock:` in the text output). A file source keeps one read in flight whatever K is
  (U-IO2), so `--read-only` measures single-read throughput.
- `--config <file>` reads that config file instead of `rawreel.json` next to `rawreel-cli` (see
  [The config file](#the-config-file)).
- `--version` prints the version of rawreel and of each dvv library; `--help` prints the usage.

Each option is valid only for the commands above and may be given once. `--frame` is at least 0;
`--count`, `--frames` and `--inflight` are at least 1.

A `<uri>` is one of:

- a file path whose name carries the size and the format (`clip_1920x1080_v210.yuv`), or a Y4M
  file (`clip.y4m`);
- `file:<path>?fmt=<format>&size=WxH`;
- `synthetic:<gradient|noise>?fmt=<format>&size=WxH[&frames=N][&seed=S]`, 10 frames unless
  `frames` is given.

Every form also takes the colour parameters `range`, `matrix` (with `kr` and `kb` for
`matrix=custom`), `transfer` and `primaries`, and a raw file takes a custom format as
`fmt=@<file.json>` (see the dvv-media README). In PowerShell, quote a URI that contains `&`.

### --via

`--via` chooses where `dump` reads the frames:

- `cpu`: a host-only store with no GPU context. The frames are read and unpacked on the host.
- `gpu`: the frames are read into host planes in the staging domain, which only the GPU Readback
  reaches (upload, unpack on the GPU, readback). There is no fallback: without a usable device
  the run fails with 4.
- `auto`: `gpu` when a usable device exists, else `cpu`. Other GPU setup errors do not fall back;
  they fail with 4.

`--via cpu` and `--via gpu` write the same bytes. The result names the path that was used.

### Example

From this folder, after a Release build: dump three frames of a synthetic source into a raw file
whose name carries the size and the format, then describe the file. The later examples use the
same `$cli` and folder.

```powershell
$cli = (Resolve-Path ../_build/rawreel/vs2026/Release/rawreel-cli.exe).Path
Set-Location (New-Item -ItemType Directory -Force $env:TEMP/rawreel-cli-example)
& $cli dump 'synthetic:gradient?fmt=v210&size=1920x1080' --count 3 --out clip_1920x1080_v210.yuv
& $cli info clip_1920x1080_v210.yuv
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
colour: matrix=unspecified range=unspecified transfer=unspecified primaries=unspecified
access: random-cheap
reads: unbuffered
```

`reads` is the file read mode in use: `unbuffered`, `buffered`, or `buffered (fallback: <reason>)`
when the volume does not allow unbuffered reads (see [The config file](#the-config-file)). The JSON
key is `reader`.

### Exit codes

| Code | Meaning                                                                     |
| ---- | --------------------------------------------------------------------------- |
| 0    | success                                                                     |
| 1    | bad arguments, frames outside the source, `--frames` above the frame count  |
| 2    | input error: a file, URI or source that cannot be read, or an output error |
| 3    | unsupported or ambiguous source                                             |
| 4    | GPU error, also no usable device for `--via gpu` or `bench`                 |

In detail:

- 1 also covers an argument that is not valid Unicode (Windows).
- 2 covers the dvv errors `Io` and `Format`, and `OutOfMemory`, `Internal` and `Cancelled`
  outside the GPU setup. An `Internal` from Upload or GpuUnpack exits with 2 too, and so does a
  URI the parser rejects, even as `Unsupported` (e.g. `file://host/x`). An unexpected exception
  also exits with 2.
- 3 covers `Unsupported` from opening a source.
- 4 covers `DeviceLost` and `ShaderCompile` anywhere, `Unsupported`, `OutOfMemory`, `Internal`
  and `Cancelled` from the GPU setup (the GPU registry, the context and a store with a GPU
  context), and `OutOfMemory` from reading a frame through a store with a GPU context.

A leak check runs at exit. It lists the leaked objects on stderr; with `DVV_LEAK_CHECK=fail` (the
default in Debug builds) a run that would exit with 0 exits with 1.

### Text and JSON output

Without `--json` the result goes to stdout, and an error goes to stderr in a line that starts
with `error:` (followed by the usage for bad arguments). With `--json` both the result and the
errors are JSON on stdout, except three that stay plain text on stderr: an argument that is not
valid Unicode, an internal error (an unexpected exception) and the leak report. The keys are:

- `info`: `uri` (resolved), `provider`, `format` (the format description: `name`, `model`,
  `components`, `planes`, `widthMultiple`), `width`, `height`, `fps` (`num`, `den`), `frames`
  (`null` when unbounded), `frameBytes`, `colour` (`matrix`, `range`, `transfer`, `primaries`),
  `access` (`randomCheap`, `randomCostly` or `sequential`), `deterministic`, `reader` (`null` for
  a source that is not a file) and `warnings`.
- `dump`: `command` (`"dump"`), `uri`, `first`, `frames`, `bytes`, `view`, `via` (`cpu` or `gpu`,
  the path used), `format` (of the written data), `out` and `converters` (`name`, `runs`, `bytes`,
  `nanoseconds` each).
- `bench`: `uri`, `device`, `format`, `width`, `height`, `frameBytes`, `frames`, `inflight`,
  `reader` (`none` for a source that is not a file), `readOnly`, `seconds`, `fps`, `gbps` and
  `stages` (`name`, `runs`, `bytes`, `nanoseconds`, `gbps` each; Produce, HostToStaging, Upload,
  GpuUnpack; only Produce with `--read-only`).
- `--version`: `rawreel`, `dvv_base`, `dvv_registry`, `dvv_platform`, `dvv_gpu`, `dvv_media`.

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

An ambiguous source (exit 3) has the code `Ambiguous` and lists the candidates; pass `?impl=<id>`
or more parameters to choose one. Without `--json` the same list goes to stderr.

```powershell
Set-Content -Path clip_33x17.yuv -Value 'x' -NoNewline
& $cli info clip_33x17.yuv --json    # exit 3
```

```json
{
  "error": {
    "candidates": [
      {
        "confidence": 30,
        "provider": "raw",
        "why": "size only; format guessed as yuv420p"
      }
    ],
    "code": "Ambiguous",
    "message": "ambiguous source 'clip_33x17.yuv'"
  }
}
```

### Unicode arguments (Windows)

On Windows the entry point is `wmain`: the arguments arrive as UTF-16 and are converted to UTF-8,
so paths with non-ASCII characters work for both the source and `--out`.

## The config file

`rawreel-gui` and `rawreel-cli` read an optional JSON file, `rawreel.json`, from the folder of
the running executable (found at run time, never from the working directory). `--config <file>`
reads another file instead. The build does not ship one; packaging decides that in M4.

```json
{ "io": { "fileReads": "auto" } }
```

| Key | Values | Default |
| --- | --- | --- |
| `io.fileReads` | `auto`, `unbuffered`, `buffered` | `auto` |

`io.fileReads` sets how raw and Y4M files are read (M3.1 decision U-IO2):

- `unbuffered`: no OS file cache, sector-aligned reads. A file whose volume does not allow it
  (for example some network drives or an unusual sector size) fails to open.
- `buffered`: through the OS file cache with a sequential-access hint (the portable way; a file
  that fits in memory plays again from RAM).
- `auto`: on Windows, unbuffered when the file's volume passes an open-time check (its sector size,
  an unbuffered open and a first aligned read), else buffered for that file with one log line
  that says why; buffered elsewhere.

Each file source reads one frame at a time; the player's prefetch keeps the pipeline busy. The mode
in use shows as `reads:` in `rawreel-cli info` and in the GUI Info pane.

A missing `rawreel.json` next to the executable means the defaults, silently. A missing
`--config` file, or a file that cannot be read, is not valid JSON or has a bad value, gives one
warning in the log ("config '<path>' ignored: <reason>; using the defaults") and the defaults. Unknown keys are ignored.

## Tests

- `rawreel.unit`: the renderer without a GPU: the compositor's CPU reference and `DisplayState`
  (with its race case).
- `rawreel.core_unit`: `rawreel_core` (no wx): the fit transform, the player logic, the command
  table and key normaliser, format choice, drop routing, the `rawreel-gui` arguments, the status
  and Info texts, and the config file loader (missing, valid and invalid files, bad values,
  `--config`, the default location next to the executable).
- `rawreel.core_session` and `rawreel.core_timer`: the headless Session suite (a host-only store,
  a fake clock and timer, injected read, out-of-memory and device-lost errors, Reopen, shutdown
  with the leak check) and the `ThreadTimer` suite.
- `rawreel.gpu` (label `gpu`): the fit compositor against its CPU reference (upscale, downscale,
  1:1, letterbox and pillarbox, dark and light), and the Session on the GPU (playback, an injected
  device loss, Reopen); exits with 77 without a usable Vulkan 1.4 device unless
  `DVV_REQUIRE_GPU=1`.
- `rawreel.gui_selftest.smoke`, `.player` and `.lost`: the selftests above; `rawreel.gui_fault.presenter`
  and `.stack`: an injected presenter failure and a failed start exit with 4. Windows only.
- `rawreel.cli_unit`: the argument parsing of `rawreel-cli`, its text and JSON forms and the
  Windows argument conversion.
- `rawreel.cli_e2e.info`, `.dump`, `.errors` and `.unicode`: the built `rawreel-cli` in a scratch
  folder: `info` of synthetic, raw and Y4M sources (with the read mode from the default, a
  `--config` and an invalid config file), a `dump --via cpu` round trip, the exit codes and error
  forms, and non-ASCII paths.
- `rawreel.cli_e2e.gpu` and `rawreel.cli_e2e.bench` (label `gpu`): `dump --via gpu` writes the
  bytes of `dump --via cpu`, and `bench` runs every stage (only Produce with `--read-only`); both
  under synchronization validation
  that must report nothing. They exit with 77 without a usable device unless `DVV_REQUIRE_GPU=1`.

## Platforms

v1.0 supports Windows only. `rawreel-gui` also builds on Linux, but its viewport has no native
window there yet (a stub; the GTK native child comes in v1.x), so the selftests are not
registered. `rawreel-gui` and its selftests are not built under sanitizers (`vs2026-asan` and the
Linux sanitizer presets): wxWidgets is not instrumented. `rawreel_core`, the renderer,
`rawreel-cli` and their tests still build and run there.

Licence: MIT.
