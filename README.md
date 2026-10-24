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

## rawreel-gui in M1b

`rawreel-gui` opens a wxWidgets window with one Vulkan viewport, a native child window with its own
Vulkan surface and presenter. The viewport shows a test pattern rendered by a Slang compute shader
(`shaders/test_pattern.slang`): 75 % colour bars in the upper two thirds, a grey ramp in the lower
third and a 1-pixel white border. Resizing redraws the pattern at the new size; minimising and
restoring work. Validation messages and errors go to stderr, and a leak check runs at exit.

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

## rawreel-gui --selftest and exit codes

`rawreel-gui --help` prints:

```text
Usage: rawreel-gui [--selftest] [--help]

  --selftest  open the window, present, resize, minimise and restore it, close it, and exit
  --help      print this text

Exit codes: 0 success; 1 bad arguments, or the GUI could not start; 4 GPU error (no Vulkan
loader, no usable device, presenter failure); 6 a --selftest check failed; 77 (--selftest only)
no usable device or no display, unless DVV_REQUIRE_GPU=1 (then 4).
```

`--selftest` runs a fixed script, each step with a 5 s deadline: the first frames at the viewport's
size, two resizes (800×600 and 333×177), minimise (no new frames, hidden), restore (a new frame),
close (no validation message, no presenter failure, no leak). A failed step exits with 6 and names
the step. CTest runs it as `rawreel.gui_selftest` (labels `gpu;display;gui`).

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
- `bench` reads the first `--frames` frames (default 60) as GPU images, `--inflight` reads at a
  time (default 4), and prints frames/s, GB/s and the throughput of each stage. It always uses
  the GPU. `--frames` must not exceed the frame count of the source.
- `--version` prints the version of rawreel and of each dvv library; `--help` prints the usage.

Each option is valid only for the commands above and may be given once. `--frame` is at least 0;
`--count`, `--frames` and `--inflight` are at least 1.

A `<uri>` is one of:

- a file path whose name carries the size and the format (`clip_1920x1080_v210.yuv`), or a Y4M
  file (`clip.y4m`);
- `file:<path>?fmt=<format>&size=WxH`;
- `synthetic:<gradient|noise>?fmt=<format>&size=WxH[&frames=N][&seed=S]`, 10 frames unless
  `frames` is given.

In PowerShell, quote a URI that contains `&`.

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
  outside the GPU stages. An unexpected exception also exits with 2.
- 3 covers `Unsupported` outside the GPU stages.
- 4 covers `DeviceLost` and `ShaderCompile`, and `Unsupported`, `OutOfMemory`, `Internal` and
  `Cancelled` in a GPU stage.

A leak check runs at exit. It lists the leaked objects on stderr; with `DVV_LEAK_CHECK=fail` (the
default in Debug builds) a run that would exit with 0 exits with 1.

### Text and JSON output

Without `--json` the result goes to stdout, and an error goes to stderr in a line that starts
with `error:` (followed by the usage for bad arguments). With `--json` both the result and the
errors are JSON on stdout. The keys are:

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

- `rawreel.unit`: the argument parsing of `rawreel-gui` and the CPU reference of the test pattern.
- `rawreel.gpu` (label `gpu`): the shader's output equals the CPU reference; exits with 77 without a
  usable Vulkan 1.4 device unless `DVV_REQUIRE_GPU=1`.
- `rawreel.gui_selftest`: `rawreel-gui --selftest`, Windows only.
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
window there yet (a stub; the GTK native child comes in v1.x), so the selftest is not registered.
`rawreel-gui` and its selftest are not built under sanitizers (`vs2026-asan` and the Linux sanitizer
presets): wxWidgets is not instrumented. The renderer, `rawreel-cli` and their tests still build and
run there.

Licence: MIT.
