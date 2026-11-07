# dvv-build 0.3.0: CI speed-up: design

Status: DRAFT, awaiting the user's approval. Date 2026-11-07. Inputs: dvv-build v0.2.0 (`bootstrap.sh`,
`bootstrap.ps1`, `deps/versions.cmake`, `deps/CMakeLists.txt`, `cmake/DvvThirdParty.cmake`,
`.github/workflows/dvv-ci.yml`), the measured dvv-build 0.2.0 / dvv-base 0.2.0 PR runs, the research
notes `.superpowers/sdd/2026-11-07-macos-apple-silicon/research-wx-cache.md` and
`research-spirv-tools-cache.md`, and a read-only spike on the development Mac (section 4).

Evidence marks used below: **[spike]** measured or read in the spike, **[code]** read in the
repositories, **[run]** measured in the CI runs, **[est]** an estimate derived from those numbers,
**[unverified]** not checked; the plan must check it first.

## 1. Intent and the measured problem

All eight repositories (seven dvv libraries and rawreel) call the one reusable workflow
`dvv-ci.yml`. A cold CI job spends 10-24 minutes bootstrapping the third-party prefix and only 1-2
minutes building and testing **[run]**.

Where the time goes on Windows (Visual Studio generator, serial) **[run]**: wxWidgets 4m52s (Debug and
Release), SPIRV-Tools 3m42s, Vulkan-ValidationLayers 14m05s. Linux takes about 16 minutes, macOS about
10 (Ninja overlaps the builds). Three things multiply the cost:

- One whole-prefix `actions/cache` entry is keyed on `hash(deps/**, DvvThirdParty.cmake,
  DvvSpdx.cmake)` **[code]** (`dvv-ci.yml`, "Restore third-party"). Any edit under `deps/` invalidates
  the whole prefix, wxWidgets included, although wxWidgets did not change.
- Caches are per repository and per ref scope; a PR can restore only from its own merge ref and from
  the default branch (GitHub's dependency-caching reference, quoted in `research-wx-cache.md`).
- The three Windows matrix jobs (`windows-msvc`, `-release`, `-asan`) share one cache key, so on a miss
  all three build the same prefix in parallel (and likewise three Linux and two macOS jobs).

The validation layer (and the SPIRV-Tools, SPIRV-Headers and Vulkan-Utility-Libraries linked into it)
is a test tool that is never shipped, and it is by far the largest cost on Windows: 14m05s + 3m42s of
about 23m **[run]**.

Goal: a cold prefix in about 6-9 minutes and a warm job in 1-3 minutes on every OS **[est]**, without
loosening any pin (section 8).

## 2. Decisions made by the user (binding)

- **U-CI1 The validation layer comes from the pinned LunarG Vulkan SDK prebuilt** on Windows, Linux and
  macOS: only the layer library and its manifest are extracted, as the macOS KosmicKrisp entry already
  does from the SDK zip. SPIRV-Headers, SPIRV-Tools and Vulkan-Utility-Libraries are dropped (only the
  validation layer uses them **[code]**: `DEPENDS` in `deps/versions.cmake`; `research-spirv-tools-cache.md` §1).
- **U-CI2 wxWidgets gets its own CI-only `actions/cache` entry**, keyed only on wx's inputs (its
  `versions.cmake` entry text, the toolchain, the compiler version, the superbuild code that builds it).
  No published prebuilt. It is warmed on `push` to `main`. GitHub's 7-day idle eviction is accepted.
- **U-CI3 A "prime" bootstrap job per OS and toolchain** that the matrix jobs `needs:`, so the three
  Windows jobs (and the Linux and macOS ones) do not build in parallel.
- **U-CI4 Optional: Ninja for the Windows superbuild** under the MSVC environment.

Every third-party input stays pinned: version, URL, SHA256, SPDX, and the licence gate.

## 3. What the spike established (summary)

LunarG publishes SDK 1.4.363.1 for all three systems, the same version the macOS KosmicKrisp entry
pins **[spike]** (`https://vulkan.lunarg.com/sdk/latest/{linux,windows,mac}.json` all answer
`1.4.363.1`; `…/sdk/versions/linux.json` lists it first). Each archive was downloaded and its SHA256
matched LunarG's published file.

