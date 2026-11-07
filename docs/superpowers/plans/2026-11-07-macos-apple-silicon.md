# macOS on Apple Silicon Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** rawreel and the dvv libraries build, test and ship on Apple Silicon macOS 27+, first
from source (stage A), then as an unsigned `rawreel.app` in a `.dmg` (stage B).

**Architecture:** Every library gets its macOS code where its Windows/Linux counterparts live
(`*_posix`, `*_win32`, `*_xlib` files gain `*_metal`/`*_cocoa`/`__APPLE__` siblings). All library
changes are made first on `feat/macos` branches inside rawreel's `libs/` and proven end to end on
the Mac (a local slice up to a playing `rawreel-gui`); only then is each library released in
dependency order and pinned, following dvv-build's "Changing a library" flow. Vulkan runs on
KosmicKrisp from a pinned LunarG macOS Vulkan SDK; the Vulkan loader is built from source; FFmpeg
is custom_ffmpeg v1.0.1, loaded at run time as today.

**Tech Stack:** C++23, Apple clang (Xcode 27 / CLT, macOS 27 SDK) with the system libc++, CMake
4.2+, Ninja, Objective-C++ for Cocoa, Vulkan 1.4 (KosmicKrisp, `VK_EXT_metal_surface`),
wxWidgets 3.3.4 `osx_cocoa`, Slang 2026.20, doctest, GitHub Actions `xcode-27` runners.

