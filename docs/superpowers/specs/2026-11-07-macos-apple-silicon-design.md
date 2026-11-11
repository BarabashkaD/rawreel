# macOS on Apple Silicon: design

Status: implemented on the branch `macos-apple-silicon` (rawreel 0.10.0, from its implementation
plan); done pending the `release.yml` dry run and the user's check of its `.dmg` (plan Tasks 21
and 22). Not released. Date 2026-11-07 (written as a draft, builder check done and the user's
answers applied, before the plan). Inputs: rawreel `main` at 4b6215e (0.9.0 plus the
dvv-build 0.1.1 / dvv-components 0.1.2 bump), the dvv libraries at their pins (dvv-build v0.1.1,
dvv-base v0.1.0, dvv-registry v0.1.0, dvv-platform v0.1.0, dvv-gpu v0.1.0, dvv-media v0.1.0,
dvv-components v0.1.2), the rawreel spec (`2026-10-19-rawreel-design.md`, which lists macOS with
`CAMetalLayer` under v1.x) and the M1b design (F8: the macOS `NativeWindow` alternative).

## 1. Intent

rawreel has only ever been built and run on Windows and Linux. From the start it was meant to
reach macOS too (spec §1.1, §13.5, M1b F8), and that time is now. The goal is a **real** macOS
port for Apple Silicon laptops: the same programs, tests and quality bar as on Linux, not a build
that merely compiles.

It serves two audiences, in this order:

- **Stage A, the developer on their own Mac:** rawreel and the dvv libraries build from source on
  an Apple Silicon Mac with one CMake workflow preset, and the whole test suite passes there, GPU
  and GUI tests included, on the real Apple GPU.
- **Stage B, people who download rawreel:** a downloadable rawreel app that opens and plays on a
  stock Apple Silicon Mac with nothing else installed (no Homebrew, no Vulkan SDK, no FFmpeg).

Stage B builds on stage A; stage A is useful on its own and is finished first.

## 2. Decisions made by the user

- **U-MAC1 Platforms:** Apple Silicon (arm64) Macs on **macOS 27 or later** only. Intel Macs and
  older macOS versions are out of scope (no universal binaries, no x86_64 slice). The user first
  chose macOS 26+, then raised the floor to 27 because custom_ffmpeg v1.0.0's macOS libraries are
  built for macOS 27.0 (`LC_BUILD_VERSION minos 27.0`). Asked again once CI was on the table, the
  user kept 27+: every Apple Silicon Mac that runs 26 can run 27, and the floor matches what the
  development Mac and the CI runners can test.
- **U-MAC2 Vulkan driver:** **KosmicKrisp** (LunarG's Mesa-based Vulkan-on-Metal driver, Vulkan
  1.4 conformant since Vulkan SDK 1.4.363.0, built on Metal 4, which needs macOS 26+). **No MoltenVK**,
  not even as a fallback. dvv-gpu keeps its hard requirement of Vulkan 1.4 plus the required
  feature list (`dvv-gpu/src/devices.h`, `forEachRequiredFeature`); it is not weakened for macOS.
- **U-MAC3 Both stages:** first building from source (stage A), then a downloadable app (stage B).
- **U-MAC4 Several repositories:** the work lands in each dvv library that needs it, each with its
  own version and release, then rawreel moves its pins (as in the dvv-build 0.1.1 /
  dvv-components 0.1.2 bump).
- **U-MAC5 A native Mac GUI:** rawreel-gui uses wxWidgets' Cocoa port (wxOSX) and behaves like a
  normal Mac app (application menu, ⌘ shortcuts, Finder integration), not a copy of the GTK layout.
- **U-MAC6 CI: approved.** macOS jobs are added to the shared dvv CI workflow (every dvv
  repository and rawreel) and a macOS job to rawreel's `release.yml`, on GitHub's `xcode-27`
  runners (Apple Silicon, macOS 27, Xcode 27; free for public repositories). They build and run the
  tests; GPU and display tests there are expected to skip (C10), so the GPU and GUI proof stays on
  the development Mac.
- **U-MAC7 KosmicKrisp comes from the LunarG macOS Vulkan SDK:** the superbuild takes the
  KosmicKrisp ICD (the driver library and its manifest) from a pinned LunarG macOS Vulkan SDK
  release (1.4.363.0 or newer: version, URL, SHA256, SPDX MIT), the exact build that passed
  conformance. It is not built from Mesa source (that needs unpinned Homebrew LLVM, libclc and the
  SPIR-V translator, or an hours-long LLVM build).
