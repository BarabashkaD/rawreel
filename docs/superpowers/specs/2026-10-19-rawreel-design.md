# rawreel — design specification

Date: 2026-10-19. Status: approved 2026-10-20 after the written-spec review (§17).

rawreel is a cross-platform, GPU-accelerated desktop application (plus a headless CLI) for viewing and
inspecting raw video: playback, pixel-exact inspection, statistics, quality metrics and GPU processing.
It is a ground-up redesign of an earlier Qt prototype and a personal learning project: modern C++,
Vulkan, Slang and a component architecture are goals in themselves.

---

## 1. Goals, scope and terms

### 1.1 Requirements (full list; drives the architecture from day one)

| Area | Requirement |
|---|---|
| Playback | Open, play, pause/stop, step forward/backward, seek, loop; view sizes (fit, 100 %, zoom 10 %–3200 % towards the cursor, pan), fullscreen; drag-and-drop; hotkeys |
| Formats | Any RGB/YUV/gray: planar, semi-planar, packed; 8–16 bit incl. bit-packed (v210); both endiannesses; gray8; custom formats defined by the user |
| Sources | Raw files without a container (main case), Y4M, containers via FFmpeg, generated test sources (FFmpeg lavfi graphs, built-in synthetic patterns) |
| Streams | 1 primary; 2 for comparison/metrics (reference + test); 3 optional; never N. Colour formats may differ; different resolutions optional (not v1) |
| Tools | Pipette (exact code values), grid overlay of various sizes, zoom into a grid-selected region, save part of a frame |
| Statistics | Per frame and between frames (min/max/mean/stddev per component, histograms, difference statistics) |
| Metrics | MSE/PSNR (per channel and weighted 6:1:1), SSIM, MS-SSIM, PSNR-HVS, CIEDE2000, more over time; VMAF optional but wanted; per-frame table, graph, aggregates, CSV/JSON export |
| Processing | One active processor per stream (edge detection, user shaders…); read-only analysers (histogram, statistics, motion vectors between frames N and M) |
| User code | User-written Slang shaders with an auto-generated parameter UI |
| Colour | BT.601/709/2020 (NCL; CL later), custom matrices, full/limited range, sRGB/BT.1886, PQ/HLG tone-mapped to SDR; HDR output later |
| Performance | "Professional": up to 8K and 16 bit; 4K 10-bit at 60 fps (~1.5 GB/s); files larger than RAM with instant seek; no dropped frames by default |
| Platforms | v1.0 supports Windows and Linux on X11 (native X11 or XWayland) (user decisions U-WIN and U-LNX1, §17): the GTK GUI, the Linux tests with ASan/TSan and a Linux package (§13.5) are part of v1.0; native Wayland and macOS later. CI builds Windows and Linux from the start (Wayland build-only); macOS CI is added only after the user's approval |
| Licence | MIT; no GPL dependency in any shipped artifact |
| Headless | `rawreel-cli` on the same core: info, metrics, export, benchmarks |
| Object lifetime | Every component and managed object in `dvv::` is created only through a factory (direct construction is a compile error), owned through smart pointers, and registered and monitored for its whole lifetime (§9.4) |

**v1.0 scope (user decision U-SCOPE, §17).** The table is the full list; v1.0 delivers this part
of it:
- one stream of any supported format (the catalogue and custom JSON formats) with the full colour
  mapping of the colour standards BT.601 (NTSC, PAL), BT.709, BT.2020 and BT.2100 (PQ, HLG), with
  full/limited range, PQ and HLG tone-mapped to SDR (custom Kr/Kb matrices: v1.x, U-D5-7);
- play, pause, step, seek and loop; fit to window only;
- sources: raw files, Y4M and FFmpeg (containers and lavfi, M4);
- on GPU device loss an error and a user Reopen (no automatic recovery);
- Windows and Linux X11/XWayland, each with a release package (U-WIN, U-LNX1, Platforms above).

Moved to v1.x: multi-stream (2–3 streams, tiles, sync, comparison), the pipette, grids, region
zoom, region save, all export (raw, PNG, Y4M; GUI and CLI), viewport zoom and pan, fullscreen,
automatic device-lost recovery, the tools, and all statistics, metrics and analysis (statistics,
histograms, MSE/PSNR, SSIM, MS-SSIM, PSNR-HVS, CIEDE2000, metrics table, graph, CSV/JSON, CLI
`metrics`). The architecture keeps them possible (§9.3, §15). Also v1.x from the M4 re-scope: the
analysis interfaces and components, the metric oracles, the `convert:` source, the Capabilities
page and `rawreel-cli capabilities`, `--diag`, the Open generator presets, `--ffmpeg-dir`, the
Open dialog FFmpeg filter, `SHADERS` in `dvv_add_component`, the AppImage (§15), native Wayland,
the Linux file-I/O default review and the Linux frame-timer jitter check. U-LNX1 brings the Linux
GUI (X11/XWayland), the Linux tests, Linux ASan/TSan and the Linux package back into v1.0.

### 1.2 Non-goals
- Not an editor or encoder: no timeline editing, no filter chains/DAGs (exactly one processor per stream).
- No more than 3 simultaneous streams.
- No C++ modules in v1 (see §15, v2).
- No plugin ABI for third-party binaries in v1 (components are compiled in; user shaders are the run-time extension point).

### 1.3 Terms

| Term | Meaning |
|---|---|
| Content | A set of sample values with an original pixel format, colour parameters and size |
| `FrameKey` | Identity of content (source + index, or processor recipe hash) |
| `IFrame` | Interface implemented once per memory technology; holds content and declares the views it offers |
| View | One representation of content: format + colour + layout + API + memory domain/device (`ViewSpec`) |
| `FrameStore` | Single owner and index of all frames and views: dedup, leases, caches, monitoring |
| Converter | Produces a new view of the *same* content (upload, readback, unpack, to-display) |
| Processor | Produces *new* content (new `FrameKey`) |
| Component | A compiled-in feature behind an interface (`ISource`, `IMetric`, …), created through the registry |
| Original | The content's own format and colour domain, exactly as read or produced |
| Lossless | A view that is bijective on code values (e.g. v210 unpacked to 16-bit planes); padding bits are not content |

---

## 2. Technology decisions

| Topic | Decision |
|---|---|
| Language | **C++23**, no modules, no `import std`; std library preferred, no Boost. Concepts, ranges, `std::span`, `std::expected`, `std::format`/`std::print`, `std::jthread`/`stop_token`, `std::move_only_function`, coroutines with our own small task layer |
| Toolchain | **CMake ≥ 4.2**; Windows **Visual Studio 2026** (MSVC v145, `/std:c++23preview` set explicitly — CMake maps C++23 to `/std:c++latest`, which tracks C++26 drafts); Linux **Clang ≥ 19** with **libstdc++ ≥ 14** (`-std=c++23`; `std::print`, `std::ranges::to` and `std::generator` need libstdc++ 14). GCC and libc++ are not supported (§17) |
| GUI | **wxWidgets 3.3** (native look), wxAUI docking, wxPropertyGrid, dark mode |
| GPU | **Vulkan 1.4** via **Vulkan-Hpp** (dynamic dispatcher; the loader is loaded at run time through dvv-platform's `DynamicLibrary`, `vulkan-1.dll` from System32 only or `libvulkan.so.1`, not through `vk::detail::DynamicLoader`), **vk-bootstrap**, **VMA** (`VMA_DYNAMIC_VULKAN_FUNCTIONS`). No volk, no SDL |
| Shaders | **Slang**: `slangc` at build time → embedded SPIR-V + reflection; `libslang` loaded at run time for user shaders |
| FFmpeg | Optional, loaded at run time, never linked; only the `lgpl` shared profile of the custom_ffmpeg project. Six libraries (`avutil`, `swresample`, `swscale`, `avcodec`, `avformat`, `avfilter`), searched in this order: `DVV_FFMPEG_DIR` (an absolute path; final, no fallthrough; an empty value counts as unset), else `<exe dir>/ffmpeg`, then `<exe dir>/../lib/ffmpeg` (for the Linux package layout, v1.x; the first one that holds avutil is final too). Each library loads with `LibrarySearch::DllDirectory` (its dependencies from its own folder, then the application folder and System32; never PATH or the working directory). Every library is checked at run time: major version equal to the build's headers, minor not older, licence LGPL (`avutil_license()` etc.), no `--enable-gpl` or `--enable-nonfree` in the configuration; a failure leaves FFmpeg off with the reason. The `ffmpeg` provider opens its own `AVFormatContext` with `protocol_whitelist=file`. It accepts any container FFmpeg demuxes (AVI, MKV, MOV, MP4, NUT, …) but decodes only lossless video codecs (U-FF, §7.2); lossy codecs (H.264, HEVC, MPEG-1/2/4, …) are out of scope for v1.0. `lavfi:` graphs have command-line trust, but only the generators and simple filters of an allowlist (31 filters, §7.2), so no graph opens a file or URL. libvmaf only in v1.x |
| VMAF | v1.x (with the metrics): optional libvmaf (BSD-2-Clause-Patent) loaded at run time; a Vulkan port is a v2 learning project |
| JSON | **nlohmann/json** (MIT) behind a small `dvv::json` wrapper |
| Tests | **doctest** (MIT) + CTest |
| Licence | Project MIT; SPDX manifest; build fails on any licence outside an allowlist (§13.2) |
| Namespace | `dvv::` for reusable libraries; the application is **rawreel** (`rawreel-gui`, `rawreel-cli`) |

Every optional run-time library degrades gracefully: missing → the dependent components are disabled with
a reason (in v1.0 FFmpeg's reason is shown by `rawreel-cli --version` and Help → About; the
Capabilities page is v1.x); nothing fails at startup except a missing Vulkan 1.4 driver.

---

## 3. Architecture

### 3.1 Layers and repositories

All repositories live side by side in one local workspace folder (its location is never written into any
file). Dependencies point only downward; platform code is reached only through dvv-platform.

| Repo | Contents | Depends on |
|---|---|---|
| `dvv-base` | `Error`, `co_try`, `CreationKey`/`Managed<T>` and the lifetime registry (§9.4), logging, coroutine `Task`/executors/thread pool, `dvv::json`, URI parser, platform paths API, licence manifest helpers | – |
| `dvv-registry` | Component descriptors, factories, self-registration, capability probing, `ParameterSchema` | base |
| `dvv-platform` | `NativeWindow` (Win32, Xlib) with `surfaceInstanceExtensions()`/`createSurface()`, `DynamicLibrary`, and the test-only `TestWindow` (`dvv::platform_testing`) since M1b; `FileReader` (a concrete move-only handle for positional reads, M2); win32 and x11 now, wayland and cocoa later | base, Vulkan-Headers (C headers only) |
| `dvv-gpu` | Vulkan loader, `GpuRegistry`, `ExecutionContext`, `GpuBuffer`/`GpuImage`, `ResourcePool`, submitter, pipeline cache, Slang integration, `Presenter`; images reach shaders through push descriptors (no bindless heap) | base, platform (since M1b: loader, surfaces) |
| `dvv-media` | Pixel-format engine + catalogue + CPU reference, custom formats as JSON, `FrameStore` + `IFrame` implementations + converters, `ISource`/`ISourceProvider` + `SourceFactory`, core source providers (synthetic, raw, Y4M) in the component library `dvv_media_sources` (D1) | base, registry, gpu, platform |
| `dvv-components` | All built-in components, one target and one CMake option each (`dvv_add_component`); in v1.0 one component, `ffmpeg` (M4), with the source providers `ffmpeg` (container files) and `lavfi` (`lavfi:` graphs); FFmpeg is loaded at run time, never linked | media, gpu, registry |
| `rawreel` | `rawreel_core` (no wx: the `Session` app-state actor, player logic, fit view transform, command table and key normaliser, format choice, drop routing, GUI argument parsing, status and info text), `rawreel_render` (no wx: the fit compositor), `rawreel_gui` (wx), `rawreel_cli` | all of the above + wx |
| `rawreel-workspace` | Workspace CMake presets, bootstrap scripts, third-party superbuild, shared docs (specs, plans) | – |

The dependency graph after M3:

```text
dvv-base ─┬─ dvv-registry
          ├─ dvv-platform ── Vulkan-Headers (C headers only)
          └─ dvv-gpu ── dvv-platform, Vulkan-Headers, VMA, vk-bootstrap (source)
dvv-media ── dvv-base, dvv-registry, dvv-gpu, dvv-platform
rawreel ── rawreel_core (no wx): dvv-media, dvv::media_sources
           rawreel_render (no wx): rawreel_core, dvv-gpu
           rawreel-gui: rawreel_render, wxWidgets core, base and aui (static), [Linux: GTK3, X11]
           rawreel-cli: dvv-media, dvv::media_sources
```

Each dvv repo installs and exports a CMake package (`dvv::gpu`, …) and is versioned by SemVer git tags.
Consumers use

```cmake
FetchContent_Declare(dvv_gpu GIT_REPOSITORY … GIT_TAG v0.3.0 FIND_PACKAGE_ARGS 0.3)
```

The workspace preset sets `FETCHCONTENT_SOURCE_DIR_DVV_<NAME>` to the sibling folders (relative paths),
so the whole workspace opens as one Visual Studio solution and edits across repos need no release.

Keeping several repos cheap for one developer:
- The workspace and CI presets set `FETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER`, and a configure check
  fails unless every dvv dependency resolved to its sibling folder (no installed package shadows it).
- One `dvv-versions.cmake` in the workspace declares every dvv dependency first, so CMake's
  "first declaration wins" rule picks one version deliberately in diamonds (components → media, gpu).
- Each repo also defines its `dvv::<lib>` ALIAS target in-tree, so in-tree and installed use look the same.
- Layering is enforced by target dependencies; one reusable CI workflow serves all repos (§13.4); no tags
  or releases before v1.0 — until then every repo builds against `main` through the workspace.

### 3.2 Headless core
Nothing below `rawreel-gui` depends on wx. Components describe parameters with `ParameterSchema`
(rendered by wxPropertyGrid in the GUI, parsed from `--param` in the CLI) and interactive tools (v1.x)
return overlay draw commands in image coordinates; the viewport renders them. In rawreel the
wx-free part of the application is the `rawreel_core` library (§3.1, §10.1).

---

## 4. Threads and data flow

| Thread | Owns / does | Never |
|---|---|---|
| GUI (wx main) | Widgets, input; sends commands to the Session; receives events via `wxQueueEvent` | Blocks, touches Vulkan queues or frame data |
| Session | Serial executor (actor) owning all app state: streams, player, selected components, results; MPSC command queue | Does heavy work itself |
| Timer (one per Session) | `ThreadTimer`: ticks the media clock and posts each tick to the Session executor; on Windows a high-resolution waitable timer (no process-wide `timeBeginPeriod`) | Touches app state |
| I/O workers | A thread pool running blocking positional `FileReader::readAt` calls, one at a time per file (M2 D8; M3.1 U-IO2): unbuffered and sector-aligned on Windows where the volume allows it, else buffered through the OS file cache with a sequential-access hint (Windows: a synchronous handle, `ReadFile` with the offset in an `OVERLAPPED`; Linux: `posix_fadvise(POSIX_FADV_SEQUENTIAL)` + `pread`), into host frames | Allocate unbounded memory, touch the GPU |
| GPU submit (one per device) | All queue submits of its device; hazard tracking; timeline semaphores | Waits on CPU-signalled events, acquires or presents |
| GPU waiter (one per device) | Resumes coroutines when timeline values are reached | – |
| Presenter (one per viewport) | Swapchain acquire and present (may block under FIFO); swapchain recreation | Records or submits rendering work |
| FFmpeg source worker (one per `ffmpeg`/`lavfi` source, M4) | A `ThreadPool(1)`: demux, decode or filter graph, the ring of decoded frames, the copy into the host frame; checks the source's `stopping` flag before each step | Touches the store except through `ProduceContext` |
| CPU pool | Aggregation, CPU fallbacks; libvmaf and export in v1.x | – |

- Coroutines: `Task<T>` with explicit executors; `co_await gpu.submit(…)` resumes on the waiter, which
  posts the continuation to the awaiting executor (never inline). The task layer is capped at `Task<T>`,
  `when_all`, executors and `stop_token` integration — no generators or custom allocators in v1.
- The record function passed to `submit` runs on the submit thread and only records commands: it
  must not block or wait for another submission, since every later submission of the device waits
  behind it.
- Queue access from the submit and presenter threads is serialised by a per-`VkQueue` mutex.
- Presenter handshake (M1b). Per frame the presenter thread acquires a swapchain image with a fence
  and waits for it on the CPU, calls the client's frame provider, hands one submission to the submit
  thread (the client's commands, a blit of its image into the swapchain image, the transition to
  `PRESENT_SRC`), and once that completes calls `vkQueuePresentKHR` without wait semaphores under
  the queue mutex. So no batch waits on a semaphore, the presenter never records or submits, and
  exactly one frame is in flight. The GUI calls `resize`, `setVisible` and `requestFrame` from any
  thread; `resize` is only a hint: the surface's `currentExtent` decides the size (the hint counts
  only when it is `0xFFFFFFFF`; 0×0 means hidden). The presenter recreates the swapchain lazily between frames
  (passing `oldSwapchain`) on a size change or `VK_ERROR_OUT_OF_DATE_KHR`/suboptimal, and destroys
  the old one after `vkQueueWaitIdle` under the queue mutex; this replaced "old images retire
  through the context" (exact retirement needs `VK_KHR_swapchain_maintenance1`, §15). The GUI never
  touches the swapchain. Destroying a presenter blocks the GUI for about one frame plus a queue-idle
  wait; on Windows the wait dispatches sent messages only.
- Every requester of the `FrameStore` resumes on its own executor (`IExecutor& resume_on`, §6.5),
  never inline; the store hops to its CPU pool before every produce, and file sources hop to the I/O
  pool themselves.