**Spec:** `docs/superpowers/specs/2026-11-07-macos-apple-silicon-design.md` (decisions U-MAC1…10,
Claude's calls C1…C12). Read it before any task; this plan cites it by those ids.

**Plan review (2026-11-07):** an optimist and a pessimist reviewer, two rounds, rulings by the
planner; the reports are not committed. Its changes are folded in below and listed at the end.

## Global Constraints

- Apple Silicon (arm64) only, **macOS 27.0 minimum**: `CMAKE_OSX_DEPLOYMENT_TARGET=27.0` and
  `CMAKE_OSX_ARCHITECTURES=arm64` for **our code and every third-party project the superbuild
  builds**; `LSMinimumSystemVersion` 27.0. Every shipped Mach-O file we build has `minos 27.0`;
  prebuilt third-party files (the SDK's KosmicKrisp dylib has `minos 11.0`) have `minos` ≤ 27.0.
- Vulkan driver **KosmicKrisp only**; MoltenVK never used, never shipped (U-MAC2). dvv-gpu's
  Vulkan 1.4 + `forEachRequiredFeature` list are not weakened. Every macOS test that can open
  Vulkan runs with `VK_DRIVER_FILES` set to the prefix's KosmicKrisp manifest.
- Toolchain: `AppleClang` ≥ 21 with libc++ on Apple only; Linux stays Clang 19 + libstdc++ 14,
  Windows stays MSVC (C1). Third-party prefix: `libs/_third_party/appleclang`.
- Presets: `ninja-macos` (Debug), `ninja-macos-release`, `ninja-macos-asan`
  (`address;undefined`); no macOS TSan (C4).
- Every third-party dependency: version + URL + SHA256 + SPDX through `dvv_third_party()`;
  the licence gate and notices cover KosmicKrisp (MIT), Vulkan-Loader (Apache-2.0) and the
  SDK's `vulkaninfo` (Apache-2.0, a test tool, never shipped).
- FFmpeg: custom_ffmpeg **v1.0.1** `macos-arm64-shared-lgpl` on macOS (U-MAC10); loaded at run
  time, never linked; Windows/Linux may stay on v1.0.0.
- Warnings as errors (`-Wall -Wextra -Wpedantic -Werror`), `-ffile-prefix-map`, privacy lint,
  conventional commits with the GitHub handle and its noreply e-mail (bootstrap sets it per
  repository), no machine paths or personal data in any committed file.
- Windows and Linux builds and tests of every changed repository stay green on CI.
- Bundle id `io.github.barabashkad.rawreel` (C12). App layout: `Contents/MacOS/{rawreel-gui,
  rawreel-cli}`, `Contents/Frameworks/{libvulkan.1.dylib, <KosmicKrisp dylib>, ffmpeg/*.dylib}`,
  `Contents/Resources/vulkan/icd.d/<KosmicKrisp manifest>` with `library_path`
  `../../../Frameworks/<KosmicKrisp dylib>`; executables carry the single `LC_RPATH`
  `@executable_path/../Frameworks`.
- Not signed beyond ad-hoc (`codesign -s -`), not notarized (U-MAC9).
- **No tag before the local slice (Task 17) is green.** Pushes, tags and releases are public:
  the executor confirms each with the user first. Versions: new platform code → minor
  (0.1.x → 0.2.0), CI/dependency-only → patch; rawreel → 0.10.0 (C9).

## Review Focus

1. **Window moved between a Retina and a non-Retina display** (backing scale 2 ↔ 1): sharp and
   correctly sized, no stale swapchain. → Task 15 `physicalPixelSize()` unit test; Task 16
   `backing_scale` selftest drives the child view's `viewDidChangeBackingProperties`; Task 22 user
   check.
2. **App in a path with spaces and non-ASCII characters, read-only** (a downloaded `.dmg`, also
   what Gatekeeper's app translocation does): FFmpeg, the loader and KosmicKrisp are found and
   nothing is written into the bundle. → Task 20 smoke mounts the `.dmg` read-only at
   `"<tmp>/rawreel 2/Загрузки"` and runs from there.
3. **The Vulkan SDK (with MoltenVK) installed system-wide:** rawreel still runs on KosmicKrisp,
   and a `VK_DRIVER_FILES` the user set is respected. → Task 15 `bundledDriverEnvironment()` unit
   test; Task 20 smoke asserts the logged driver name on a Mac with the SDK installed.
4. **Started from Finder** (no terminal environment, cwd `/`): same behaviour; config and logs
   under `~/Library/Application Support/rawreel`. → Task 20 smoke runs once through
   `open -W -n --env … --stdout … --stderr … -a`.
5. **Display-scale change without a size change** (the window keeps its point size, so no
   `wxEVT_SIZE`): the presenter is resized anyway. → Task 16 `backing_scale` selftest.

---

## Phase 0: groundwork

### Task 0: KosmicKrisp spike (throwaway)

**Files:** none committed except the record in Step 4.

- [ ] **Step 1:** Install LunarG Vulkan SDK 1.4.363.0 (or the newest at execution time) for macOS
  on the development Mac. Record its download URL and SHA256.
- [ ] **Step 2:** Run `VK_DRIVER_FILES=<SDK KosmicKrisp manifest> vulkaninfo --json`. Expected:
  one device, `apiVersion` ≥ 1.4.0, a graphics + compute queue family, every feature of
  `libs/dvv-gpu/src/devices.h` `forEachRequiredFeature` `true`, `VK_KHR_swapchain`, and on the
  instance `VK_EXT_metal_surface`. Also what the presenter needs (`presenter_core.cpp:178`,
  `:204-207`): surface `supportedUsageFlags` include `TRANSFER_DST`, a BGRA8 surface format whose
  format has `BLIT_DST`; and storage-image use without a declared format for the formats
  `devices.h:32-36` names (`R8_UINT`, `R16_UINT`, …: `STORAGE_IMAGE` in their
  `optimalTilingFeatures`).
- [ ] **Step 3:** Read the manifest's `is_portability_driver`. `true` → dvv-gpu needs portability
  enumeration (Task 10 Step 3). Record `otool -L`/`otool -l` of the SDK's `vulkaninfo` (Task 3
  ships it into the prefix as a test tool and its loader reference must not be absolute).
- [ ] **Step 4:** **Stop and report to the user if anything in Step 2 is missing** (spec §10).
  Otherwise write the findings (SDK URL/SHA256, KosmicKrisp dylib and manifest names, the
  installer's command-line component, portability, the feature excerpt) into "Changes during
  execution" and commit `docs: record the KosmicKrisp spike`.

### Task 1: custom_ffmpeg v1.0.1 (repository `BarabashkaD/custom_ffmpeg`)

**Files** (clone next to rawreel; follow that repository's own `CLAUDE.md`):
- Modify: `build/build_macos.sh` / `build/lib/target_env.sh` (macOS shared link flags)
- Create: a macOS load check run by its build workflow (e.g. `build/check_macos_rpath.sh`)
- Modify: `.github/workflows/build.yml`, `README.md`, release notes

- [ ] **Step 1: Write the failing check.** For each `lib/*.<major>.dylib` of a macOS shared build:
  `otool -l` shows `LC_RPATH @loader_path`, and a 10-line C probe `dlopen`s `libavutil`,
  `libswresample`, `libavcodec`, `libavformat`, `libswscale`, `libavfilter`, `libavdevice` by
  full path from a copy of the folder in a temporary directory. On the v1.0.0 archive. Expected:
  FAIL, "no LC_RPATH's found".
- [ ] **Step 2:** Add `-Wl,-rpath,@loader_path` to the macOS **shared** link flags (FFmpeg's
  `--extra-ldflags` and the shared dependencies built for it). Static builds, other platforms and
  the 27.0 deployment target unchanged.
- [ ] **Step 3:** Build the macOS shared variants and run the check. Expected: PASS; `minos 27.0`;
  install names still `@rpath/…`.
- [ ] **Step 4:** Wire the check into the macOS job of `build.yml`, commit
  `fix: add an @loader_path rpath to the macOS shared libraries`; after the user confirms, release
  v1.0.1 through its release workflow. Record the asset URL and SHA256 for Task 3.

## Phase 1: the local slice (branches only, no tags)

Initialise rawreel's submodules (`git submodule update --init`) and, in each `libs/dvv-<x>` that
changes, `git switch -c feat/macos`. The dvv root is `libs/`, so every library builds against its
siblings' working trees; raise `project(VERSION)` to the planned version early (the root checks
versions, not tags). Run each library's tests with `cmake --workflow --preset ninja-macos` in its
folder. Commit on the branches; do not push yet.

### Task 2: dvv-build: toolchain check and macOS presets

**Files:** Modify `cmake/DvvCompilerOptions.cmake:10-28`, `cmake/DvvPresets.json`,
`cmake/DvvAppPresets.json`; Test `tests/cmake/test_presets_consistency.cmake`

**Interfaces:**
- Produces: presets `ninja-macos`, `ninja-macos-release`, `ninja-macos-asan` (configure, build,
  test, workflow) in both preset files: condition `hostSystemName == Darwin`, generator
  `Ninja Multi-Config`, `CMAKE_PREFIX_PATH=<root>/_third_party/appleclang`,
  `CMAKE_OSX_DEPLOYMENT_TARGET=27.0`, `CMAKE_OSX_ARCHITECTURES=arm64`,
  `CMAKE_BUILD_RPATH=<root>/_third_party/appleclang/lib`, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`;
  `ninja-macos-asan` adds `DVV_SANITIZE=address;undefined`.

- [ ] **Step 1: Failing test:** `test_presets_consistency.cmake` expects the three presets in both
  files with those cache variables.
- [ ] **Step 2:** Run `ctest -R presets_consistency`. Expected: FAIL.
- [ ] **Step 3:** Add the presets. In `DvvCompilerOptions.cmake`, on `APPLE` require
  `CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"`, version ≥ 21 (message: "AppleClang <v> is not
  supported; use AppleClang 21 or newer (Xcode 27)"), and `_LIBCPP_VERSION` via
  `check_cxx_source_compiles`; elsewhere unchanged. (No script-mode unit test: the check runs at
  configure time, so configuring is its test.)
- [ ] **Step 4:** Expected: the test passes; configuring a library with `ninja-macos` succeeds on
  the Mac; Linux/Windows configures are unchanged (CI, Task 17).
- [ ] **Step 5:** Commit `feat: macOS presets and the AppleClang toolchain check`.

### Task 3: dvv-build: the macOS third-party prefix

**Files:** Modify `cmake/DvvThirdParty.cmake` (`DVV_THIRD_PARTY_PLATFORMS` gains `Darwin`),
`deps/versions.cmake`, `deps/CMakeLists.txt:30-36`, `deps/write-notices.cmake`, `bootstrap.sh`;
Create `deps/post-install/kosmickrisp.cmake`; Modify `tools/check-vulkan-device.cmake`,
`cmake/DvvCompilerOptions.cmake:33` (`DVV_TEST_ENVIRONMENT`); Test
`tests/deps/test_versions.cmake`, `tests/spdx/test_third_party_gate.cmake`,
`tests/deps/test_superbuild_scripts.cmake`, `tests/ci/test_check_vulkan_device.cmake`

**Interfaces:**
- Consumes: Task 0 (SDK URL/SHA256, file names, component), Task 1 (v1.0.1 URL/SHA256).
- Produces: `bootstrap.sh --toolchain appleclang` (the default on Darwin) fills
  `<root>/_third_party/appleclang` with: nlohmann_json, doctest, Vulkan-Headers, VMA, vk-bootstrap;
  Slang `slang-2026.20-macos-aarch64` (`PREBUILT`, tool); wxWidgets `osx_cocoa`, static,
  **Release only** (as Linux; the Ninja superbuild cannot build Debug + Release,
  `DvvThirdParty.cmake:211-214`), builtin zlib/libpng, the Windows option list minus MSW-only ones;
  custom_ffmpeg v1.0.1 (`ONLY_ON Darwin`, `PREBUILT`, runtime, SHIPPED as today);
  Vulkan-Loader 1.4.365 from source → `lib/libvulkan.1.dylib` (Apache-2.0, runtime, SHIPPED);
  KosmicKrisp from the SDK → `lib/<dylib>` + `share/vulkan/icd.d/<manifest>` with
  `library_path` `../../../lib/<dylib>` (MIT, runtime, SHIPPED, `PREBUILT`); `bin/vulkaninfo`
  from the same SDK (Apache-2.0, tool, not shipped); the validation layer built for macOS in
  `share/vulkan/explicit_layer.d/`.
- Produces: on Apple, `DVV_TEST_ENVIRONMENT` gains
  `VK_DRIVER_FILES=set:<the manifest, found with find_file in CMAKE_PREFIX_PATH>`, so every
  library's tests that apply `DVV_TEST_ENVIRONMENT` (all of them, dvv-platform and dvv-media
  included) run on KosmicKrisp only. A missing manifest is a configure error on Apple.
- Produces: the superbuild's `common_args` pass `CMAKE_OSX_DEPLOYMENT_TARGET` and
  `CMAKE_OSX_ARCHITECTURES` on Apple (a plain build would target the SDK version).

- [ ] **Step 1: Failing tests:** `test_versions`: each `ONLY_ON Darwin` entry has a version, an
  https URL and a 64-hex SHA256; `test_third_party_gate`: `ONLY_ON Darwin` accepted, an unknown
  platform still rejected; `test_superbuild_scripts`: `common_args` contain the two `CMAKE_OSX_*`
  values on Apple, the KosmicKrisp post-install rewrites `library_path` as above;
  `test_check_vulkan_device`: the Darwin branch runs the prefix's `vulkaninfo` with
  `VK_DRIVER_FILES`.
- [ ] **Step 2:** Run them. Expected: FAIL.
- [ ] **Step 3:** Implement. `bootstrap.sh` Darwin branch: compiler `/usr/bin/clang++`,
  prerequisites checked first (`ninja`, `cmake`, `xcrun --show-sdk-version` ≥ 27; the message
  names `brew install ninja`). `kosmickrisp.cmake` runs the SDK installer headless into the build
  tree and copies only the KosmicKrisp files and `vulkaninfo`.
- [ ] **Step 4:** Tests pass; `libs/dvv-build/bootstrap.sh` from an empty prefix succeeds;
  `cmake -P tools/check-vulkan-device.cmake` finds one KosmicKrisp device with Vulkan ≥ 1.4;
  `otool -l` shows `minos 27.0` on `libvulkan.1.dylib`, the wx archives' objects and the FFmpeg
  dylibs (which also show `LC_RPATH @loader_path`); no `otool -L` entry names `/usr/local`,
  `/opt/homebrew` or the SDK.
- [ ] **Step 5:** Commit `feat: the macOS third-party prefix (Slang, wxOSX, FFmpeg, Vulkan)`.

### Task 4: dvv-build: tools and CI

**Files:** Modify `tools/sanity.sh` (uptime from `sysctl -n kern.boottime` on Darwin),
`.github/workflows/dvv-ci.yml`, `tests/privacy/test_privacy_lint.cmake`, `README.md`
("Prerequisites", "Building …" gain macOS), `CHANGELOG.md` (`[0.2.0]` entry, undated)

- [ ] **Step 1:** Add one privacy-lint test case: a made-up macOS home path (`/Users/` + a name) is flagged (the rule exists,
  `tools/privacy-lint.cmake:95`; only the case is missing). Run. Expected: PASS.
- [ ] **Step 2:** `dvv-ci.yml`: matrix entries `{ name: macos-appleclang, os: xcode-27,
  toolchain: appleclang, preset: ninja-macos }` and `macos-appleclang-asan`; steps "Bootstrap
  (macOS)" (`brew install ninja`, `bootstrap.sh`), "Check the Vulkan device (macOS)" (prints the
  device check; never fails the job, C10), "Build and test (macOS)" (`ctest` with `-LE display`
  unless the device check found a device and a desktop session). The cache key already includes
  the toolchain (`dvv-ci.yml:153`).
- [ ] **Step 3:** `tools/sanity.sh` on the Mac. Expected: PASS. Commit
  `feat: macOS sanity cycle and CI jobs`.

### Task 5: dvv-base: `dvv::MoveOnlyFunction`

**Files:** Create `include/dvv/base/move_only_function.h`; Modify
`include/dvv/base/executor.h`, `include/dvv/base/executors.h`, `src/executors.cpp`,
`tests/test_executors.cpp`, `tests/CMakeLists.txt`; Test `tests/test_move_only_function.cpp`

**Interfaces:**
- Produces: `template <class Signature> using MoveOnlyFunction = …;` in namespace `dvv`:
  `std::move_only_function<Signature>` when `__cpp_lib_move_only_function` is defined, else
  `dvv::detail::MoveOnlyFunction<Signature>`. The detail type supports the signature forms the
  code uses (`R(Args...)`, `R(Args...) noexcept`; no `const` form is used anywhere — re-check with
  `git grep` across the libs and rawreel), no small-buffer optimisation, move-only, empty-state
  `operator bool`, `= nullptr` empties, calling an empty one asserts in Debug. The detail type is
  compiled and tested on every platform.

- [ ] **Step 1: Failing tests,** each for both `dvv::detail::MoveOnlyFunction` and
  `dvv::MoveOnlyFunction`: holds a lambda capturing `std::unique_ptr<int>` and returns `*p`;
  move-construct and move-assign leave the source empty; the target is destroyed exactly once
  (counter); a move-only argument is forwarded without a copy; default-constructed is empty;
  `= nullptr` empties.
- [ ] **Step 2:** Run in `libs/dvv-base`. Expected: FAIL to compile.
- [ ] **Step 3:** Implement; replace `std::move_only_function` in the listed files.
- [ ] **Step 4:** Run. Expected: PASS. Commit `feat: dvv::MoveOnlyFunction`.

### Task 6: dvv-base: per-user paths on macOS

**Files:** Modify `src/paths.cpp:10-110`, `include/dvv/base/paths.h:1-20`; Test
`tests/test_paths.cpp`

- [ ] **Step 1: Failing tests (macOS block):** `userDataDir("rawreel")` ==
  `$HOME/Library/Application Support/rawreel`; `userCacheDir("rawreel")` ==
  `$HOME/Library/Caches/rawreel`; `XDG_DATA_HOME` ignored; unset `HOME` → `Errc::Io` "HOME is not
  set"; `executablePath()` is absolute, canonical, and names the test binary.
- [ ] **Step 2:** Run. Expected: FAIL ("executablePath is not implemented on this platform").
- [ ] **Step 3:** Implement with `_NSGetExecutablePath` + `std::filesystem::canonical` and an
  `#elif defined(__APPLE__)` branch for the directories.
- [ ] **Step 4:** Run. Expected: PASS. Commit `feat: per-user paths on macOS`.

### Task 7: dvv-platform: the Metal window alternative and its surface

**Files:** Modify `include/dvv/platform/native_window.h`, `src/native_window.cpp`,
`src/surface.cpp`, `src/surface_detail.h`, `CMakeLists.txt` (Apple sources, `-framework
QuartzCore`); Create `src/surface_metal.cpp`; Test the existing native-window and surface tests

**Interfaces:**
- Produces: `enum class WindowSystem : std::uint8_t { Win32, Xlib, Metal };`,
  `windowSystemName(WindowSystem::Metal) == "Metal"`, `defaultWindowSystem() == Metal` on Apple,
  `struct MetalWindow { void* layer = nullptr; };  // CAMetalLayer*`,
  `using NativeWindow = std::variant<std::monostate, Win32Window, XlibWindow, MetalWindow>;`,
  `detail::createMetalSurface(VkInstance, PFN_vkGetInstanceProcAddr, const MetalWindow&) noexcept
  -> std::expected<VkSurfaceKHR, VkResult>`; Metal instance extensions `VK_KHR_surface`,
  `VK_EXT_metal_surface`.

- [ ] **Step 1: Failing tests:** name and `windowSystemOf(MetalWindow{})` == `Metal`; the Metal
  instance extensions; `createSurface` with `MetalWindow{nullptr}` →
  `VK_ERROR_INITIALIZATION_FAILED`; an instance without `vkCreateMetalSurfaceEXT` →
  `VK_ERROR_EXTENSION_NOT_PRESENT` (the existing fake-instance pattern).
- [ ] **Step 2:** Run in `libs/dvv-platform`. Expected: FAIL to compile.
- [ ] **Step 3:** Implement; give every `switch`/`std::visit` the compiler reports a Metal case.
- [ ] **Step 4:** Run. Expected: PASS. Commit `feat: the Metal window alternative`.

### Task 8: dvv-platform: Cocoa test window and the macOS loader name in its tests

**Files:** Create `src/testing/test_window_cocoa.mm`; Modify `CMakeLists.txt`
(`enable_language(OBJCXX)` on Apple; `-framework AppKit -framework QuartzCore` for
`dvv::platform_testing`), `include/dvv/platform/testing/test_window.h` (comments name Cocoa),
`tests/surface_support.cpp:16`, `tests/surface_support.h:12`, `tests/package/main.cpp:16`

**Interfaces:**
- Consumes: `MetalWindow` (Task 7); `VK_DRIVER_FILES` from `DVV_TEST_ENVIRONMENT` (Task 3).
- Produces: `TestWindow::create()` on macOS opens an `NSWindow` whose content view is
  layer-backed by a `CAMetalLayer` with `contentsScale` = `backingScaleFactor`; `clientSize()` is
  in **physical pixels**; `native()` = `MetalWindow{layer}`; `resize()` sets the content size in
  points so the physical size equals the argument; `minimise/restore` miniaturize/deminiaturize;
  `pump()` drains pending events (`nextEventMatchingMask:… untilDate:[NSDate distantPast]`);
  `Errc::Unsupported "no display …"` without a window-server session. Main thread only.
  Test loader name on Apple: `@rpath/libvulkan.1.dylib`; package test library `libSystem.B.dylib`.

- [ ] **Step 1:** The display tests are the failing tests: `DVV_REQUIRE_GPU=1 ctest --preset
  ninja-macos -L display` in `libs/dvv-platform`. Expected: FAIL (no Cocoa test window; with
  `DVV_REQUIRE_GPU=1` a skip is a failure).
- [ ] **Step 2:** Implement the window and the test library names.
- [ ] **Step 3:** Run. Expected: PASS, including a resize to 333×177 reading back 333×177.
  Commit `feat: Cocoa test window`.

### Task 9: dvv-platform: file reading and library loading on macOS

**Files:** Modify `src/file_reader_posix.cpp:20-60`, `src/file_reader_detail.h`,
`include/dvv/platform/file_reader.h:10-15`, `include/dvv/platform/dynamic_library.h:15-30`;
Test `tests/test_file_reader.cpp` (beside the Windows flags test `:429-441`, and `:347`, `:778`),
`tests/test_dynamic_library.cpp`

**Interfaces:**
- Produces: `detail::nativeOpenFlags(mode)` (existing) without `O_DIRECT` on Apple, and a new
  pure `detail::nativeFcntlHints(mode) -> FcntlHints{bool noCache; bool readAhead;}` (`F_NOCACHE`
  and `F_RDAHEAD` cannot be read back, so the seam is what the test can see). `posix_fadvise`
  stays Linux-only.

- [ ] **Step 1: Failing tests:** a **Linux** block: `nativeOpenFlags(Unbuffered)` has `O_DIRECT`;
  an **Apple** block: no `O_DIRECT`, `nativeFcntlHints(Unbuffered).noCache`,
  `nativeFcntlHints(<sequential hint>).readAhead`. The existing unbuffered read test
  (`:337-350`) must pass on macOS.
- [ ] **Step 2:** Run. Expected: FAIL (`O_DIRECT` undeclared on Apple).
- [ ] **Step 3:** Implement; apply the hints with `fcntl` after `open`.
- [ ] **Step 4:** `ninja-macos` and `ninja-macos-asan` workflows in `libs/dvv-platform`.
  Expected: PASS. Commit `feat: file reading on macOS`.

### Task 10: dvv-gpu on macOS (loader, compute, presentation)

**Files:** Modify `include/dvv/gpu/presenter.h`, `include/dvv/gpu/submission.h`,
`src/presenter_core.h`, `src/presenter_core.cpp` (`dvv::MoveOnlyFunction`), `src/loader.cpp:30-45`,
`src/loader.h:18-25`, `tests/test_loader.cpp:32,47`, `src/instance.cpp` (only if Task 0 found
`is_portability_driver: true`), `src/presenter_logic.cpp` (only as Step 5 demands)

**Interfaces:**
- Produces: `loaderLibrary("")` on Apple == `{.name = "@rpath/libvulkan.1.dylib",
  .search = LibrarySearch::Default}` (resolved through the executable's `LC_RPATH`:
  `CMAKE_BUILD_RPATH` in a build tree, `@executable_path/../Frameworks` in the app).
- Produces (layer geometry rule, default; confirmed in Step 5): the GUI/test-window thread owns the
  `CAMetalLayer`'s frame and `contentsScale`; nobody in our code writes `drawableSize`; the
  driver's swapchain sets it from the swapchain extent; the presenter's extent is the caller's
  physical `PixelSize`.

- [ ] **Step 1: Failing test:** `test_loader.cpp` Apple case asserts the name above.
- [ ] **Step 2:** Run in `libs/dvv-gpu`. Expected: FAIL.
- [ ] **Step 3:** Implement the loader name, the migration, portability enumeration if needed.
- [ ] **Step 4:** `DVV_REQUIRE_GPU=1 DVV_VK_VALIDATION=sync ctest --preset ninja-macos -LE
  display`. Expected: PASS, zero validation messages; `dvv-gpu-info` lists KosmicKrisp with no
  missing requirement.
- [ ] **Step 5:** Same with `-L display`. For each failure (e.g. `OUT_OF_DATE` timing on resize,
  minimised 0×0 extent), extend `PresenterLogic`'s cases with a unit case per new behaviour (they
  run on every platform). Record in "Changes during execution" what KosmicKrisp does to
  `drawableSize` on swapchain creation, and change the geometry rule above only if it does not
  set it.
