# rawreel

rawreel is a raw video player. This repository holds `rawreel-gui`, the application on top of the
dvv libraries ([dvv-base](https://github.com/BarabashkaD/dvv-base),
[dvv-platform](https://github.com/BarabashkaD/dvv-platform) and
[dvv-gpu](https://github.com/BarabashkaD/dvv-gpu)). It builds through
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
```

The executable is `_build/rawreel/<preset>/<config>/rawreel-gui.exe` in the workspace folder. To
run it with synchronization validation, point the loader at the layer of the third-party prefix
and choose the mode (CTest does the first for the tests):

```powershell
$env:VK_ADD_LAYER_PATH = (Resolve-Path ../_third_party/msvc/bin).Path
$env:DVV_VK_VALIDATION = 'sync'
```

## --selftest and exit codes

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

## Tests

- `rawreel.unit`: argument parsing and the CPU reference of the test pattern.
- `rawreel.gpu` (label `gpu`): the shader's output equals the CPU reference; exits with 77 without a
  usable Vulkan 1.4 device unless `DVV_REQUIRE_GPU=1`.
- `rawreel.gui_selftest`: `rawreel-gui --selftest`, Windows only.

## Platforms

v1.0 supports Windows only. `rawreel-gui` also builds on Linux, but its viewport has no native
window there yet (a stub; the GTK native child comes in v1.x), so the selftest is not registered.
`rawreel-gui` and its selftest are not built under sanitizers (`vs2026-asan` and the Linux sanitizer
presets): wxWidgets is not instrumented. The renderer and its tests still build and run there.

Licence: MIT.
