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
streams), a Log pane, a status bar (frame and length, target / actual fps, the pipeline's
bottleneck) and an infobar for warnings and errors. The log also goes to `rawreel.log` in the
user's data folder.

```text
Usage: rawreel-gui [--appearance system|light|dark] [--gpu N] [--selftest SCENARIO]
                   [--inflight K] [URI]

  --appearance  light or dark windows, or the system setting (default)
  --gpu N       use GPU N (the index dvv-gpu-info lists) instead of the best one
  --selftest S  run a scripted check and exit: smoke, player, lost or perf
  --inflight K  frames in flight for --selftest perf (default 4)
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

Every scenario ends by closing the window: no validation message, no presenter failure, no leak.
CTest runs them as `rawreel.gui_selftest.smoke`, `.player` and `.lost` (labels `gpu;display;gui`).
`perf` is planned for M3.1 and exits with 6 ("scenario perf is not available") until then.

**Exit codes** (`--help` lists them): 0 success; 1 bad arguments (e.g. a second URI: "at most one
URI"), or the GUI could not start; 4 GPU error (no Vulkan loader, no usable device, a presenter
failure in a selftest); 6 a `--selftest` check failed; 77 (`--selftest` only) no usable device or
no display, unless `DVV_REQUIRE_GPU=1` (then 4).

**Planned for M3.1:** the 4K60 performance run (`--selftest perf`, a 4K `yuv420p10le` file at
60 fps) and faster disk reads (overlapped IOCP reads on Windows, built only if the `bench` gate
shows a gain). FFmpeg sources and packaging come in M4.

## rawreel-cli

```text
rawreel-cli info <uri> [--json]
rawreel-cli dump <uri> --out <file> [--frame N] [--count K] [--view original|planes]
                 [--via cpu|gpu|auto] [--json]
rawreel-cli bench <uri> [--frames N] [--inflight K] [--json]
rawreel-cli --version [--json]
rawreel-cli --help
```

- `info` describes a source: provider, format, size, frame rate, frame count, frame bytes, colour,
  access, reader and warnings.
- `dump` writes `--count` frames (default 1) from frame `--frame` (default 0) to `--out`, one
  after the other. `--view original` (the default) writes the original bytes of each frame;
  `--view planes` writes its unpacked planes (format `<name>@planes`). `--via` defaults to `auto`.
- `bench` reads the first `--frames` frames (default 60) as GPU images in batches of `--inflight`
  frames (default 4; a batch is awaited as a whole before the next starts, so K is a batch size,
  not a sustained queue depth), and prints frames/s, GB/s and the throughput of each stage. It
  always uses the GPU. `--frames` must not exceed the frame count of the source.
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
reader: unbuffered
```

`reader` is `unbuffered` or `buffered`, depending on what the file system allows.

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
  `reader` (`none` for a source that is not a file), `seconds`, `fps`, `gbps` and `stages` (`name`,
  `runs`, `bytes`, `nanoseconds`, `gbps` each; Produce, HostToStaging, Upload, GpuUnpack).
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

## Tests

- `rawreel.unit`: the renderer without a GPU: the compositor's CPU reference and `DisplayState`
  (with its race case).
- `rawreel.core_unit`: `rawreel_core` (no wx): the fit transform, the player logic, the command
  table and key normaliser, format choice, drop routing, the `rawreel-gui` arguments and the status
  and Info texts.
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
  folder: `info` of synthetic, raw and Y4M sources, a `dump --via cpu` round trip, the exit codes
  and error forms, and non-ASCII paths.
- `rawreel.cli_e2e.gpu` and `rawreel.cli_e2e.bench` (label `gpu`): `dump --via gpu` writes the
  bytes of `dump --via cpu`, and `bench` runs every stage; both under synchronization validation
  that must report nothing. They exit with 77 without a usable device unless `DVV_REQUIRE_GPU=1`.

## Platforms

v1.0 supports Windows only. `rawreel-gui` also builds on Linux, but its viewport has no native
window there yet (a stub; the GTK native child comes in v1.x), so the selftests are not
registered. `rawreel-gui` and its selftests are not built under sanitizers (`vs2026-asan` and the
Linux sanitizer presets): wxWidgets is not instrumented. `rawreel_core`, the renderer,
`rawreel-cli` and their tests still build and run there.

Licence: MIT.