- [ ] **Step 6:** All dvv-gpu tests, Debug, Release, asan. Expected: PASS, zero validation
  messages. Commit `feat: dvv-gpu on macOS`.

### Task 11: dvv-media and dvv-registry on macOS

**Files:** dvv-media `tests/store_fakes.h`, `tests/store_fakes.cpp` (`MoveOnlyFunction`);
dvv-registry `cmake/DvvComponents.cmake` (only if Step 2 fails)

- [ ] **Step 1:** dvv-media: migrate; `DVV_REQUIRE_GPU=1` workflows `ninja-macos`,
  `ninja-macos-asan`. Expected: PASS, GPU unpack bit-exact for the whole catalogue on
  KosmicKrisp, zero validation messages.
- [ ] **Step 2:** dvv-registry workflows. Expected: PASS, including the link tests proving
  `$<LINK_LIBRARY:WHOLE_ARCHIVE,…>` keeps the registrations and the plain-link control does not.
- [ ] **Step 3:** Commit on each branch (`test: …` / `fix: …` as applicable).

### Task 12: dvv-components on macOS

**Files:** Modify `src/ffmpeg/checks.cpp:80-120`, `src/ffmpeg/loader_detail.h`,
`cmake/DvvComponent.cmake:85-100`, `cmake/check-no-ffmpeg-imports.cmake`; Test
`tests/test_ffmpeg_checks.cpp:190-200`, `tests/run-import-check-selftest.cmake`