| | URL (`https://sdk.lunarg.com/sdk/download/1.4.363.1/…`) | size | SHA256 (LunarG's `…/sdk/sha/1.4.363.1/<os>/<file>.txt`) |
|---|---|---|---|
| Linux | `linux/vulkan_sdk.tar.xz` | 366 690 104 B (350 MiB; 1.94 GiB unpacked) | `b7867d8ca658563813af666f34b93db87f1b1c7d9d69d97e924c210c7ba33eb8` |
| Windows | `windows/vulkan_sdk.exe` | 303 066 832 B | `c02932bd9a968b22d404536108adaacac908c081cd6b0113f5bfefc523dd0a6a` |
| macOS | `mac/vulkan_sdk.zip` (already pinned) | 397 584 552 B | `a65c707cf0a32bf8c2ca6b9af1985b7af61dbadfc19b352dae51a323f3a547a6` (equals the pin in `versions.cmake`) |

Where LunarG publishes the SHA256: `https://sdk.lunarg.com/sdk/sha/<version>/<os>/<file>.txt`, a
one-line `sha256sum` file named after the real file name (`vulkansdk-linux-x86_64-1.4.363.1.tar.xz`,
`vulkansdk-windows-X64-1.4.363.1.exe`, `vulkansdk-macos-1.4.363.1.zip`). The pin is still copied into
`versions.cmake` (we do not trust the URL at build time). The tarball is x86_64 only (the only top
folder is `x86_64/`) **[spike]**; the Linux slang entry is x86_64 only as well **[code]**, so no new
architecture limit arises.

| | layer file | manifest | `api_version` / `implementation_version` | binary facts |
|---|---|---|---|---|
| Linux | `1.4.363.1/x86_64/lib/libVkLayer_khronos_validation.so` (411 MB, unstripped: it has `.debug_info`, `.symtab`) | `1.4.363.1/x86_64/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json` (131 KB; `library_path` is already `../../../lib/libVkLayer_khronos_validation.so`) | 1.4.363 / 1 | ELF64 x86-64. NEEDED: `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1`, `libc.so.6`, `ld-linux-x86-64.so.2`. Highest symbol versions: GLIBC_2.34, GLIBCXX_3.4.30, CXXABI_1.3.13, GCC_3.4 |
| Windows | `Bin/VkLayer_khronos_validation.dll` (21.9 MB; the `.pdb` beside it is 129 MB and not needed) inside the installer's payload | `Bin/VkLayer_khronos_validation.json` (133 KB; `library_path` `.\VkLayer_khronos_validation.dll`) | 1.4.363 / 1 | PE, imports only `KERNEL32`, `ADVAPI32`, `MSVCP140`, `VCRUNTIME140(_1)` and the UCRT api-sets |
| macOS | `~/VulkanSDK/1.4.363.1/macOS/lib/libVkLayer_khronos_validation.dylib` (72 MB) | `…/macOS/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json` (181 KB; `library_path` `../../../lib/libVkLayer_khronos_validation.dylib`) | 1.4.363 / 1 | universal x86_64 + arm64, `minos 11.0`, built with SDK 27.0, links only `/usr/lib/libc++.1.dylib` and `libSystem`; signed by LunarG with the hardened runtime |

- **Linux fit on Ubuntu 24.04** **[spike]**: the layer needs glibc 2.34 (24.04 has 2.39) and GLIBCXX
  3.4.30 (the gcc-14 libstdc++ on 24.04 provides up to 3.4.33). Nothing else beyond the five
  libraries above is needed, and all are in a stock runner. It does not need GTK or X libraries.
- **Windows installer** **[spike]** (not run): `vulkan_sdk.exe` is a PE32+ console program and a Qt
  Installer Framework installer (`strings`: "Built with Qt Installer Framework 4.10.0", the headless
  help text of the framework, the options `accept-licenses`, `default-answer`, `confirm-command`).
  The macOS installer is the same framework, and its headless command is already proven in
  `deps/post-install/kosmickrisp.cmake`. The installer has exactly one installable component,
  `com.lunarg.vulkan.core` (plus the empty parent `com.lunarg.vulkan`); the payload is 7z archives
  (listed by `bsdtar`): `Bin/` (all DLLs and manifests, 116 files, 257 MB compressed), `Lib/`,
  `Include/`, `Source/`, `Templates/`, `Helpers/` (`install_helper.exe`, `launch_helper.exe`,
  `VC_redist.X64.exe`, `VulkanRT.exe`), `cmake/` and others. So the headless command is
  `vulkan_sdk.exe --root <dir> --accept-licenses --default-answer --confirm-command install
  com.lunarg.vulkan.core`, and the layer lands in `<dir>/Bin/` (**inferred** from the payload's folder
  names; the installer script could move it: **[unverified]**).
  There is no smaller artifact: the guessed names `VulkanRT-1.4.363.1-Components.zip` and
  `VulkanRT-Installer.exe` under the same download path answer 404, and the SDK's runtime
  installer is not known to carry layers **[unverified]**. The core component is one 303 MB download.
- **macOS** **[spike]**: the layer is in the already installed SDK (component `com.lunarg.vulkan.core`,
  the one `kosmickrisp.cmake` installs), next to the KosmicKrisp driver.
- **Layer version** **[spike]**: 1.4.363 against today's source-built 1.4.365. The SDK's `VERSIONS.txt`
  (macOS, installed) names Vulkan-ValidationLayers commit `317cff060893`, SPIRV-Tools `ef96ed763b43`
  and SPIRV-Headers `496543121ce6`; the last two are exactly the commits `versions.cmake` pins as
  "the known-good commit" of 1.4.365 **[code]**, so the SPIR-V side is identical. The layer source is two
  patch releases older. The Linux SDK has no `VERSIONS.txt`; the Windows one was not looked for
  **[unverified]**.
- **dvv-gpu requirement** **[code]**: `libs/dvv-gpu/src/instance.cpp` (~line 288) takes the layer only
  when `layer->specVersion >= VK_API_VERSION_1_4`; the manifest `api_version` 1.4.363 gives
  specVersion 1.4.363, which passes. Nothing else in dvv-gpu depends on a layer version: the code
  enables the layer through vk-bootstrap (`enable_validation_layers`), and in `sync` mode sets the layer
  setting `validate_sync` (`VK_EXT_layer_settings`); that key exists in all three 1.4.363 manifests
  **[spike]** (`"key": "validate_sync"`). The tests only count validation messages
  (`tests/gpu_fixture.cpp`, `gpu_test_registry.cpp`, one test that provokes an error and needs only
  that the layer reports it) and find the manifest with `find_file(VkLayer_khronos_validation.json …
  PATH_SUFFIXES bin share/vulkan/explicit_layer.d)` in the prefix (`tests/CMakeLists.txt`). No other
  layer settings file is used **[code]** (grep of `libs/dvv-gpu`, `libs/dvv-build/tools`).
  A residual risk: the repositories compile against Vulkan-Headers 1.4.365, and a layer two patch
  releases older may not know a 1.4.364/1.4.365 addition; dvv-gpu's required features are older, so this
  is expected not to matter but is **[unverified]** until the GPU tests run.

Nothing found blocks the LunarG-layer decision. The one thing the spike could not settle (the Windows
installer was not run) is section 5.2.

## 4. wxWidgets: its own CI cache entry (U-CI2)

### 4.1 Where wx installs

Today wx installs into the prefix root with `-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>` mixed with every
other dependency **[code]** (`dvv_third_party_common_args`), so it cannot be cached alone. The
three wx entries get `-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>/wxWidgets` after the common arguments (the
later `-D` wins), so wx lives in `<prefix>/wxWidgets/` (Windows: Debug and Release libraries, as now).
Consumers find it through a second `CMAKE_PREFIX_PATH` element, `<prefix>;<prefix>/wxWidgets`, in
`cmake/DvvPresets.json` and `cmake/DvvAppPresets.json` (the three toolchain presets of each set it to
the prefix only today **[code]**). Whether wx's installed `wxWidgetsConfig.cmake` is relocatable and free
of build paths, and whether any consumer needs more than that preset change, is **[unverified]**; the
restored cache lands at the same absolute path (the path string is part of the cache version), so
relocatability is not needed in CI.

### 4.2 The wx input hash (authoritative, computed by CMake)

A function `dvv_wx_inputs_hash(<out>)` in `cmake/DvvThirdParty.cmake` hashes (SHA256, one text):

- the wx entry of this OS: VERSION, URL, SHA256, LICENSE_FILES, CONFIGS, CMAKE_ARGS (SPDX only
  changes notices, which are rebuilt from the entry, not from the cache);
- the common arguments without the compiler paths (`-DCMAKE_*_COMPILER=`), because the compiler enters
  through its version below;
- `CMAKE_GENERATOR`;
- the compiler: `CMAKE_CXX_COMPILER_ID` and `_VERSION`; on Windows the MSVC toolset
  (`CMAKE_CXX_COMPILER_VERSION` is the toolset's `cl` version); on Apple the SDK version
  (`CMAKE_OSX_SYSROOT` resolved) and the deployment target;
- `DVV_WX_RECIPE_REV`, an integer next to the function that is bumped whenever the code in
  `deps/CMakeLists.txt` / `DvvThirdParty.cmake` that builds a project changes in a way that affects wx
  (the "superbuild code that builds it"). Hashing those whole files would invalidate wx on every
  unrelated edit, which is what this design removes.

The hash is written to `<prefix>/wxWidgets/dvv-wx-inputs.sha256` as the last step of wx's install.

### 4.3 How the superbuild and bootstrap treat a restored wx

- `deps/CMakeLists.txt`, for the wxWidgets entry: when `<prefix>/wxWidgets/dvv-wx-inputs.sha256` exists
  and equals the hash just computed, the entry is not built. It becomes a no-op target (download,
  configure, build and install are empty, as the shared-archive branch already does for vulkaninfo)
  whose `SOURCE_DIR` for the notices is `<prefix>/wxWidgets/dvv-licences/`. A mismatch removes
  `<prefix>/wxWidgets` and builds it, so a stale restore can only cost time, never reuse the wrong objects.
- The licence texts: the notices read LICENSE_FILES from the entry's source directory, which a skipped
  entry has not downloaded. wx's install step therefore copies its LICENSE_FILES (same relative paths)
  into `<prefix>/wxWidgets/dvv-licences/`, and the skipped entry points there. The licence gate and
  SPDX declaration stay in `versions.cmake` unchanged.
- `bootstrap.sh` / `bootstrap.ps1` keep the whole-prefix stamp `versions.sha256` (hash of `deps/**`
  and the two modules, plus `DVV_BUNDLE_FFMPEG`). Its fast path (exit when equal) gains one condition:
  `<prefix>/wxWidgets/dvv-wx-inputs.sha256` must exist, because the CI prefix cache excludes wx (4.4)
  and could otherwise report "up to date" without it. Existence only: the real comparison is in CMake.
- The stamp does not see the compiler. That was already true for the whole-prefix cache and stays so
  for local runs (non-goal); in CI the wx cache key carries the compiler fingerprint (4.4).

### 4.4 Cache keys and restore order (CI)

wx cache (new), one entry per OS and toolchain:

- path: `<DVV_ROOT>/_third_party/<toolchain>/wxWidgets`
- key: `wx-<runner.os>-<toolchain>-<entry-hash>-<fingerprint>`, where
  - `<entry-hash>`: the first 16 hex digits of `dvv_wx_inputs_hash` without its compiler terms, printed
    by a new `tools/wx-cache-key.cmake` (`cmake -P`, writes `hash=…` to `$GITHUB_OUTPUT`) from the same
    function and the same `versions.cmake`, so the key and the in-prefix marker cannot drift;
  - `<fingerprint>`: a shell-computed string per OS: Linux, `clang-19 --version` plus the `dpkg`
    versions of `libgtk-3-dev`, `libpng-dev`, `zlib1g-dev` (wx links the system's GTK, zlib and libpng
    there, `versions.cmake`); Windows, `vswhere -latest -property catalog_buildVersion` plus the
    `VCToolsVersion` directory; macOS, `xcodebuild -version` plus `xcrun --show-sdk-version`.
    The runner image version is deliberately not used, since it changes weekly without touching wx.
- restore with `actions/cache/restore` (exact key only; no `restore-keys`, a different wx must never
  be restored), save with `actions/cache/save` when `cache-hit != 'true'`.

The whole-prefix cache keeps its present key (`hash(deps/**, DvvThirdParty.cmake, DvvSpdx.cmake)`) and
path, with the wx folder excluded (`!<DVV_ROOT>/_third_party/<toolchain>/wxWidgets`), so wx is not
stored twice. Order in a job: restore wx, restore prefix, bootstrap (fast path or superbuild), save
what missed. The rest of the prefix is small and cheap to rebuild (nlohmann_json, doctest, Vulkan-Headers,
VMA, vk-bootstrap, the slang, FFmpeg and SDK downloads); on Windows everything except wx and the
two Khronos builds took about a minute **[est from run]**.

### 4.5 Prime job and main warm-up

`dvv-ci.yml` gets a `prime` job with a three-entry matrix (the distinct OS and toolchain pairs: `windows-2025
msvc`, `ubuntu-24.04 clang`, `xcode-27 appleclang`). It does what each `build` job's bootstrap steps do
today (install tools, restore wx and prefix, bootstrap, save both) and nothing else. `build`
(`needs: [dvv-build-min, prime]`, matched on OS and toolchain: `needs` is per job, so every build job
waits for all three primes, or the primes are split into three jobs `prime-windows`, `prime-linux`,
`prime-macos`; the plan picks the second so a slow macOS prime does not hold up Windows) then only restores,
runs bootstrap (a no-op on a hit) and never saves. The `lint` job also `needs:` `prime-linux` and drops its own
bootstrap and save.

- On a PR: the prime job restores from the PR's merge ref, then from `main`'s caches (GitHub's
  base-branch fallback), so a PR that touches only `deps/` entries other than wx hits `main`'s wx entry. A
  miss saves under the PR's scope, visible to the PR's own `build` jobs in the same run.
- Warm-up: each repository's own `ci.yml` already runs `dvv-ci` on `push` to `main` **[code]**
  (`libs/dvv-base/.github/workflows/ci.yml`, with `cancel-in-progress` off for pushes so the save is reached);
  every merge to `main` therefore leaves the entries in `main`'s scope. Each of the eight repositories
  warms its own (caches are not shared across repositories).
- Eviction: an entry not read for 7 days is removed (accepted by the user, U-CI2); a repository idle
  for a week pays one cold prime run, once.
- On a miss, `prime` builds wx once (about 5 minutes **[run]** on Windows) and saves it; the matrix jobs
  start afterwards and hit. If two runs miss the same key at once, the second save only warns
  (existing behaviour **[code]**: "the first save wins and the others only warn").
- If `prime` fails, the matrix does not start: a bootstrap failure is reported once, not three times.

## 5. The validation layer from the SDK

### 5.1 Entries

`Vulkan-ValidationLayers`, `Vulkan-Utility-Libraries`, `SPIRV-Headers` and `SPIRV-Tools` leave
`versions.cmake`; their source-build post-install `deps/post-install/Vulkan-ValidationLayers.cmake`
(which rewrote the Linux `library_path`) is replaced. Three `PREBUILT` entries named
`Vulkan-ValidationLayers`, like the three `slang` entries:

- version 1.4.363.1, the SDK URL and SHA256 of section 3, `ONLY_ON Linux` / `Windows` / `Darwin`;
- SPDX `Apache-2.0 AND BSD-2-Clause AND BSL-1.0 AND MIT`, the expression of the source build today, which
  already covered the statically linked SPIRV-Tools (Apache-2.0), SPIRV-Headers (MIT) and
  Vulkan-Utility-Libraries (Apache-2.0) **[code]**; the SDK build links the same set (the
  `VERSIONS.txt` commits above). That the binary links nothing else (for example a different regex or
  hashing library) is **[unverified]**; the plan checks the layer's strings/symbols against the expression;
- `LINKAGE runtime`, not `SHIPPED`;
- macOS: `DEPENDS kosmickrisp`, which shares the already extracted SDK zip exactly as the `vulkaninfo`
  entry does **[code]**; the layer is copied by `kosmickrisp.cmake`'s post-install (the installer is
  deleted there), not by a second one.

Licence texts: the SDK archives carry only LunarG's statement (`LICENSE.txt`, 944 bytes: "The Vulkan SDK
is comprised of 100% open-source components … The SDK itself is not licensed") **[spike]**, not the layer's
licences. As for Mesa under kosmickrisp, the texts become `dvv_licence_supplement`s fetched from pinned
URLs with SHA256: Vulkan-ValidationLayers `LICENSE.txt` and `LICENSES/*.txt` at commit `317cff060893…`,
SPIRV-Tools `LICENSE` at `ef96ed763b43…`, SPIRV-Headers `LICENSE` at `496543121ce6…`. The URLs and hashes
are computed in the plan; **[unverified]** that these files exist at those commits under those names (the
current entries list `LICENSE.txt LICENSES/*.txt` for the layer, `LICENSE` for the other two **[code]**).
The bundled third-party files of the layer (parallel_hashmap, xxhash, inplace_function, VMA) are in the
SPDX expression today; they come along in the same binary.

### 5.2 Post-install per OS

All three copy only the layer and its manifest into the prefix and delete the rest of the extracted SDK
(a pattern `kosmickrisp.cmake` already follows), so the prefix cache does not carry 2 GB of SDK.

- **Linux** **[spike]** layout known. The tarball is a plain archive; the standard `PREBUILT` extract
  unpacks all 1.94 GiB (about 3 s with multithreaded `xz` on this Mac; the runner's CMake extract is
  single-threaded, time **[unverified]**, expect tens of seconds). A cheaper variant is a download-only
  entry plus `cmake -E tar xJf <archive> <member>…` of the layer and manifest; the plan measures both.
  Copy `x86_64/lib/libVkLayer_khronos_validation.so` to `<prefix>/lib/`, strip its debug sections
  (the file is 411 MB; `strip --strip-debug`; the plan checks that the stripped layer still loads) and
  the manifest to `<prefix>/share/vulkan/explicit_layer.d/` (its relative `library_path` already
  works, so the old rewrite step goes). Then remove the extracted SDK.
- **Windows** run the installer headless as in 3 (`--root <build>/sdk … install com.lunarg.vulkan.core`),
  copy `Bin/VkLayer_khronos_validation.dll` and `.json` to `<prefix>/bin/` (the manifest's
  `.\VkLayer_khronos_validation.dll` works beside the DLL, as the source build's did **[code]**), leave the
  `.pdb` behind, remove the installer and the installed SDK. **The one open technical risk**: the framework
  installer runs its component script, and the payload includes `Helpers/VulkanRT.exe`,
  `install_helper.exe` and `VC_redist.X64.exe`; whether a headless install also installs a Vulkan runtime
  system-wide, sets environment variables or writes registry layer keys, which could change which loader
  or layers the tests see, is **[unverified]** (the exe was not run). The plan's first task runs the install on a
  scratch `windows-2025` job and compares loader, registry (`HKLM\SOFTWARE\Khronos\Vulkan`) and `PATH`
  before and after. Fallback if it does change the machine: extract the `Bin` 7z segment from the exe with
  `cmake -E tar` (works with `bsdtar` here **[spike]**: the segment at byte offset 34 765 651 lists `Bin/…`); it
  depends on the framework's file layout, so it is a fallback, not the default.
- **macOS** `kosmickrisp.cmake` also copies `macOS/lib/libVkLayer_khronos_validation.dylib` to
  `<prefix>/lib/` and the manifest to `<prefix>/share/vulkan/explicit_layer.d/` (relative `library_path`
  already `../../../lib/…`). The universal dylib (72 MB, x86_64 and arm64) can be thinned with `lipo
  -thin arm64`, but that invalidates LunarG's signature, and the macOS app keeps the vendor signature
  of KosmicKrisp (commit `29a30cc`); the layer is a test tool never shipped, so thinning is allowed, but
  the plan keeps it whole unless the cache size matters (decision for the plan, not required).

Manifest location, unchanged: Windows `<prefix>/bin/VkLayer_khronos_validation.json`, Linux and macOS
`<prefix>/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json`. `VK_ADD_LAYER_PATH`
points at that folder as today (`tests/CMakeLists.txt` `find_file`, `tools/sanity.ps1`,
`tools/sanity.sh`, `dvv-gpu` README); no test or script changes. The `post-install` steps keep the
"already installed" short-circuit on re-run, as the macOS one does.

### 5.3 Removal of the three source dependencies

`deps/versions.cmake` loses four entries (Vulkan-Utility-Libraries, SPIRV-Headers, SPIRV-Tools and the
source Vulkan-ValidationLayers) and the long comment. `tests/deps/test_versions.cmake` and
`tests/deps/test_superbuild_scripts.cmake` (the manifest-rewrite test), `README.md` (dependencies and CI
sections) and the notices expectations are updated. The CI "Install Linux window-system packages" step
no longer needs `libxcb1-dev libxrandr-dev libwayland-dev libx11-dev` for the layer; `libx11-dev` stays
if GTK needs it for wx (the plan checks `pkg-config` needs of wx, which are those of GTK 3 itself), so only
the layer-only packages go, **[unverified]** which.

Behavioural difference to state in the release notes: the source layer was built with XCB, Xlib and Wayland
WSI support; the SDK's Linux layer is LunarG's build (**[unverified]** WSI support, the layer is a
dispatching layer and validates surfaces through the loader, so no loss is expected, but the Linux
GPU/display tests are the proof).

## 6. Expected timings

Measured today **[run]**: cold Windows about 23-24 min of bootstrap (4m52 + 3m42 + 14m05 plus about a minute
of the rest), Linux about 16, macOS about 10. The estimates below **[est]** use the measured wx time
(about 5 min; the Linux and macOS wx times are the research's rough 4.5-5 min, not measured) and
the SDK download (350-400 MB) plus extraction; the Windows install time is unknown.

| OS | cold (all caches miss), prime job | wx hit, rest misses (deps edit) | all hit |
|---|---|---|---|
| Windows | 7-10 min (wx 5 + SDK download and install 1-3 + rest 1) | 2-4 min | 1-2 min (restore) |
| Linux | 6-8 min (wx 4.5 + SDK 1-2 + rest 1) | 2-3 min | 1 min |
| macOS | 6-8 min (wx 5 + SDK, already downloaded today + rest 1) | 1-3 min | 1 min |

Then every matrix job adds restore (about 1 min) plus the build and test of 1-2 min **[run]**. Whole-run cost
on a cold Windows miss falls from about 3 x 24 = 72 runner-minutes to about 1 x 9 + 3 x 3 = 18. These
are estimates; the plan's last task measures them and replaces this table.

## 7. Non-goals

- No published wxWidgets prebuilt (U-CI2); the research option (b) is dropped.
- No cache shared across repositories; no keep-alive schedule against the 7-day eviction (a one-line
  `schedule:` in each caller would do it; not requested).
- No Ninja on Windows unless measured to pay off (U-CI4, optional, the last task).
- No change to what is shipped: the layer is not shipped, wx is built from the same pinned source with
  the same options, and the packages are unchanged.
- No compiler-version detection in the local bootstrap stamp.
- No change to dvv-gpu, to the Vulkan-Headers pin (1.4.365 stays although the layer is 1.4.363) or to
  KosmicKrisp.

## 8. Risks

- **Windows installer side effects** (5.2): unverified; the first task checks them; fallback is carving
  the `Bin` segment.
- **Layer two patch releases older than the headers** (3): a GPU test could report a false validation
  error about something new in 1.4.364/365; expected not to occur.
- **Trust moves from "built from a pinned source tarball" to "a pinned LunarG binary"**: the SHA256 is
  pinned and published by LunarG, but it is a binary we did not build. Accepted by U-CI1 for a never-shipped
  test tool. LunarG's download URL is stable per version (`/sdk/download/<version>/…`) but old SDKs may be
  withdrawn: **[unverified]** LunarG's retention policy; the `versions.json` list reaches back to 1.3.296.
- **Size**: three SDK downloads of 300-400 MB per cold prefix (macOS already pays it). Mitigation: the
  prefix keeps only the layer (stripped on Linux).
- **Cache key drift** between the shell fingerprint and CMake's compiler terms: the marker file is
  authoritative, so a drift costs a rebuild, never a wrong reuse (4.3).
- **Cache pressure**: the 10 GB per-repository limit; the whole-prefix entry shrinks by wx, the SPIRV/layer
  build trees and installs; the new wx entries are per OS (size **[unverified]**).
- **7-day idle eviction** and **PR-scoped saves**: accepted; only time is lost.
- **Linux layer is x86_64 only**: same as slang today.
- **Sanitizers**: the Linux asan/tsan presets load the same layer binary as before (built with
  GCC/Clang instrumentation off in both cases); unchanged **[unverified]** for tsan, covered by the GPU tests'
  existing suppression note (`tsan.supp`).

## 9. What stays true for local bootstrap users

- `bootstrap.ps1` / `bootstrap.sh` with no flags still produce a working prefix and keep the up-to-date
  stamp short-circuit. The prefix layout differs by `<prefix>/wxWidgets/` (and the presets add it to
  `CMAKE_PREFIX_PATH`) and by the validation layer coming from the SDK.
- wxWidgets is still built from its pinned source with the same options; nothing is downloaded from a
  non-pinned place. The first bootstrap after upgrading rebuilds wx once (it moves).
- Local builds need to download one SDK archive per OS (300-400 MB, once; the file is in the build tree's
  download cache). Offline builds lose the source-built layer; that is the price of U-CI1.
- Version, URL, SHA256, SPDX and the licence gate are unchanged in kind; the notices gain the supplements
  of 5.1; `THIRD_PARTY_NOTICES-all.txt` no longer lists the three source dependencies.
- The local Windows build stays Visual Studio unless U-CI4 is adopted.

## 10. Task outline for the plan

1. Spike on a scratch Windows runner (a `workflow_dispatch` in a throwaway branch of dvv-build): run the
   SDK installer headless, record the installed tree, side effects (loader, registry, `PATH`) and time;
   decide installer versus carving. Check the Linux strip and the `cmake -E tar` member extraction
   times. (Needs the user to allow running a workflow.)
2. `dvv_wx_inputs_hash`, `DVV_WX_RECIPE_REV`, `tools/wx-cache-key.cmake`; wx entries install into
   `<prefix>/wxWidgets` and copy their licences to `dvv-licences/`; the marker and the skipped-entry
   branch in `deps/CMakeLists.txt`; tests in `tests/deps`.
3. `bootstrap.sh` / `bootstrap.ps1`: fast path requires the wx marker; presets add `<prefix>/wxWidgets`
   to `CMAKE_PREFIX_PATH`; check how rawreel and dvv-components find wx.
4. Validation layer: three `PREBUILT` entries (SHA256 from section 3), post-install per OS, licence
   supplements with computed hashes, kosmickrisp post-install copies the macOS layer, remove the four source
   entries and the old post-install, update `tests/deps` and the SPDX check against the layer binary.
5. `dvv-ci.yml`: fingerprint steps, wx cache restore/save, exclude wx from the prefix cache, `prime-*`
   jobs, `needs`, lint on `prime-linux`, drop the layer-only apt packages; check on dvv-build's own CI.
6. Run dvv-base's PR on the new dvv-build: GPU and display tests with `DVV_VK_VALIDATION=sync` on Linux
   (and Windows/macOS where a device exists), confirm the layer is found and `validate_sync` takes effect.
7. Measure cold, wx-hit and warm times on all OSs; replace the table in section 6 with the measurements.
8. Optional (U-CI4): Ninja Multi-Config for the Windows superbuild (keeps the Debug/Release `CONFIGS` logic),
   with `bootstrap.ps1` locating the MSVC environment through `vswhere`; adopt only if the measurement
   (task 7) shows a gain over `UseMultiToolTask`.
9. README, CHANGELOG, `VERSION` 0.3.0, release; then each of the eight repositories bumps its
   `dvv-deps.txt` and its `dvv-ci.yml@v0.3.0` pin (the release order of the earlier dvv-build bumps).

## Decisions after the spike (2026-11-07)

- **Windows install method:** the user approved one throwaway `workflow_dispatch` run on a scratch
  branch of dvv-build when this is implemented, to check whether the headless LunarG installer
  leaves anything on the runner (Vulkan runtime, registry keys, PATH). If it does, the layer is
  unpacked straight from the installer's archive segment instead (the fallback above).
- **Layer version (Claude's call):** a 1.4.363 layer with 1.4.365 headers is accepted — an older
  layer with newer headers is normal and only lacks checks for the newest extensions; the headers
  pin is not lowered.
- **Licence texts (Claude's call):** the layer's licence texts become pinned-URL licence
  supplements, as for Mesa/KosmicKrisp, since the SDK ships none for it.
- **Keep-alive (from the user's earlier decision):** no scheduled keep-alive job — a cache that
  lapses after 7 idle days is the intended behaviour.
- **wx location (Claude's call):** wx installs into `<prefix>/wxWidgets`, a second
  `CMAKE_PREFIX_PATH` entry in the shared presets, so it can be cached and restored on its own.
- **Linux layer size (Claude's call):** the extracted Linux layer is stripped (the SDK's `.so` is
  411 MB unstripped) before it enters the prefix and its cache.

## Design review outcome (2026-11-07, optimist + pessimist, two rounds)

These supersede the sections above where they differ.

1. **Order:** the validation-layer swap is its own first step (most of the saving, independent of
   the wx cache; `release.yml`, which uses no cache, gains it too). The wx cache comes second.
2. **Windows layer:** a plain `PREBUILT` entry — CMake extracts `Bin/` straight from the LunarG
   installer exe (verified with CMake 4.4.5: 116 `Bin/` entries, DLL bit-identical to the carved
   copy, 5 s); the installer is never run on runners (it stays only as a fallback). bootstrap
   requires a CMake version whose bundled libarchive reads the installer (system bsdtar 3.7.4
   cannot). Linux: a plain `PREBUILT` entry too; drop the "measure both" step.
3. **Licences:** the SDK payloads contain no licence file for the layer; the post-install writes
   the pinned licence supplement into the entry's source folder (Linux keeps it when the SDK
   folder is deleted). The Windows layer embeds mimalloc (the source build had
   `USE_MIMALLOC=OFF`), so a mimalloc licence supplement is added for Windows.
4. **wx location:** a sibling folder `_third_party/<toolchain>-wx`, not inside the prefix —
   actions/cache cannot exclude a sub-folder of a cached path (actions/toolkit resolves paths
   with `implicitDescendants: false` and archives them whole), so an old wx inside the prefix
   entry could be restored over a fresh one with a matching marker. Presets, rawreel's
   `packaging/dry-run.cmake` (it passes a single `-DCMAKE_PREFIX_PATH`) and the presets test learn
   the second path.
5. **Key and marker (ruling between the reviewers):** the marker hashes only wx's own
   versions.cmake entry text, a `DVV_WX_RECIPE_REV` and the toolchain name — computable in
   `cmake -P` without the Apple FATAL_ERROR path; the compiler version is part of the CI cache key
   only. bootstrap compares the marker's content (not just its existence); a mismatch forces
   ExternalProject to rebuild wx (the hash is passed in wx's `CMAKE_ARGS`).
6. **Upgraded local prefixes:** bootstrap removes a stale root-level wx and the source-built layer
   from an existing prefix, so they cannot be found before the new ones.
7. **CI job graph:** per-OS build jobs (Windows, Linux, macOS), each `needs:` only its own prime
   job; `fail-fast: false`; prime jobs restore with `lookup-only: true` (no cost on warm runs);
   lint (Format, privacy lint, check-pins) needs no prime job, so a failed prime cannot skip it.
   A caller still pinned to 0.2.0 keeps working on 0.2.0's workflow.
8. **Kept as is:** the Linux apt packages (GTK depends on them; bootstrap.sh's pkg-config check
   and release.yml's list need them); no dpkg-version fingerprint; no Ninja in 0.3.0.
9. **Sanitizers:** confirm `tsan.supp` covers the prebuilt layer (today it lists only lavapipe);
   ASan/TSan jobs load an uninstrumented layer as before.