- **The player (M3).** The Session executor runs the player logic (`PlayerLogic`) and the media
  clock, driven by the timer thread. The clock advances only when the frames of **all** active
  streams are ready (no frame drops; a slow frame stalls and re-anchors the schedule). Prefetch
  requests display views ahead of the play head in a fixed window `W = clamp(budget / (2 · 8 · w ·
  h · streams), 2, 8)` frames (the device tier's budget; halved once on `OutOfMemory`), with at most
  K = 4 requests in flight. Requests are tagged with the stream set and the GPU stack epoch; a
  result of an older seek, stream or stack is dropped as stale instead of being cancelled
  (`stop_token` cancellation is v1.x, X4: the M3.1 4K60 run measured seeks of at most 122.5 ms, so
  Task C was not triggered; adaptive, direction-aware prefetch is v1.x). Frames reach the viewport
  through `IDisplaySink`
  (the GUI's `DisplayState`, read by the presenter thread); discrete events (`streamsChanged`,
  `formatNeeded`, `stackChanged`, `error`) go through `ISessionEvents` and are posted to the GUI
  thread; the GUI polls `status()` every 50 ms for the status bar: target vs actual fps and the
  current bottleneck, derived from the store's converter stats (disk / upload / unpack / display).
- **FFmpeg sources (M4).** Each `ffmpeg` or `lavfi` source owns one worker, a `ThreadPool(1)`, that
  runs every demux, decode and filter step and keeps a small ring of decoded frames; concurrent
  `produce` calls (prefetch) queue on it. Each source has an atomic `stopping` flag: its destructor
  sets it before joining the worker, and the worker checks it (and the process-wide
  `stopFfmpegSources()` generation) before each step, so the current and every queued `produce`
  end with `Internal` "source closed". The GUI calls `stopFfmpegSources()` before the Session
  shutdown, which bounds the exit time; closing a stream and Reopen drain the old sources in the
  background.

---

## 5. GPU core

### 5.1 Vulkan baseline
- **Vulkan 1.4** devices. The instance requests API 1.4 but accepts a loader ≥ 1.3 (Ubuntu 24.04 ships
  loader 1.3.275, which drives a 1.4 device): vk-bootstrap `set_minimum_instance_version(1, 3, 0)` and
  `require_api_version(1, 4, 0)`. A device below 1.4 is listed but not usable. Required features are
  checked at startup and logged (Help → GPU info; the Capabilities page is v1.x): timeline
  semaphores, synchronization2, dynamic rendering, buffer device address with `shaderInt64`
  (64-bit addresses in shaders), descriptor indexing, 16-bit storage, maintenance5/6, since M2
  `pushDescriptor`, `shaderStorageImageExtendedFormats` and `shaderStorageImageWriteWithoutFormat`,
  and since M3 `shaderStorageImageReadWithoutFormat` (ToDisplay reads the unpacked planes).
  The six descriptor-indexing features stay required, kept for a possible bindless heap (F12) and user
  shaders. `R8_UINT` and `R16_UINT` must support `STORAGE_WRITE_WITHOUT_FORMAT` (the unpacked planes,
  §6.3; checked when the unpack pipeline is set up, `Unsupported` otherwise) and, since M3,
  `STORAGE_READ_WITHOUT_FORMAT` (checked when the ToDisplay pipeline is set up, `Unsupported`
  otherwise; NVIDIA has both, so the typed `r8ui`/`r16ui` fallback was not needed). `shaderInt16`/
  `shaderFloat16` are optional (shader variants without them exist); `hostImageCopy` and
  `VK_EXT_memory_budget` are used when present.
- Missing loader or a device below 1.4 → a clear startup dialog (GUI) or exit code 4 (CLI).
- **Validation** modes `off`, `on`, `sync` (synchronization validation), from the options or the
  environment variable `DVV_VK_VALIDATION`; the default is `on` in Debug builds when the layer is present.
  Only `VK_LAYER_KHRONOS_validation` ≥ 1.4 counts: an older one (e.g. a system's 1.3) does not know
  Vulkan 1.4, reports false errors and is treated as absent. The tests load the layer built from source
  in the third-party prefix (§13.2). `DVV_VK_VALIDATION_REQUIRED=1` turns a missing layer into an error
  (CI). The messenger routes every message to the dvv logger and counts errors of any type and warnings
  of the validation and performance types; tests fail on a count above zero.
- Synchronization validation cannot see buffer-device-address accesses, so the command recorder inserts
  barriers itself (§16).

### 5.2 Devices
- One `VkInstance`; `GpuRegistry` enumerates GPUs, records capabilities and ranks them (usable first;
  discrete > integrated > virtual > CPU; then device-local memory). It reports them as `GpuInfo`;
  dvv-gpu does not depend on dvv-registry, so a higher layer turns `GpuInfo` into registry
  `Capabilities`. `dvv-gpu-info` prints them (text or `--json`; exit 0 with a usable device, 1 on bad
  arguments, 4 on a GPU error such as no Vulkan loader or no usable device). The JSON form,
  `toJson(const GpuInfo&)`, has its own header `dvv/gpu/gpu_info_json.h`, so `dvv/gpu/gpu_info.h` does
  not pull in the JSON library.
- One `VkDevice` per used GPU, wrapped in an **`ExecutionContext`**: queues, VMA allocator, on-disk
  pipeline cache (per-user cache dir, §5.4), `ResourcePool`s, submitter. Components reach the GPU **only** through an `ExecutionContext`.
- v1: one active GPU, selectable at start (`rawreel-gui --gpu N`, the index `dvv-gpu-info` lists;
  default the best usable one). The Preferences dialog and a GPU menu are v1.x, as is placement of
  streams/work across GPUs.
- **Queues:** v1 uses one universal (graphics + compute + transfer) queue — lavapipe exposes only one, so
  this is the path CI can test. The M2 `bench` kept it: its bottleneck was the file read, not the
  upload (§16). A dedicated transfer queue is an option for when the upload becomes the bottleneck;
  async compute is v1.x.
- **Device lost** (M3: detect, report, user reopen). A context enters a permanent lost state
  (`ExecutionContext::lost()`) when a Vulkan call returns `VK_ERROR_DEVICE_LOST` (submit, timeline
  wait or counter query, idle wait, a presenter's acquire, fence, present or queue-idle call) or
  when `dvv::gpu::testing::injectDeviceLost(context)` is called (in the main library, so the shipped
  `rawreel-gui --selftest lost` can use it). Submissions fail fast: pending and later ones complete
  with `DeviceLost` (later ones are not recorded); the creators (`createBuffer`, `createImage`,
  `createResourcePool`, `createComputePipeline`, `createPresenter`) return `DeviceLost`; the submit
  and waiter threads wait in bounded slices (1 s) and stop waiting once the device is lost; a lost
  context is destroyed safely (its idle wait's `DeviceLost` ignored, the pipeline cache not saved)
  and is never reset. The application creates a new context. rawreel's Session pauses, releases
  its GPU stack (context and store) and reports `error(DeviceLost "GPU device lost: …. Restart the
  GPU to continue.")`; the GUI shows it in the infobar with a Restart GPU button (the menu item
  View → *Restart GPU and reopen*, command id `reopen`; "Reopen" below means this command). Reopen
  rebuilds the stack and reopens the stream through the normal open path at the frame it showed,
  paused; after a failed open (its stack build failed, no stream kept) it opens that request
  again. The first stack
  is built synchronously in `Session::create` (a startup GPU failure exits 4); every later stack
  (Reopen, an open after a loss) is built on the I/O pool as a job of the open FIFO. A Session
  stack epoch tags every request, so results and loss reports of an old stack are ignored.
  Automatic recovery and the GPU menu are v1.x; `--gpu N` keeps "one active GPU, selectable". The
  device generation stays 1 in v1.0 (each reopen makes a new context with its own device);
  automatic recovery (v1.x) recreates the context's device with the next generation, invalidates
  views of the old generation and rebuilds them lazily.

### 5.3 Resources
- `GpuBuffer` / `GpuImage` (dvv-gpu): RAII over a VMA allocation + handles + debug name + device
  generation + buffer device address (buffers) or image view (images with `STORAGE` or `SAMPLED`
  usage: 2D, the whole image, retired with the image). The C++ object dies with its last owner; its
  destructor moves the raw handles and the VMA allocation into the context's retire queue, which frees
  them once all retire points have passed (the retire queue has its own counters, §9.4).
- `ResourcePool`: transient buffers/images reused by `(size, format, usage)`, returned after retire.
  It pools buffers only, keyed by `(size, usage, memory)`; images are not pooled yet: frame entries
  are cached and evicted as wholes, and image pooling comes when measured (F10, §15). A pool is created
  by `ExecutionContext::createResourcePool()` and keeps its context alive. `acquireBuffer()` hands out a
  lease (a `shared_ptr` whose release returns the buffer to the pool); each buffer is created and
  registered once (§9.4). A returned buffer is tagged with the latest timeline value reserved at its
  return and handed out again only after the GPU has finished that submission, so it is never reused
  while a submission made or being recorded at its return may still use it. Shaders reach buffers by
  device address, so it is the lease, not the command recorder, that keeps a pooled buffer from being
  reused: users keep the lease alive (e.g. captured in the record function) until the submission that
  uses it has completed.
- Non-frame GPU resources: swapchain images (`Presenter`, one per viewport), composite targets; later
  overlay targets, glyph atlas (rasterised by wx, uploaded once), colour LUTs, tone-map curves,
  histogram and metric reduction buffers, pipette probe buffers (v1.x).
- Rule: anything that may be shown, analysed or saved is a **frame** (§6) and lives in the `FrameStore`;
  everything else is a resource.

### 5.4 Shaders and pipelines
- Built-in shaders are Slang modules compiled by `slangc` at build time to SPIR-V, embedded as generated
  C++ arrays; the reflection JSON is turned into `ParameterSchema` code. M1a embeds the reflection JSON
  next to the SPIR-V (`dvv::gpu::EmbeddedShader`); the conversion to a schema arrives with the first
  component that needs it (§15), because dvv-gpu does not depend on dvv-registry. Until then the C++
  side declares a shader's push-constant struct by hand.
- `dvv_add_shaders(<target> [NAMESPACE <ns>] SHADERS <file.slang>…)` (dvv-gpu, §13.2) generates one
  header per shader, `<ns as a path>/<name>.h`, declaring `const dvv::gpu::EmbeddedShader&
  <ns>::<name in camelCase>() noexcept`. The namespace defaults to `<target>_shaders`, so the headers of
  two libraries never collide. Each shader has one compute entry point `main`; two shaders of a target
  that give the same function name, or a name that is a C++ keyword, fail configuring. The target must
  use the keyword form of `target_link_libraries()` (the function links `dvv::gpu` `PUBLIC`), and the
  generated headers exist in the build tree only; they are not installed with the target.
- Compute pipelines (`ExecutionContext::createComputePipeline(shader, push-constant size)`, managed)
  take their SPIR-V chained into the pipeline create info (maintenance5, no `VkShaderModule`), have one
  compute push-constant range at offset 0 (none for a push-constant size of 0) and buffers by device
  address only. The push-constant size must be a multiple of 4
  and cover the shader's push-constant block in its reflection JSON; a size above the device's `maxPushConstantsSize` is `Unsupported` (§11).
  `CommandRecorder::dispatch` takes exactly that many push-constant bytes and group counts within the
  device's limits. A pipeline and its layout retire like resources (§5.3).
- **Pipeline cache:** one `VkPipelineCache` per context, kept in the file `ContextOptions` names (empty:
  in memory only). The default, `defaultPipelineCacheFile(app, device)`, is one file per device,
  `vulkan-pipelines-<device UUID>.bin` in the per-user cache directory, since a cache only fits the
  device and driver build that wrote it. The file holds the driver's data followed by a dvv trailer
  (data size, checksum, magic). Before the data reaches the driver, the header is validated (header size
  exactly 32, header version one, vendor id, device id, pipeline-cache UUID) and the trailer checked; a
  corrupt, truncated or foreign file is ignored with a log line, and data the driver still rejects is
  replaced by an empty cache. Saving is atomic: each save writes a temporary file of its own and renames
  it over the cache file (concurrent savers leave one complete file, last writer wins). The cache is
  saved by `savePipelineCache()` and when the context closes, after its threads have stopped; on
  close only when its data changed since it was loaded or last saved, or the file is gone.
- User shaders are compiled at run time by the dynamically loaded Slang library; reflection generates the
  parameter UI; diagnostics with line numbers go to the shader panel; the last working version stays
  active on error.
- **Images reach shaders through push descriptors** (Vulkan 1.4 core; M2 D6), not a bindless heap.
  A pipeline declares its storage images with `ComputeBindings{storageImages}`
  (`createComputePipeline(shader, push-constant size, bindings)`): set 0, bindings
  0..storageImages-1, a descriptor-set layout with the `PUSH_DESCRIPTOR` flag; more images than the
  device's `maxPushDescriptors`, or reflection bindings other than those, are `Internal`. The
  `dispatch` overload that takes the images checks them (this device and generation, `STORAGE` usage,
  each once), moves each image that is not yet in `GENERAL` there in the automatic barrier (tracked
  like every image layout), pushes the descriptors (`vkCmdPushDescriptorSet`, set 0) and records the
  dispatch. Shaders declare `[[vk::binding(i, 0)]] [format("unknown")] RWTexture2D<uint>`, which
  slangc emits as `ImageFormat Unknown` (hence `shaderStorageImageWriteWithoutFormat`, §5.1).
  Synchronization validation sees these image accesses.
- Shaders receive buffers by device address (push constants) and images through push descriptors,
  plus the format descriptor parameters; there are no per-frame descriptor sets.
- **One generic unpack shader** (dvv-media `unpack.slang`) driven by push constants computed from the
  format descriptor, one dispatch per output component. Each invocation loads the two dwords around
  its sample's word at the absolute byte address and shifts, so planes and words need no alignment
  beyond a 4-aligned frame base; a buffer read by the unpack holds at least `offset + alignUp(bytes,
  4) + 4` bytes (checked, `Internal` otherwise). Specialisation-constant fast paths for common
  formats come only when measured.
- A bindless descriptor heap is F12 (v1.x, if ever): only if push-descriptor limits bind, e.g. user
  shaders with many inputs.

---

## 6. Frame model

### 6.1 Content vs representation

```cpp
namespace dvv {
struct FrameKey  { std::uint64_t origin; std::int64_t index; /* == , hash */ };
struct FrameDesc { FormatId format; ColourId colour; std::uint32_t width, height; };  // never "where"

enum class Api     : std::uint8_t { HostPlanes, VkBuffer, VkImage };   // AvFrame: v1.x (M4 D4)
enum class MemKind : std::uint8_t { Host, HostVisible, DeviceLocal };
struct Domain   { MemKind kind; DeviceId device; std::uint32_t generation; };   // generation: §5.2
enum class LayoutPolicy : std::uint8_t { Any, Tight, RowAlign256, VkOptimal };
struct ViewSpec { FormatId format; ColourId colour; LayoutPolicy layout; Api api; Domain domain;
                  std::uint8_t downscaleLog2 = 0; };        // (FrameKey, ViewSpec) = dedup key
enum class Fidelity : std::uint8_t { Exact, Lossless, Derived };
struct SyncPoint { DeviceId device; std::uint8_t timeline; std::uint64_t value; };
}
```

- `FrameKey.origin` is a source id (raw/decoded/generated frames) or `hash(processor id, processor
  version, parameters, input keys)` for processor outputs.
- `PixelFormatDescriptor` and colour parameters are interned (`FormatId`, `ColourId`).
- `ColourParams` (matrix, range, transfer, primaries) records what the stream uses. The primaries
  are information only: the display path converts no gamut (§8, U-D5-7).
- Residency is only a value inside `ViewSpec`; `IFrame` has no residency concept. "Unpacked planes" and
  "display image" are simply views with a different format/layout.

### 6.2 `IFrame` implementations

```cpp
struct HostPlanesB; struct VkBufferB; struct VkImageB;   // binding payloads (AvFrameB: v1.x)
using Binding = std::variant<HostPlanesB, VkBufferB, VkImageB>;

class IFrame {
public:
    virtual ~IFrame() = default;
    virtual const FrameDesc& desc() const noexcept = 0;
    virtual std::span<const ViewSpec> nativeViews() const noexcept = 0;  // zero-copy views offered
    virtual Binding bind(const ViewSpec& native) const = 0;              // no I/O, no GPU work
    virtual std::size_t bytes() const noexcept = 0;
};
```

| Implementation | Native views |
|---|---|
| `HostFrame` (M2) | HostPlanes / Host: aligned host memory; the frame may start at an offset inside it (a Y4M frame after its `FRAME` marker) |
| `StagingFrame` (M2) | One host-visible buffer. In `HostReadback` memory (made by Readback): HostPlanes / HostVisible(d) **and** VkBuffer / HostVisible(d) — same bytes, counted once. In `HostUpload` memory (made by HostToStaging): only VkBuffer / HostVisible(d), so the CPU never reads write-combined memory |
| `VkBufferFrame` (M2) | VkBuffer / DeviceLocal(d): the original, tight, in a padded buffer (§5.4) |
| `VkPlanesFrame` (M2) | VkImage / DeviceLocal(d): the unpacked planes, one image per component (§6.3) |
| `MappedFrame` (v1.x) | HostPlanes / Host (read-only file mapping slice) |
| `AvFrameFrame` (v1.x) | AvFrame / Host and HostPlanes / Host; later VkImage for FFmpeg Vulkan decode |

Vulkan implementations are thin adapters holding `shared_ptr<GpuBuffer|GpuImage>` (§5.3). The M2
bindings are `HostPlanesB`, `VkBufferB` and `VkImageB`. `Api::AvFrame`, `AvFrameB` and
`AvFrameFrame` are v1.x (M4 decision D4): the FFmpeg sources copy each decoded frame into a
catalogue original (`HostFrame`), so the store sees no FFmpeg type.
Components never hold an `IFrame`; they hold a **`FrameRef`** (key + desc + store).

### 6.3 Representations on the GPU

| View | Resource | Reason |
|---|---|---|
| Original (any packing) | `VkBuffer` storage buffer, accessed by buffer device address | Most raw formats have no `VkFormat`; shaders decode via the descriptor; byte-level tools read it |
| Losslessly unpacked | One `VkImage` per component (padding is not content), `R8_UINT` for ≤ 8 bits and `R16_UINT` for 9–16 bits, at the component's subsampled size, LSB-aligned code values, layout `GENERAL` at rest | Uniform for every format; code values kept exactly — the default analysis input (§6.6) |
| Display (M3) | `VkImage` `RGBA16F` (`R16G16B16A16_SFLOAT`) at frame size, linear light, BT.709 primaries, storage usage, layout `GENERAL` at rest; format `rgba16f@display`, colour `{rgb, full, linear, bt709}` | Input of the fit compositor (§10.2); zoom and overlays v1.x |

With one universal queue (§5.2) frame resources use `VK_SHARING_MODE_EXCLUSIVE` (no ownership transfers,
no loss of image compression). If a dedicated transfer queue is enabled, uploads use explicit queue-family
ownership transfers. A published image rests in one read layout; readers never transition it (the
unpacked planes rest in `GENERAL`, written as storage images and readable as storage or sampled
images). The host form of the unpacked view is planar, tight and little-endian: 8- or 16-bit
containers, planes in component order (Y U V A / R G B A / Y); for v210 it is byte-identical to an
FFmpeg `yuv422p10le` frame. The unpacked format is a derived descriptor `<name>@planes`.

### 6.4 Converters and processors

The M2 converters:

| Converter | From → to | Fidelity |
|---|---|---|
| HostToStaging | original HostPlanes/Host → original VkBuffer/HostVisible(d) (`memcpy` on the CPU pool into `HostUpload` memory, then `flush()`) | Exact |
| Upload | original VkBuffer/HostVisible(d) → original VkBuffer/DeviceLocal(d) (universal queue) | Exact |
| GpuUnpack | original VkBuffer/DeviceLocal(d) → unpacked VkImage/DeviceLocal(d) (§5.4) | Lossless |
| Readback | VkBuffer or VkImage/DeviceLocal(d) → HostPlanes/HostVisible(d) (`HostReadback` memory, then `invalidate()`) | Exact |
| CpuUnpack | original HostPlanes/Host or HostVisible → unpacked HostPlanes/Host (the CPU reference; the CPU fallback and the `--via cpu` path) | Lossless |

`StagingAlias` needs no code: a `HostReadback` `StagingFrame` offers both views natively (§6.2).
Cost model: bytes moved times a factor (host copy 1, PCIe 2, GPU-local 0.5, CPU unpack 4), so the
GPU wins for device targets and the CPU for host targets.

M3 adds `ToDisplay`: unpacked VkImage/DeviceLocal(d) → display VkImage/DeviceLocal(d) (§6.3),
Derived, the store's one colour-changing edge. Its shader reads the planes as storage images
without a format, takes chroma from the nearest sample and applies the colour model of §8; the
planner adds it after GpuUnpack, and `maxPlanDepth` is 6. A host-only store has no path to the
display view (`Unsupported`).

Later converters: `Downscale` (display → display at 2^-n, Derived) and `HostImageCopy` (HostPlanes → VkImage
with Vulkan 1.4 `hostImageCopy`, Exact) later, when needed; `WrapAsAvFrame`/`AvFrameToHost`
(HostPlanes ↔ AvFrame aliases, Exact) in v1.x (M4 D4: decoded frames are copied); `PeerCopy` (DeviceLocal(d0) → DeviceLocal(d1) via host,
Exact) in v1.x. A dedicated transfer queue for Upload is optional (§5.2).

A converter keeps the `FrameKey`. A processor creates a new `FrameKey` with its own original format;
its output can be shown, analysed and saved like any other frame.

### 6.5 `FrameStore`

```cpp
class FrameStore final : public Managed<FrameStore>, public std::enable_shared_from_this<FrameStore> {
public:
    static Result<std::shared_ptr<FrameStore>> create(FrameStoreOptions,
                                                     std::source_location = current());
    Result<OriginId> attach(std::shared_ptr<ISource>);  // the store owns the source until detach
    void detach(OriginId);
    Task<Result<ReadLease>>    acquire(FrameKey, ViewSpec want, IExecutor& resume_on,
                                       std::source_location = current());
    Task<Result<OriginalRead>> acquireOriginal(FrameKey, Api, Domain, IExecutor& resume_on,
                                               bool requireExact = false,
                                               std::source_location = current());
    Task<Result<PinLease>>     pin(FrameKey, ViewSpec, IExecutor& resume_on,   // prefetch window
                                   std::source_location = current());
    Result<ReadLease> adopt(const FrameKey&, std::shared_ptr<IFrame>, SyncPoint ready = {},
                            std::source_location = current());               // I/O, decoder
    StoreStats stats() const;                // tiers, counters, StoreStats::converters
    std::vector<LeaseInfo> outstandingLeases() const;
    void trim(Domain, std::size_t targetBytes);
    Domain hostDomain() const;
    std::optional<Domain> stagingDomain() const, deviceDomain() const;  // with a GPU context
    // Internal until processors arrive (v1.x): acquireAs<B>, create → WriteLease, publish.
};
```

The public API in M2 is the one above; `acquireAs`, `create` and `publish` exist internally (converters
use them) and become public with processors in v1.x. `acquire`, `acquireOriginal` and `pin` take the
key and the view by value, because the tasks are lazy and a reference to a temporary would dangle.
Every requester resumes on its own `resume_on` executor, never inline; it must outlive the operation.
`attach`/`detach` give the store the producer of a key's original (§7.1); the source never holds the
store. `FrameStoreOptions` names the CPU and I/O executors, the optional `ExecutionContext` (none: a
host-only store) and the byte budgets per tier (host 2 GiB, staging 1 GiB, device half of the
device-local memory by default; `VK_EXT_memory_budget` budgets are v1.x).

- **Single source of truth.** One index `(FrameKey → record → entries)` under **one store mutex**
  serves dedup, single-flight creation, lease accounting, monitoring and the RAM/VRAM cache tiers (one
  LRU tier per domain/device with a byte budget from `FrameStoreOptions`; `VK_EXT_memory_budget` and
  Preferences are v1.x). The mutex is never held across `co_await`, a post, a produce, a converter body,
  a frame release or a logger call. A sharded index comes only when contention is measured.
- **Dedup and single-flight.** A request returns an existing view that satisfies the spec (same format,
  colour, api and domain; layout `Any` matches all; preferring Exact > Lossless > Derived; never
  Derived for an original-format request), joins one in creation, or plans the cheapest converter path
  (a bounded relaxation over at most `maxPlanDepth` steps) over the store's **static converter graph** (host-only or with a
  GPU, fixed at `create`), with the key's **published and pending** entries and Produce as sources,
  inserting pending entries first. Waiters are resumed on their own executor. Failures are not cached,
  except the permanent "no converter path", cached by (original format, colour-equal, target state)
  and never by what exists. The device generation is part of the key (`Domain::generation`).
- **Cancellation** is not built yet: the M3 player drops stale results instead (§4). A
  `std::stop_token` that cancels only the caller's wait (the shared creation stops when no waiter
  is left) is v1.x (X4): M3.1 Task C ran only if the 4K60 run measured `seekMaxMs > 250`, and the
  run measured at most 122.5 ms.
  M2 keeps the hook: waiters are removable nodes and pending creations count their waiters.
- **Converter stats.** Each converter, and Produce, keeps relaxed counters (`runs`, `bytes`,
  `nanoseconds`), reported in `StoreStats::converters`; they prove the path a view took (CLI `--via`)
  and feed `bench`.
- **Immutability.** A frame is writable only until it is published (`WriteLease` → `publish`); afterwards
  it and all its views are read-only and lock-free for readers. Changing content means a new frame.
  Locks remain only inside the store (shards, LRU lists) and in reusable scratch buffers, and are never
  held across `co_await` or a callout.
- **GPU sync.** GPU conversions complete before an entry is published: a converter `co_await`s its
  submission, so every published view's `ready` `SyncPoint` is already reached, and there is no
  `lease.retire(lastUse)`. A lease keeps its resources alive by shared ownership; a reader captures the
  lease (or its `shared_ptr<GpuImage>`) in its record function, the `ResourcePool` rule of §5.3. The
  `SyncPoint` stays in the data model for later pipelining (F4). Barriers come from the command
  recorder (§16).
- **Eviction.** An entry with readers or pins is never evicted. Derived views go first, then Lossless,
  then Exact, each in LRU order; a failed allocation evicts the tier to half its budget and retries
  once, then reports `OutOfMemory`; `trim(domain, targetBytes)` evicts on request.
  Source frames are simply produced again by their `ISource` (this is what makes files larger than RAM
  work). Processor outputs (v1.x) are recomputed from their recipe when the inputs are deterministic;
  only otherwise is the last Exact copy spilled to host, under a byte budget with backpressure when full
  — never silently dropped. The detailed spill policy is settled with processors in v1.x.
- **Monitoring.** Always-on relaxed atomic counters per domain/device (bytes, frames, views, leases, pins,
  hits/misses, creations); a sharded registry of live frames/views tagged with the creating
  `source_location`; a debug panel; `outstandingLeases()`; a leak check at shutdown; trace hooks behind a
  macro. This is the frame-specific part of the lifetime registry (§9.4); leases are accounted here, not
  as managed objects.
- **Deadlock rules.** Fixed lock order; acyclic converter paths; the GPU never waits on CPU signals;
  blocking is forbidden on the GUI, Session and submit threads.

### 6.6 The analysis rule
Analytics, probing, statistics, histograms, metrics, analysers and the pipette run **only on the original
code values in the original colour domain**. This is enforced by type:
- These entry points accept only `OriginalRead`, which only `FrameStore::acquireOriginal` can create; its
  colour is forced to the frame's own, and it is built only through Exact or Lossless paths.
- **Default input is Lossless:** the unpacked planes (§6.3) carry exactly the original code values, are
  created once and shared by every analyser, so no analyser shader has to decode v210, big-endian or
  bit-packed layouts. Byte-level consumers (frame hash, raw export, bit inspector) pass
  `requireExact = true` and receive the original bytes.
- Display conversion is always `Derived` and can never become an original.
- A metric may convert internally as part of its definition (e.g. CIEDE2000 → Lab); its input is still
  the original.
- Two streams with different formats are compared only after an **explicit, user-chosen** conversion of
  one of them. That conversion runs as a built-in processor (new `FrameKey`, so its output is a legitimate
  original for the metric) and is shown in the stream's UI; results are labelled, e.g. "computed in yuv420p10, B converted from rgb48le". Nothing is
  ever converted silently. Metrics declare the formats they support; unsupported combinations are
  disabled with the reason shown.

---

## 7. Sources and pixel formats

### 7.1 `ISource`

```cpp
struct SourceInfo {
    FrameDesc desc; Rational fps;
    std::optional<std::int64_t> frameCount;    // nullopt = unbounded generator
    SourceAccess access;                       // RandomCheap | RandomCostly | Sequential
    bool deterministic;                        // identical content on re-produce → store may evict freely
    std::string resolvedUri;                   // includes the format: reopens without guessing
    std::string provider;                      // component id
    std::vector<std::string> warnings;         // e.g. "N trailing bytes ignored"
};
class ISource {
public:
    virtual ~ISource() = default;
    virtual const SourceInfo& info() const noexcept = 0;
    // May run concurrently for different indices; Internal outside [0, frameCount), Io for read errors.
    virtual Task<Result<FrameRef>> produce(std::int64_t index, ProduceContext&) = 0;
};
```

`ISource` replaces a file-only reader and is also the store's producer for its `FrameKey.origin`
(after `FrameStore::attach`). `ProduceContext` (made by the store for one call) gives the key, the I/O
executor, `allocateHost` (an aligned host frame, the data at an offset inside it) and `publish`. A raw
frame is read into an aligned `HostFrame` covering the aligned range around it, then copied into
staging by HostToStaging; reading straight into mapped Vulkan memory is not reliable (F3, later).

| Source | URI example | Availability |
|---|---|---|
| `RawFileSource` | `file:clip_1920x1080_yuv420p10le.yuv` | Always (M2) |
| `Y4mSource` | `file:clip.y4m` | Always (M2) |
| `SyntheticSource` | `synthetic:gradient?w=34&h=17&fmt=v210` | Always (M2; scalar CPU reference patterns `gradient` and `noise`) |
| `FfmpegSource` | `file:clip.mkv` | FFmpeg present (M4; lossless video codecs only, §7.2) |
| `LavfiSource` | `lavfi:testsrc2=s=3840x2160:r=60,format=yuv422p10le` | FFmpeg with avfilter present (M4; in-process libavfilter, allowlisted filters only) |

### 7.2 `SourceFactory`

```cpp
struct SourceRequest { Uri uri; std::optional<FormatHint> hint; };
struct Match { int confidence = 0; FormatGuess guess; std::string why; };   // 0 = cannot open
struct Candidate { std::string_view component; int confidence; FormatGuess guess; std::string why; };
struct Ambiguous { std::vector<Candidate> candidates; };                       // best first
class ISourceProvider {                     // component kind Source (§9.3)
public:
    virtual std::span<const std::string_view> schemes() const noexcept = 0;
    virtual Match match(const SourceRequest&, std::span<const std::byte> head) const = 0;  // no I/O
    virtual Task<Result<std::unique_ptr<ISource>>> open(OpenTicket, const SourceRequest&,
                                                        const FormatGuess&, const OpenContext&) = 0;
};
class SourceFactory final : public Managed<SourceFactory> {
public:
    static Result<std::unique_ptr<SourceFactory>> create(ComponentRegistry&, std::shared_ptr<IExecutor> io,
                                                         std::source_location = current());
    Task<Result<std::variant<std::unique_ptr<ISource>, Ambiguous>>> open(const SourceRequest&);
    template <class T> static CreationKey<SourceFactory> issueKey(OpenTicket&&);   // for providers
};
```

`SourceFactory::create` makes one instance of every available `Kind::Source` component. A concrete
source's `CreationKey` must be issued for its own type, which only the provider knows: the factory
passes a move-only `OpenTicket` into `open`, and the provider turns it into the typed key with
`SourceFactory::issueKey<T>(std::move(ticket))`.

1. **Scheme** selects candidate providers (a bare path means `file:`); for `file:` the factory reads the
   first 4096 bytes on the I/O pool (a missing file is `Io` at once).
2. **Match:** each provider's `match()` looks only at the request and the first bytes and returns a
   confidence:

   | Evidence | Confidence |
   |---|---|
   | Y4M magic `YUV4MPEG2 ` | 100 |
   | `synthetic:` URI | 100 |
   | explicit `fmt` + size in the query, or a hint | 95 |
   | explicit clip description (`clip=@<file.json>`, M4) | 95 |
   | FFmpeg-recognised container (M4) | 90 |
   | clip description next to the file (the sidecar gives size and format, alone or with the name; M4) | 75 |
   | filename with size and format (`<name>_WxH[_fmt][_fps]`) | 70 |
   | size only (guess `yuv420p`) | 30 |

3. **Decide:** the best match opens when it has ≥ 60 and no other is within 10; a best match below 60
   or a near tie returns `Ambiguous`, best first (GUI: format dialog prefilled with the best guess;
   CLI: exit code 3 listing the candidates); no confidence above 0 → `Unsupported` naming every
   provider and its `why`.
4. `?impl=<component>` forces a provider: it opens with any confidence above 0; an unknown id is
   `Unsupported` listing the ids.

An exception from a provider's `match` or `open` is caught at the boundary (`Internal`), and the
provider is disabled for the factory's lifetime (§11). An exception from a source's `produce` fails
only that request (`Internal` "Produce threw: …", caught by the store); the provider stays enabled.

URIs use a small RFC 3986 subset parser (Windows drive paths, percent-encoding). For `file:` and
`synthetic:` the query holds parameters (`?fmt=v210&size=1920x1080&fps=50`); for `lavfi:` everything
after the scheme is passed unchanged. Source components are registered as **providers**
(`schemes()`, `match()`, `open()`); the core providers `synthetic`, `raw` and `y4m` live in dvv-media's
component library `dvv_media_sources` (linked with `dvv_link_components(<exe> dvv::media_sources)`).
Filename detection (`<name>_WxH[_fmt][_fps][_colour]`; query parameters win over the filename) is a function
inside the raw provider; the clip description sidecar (M4, below) is read by the factory and used
only by the raw provider; an `IFormatDetector` component kind is v1.x. Parameters: `synthetic:<gradient|noise>?fmt=&size=WxH|w=&h=&fps=&frames=&seed=&colour=&range=`
(defaults fps 25/1, frames 10, seed 1); `file:<path>?fmt=&size=WxH|w=&h=&fps=&colour=&range=&impl=`,
where `fmt=@<file.json>` names a custom format (§7.3; raw files only). The colour keys (below) are
the same for every provider; in a Y4M URI they override the header. URI paths become file-system paths only through
UTF-8 (`pathFromUtf8`, never the ANSI code page). Sessions store the **resolved** URI, which always
carries `fmt`, `size` and `fps`, so they reopen without guessing.

**Colour keys, the file-name token and clip descriptions (M4, D5, D5R).**
- *Colour keys* (raw, synthetic, Y4M, ffmpeg; U-D5-7): `colour=<standard>` sets matrix, curve and
  primaries from one of the six tokens `bt601-ntsc`, `bt601-pal`, `bt709`, `bt2020`, `bt2100-pq`,
  `bt2100-hlg` (§8); `range=full|limited` sets the range and is accepted with every token
  (U-D5-9). Errors (`Format`): "bad colour 'x' (bt601-ntsc, bt601-pal, bt709, bt2020, bt2100-pq,
  bt2100-hlg)" and "bad range 'x' (full, limited)".
- *Removed keys:* `matrix`, `kr`, `kb`, `transfer` and `primaries` are "unknown parameter 'matrix'
  (set the colour with colour=<standard>: bt601-ntsc, bt601-pal, bt709, bt2020, bt2100-pq,
  bt2100-hlg)" (one dvv-media helper, `unknownParameterText` in `uri_colour.h`, also used by the
  ffmpeg provider); any other name stays "unknown parameter 'x'". Custom matrices are v1.x.