**Interfaces:**
- Produces: `libraryFileName("avutil", 60) == "libavutil.60.dylib"` on Apple; `planSearch()`
  default directories on Apple: `exe_dir/ffmpeg`, `exe_dir/../lib/ffmpeg`,
  `exe_dir/../Frameworks/ffmpeg`; the import check uses `otool -L` (`OTOOL` argument; configure
  finds `otool` instead of requiring `readelf`, `DvvComponent.cmake:94`).

- [ ] **Step 1: Failing tests:** the names; the third Apple directory; the import-check self-test
  passes a clean saved `otool -L` listing (`/usr/lib/libc++.1.dylib`,
  `/usr/lib/libSystem.B.dylib`, `/System/Library/Frameworks/…`) and fails one naming
  `@rpath/libavcodec.62.dylib`.
- [ ] **Step 2:** Run in `libs/dvv-components`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** All tests (CTest sets `DVV_FFMPEG_DIR` to the prefix's FFmpeg). Expected: PASS,
  FFmpeg sources open. Commit `feat: dvv-components on macOS`.

### Task 13: rawreel: the console program and the core on macOS

**Files:** Modify (`MoveOnlyFunction`) `src/core/session.h`, `src/core/session_types.h`,
`src/core/timer.cpp`, `src/gui/player_panel.{h,cpp}`, `src/gui/selftest.{h,cpp}`,
`src/gui/transport_bar.{h,cpp}`, `src/render/display_state.{h,cpp}`,
`tests/session_fixture.{h,cpp}`; `CMakeLists.txt` (`project(VERSION 0.10.0)`),
`dvv-deps.txt` (the planned minimums)

- [ ] **Step 1:** Migrate; `libs/*` point at the `feat/macos` branch heads.
- [ ] **Step 2:** `DVV_REQUIRE_GPU=1 cmake --workflow --preset ninja-macos` is expected to stop at
  `rawreel_gui`; run `ctest --preset ninja-macos -LE gui`. Expected: PASS (`rawreel.unit`,
  `core_*`, `gpu`, `cli_unit`, `cli_e2e.*`, `packaging_scripts`).
- [ ] **Step 3:** `rawreel-cli bench` on a 4K raw file, buffered and unbuffered (the existing
  option), recorded in "Changes during execution" (the `F_NOCACHE` choice, spec §10).
- [ ] **Step 4:** Commit `build: rawreel core and rawreel-cli on macOS`.

### Task 14: rawreel: the FFmpeg folder and driver isolation at start-up

**Files:** Create `src/core/vulkan_driver.h`, `src/core/vulkan_driver.cpp`; Modify
`CMakeLists.txt` (`rawreel_core` sources), `src/gui/main.cpp` (`run()`), `src/cli/main.cpp`,
`src/core/startup.cpp` (`currentFfmpegOrigin`/`maskFolders` know `Frameworks/ffmpeg`); Test
`tests/test_vulkan_driver.cpp` (new, in `tests/CMakeLists.txt`), `tests/test_startup.cpp`

**Interfaces:**
- Produces: `rawreel::bundledDriverEnvironment(const std::filesystem::path& exe_dir,
  const std::function<std::optional<std::string>(std::string_view)>& get_env)
  -> std::optional<std::string>`: the manifest path for `VK_DRIVER_FILES`, or nullopt.
  `rawreel::applyBundledDriver()` (Apple: computes it from `dvv::paths::executablePath()` and
  `setenv(…, 0)`; elsewhere nothing) is the **first statement** of `run()` in `src/gui/main.cpp`
  and of `main()` in `src/cli/main.cpp` — before the pools, `startGpu()` (`app.cpp:137`) or the
  CLI's GPU paths (`commands.cpp:146,188`) can open the loader. The GPU stack's start logs
  `"Vulkan driver: <driverName> <driverInfo>"` from `GpuInfo` on all platforms.

- [ ] **Step 1: Failing tests:** manifest at `exe_dir/../Resources/vulkan/icd.d/<manifest>` and no
  variables → that path; `VK_DRIVER_FILES` set → nullopt; `VK_ICD_FILENAMES` set → nullopt; no
  manifest (build tree) → nullopt; `maskFolders` masks a `…/Contents/Frameworks/ffmpeg` origin like
  the existing package folder.
- [ ] **Step 2:** Run `rawreel.core_unit`. Expected: FAIL.
- [ ] **Step 3:** Implement, including the log line.
- [ ] **Step 4:** Run. Expected: PASS. Commit `feat: rawreel uses its bundled Vulkan driver on macOS`.

### Task 15: rawreel: the Cocoa viewport

**Files:** Create `src/gui/viewport_cocoa.mm`, `packaging/Info.plist.in`; Modify
`CMakeLists.txt` (`elseif(APPLE)`: `enable_language(OBJCXX)`, `viewport_cocoa.mm`,
`-framework AppKit -framework QuartzCore`, `MACOSX_BUNDLE`, `MACOSX_BUNDLE_INFO_PLIST`,
`INSTALL_RPATH @executable_path/../Frameworks`), `src/gui/native_window.h` (comments),
`src/gui/viewport.{h,cpp}` (`:39` guard, a resize entry used by both `onSize` and the scale
change), `src/core/view.{h,cpp}`; Test `tests/test_view.cpp`

**Interfaces:**
- Produces: `rawreel::physicalPixelSize(double width_points, double height_points, double scale)
  -> dvv::platform::PixelSize` (round to nearest, at least 1 for a non-empty view);
  `nativeWindowOf()` on wxOSX returns `MetalWindow{layer}` of an `NSView` subclass added as a
  child of the viewport's `GetHandle()` view, layer-backed by a `CAMetalLayer`;
  `syncNativeWindow()` updates the child's frame and `contentsScale` (Task 10's geometry rule);
  the subclass's `viewDidChangeBackingProperties` updates `contentsScale` and calls the viewport's
  resize entry (Review Focus 5) on the main thread.