- **U-MAC8 Clean-machine proof on the development Mac:** the stage B smoke test runs the packaged
  app on the development Mac and fails if any loaded library or Vulkan driver comes from outside
  the app bundle or the system (`/usr/lib`, `/System`), using macOS's own loaded-image tracing.
  No second Mac or VM is required.
- **U-MAC9 Not signed beyond the ad-hoc signature, not notarized**, like the unsigned Windows zip.
  Users allow the app in System Settings → Privacy & Security on first launch; the README says
  how. Room is left to add Developer ID signing later without restructuring the packaging.
- **U-MAC10 A new custom_ffmpeg release, v1.0.1:** its macOS shared libraries carry an
  `LC_RPATH` of `@loader_path`, so they find each other from whatever folder they are loaded
  from (the counterpart of Linux's `RUNPATH $ORIGIN`). Same macOS 27.0 target. The fix is made
  where FFmpeg is built, not by patching binaries in the superbuild, so the shipped files are the
  released ones. dvv-build pins v1.0.1 for macOS; Windows and Linux may stay on v1.0.0.

## 3. Goals, non-goals, anti-goals

**Goals**
1. Every dvv library and rawreel configure, build and test on Apple Silicon macOS 27+ with a
   macOS preset, Debug and Release, warnings as errors, like `ninja-clang` on Linux.
2. `rawreel-cli` (`info`, `dump`, `dump --via gpu`, `bench`, `--version`) and `rawreel-gui`
   (playback, drag-and-drop, docks, format dialog, selftests) work on macOS with the same
   behaviour as on Linux.
3. The GPU path runs on the Apple GPU through KosmicKrisp, with zero validation messages under
   `DVV_VK_VALIDATION=sync` (and GPU-assisted validation available, which KosmicKrisp supports).
4. The viewport is crisp on Retina displays: the swapchain is sized in physical pixels and follows
   the window's backing scale factor, including when the window moves between displays.
5. FFmpeg sources work on macOS through the existing run-time binding (no linking), using
   custom_ffmpeg's `macos-arm64-shared-lgpl` release.
6. A downloadable package (stage B) holding `rawreel.app` with FFmpeg, the Vulkan loader and the
   KosmicKrisp driver inside, checked by the same kind of package checks and smoke tests the
   Windows and Linux packages have.

**Non-goals**
- Intel Macs, macOS before 27, universal binaries, iOS/iPadOS.
- Developer ID signing and notarization (U-MAC9).
- MoltenVK, or a native Metal backend in dvv-gpu.
- The Mac App Store (sandboxing, App Store review).
- New player features. The port brings the existing 0.9.0 feature set to macOS; nothing more.
- Hardware video decoding (VideoToolbox). It isn't on the other platforms either (rawreel opens
  only lossless codecs).
- A rawreel crash reporter on macOS (see C7).

**Anti-goals (would make this a failure even if it technically works)**
- A Mac build that passes only because GPU or GUI tests skip. On the development Mac,
  `DVV_REQUIRE_GPU=1` is used and the display tests run.
- Relaxing dvv-gpu's Vulkan 1.4 / feature requirements, or the project rules (warnings as errors,
  licence gate, privacy lint, pinned third-party with SHA256), to make macOS pass.
- Breaking or slowing down Windows or Linux. Every change keeps their builds and tests green.
- An app that only runs on the machine that built it: anything picked up from `/opt/homebrew`,
  `/usr/local`, the Vulkan SDK or a user home path in a shipped binary is a package-check failure.
- A blurry (non-Retina) or wrongly sized viewport.

## 4. Constraints carried over from the project

All existing rules apply unchanged on macOS: C++23, warnings as errors, `-ffile-prefix-map`, the
privacy lint (it must also catch `/Users/<name>` paths), the third-party superbuild with version +
URL + SHA256 + SPDX for every dependency and the licence gate, FFmpeg loaded at run time and
never linked (the import check), GPU tests exiting 77 without a usable device unless
`DVV_REQUIRE_GPU=1`, conventional commits, no machine-specific or personal data in any committed
file, and no wx in the sanitizer builds.

## 5. What exists today (findings)

No dvv repository or rawreel has any macOS code yet. Useful starting points:

- dvv-platform's `dynamic_library_posix.cpp` and `file_reader_posix.cpp` are shared POSIX code.
- dvv-registry's component linking already names Apple `-force_load` in its design.
- rawreel's `src/gui/main.cpp` already excludes the X11 start-up for `__APPLE__`; `CMakeLists.txt`
  falls through to `viewport_stub.cpp` for non-Windows, non-GTK toolkits.