- *File-name token* (raw only):

  ```text
  stem   = name "_" W "x" H [ "_" fmt ] [ "_" fps ] [ "_" colour ]
  colour = "bt601-ntsc" | "bt601-pal" | "bt709" | "bt2020" | "bt2100-pq" | "bt2100-hlg"
  ```

  The name is read from the right: a last token equal to a colour token (exact, lower case) is
  taken, then fps, fmt and WxH as before. No catalogue format or fps token equals a colour token.
  The token counts only when the whole name parses (`clip_bt709.yuv` gives nothing), gives no range
  and does not change the confidence (table above). Examples: `clip_1920x1080_bt709.yuv`,
  `clip_3840x2160_yuv420p10le_50_bt2100-pq.yuv`. The parser is public
  (`dvv::media::parseFileName`, `dvv/media/file_name.h`). Y4M names are not parsed (backlog).
- `clip=@<file.json>` (raw only) reads a clip description (§7.4); the path is used as given, like
  `fmt=@`. A bad or missing file, or `clip` without `@` ("clip needs @<file.json>"), is an open
  error. A clip without a size on a file whose name has none fails with "no frame size: <clip file>
  has no size; …". On a Y4M or container URI `clip` is "unknown parameter 'clip'".
- **The sidecar** `<file>.<ext>.json` (the raw file's full name plus `.json`, e.g. `clip.yuv.json`):
  for a `file:` URI without `clip=`, the factory reads it in step 2 together with the head (a
  missing file is no sidecar) and passes it in the request; only the raw provider uses it, so a
  `clip.mkv.json` never affects a container. It wins over the file name; the Log gets one Info line
  "clip.yuv.json describes clip.yuv" (plus " (it wins over the file name)" when the name also gave
  values). A bad sidecar is ignored with the
  warning "clip.yuv.json ignored: <reason>" (infobar and Log), and the file opens by its name. It is
  read again on every open and reopen and leaves no trace in the resolved URI.
- **Precedence** (first wins). Format, size and fps (raw): the query keys (`fmt`, `size`/`w`/`h`,
  `fps`), the request's hint, `clip=@`, the sidecar, the file name, the defaults. Matrix, curve and
  primaries: `colour=`; the file (raw: `clip=@`'s colour, else the sidecar's, else the file-name
  token; Y4M: none; ffmpeg and lavfi: the tags, mapped to a standard, §8); BT.709. Range: `range=`;
  the file (the clip JSON or sidecar `range`, Y4M `XCOLORRANGE`, the container tag or the `yuvj*`
  alias); limited for YUV, full for RGB and gray. A standard never sets the range
  (`resolveColour`, §8).
- The resolved URI keeps the query's colour form (`colour=bt709&range=full` stays exactly that) and
  `clip=` after `fps`.

**Y4M limits (M2):** header tags `W H F I A C X…`; only parameterless `FRAME` headers, so frame `i` is
at `header + i·(6 + frame bytes)` without an index scan, and each read checks the marker
(`Unsupported` "Y4M frame parameters are not supported" otherwise; an index scan is F6); a header
ending in CR LF is unsupported. `C` maps to the catalogue: `420jpeg`, `420paldv`, `420mpeg2`, `420` →
`yuv420p`, `422` → `yuv422p`, `444` → `yuv444p`, `mono` → `gray`, `420p10 422p10 444p10 420p12 422p12
444p12 420p16 444p16` → the matching `…le` formats, `mono16` → `gray16le`, others `Unsupported`; a
missing `C` means `420jpeg`. The 4:2:0 variants differ only in chroma siting, which M2 does not carry.
`XCOLORRANGE=FULL|LIMITED` sets the colour range; other `X` tags are ignored.

**FFmpeg container sources (M4, provider `ffmpeg` in dvv-components).**
- *Match:* confidence 90 when FFmpeg's content probe scores above 50 (`AVPROBE_SCORE_EXTENSION`) and
  the demuxer is not `rawvideo`, `yuv4mpegpipe` or `image2pipe`; else 0. An extension alone scores
  at most 50, so a raw `*.yuv` or `*.dat` stays with the raw provider and a Y4M file with the Y4M
  provider.
- *Stream (D5):* FFmpeg's best video stream, else the first video stream; attached pictures
  (cover art) never count. No video stream is `Unsupported` "no video stream".
- *Codec scope (U-FF):* any container FFmpeg demuxes (AVI, MKV, MOV, MP4, NUT, …), but only
  lossless video codecs. The FFmpeg codec name of the chosen stream
  (`avcodec_get_name(codecpar->codec_id)`) is checked at open, before the index scan and the
  decoder, against `rawvideo`, `v210`, `v410`, `r210`, `ffv1`, `huffyuv`, `ffvhuff`, `utvideo`,
  `magicyuv`, `png` and `qtrle`; any other codec is `Unsupported` "codec <name> is not supported in
  v1.0 (lossless codecs only)". The same list is FFmpeg's `codec_whitelist` on open, so no other
  decoder runs, not even while FFmpeg probes the streams. The list applies whatever FFmpeg build is
  bundled. H.264, HEVC, MPEG-1/2/4 and every other lossy codec are out of scope for v1.0; no
  fixture or test decodes one, and a lossy clip appears only to check the refusal.
- *Index:* the container's own index when it lists every frame (AVI, MOV, MP4); otherwise a
  demux-only scan at open (MKV, NUT), which takes longer on large files and stops when the
  application stops its FFmpeg sources (shutdown). The mode is `Sequential` when a packet has no `pts` or two packets share one;
  `frameCount` is then the packet count, and a request behind the newest decoded frame restarts
  decoding from the beginning (backward seeks are slow, R17). Otherwise the frames are ordered by
  `pts`, the access is `RandomCostly`, a request a few frames ahead decodes forward and any other
  request seeks. Every decoded frame is checked against frame 0 (size and format; Format "frame
  <i> changed to …" otherwise); a file that changed under an open source is Format "the file changed
  while open".
- *Ring:* the decoded frames wait in a ring of `max(1, min(8, 256 MiB / frame bytes))` frames.
- *Colour keys (D28):* a `file:` URI the `ffmpeg` provider opens takes the colour keys (`colour`,
  `range`) and `impl`; they override the container's colour.
  `fmt`, `size` and `fps` are accepted only when equal to the stream's ("fmt is set by the
  container (<format>)" and the same for size and fps); any other key is "unknown parameter".
  The resolved URI carries `fmt`, `size`, `fps`, only the query's colour keys and `impl=ffmpeg`.
  Y4M files stay with the Y4M provider, whose colour keys override the header (above).
- *Reads:* FFmpeg reads the file through its own I/O (`reader` `"ffmpeg"`); `io.fileReads` (§10.4)
  does not apply.

**`lavfi:` sources (M4, provider `lavfi`).** The URI body is the graph exactly as given; it takes no
query (a `?` is an error). The graph is parsed with `avfilter_graph_segment_parse` and every filter
name is checked before any filter is created, against an allowlist of 31 video generators and
simple filters of the pinned FFmpeg build, none of which takes a path or URL: `testsrc`,
`testsrc2`, `smptebars`, `smptehdbars`, `pal75bars`, `pal100bars`, `rgbtestsrc`, `yuvtestsrc`,
`colorchart`, `colorspectrum`, `color`, `gradients`, `allrgb`, `allyuv`, `nullsrc`, `mandelbrot`,
`format`, `scale`, `setparams`, `setsar`, `setdar`, `crop`, `pad`, `hflip`, `vflip`, `transpose`,
`null`, `loop`, `tpad`, `drawbox` and `drawgrid`. Any other filter, including every filter that
opens files or URLs (`movie`, `amovie`, `subtitles`, `drawtext`, `sendcmd`, …) and filters that
drop frames (`select`, `trim`, `fps`), is `Unsupported` "lavfi: filter <name> is not supported in
v1.0". Only for a filter that reads files or URLs is "; open files with file: instead" appended
(`select`, `trim` and `fps` get the plain text). A graph is one chain: a filter with two inputs
(scale's `ref`) is refused, and the graph needs exactly one output. Frame 0 decides the format,
size and colour (set the colour with `setparams`; View → Format… does not apply to a `lavfi:`
stream), the sink the frame rate; later frames are checked against frame 0. The source is
`Sequential` and unbounded (`frameCount` empty); a finite graph ends with Io "the lavfi graph
ended after <n> frames" (D6). A graph is deterministic unless it uses `gradients` or a random
colour (D6 "seeded generators only"); such a stream shows different frames after a seek back. A
lavfi graph reads no file (`reader` `"lavfi"`).

### 7.3 Pixel-format engine
- **One descriptor model (M2 D2): per-plane pixel groups with sample slots.** A `PixelFormatDescriptor`
  has a colour model, up to 4 components (kind, significant bits 1–16, horizontal and vertical
  subsampling; the order is the unpacked plane order) and up to 4 planes. A plane is a row of
  *groups*: a group spans `groupPixels` full-resolution pixels in `groupBytes` bytes made of words of
  `wordBytes` (1, 2 or 4) with one byte order; each sample of the group is a slot `{component, index
  in the component's own grid, word, shift}`, value = `(word >> shift) & mask(bits)`. Row bytes =
  `ceil(width / groupPixels) · groupBytes`, rounded up to `rowAlign` (1, or 128 for v210); plane rows
  = `ceil(height / 2^subY)`. This one rule covers planar, semi-planar, packed, 8–16 bit, LSB- and
  MSB-aligned, both byte orders and bit-packed groups (v210, x2rgb10le), and matches FFmpeg's
  `av_image_fill_linesizes` with alignment 1 (and FFmpeg's v210 stride `ceil(w/48)·128`). A field
  `widthMultiple` (default 1; v210: 2) rejects widths FFmpeg's encoder rejects: `geometry()` returns
  `Format` "v210 needs a width that is a multiple of 2". `validate()` checks a descriptor (slots inside
  their group and word, no overlapping bits, every sample exactly once, at most 6 slots per component
  for the GPU unpack).
- **The M2 catalogue is 37 formats (M2 D3), all bit-exact on the GPU**, named after FFmpeg `pix_fmt`
  names (`v210` is FFmpeg's codec name): gray `gray gray10le gray12le gray16le gray16be`; planar YUV
  `yuv420p yuv422p yuv444p yuv420p10le yuv422p10le yuv444p10le yuv420p12le yuv422p12le yuv444p12le
  yuv420p16le yuv444p16le yuv420p10be`; semi-planar `nv12 nv21 p010le p016le`; packed YUV `yuyv422
  uyvy422 y210le v210`; packed RGB `rgb24 bgr24 rgba bgra argb rgb48le rgb48be rgba64le x2rgb10le`;
  planar RGB `gbrp gbrp10le gbrp16le`. Every layout class has a member, so a further format is one
  table line plus one layout golden (F7).
- **FFmpeg `pix_fmt` names (M4):** the output of a decoder or a `lavfi:` graph maps by its `pix_fmt`
  name (`av_get_pix_fmt_name`): a catalogue name maps to itself, and FFmpeg's deprecated full-range
  names `yuvj420p`, `yuvj422p` and `yuvj444p` map to `yuv420p`, `yuv422p` and `yuv444p` with full
  range (when the frame and stream leave the range unspecified). Any other name is `Unsupported`
  ("decoder output <name> is not in the catalogue" for a container source).
- **Custom formats as JSON (M2):** `formatToJson` / `formatFromJson` (validates, including the GPU
  limits, and interns; a name equal to a catalogue name is `Format`); raw URIs accept
  `fmt=@<file.json>`, the resolved URI keeps it, and `rawreel-cli info --json` prints the descriptor.
  The schema is in the dvv-media README. The GUI's format dialog loads such a file (Load JSON…,
  `fmt=@…`, M3); an editor dialog that saves the same JSON is v1.x.
- A scalar CPU reference pack/unpack for every descriptor (slow, obviously correct) is the ground truth for
  tests and the CPU fallback (`CpuUnpack`, §6.4). Each catalogue format also has a hand-derived
  **layout golden** (a tiny frame with distinct code values and its packed bytes, from FFmpeg's
  `pixdesc.c`/`v210dec.c` definitions), because CPU and GPU share the descriptor and a round trip
  alone would accept a wrong table entry (§12).

### 7.4 Clip description JSON (M4, D5)
A clip description says how to read one raw clip. It is read by Load JSON… in the format dialog,
by `clip=@<file.json>` and as the sidecar `<file>.<ext>.json` (§7.2). The pixel-format JSON (§7.3)
is unchanged. Parser: `dvv::media::parseClipJson` / `loadClipJson` (`dvv/media/clip_json.h`).

| Key | Form | Rules |
|---|---|---|
| `rawreelClip` | integer `1` | required; another value is "unsupported version <n> (this rawreel reads 1)" |
| `format` | string | a catalogue name, or `"@<path>"` to a pixel-format JSON (relative to the clip file's folder) |
| `size` | `{"width": W, "height": H}` | both required, integers 1..65535 |
| `frameRate` | integer, or string `"N"` / `"N/D"` | integers > 0; a decimal is refused ("write N or N/D, e.g. \"30000/1001\"") |
| `colour` | `{"standard": S, "range": R}` | `standard` required, a `colour=` token (§7.2); `range` optional, `"full"` or `"limited"`, with every standard; errors "colour: missing key 'standard'", "colour: unknown key 'custom' (standard, range)", "colour.standard: bad colour 'x' (…)", "colour.range: bad range 'x' (full, limited)" |

Every key except `rawreelClip` is optional, but at least one group must be present. Parsing is
strict: an unknown key at any level is an error. Errors are `Format` "clip description '<file>':
<json path>: <reason>"; a file over 1 MiB or one that cannot be read gives the small-file errors.
Example: `{ "rawreelClip": 1, "format": "yuv420p10le", "size": { "width": 3840, "height": 2160 },
"frameRate": "60000/1001", "colour": { "standard": "bt2100-pq" } }`. The `preset` and `custom`
colour forms of D5 are gone; the version stays 1 (no v1 file was published).

---

## 8. Colour and display

- SDR first, HDR-ready. Colour is one **standard** (U-D5-7, below): matrices BT.601/709/2020 NCL (CL
  later), full/limited range, the BT.1886 curve (sRGB and linear only as file tags), PQ/HLG
  tone-mapped to SDR. Custom Kr/Kb matrices are v1.x.
- Display path (M3, one viewport): original view → upload and GPU unpack → `ToDisplay` (Derived,
  linear `RGBA16F`, BT.709 primaries, §6.3, §6.4) → the fit compositor (a compute pass: the frame
  fitted into the viewport, centred, aspect kept, a dark or light background, sRGB encode into an
  `RGBA8` image at the swapchain extent; nearest texel at zoom ≥ 1, bilinear taps below, at most
  4 × 4 taps, so below 1:8 it still undersamples) → the presenter's blit into the swapchain. FIFO
  present. Zoom/pan, the pixel grid, overlays (grid, tool overlays, text through a glyph atlas)
  and mailbox are v1.x.
- **ToDisplay math** (the shader and the CPU reference `toDisplayReference` share it, float32):
  code values → R'G'B' by one affine map (range normalisation, the YUV matrix, component order,
  gray), clamp; the transfer's EOTF to linear light (sRGB, BT.1886 power 2.4, linear; PQ: ST 2084
  through the BT.2390 EETF from 1000 to 100 nits per channel; HLG: BT.2100 inverse OETF and OOTF for
  a 1000-nit display, gamma 1.2, then the same EETF); clamp. **No gamut conversion** (U-D5-7): the
  primaries are information only, so BT.2020 primaries look as on a monitor that ignores them; the
  Info pane, the dialog and the CLI mark them "(not converted)".
  Chroma is taken from the nearest sample (bilinear chroma with `ChromaLocation` is v1.x).
- **Colour standards (M4, D5R; U-D5-7, U-D5-9; `colour=<token>`, `dvv/media/colour_preset.h`):**

  | Standard (GUI) | Token | matrix | range (default) | curve | primaries |
  |---|---|---|---|---|---|
  | BT.601 (NTSC) | `bt601-ntsc` | BT.601 | limited | BT.1886 | BT.601-525 |
  | BT.601 (PAL) | `bt601-pal` | BT.601 | limited | BT.1886 | BT.601-625 |
  | BT.709 | `bt709` | BT.709 | limited | BT.1886 | BT.709 |
  | BT.2020 | `bt2020` | BT.2020 NCL | limited | BT.1886 | BT.2020 |
  | BT.2100 | `bt2100-pq` | BT.2020 NCL | limited | PQ | BT.2020 |
  | BT.2100 | `bt2100-hlg` | BT.2020 NCL | limited | HLG | BT.2020 |

  - The GUI has one BT.2100 entry with a Curve choice (PQ, HLG); the token carries the curve (there
    is no plain `bt2100`). The SDR standards have the fixed curve BT.1886.
  - The range is a choice on every standard (U-D5-9): the default is the standard's (limited for
    YUV, full for RGB and gray), and Full on BT.601, BT.709 or BT.2020 is marked "(not standard)".
  - RGB and gray formats: the matrix is not used; the standard sets the curve and the primaries.
- **The colour in use** (`resolveColour`): matrix, curve and primaries come from the first source
  in the §7.2 precedence that gives them, and the given fields pick the standard they point to
  (`standardOf`, below), which fills the missing ones. A stream that gives no colour is **BT.709 at
  any size** (no height rule, U-D5-8): matrix BT.709, curve BT.1886 (also on RGB and gray),
  primaries BT.709. So untagged SD material decodes with BT.709 weights, and untagged SDR shows
  darker shadows than players that pass code values through (0.10 shows as about 0.05).
- **The range rule:** `range=` (written by the dialog's Range choice); else the range the file
  signals (clip JSON, sidecar, Y4M `XCOLORRANGE`, container tag, `yuvj*`), which always wins over
  the standard's default and preselects the dialog's Range; else limited for YUV and full for RGB
  and gray. A standard never sets the range (M4-D5-RGBRANGE, for all formats).
- **Container and lavfi tags to a standard** (`standardOf`, after `mapColour`; ITU-T H.273 codes):
  - Equivalent codes: transfer 1, 6, 14, 15 (and 4, 5) = BT.1886; primaries 6 and 7 = BT.601-525
    (NTSC), 5 = BT.601-625 (PAL); matrix 5 = 6 (BT.601; the primaries choose NTSC or PAL, the
    matrix code is only a weak hint when the primaries are 2).
  - Order: transfer 16 → `bt2100-pq`, 18 → `bt2100-hlg`; else by matrix (9 `bt2020`, 1 `bt709`,
    5/6 `bt601-*`); matrix 2 or RGB (0, also the GBR matrix on a YUV format) → by primaries; all 2 →
    BT.709 with "colour not signalled".
  - A match needs every given field to be the standard's code, an equivalent or 2. Otherwise the
    entry is "From file (no standard)": the given values are used as they are (no snapping), the
    missing fields come from the standard the given fields point to, else BT.709 (v1.0,
    M4-D5R-NSPIX2: the values shown are the values rendered). The source note "signalled but
    non-standard (P/T/M = x/y/z)" names the codes; v1.0 shows it in the Log and as the CLI `note:`
    line (JSON `notes`), not in the label. "Not signalled" and "signalled but non-standard" are
    reported differently.
  - The transfer is read from the decoded frame first (it carries the HLG ATC SEI), then codecpar.
  - Raw files always give a whole standard (`colour=`, clip JSON, sidecar or name token).
- **Auto detect** (U-D5-8): a format-dialog check box, off by default, remembered for the session
  only. When on, an untagged clip gets a standard guessed from the height (≤ 480 → `bt601-ntsc`,
  ≤ 576 → `bt601-pal`, else `bt709`), marked "(auto)". Nothing changes automatically without it.
- **Size-mismatch hint** (U-D5-8; grey, one line, dialog values line and Info pane, independent of
  Auto detect): BT.709, BT.2020 or BT.2100 on ≤ 576 lines gives "SD size: BT.601 is common for
  SD"; BT.601 above 576 lines gives "HD size: BT.709 is common".
- **Tone-mapping status:** PQ and HLG keep the BT.2390 EETF. The main window's 4th status-bar field
  (`mappingText`, rawreel `core/status_text.h`, set on a stream change) shows
  `PQ → SDR tone mapping (1000 → 100 nits)`, `HLG → SDR tone mapping (1000 → 100 nits)`,
  `SDR, no tone mapping` (BT.1886, sRGB, linear), or nothing without a stream.

- HDR swapchain (`VK_EXT_swapchain_colorspace`) in v1.x.
- The pipette (v1.x) shows: code values per component (from the original), normalised values, and the
  displayed RGB, labelled as display values.

---

## 9. Components and extensibility

### 9.1 Adding a component

```cmake
dvv_add_component(<id> KIND <Kind> SOURCES <file>... [LINK <target>...] [REGISTERS <component id>...])
# e.g. dvv_add_component(ffmpeg KIND Source SOURCES … REGISTERS ffmpeg lavfi)
#   → option DVV_COMPONENT_FFMPEG, static lib dvv::component_ffmpeg, WHOLE_ARCHIVE link
dvv_components_link(<executable>)   # links every enabled component library as a whole archive
```

Plus one class implementing the interface and one line `DVV_REGISTER(<Interface>, <Class>)` per
registered id. The GUI, CLI and conformance tests pick it up automatically (the conformance test
compares the registry with the enabled CMake set). v1.0 has no `SHADERS` argument (it comes with
the first shader component, v1.x) and no `REQUIRES`: a component's needs are checked by its
`probe` at run time.

- Each component is its own static library, linked with `$<LINK_LIBRARY:WHOLE_ARCHIVE,component>`
  (MSVC `/WHOLEARCHIVE`, GNU `--whole-archive`, Apple `-force_load`) applied **only** to component
  libraries, so self-registering static objects survive linking. M1a provides that link step as
  `dvv_link_components(<target> <component libraries>...)` (dvv-registry); `dvv_add_component()` arrives
  with dvv-components (§15).
- Compile-time on/off by CMake option per component; run-time on/off by capability probing.
- `DVV_REGISTER` is used at namespace scope in a `.cpp` of a component library. Registration happens
  during static initialisation, so the `ComponentRegistry` is created from `main` (never from a static
  initialiser), and component libraries are never loaded dynamically.

### 9.2 Common contract

```cpp
struct ComponentInfo { std::string_view id, name; Kind kind; int version; };
template <class T, class I>                 // used as Component<IMetric> T
concept Component = std::derived_from<T, I> && requires(const Capabilities& caps) {
    { T::info() } noexcept -> std::same_as<ComponentInfo>;
    { T::schema() } -> std::same_as<const ParameterSchema&>;
    { T::probe(caps) } -> std::same_as<std::expected<void, Unavailable>>;
};
```

- `info()` is `noexcept`: the registry needs the id to report anything about a component. A `schema()`
  or `probe()` that throws, and a constructor that throws (except `std::bad_alloc`, which is
  `OutOfMemory` and leaves the component available), make the component unavailable with an
  `Internal` reason; the rest of the registry keeps working.
- `probe` runs at startup and when the GPU changes; failure greys the component out with the reason.
- `ParameterSchema`: int, float, bool, enum, range, colour with min/max/default → wxPropertyGrid, CLI
  `--param name=value`, session file. User shader schemas come from Slang reflection. A schema is built
  with `ParameterSchema::Builder`, whose `build()` returns `Errc::Internal` for a broken declaration
  (duplicate or invalid name, bad limits, an enum option that is empty, repeated or starts or ends with
  white space, a default that does not fit); values that do not fit the schema are `Errc::Format`.

### 9.3 Component kinds

| Interface | Input → output | v1 examples |
|---|---|---|
| `ISource` provider | URI → `ISource` (§7) | raw, Y4M, synthetic; FFmpeg, lavfi |
| `IFormatDetector` | path + first bytes → format guesses | filename pattern |
| `IExporter` (v1.x) | `begin / write(FrameRef, region) / finish` | raw region (original format), PNG (display), Y4M |
| `ITool` (v1.x) | viewport events in image coordinates → `ToolOutput{overlay commands, info}` | pipette, grid, region zoom, region save |
| `IMetric` | (reference, test) `OriginalRead`s → per-frame `Result` + `aggregate()` | MSE/PSNR, SSIM, statistics |
| `IAnalyser` | one frame, or frames N and M → `Result` + visualisation | histogram, frame statistics |
| `IProcessor` | `FrameRef` → new `FrameRef` with its own format | edge detection (v1.x), user shader (v1.x) |

v1.0 registers only `ISource` providers (U-SCOPE): there are no tools and no exporters, so the
`ITool` and `IExporter` kinds do not exist in v1.0 (v1.x); metrics, analysers and processors are
v1.x as well (the M4 re-scope moved the analysis interfaces, the metric and analyser components and
their oracles to v1.x).

- **Result model:** a set of named values, each a `std::variant` of scalar, vector, table, histogram,
  vector field or image (an image result is a new frame). One generic panel, one Vulkan graph, CSV/JSON
  export and the CLI serve every metric and analyser.
- **Rules:** components see only dvv-media and dvv-gpu (via `ExecutionContext` and frames), never wx, the
  Session or other components. Tools are stateful but never render. Session files store
  `id + version + parameters`, so new component versions can migrate old parameters.

### 9.4 Object creation and lifetime (global rule)

**Scope.**
- *Managed objects:* every component (§9.3) and every service or resource in `dvv::` —
  `ComponentRegistry`, `GpuRegistry`, `ExecutionContext`, `GpuBuffer`/`GpuImage`, `ResourcePool`,
  `ComputePipeline`, `Presenter`, `FrameStore`, `IFrame` implementations, `ISource` instances.
  `ComponentRegistry` and `GpuRegistry` are their own creation authorities (a static `create()`); the
  `GpuRegistry` creates `ExecutionContext`s, and an `ExecutionContext` creates its buffers, images,
  `ResourcePool`s, `ComputePipeline`s and `Presenter`s (`createPresenter`, owned through
  `std::unique_ptr`; a presenter keeps its context alive and is destroyed before its window).
- *Internal parts* (exempt): implementation objects owned only by managed objects and created only by
  them, e.g. dvv-gpu's `InstanceCore` (the instance and messenger of a `GpuRegistry`), `DeviceCore` (the
  device, queue, allocator and retire queue, shared by an `ExecutionContext` and its resources until
  their deferred destruction is done), `Submitter` (the context's submit and waiter threads) and
  `PipelineCache` (the context's pipeline cache and its file), and the `Presenter`'s `PresenterCore`
  (its thread, surface and swapchain); in dvv-media the store's entries and records, its planner and
  converters, `ProduceContext` (handed out only by reference during `produce`) and the unpack push
  constants. They are never handed out and are not registered; the managed objects that hold them
  are, so leaks still show. A `GpuImage`'s view and a `ComputePipeline`'s push-descriptor set layout
  are raw handles inside their owner and retire with it.
- *The M2 managed types* (all `final`):

  | Type | Authority → ownership |
  |---|---|
  | `FrameStore` | its own static `create()` → `shared_ptr` (frame refs and leases share it); holds its context, executors and attached sources |
  | `HostFrame`, `StagingFrame`, `VkBufferFrame`, `VkPlanesFrame` | `FrameStore` → `shared_ptr<IFrame>`; made by converters and `ProduceContext::allocateHost` |
  | `SyntheticSource`, `RawFileSource`, `Y4mSource` | `SourceFactory` through `OpenTicket` → `issueKey<T>` → `unique_ptr<ISource>`, then owned by the store after `attach`; never hold the store |
  | `SourceFactory` | its own static `create()` → `unique_ptr`; owns the provider instances |
  | `SyntheticProvider`, `RawFileProvider`, `Y4mProvider` | `ComponentRegistry` (`DVV_REGISTER`); stateless |

- *Handles* (exempt; move-only RAII, created only by their owner but not registered individually):
  leases (`ReadLease`, `PinLease`) and `OriginalRead`, `OpenTicket`, `Task`/coroutine
  frames, awaiters, the `CommandRecorder` passed to a record function (valid only during that call),
  and dvv-platform's `DynamicLibrary` (created by `DynamicLibrary::open`; the Vulkan loader's is kept
  loaded for the whole process), `FileReader` (created by `FileReader::open`) and `AlignedBuffer`
  (`allocateAligned`). The `FrameStore` accounts for leases (§6.5); a lease alive at exit keeps its
  frame alive, so the leak check names the frame type and `outstandingLeases()` the acquiring
  `source_location`.
- *Test helpers* (exempt, tests only): the `dvv::test` authority and dvv-platform's `TestWindow`
  (target `dvv::platform_testing`).
- *Value types* (exempt): `FrameKey`, `ViewSpec`, `FrameDesc`, `Domain`, `FrameRef` (a copyable
  struct of key, desc and a `shared_ptr` to the store), `Error`, format descriptors,
  `ColourParams`, `SourceInfo`, `Match`, `Candidate`, `Ambiguous`, `StoreStats`, `CodeValues`,
  `ComputeBindings`, result values, and the static `DVV_REGISTER` descriptors.
- *Process infrastructure* (exempt): `LifetimeRegistry`, `Logger` and its sinks, and dvv-media's
  format and colour tables (entries heap-allocated and never moved or removed, so references stay
  valid while custom formats are interned). They are never destroyed, so they outlive the leak check.

```cpp
namespace dvv {
template <class F> class CreationKey {      // passkey + lifetime token in one; only F can make one
    friend F;
    explicit CreationKey(LifetimeToken token) noexcept;
public:
    LifetimeToken token() && noexcept;
};

class ManagedObject {                       // non-template base: identity through interface pointers
public:
    ObjectId objectId() const noexcept;
protected:
    explicit ManagedObject(LifetimeToken token) noexcept;
    ~ManagedObject();                       // unregisters; never blocks, never calls out
};

template <class T> class Managed : public ManagedObject {   // CRTP: no copy/move, per-type record
protected:
    template <class F> explicit Managed(CreationKey<F>&& key) noexcept;
public:
    Managed(const Managed&) = delete; Managed& operator=(const Managed&) = delete;
    Managed(Managed&&) = delete;      Managed& operator=(Managed&&) = delete;
};

class PsnrMetric final : public IMetric, public Managed<PsnrMetric> {
public:
    PsnrMetric(CreationKey<ComponentRegistry>, const ParameterValues&) noexcept;  // unusable without the key
};
}
```

- **Factory-only creation, enforced at compile time.** Every constructor takes a `CreationKey<F>` first.
  Each kind has one creation authority `F` (`ComponentRegistry`, `SourceFactory`, `FrameStore`,
  `ExecutionContext`, …); where another party builds the object, the authority passes a key on (e.g.
  `SourceFactory` hands one to a provider's `open`, `ResourcePool` obtains buffers from its
  `ExecutionContext`). Tests use a dedicated `dvv::test` authority. Managed concrete classes are `final`.
  No `new`/`delete` outside factories. The guarantee is against accidental construction: a key that is
  held can still be passed on deliberately.
- **Fallible creation.** The authority's factory functions return `std::expected<std::unique_ptr<T>,
  Error>` (or `shared_ptr` where ownership is shared): fallible work (Vulkan calls, file opens) happens first,
  then a `noexcept` constructor receives the acquired handles.
- **Ownership.** `std::unique_ptr` for single ownership (default), `std::shared_ptr` where ownership is
  shared (frames, GPU resources). An object may hold a strong reference **up** to its parent (a resource
  keeps its device core alive until the deferred destruction of §5.3 is done) but never in a cycle.
  `std::weak_ptr` (only for `shared_ptr`-owned objects) or ids are for observers, caches and
  cross-thread or persistent references; plain `T&`/`T*` only for scoped borrows that are not stored.
  Avoid `weak_ptr::lock()` on hot paths.
- **Lifetime registry.** The token records type (pointer to a static per-type record), instance id
  and owner; Debug builds also record the creating `std::source_location` and time (Release with
  `--diag` is v1.x, §10.3). A factory whose callers should appear there takes the location as a defaulted last
  parameter, e.g. `ComponentRegistry::createComponent<I>(std::string_view id, const ParameterValues&
  values = {}, std::source_location where = std::source_location::current())`, so a leak report
  names the caller, not the factory. The registry is a sharded intrusive list, owns nothing, and is
  an intentionally leaked function-local static. Per-type relaxed counters are always on.
  `ResourcePool` registers objects on creation, not on each reuse. GPU handles waiting in a retire
  queue are counted there as "retiring".
- **Leak check.** An explicit call at the end of `main` after the shutdown order Session stops →
  `FrameStore` drains → each context drains its retire queue (`vkDeviceWaitIdle`); never from a static
  destructor. It fails Debug runs and CI; Release logs a warning.
- **Checked at build time.** `DVV_REGISTER` and the factory helpers `static_assert` that the class is
  `final`, derives `Managed<T>` and is not copy- or move-constructible. Compile-fail CTest tests prove
  that direct construction does not compile (`std::is_constructible` cannot prove it): each runs
  `cmake -P ${DVV_CMAKE_DIR}/run-compile-fail.cmake` (installed with dvv-base), which builds an
  `EXCLUDE_FROM_ALL` target and passes only when that build fails with a diagnostic matching the
  expected pattern of the compiler, so a typo cannot pass. A control case must build. The tests build
  in the shared build tree and therefore run under `RESOURCE_LOCK dvv_build_tree`.
  The conformance test (§12) creates every component through the registry and, after a drain, checks
  that the live count returns to zero.

---

## 10. Application

### 10.1 Session
The `Session` actor (rawreel's `rawreel_core`, no wx) owns the stream (one in v1.0; its per-slot
state and `FrameSet` keep room for 1–3), the player and media clock (§4), the GPU stack (context and
frame store, §5.2) and later the active tools, the processor per stream, metric/analyser selections
and results (v1.x). Commands are thread-safe and non-blocking and run in call order on its
executor; opens are serialised in one FIFO, and an open replaces the stream (`open` of slot ≥ 1 is
`Unsupported` "one stream in v1.0"). Session files (JSON: resolved source URIs, view state and
component parameters, with the migration of component parameters) are v1.x.

### 10.2 GUI (`rawreel-gui`)
- wxAUI main frame (M3): a fixed centre pane with one viewport showing the one stream, fit to
  window (the compositor computes the fit from the presenter's extent and the image size, so a
  resize needs no GUI round trip), the transport bar (first, step back, play/pause, step forward,
  last, loop, a slider, the frame label), and two dockable panes: Info (the stream: URI, format,
  size, fps, frames, the colour standard with its origin, e.g. `colour: BT.2100 PQ (from file)`,
  `colour: BT.709 (default)` or `colour: no standard (from file)`, then the values line with the
  primaries "(not converted)" and the size-mismatch hint (§8), the clip description file when one
  is used, "tone-mapped (BT.2390, 1000 → 100 nits)" for PQ/HLG) and Log (warnings and errors; also a rotating log file in the user data directory).
  Help → About (M4, layout approved by the user, U-ABOUT1) shows "rawreel <version>", "Raw video
  player (Vulkan)", the copyright (GitHub handle) with "MIT licence", the repository link, then the
  GPU (the Vulkan device and API), FFmpeg (the status line: its version, or not available with the
  reason) and wxWidgets lines (with the appearance, requested → result), and a collapsed
  Components pane with the dvv package versions; buttons Third-party notices (opens
  `THIRD_PARTY_NOTICES.txt` next to the executable), Copy to clipboard (the whole text) and OK. No
  icon in v1.0.
  Deferred panels: Properties (wxPropertyGrid) → v1.x; the Capabilities page → v1.x (M4 re-scope;
  Help → GPU info writes the device to the log); the frame-store debug panel → v1.x (today
  Help → Diagnostics logs the store counters); Metrics table, Graph, Histogram and the Shader
  editor → v1.x.
- The Vulkan viewport is a native child window with a directly created `VkSurfaceKHR` (Win32; Xlib
  in v1.x) owning one `Presenter` (§4). Tiles for 2–3 streams, zoom/pan and fullscreen are v1.x.
- Minimum size (U-GUI1): the clip view (viewport) stays at least 320×240 DIP. The frame's minimum
  client size is that plus the transport bar, an infobar reserve and each dock's own minimum
  (Info 160 DIP wide, Log 60 DIP high, plus caption and sash); it is recomputed on a DPI change.
  When the window shrinks, the docks give way first, down to their minimum, and grow back to the
  user's size; a sash drag that takes the viewport below 320×240 is clamped when the mouse is
  released.
- Opening: *Open…* (one file; "All files" opens containers, the "Media files (FFmpeg)" filter is
  v1.x), *Open URI…* (a `lavfi:` graph is typed there; the generator presets are v1.x), a URI on the command line (at most one), drag-and-drop (the first file opens; more are
  logged as ignored). An open first closes the current stream (U-GUI1; only a text that does not
  parse as a URI just warns and keeps the stream), so a failed open leaves no stream and its error
  in the infobar; when the open failed because the GPU stack could not be built, *Restart GPU and
  reopen* opens the failed request (§5.2). A stream change (open or close) dismisses the infobar; the new stream's own warnings
  (e.g. trailing bytes) still show. Every cancelled open dialog writes an Info line to the Log
  ("open cancelled"; the format dialog "open of <uri> cancelled"). *Open…* starts in the folder of
  the last opened clip (an existing folder only); Load JSON… starts in the last JSON file's folder
  of the session, else the clip's folder. *Open URI…* is a wide multi-line field (line breaks are
  removed) prefilled with the last text accepted (the example only on first use); a text without a
  scheme that contains `=`, `&` or `?` and fails as a missing file gets the hint "no scheme: opened
  as a file path; a URI needs e.g. synthetic: or lavfi:". Only a `file:` URI without `fmt` that no
  provider matches (`dvv::media::isNoProviderMatch`; no message-text match), or an ambiguous one,
  opens the format dialog; a file a provider recognises but refuses (e.g. a raw file whose name
  gives the size, a broken Y4M header) is an error. The open error of a raw-like `file:` URI (no
  `impl` or `impl=raw`, not a Y4M or container extension, or no provider matched) gets a Format…
  button: the dialog is prefilled from the file name and the query, else the last choice, and its
  choice reopens the file with `impl=raw`. The format dialog (M4, D5, D5R; U-D5-6, U-D5-6a; not
  resizable):
  - Title `Format of the stream - <short file name>`: a name over 40 characters is shortened in the
    middle with `...`; a non-file URI shows its scheme and path; the full path is the tooltip of
    the detection line.
  - Detection line (the Log line says the same): `Format not detected: <reason>`; `<What> detected
    from the file name; <rest> not detected` (lists of size, format, frame rate and colour; colour
    only when the name gave it); `Format detected from the file name`; `Format detected from the
    container (MKV)` (the demuxer's short name); `Format detected from the Y4M header`; `Format
    detected from clip.yuv.json`; `Format detected from the clip description (<file>)`; `Format
    given in the URI`.
  - Template lines, only when the format is not or only partly detected: `Name the file
    <name>_WxH[_fmt][_fps]` with an (i) icon, and `Example: clip_1920x1080_yuv420p_25.yuv`. Their
    tooltip explains WxH, fmt and fps, the colour token (`_bt709` etc. at the end: the six tokens of
    §7.2) and the clip description next to the file.
  - Prefill: format, size and fps from the URI's own keys, else the best candidate, else the last
    choice, else 1920x1080 `yuv420p` 25 fps; the colour from the URI (below), not the last choice's
    colour.
  - Format: a catalogue name or a JSON format. Load JSON… of a pixel format appends (or replaces)
    and selects the entry `Custom (from <file>.json)` (tooltip: the full path; writes
    `fmt=@<path>`); its width/height rule is checked in the dialog like a catalogue format's.
  - Size row on one line, `[preset] [W] x [H] @ [fps]`: presets labelled `WxH (name)` (`176x144
    (QCIF)` … `1920x1080 (1080p)` … `4096x2160 (DCI 4K)`) and Custom; the frame rate is an
    editable list (`23.976` … `120`; the NTSC decimals give N·1000/1001); a bad fps text shows on
    the red line.
  - Colour: the five standards (BT.601 (NTSC), BT.601 (PAL), BT.709, BT.2020, BT.2100, §8). The one
    that applies without a key has a suffix: ` (default)`, ` (from file)`, ` (from the file name)`,
    ` (from <clip>.json)` or ` (auto)`; a non-standard file colour adds the first entry `From file
    (no standard)`. Under it: Curve (PQ/HLG for BT.2100, read-only BT.1886 otherwise), Range
    (Limited/Full on every standard, preselected from the file, Full on an SDR standard marked
    "(not standard)"), the Auto detect check box (§8), and a read-only grey values line (matrix,
    "not used (RGB format)" or "(gray format)" there, and primaries "(not converted)"; for `From
    file (no standard)` also range and curve) with the size-mismatch hint (§8).
  - OK writes no colour key when the standard and options equal what applies without a key; else
    `colour=<token>`, plus `range=<r>` when the range differs from the one that applies without a
    key. It strips the old keys (`matrix`, `kr`, `kb`, `transfer`, `primaries`). A URI's `colour=T`
    selects T, its range from `range=`, else the base; no key selects the suffixed entry. Known v1.0
    limit (M4-D5-FILECOLOUR): a stream first opened with colour keys does not show the file's
    colour; View → Format… after a plain open does.
  - Locked fields: on a Y4M or container stream View → Format… shows format, size and fps as one
    text, `3840x2160 @ 50  (from file)`, hides the candidate list, and only the colour is editable.
  - Load JSON… ("Load a clip description or pixel format") tells the kinds apart by the
    `rawreelClip` key: a clip description (§7.4) fills only the groups it has, its colour selecting
    the standard and the range (on a locked stream only the colour, with a note); any other JSON is
    a pixel format (on a locked stream it changes nothing and the note says the pixel format was not
    loaded); a file that is not JSON gives the red line and changes nothing. The dialog never writes
    `clip=`.

  View → Format… reopens the stream with a new format or
  colour at the current frame; on an FFmpeg container stream it changes only the colour keys (the
  container sets the format, size and fps), and on a `lavfi:` stream it reopens nothing and says
  "set the colour in the graph (setparams=…)" in the log and the infobar. The custom-format editor and the Preferences dialog (GPU, cache
  budgets, frame dropping) are v1.x.
- Dark mode (`--appearance system|light|dark`, `wxApp::SetAppearance` at start) and `--gpu N`
  (§5.2).
- Hotkeys from one command table (wx-free): Ctrl+O Open, Ctrl+U Open URI, Ctrl+W Close stream, Ctrl+Q
  Quit as menu accelerators; Space play/pause, Left/Right step, Shift+Left/Right 10 frames,
  Home/End first/last, L loop through `wxEVT_CHAR_HOOK` (ignored while a text field has the focus).
  Stop, Format…, Restart GPU and reopen, GPU info and Diagnostics are menu-only.
- Status bar: position, target vs actual fps, bottleneck (polled every 50 ms), and the
  tone-mapping status of the stream (§8; set on a stream change, fixed width).
- Non-modal error reporting via a `wxInfoBar` (a Format… button for "N trailing bytes
  ignored" and a raw-like file's open error, see Opening; a Restart GPU button for GPU failures,
  §5.2), status bar and Log pane; modal dialogs only
  for fatal startup problems.
- `--selftest <scenario> [URI]` (Windows only in v1.0) drives the real window on a 50 ms timer,
  each step with a 5 s deadline, through the command layer and synthesised wx events (no OS input
  injection), and exits with 6 naming a failed step. `smoke`: the M1b script (at least 2 frames at
  the viewport's size; client size 800×600, then a shrink that stops at the minimum size (the
  viewport at least 320×240 DIP), each presented by a new swapchain;
  minimise: after 300 ms no new frame, hidden, not failed; restore: a new frame) with no stream and
  with one, plus the panes, menus, status fields, infobar and Help items, and the open rules
  through `MainFrame::openUri` (a cancel is logged, a failed open closes the stream, a stream
  change dismisses the infobar). `player`: play, pause, step, seek, loop, the slider, a resize that
  keeps the fit, View → Format…, drops and the format dialog. `lost`: an injected device loss →
  infobar with Restart GPU within 5 s, no hang → Reopen shows the kept frame again. Every scenario closes the window: no validation message, no presenter
  failure, and the leak check passes. `perf` (the 4K60 run, E7; manual, Windows only, not a CTest
  entry) was built in M3.1 Task 11b and prints one JSON line; its results are in §16.
- `rawreel-gui` exit codes (`--help` lists them): 0 success; 1 bad arguments (a second URI
  included), or the GUI could not start; 4 GPU error (no Vulkan loader, no usable device,
  presenter failure); 6 a `--selftest` check failed; 77 (`--selftest` only) no usable device or no
  display, unless `DVV_REQUIRE_GPU=1` (then 4).

### 10.3 CLI (`rawreel-cli`)
Since M2 (console, no wx; the rawreel README lists the options and JSON keys):
- `info <uri> [--json]`: the source description; `--json` includes the format descriptor.
- `dump <uri> --out <file> [--frame N] [--count K] [--view original|planes] [--via cpu|gpu|auto]
  [--json]`: writes K consecutive frames back to back, the original bytes (Exact) or the unpacked
  planes (Lossless). `--via cpu` uses a host-only store; `--via gpu` reads host planes in the staging
  domain, reachable only through GPU Readback, with no fallback; `auto` is gpu when a usable device
  exists. `--json` prints the converter stats, which prove the path.
- `bench <uri> [--frames N] [--inflight K] [--json]`: runs the frames through produce, staging,
  upload and unpack in batches of K acquires (`whenAll` per batch, so K is the batch size, not a
  sustained queue depth) and prints frames/s and GB/s per stage.
- `--version [--json]` (rawreel and dvv package versions, wxWidgets, the Vulkan line and, since
  M4, the FFmpeg status line; it exits 0 without a Vulkan driver), `--help`. FFmpeg is found
  through `DVV_FFMPEG_DIR` (an absolute path) or the `ffmpeg/` folder next to the executable (§2).
  `--ffmpeg-dir`, `metrics`, `stats`, `capabilities` and `--diag` are v1.x.

M3 adds no command: v1.0 has no `export` command (export is v1.x, U-SCOPE), and `metrics --ref
<uri> --test <uri> --metric psnr,ssim [--compare-format …] [--csv|--json]` is v1.x too; FFmpeg and
lavfi sources reach the CLI through the URI in M4. Exit codes (unchanged): 0 success, 1 bad
arguments (including a frame index outside the source), 2 input error, 3 unsupported/ambiguous
(the candidates listed), 4 GPU error; 5 (metric threshold failed) is reserved for `metrics`.
`--json` also prints errors as JSON (`{"error": {"code", "message"}}`). On Windows the entry point
is `wmain`, and the arguments are converted to UTF-8, so file names outside the ANSI code page
work. A global option `--diag` (CLI and GUI: full lifetime data in Release and a dump of the live
managed objects at exit, §9.4) is v1.x (M4 re-scope); in v1.0 only Debug builds record the
detailed lifetime data.

### 10.4 Platform
v1: Windows (Win32 surface, unbuffered or buffered positional reads on an I/O pool) and Linux
X11/XWayland (Xlib surface, `pread` thread pool) (U-WIN, U-LNX1, §17).
v1.x: Wayland (`wl_subsurface` approach as in wx `glegl.cpp`) and macOS (`CAMetalLayer`, MoltenVK)
as further `NativeWindow` alternatives. Since M2 the move-only `FileReader` handle replaces the
planned asynchronous reader interface: blocking positional `readAt` calls, run on an I/O thread
pool, with a size read once at open. Since M3.1 Task B3 (user decision U-IO2, §16, §17) it has two
modes behind one interface. Buffered (portable): reads go through the OS file cache with a
sequential-access hint (Windows `FILE_FLAG_SEQUENTIAL_SCAN`, Linux
`posix_fadvise(POSIX_FADV_SEQUENTIAL)`), any offset, length and address. Unbuffered (Windows
`FILE_FLAG_NO_BUFFERING`, Linux `O_DIRECT` on request): sector-aligned reads of the aligned
superset of a frame into aligned host memory. `Auto`, the default of the file sources, is
unbuffered on Windows when an open-time check passes (the volume's sector size, the unbuffered
open, the first aligned read) and otherwise buffered for that file with one log line; it is
buffered on other platforms. The check may fail, for example on some network drives; a network
file that passes it reads unbuffered. An explicit unbuffered mode that fails is an error. Windows
handles are synchronous: each read is a `ReadFile` with an `OVERLAPPED` that carries only the
offset (no event, no completion port). A reader runs one read at a time, so a file source never has
more than one read in flight. The mode comes from the application's config file (`rawreel.json` next to the
executable, key `io.fileReads`: `auto`, `unbuffered` or `buffered`; `--config` overrides the file)
and shows in `rawreel-cli info` and the GUI Info pane. The zip ships no `rawreel.json`, so the
built-in defaults apply until the user creates one (U-IO2-config). FFmpeg container sources read
through FFmpeg's own file I/O and `lavfi:` graphs read no files, so `io.fileReads` applies to the
raw and Y4M providers only. Since M1b the
`NativeWindow` variant with the free functions `surfaceInstanceExtensions()` and `createSurface()`
replaces the planned `ISurfaceFactory` interface, and the move-only `DynamicLibrary` handle replaces
`IDynamicLibrary`. User data and cache directories are discovered at run time per platform.

Linux X11 recipe (v1.0, U-LNX1): `main` calls `XInitThreads()` first and restricts GDK to the X11
backend with `gdk_set_allowed_backends("x11")` before GTK initialises, so XWayland is used under a
Wayland session. It changes no environment variable while GTK's own threads run; only a
`GDK_BACKEND` the user set is replaced with `x11`, still while the process has one thread. Each
viewport is a native child window: the widget is realized, its GDK window made native
(`gdk_window_ensure_native`, then a display sync), and it gets its own `VK_KHR_xlib_surface`, so
the surface never covers the whole top-level window. Without an X11 window `rawreel-gui` stops with
the error "rawreel-gui needs X11 or XWayland". Scales other than 1x are not tested yet.

---

## 11. Error handling

```cpp
struct Error {
    Errc code;                 // Io, Format, Unsupported, OutOfMemory, DeviceLost, ShaderCompile, Cancelled, Internal
    std::string message;       // for people; includes file/frame/component context
    std::source_location where;
    std::shared_ptr<const Error> cause;   // chain
};
```

- `std::expected<T, Error>` across library boundaries.
- Inside coroutines, `co_try(expr)` expands to `co_await dvv::detail::tryAwait(expr)`: on a value it
  resumes with the value; on an error its `await_suspend` stores the `Error` in the promise and completes
  the task through its continuation. Outside coroutines the statement macro `DVV_TRY(var, expr)` returns
  the error early. GNU statement-expressions (`({ … })`) are not used (MSVC and `-Wpedantic`).
- Vulkan-Hpp with `VULKAN_HPP_NO_EXCEPTIONS`; results converted to `Error` in dvv-gpu only, with the
  message `<call> failed: <result name> (VkResult <n>)`:

  | `VkResult` | `Errc` |
  |---|---|
  | `ERROR_DEVICE_LOST` | `DeviceLost` |
  | `ERROR_OUT_OF_HOST_MEMORY`, `ERROR_OUT_OF_DEVICE_MEMORY`, `ERROR_OUT_OF_POOL_MEMORY`, `ERROR_FRAGMENTED_POOL`, `ERROR_FRAGMENTATION`, `ERROR_MEMORY_MAP_FAILED`, `ERROR_TOO_MANY_OBJECTS` | `OutOfMemory` |
  | `ERROR_INITIALIZATION_FAILED`, `ERROR_INCOMPATIBLE_DRIVER`, `ERROR_LAYER_NOT_PRESENT`, `ERROR_EXTENSION_NOT_PRESENT`, `ERROR_FEATURE_NOT_PRESENT`, `ERROR_FORMAT_NOT_SUPPORTED`, `ERROR_NOT_PERMITTED`, `ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR` | `Unsupported` |
  | any other error | `Internal` |

  Errors from vk-bootstrap (instance creation) read `<what> failed: <vk-bootstrap message>`, followed
  by ` (<result name>)` when vk-bootstrap reports a `VkResult`, e.g. `creating the Vulkan instance
  failed: … (ErrorIncompatibleDriver)`; the code is that result's `Errc`, or `Unsupported` when there is
  none (vk-bootstrap's own version, layer and extension checks).

  A missing Vulkan loader is `Unsupported` ("Vulkan loader not found"); programming errors such as an
  unknown `DeviceId` or a zero-sized buffer are `Internal`. Loaders report a listed validation layer
  whose library does not load as `ERROR_LAYER_NOT_PRESENT` (Linux loader 1.3.275) or
  `ERROR_OUT_OF_HOST_MEMORY` (Windows loader 1.4.341): unless validation is required, and only when
  `vkCreateInstance` itself failed (vk-bootstrap's `failed_create_instance`), the instance is built
  once more without the layer (a real out-of-memory then fails again) after the warning
  `VK_LAYER_KHRONOS_validation could not be loaded (<result name>); validation is off`, and the
  loader's messages from the failed attempt do not count as validation findings. With validation
  required the error stays as the loader reported it (`Unsupported` on Linux, `OutOfMemory` on
  Windows).

  GPU work: a command the recorder cannot record (a resource or pipeline of another device or
  generation, a range outside a buffer, a missing usage flag, a push-constant size other than the
  pipeline's, group counts outside the device's limits, an unsupported format or layout) is a
  programming error, `Internal`: nothing more is recorded, and the submission completes with that error
  after signalling its timeline value. `createComputePipeline` returns `Internal` for data that is not
  SPIR-V and for a push-constant size that is not a multiple of 4 or smaller than the shader's block,
  `Format` for reflection JSON it cannot read, and `Unsupported` for a size above the device's limit.
  A pipeline-cache file that cannot be written is `Io` from `savePipelineCache()` and a log line when
  the context closes; a cache file that cannot be used is never an error (§5.4).

  Presentation (M1b; the WSI results the table above does not list, such as
  `ERROR_SURFACE_LOST_KHR` and `ERROR_NATIVE_WINDOW_IN_USE_KHR`, fall into "any other error"):

  | Situation | `Errc` / behaviour |
  |---|---|
  | `GpuRegistryOptions::windowSystem` set, but the loader lacks the surface extensions | `Unsupported` |
  | `ContextOptions::presentation` on a headless registry | `Internal` |
  | `presentation` on a device without `VK_KHR_swapchain` | `Unsupported` |
  | `createPresenter` on a context without presentation, with an empty frame provider, or for a window of another window system (or none) | `Internal` |
  | Surface creation failed | converted: `EXTENSION_NOT_PRESENT` and `INITIALIZATION_FAILED` → `Unsupported`, out of memory → `OutOfMemory`, `NATIVE_WINDOW_IN_USE` → `Internal` |
  | The queue cannot present to the surface; no swapchain format and usage fit (`TRANSFER_DST`, a UNORM or SRGB format with `BLIT_DST`) | `Unsupported` |
  | The presenter thread cannot start | `Internal` (`OutOfMemory` for `std::bad_alloc`) |
  | The frame provider returns an error; its image cannot be blitted | that error, or `Internal`, to `onError`; the frame is skipped |
  | Recording or submitting a frame fails | the submission's error; the presenter fails |
  | Acquire or present `TIMEOUT`, `NOT_READY`, `SUBOPTIMAL`, `OUT_OF_DATE`; a 0×0 surface | not an error (retry, recreate, or hidden) |
  | Surface lost (acquire, present or swapchain creation) | `Internal` "the window's Vulkan surface was lost (was the window destroyed before its presenter?)"; the presenter fails |
  | The acquire fence does not signal within 5 s | `Internal` "acquire fence did not signal"; the presenter fails (the fence is leaked) |
  | `ERROR_DEVICE_LOST` | `DeviceLost`; the presenter fails (the user reopens, §5.2) |
  | Swapchain creation out of memory | `OutOfMemory`; the presenter fails |
  | `DynamicLibrary::open`: missing or unloadable; empty name or a path with `SystemDirectory` | `Unsupported`; `Internal` |

  A failed presenter is terminal: `stats().failed` is set, `failure()` returns the error, and
  `onError` is called once (on the presenter thread). Replacing the presenter is the recovery; in
  rawreel every presenter failure (`DeviceLost` or another error) leads to the infobar's Restart GPU,
  which replaces the context, the store and the presenter (one path, M3 design D25).
- Exceptions stay enabled (wx, std) but never cross a component boundary: the registry wrapper converts
  them to `Errc::Internal` and disables that component for the session.
- `Cancelled` (via `stop_token`) is never shown as an error.

| Failure | Response |
|---|---|
| Vulkan missing / below 1.4 | Startup dialog; CLI exit code 4 |
| Optional library missing | Capability off with the reason (not an error); for FFmpeg the reason is in `--version` and About (the Capabilities page is v1.x) |
| FFmpeg missing, wrong version, GPL or nonfree, a symbol missing (any of the six libraries) | FFmpeg off with the reason; never an error at startup |
| `DVV_FFMPEG_DIR` relative, or set and failing | FFmpeg off with the reason ("DVV_FFMPEG_DIR must be absolute"); no fallthrough |
| `lavfi:` URI without FFmpeg | `Unsupported` with the FFmpeg status line (CLI 3; GUI infobar) |
| Container `file:` without FFmpeg | As any file no provider matches: the format dialog, a log line with the FFmpeg status; CLI 3 with the status line |
| A `file:` a provider recognises but refuses (a raw file whose name gives the size, a broken Y4M header) | The error in the infobar and Log, not the format dialog (only a no-match or `Ambiguous` opens it, §10.2); a raw-like file's error has a Format… button |
| Lossy video codec; `lavfi` filter not on the allowlist | `Unsupported` naming the codec / "lavfi: filter <name> is not supported in v1.0" (CLI 3; U-FF) |
| No usable video stream | `Unsupported` "no video stream" (CLI 3) |
| FFmpeg I/O errno (`ENOENT`, `EIO`, …) | `Io` (CLI 2) |
| Invalid data, truncated packet, a frame the decoder does not produce | `Format` (CLI 2), "frame <i> not produced by the decoder" |
| A damaged frame (the decoder reports an error or a corrupt frame, e.g. an FFV1 slice CRC mismatch) | `Format` (CLI 2) "frame <i> is damaged (…)" for that frame and every frame up to the next key frame (they depend on it); damaged frames are never concealed or cached. Read forward, the decoder keeps its position (no re-seek); from the next key frame on the frames are exact again |
| Decoder or graph output not in the catalogue; demuxer or decoder not found | `Unsupported` naming the format (CLI 3) |
| `fmt`/`size`/`fps` on an `ffmpeg` URI differ from the stream; invalid colour key | `Format` "<key> is set by the container" / as for Y4M (CLI 2) |
| A decoded or filtered frame changes size or format | `Format` "frame <i> changed to <w>x<h> <fmt>" (CLI 2) |
| View → Format… on a `lavfi:` stream | Log and infobar "set the colour in the graph (setparams=…)"; nothing reopened |
| A finite `lavfi` graph ends | `Io` "the lavfi graph ended after n frames"; the player pauses with the infobar |
| Other `AVERROR`; `AVERROR(ENOMEM)` | `Internal` "<call> failed: <text> (AVERROR <n>)"; `OutOfMemory` |
| Crash (Windows) | `CrashHandler`: a minidump in `<userDataDir>/crashes/` and a log line; the process ends with the exception code |
| File size not a multiple of frame size | Open with warning "N trailing bytes ignored" in the infobar, with a Format… button |
| Ambiguous raw file, or a `file:` without `fmt` that no provider matches | The format dialog (`formatNeeded`; not an error) |
| An open dialog cancelled (file, Open URI, format) | Info line in the Log ("open cancelled" / "open of <uri> cancelled"); the stream is already closed when the format dialog was cancelled |
| Invalid format choice | `Format`, shown in the dialog; OK stays disabled |
| `open` of a second stream (slot ≥ 1) | `Unsupported` "one stream in v1.0" → infobar and Log |
| Several files dropped / several URIs on the command line | The first file opens, the Log says "N files ignored" / exit 1 |
| Read error / truncated frame | Playback pauses on the last frame shown; the error names the frame (infobar, Log); a placeholder frame with an error overlay is v1.x |
| Out of memory (RAM/VRAM) | Evict, retry; the Session halves its prefetch window and retries once; then reports |
| Device lost (real or injected) | §5.2: pause, the GPU stack is released, `error(DeviceLost)` → infobar with Restart GPU; no automatic rebuild |
| Building the GPU stack fails (Reopen, an open with no stack) | `error` "… Restart the GPU to retry." → the infobar keeps Restart GPU; Reopen then opens the failed request |
| Presenter fails with another error | Log and infobar with Restart GPU (one path, D25) |
| Startup GPU failure (GUI) | Dialog, exit 4 (selftest: 4 or 77) |
| `SetAppearance` refused | Log line (Info) |
| User shader compile error | Diagnostics in the shader panel; last working version stays active |
| Component error or exception | Logged; component disabled for the session; everything else continues |

Logging: small `std::format`-based logger in dvv-base; sinks: rotating file in the per-user data
directory (`rawreel.log`, 4 MiB, 3 backups, M3), GUI Log pane, stderr (CLI); Vulkan validation
messages routed whenever validation is on (§5.1).
Crashes: the Windows minidump is implemented (M4): `SetUnhandledExceptionFilter` writes
`<userDataDir>/crashes/<name>.dmp` and a log line; `abort` and `__fastfail` are not captured. The
release ships the PDBs as the asset `rawreel-<v>-windows-x64-symbols.zip` for the maintainer.
Linux has no crash handler in v1.0 (U-LNX1): a crash leaves the system's core dump, if the system
keeps one, and the Linux symbols ship as `rawreel-<v>-linux-x64-symbols.tar.xz`. A Linux handler
and a common crash reporter are v1.x.

---

## 12. Testing

- doctest + CTest in every repo; TDD for implementation.
- **Fixtures are generated, never committed:** tiny frames (4×4, 16×16, odd sizes such as 33×17) from
  `SyntheticSource` (no dependencies), and richer content from the prefix's `ffmpeg` program
  (`testsrc2` and the other generators; the oracle fixtures of M4, built in the build tree keyed by
  the custom_ffmpeg version, never committed). `lavfi:` sources in tests use only filters of the
  provider's allowlist (31 filters, §7.2). FFmpeg-dependent tests are skipped (exit 77) with a clear
  message when FFmpeg is absent; with `DVV_REQUIRE_FFMPEG=1` (every Windows run of M4) a missing
  FFmpeg fails them, so coverage cannot disappear silently.
- **Pixel formats (M2):** every catalogue format has a hand-derived layout golden (a 2×2 frame, 6×1
  for v210, with distinct code values and its expected packed bytes, written as literal arrays in the
  test), and v210 has the partial-group goldens 2×1 and 4×1. `unpack(pack(x)) == x` for every format
  at 1×1, 2×2, 33×17, 49×3 and 64×64 with two noise seeds (v210: sizes rounded up to its width
  multiple, 1×1 skipped). The GPU unpack is bit-exact against the CPU reference for the same matrix
  (`dvv_media.gpu`, one submission per format), with zero validation messages.
- **FFmpeg as an independent oracle** (M4, dvv-components). The fixtures are made by the prefix's
  `ffmpeg` program (`tests/oracle/make-fixtures.cmake`, CTest `FIXTURES_SETUP`) in the build tree;
  every reference command uses `-fps_mode passthrough`. The rows marked "(ffmpeg tests)" run in
  `dvv_components_ffmpeg_tests` (label `ffmpeg`); the others in `dvv_components_oracle_tests`
  (label `ffmpeg;oracle`), which run only in non-sanitizer configurations. A failure prints the
  frame, position, value and reference.

| Under test | Reference | Comparison |
|---|---|---|
| Unpack of every catalogue format FFmpeg reads (directly or after a lossless same-depth planar step) | `extractplanes` | bit-exact |
| `ffmpeg` provider originals (lossless-codec container fixtures; forward, backward, random) (ffmpeg tests) | `framemd5` (same `pix_fmt`) | identical hashes |
| `lavfi` provider (ffmpeg tests) | the same graph through `ffmpeg … -f rawvideo` | bit-exact frames |
| `toDisplayReference` + `encodeSrgb8` (M3) | swscale to rgb24 (`accurate_rnd+full_chroma_int+bitexact`): yuv444p and yuv444p10le × {BT.601, BT.709} × {limited, full}; gray, gray10le, gbrp full only; transfer sRGB | ±1 code value |
| Statistics, metrics and their distortions (v1.x, with the metrics) | `signalstats`, `entropy`, `histogram`; `psnr`, `ssim`, VMAF, … | v1.x |
| Motion vectors (v1.x) | exact shifts via `crop`/`scroll`; `mestimate` as plausibility | exact / plausible |
| Edge detection (v1.x) | `edgedetect`, `sobel` | tolerance |

  FATE reference hashes (`tests/ref/fate/filter-pixfmts-*`) confirm pix_fmt layouts; `tiny_psnr` and
  `tiny_ssim` document the algorithm definitions. FFmpeg is used only as an external program/library and
  reference data; no FFmpeg code is copied into the MIT repos.

| Layer | Content | Where |
|---|---|---|
| Unit (CPU) | descriptors, detection, URI, colour matrices, `Error`/`Task`/pool, registry, schema, JSON, result model, lifetime registry and leak check | every CI job |
| Compile-fail | `run-compile-fail.cmake` tests (the build of an `EXCLUDE_FROM_ALL` target must fail with the expected diagnostic): direct construction of a managed object (§9.4) | every CI job |
| GPU correctness | GPU unpack bit-exact vs CPU reference for every format (M2); metrics vs CPU reference and FFmpeg goldens in v1.x; validation + synchronization validation, any message fails | CI on Mesa lavapipe; locally on hardware |
| CLI end-to-end | CTest scripts `tests/cli/run-cli-*.cmake` (rawreel): `info` of synthetic, raw and Y4M sources (text and JSON), `dump` CPU vs GPU byte-identical with converter stats proving the path, error exit codes, a non-ASCII file name, `bench`; every run fails on a leak report, and the GPU scripts on any counted validation message | locally on Windows (NVIDIA); CI without GPU skips the GPU scripts |
| Frame store | dedup, single-flight, pin vs eviction, fidelity rules, device-lost, leak check — with fake host-only frames and converters, no Vulkan needed | CI + TSan |
| Component conformance | for every registered component: valid schema, `probe` does not throw, golden results on fixtures; registry == enabled CMake set; created only via the registry, live count back to zero after destruction and drain (§9.4) | CI |
| Session integration | headless Session (`rawreel.core_session`, host-only store, fake clock and timer, slow/failing test sources): no drops, stall instead of skip, step, seek, loop, an open replacing the stream, serialised opens, injected read/OOM/device-lost errors, Reopen at the kept position, stale results ignored, shutdown with the leak check (2–3 streams in sync: v1.x) | every Windows preset; the concurrency cases ×10 under ASan at checkpoints |
| Display path | `ToDisplay` vs its CPU reference for all 37 formats under 8 colour sets (`dvv_media.gpu`); the fit compositor vs `compositeReference` per pixel (upscale, downscale incl. multi-tap, 1:1, letterbox, pillarbox, dark and light; `rawreel.gpu`) | locally on Windows (NVIDIA), sync validation |
| GUI | four layers: pure units (fit math, player, commands and keys, format choice, drop routing, status and Info text), the headless Session, offscreen GPU goldens, and the `rawreel-gui --selftest` scenarios `smoke`, `player` and `lost`; a manual release checklist with screenshots | locally on Windows (NVIDIA); Windows CI skips the GPU layers (no GPU); Linux on lavapipe (WSLg locally, Xvfb in CI) |
| FFmpeg (M4) | dvv-components: the loader (status, licence, six versions, the deliberate no-FFmpeg entries), the providers (container and `lavfi` fixtures, seek, the ring, the stop flag, the frame check, the allowlists) and the oracle suite above; rawreel: the FFmpeg status line, About text and the `run-cli-ffmpeg-*` CLI scripts. Labels `ffmpeg` and `ffmpeg;oracle` (the oracle only in non-sanitizer configurations); `DVV_REQUIRE_FFMPEG=1` makes a skip a failure | locally on Windows and Linux with the prefix's FFmpeg |
| Package | label `package`: the install and package tests (the zip layout, the third-party notices) run in the full cycles only | locally on Windows; the release workflow |
| Performance | `rawreel-cli bench` → CSV; 4K 10-bit 60 fps checked on hardware with `rawreel-gui --selftest perf` (built in M3.1; E7 passed: median 59.999 fps, 10 of 12 runs clean, seeks at most 122.5 ms, §16) | nightly / manual |

Known limit: FFV1 decoding is CPU-bound, so 4K 60 fps FFV1 playback is not a v1.0 target (about
50 fps for easy content and far less for noisy content on a typical 8-core desktop CPU; FFmpeg's
own decoder is similarly limited on noisy content), while 4K60 raw and Y4M playback is the v1.0
target and is met (E7). v1.x: frame threading.

GPU tests are a separate test executable per repository (CTest label `gpu`). Without a usable Vulkan 1.4
device it exits with 77, which CTest reports as skipped (`SKIP_RETURN_CODE 77`), unless
`DVV_REQUIRE_GPU=1` turns that into a failure (Linux CI, §13.4). Their CTest environment points the
loader at the validation layer of the third-party prefix (`VK_ADD_LAYER_PATH`), and every GPU test checks
at its end that no validation message was counted.

Display tests (M1b, CTest label `display`) need a window: dvv-platform's `surface` test and
dvv-gpu's presentation tests (`dvv_gpu.present`, labels `gpu;display`) use raw native windows from
`TestWindow`, not wx. They exit with 77 without a usable device or a display; `DVV_REQUIRE_GPU=1`
turns a missing display into a failure as well. The GUI selftests `rawreel.gui_selftest.smoke`,
`.player` and `.lost` (labels `gpu;display;gui`) are not built under sanitizers (wx and GTK are not
instrumented) and are registered on Windows and Linux. They drive the real windows only through the
command layer (`MainFrame::execute`), synthesised wx key, scroll and drop events and dialog hooks,
never OS input injection; real input is covered by the manual checklist. Platform scope (U-WIN,
U-LNX1): v1.0 is tested on Windows (NVIDIA locally; the Windows CI runners have no GPU and skip
these tests) and on Linux (lavapipe, under WSLg locally and Xvfb in CI; the Linux sanity cycle
`tools/sanity.sh` runs the `sanity` tests, with `--gui` the display tests too). Both platforms have
a manual release checklist.

Sanitizers: Linux Clang ASan+UBSan and a separate TSan job; MSVC ASan in one Windows job. The GPU tests
run under the sanitizers too. Vulkan drivers are not instrumented, so TSan builds give the GPU tests a
suppression file that matches only reports with a frame inside the driver (`race:libvulkan_lvp.so`).
A report is suppressed when the stack of either racing access, or of the allocation, has a driver
frame, so a race between dvv code and the driver is hidden as well; only races between two accesses
in dvv code are still reported. CI never sets `TSAN_OPTIONS` itself.
Fuzzing (v1.x): libFuzzer on the Y4M parser, filename detection, URI parser and custom-format JSON.

---

## 13. Build, CI, packaging

### 13.1 Repository skeleton
Each repo: `CMakePresets.json`, `cmake/`, `include/dvv/<lib>/`, `src/`, `tests/`, `.clang-format`,
`.clang-tidy`, `LICENSE` (MIT), `README.md`. `cmake_minimum_required(VERSION 4.2)`. Presets: `vs2026`
(Windows), `ninja-clang` (Linux, Ninja Multi-Config).
The presets themselves live once in `rawreel-workspace/cmake/DvvPresets.json`; every repository's
`CMakePresets.json` only includes that file.

### 13.2 Third-party superbuild (rawreel-workspace)
- `deps/versions.cmake`: per dependency name, version, URL, SHA256, SPDX licence expression.
  `THIRD_PARTY_NOTICES.txt` is generated from it.
- **Licence gate:** configure parses each SPDX expression (`AND`, `OR`, `WITH`) and fails unless it is
  satisfiable from an allowlist of licences (MIT, BSD-2/3-Clause, BSD-2-Clause-Patent, Apache-2.0, Zlib,
  BSL-1.0, LGPL for run-time-loaded libraries, …) plus listed exceptions (`WxWindows-exception-3.1`,
  `Bison-exception-2.2`, `LLVM-exception`). Substring matching on "GPL" is not used: wxWidgets is
  `LGPL-2.0-or-later WITH WxWindows-exception-3.1`, and glslang inside Slang carries
  `GPL-3.0-or-later WITH Bison-exception-2.2`. A second, tool-only list (`NCSA`, `CC-BY-4.0`) is
  accepted only for `tool` linkage (build tools such as `slangc`), never for static or run-time
  libraries (§17). Since M1b `libpng-2.0`, `IJG` and `X11` (code bundled in wxWidgets) are allowed
  for static linking as well (§17). Since M4 `LGPL-2.0-or-later` (LAME inside custom_ffmpeg) and
  `BSD-3-Clause-Clear` (SVT-AV1 inside custom_ffmpeg) are allowed for `runtime` linkage only
  (`DVV_SPDX_RUNTIME_ONLY`). The user accepted these five allowances in M4 (§17).
- **Shipped set (M4):** a declaration marked `SHIPPED` is part of the packages. The superbuild
  writes `THIRD_PARTY_NOTICES.txt` from the shipped entries only (it goes into the zip, with the MSVC
  runtime licence) and `THIRD_PARTY_NOTICES-all.txt` from every entry (a developer reference, never
  shipped). `dvv_licence_supplement()` adds a SHA256-pinned licence text that an archive lacks; in
  v1.0 these are SVT-AV1's fastfeat (BSD-3-Clause) and safestringlib (MIT) and libvpl's
  `third-party-programs.txt`, copied after the entry's own licence files in both notices.
  `<prefix>/dvv-third-party-state.cmake` records `DVV_BUNDLE_FFMPEG` and the shipped names for
  rawreel's packaging scripts.
- **`DVV_BUNDLE_FFMPEG` (M4, default `ON`; [U4]: FFmpeg is bundled, §17):** with `ON`,
  custom_ffmpeg is `SHIPPED`, its six libraries go into the package's `ffmpeg/` folder, and the
  release has the sources archive `rawreel-<v>-third-party-sources.tar.xz` (custom_ffmpeg with its
  submodules and build scripts, and LAME 3.100). With `OFF`, the package has no `ffmpeg/` folder
  and no sources archive, and the README explains how to add an LGPL build. Bootstrap passes the
  value on every run.
- Each declaration (`dvv_third_party()`) names its linkage (`static`, `runtime`, `tool`) and its licence
  files; options: `DEPENDS` (build order), `ONLY_ON Windows|Linux` (the gate still checks the entry on
  every platform), `PREBUILT` (an archive installed as is under `<prefix>/<name>`), `POST_INSTALL`
  (a CMake script run after the install) and `CONFIGS <config>...` (M1b; `Debug` and/or `Release`,
  default `Release`, not with `PREBUILT`; a multi-config generator builds and installs each listed
  configuration in order, a single-config generator accepts only `Release`). A checksum change fails
  the build loudly.
- Built statically: wxWidgets 3.3, Vulkan-Headers/Vulkan-Hpp, VMA, nlohmann/json, doctest. MSVC runtime
  `/MD`. `CMAKE_POLICY_VERSION_MINIMUM=3.5` is passed only to dependencies that need it under CMake 4.
  vk-bootstrap is installed with its source and compiled into dvv-gpu (one runtime library setting for
  Debug and Release builds); VMA's implementation is compiled in its own dvv-gpu translation unit.
- wxWidgets 3.3.4 (M1b): MSVC builds Debug (`/MDd`, debug information embedded) and Release into
  one prefix, because a Release-only static wx cannot link into Debug builds; Linux builds Release
  only, with GTK 3 and the system's zlib and libpng. Regex, expat/XML/XRC, rich text, STC, web view,
  media control, OpenGL, web request, secret store, JPEG, TIFF, WebP, lunasvg, Wayland and
  `wxUSE_APPLE_IEEE` are off (not needed through v1; a smaller licence expression). Windows bundles
  zlib, libpng and nanosvg, Linux nanosvg; both always contain wx's IJG and X11 code.
- Built for tests and development: the Vulkan validation layer (Vulkan-ValidationLayers with
  Vulkan-Utility-Libraries, SPIRV-Headers and SPIRV-Tools, all from source; Python 3 is needed), loaded
  by the GPU tests from the prefix. A post-install script makes the Linux layer manifest's library path
  relative. Since M1b the Linux layer is built with window-system support (XCB, Xlib with XRandR,
  Wayland), which needs their development packages. Slang is a prebuilt archive per platform
  (`tool`).
- Loaded at run time, never linked: Vulkan loader, FFmpeg (custom_ffmpeg `lgpl` shared release; its
  GitHub build attestation was verified when it was pinned (M4 Task 0) and is verified again by
  rawreel's release workflow; CI relies on the SHA256 pin), Slang (`slangc` at build time, `libslang` at run time), libvmaf (v1.x).
- `dvv_add_shaders()` runs the pinned `slangc` of the prefix (never one from `PATH`; its version is
  checked) → SPIR-V and reflection JSON embedded as generated `.cpp`, plus reflection → schema code
  (later, §5.4). The Slang pin lives in two places that change together: the archives in
  rawreel-workspace's `deps/versions.cmake` and `DVV_SLANGC_VERSION` in dvv-gpu's
  `cmake/DvvShaders.cmake`; a mismatch fails configuring with a message naming both.
- Bootstrap scripts (`bootstrap.ps1`, `bootstrap.sh`) clone all repos as siblings and build the
  third-party tree. All paths are relative or discovered; nothing machine-specific is committed.

### 13.3 Quality gates
`/W4 /WX` and `-Wall -Wextra -Wpedantic -Werror` for our code only: third-party include directories are
`SYSTEM` (MSVC `/external:anglebrackets /external:W0`), and the VMA implementation lives in its own
translation unit (`src/vma.cpp`), where VMA is a `SYSTEM` include. Third-party sources compiled into
a dvv target (e.g. `VkBootstrap.cpp`) never fail the build on warnings: their source options cancel
warnings as errors (`/WX-`, `-Wno-error`) while the warnings stay visible. Third-party headers are
always included as `SYSTEM`: the clang-tidy header filter (`.*/(include/dvv|src)/.*`) matches any path containing `/src/`,
so a third-party header under such a path would otherwise be checked as ours (§17).
clang-format and clang-tidy (Clang 19) in CI;
conventional commits; a **privacy lint** (absolute machine paths such as `X:\Users\…` or `/home/<name>`,
non-noreply e-mail addresses, IP addresses) in CI and as a local pre-push hook.

### 13.4 CI (GitHub Actions)

| Workflow | Jobs |
|---|---|
| Each dvv repo (PR, push), via one reusable workflow in rawreel-workspace | Windows VS 2026 build + tests; Linux Clang 19 build + tests + lavapipe GPU tests with validation; Clang ASan+UBSan; TSan; lint (format, tidy, privacy); build-only job for Linux Wayland. macOS jobs only after the user's approval |
| rawreel-workspace (nightly, manual) | all repos at `main` from source; integration tests; GUI `--selftest` (Windows with a GPU; Linux with lavapipe under Xvfb); `bench` CSV artifact |
| Release (rawreel `release.yml`, M4) | `workflow_dispatch` with the inputs `version` and `dry_run`; one Windows build job: the superbuild, the packages, `SHA256SUMS`, GitHub build attestations, and a **draft** release with the hand-written notes (`docs/release-notes-<version>.md`). The dry run runs at Gate A, the real run at Gate B (Task 12, each only after the user's go-ahead). The pre-release stages 1.0.0-alpha and 1.0.0-beta (U-M4ORDER) are development builds and are never released |

Third-party builds are cached by the hash of `deps/` and the superbuild modules (the inputs of
bootstrap's own up-to-date stamp); the lint job shares the Linux cache entry and builds its tree before
clang-tidy, because tests include generated shader headers. The lavapipe job needs Mesa ≥ 25.0 (Vulkan
1.4 lavapipe); Ubuntu 24.04 `noble-updates` provides it (25.2.x at review time). The Linux jobs install
`mesa-vulkan-drivers libvulkan1 vulkan-tools`, print `vulkaninfo --summary` and fail unless a device
reports apiVersion ≥ 1.4 (`tools/check-vulkan-device.cmake`); their tests, the sanitizer jobs included,
run with `DVV_REQUIRE_GPU=1`, `DVV_VK_VALIDATION=sync` and `DVV_VK_VALIDATION_REQUIRED=1`. Windows
runners have no GPU, so the GPU tests are skipped there. A change to the reusable workflow reaches
rawreel-workspace `main` before any repository's CI relies on it.

Since M1b the Linux jobs (build matrix, lint and the workspace integration job) also install
`libgtk-3-dev pkgconf libx11-dev libxcb1-dev libxrandr-dev libwayland-dev zlib1g-dev libpng-dev xvfb
xauth` (bootstrap builds wxWidgets and the layer's window-system support, and `bootstrap.sh` checks
these first); the build and integration jobs run their workflow under `xvfb-run -a -s "-screen 0
1920x1080x24"` with `NO_AT_BRIDGE=1`, so the display tests find a display. These are the existing
Linux jobs; v1.0 adds one: the Linux job of rawreel's `release.yml` (U-LNX1, below). The GUI is
not built under sanitizers. The GUI `--selftest` runs locally on Windows with an NVIDIA GPU and on
Linux with lavapipe under WSLg; CI's Windows runners have no GPU and skip it, the Linux jobs run it
under Xvfb.

### 13.5 Packaging
- Windows (v1.0): the portable zip `rawreel-<v>-windows-x64.zip` (CPack ZIP) with `rawreel-gui.exe`,
  `rawreel-cli.exe`, `THIRD_PARTY_NOTICES.txt` and, per
  `DVV_BUNDLE_FFMPEG` (default `ON`; [U4]: bundled), the `ffmpeg/` folder with the six LGPL
  libraries (replaceable or removable; `ffmpeg/SOURCE.txt` names the sources asset). With FFmpeg
  bundled the release also has `rawreel-<v>-third-party-sources.tar.xz` (the complete corresponding
  source of the bundled libraries). The PDBs ship as `rawreel-<v>-windows-x64-symbols.zip`. Not
  code-signed in v1 (README notes the SmartScreen warning).
- The zip ships no VC++ runtime (U-DIST1; no `vcruntime140*`, `msvcp140*` or `concrt140` file, and
  package-smoke checks it): the user installs the Microsoft Visual C++ Redistributable (x64), at
  least as recent as the build tools (Microsoft's rule). The minimum version is kept in one place
  and checked against the toolset at configure time; the README's Requirements names it with
  Microsoft's latest-redist link and the "MSVCP140.dll was not found" error. A setup wizard that
  brings the runtime and checks the prerequisites is v1.x.
- Linux (v1.0, U-LNX1): the package `rawreel-<v>-linux-x64.tar.xz` (CPack TXZ) with one folder
  `rawreel-<v>/`: `rawreel-gui`, `rawreel-cli`, README, LICENSE, `THIRD_PARTY_NOTICES.txt` and, per
  `DVV_BUNDLE_FFMPEG`, the same `ffmpeg/` folder as the zip.
  - RUNPATH rule: the exes have no RUNPATH or RPATH (they find FFmpeg at run time, as on Windows);
    each FFmpeg library has RUNPATH `$ORIGIN`, so it finds its siblings.
  - SONAME files: each FFmpeg library ships as a real file named by its SONAME
    (`lib<name>.so.<major>`, no symlinks); configure fails when a file's major differs from the
    header's.
  - Symbols: Release builds with `-g` and a GNU build ID; the debug info is split into
    `<exe>.debug` with a `.gnu_debuglink` and ships as `rawreel-<v>-linux-x64-symbols.tar.xz`.
  - Baseline: Ubuntu 24.04 x64; the exes need at most GLIBC 2.39 and GLIBCXX 3.4.33 (checked by
    the package smoke), and every NEEDED soname must be in the allowed system list.
  - System dependencies: `libgtk-3-0t64 libsm6 libice6 libvulkan1 mesa-vulkan-drivers` (one list in
    packaging, named by the README and installed in the release's clean-container check). wx is
    linked statically, and the Vulkan loader and drivers come from the system.
  - Notices: GTK, GLib, X11 and the other system libraries are dynamic system dependencies and are
    not shipped, so they need no notices; libstdc++ and libgcc_s fall under the GCC Runtime Library
    Exception; static wx is covered by its WxWindows exception, already in the notices.
  - One third-party sources archive (built with the Windows package) covers both packages, which
    bundle the same custom_ffmpeg commit; the release checks that the commits match.
- v1.x: the AppImage (bundling wx/GTK; Vulkan loader and drivers from the system; FFmpeg in
  `lib/ffmpeg`).
- v1.x: macOS `.app`/`.dmg`; Flatpak possibly.
- Versions: SemVer per repo, tagged from v1.0 on (§3.1); versions shown in *About* and `--version`.
  v1.0.0 has hand-written release notes; the changelog generated from conventional commits
  (git-cliff) starts with v1.1.

### 13.6 GitHub
New public repos under the user's account, created only after the user's explicit go-ahead at that
time. Commits use the GitHub handle and noreply address.

---

## 14. Conventions
- clang-format: Google style, 4-space indent, 100 columns.
- Names: PascalCase types, camelCase functions/methods, snake_case variables, trailing underscore for
  members, UPPER_SNAKE constants, `I` prefix for interfaces.
- Headers `.h` with `#pragma once`; sources `.cpp`.
- Managed `dvv::` objects: `final` classes, `CreationKey<F>` constructors, `Managed<T>` base, creation
  through the authority's factory, smart-pointer ownership, strong references only upwards, `weak_ptr` or
  ids for observers (§9.4).
- `.gitattributes` in every repo normalises line endings (LF in the repo; CRLF only for `.ps1`, `.bat`,
  `.cmd`, `.sln`).
- Conventional commits; no machine-specific or personal data in any committed file or message.

---

## 15. Milestones and plans

One implementation plan per milestone; each milestone ends with working, tested software.

| Milestone | Content | Demo |
|---|---|---|
| **M0 Foundation** | rawreel-workspace (`.gitattributes`, presets incl. `/std:c++23preview`, `dvv-versions.cmake`, bootstrap, superbuild with SPDX licence gate, privacy lint, reusable CI workflow); dvv-base (`Error`, `co_try`/`DVV_TRY`, `CreationKey`/`Managed<T>` + lifetime registry + leak check, logger, `Task`/executors/thread pool, `dvv::json`, URI parser, paths) | CI green on Windows and Linux; base unit tests incl. compile-fail tests |
| **M1a GPU core (headless)** | dvv-registry; dvv-gpu without presentation (loader, `GpuRegistry`, `ExecutionContext` with one universal queue, `GpuBuffer`/`GpuImage` + retire queue, `ResourcePool`, submitter + waiter, pipeline cache, Slang build integration); device generation in the data model | Test runs a `slangc`-built compute shader and verifies a buffer round trip on lavapipe in CI and on local hardware; Capabilities data printed |
| **M1b Platform and presentation** (done) | dvv-platform (win32/x11 surfaces, dynamic libraries); dvv-gpu `Presenter` with its own thread and resize handshake; minimal wx frame with a native child viewport | wx window with a Vulkan viewport showing a shader test pattern; resize and minimise work (on Windows) |
| **M2 Media** (done) | new repository dvv-media: pixel-format engine (one descriptor model), the 37-format catalogue with layout goldens, CPU reference, custom formats as JSON; `FrameStore` with the trimmed public API (§6.5), four `IFrame` implementations and five converters (HostToStaging, Upload, GpuUnpack, Readback, CpuUnpack); generic GPU unpack shader; `ISource`/`ISourceProvider` + `SourceFactory` with the synthetic, raw and Y4M providers (`dvv_media_sources`). dvv-platform `FileReader`; dvv-gpu images through push descriptors; dvv-registry kind check; rawreel `rawreel-cli` (`info`, `dump`, `bench`) | CLI `info` and frame dump, CPU and GPU byte-identical; GPU unpack bit-exact for all formats |
| **M3 Player** (done; re-scoped by U-SCOPE) | dvv-gpu device lost (lost state, fail-fast submissions, bounded waits, safe destruction, `testing::injectDeviceLost`, required `shaderStorageImageReadWithoutFormat`); dvv-media display path (`display.h`, the `ToDisplay` converter and shader with custom Kr/Kb matrices and PQ/HLG tone mapping, the planner's colour-changing edge, URI `transfer`/`primaries`/`matrix=custom`, Y4M colour overrides); rawreel `rawreel_core` (the Session with player, media clock, prefetch and Reopen; fit transform; command table; format choice; drop routing), `rawreel_render` (the fit compositor), `rawreel-gui` (wxAUI frame, one viewport with one stream fit to window, transport bar, Info and Log panes, status bar, infobar with Reopen, format and Open URI dialogs, hotkeys, drag-and-drop, dark mode, `--gpu`, selftests `smoke`, `player`, `lost`) | Usable single-stream raw video player (Windows) |
| **M3.1 Performance** (done, U-M31) | the 4K60 performance run (`rawreel-gui --selftest perf`, E7); disk reads per U-IO2 (Task B3: unbuffered by default on Windows with a buffered fallback, buffered as the portable mode, one read in flight per file source, `rawreel.json` config file; no IOCP); `stop_token` cancellation not triggered (seeks at most 122.5 ms; v1.x, X4) | 4K `yuv420p10le` at 60 fps measured: E7 pass with a thin margin, median 59.999 fps with 0 stalls, 10 of 12 runs clean (§16) |
| **M4 Sources and release** (done; re-scoped by U-SCOPE and the M4 re-scope) | new repository dvv-components with the `ffmpeg` component: FFmpeg loaded at run time (never linked), the providers `ffmpeg` (any container FFmpeg demuxes, lossless video codecs only, U-FF) and `lavfi` (allowlisted filters), the conformance test and the FFmpeg oracle; dvv-platform `LibrarySearch::DllDirectory`; dvv-media `formatAlias` and `uri_colour.h`; superbuild custom_ffmpeg pin, `SHIPPED` notices and `DVV_BUNDLE_FFMPEG`; rawreel `rawreel-cli --version` and Help → About with the FFmpeg status, the Windows minidump and symbols, the Windows zip, the third-party sources archive and the release workflow | **v1.0.0 release** (the result of M4; published by the gated Task 12) |
| **Linux for v1.0** (U-LNX1, before the publication) | rawreel-gui on Linux X11/XWayland (X11-only GDK, the GTK native child viewport), the GUI selftests on Linux, the Linux package (`.tar.xz`, split symbols, the system-library baseline), the release workflow's Linux job with a clean-container check, the Linux sanity cycle `tools/sanity.sh` | the Linux package passes its smoke (GUI under Xvfb) and the user's GUI check under WSLg |
| v1.x | multi-stream (tiles, sync, comparison), the pipette, grids, region zoom, region save, all export (raw/PNG/Y4M, GUI and CLI), viewport zoom/pan, fullscreen, automatic device-lost recovery and the GPU menu; frame statistics, histogram, MSE/PSNR, SSIM, MS-SSIM, PSNR-HVS, CIEDE2000, metrics table, graph, CSV/JSON, CLI `metrics` (U-SCOPE); processors (edges), user Slang shaders + editor, libvmaf, motion vectors, native Wayland, the Linux file-I/O default review and frame-timer jitter check, macOS, HDR swapchain, multi-GPU placement, different resolutions, external host memory import, FFmpeg Vulkan decode, fuzzing, crash reporter | |
| v2 | Vulkan VMAF port; C++ modules experiment for leaf libraries once Clang, libstdc++ and CMake on the Ubuntu 26.04 baseline support `import std` and named modules | |

The first plan covers **M0**.

v1.0 scope and platform: U-SCOPE (§1.1) decides what v1.0 delivers; the M4 re-scope only checks it.
M1b to M4 followed U-WIN (Windows only): each milestone tested on Windows, kept the code portable
and ended with one Linux sanity build (configure and build, no tests). U-LNX1 then brought the
Linux tests, ASan/TSan, the GTK GUI and the Linux package into v1.0, in one Linux milestone before
the publication. The milestone records below keep their Linux deferrals as written at the time;
native Wayland, the Linux file-I/O default review and the frame-timer jitter check stay v1.x.

**M1a demo** (dvv-gpu GPU tests, §12): `tests/gpu_test_compute.cpp` runs the `slangc`-built `add_one`
shader over a buffer (round trip of 1000 values), two dependent dispatches in one submission (the
barrier check of §16) and a pooled buffer reused only after the GPU finished with it;
`gpu_test_submission.cpp` holds the buffer → image → buffer round trip and the deferred destruction of
resources a submission uses, `gpu_test_pipeline.cpp` the pipeline-cache round trip. `dvv-gpu-info`
prints the capabilities data.

**Deferred from M1a** (each lands with the milestone or component that first needs it):
- `dvv_add_component()` with `KIND`, `SHADERS`, `REQUIRES` and the per-component CMake option (§9.1), and
  the component conformance test (§12): with dvv-components. Done in M4 without `SHADERS` (v1.x,
  with the first shader component) and without `REQUIRES` (dropped).
- Component interfaces beyond `ISourceProvider` (`IMetric`, …): with dvv-components. Version
  migration of session parameters: with session files (v1.x).
- Slang reflection → `ParameterSchema` (§5.4), which also replaces the push-constant structs written by
  hand (today only their size is checked against the reflection): with the first component that has
  shader parameters.
- A test that editing a Slang module a shader imports rebuilds the shader (`dvv_add_shaders` passes
  slangc's depfile to the build, untested so far): with the first shader that imports a module.
- Images in `ResourcePool` (§5.3): when measured (F10). A leased image needs another design, because
  the command recorder keeps images through `shared_from_this()`, which a lease must not hand out.
- Tests of the out-of-memory paths (the submitter's signal-only batch and host-signal fallback, the
  resource pool's give-back) and of the pipeline cache's rarer paths (data the driver rejects after
  validation, a save that cannot write): reviewed, not tested; with fault injection (§12).
- Finer, per-resource barriers instead of the recorder's global barrier (§16): once measured.
- Automatic `flush()`/`invalidate()` of host-visible buffers around submissions (today the caller
  calls them; both do nothing on host-coherent memory): once a component needs it.
- TSan suppressions built into the test executable (`__tsan_default_suppressions()`) instead of
  `TSAN_OPTIONS` (§12): optional.
- vk-bootstrap 1.4.365 passes empty application and engine names to the driver (upstream bug): fixed by
  a later vk-bootstrap or a post-install patch.

**M1b demo** (Windows, NVIDIA): `rawreel-gui` shows a test pattern (75 % colour bars, a grey ramp, a
white border) rendered by a Slang compute shader in its Vulkan viewport, and redraws through resize,
maximise, minimise/restore and close with no validation message and no leak. `rawreel.gui_selftest`
automates the check (§10.2); the user checked it by hand.

**Deferred from M1b** (target in brackets):
- More than one frame in flight; a pipelined provider → submit → present (M3 kept one; v1.x unless
  the M3.1 4K60 run measures a need, X21).
- MAILBOX/IMMEDIATE present modes as an option, a vsync preference (v1.x, X21).
- `VK_KHR_swapchain_maintenance1` present fences: exact retirement of the old swapchain instead of
  `vkQueueWaitIdle` (v1.x, X21).
- Correct colour on screen: done in M3 (the `RGBA16F` display image and the compositor's sRGB
  encode into an `RGBA8` image, skipped for an `_SRGB` swapchain format); HDR (v1.x).
- A graphics-pass (dynamic rendering) blit instead of `vkCmdBlitImage`, which also removes the
  `TRANSFER_DST`/`BLIT_DST` requirement on the swapchain (v1.x; M3 keeps the blit).
- Presenter device-lost recovery: M3 replaces the context and the presenter through the user's
  Reopen (§5.2); automatic recovery on a new device generation is v1.x.
- A swapchain readback hook for render goldens: not needed; M3's goldens read back the
  compositor's own image (§12).
- Wayland (`wl_subsurface`) and macOS (`CAMetalLayer`) `NativeWindow` alternatives (v1.x).
- `LibrarySearch::ApplicationDirectory` for the FFmpeg DLLs next to the executable (M4). (The file
  reader landed in M2 as `FileReader`, §10.4.)
- Windows presentation coverage in CI through a Mesa lavapipe build for Windows (v1.x; needs a
  licence review and a pin).
- File log sink and the Log panel; `wxInfoBar` errors, status-bar fps, dark mode, AUI (done in M3).
- The M1a minor findings left in the progress ledger: bootstrap after an interrupted first run and
  stamp removal on `--force`, README exit-code lists, SPDX exception pairing, privacy-lint edge
  cases (not done in M2; triaged with the M2 minor items).
- Sanitizer suppressions for GTK/GLib/fontconfig, so the GUI smoke test can run under ASan (v1.x).
- Surface-lost recovery by recreating the presenter for a new native window, e.g. when a viewport is
  re-parented (v1.x; M3's viewport is a fixed centre pane and is never re-parented).
- U-WIN (v1.x when M1b ended; U-LNX1 later moved into v1.0: `rawreel-gui` on Linux (the GTK
  native child viewport, `--selftest` under WSLg and Xvfb, the manual check), the display and
  presentation tests on Linux, and the Linux ASan and TSan CI runs). Still v1.x: a review of
  X11/xcb sanitizer suppressions and the lavapipe present-time measurement.

**M2 demo** (Windows, NVIDIA, sync validation, zero validation messages; Linux had the sanity build
only, decision U-WIN). The three exit criteria and the tests that prove them:
1. `rawreel-cli info <uri> [--json]` describes a synthetic source, a raw file and a Y4M file
   (`rawreel.cli_e2e.info`, text and JSON).
2. `rawreel-cli dump` writes the original bytes or the unpacked planes, and `--via cpu` and `--via gpu`
   give byte-identical files; the converter stats prove the path: `GpuUnpack` ran for planes and
   `Upload` for the original under `--via gpu`, and no GPU converter under `--via cpu`
   (`rawreel.cli_e2e.dump`, `rawreel.cli_e2e.gpu`).
3. The GPU unpack is bit-exact for all 37 catalogue formats at all their test sizes and two seeds
   (`dvv_media.gpu`, under the `vs2026` Debug and Release and the `vs2026-asan` presets); the layout
   goldens of all 37 formats and the v210 partial-group goldens are in `dvv_media.unit`.

**Deferred from M2** (the decided list; target in brackets):
- Bindless descriptor heap (F12, v1.x if ever): replaced by push descriptors; a heap only if
  push-descriptor limits ever bind.
- Specialisation-constant fast paths for the unpack (when measured).
- IOCP async reads on Windows (F1): dropped. The `bench` gate rule is replaced by the user
  decision U-IO2 (§17; it supersedes U-IO and the Task B2 choice): unbuffered reads by default on
  Windows with a buffered fallback, buffered reads as the portable mode, one read in flight per
  file source (M3.1 Task B3, §16); no IOCP and no overlapped read-ahead. io_uring on Linux only
  if measured (v1.x); mmap and `MappedFrame` (v1.x); reads straight into staging (F3, later).
- Sharded store index (when measured); `stop_token` cancellation: v1.x (X4; M3.1 Task C was not
  triggered, the 4K60 run measured `seekMaxMs` of at most 122.5 ms; M3 drops stale results); the
  prefetch policy: M3 built the fixed window (D11), adaptive prefetch is v1.x (X6);
  `VK_EXT_memory_budget` budgets (v1.x, X5); image pooling (when measured, F10).
- `ToDisplay` and the colour engine (done in M3); the media clock (done in M3).
- The `IFormatDetector` component kind (with the second detector, v1.x).
- The custom-format editor dialog (v1.x, X7; M3's format dialog loads JSON formats, `fmt=@…`).
- `AVPixelFormat` mapping and the FFmpeg `extractplanes`/`framemd5` oracle (M4).
- U-WIN (v1.x): the Linux test runs (the lavapipe GPU tests, the POSIX `FileReader` tests, Linux
  ASan, TSan and UBSan), the lavapipe device check and the lavapipe `bench`.

**Inherited by M3** (with the targets U-SCOPE and U-M31 gave them):
- `ToDisplay` and the colour engine: done in M3.
- The media clock: done in M3.
- Prefetch: M3 built the fixed window (D11); `stop_token` cancellation is v1.x (X4; M3.1 Task C
  was not triggered, seeks at most 122.5 ms); adaptive prefetch v1.x (X6).
- `VK_EXT_memory_budget` budgets: v1.x (X5).
- The custom-format editor dialog: v1.x (X7); M3 loads JSON formats in the format dialog.
- F1, IOCP on Windows, from the `bench` rule (§16: Produce seemed the bottleneck at 0.46 GB/s, an
  artifact of the summed-time metric), and the 4K60 check: M3.1 (Tasks B, B2, B3 and 11b). The gate
  rule (Task B: F1 not built, drive-limited) is replaced by U-IO2 (§17): unbuffered reads are the
  Windows default with a buffered fallback per file, buffered reads the portable mode, one read in
  flight per file source, the mode in a config file (Task B3, §16). No IOCP; io_uring on Linux only
  if measured (v1.x).
- Risk for the 4K60 target (E7), as M3.1 Task 11b measured it (§16): the read path. Upload plus
  GpuUnpack take 4.6 ms per 4K frame (the M2 estimate of 16.6 ms is void). The read is the slowest
  stage (70.7 frames/s alone, one read in flight), and the pipeline up to unpack runs at 61.0
  frames/s, about 1.7 % headroom over 60 fps. E7 passed with a thin margin, so M4 Task P is not
  opened; the candidate fixes (§16) wait for v1.x, or for Task P if a later E7 run misses.

**M3 demo** (Windows, an NVIDIA desktop GPU, sync validation, zero validation messages; Linux had
the sanity build only, U-WIN). The exit criteria (M3 design v2 §1, narrowed to Windows) and what
proves them:
- E1 headless player: the Session integration suite (`rawreel.core_session`, in
  `rawreel_core_tests`) passes in `vs2026` Debug and Release and in `vs2026-asan`; its concurrency
  cases, the timer, `DisplayState` and the renderer's race case pass ×10 in a row under
  `vs2026-asan` (`rawreel\.(core_session|core_timer|unit|gpu)$`).
- E2 display path: `dvv_media.gpu` (ToDisplay against its CPU reference for all 37 formats under 8
  colour sets, incl. custom Kr/Kb, PQ and HLG) and `rawreel.gpu` (the fit compositor goldens:
  upscale, downscale incl. multi-tap, 1:1, letterbox, pillarbox, dark and light) pass with zero
  validation messages.
- E3 device lost: the dvv-gpu loss tests (`dvv_gpu.gpu`, `dvv_gpu.present`; ×10 under ASan), the
  `rawreel.gpu` Session case (loss mid-playback → `error`, the live context count 0 within 5 s,
  Reopen → the stream is back, paused at the same frame) and `rawreel.gui_selftest.lost` pass.
- E4 (export): cut by U-SCOPE.
- E5 GUI selftests: `rawreel.gui_selftest.smoke`, `.player` and `.lost` pass in Debug and Release
  (×5 each in M3 Task 11a).
- E6 manual checklist: walked through on Windows (M3 Task 11a record; the user's verdicts at the
  M3 alpha review).
- E7 (4K60): moved to M3.1 (U-M31); passed there with a thin margin (M3.1 results below, §16).

**Deferred from M3** (the decided deferrals, M3 design v2 §2; target in brackets):

| # | Item | Target |
|---|---|---|
| X1 | Session files + parameter migration | v1.x |
| X2 | Preferences dialog (`--gpu` meets "selectable"; default budgets) | v1.x |
| X3 | Frame-dropping option | v1.x |
| X4 | `stop_token` cancellation | v1.x (M3.1 Task C not triggered: `seekMaxMs` at most 122.5 ms, trigger > 250) |
| X5 | `VK_EXT_memory_budget` budgets | v1.x |
| X6 | Adaptive prefetch; timestamp bottleneck | v1.x |
| X7 | Custom-format editor dialog (M3 loads JSON) | v1.x |
| X8, X9 | Custom matrices, PQ/HLG tone mapping | done in M3 |
| X10 | Glyph-atlas text overlays | v1.x |
| X11 | Error placeholder frame (M3 keeps the last frame; background + infobar after a GPU failure) | v1.x |
| X12 | Capabilities page (today Help → GPU info) | v1.x (M4 re-scope; the FFmpeg status is in `--version` and About) |
| X13 | Frame-store debug panel (today Help → Diagnostics) | v1.x |
| X14 | Properties pane | v1.x |
| X15 | *Open generator…* with lavfi (today *Open URI…*) | v1.x (M4 re-scope; `lavfi:` URIs open through *Open URI…*) |
| X16 | `ITool`/`IExporter` registry kinds | v1.x |
| X17 | Bilinear chroma with `ChromaLocation` | v1.x |
| X18 | Reverse play, speed, per-stream offset | v1.x |
| X19 | Persisted UI state | v1.x |
| X20 | Compressed PNG | v1.x, with PNG export |
| X21 | More frames in flight, mailbox, maintenance1 | v1.x unless the M3.1 4K60 run measures a need |
| X22 | Everything U-SCOPE moved (multi-stream, pipette, grids, region zoom/save, export, zoom/pan, fullscreen, automatic recovery, per-device generations) | v1.x |

**Deferred from M3 (U-WIN → v1.x):** the Linux GUI legs (the WSLg and Xvfb selftests, the WSLg E6
walk-through, the GTK checks incl. real GTK drag-and-drop and CHAR_HOOK focus on the native child),
the lavapipe GPU runs and the Linux ASan/TSan runs.

**M3 Linux sanity build:** the workspace configured fresh and built with `ninja-clang` (Clang 19,
libstdc++ 14) in WSL Ubuntu 24.04, no tests: built with no warning or error in our code.

**M3.1 results** (U-M31; Tasks B3 and 11b, §16):
- E7, the 4K60 run (`rawreel-gui --selftest perf`; a 4K `yuv420p10le` raw file plays 600 frames;
  pass at ≥ 59.5 fps, 0 stalls after the warm-up and at most 6 overwritten): **pass, with a thin
  margin.** The median of three runs is 59.999 fps with 0 stalls and 0 overwritten; 10 of 12 runs
  were clean, both misses were stalls. One data point (U-HW); §16 states what a typical consumer
  PC needs.
- Disk reads: U-IO2 built (Task B3): unbuffered reads by default on Windows (buffered fallback per
  file), buffered reads as the portable mode, one read in flight per file source, the mode in
  `rawreel.json`. IOCP not built, no overlapped read-ahead.
- Task C not triggered (seeks at most 122.5 ms against the 250 ms trigger), so `stop_token`
  cancellation (X4) is v1.x.
- No E7/R7 miss to carry: M4 Task P is not opened. The measured risk is the read path (about 1.7 %
  pipeline headroom); the candidate fixes are in §16.

**Inherited by M4** (as the M4 re-scope decides): the swscale display-colour oracle (§12, with
FFmpeg sources); GUI *About* and `--version`; the Windows minidump (§11); MSVC path stripping
(§16); X12 (Capabilities page) and X15 (*Open generator…* with lavfi); the manual Reopen check
after a real TDR (R14: the driver may refuse a new device while old references drain; the infobar
keeps Reopen). The Properties pane (X14) and the Vulkan graph in a dockable pane leave the M4 list
(→ v1.x). The M4 re-scope kept the oracle, About, `--version`, the minidump and path stripping
(§16, D16); X12 and X15 went to v1.x.

**M4 demo** (Windows, NVIDIA, sync validation; exit criteria E1–E6 of the M4 design, adapted to
U-WIN and U-FF):
- E1 FFmpeg sources: dvv-components `dvv_components.ffmpeg` (label `ffmpeg`) decodes the lossless
  container fixtures (FFV1 in MKV and the others, made in the build tree) to originals equal to
  `ffmpeg -f framemd5`, including a fixture without `pts` (`Sequential`) forward, backward and at
  random; `lavfi:` frames equal `ffmpeg … -f rawvideo`; colour keys on an `ffmpeg` URI override the
  container; a codec outside the lossless allowlist is refused. `dvv_components.ffmpeg_absent` and `.ffmpeg_relative`
  check a missing FFmpeg and a relative `DVV_FFMPEG_DIR`, the unit test the GPL refusal, and
  `dvv_components.no_ffmpeg_imports` that no binary imports FFmpeg.
- E2 oracles: `dvv_components.oracle` (label `ffmpeg;oracle`, non-sanitizer configurations):
  `extractplanes` bit-exact against our unpack for every catalogue format FFmpeg reads, and the
  display colour within ±1 code value of swscale (§12).
- E3 application: rawreel's CLI end-to-end tests run `info`, `dump` and `bench` on `ffmpeg` and
  `lavfi` URIs and check `--version`; `rawreel-gui --selftest smoke` opens a `lavfi:` URI and About;
  the M3 selftests stay green; a forced crash leaves one minidump.
- E4 packages: the local dry run (Task 8) builds the zip, the symbols zip and, when bundled, the
  sources archive, and runs the package smoke, the manifest and notices checks and the privacy scan.
- E5 manual checklist on the packaged Windows build (Task 11, the user's verdicts).
- E6 publication: gated, Task 12 (Gate A dry run of `release.yml`, Gate B tags and release, each
  only after the user's go-ahead).
Records: the M4 plan's `## Changes during execution` (Tasks 8, 11 and 12). v1.0.0 is the result of
M4; it ships with the tags after Gate B.

**Deferred from M4** (M4 re-scope and U-WIN; target in brackets): the analysis interfaces, every
metric and analyser component, the metric oracle suite and libvmaf, `convert:` and
`--compare-format`, frame offsets, the analysis runner, results and CSV/JSON, the CLI `metrics` and
`stats`, the GUI analysis panes [v1.x, with the metrics]; the Capabilities report
(`rawreel-cli capabilities`, the pane) and `--diag` [v1.x]; Open generator presets (X15) and the
Open dialog FFmpeg filter [v1.x]; `--ffmpeg-dir` [v1.x, if users ask]; `SHADERS` in
`dvv_add_component` and `gpu.compute` [v1.x, with the first shader component]; `Api::AvFrame`
and FFmpeg Vulkan decode, hardware decoding, `?stream=N`, a lazy or decode-built index, more
catalogue entries for decoder outputs [v1.x]; NSIS and code signing [v1.x]; the Linux `.tar.xz`,
the AppImage, Linux symbols, the Linux tests, ASan/TSan and GUI [v1.x, U-WIN] (moved back into
v1.0 by U-LNX1, except the AppImage); fuzzing the FFmpeg
provider [v1.x]; git-cliff [from v1.1].

---

## 16. Risks and open items
- **Vulkan 1.4 on CI and WSL:** Ubuntu 24.04 `noble-updates` ships Mesa ≥ 25 with a Vulkan 1.4 lavapipe
  (§13.4). In WSL the GPU driver (dzn) reports below 1.4, so WSL GPU tests run on lavapipe. Confirmed in
  M1a: lavapipe of Mesa 25.2.8 reports Vulkan 1.4.318 under Ubuntu's 1.3.275 loader (§5.1). Older
  hardware without a 1.4 driver (e.g. pre-Gen12 Intel iGPUs on Windows) is intentionally unsupported.
- **VS 2026 runner image:** resolved in M0. GitHub's `windows-2025` image ships Visual Studio 2026
  since June 2026; the CI asserts version 18.x before building, so an image change fails loudly.
- **Machine paths in MSVC binaries:** Clang builds map the workspace root away with
  `-ffile-prefix-map`; MSVC still embeds absolute `__FILE__` and PDB paths. Resolved in M4 (D16):
  release builds run in neutral roots (the CI runner's workspace, or a neutral root for the local
  dry run), Release exes are linked with `/PDBALTPATH:%_PDB%` (the PDBs go into the separate symbols
  zip), and the packaging's privacy scan refuses a package that contains a user-home or machine path.
- **M4 risks (one line each):**
  - R2: an `extractplanes` or display-colour oracle row disagrees: an unpack mismatch is fixed in
    dvv-media with a regression test; a tolerance changes only by a recorded decision.
  - R6: FFmpeg's DLL dependencies are not found from `ffmpeg/`: the loader uses
    `LibrarySearch::DllDirectory`, and the package smoke runs from the extracted zip.
  - R7: the index scan of a large container is slow: the open runs on the I/O pool; a lazy index
    is v1.x.
  - R8: decoder output formats outside the catalogue make common files `Unsupported`: the message
    names the format; more catalogue entries are v1.x.
  - R12: the LGPL obligations of the bundled FFmpeg are judged insufficient: the sources archive;
    [U4] is decided (bundled); `DVV_BUNDLE_FFMPEG=OFF` stays the build option without FFmpeg.
  - R13: sanitizer reports inside FFmpeg's threads: library-scoped suppressions under the §12 rule.
  - R14: the release workflow cannot create a release or attest: it lives in rawreel (its own
    token), attestations cover the built files only, and a manual upload of the workflow artifacts
    is the last resort.
  - R15: publication shows CI failures that local runs did not: the Gate A fix loop, with the privacy
    scan, before any tag.
  - R16: scope: cut order (1) the minidump and symbols asset, (2) the display-colour oracle rows;
    never cut the FFmpeg sources, the package, the licence gate, the sources archive when bundled,
    the gated release.
  - R17: `Sequential` sources (`lavfi:`, files without `pts`) restart from the start on a backward
    seek: documented in the README; the ring and the FrameStore cache serve short steps back; a
    decode-built index is v1.x.
  - R11 (the Linux glibc/GTK baseline of the `.tar.xz`): v1.0 with the Linux package (U-LNX1,
    §13.5): GLIBC ≤ 2.39, GLIBCXX ≤ 3.4.33, system sonames from an allow-list, checked by
    package-smoke and the release workflow's clean-container run.
- **No TSan on Windows:** data races are found by review, Windows ASan and the Linux TSan CI job
  (U-LNX1).
- **Synchronization validation does not see buffer device addresses** (M1a spike): a missing barrier
  between two dispatches that use the same buffer through its address goes unreported. The command
  recorder therefore inserts barriers automatically and conservatively: a global memory barrier (all
  earlier compute and transfer writes → later reads and writes) before every work command (dispatch,
  copy, fill, image transition, the latter combined with the layout change), and at the end of every
  submission a barrier that makes all writes visible to the host. Measured in M1a: the dependent-dispatch
  test (two `add_one` dispatches over the same 1000 values in one submission, §15) fails without the
  automatic barrier on NVIDIA hardware in 5 of 5 runs, every value incremented only once, while
  synchronization validation (required and active) reports nothing; with the barrier it passes. Commands
  recorded through `raw()` get the barrier before them but must order themselves among each other (and
  bring their own barriers for stages other than compute and transfer). Finer, per-resource barriers
  are an optimisation for later, once measured.
- **Slang licence:** checked at pinning (M1a). The conservative SPDX expression of the release archive
  includes `GPL-3.0-or-later WITH Bison-exception-2.2`, `NCSA` and `CC-BY-4.0`; it passes the gate only
  as a build tool (§13.2, §17). Loading `libslang` at run time for user shaders (v1.x) needs a new
  licence review.
- **wx 3.3 native child window + direct Vulkan surface on GTK/X11:** proven with SDL in the predecessor
  cube project. Exercised in v1.0 (U-LNX1): the GTK native child with its own Xlib surface (§10.4);
  1x scale only. On Windows the native child works (M1b).
- **wx and WSI after M1b:**
  - One MSVC prefix holds the Debug and Release static wx: wx's own CMake config keeps them apart,
    so the planned separate Debug prefix was not needed (resolved in M1b).
  - Lavapipe WSI under Xvfb in CI (FIFO on an unmapped window, the MIT-SHM path) is open until the
    first published CI run; the 100 ms acquire timeout and the hidden logic already avoid unbounded
    waits.
  - `vkQueuePresentKHR` under FIFO holds the queue mutex: measured in M1b on NVIDIA with sync
    validation (the layer hooks the present), the longest present under the lock per run was
    0.14–0.86 ms in steady state, with single outliers of 1.2–2.4 ms; presents right after a
    resize took 0.29–2.06 ms. Without validation 0.14–1.0 ms. Accepted; more swapchain images or
    MAILBOX come only if the M3.1 4K60 run measures a need (X21).
  - `vkQueueWaitIdle` on each swapchain recreation can make an interactive resize stutter: accepted
    until exact retirement (§15, M1b deferrals).
  - Sanitizer reports inside libX11, xcb or Mesa WSI threads: the display tests run in the Linux
    ASan and TSan CI jobs under Xvfb with only the lavapipe suppression (`race:libvulkan_lvp.so`,
    §12); rawreel-gui is not built under sanitizers (wx and GTK are not instrumented). A report
    inside libX11 or xcb gets a library-scoped suppression under the §12 rule.
  - The fence-acquire, semaphore-less present scheme (§4) produced no synchronization-validation
    hazard on NVIDIA. VUID-VkSwapchainCreateInfoKHR-imageExtent-01274 during an interactive
    drag-resize is a known false positive (the window changes between the capabilities query and the
    create).
- **File reads:** since M3.1 Task B3 (U-IO2) unbuffered by default on Windows with a buffered
  fallback per file, buffered with a sequential-access hint as the portable mode, one read in
  flight per file source; validated on Windows by the FileReader and source tests (an injected
  open-time failure exercises the fallback; a real network share was not tested); on Linux the
  `pread` path runs in the FileReader and source tests of the Linux CI (U-LNX1); the Linux file-I/O
  default review stays v1.x. mmap is v1.x.
- **Upload and unpack throughput (M2 `bench`):** on an NVIDIA desktop GPU with a local
  NVMe SSD (Windows 11, Release build, unbuffered reads), 60 frames of 4K yuv420p10le at K = 4 ran
  at 46.4 frames/s end to end (1.16 GB/s), with Produce at 0.46 GB/s, HostToStaging 3.79 GB/s,
  Upload 2.24 GB/s and GpuUnpack 4.56 GB/s per run (the median run of three, by frames/s; per-run
  stage times include allocation and queue waits, so they are not device or link rates). D18 rule: on local
  NVIDIA + NVMe with 4K yuv420p10le at K = 4:
  - end-to-end ≥ 60 fps → the universal queue stays (spec §17 default) and no M3 item;
  - otherwise the bottleneck stage's fallback becomes an M3 item:
    - Produce < 3 GB/s → F1 (IOCP on Windows / io_uring);
    - Upload < 3 GB/s → a transfer queue;
    - otherwise the R6 order: one submission for upload + unpack, then prefetch, then reads into
      staging (F3), then F1.

  Outcome: M3 item: F1 (IOCP on Windows / io_uring) (bottleneck Produce at 0.46 GB/s). Upload is
  also below 3 GB/s, but the rule takes the bottleneck stage's fallback only. The reads already
  overlap, so the `bench` gate at K = 1 and K = 8 (three runs each, medians) and any IOCP work are
  planned for M3.1 (Task B Step 0; IOCP only if K = 8 beats K = 1 by at least 20 %; §15; superseded
  by U-IO2, see Task B3 below). `bench`
  K is a batch size, not a sustained queue depth (§10.3).

  Gate (M3.1 Task B Step 0, same setup, three runs each): median Produce 1.823 GB/s at K = 1
  (runs 1.823, 1.756, 1.826; 27.9 to 40.0 frames/s) and 0.405 GB/s at K = 8 (runs 0.455, 0.405,
  0.387; 56.3 to 62.1 frames/s). K = 8 is 0.22 × K = 1, below the 1.2 × needed, so **F1 is not
  built: drive-limited**. Produce GB/s divides bytes by the summed time of overlapping reads, so it
  falls as K grows; by wall clock (at most 4 reads at once on the 4-thread I/O pool) K = 8 moves
  1.54 to 1.62 GB/s, not more than one read at a time (1.82 GB/s, about 73 frames/s of 4K
  yuv420p10le). The M2 figure of 0.46 GB/s at K = 4 is this effect of the metric. The next R6
  items wait for a controller decision; E7 is measured by M3.1 Task 11b.

  Read path (M3.1 Task B2, user decisions U-HW, U-IO and U-SIMPLE, §17; they replace F1 and the
  gate rule above): the target is a typical modern consumer PC (an NVMe or SATA SSD), not the
  development machine, and the simplest read path that is fast enough wins. `rawreel-cli bench
  --read-only` (new: only Produce runs, so frames/s and GB/s are the read stage's wall clock)
  read 120 frames of 4K `yuv420p10le` (2.99 GB, 24883200 bytes a frame) from an
  NVMe SSD (PCIe 3) on the same PC, Release build, no validation, three runs each, medians; cold
  means the file's cached pages were purged before the run:

  | Read path | K = 1 | K = 2 | K = 4 | full pipeline, K = 4 |
  | --- | --- | --- | --- | --- |
  | (a) unbuffered (M2), cold | 1.74 GB/s (70.0 fps) | 1.77 GB/s | 1.92 GB/s (77.3 fps) | 56.7 fps |
  | (a) unbuffered, warm | 1.82 GB/s | 1.83 GB/s | 1.89 GB/s | 59.6 fps |
  | (b) buffered + sequential hint, cold | 1.22 GB/s (49.0 fps) | 1.52 GB/s | 1.82 GB/s (73.1 fps) | 59.3 fps |
  | (b) buffered + sequential hint, warm | 2.41 GB/s | 5.10 GB/s | 6.85 GB/s (275 fps) | 72.4 fps |

  4K60 of this format needs 1.49 GB/s. K is the number of reads in flight; the player keeps K = 4
  requests in flight on a 4-thread I/O pool (D11), so K = 4 is the configuration that plays. There
  (b) is 5.5 % below (a) from a cold cache, above the stream rate, and up to 3.6 × (a) from a warm
  one; end to end it is as fast or faster. A second, interleaved cold series with the committed
  code (a and b alternating, three runs) confirmed it: K = 4 1.83 against 1.91 GB/s (−4 %), K = 2
  1.59 against 1.79, K = 1 1.24 against 1.87, full pipeline 54.6 against 51.4 fps. The warm gain
  needs the OS to keep the file cached: read-only re-reads stayed at 6.4 to 6.5 GB/s, but runs of
  the full GPU pipeline evicted part of the file, and later runs read partly from the drive. One read at a time (K = 1), (b) is 30 % below (a) and below
  the stream rate, but the player never reads that way. A scratch harness with plain `ReadFile`
  loops gave the same picture (2 to 4 parallel reads: buffered 2.08 to 2.36 GB/s, unbuffered 2.31
  GB/s; one read at a time 1.20 against 1.75 GB/s). **Decision (U-SIMPLE):** (b): `FileReader` reads
  through the OS file cache with `FILE_FLAG_SEQUENTIAL_SCAN` (Linux
  `posix_fadvise(POSIX_FADV_SEQUENTIAL)`) on the existing overlapped handle and I/O pool; the
  unbuffered mode, its alignment rules, probe and fallback are removed; no overlapped read-ahead API
  and no IOCP (an overlapped window of 2 to 4 reads from one thread could at best match the 2 to 4
  parallel reads the pool already issues). For consumer drives: a SATA SSD (about 0.55 GB/s) is
  drive-bound either way (4K60 of this format needs an NVMe drive); a faster PCIe 4 or 5 NVMe drive
  could make unbuffered reads faster than cached copies, but the cached path already passes 1.49
  GB/s with a wide margin, and a replayed file that fits in RAM no longer touches the drive. This
  is one machine, one data point; Task 11b measures E7.

  Read path, revised (M3.1 Task B3, user decision U-IO2, §17; it supersedes U-IO and the (b)
  choice above): unbuffered reads are the Windows default (open-time check: sector size,
  unbuffered open, first aligned read; any failure reads that file buffered, with one log line),
  buffered reads with the sequential-access hint are the portable mode, and each file source has
  at most one read in flight (a mutex in `FileReader::readAt`: no setting limits only file reads;
  the player's K and the I/O pool stay). The mode is set by `rawreel.json` next to the executable
  (`io.fileReads`). Same bench, file and drive as above, cold, unbuffered and buffered
  interleaved, three runs, medians, one read in flight: unbuffered 2.00 GB/s (80.3 fps), buffered
  1.32 GB/s (53.2 fps); `--inflight 4` gives the same (1.95 and 1.28 GB/s). Full pipeline at
  K = 4: unbuffered 64.8 fps, buffered 44.5 fps. With one read in flight only the unbuffered mode
  meets the 1.49 GB/s of 4K60 on this drive; a file that falls back to buffered reads (some
  network drives, an unusual sector size) plays slower, which U-IO2 accepts. One data point (U-HW).
  B3 review fix: with reads serialised, the overlapped handle and the per-read event gained
  nothing, so both modes now use synchronous handles with plain positional `ReadFile` calls
  (U-SIMPLE). Same bench, one read in flight, the overlapped and the synchronous build
  interleaved on one new file, three runs, medians: unbuffered 1.93 against 1.94 GB/s (77.5 and
  78.1 fps, unchanged), buffered 1.16 against 1.41 GB/s (46.5 and 56.7 fps, 22 % faster, still
  below 1.49 GB/s).
- **Playback throughput (M3 `perf`):** on the machine above (an NVIDIA desktop GPU, an NVMe SSD,
  unbuffered reads, Release, validation off, 120 Hz display, frame maximised, K = 4), 600 frames
  of a 4K `yuv420p10le` raw file played at a median of 59.999 fps with 0 stalls after the warm-up,
  0 overwritten FrameSets, a p99 tick lateness of 0.246 ms and seeks of 59.0 ms median and
  106.8 ms max (`rawreel-gui --selftest perf`, three runs, the median by fps). E7 rule: pass when
  actual fps ≥ 59.5, 0 stalls and at most 6 overwritten of 600; `seekMaxMs > 250` would trigger
  M3.1 Task C. **Verdict: pass, with a thin margin:** 10 of 12 runs (K = 4, 6 and 8) passed, the
  two misses were stalls (58.0 fps with 4 stalls; 59.7 fps with 1 stall). `bench` up to unpack runs
  at 61.0 frames/s; the slowest stage is the read (alone 70.7 frames/s, 14.1 ms per frame with
  one read in flight), while Upload + GpuUnpack take 4.6 ms per frame (the M2 risk of 16.6 ms is
  gone).
  Seeks stayed at or below 122.5 ms in every run, so Task C is not triggered. One data point (U-HW).

  **What a typical consumer PC needs (U-HW)** for 4K60 of uncompressed 10-bit 4:2:0: the stream is
  24,883,200 bytes per frame, 1.49 GB/s. The file must be read at about 1.5 GB/s or more,
  sustained, in single large requests (one read in flight, U-IO2), so in practice an NVMe SSD with
  unbuffered reads (the Windows default); about 1.8 GB/s gives the read headroom measured here
  (about 18 %). SATA SSDs (about 0.55 GB/s) and hard disks cannot play this format at 60 fps; a
  mainstream NVMe drive that sustains less (e.g. some DRAM-less or QLC drives) is borderline or
  below. The buffered fallback (e.g. a network drive, an unusual sector size) may not keep up and
  drop below 60 fps: the player then stalls on late frames (it waits for a frame, it never skips
  one). Even with that read headroom the whole pipeline here has only about 1.7 % headroom: 2 of 12
  ten-second runs stalled, roughly one stall per minute of 4K60 10-bit playback (one data point).
  The measured headroom on the development machine, as one data point: median of 3 runs 59.999
  fps with 0 stalls; 10 of 12 runs clean; the read stage, the slowest, at 70.7 fps; upload plus
  GPU unpack 4.6 ms per frame.

  Candidate fixes for more margin, in the measured order (M3.1 final review; not built; v1.x, or M4
  Task P if a later E7 run misses): (1) the read path: about 2.3 ms per frame of non-read work does
  not overlap the read (16.4 ms pipeline against 14.1 ms read), name that stage; (2) reads straight
  into staging (F3, drops the 3.36 ms HostToStaging copy) and/or a larger prefetch window for 4K,
  where W is capped at 8 frames (133 ms; one constant, U-SIMPLE) and one read hiccup drains it for
  seconds at 1.7 % refill headroom; (3) last, upload + unpack in one submission (4.6 ms, not
  limiting). More than one read in flight per file needs the user's approval (U-IO2).
- **Module status** for a possible future move: see the separate research note kept outside the repo.

## 17. Review decisions

IDs of the form U-<name> mark maintainer decisions; this section summarises them.

Decisions taken in the written-spec review (2026-10-20):
- **Vulkan 1.4 stays the minimum** (§2, §5.1). It is a learning goal; the CI risk is handled as in §16.
- **Multiple repositories stay** (§3.1); friction is reduced in the tooling, not by merging repos.
- **M1 is split** into M1a (headless GPU core) and M1b (platform and presentation) (§15).
- **Factory-only creation and lifetime registry** for managed `dvv::` objects added as a global rule (§9.4).
- **Lossless is the default analysis input** (§6.6); byte-level tools request Exact.
- **macOS CI only after the user's approval** (§1.1, §13.4).
- **Accepted from the review:** separate presenter threads and a resize handshake (§4); a portable
  `co_try` (§11); libstdc++ 14 and explicit `/std:c++23preview` (§2); optional 16-bit shader
  arithmetic (§5.1); a single universal queue in v1 (§5.2, §6.3); device generation in `Domain` (§6.1);
  a trimmed `FrameStore` API and capped task layer (§4, §6.5); SPDX-expression licence gate (§13.2);
  warnings as errors for our code only (§13.3); FetchContent safeguards and one reusable CI workflow
  (§3.1); a CI job that fails on skipped FFmpeg tests (§12); X11 recipe (§10.4); bindless and the async
  reader moved to M2, device-lost recovery to M3 (§15).
- **Defaults taken, open to change:** v1 uses one universal queue (a transfer queue only if the M2
  benchmark needs it); the leak check fails Debug and CI and only warns in Release.

Decision taken during M0 (2026-10-20):
- **Linux uses Clang only** (§2, §13.1, §13.4): Clang 19 with libstdc++ 14 (`libstdc++-14-dev`, no
  GCC compiler). GCC is not supported. libc++ 19 was rejected: it lacks `std::move_only_function` and
  `std::generator`, and `std::jthread` needs `-fexperimental-library` there.

Decisions taken during M1a (2026-10-20 and 2026-10-21):
- **Device API 1.4, loader ≥ 1.3** (§5.1): the instance requests 1.4 but accepts Ubuntu 24.04's 1.3.275
  loader. The validation layer must be ≥ 1.4 and comes from the third-party prefix (§5.1, §13.2).
- **Tool-only licences** (user decision, 2026-10-20): `NCSA` and `CC-BY-4.0` are accepted only for
  `tool` linkage, never for static or run-time libraries (§13.2). Slang's `slangc` needs them.
- **Third-party sources compiled into a dvv target never fail the build on warnings** (user decision,
  2026-10-21): per-source `/WX-` (MSVC) or `-Wno-error` (Clang) cancels the target's warnings as errors
  for that file only; the warnings stay visible (§13.3). First case: `VkBootstrap.cpp` in dvv-gpu.
- **Third-party headers are always `SYSTEM`** (user decision, 2026-10-21): the clang-tidy
  `HeaderFilterRegex` `.*/(include/dvv|src)/.*` stays as it is (it must cover private headers in `src/`),
  although it also matches third-party paths containing `/src/` (such as the prefix's
  `src/vk-bootstrap`); `SYSTEM` include directories keep those headers out of the checks (§13.3).
- **GPU tests** (§12): a separate executable per repository, label `gpu`, exit 77 = skipped unless
  `DVV_REQUIRE_GPU=1`; every test fails on a counted validation message; the sanitizer jobs run them too.
- **TSan and uninstrumented drivers** (§12): TSan builds suppress only reports with a frame inside the
  Vulkan driver (`tests/tsan.supp`, `race:libvulkan_lvp.so`, passed by CTest through `TSAN_OPTIONS` in
  thread-sanitizer builds). A race between dvv code and the driver is hidden too (either racing
  access, or the allocation, has a driver frame); only races between two accesses in dvv code are
  still reported.
- **Automatic conservative barriers** in the command recorder, because synchronization validation
  cannot see buffer device addresses (§16); without them the dependent-dispatch test fails on hardware
  while validation stays silent.
- **ResourcePool returns a buffer at once, tagged** (§5.3): instead of "returned once retired", a
  released lease goes back to the pool immediately with the latest reserved timeline value and is handed
  out again only once that value is reached (the same guarantee, no extra queue). Images wait for M2.
- **Pipeline cache** (§5.4): one file per device by default; header validated and a dvv checksum
  trailer checked before the driver sees the data; an unusable file is ignored with a log line, never an
  error; saved atomically on request and, when it changed, on close. `PipelineCache`, like
  `Submitter`, is an internal part, not a managed object (§9.4).
- **Errors of GPU work** (§11): commands the recorder cannot record and malformed pipeline requests are
  programming errors (`Internal`); a push-constant size above the device's limit is `Unsupported`.
- **Layering:** dvv-gpu depends only on dvv-base in M1a (dvv-platform arrives in M1b); GPU capabilities
  are `GpuInfo`, turned into registry `Capabilities` by a higher layer (§5.2).
- **vk-bootstrap builds only the instance**; physical devices, the feature check and the device use
  dvv-gpu's own Vulkan-Hpp code with explicit dispatchers (one per instance and per device, no global
  default dispatcher). The loader is loaded at run time and never unloaded, because vk-bootstrap keeps
  the first `vkGetInstanceProcAddr`.

Decisions taken during M1b (2026-10-23):
- **Windows only for v1.0** (user decision U-WIN; for Linux replaced by U-LNX1 below): the code
  stays portable (Windows-only code in `*_win32.cpp`/`*_msw.cpp` files or `#if defined(_WIN32)`
  blocks; the X11/GTK paths keep compiling) and Linux builds, but Linux support (its tests,
  sanitizers and the GUI) follows in v1.x. Each milestone ends with one Linux sanity build
  (configure and build, no tests). Accepted risk: no TSan while only Windows is tested (§16). This
  held for M1b to M4; U-LNX1 brings Linux X11/XWayland into v1.0.
- **Licences for static linking** (controller decisions, accepted by the user in M4): `libpng-2.0`
  (wxWidgets' bundled libpng on Windows; permissive but not OSI-approved; the second reason, the M3
  PNG exporter, is void for v1.0 since PNG export is v1.x, and `libpng-2.0` stays allowed for wx's
  bundled copy), and `IJG` and `X11` (code wxcore always contains: a JPEG-derived colour quantizer
  and an XPM decoder) (§13.2).
- **M1b design decisions D1–D17** (in full in `2026-10-23-m1b-design.md`, which code comments cite
  as "design M1b"), in short:
  - D1: the wx frame lives in the new `rawreel` repository (`rawreel-gui`), not in a demo of a
    library repository.
  - D2: surfaces come from two free functions over a closed `NativeWindow` variant, not an
    `ISurfaceFactory`; dvv-platform returns `VkResult`, dvv-gpu converts it (§10.4, §11).
  - D3: `DynamicLibrary` is a move-only handle (§9.4); `LibrarySearch::SystemDirectory` loads
    `vulkan-1.dll` from System32 only (no process-wide DLL search change);
    `vk::detail::DynamicLoader` is dropped.
  - D4: fence acquire, host wait, semaphore-less present; rendering goes through the submit thread
    (§4).
  - D5: exactly one frame in flight.
  - D6: the client renders into its own `GpuImage`, which the presenter blits (scaled when sizes
    differ) into the swapchain image.
  - D7: lazy swapchain recreation on the presenter thread between frames; the old swapchain is
    destroyed after `vkQueueWaitIdle`.
  - D8: the surface's `currentExtent` decides the size; `resize()` is a hint; 0×0 means hidden.
  - D9: visibility is explicit (`setVisible` on iconize/restore and hide/show, `requestFrame` on
    paint); nothing runs while hidden.
  - D10: the presenter's decisions are a pure, GPU-free unit-tested class (`PresenterLogic`).
  - D11: presentation tests use raw native windows (`TestWindow`), not wx; only the GUI smoke test
    uses wx, outside sanitizer builds.
  - D12: one skip switch: display tests exit 77 without a device or a display unless
    `DVV_REQUIRE_GPU=1`.
  - D13: wxWidgets 3.3.4, static; MSVC Debug and Release, Linux Release with GTK 3; the unneeded
    parts are off (§13.2).
  - D14: wx's licence is `LGPL-2.0-or-later WITH WxWindows-exception-3.1`, plus the bundled code's
    licences (above).
  - D15: window-system support is opt-in (`GpuRegistryOptions::windowSystem`,
    `ContextOptions::presentation`, `OptionalFeatures::swapchain`); the registry is headless by
    default.
  - D16: swapchain: `B8G8R8A8_UNORM` or `R8G8B8A8_UNORM` with `SRGB_NONLINEAR`, else the first
    format, which must be UNORM or SRGB; `TRANSFER_DST` usage and `BLIT_DST` required; FIFO only;
    `max(minImageCount + 1, 2)` images, capped.
  - D17: the M1a leftovers (`createComponent<I>`, the UTF-8 environment API, `vulkan-1.dll` from
    System32) land in M1b on their repositories.

Decisions taken in M2 (2026-10-23 and 2026-10-24; in full in `2026-10-24-m2-design.md`, which code
comments cite as "M2 design D<n>"), in short:
- D1: the core source providers (synthetic, raw, Y4M) live in dvv-media as the component library
  `dvv_media_sources`, not in dvv-components.
- D2: one descriptor model: per-plane pixel groups with sample slots; `widthMultiple` (v210: 2) (§7.3).
- D3: the M2 catalogue is 37 formats, all bit-exact; custom formats as JSON in M2 (the editor
  dialog: v1.x since U-SCOPE).
- D4: bit-exactness is checked against the generated code values and a hand-derived layout golden
  per format (§12); the FFmpeg oracle joins in M4.
- D5: unpacked planes are one `R8_UINT`/`R16_UINT` image per component, LSB-aligned, `GENERAL` at
  rest; the host form is planar, tight and little-endian (§6.3).
- D6: images reach shaders through push descriptors, not a bindless heap (§5.4); a heap is F12.
- D7: one generic unpack shader driven by push constants; fast paths when measured.
- D8: `FileReader` is a concrete handle with blocking positional reads on an I/O pool, with a
  probe at open and a buffered fallback (§10.4); io_uring (only if measured) and mmap later; no
  IOCP (U-IO2). (M3.1 Task B2, U-SIMPLE, removed the unbuffered mode; Task B3, U-IO2, restored it
  as the Windows default with the open-time check and the buffered fallback, and made reads one at
  a time per file, §16. The B3 review replaced the overlapped handle and per-call event with
  synchronous handles.)
- D9: a raw frame is read into an aligned host frame and copied into staging (one extra copy).
- D10: store simplifications: one mutex, conversions complete before publishing (no `retire()`),
  prefetch in M3 (cancellation: v1.x, M3.1 Task C was not triggered), fixed budgets, OOM evicts to
  half the budget and retries once, no
  image pooling (§6.5).
- D11: sources are attached to the store explicitly (`attach`/`detach`).
- D12: every requester resumes on its own executor (`IExecutor& resume_on`).
- D13: the M2 converters are HostToStaging, Upload, Readback, GpuUnpack and CpuUnpack; `ToDisplay` in
  M3 (§6.4).
- D14: providers implement `ISourceProvider` and get their typed key through `OpenTicket` +
  `issueKey<T>`; the registry marks a component whose kind differs from `InterfaceKind<I>`
  unavailable; `IFormatDetector` waits for a second detector.
- D15: Y4M: parameterless `FRAME` only, the `C` mapping, a missing `C` is `420jpeg`, `XCOLORRANGE`
  (§7.2).
- D16: synthetic patterns `gradient` and `noise`, scalar CPU code packed by the CPU reference.
- D17: the media clock moves to M3.
- D18: a minimal `rawreel-cli bench` (batches of K acquires, `whenAll` per batch, so K is the batch
  size, not a sustained queue depth) with a rule that turns the bottleneck stage's fallback into an
  M3 item below 60 fps (§16; the item moved to M3.1, U-M31).
- D19: `rawreel-cli` lives in the rawreel repository, without wx; on Windows `wmain` with UTF-8
  arguments, and every UTF-8 → path conversion goes through `pathFromUtf8`.
- D20: colour is carried, not converted: `ColourParams` interned as `ColourId`; `acquireOriginal`
  forces the frame's own colour.
- Design choices of the M2 review (arbiter):
  - The planner works over a static converter graph with published **and pending** entries as
    sources; "no path" depends only on format, colour and target (§6.5).
  - The GPU unpack loads two dwords at the absolute byte address, correct for any plane offset (§5.4).
  - v210 needs an even width; odd-width v210 is not a dvv extension.
  - "Flag accepted but I/O refused" is detected by a probe read at open, so `readAt` stays `const`
    and thread-safe.
  - `--via` selects the target domain, and a `HostUpload` staging frame offers only the `VkBuffer`
    view, so the converter stats prove the path without an API addition.
  - The kind check runs at `ComponentRegistry::create(caps)`, not as a `static_assert`.
  - `ColourId` interning stays (custom matrices in M3 need a table), and all 37 formats keep their
    layout golden.
  - Storage images use `format("unknown")` with `shaderStorageImageWriteWithoutFormat` and a
    per-format check, so one pipeline serves both container sizes.
  - `bench --inflight 4` with an end-to-end 60 fps rule naming the bottleneck stage.

Decisions taken in M3 (2026-10-23 and 2026-10-24; the M3 design v2, re-scoped by U-SCOPE, which
code comments cite as "design M3" or "design v2 D<n>"):
- User decisions: **U-SCOPE** (§1.1: v1.0 is a single-stream viewer with the full colour mapping,
  fit to window and Reopen after a device loss; multi-stream, tools, export, zoom/pan, fullscreen,
  automatic recovery and all analysis → v1.x; M4 = FFmpeg sources, packaging and release).
  **U-M31**: the 4K60 run (E7 and its risk), faster disk reads (IOCP) and `stop_token`
  cancellation move to a milestone M3.1, planned after M3 (M3.1 or M4 first is decided at the M3
  review); an E7 miss not fixed there becomes M4 Task P. **U-M3A**: M3 alpha (the player and its
  GUI checks) ends with a user review; M3 beta/release (spec, READMEs, final verification) follows.
  **U-HW** (M3.1): the target is a typical modern consumer PC, not the development machine; prefer
  simple solutions. **U-IO** (M3.1): storage reads run in parallel (2 to 4 in flight) issued from
  one thread with overlapped I/O and a small read-ahead window; no new reader thread pool and no
  IOCP worker pool. **U-SIMPLE** (M3.1, later, takes precedence): common-sense approaches, no
  vendor-specific hardware features and no very low-level OS APIs unless they bring a significant
  measured gain; measure first, and build the overlapped read-ahead only if plain buffered reads
  with the OS read-ahead are more than about 25 % slower than the best option and below the
  stream rate. Under them M3.1 Task B2 measured, kept the M2 I/O pool (whose K = 4 reads in flight
  are the parallelism) and replaced F1 and the `bench` gate rule with buffered reads and a
  sequential-access hint; the overlapped read-ahead was not built (§16). **U-IO2** (M3.1, later,
  supersedes U-IO and that choice): two read implementations behind one interface; buffered
  reads (sequential-access hint) are the portable one, unbuffered reads (`FILE_FLAG_NO_BUFFERING`,
  sector-aligned) the Windows default, because only they meet the higher read rates; the
  unbuffered path is checked at open (sector size, unbuffered open, first aligned read) and any
  failure falls back to buffered for that file with one log line; no parallel file reads (one read
  in flight per file source; the frame store's prefetch is enough, the thread pools stay); a JSON
  config file of the application (`rawreel.json` next to the executable, `--config` overrides it)
  can force the mode (`io.fileReads`: `auto`, `unbuffered`, `buffered`). M3.1 Task B3 built it (§16).
- D1: one milestone, ordered headless-first: the Session and the display path are tested before
  any window.
- D2: one viewport, one stream; `MAX_STREAMS = 1`, but `FrameSet` keeps its per-stream array and
  the Session its per-slot state; `open` of slot ≥ 1 is `Unsupported` (multi-stream tiles → v1.x).
- D3: display path ToDisplay (store converter) → fit compositor (compute) → presenter blit; no
  overlays.
- D4: ToDisplay math: custom Kr/Kb, PQ/HLG through the BT.2390 EETF (1000 → 100 nits), the
  `Unspecified` defaults of §8, nearest chroma; the Info pane says "tone-mapped".
- D5: ToDisplay reads the planes as storage images without a format
  (`shaderStorageImageReadWithoutFormat`, §5.1).
- D6: the planner's colour-changing edge; `maxPlanDepth` 6.
- D7: device lost: detect → error → the user reopens (§5.2); no automatic rebuild and no
  per-device generation counter (→ v1.x with automatic recovery); a Session stack epoch drops late
  results and reports.
- D8: device loss is testable through `testing::injectDeviceLost`, kept in the main library so the
  shipped binary's `--selftest lost` checks "error, no hang".
- D9: the GPU menu and the "3 losses in a row" limit → v1.x; `--gpu N` at start stays.
- D10: the Session is a wx-free actor in `rawreel_core`: frames go straight to `DisplayState`,
  events through `ISessionEvents` posted to the GUI thread, status polled every 50 ms.
- D11: the player: `PlayerLogic` + `ThreadTimer`, no drops, a fixed prefetch window, K = 4 in
  flight (§4).
- D12: tools and exporters as application classes → v1.x (no tools or exporters in v1.0).
- D13: ruthless cuts, U-SCOPE included.
- D14, D15, D16: the in-tree PNG encoder, PNG export on the CPU and the pipette from the exact host
  original → v1.x (`codeValuesAt` is not built).
- D17: hotkeys from one wx-free command table; bare keys through `wxEVT_CHAR_HOOK`, Ctrl keys as
  menu accelerators; the key normaliser kept as designed.
- D18: wxAUI layout with a fixed centre pane and the Info and Log panes.
- D19: dark mode through `SetAppearance` at start (`--appearance`).
- D20: status bar fields: position, fps, bottleneck (zoom and export progress → v1.x).
- D21: opening rules: one stream; an open replaces it; `Ambiguous` or an undetectable raw file
  raises the format dialog; View → Format… rebuilds the URI query and reopens at the kept position.
- D22: export granularity → v1.x with export.
- D23: fit to window only; the renderer computes the fit from the presenter's extent and the image
  size (no GUI round trip); `CompositeParams` keeps only `dark`.
- D24: fullscreen → v1.x (the user maximises the window).
- D25: one Reopen path for every GPU-side failure; a presenter failing with another error is not
  recreated automatically.
- M3 re-scope and plan choices: the first GPU stack is built synchronously in `Session::create`
  (a startup GPU failure exits 4), every later one on the I/O pool as a job of the open FIFO;
  `releaseStack()` flushes a pending show, so the kept position is the last frame shown; Reopen
  reopens the stream through the normal open path (a new source and origin, never a source shared
  by two stores); one dropped file opens and the rest are logged, a second command-line URI exits
  1; the selftests are `smoke`, `player` (incl. drop, format dialog, View → Format…, resize) and
  `lost`; the exit-criterion numbers E1–E7 are kept with E4 (export) cut.

Decisions taken in M4 (2026-10-23 to 2026-10-25; the M4 design v2 and the M4 plan, which code
comments cite as "M4 design D<n>"):
- User decisions: **U-WIN**: v1.0 supports Windows only; the code stays portable (one Linux sanity
  build per milestone), and the Linux tests, ASan/TSan, GUI and package are v1.x (for Linux replaced
  by U-LNX1, below). **U-FF**: the
  FFmpeg source accepts any container FFmpeg demuxes but only lossless video codecs (an open-time
  decoder allowlist); MPEG-family and other lossy codecs are out of scope for v1.0. Task 0's
  codec-scope outcome: the pinned custom_ffmpeg 1.0.0 build is the full LGPL build (no lossless-only
  build exists), so the [U4] patent note and the `BSD-3-Clause-Clear` allowance are kept (both
  accepted by the user, below), LAME is present; the runtime allowlist applies in every case.
  **U-M4ORDER**: packaging (Task 8) runs after release preparation, which is split into
  1.0.0-alpha, 1.0.0-beta and 1.0.0; the pre-release versions are development builds, never
  released.
- M4 re-scope (one line each): M4 = FFmpeg sources (container and `lavfi:`), packaging and the gated
  release, every analysis item → v1.x; dvv-components holds only the `ffmpeg` component; a reduced
  oracle suite (`extractplanes`, `framemd5`, `rawvideo`, swscale display colour); the Linux package
  and the AppImage → v1.x (with U-WIN; U-LNX1 brings the Linux package back); the sources archive (custom_ffmpeg and LAME 3.100) only when
  FFmpeg is bundled; the FFmpeg status is one line of `--version` and About, the Capabilities report
  and `--diag` → v1.x; generator presets → v1.x; the minidump and the symbols asset stay; `ffmpeg`
  URIs take the colour keys as overrides, a `lavfi:` stream says "set the colour in the graph"; one
  store per FFmpeg source.
- M4 design choices (one line each): `release.yml` is `workflow_dispatch` only with a `dry_run`
  input (dry run at Gate A, real run at Gate B, a draft release); the dry run skips attestation;
  Release PDBs with `/PDBALTPATH:%_PDB%` as a separate symbols asset (D16); the FFmpeg ring is capped
  at `min(8 frames, 256 MiB)`; `stopFfmpegSources()` bounds exit, close and Reopen drain in the
  background; `main` keeps the v1.0.0 pins after the release.
- Controller decisions: zip only, NSIS → v1.x (former U3); the Linux system-GTK baseline (former
  U6, back in v1.0 with the Linux package, U-LNX1); `DVV_BUNDLE_FFMPEG` (default `ON`) decides the bundled
  FFmpeg, the shipped set and the sources archive.
- User decisions of 2026-10-25 (they confirm the controller defaults):
  - [U4]: FFmpeg is bundled in the package (`DVV_BUNDLE_FFMPEG` stays `ON`), with the patent note
    and the sources archive. `DVV_BUNDLE_FFMPEG=OFF` stays only as the build option for a package
    without FFmpeg.
  - Licences accepted: `LGPL-2.0-or-later` (LAME inside custom_ffmpeg, pinned LAME 3.100; former
    U1, part) and `BSD-3-Clause-Clear` (SVT-AV1 inside custom_ffmpeg) in the runtime-only list, and
    the static-linking licences `libpng-2.0`, `IJG` and `X11` (M1b, above).
  - The Linux legs of the shared `dvv-ci.yml` (Clang 19 + libstdc++ 14) are in place before the
    first publication.
- [U5] (publication): asked at publication (Task 12).
- GUI check fixes (U-GUI1, U-ABOUT1, user 2026-10-26; §10.2): the GUI closes the stream before an
  open (this changes D21's "an open replaces it"); the format dialog only when no provider matched
  or the match is ambiguous, a recognised but refused file is an error (a raw-like one with a
  Format… button); a stream change dismisses the infobar; every cancel is logged; the dialogs
  start in the clip's folder; *Open URI…* keeps the last text; the D25 command is named *Restart
  GPU and reopen* (button Restart GPU); the clip view stays at least 320×240 DIP and the docks give
  way; the About layout as approved.

Decisions taken for Linux in v1.0 (2026-10-27; the Linux milestone plan,
`docs/superpowers/plans/2026-10-27-m4l-linux.md`):
- User decision **U-LNX1**: v1.0 gets full Linux support. It overrides U-WIN for Linux:
  `rawreel-gui` runs on Linux X11 and XWayland (GDK restricted to X11, the GTK native child
  viewport), the Linux tests run again (lavapipe; WSLg locally, Xvfb in CI; ASan and TSan in CI),
  and the release gets a Linux package (§13.5) with the bundled FFmpeg, built and checked by the
  release workflow's Linux job (including an install of the package's system dependencies into a
  clean Ubuntu 24.04 container). Native Wayland, the Linux file-I/O default review (buffered `Auto`
  stays) and the Linux frame-timer jitter check stay v1.x. It also overrides the "no behaviour
  changes" rule of the GUI check for the Linux scope only; Windows behaviour is unchanged.
- Choices (one line each): Linux has no crash handler in v1.0 (the system's core dump, the symbols
  asset; see Crashes); GDK is limited with `gdk_set_allowed_backends("x11")`, not by changing environment
  variables while GTK's threads run (§10.4); the Linux sanity cycle is `tools/sanity.sh` and checks
  every cloned repository (no change detection, because git status from WSL on a Windows checkout
  is unreliable); one third-party sources archive covers both packages.