- `Info.plist.in`: bundle id `io.github.barabashkad.rawreel`, `LSMinimumSystemVersion` 27.0,
  `NSHighResolutionCapable` true, version from `rawreel_VERSION_STRING`, `CFBundleDocumentTypes`
  for the extensions rawreel opens with role Viewer and `LSHandlerRank` Alternate (C5).

- [ ] **Step 1: Failing tests:** `physicalPixelSize(400, 300, 2.0) == {800, 600}`;
  `(400.5, 300, 2.0) == {801, 600}`; `(1, 1, 1.5) == {2, 2}`; `(0, 300, 2.0).empty()`.
- [ ] **Step 2:** Run `rawreel.core_unit`. Expected: FAIL.
- [ ] **Step 3:** Implement the function, the view, the plist and the CMake branch.
- [ ] **Step 4:** Unit tests pass; `rawreel-gui.app/Contents/MacOS/rawreel-gui
  'lavfi:testsrc2=s=1280x720:r=25,format=yuv420p'` (README variables) plays sharp on the built-in
  display, and resizing keeps it fitted. Commit `feat: rawreel-gui viewport on macOS`.

### Task 16: rawreel: Mac app behaviour and the GUI selftests

**Files:** Modify `src/gui/app.{h,cpp}` (`MacOpenFiles`), `src/gui/main_frame.cpp` (menu items on
`wxID_ABOUT`, `wxID_EXIT`, and `wxID_PREFERENCES` if a settings item exists; `:860` review),
`src/gui/selftest.cpp` (`:528`, `:555`, `:632`, `:1769` review; new steps),
`src/gui/main.cpp:22,43,67` review, `src/gui/crash_handler.cpp:260` (comment: macOS crash reports,
C7), `tests/CMakeLists.txt:173` (`gui_start.no_display` → `NOT WIN32 AND NOT APPLE`; the other GUI
selftests already register on every platform)

**Interfaces:**
- Consumes: the drop route in `src/core/drop.h`; the viewport resize entry and the child view
  (Task 15).

- [ ] **Step 1: Failing selftest steps:** `open_files` calls `wxGetApp().MacOpenFiles({<y4m
  fixture>})` and expects the session state of the existing drop step; `backing_scale` forces the
  child view's layer scale through a test hook in `viewport_cocoa.mm`, then invokes
  `viewDidChangeBackingProperties` (the real Cocoa entry, not `syncNativeWindow()` directly) and
  checks the presenter extent equals `physicalPixelSize()` of the unchanged point size.
- [ ] **Step 2:** `ctest --preset ninja-macos -L gui`. Expected: FAIL.
- [ ] **Step 3:** Implement `MacOpenFiles` through the drop route and the menu ids; give each
  reviewed `__WXGTK__`/`_WIN32` branch its macOS behaviour.
- [ ] **Step 4:** Commit `feat: rawreel-gui behaves as a Mac app`.

### Task 17: local slice checkpoint, then the library releases

**Files:** each library's `CMakeLists.txt` (version), `CHANGELOG.md` (dated entries),
`dvv-deps.txt` and `.github/workflows/ci.yml` (`@v0.2.0`); rawreel `libs/*`, `dvv-deps.txt`,
`.github/workflows/ci.yml`

- [ ] **Step 1: Slice gate.** In rawreel, with `libs/*` on the `feat/macos` heads:
  `DVV_REQUIRE_GPU=1 DVV_VK_VALIDATION=sync cmake --workflow --preset ninja-macos` and the
  `ninja-macos-asan` workflow. Expected: **every** test passes, display and GUI included, zero
  validation messages. Also each changed library's own workflows. Fix on the branches until green;
  nothing is tagged before this.