- custom_ffmpeg v1.0.0 already publishes `macos-arm64-shared-lgpl` (and minimal/full) archives.
  Checked: built for macOS 27.0 (SDK 27), install names already `@rpath`, ad-hoc linker
  signatures, **but no `LC_RPATH`**: `dlopen` of `libavcodec.62.dylib` by full path fails with
  "Library not loaded: @rpath/libswresample.6.dylib … no LC_RPATH's found", even with its
  dependencies loaded first. Adding `@loader_path` as an rpath (`install_name_tool -add_rpath`)
  and re-signing ad-hoc makes all of them load (tested), hence U-MAC10.
- Slang publishes macOS aarch64 release archives.
- On the development Mac (M4, macOS 27, Command Line Tools with the macOS 27 SDK, Homebrew LLVM
  23, CMake 4.4): **Apple clang 21 and Homebrew LLVM 23 both lack `std::move_only_function`** in
  libc++ (`__cpp_lib_move_only_function` is undefined), which the dvv libraries and rawreel use
  about 60 times. `std::jthread`, `std::stop_token`, `std::expected`, `std::print`/`std::println`
  compile with both. Nothing else has been probed yet.

What is missing, by repository:

| Repository | Work |
|---|---|
| dvv-build | compiler check accepts AppleClang + libc++ (today: Clang + libstdc++ 14 only); macOS presets; `bootstrap.sh` macOS branch (prerequisites through Homebrew: ninja and whatever else the superbuild needs); superbuild: Slang macOS aarch64 archive, wxWidgets `osx_cocoa`, custom_ffmpeg v1.0.1 `macos-arm64-shared-lgpl` (U-MAC10), **Vulkan-Loader from source** (new: on Windows/Linux the system loader is used), **KosmicKrisp from the pinned LunarG macOS Vulkan SDK** (new, U-MAC7), Vulkan-ValidationLayers for macOS; `tools/check-vulkan-device.cmake`; `tools/sanity.sh` (reads `/proc/uptime`); macOS jobs in the shared CI workflow (U-MAC6) |
| dvv-base | `executablePath()` (`_NSGetExecutablePath` + canonical path; today `/proc/self/exe`); `userDataDir`/`userCacheDir` → `~/Library/Application Support/<app>` and `~/Library/Caches/<app>`; a `move_only_function` replacement (C2) |
| dvv-registry | verify whole-archive linking with `-force_load` on Apple ld; tests |
| dvv-platform | new `WindowSystem::Metal` and `MetalWindow` (a `CAMetalLayer*`) alternative in `NativeWindow`; `surface_metal` through `VK_EXT_metal_surface`; a macOS `TestWindow` (Objective-C++); `file_reader_posix`: no `O_DIRECT` or `posix_fadvise` on macOS → `fcntl(F_NOCACHE)` for `Unbuffered`, `F_RDAHEAD` (or nothing) for the sequential hint; `.dylib` names in tests (`libc.so.6`, `libvulkan.so.1`) |
| dvv-gpu | loader name `libvulkan.1.dylib` and where it is searched (development: the superbuild prefix; app: inside the bundle); portability enumeration only if KosmicKrisp needs it (found by the spike, §10); presenter on a `CAMetalLayer` (resize, drawable size in physical pixels); `thread_wait` is Windows-only by design and needs no macOS version |
| dvv-media | expected to build as is; run its tests |
| dvv-components | `libraryFileName()` → `libavutil.60.dylib`; the no-FFmpeg-import check with `otool -L`/`llvm-objdump --macho` instead of `readelf -d`; FFmpeg folder discovery inside the app bundle; TSan suppressions name `.dylib` files |
| rawreel | CMake `APPLE` branch: `MACOSX_BUNDLE`, `Info.plist`, a Cocoa viewport (`viewport_cocoa.mm`); `nativeWindowOf()` and `syncNativeWindow()` for wxOSX; review of the `__WXGTK__`/`_WIN32` branches in `main_frame.cpp`, `selftest.cpp`, `viewport.cpp`, `startup.cpp`; release symbols as dSYM (`packaging/symbols.cmake` knows PDB and split `.debug` only); macOS packaging, manifest, package checks and smoke for Mach-O; README "Building and testing (macOS)" and "Current limits"; `release.yml` macOS job (U-MAC6) |
| custom_ffmpeg | v1.0.1 with `LC_RPATH @loader_path` in the macOS shared libraries (U-MAC10) |

## 6. How it works (decided parts)

### 6.1 The viewport window