- [ ] **Step 2:** With the user's go-ahead, release in dependency order, each: push the branch,
  CI green on Windows, Linux and macOS, merge, tag, push the tag. Order and versions:
  dvv-build 0.2.0 → dvv-base 0.2.0 → dvv-registry 0.1.1 → dvv-platform 0.2.0 → dvv-gpu 0.2.0 →
  dvv-media 0.1.1 → dvv-components 0.2.0. Before each library's CI, its `dvv-deps.txt` minimums
  and `ci.yml` `@v<dvv-build>` name the versions already tagged. Record in "Changes during
  execution" what the `xcode-27` device check printed (C10).
- [ ] **Step 3:** rawreel: pin the tags (`git -C libs/dvv-<x> checkout v<ver>`), raise
  `dvv-deps.txt`, re-run Step 1's workflow. Expected: PASS. Commit
  `build: pin the dvv libraries with macOS support`.

### Task 18: stage A documentation and checkpoint

**Files:** `README.md` ("Building and testing (macOS)" after the Linux section, "Current limits",
"Bug reports" → `~/Library/Logs/DiagnosticReports`)

- [ ] **Step 1:** Write the section in the Linux section's form: `bootstrap.sh`,
  `cmake --workflow --preset ninja-macos`, the `export` lines (`DVV_FFMPEG_DIR`,
  `VK_DRIVER_FILES`, `VK_ADD_LAYER_PATH`, `DVV_VK_VALIDATION`) with relative paths only.
- [ ] **Step 2:** From a fresh clone, follow it literally. Expected: spec §8 items 1–4 hold;
  record the 4K raw playback rate on the built-in display (`perf` overlay).
- [ ] **Step 3:** Push (user confirms); rawreel CI green on all three platforms. Commit
  `docs: building and testing on macOS`.

## Phase 2: rawreel stage B

### Task 19: the app bundle, its symbols and the `.dmg`

**Files:** Modify `packaging/symbols.cmake` (Apple: `dsymutil` → `<exe>.dSYM`, then `strip -S`),
`packaging/packaging.cmake:48-60,258-281`, `packaging/make-packages.cmake:70`,
`packaging/write-manifest.cmake`; Create `packaging/manifest-macos.txt`; Test
`tests/packaging/test_packaging_scripts.cmake`

**Interfaces:**
- Produces: `rawreel-<ver>-macos-arm64.dmg` holding `rawreel.app` (Global Constraints layout; the
  KosmicKrisp manifest rewritten to `../../../Frameworks/<dylib>`) plus `README.md`, `LICENSE`,
  `THIRD-PARTY-NOTICES`, `SOURCE.txt`; `rawreel-<ver>-macos-arm64-symbols.zip` with both `.dSYM`.
  Every Mach-O file is ad-hoc signed after its last modification, inside-out, then the bundle
  (no `--deep`). `.dmg` via `hdiutil create -fs APFS -format UDZO`.

- [ ] **Step 1: Failing test:** `manifest-macos.txt` matches the layout; `write-manifest` lists the
  six FFmpeg major names, `libvulkan.1.dylib`, the KosmicKrisp dylib and its manifest.
- [ ] **Step 2:** `ctest -R packaging_scripts`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** `cmake -DBUNDLE=ON -P packaging/dry-run.cmake` builds both files. Expected: the
  script tests pass. Commit `feat: the macOS app package`.

### Task 20: package checks and the package smoke on macOS

**Files:** Modify `packaging/check-package.cmake`, `packaging/package-smoke.cmake`
(`PLATFORM macos`), `packaging/dry-run.cmake:21` (Darwin host),
`packaging/scan-artifact-privacy.cmake` (a `/Users/<name>` rule; the CI runner home is allowed, as on Linux); Create
`tests/packaging/data/otool-L-exe.txt`, `otool-L-library.txt`, `otool-l-load-commands.txt`,
`dyld-print-libraries.txt`

**Interfaces:**
- Produces (pure functions on saved text, like the `readelf` ones):
  `rawreel_otool_dependencies(TEXT <otool -L> DEPS <var>)`,
  `rawreel_otool_load_commands(TEXT <otool -l> RPATHS <var> MINOS <var>)`,
  `rawreel_macho_errors(NAME <file> KIND exe|library DEPS <…> RPATHS <…> MINOS <…> ERRORS <var>)`
  (all: `MINOS` ≤ 27.0, and exactly 27.0 for files built by rawreel or the superbuild from source;
  exe: rpaths exactly `@executable_path/../Frameworks`, no FFmpeg
  dependency; library: every dependency `@rpath/`, `@loader_path/`, `/usr/lib/` or `/System/`),
  `rawreel_loaded_image_errors(TEXT <DYLD_PRINT_LIBRARIES output> BUNDLE <path> ERRORS <var>)`
  (every image inside the bundle, `/usr/lib/` or `/System/`; U-MAC8).

- [ ] **Step 1: Failing tests** over the saved listings: a clean exe and library pass; an exe with
  `/opt/homebrew/lib` in its rpaths, a library depending on `/usr/local/lib/libvulkan.1.dylib`, a
  file with `minos 28.0`, and a loaded-image list containing `/usr/local/lib/libMoltenVK.dylib`
  each fail, naming the file and the path.
- [ ] **Step 2:** `ctest -R packaging_scripts`. Expected: FAIL.
- [ ] **Step 3:** Implement the checks and the smoke: mount the `.dmg` read-only at
  `"<tmp>/rawreel 2/Загрузки"` (`hdiutil attach -readonly -mountpoint`) and run from there
  (Review Focus 2); `rawreel-cli info` and `rawreel-gui --selftest smoke` with
  `DYLD_PRINT_LIBRARIES=1` and `VK_LOADER_DEBUG=driver`, audited; the log names KosmicKrisp with
  the Vulkan SDK installed (Focus 3); one run through `open -W -n --env DYLD_PRINT_LIBRARIES=1
  --stdout <file> --stderr <file> -a <app> --args --selftest smoke`, checked from the files
  (Focus 4).
- [ ] **Step 4:** `cmake -DBUNDLE=ON -P packaging/dry-run.cmake`. Expected: every check and smoke
  step passes. Commit `feat: macOS package checks and smoke test`.

### Task 21: release workflow, release notes, final review

**Files:** `.github/workflows/release.yml` (`RELEASE_REF` for a dry run becomes
`github.ref_name` instead of `main` (`release.yml:31`) — unchanged when started from `main`; job
`macos` on `xcode-27` like `windows`/`linux`: build, unit and CLI tests, package, smoke without
the GPU steps if no device, attest, upload), `README.md` ("Packages", "Verifying downloads",
first-launch approval for an unsigned app), `docs/release-notes-0.10.0.md`, the spec's status

- [ ] **Step 1:** Implement; run `release.yml` as a dry run from the branch (user confirms).
  Expected: the `.dmg` and symbols zip as workflow artifacts, every job green.
- [ ] **Step 2:** Whole-branch review (superpowers:requesting-code-review) across rawreel and the
  library changes; fix findings (a library fix after its tag is a patch release).
- [ ] **Step 3:** Commit `docs: rawreel 0.10.0 release notes and macOS packages`.

### Task 22: the user's check (manual)

- [ ] The user installs the dry-run `.dmg`, approves the first launch, opens a raw file from
  Finder with rawreel not running ("Open With", cold start) and again while running, drags a file
  onto the window, moves the window between the built-in display and a non-Retina external one if
  available (Review Focus 1), and checks About and ⌘Q. Findings go back to the owning task.