`dvv::platform::NativeWindow` gains a third alternative, `MetalWindow { void* layer; }`, a
`CAMetalLayer*` that the caller keeps alive while any surface made from it exists, and
`WindowSystem::Metal` (`windowSystemName` → `"Metal"`). `defaultWindowSystem()` returns `Metal` on
macOS. Adding the alternative makes the compiler point at every `std::visit` and `switch` over
`NativeWindow`/`WindowSystem` that needs a macOS case.

rawreel-gui's viewport panel on wxOSX hosts a layer-backed child `NSView` whose layer is a
`CAMetalLayer`. `nativeWindowOf()` returns that layer. The layer's `contentsScale` follows the
window's backing scale factor; the presenter's `PixelSize` is the view's size × that factor. A
change of display (scale factor change) is a resize.

### 6.2 Vulkan on macOS

dvv-gpu loads the Vulkan loader `libvulkan.1.dylib` at run time, as it loads `vulkan-1.dll` and
`libvulkan.so.1` today. The loader finds KosmicKrisp through its ICD manifest: in a build tree
through the superbuild prefix (the tests set the variables, as they set `VK_ADD_LAYER_PATH`
today), in the app through the bundle's own `Contents/Resources/vulkan/icd.d`. The Vulkan
instance and device requirements are unchanged.

So that a system-wide Vulkan SDK or MoltenVK can never be picked instead (C11), rawreel-gui and
rawreel-cli on macOS set `VK_DRIVER_FILES` to the bundled KosmicKrisp manifest before the loader
is opened, unless the user has set `VK_DRIVER_FILES` or `VK_ICD_FILENAMES` themselves; the macOS
tests set it to the superbuild prefix's KosmicKrisp manifest. The log names the Vulkan driver in
use (`GpuInfo::driverName` and `driverInfo`) when the GPU stack starts, so a log attached to a
report shows which one ran. `--version` names the driver of the device it reports (`Vulkan: <device>, API <version>, driver <driverName> <driverInfo>`), and the GUI logs `Vulkan device: <device>, driver <driverName> <driverInfo>` for the device it uses.

### 6.3 The app

`rawreel.app` holds `rawreel-gui` as the main executable and `rawreel-cli` next to it in
`Contents/MacOS`. FFmpeg's six libraries, the Vulkan loader and the KosmicKrisp driver are inside
the bundle; every shipped Mach-O file refers to its dependencies only through `@rpath`/
`@executable_path`/`@loader_path` or system paths (`/usr/lib`, `/System`).

## 7. Calls made by Claude (not agreed by the user; review these)

- **C1 Toolchain: Apple clang** (Xcode or Command Line Tools with the macOS 27+ SDK) and the
  system libc++, with a deployment target of macOS 27.0 (`LSMinimumSystemVersion` 27.0). Reason: the system libc++ ships with
  macOS, so nothing has to be bundled or statically linked, and it is what wxOSX and Apple's
  frameworks expect. The dvv-build compiler check is extended to accept `AppleClang` with libc++
  on Apple only; Linux keeps Clang 19 + libstdc++ 14. Homebrew LLVM is not used for building
  (its libc++ lacks `move_only_function` too, so it buys nothing).
- **C2 `move_only_function`:** dvv-base gets `dvv::MoveOnlyFunction<Sig>`, an alias for
  `std::move_only_function` where the standard library has it (`__cpp_lib_move_only_function`)
  and a small own implementation elsewhere. All uses in the dvv libraries and rawreel move to the
  alias. Windows and Linux keep the standard type through the alias, so their behaviour doesn't
  change. The own implementation gets its own unit tests, which run on every platform.