## Plan review changes (2026-11-07)

- Tags moved behind a local end-to-end slice (Task 17), one release step in dependency order.
- Spike: presenter needs (`TRANSFER_DST`, BGRA8 `BLIT_DST`, storage formats), portability from
  `is_portability_driver`, `vulkaninfo`'s linkage; FIFO not checked (always supported).
- Superbuild passes `CMAKE_OSX_DEPLOYMENT_TARGET`/`ARCHITECTURES`; package check on `minos`.
- `VK_DRIVER_FILES` once in dvv-build's `DVV_TEST_ENVIRONMENT` for every library's tests.
- Driver override is the first statement of both `main()`s, not in `startup.cpp`.
- Scale change wired through `viewDidChangeBackingProperties`; the selftest drives that entry.
- Layer geometry: one writer rule, confirmed against KosmicKrisp in Task 10.
- Simplified: wx Release only; no compiler-check script test; no `const` form or small buffer in
  `MoveOnlyFunction`; `nativeOpenFlags` extended plus a small `nativeFcntlHints` seam (with a
  Linux `O_DIRECT` case); privacy-lint and CI-cache work already present; no `tsan.supp` change;
  GUI selftests already registered; one smoke run covers path and read-only cases.
- Fixed: the app manifest's `library_path` points to `Frameworks`; `release.yml` dry runs build the
  dispatched ref; `open` smoke uses `--env/--stdout/--stderr`; artifact privacy scan gains
  `/Users/<name>`; Task 13 runs `-LE gui` before the viewport exists.

## Follow-ups after rawreel 0.10.0

- **lavapipe on macOS CI (user decision, 2026-11-07).** GitHub's `xcode-27` runners expose no
  Metal device KosmicKrisp can use (the dvv-build 0.2.0 CI device check: "Failed to detect any
  valid GPUs"), so the macOS GPU tests skip in CI. Mesa's lavapipe (the software Vulkan driver the
  Linux CI uses) builds on macOS. Plan: a spike on the development Mac (a lavapipe build of the
  Mesa version Linux CI uses; `dvv-gpu-info` must show Vulkan 1.4 and no missing requirement;
  whether lavapipe offers `VK_EXT_metal_surface` for the display tests); then a CI-only "macOS
  software GPU" job that runs the compute and unpack tests with `VK_DRIVER_FILES` pointing at
  lavapipe — a deliberate, documented exception to U-MAC2's KosmicKrisp-only rule for CI (never
  shipped), with the development Mac staying the authority on the real GPU. Open points: LLVM
  pinning (lavapipe links LLVM; unpinned Homebrew LLVM vs a pinned prebuilt), its licence entries,
  and caching the build in CI.
- **CI speed-up (user decisions, 2026-11-07).** Measured on the dvv-build 0.2.0 and dvv-base 0.2.0
  PR runs: a cold job spends 10–24 min of its time bootstrapping the third-party prefix (Windows:
  wxWidgets 4m52s Debug+Release, SPIRV-Tools 3m42s, Vulkan-ValidationLayers 14m05s, run serially
  by the Visual Studio generator; Linux ~16 min, macOS ~10 min with Ninja overlapping them) and
  1–2 min building and testing. The cache cannot help across the 8 repositories or PR scopes
  (GitHub caches are per repository and per ref scope), and the three Windows matrix jobs each
  build the same prefix on a miss. To do, as a dvv-build 0.3.0:
  1. **Validation layer from the pinned LunarG SDK prebuilt (user decision A)** on Windows, Linux
     and macOS — extract only the layer and its manifest (as for KosmicKrisp); drop the
     SPIRV-Headers, SPIRV-Tools and Vulkan-Utility-Libraries entries, whose only consumer is the
     layer (research: `.superpowers/sdd/…/research-spirv-tools-cache.md`). The layer version
     then follows the SDK (≥ 1.4, as dvv-gpu requires).
  2. **wxWidgets in its own CI-only cache (user decision, no published prebuilt)**: wx installs into
     its own part of the prefix (or keeps its own stamp) so bootstrap can restore it separately;
     its actions/cache key hashes only wx's own inputs (its versions.cmake entry, toolchain,
     compiler version, the superbuild code that builds it), so dvv-build changes that don't touch
     wx keep it; a push to main warms main's scope for later PRs; GitHub evicts it after 7 idle
     days (rebuilt once, ~5 min). No cross-repo sharing: each repo builds it once per wx change.
     (A published prebuilt was considered and rejected by the user: `…/research-wx-cache.md`.)
  3. **A prime bootstrap job** per OS/toolchain that the matrix jobs depend on, so the three
     Windows jobs no longer build the same prefix in parallel.
  4. Optional: the Windows superbuild with Ninja under the MSVC environment, so remaining
     from-source dependencies build in parallel.

## Changes during execution

(Filled in while executing: the Task 0 spike record, the `drawableSize` behaviour, the `xcode-27`
device check output, the `bench` comparison, the playback data point, any change to this plan.)

### Task 0: KosmicKrisp spike

Result: nothing required is missing. The approach works on KosmicKrisp alone (MoltenVK never used).

- SDK: LunarG Vulkan SDK 1.4.363.1 (newest at execution time; `https://vulkan.lunarg.com/sdk/latest/mac.json`
  says `1.4.363.1`, not 1.4.363.0).
  URL `https://sdk.lunarg.com/sdk/download/1.4.363.1/mac/vulkan_sdk.zip`, 397584552 bytes, SHA256
  `a65c707cf0a32bf8c2ca6b9af1985b7af61dbadfc19b352dae51a323f3a547a6` (equals the checksum at
  `https://sdk.lunarg.com/sdk/sha/1.4.363.1/mac/vulkan_sdk.zip.txt`, which names it
  `vulkansdk-macos-1.4.363.1.zip`). The zip holds `vulkansdk-macOS-1.4.363.1.app`.
- Installer (user level, no sudo, no `/usr/local`): inside the unzipped folder run
  `vulkansdk-macOS-1.4.363.1.app/Contents/MacOS/vulkansdk-macOS-1.4.363.1 --root <HOME>/VulkanSDK/1.4.363.1 --accept-licenses --default-answer --confirm-command install com.lunarg.vulkan.core`.
  `... --root <any> search .` lists the components: `com.lunarg.vulkan.core` (always installed;
  holds KosmicKrisp, MoltenVK, loader, `vulkaninfo`, headers, glslang, shaderc), `.usr` (system
  global, NOT installed), `.volk`, `.sdl2`, `.glm`, `.vma`, `.ios`, and the umbrella
  `com.lunarg.vulkan`. Only `core` is needed. Install takes about 25 s after the unzip.
- Files (relative to `<SDK>` = `<HOME>/VulkanSDK/1.4.363.1/macOS`): manifest
  `<SDK>/share/vulkan/icd.d/libkosmickrisp_icd.json` (`api_version` 1.4.0, `library_path`
  `../../../lib/libvulkan_kosmickrisp.dylib`; next to it `MoltenVK_icd.json`); driver
  `<SDK>/lib/libvulkan_kosmickrisp.dylib` (also `KosmicKrisp.framework`); loader
  `<SDK>/lib/libvulkan.1.4.363.dylib` with symlinks `libvulkan.1.dylib`, `libvulkan.dylib`;
  `<SDK>/bin/vulkaninfo` (universal x86_64 + arm64).
- Portability: the manifest has no `is_portability_driver` key, so it is not a portability driver;
  the device shows no `VK_KHR_portability_subset`. dvv-gpu needs no portability enumeration for
  KosmicKrisp (Task 10 Step 3 is not needed on that ground). The loader still advertises
  `VK_KHR_portability_enumeration`.
- vulkaninfo linkage: `otool -L` (arm64) lists only AppKit, QuartzCore, libc++, libSystem, libobjc;
  there is no `LC_RPATH` and no libvulkan entry. The loader is found by `dlopen("libvulkan.dylib")`
  / `libvulkan.1.dylib` and `/usr/local/lib/...` by name, so without the system-global component
  it fails ("Vulkan loader is not installed, not found, or failed to load") unless
  `DYLD_LIBRARY_PATH=<SDK>/lib` is set. Task 3 must set that (or add an rpath/`install_name_tool`
  step) when it runs the shipped `vulkaninfo`; the "loader reference is not absolute" holds, but it
  is a bare name that needs the environment.
- Run: `DYLD_LIBRARY_PATH=<SDK>/lib VK_DRIVER_FILES=<SDK>/share/vulkan/icd.d/libkosmickrisp_icd.json <SDK>/bin/vulkaninfo`
  (not the SDK's `setup-env.sh`). `--json` writes `VP_VULKANINFO_Apple_M4_26_2_99.json` in the
  current directory (a profiles file, no surface data), nothing on stdout; the text output has the
  surface data. One device: Apple M4, `DRIVER_ID_MESA_KOSMICKRISP`, driver 26.2.99
  (`vulkan-sdk-1.4.363 (git-269a955c89)`), conformance version 1.4.3 (patch 2), `apiVersion` 1.4.363, integrated GPU.
- Queue: one family, `GRAPHICS | COMPUTE | TRANSFER`, count 1, present support true.
- Extensions: instance `VK_EXT_metal_surface` yes; device `VK_KHR_swapchain` (revision 70) yes.
- Required features of `forEachRequiredFeature` (all 19 `true`): `shaderInt64`,
  `shaderStorageImageExtendedFormats`, `shaderStorageImageWriteWithoutFormat`,
  `shaderStorageImageReadWithoutFormat`, `storageBuffer16BitAccess`,
  `uniformAndStorageBuffer16BitAccess`, `timelineSemaphore`, `bufferDeviceAddress`,
  `descriptorIndexing`, `runtimeDescriptorArray`, `descriptorBindingPartiallyBound`,
  `descriptorBindingVariableDescriptorCount`, `shaderSampledImageArrayNonUniformIndexing`,
  `descriptorBindingSampledImageUpdateAfterBind`, `synchronization2`, `dynamicRendering`,
  `maintenance5`, `maintenance6`, `pushDescriptor`.
- Surface (a real `VK_EXT_metal_surface` surface that vulkaninfo made in the GUI session):
  `supportedUsageFlags` = TRANSFER_SRC, TRANSFER_DST, SAMPLED, STORAGE, COLOR_ATTACHMENT,
  INPUT_ATTACHMENT, ATTACHMENT_FEEDBACK_LOOP_EXT; present modes IMMEDIATE and FIFO; min/max image
  count 3/3; formats (60 pairs over 12 colour spaces): `B8G8R8A8_SRGB`, `B8G8R8A8_UNORM`,
  `R16G16B16A16_SFLOAT`, `A2B10G10R10_UNORM_PACK32`, `A2R10G10B10_UNORM_PACK32`.
  `optimalTilingFeatures` of `B8G8R8A8_UNORM` and `B8G8R8A8_SRGB` include `BLIT_DST`.
- Storage formats: `STORAGE_IMAGE` is in `optimalTilingFeatures` of `R8_UINT`, `R16_UINT` (and
  `R8_UNORM`, `R8_SINT`, `R8G8_UNORM`, `R8G8_UINT`, `R16_UNORM`, `R16_SINT`, `R16G16_UINT`,
  `R8G8B8A8_UNORM`, `R8G8B8A8_UINT`, `R16G16B16A16_UINT`). Format features carry no
  without-format bit; that is the device feature above.
- Not checked here: `VK_PRESENT_MODE_FIFO_KHR` is listed; actual swapchain creation and
  `drawableSize` behaviour are left to Task 10.

### Task 10: dvv-gpu on KosmicKrisp

- Portability enumeration is not needed: `dvv-gpu-info` (dvv-gpu's own instance, no portability
  flag) lists the one device, Apple M4, Vulkan 1.4.363, driver KosmicKrisp
  `vulkan-sdk-1.4.363 (git-269a955c89)`, `usable` (no missing requirement); `src/instance.cpp` is
  unchanged.
- `drawableSize` (measured on a 2x display with the Cocoa TestWindow, layer bounds 166.5 x 88.5 pt,
  `contentsScale` 2): before any swapchain it is 0x0 (nothing writes it). KosmicKrisp reports
  `currentExtent` = bounds x `contentsScale` (333x177; also 97x61 for 48.5 x 30.5 pt), never
  `0xFFFFFFFF`, and `vkCreateSwapchainKHR` sets `drawableSize` to the swapchain extent at once
  (read right after the call, before any acquire or present). A layer resized without a new
  swapchain keeps the old `drawableSize`. The geometry rule of Task 10 stands unchanged; the
  dvv-gpu display test `present_test_metal.cpp` asserts it for fractional point bounds.
- Surface image counts are 3..3; the presenter's request (one more than the minimum, clamped to
  the maximum) is 3.
- A miniaturized window keeps its `currentExtent` (not 0x0): on macOS minimising is not the
  presenter's 0-extent path; the GUI's `setVisible(false)` hides it as on Linux.
- No presenter behaviour changed: every display test passed on KosmicKrisp as written, so
  `PresenterLogic` gained no case.

### Task 13: rawreel core and rawreel-cli on macOS

- `std::move_only_function` became `dvv::MoveOnlyFunction` in the core, render, gui and test
  sources; `project(VERSION)` is 0.10.0. `dvv-deps.txt` is unchanged: the libraries still report
  `project(VERSION 0.1.x)`, so raising the minimums now would fail the dvv version check; Task 17
  raises them with the library releases.
- `ctest --preset ninja-macos -LE gui` (Debug and Release, `DVV_REQUIRE_GPU=1`,
  `DVV_VK_VALIDATION=sync`): 20 of 21 pass. `rawreel.packaging_scripts` fails at its first
  `dry-run.cmake` case ("Windows or Linux only"): the Darwin host support of `dry-run.cmake` is
  Task 20, so the test passes only from there.
- `bench` on a 3840x2160 v210 file (100 frames, 22118400 bytes each, 2.2 GB, written just before,
  so partly in the page cache), Release, 90 frames, inflight 4, Apple M4 on KosmicKrisp; each
  mode twice (the first run of each pair was the slower one):

  | `io.fileReads` | end-to-end | read-only wall clock |
  |---|---|---|
  | buffered | 65.2 and 140.1 frames/s (1.44 and 3.10 GB/s) | 186 and 444 frames/s (4.1 and 9.8 GB/s) |
  | unbuffered (`F_NOCACHE`) | 134.0 and 159.7 frames/s (2.96 and 3.53 GB/s) | 301 and 403 frames/s (6.7 and 8.9 GB/s) |

  Unbuffered is not slower than buffered on this machine and file: the SSD feeds 4K v210 well above
  25 fps either way, and the run to run spread (first run after a file write) is larger than the
  difference of the second runs. The `F_NOCACHE` choice stands; a cold-cache comparison needs
  `purge` (root) and was not run.

### Task 21: FFmpeg sources

- User decision: each platform may pin its own custom_ffmpeg release (Windows and Linux 1.0.0,
  macOS 1.0.1), and rawreel builds and publishes no third-party sources archive any more. Each
  package's `SOURCE.txt` points to the custom_ffmpeg tag of its release
  (`https://github.com/BarabashkaD/custom_ffmpeg/tree/v<version>`, with its submodules) as the
  corresponding source; `release.yml` checks that each package's `SOURCE.txt` names the release of
  its own prefix.