- **C3** (withdrawn: where KosmicKrisp comes from is now the user's decision U-MAC7.)
- **C4 Preset names:** `ninja-macos` (Debug), `ninja-macos-release`, `ninja-macos-asan`
  (AddressSanitizer + UndefinedBehaviorSanitizer). ThreadSanitizer stays Linux-only: one TSan
  platform is enough, and the macOS system frameworks add noise.
- **C5 Mac app behaviour:** wx standard IDs put About, Settings and Quit in the application menu;
  wx maps Ctrl accelerators to ⌘. Files opened from Finder or the Dock (`wxApp::MacOpenFiles`)
  take the same route as a drop on the window. `Info.plist` declares the file types rawreel opens
  (Y4M, YUV/raw, NUT, MKV, and the other FFmpeg containers rawreel accepts) with the Viewer role,
  rank Alternate, so rawreel never takes over a file type from another app.
- **C6 Release symbols:** dSYM bundles, shipped as a separate symbols archive, the macOS
  counterpart of the PDB zip and the Linux `.debug` files.
- **C7 Crash reports:** no rawreel crash handler on macOS; macOS writes its own crash reports
  (`~/Library/Logs/DiagnosticReports`). The README's "Bug reports" section says where to find
  them. This matches Linux, which relies on core dumps.
- **C8 Package format:** a `.dmg` holding `rawreel.app` (plus `README`, licences and third-party
  notices, as the other packages have). `rawreel-cli` is used from inside the app; the README
  shows how to put it on `PATH`.
- **C9 Versions:** each changed library gets a minor version bump (new platform), and rawreel goes
  to 0.10.0 with macOS listed in its release notes.
- **C10 GitHub-hosted macOS runners** expose only a paravirtual Metal device ("Apple Paravirtual
  device"); whether KosmicKrisp runs on it is undocumented. The first macOS CI run records what
  `dvv-gpu-info` reports there. If KosmicKrisp does not run, the GPU tests skip (77) in CI. If it
  does, they run there too, but the development Mac stays the authority. The display tests run in
  CI only if the runner's desktop session allows it; otherwise they are excluded by label in the
  macOS CI job, as Linux CI handles `display`.
- **C11 Driver isolation:** the `VK_DRIVER_FILES` rule of §6.2, so "no MoltenVK" (U-MAC2) holds
  even on a Mac with the Vulkan SDK installed, and test results are always about KosmicKrisp.
- **C12 Bundle identifier:** `io.github.barabashkad.rawreel` (the GitHub handle, which the privacy
  rules allow). Settings and macOS permissions hang on it, so it does not change later.

## 8. Done means

**Stage A (from source)**, on the Apple Silicon development Mac, macOS 27+:
1. `libs/dvv-build/bootstrap.sh` prepares the macOS third-party prefix from a clean clone.
2. `cmake --workflow --preset ninja-macos` configures, builds Debug and Release and passes **every**
   test, with `DVV_REQUIRE_GPU=1` and `DVV_VK_VALIDATION=sync`, with zero validation messages; the
   display and GUI selftests run (not skipped). The same holds for each dvv library on its own.
3. `ninja-macos-asan` builds and passes (rawreel-gui is not built there, as on the other platforms).
4. `rawreel-cli --version` names the FFmpeg version; `rawreel-gui
   'lavfi:testsrc2=s=1280x720:r=25,format=yuv420p'` plays; a 4K raw file plays at 60 fps on the
   built-in Retina display (recorded as a data point, as the spec does for the other platforms).
5. Windows and Linux builds and tests of every changed repository stay green.

**Stage B (the app):**
6. The packaging dry run on macOS builds the `.dmg` and its symbols archive, and the package
   checks pass: no non-system, non-bundle-relative dependency in any Mach-O file, no FFmpeg
   import in the executables, no machine or user-home path in any shipped file, the licence
   notices complete (KosmicKrisp/Mesa and Vulkan-Loader included).
7. The package smoke test passes on the development Mac (U-MAC8): the app opens, plays the
   `lavfi` test source and a raw file, and `rawreel-cli info` works, and the loaded-image audit
   finds every loaded library and the Vulkan driver inside the bundle or in `/usr/lib`/`/System`,
   with Homebrew and the Vulkan SDK still installed on that Mac.
8. The README documents building, testing, the package and its limits on macOS.
9. The macOS CI jobs (U-MAC6) are green for every changed repository, and a `release.yml` dry run
   produces the `.dmg` and its symbols archive as workflow artifacts.

## 9. Open questions for the user

None. (Q1 signing was answered by U-MAC9, Q2 CI by U-MAC6.)

## 10. Left to the builder

- The order of the repositories and the release steps, within U-MAC4's "each library releases,
  then rawreel moves its pins".
- **First step, a throwaway spike:** install the Vulkan SDK 1.4.363.0 (or newer, the version U-MAC7 will pin) on the
  development Mac, build dvv-gpu's `dvv-gpu-info` against it, and run it on KosmicKrisp. Its
  `missingRequirements()` output lists any required feature KosmicKrisp lacks, and the run shows
  whether KosmicKrisp needs portability enumeration. If any required feature is missing, stop and
  bring it to the user (U-MAC2 and the anti-goals forbid working around it silently).
- The exact `fcntl` choices in `file_reader_posix` on macOS, judged by `rawreel-cli bench`.
- How the wxOSX viewport child view is created and kept in sync on resize, within §6.1.
- Which of the other C++23 library features the code uses also need a fallback on Apple libc++
  (found by compiling; each one follows the C2 pattern).
- How the package checks read Mach-O files (`otool`, `llvm-objdump`, `codesign -dv`), within the
  checks listed in §8.
