# dvv-build 0.3.0 CI speed-up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A cold third-party prefix in about 6-9 minutes and a warm CI job in 1-3 minutes on every OS, with every pin kept.

**Architecture:** Step 1: the Vulkan validation layer comes from the pinned LunarG SDK 1.4.363.1 prebuilt on all three OSs, so SPIRV-Headers, SPIRV-Tools, Vulkan-Utility-Libraries and the layer's source build go away. Step 2: wxWidgets installs into a sibling prefix `_third_party/<toolchain>-wx`, marked with a hash of its inputs, and gets its own CI cache entry; the superbuild skips it when the marker matches. Step 3: `dvv-ci.yml` gets per-OS prime jobs that fill the caches once, and per-OS build jobs that only restore.

**Tech Stack:** CMake 4 (ExternalProject superbuild, `cmake -P` scripts), POSIX sh, PowerShell 7, GitHub Actions (`actions/cache/restore@v5`, `actions/cache/save@v5`).

**Spec:** `docs/superpowers/specs/2026-11-07-dvv-ci-speedup-design.md` (rawreel). Its last section, "Design review outcome", supersedes the earlier sections where they differ.

## Global Constraints

- Every third-party input stays pinned: VERSION, URL, SHA256 (64 lower-case hex), SPDX, and the licence gate (`dvv_spdx_evaluate`).
- LunarG SDK 1.4.363.1, files and SHA256 (spec §3):
  - Linux: `https://sdk.lunarg.com/sdk/download/1.4.363.1/linux/vulkan_sdk.tar.xz`, `b7867d8ca658563813af666f34b93db87f1b1c7d9d69d97e924c210c7ba33eb8`.
  - Windows: `https://sdk.lunarg.com/sdk/download/1.4.363.1/windows/vulkan_sdk.exe`, `c02932bd9a968b22d404536108adaacac908c081cd6b0113f5bfefc523dd0a6a`.
  - macOS: the existing kosmickrisp zip pin.
- The layer is `LINKAGE runtime`, never `SHIPPED`. SPDX: `Apache-2.0 AND BSD-2-Clause AND BSL-1.0 AND MIT`, plus `MIT` for mimalloc on Windows (already in the expression).
- Manifest locations do not change:
  - Windows: `<prefix>/bin/VkLayer_khronos_validation.json` next to the DLL.
  - Linux and macOS: `<prefix>/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json`, with `library_path` `../../../lib/<layer library>`.
- The Vulkan-Headers pin stays at 1.4.365.
- wxWidgets is built from the same pinned source with the same options. No published wx prebuilt. No keep-alive schedule.
- The wx sibling prefix is `<root>/_third_party/<toolchain>-wx`. Its marker `<sibling>/dvv-inputs.sha256` hashes only:
  - wx's own entry (VERSION, URL, SHA256, LICENSE_FILES, CONFIGS, CMAKE_ARGS);
  - `DVV_SEPARATE_PREFIX_RECIPE_REV`;
  - the toolchain name.
  The compiler fingerprint is part of the CI cache key only.
- Linux keeps its apt package list (design review item 8). No Ninja on Windows in 0.3.0. No dpkg fingerprint.
- Commits: identity BarabashkaD / `6252647+BarabashkaD@users.noreply.github.com`. Committed files carry no machine paths, real names or personal e-mail; the privacy lint and the pre-push hook enforce this, and the hook is never bypassed.
- Work happens on side branches (`ci-speedup`) of dvv-build and rawreel. Nothing is merged, tagged or released without the user. The user authorised pushes and CI runs on side branches (2026-11-08).

## Review Focus

1. **An upgraded local prefix** (a 0.2.0 prefix with wx at the root and the source-built layer) must not find the old wx before the new one. The superbuild removes `<prefix>/lib/cmake/wxWidgets`. Test: Task 3, step "stale root wx".
2. **A deleted or stale wx sibling with a surviving build tree** must rebuild wx, not trust ExternalProject's stamps. The superbuild removes wx's stamp dir when the marker mismatches. Test: Task 3, steps "delete sibling" and "marker mismatch".
3. **A bootstrap fast path with the prefix stamp equal but no or wrong wx marker** must run the superbuild. Test: Task 3, bootstrap step.
4. **The Windows layer extraction under the pinned CMake (pip `cmake==4.3.2`)**: libarchive must read the installer exe. Test: Task 1, step 1. If it cannot, raise the pin.
5. **GPU tests under `DVV_VK_VALIDATION=sync`** must find the prebuilt layer and report 0 unexpected messages on Linux lavapipe, including asan and tsan. Test: Task 5 rawreel CI with test_libs.

---

### Task 1: Validation layer from the LunarG SDK (dvv-build)

**Files:**
- Modify:
  - `deps/versions.cmake`: remove Vulkan-Utility-Libraries, SPIRV-Headers, SPIRV-Tools and the source Vulkan-ValidationLayers; add three PREBUILT `Vulkan-ValidationLayers` entries and the licence supplements.
  - `deps/post-install/Vulkan-ValidationLayers.cmake`: rewrite it for the Linux and Windows SDK layouts.
  - `deps/post-install/kosmickrisp.cmake`: also install the macOS layer and its manifest.
- Create: `deps/licenses/Vulkan-ValidationLayers-supplement/...` (the texts, fetched from the pinned commits).
- Test:
  - `tests/deps/test_versions.cmake`
  - `tests/deps/test_superbuild_scripts.cmake`: drop the manifest-rewrite test and add tests of the new post-install.
- Docs: `README.md` (dependency list), `.github/workflows/dvv-ci.yml` comments that mention building the layer.

**Interfaces:** Produces the prefix files named in Global Constraints. The entry name stays `Vulkan-ValidationLayers`, so the notices keep that heading.

- [ ] **Step 1: Facts check (write `<scratchpad>/layer-facts.md`).**
  - In a venv with `pip install cmake==4.3.2`, download the Windows exe, check its SHA256, and run `cmake -E tar tf` and `xf` on it in an empty folder. Record whether `Bin/VkLayer_khronos_validation.dll` and `.json` come out, and whether they are bit-identical to those from CMake 4.4.5. If 4.3.2 fails, find the lowest pip `cmake` that works and record it; Step 3 then raises `tools/requirements.txt` to it.
  - Download the Linux tarball and check its SHA256. Record:
    - the path of the layer `.so` and its manifest;
    - the archive's top folder;
    - whether a LunarG `LICENSE.txt` exists, and where.
  - Check the Linux, macOS (`~/VulkanSDK/1.4.363.1/macOS/lib/...`) and Windows layer binaries for embedded mimalloc (`strings | grep -i mimalloc`) and record the result per OS.
  - Fetch the licence texts:
    - Vulkan-ValidationLayers `LICENSE.txt` and every file in `LICENSES/` at commit `317cff060893` (resolve the full SHA with `gh api repos/KhronosGroup/Vulkan-ValidationLayers/commits/317cff060893`);
    - SPIRV-Tools `LICENSE` at `ef96ed763b43`;
    - SPIRV-Headers `LICENSE` at `496543121ce6`;
    - mimalloc `LICENSE` at the commit or tag the Windows layer embeds. If unknown, the tag Vulkan-ValidationLayers 1.4.363's `scripts/known_good.json` names.
    Record each raw URL and SHA256.
- [ ] **Step 2: Tests first.**
  - In `test_versions.cmake`: per platform, `Vulkan-ValidationLayers` is PREBUILT, VERSION 1.4.363.1, with the URL and SHA256 above. On Darwin it `DEPENDS kosmickrisp` and shares its archive (`dvv_third_party_shared_archive` returns `kosmickrisp`). SPIRV-Tools, SPIRV-Headers and Vulkan-Utility-Libraries are declared on no platform.
  - In `test_superbuild_scripts.cmake`: run the new post-install on fake SDK trees in WORK_DIR (a Linux tree and a Windows `Bin/` tree with dummy files, manifest `library_path` as in the SDK). It must:
    - (a) put the layer and manifest where Global Constraints say, with the Linux manifest's `library_path` equal to `../../../lib/libVkLayer_khronos_validation.so`;
    - (b) leave in SOURCE_DIR only the licence file(s) named by LICENSE_FILES;
    - (c) on a second run with the SDK gone, return with "already installed";
    - (d) fail with a clear message when the SDK is gone and an output is missing.
    Strip is skipped when no `strip` program is found (a message says so), so the test runs on any host.
  - Run `cmake -P tests/deps/test_versions.cmake` and the superbuild script test. Expected: FAIL.
- [ ] **Step 3: Implement.**
  - Entries:
    - Linux and Windows: `POST_INSTALL post-install/Vulkan-ValidationLayers.cmake`. Use `LICENSE_FILES` = the LunarG statement the archive holds; if an archive has none, use the first supplement file, kept by the post-install.
    - macOS: `DEPENDS kosmickrisp`, PREBUILT with kosmickrisp's URL and SHA256, `LICENSE_FILES Licenses/LICENSE.txt`, no POST_INSTALL.
    - `dvv_licence_supplement(Vulkan-ValidationLayers ...)` for the texts of Step 1. mimalloc gets `PLATFORMS` limited to the OSs whose binary embeds it.
    - Keep a comment block in the style of the file, saying why: never shipped, U-CI1.
  - The post-install picks the layout by what exists:
    - SOURCE_DIR/`Bin/` means Windows: copy DLL and JSON into `<prefix>/bin`.
    - Otherwise Linux: `*/x86_64/lib/libVkLayer_khronos_validation.so` goes into `<prefix>/lib`, then `strip --strip-debug` it if `find_program(strip)` finds one; the manifest goes into `<prefix>/share/vulkan/explicit_layer.d`, with `library_path` rewritten to `../../../lib/libVkLayer_khronos_validation.so`.
    - Then it replaces SOURCE_DIR's content with the licence file(s) only, as `kosmickrisp.cmake` does.
  - `kosmickrisp.cmake` also copies `macOS/lib/libVkLayer_khronos_validation.dylib` into `<prefix>/lib`, and the manifest into `<prefix>/share/vulkan/explicit_layer.d`, with `library_path` set to `../../../lib/libVkLayer_khronos_validation.dylib`. Add both to its `outputs` list. Keep the dylib whole: no lipo.
  - README dependency list and dvv-ci.yml comments: the layer is LunarG's prebuilt.
- [ ] **Step 4: Run the script tests (expected PASS).** Then run a full local macOS bootstrap into a scratch dvv root (a copy of `libs/` with this dvv-build), `--toolchain appleclang`. Expected:
  - the layer and manifest are in the prefix;
  - `VK_ADD_LAYER_PATH=<prefix>/share/vulkan/explicit_layer.d` plus `vulkaninfo --summary` (with `DYLD_LIBRARY_PATH=<prefix>/lib` and `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`) lists the layer;
  - `THIRD_PARTY_NOTICES-all.txt` has the supplements and no SPIRV entries.
  Run dvv-gpu's tests on that prefix with `DVV_VK_VALIDATION=sync DVV_VK_VALIDATION_REQUIRED=1 DVV_REQUIRE_GPU=1`. Expected: all pass.
- [ ] **Step 5: Commit** `feat: validation layer from the LunarG SDK prebuilt`.

### Task 2: wxWidgets in its own prefix (dvv-build)

**Files:**
- Modify:
  - `cmake/DvvThirdParty.cmake`: a `SEPARATE_PREFIX <suffix>` one-value keyword; `dvv_third_party_separate_prefix_hash(<name> <toolchain> <out>)`; `DVV_SEPARATE_PREFIX_RECIPE_REV` = 1.
  - `deps/CMakeLists.txt`: the separate-prefix branch.
  - `deps/versions.cmake`: the three wx entries get `SEPARATE_PREFIX wx`.
  - `cmake/DvvPresets.json` and `cmake/DvvAppPresets.json`: `CMAKE_PREFIX_PATH` `<prefix>;<prefix>-wx` in all toolchain presets.
  - `CMakeLists.txt`: the self test's `find_file(DVV_WX_CONFIG ...)` and `tests/deps/run-wx-consumer.cmake` callers pass both paths.
  - `bootstrap.sh` and `bootstrap.ps1`.
- Create:
  - `deps/separate-prefix-finish.cmake`: copies LICENSE_FILES into `<sibling>/dvv-licences/` and writes the marker last.
  - `tools/separate-prefix-hash.cmake`: `cmake -DNAME=wxWidgets -DTOOLCHAIN=<tc> -P ...` prints `hash=<64 hex>`. It sets `CMAKE_SYSTEM_NAME` from `CMAKE_HOST_SYSTEM_NAME`, and `DVV_BUNDLE_FFMPEG` ON, before it includes `versions.cmake`.
- Test: `tests/deps/test_superbuild_scripts.cmake`, `tests/cmake/test_presets_consistency.cmake` and the drift test.

**Interfaces:**
- Produces `<root>/_third_party/<tc>-wx` with `dvv-inputs.sha256` (64 hex, no newline) and `dvv-licences/<LICENSE_FILES paths>`.
- Produces `tools/separate-prefix-hash.cmake`, for Task 3's cache key and bootstrap.
- The hash function's text input is one line per key, `<KEY>=<value>`: VERSION, URL, SHA256, LICENSE_FILES, CONFIGS, CMAKE_ARGS, then `RECIPE_REV=<n>` and `TOOLCHAIN=<tc>`. The SHA256 is taken over that text.

- [ ] **Step 1: Tests first.**
  - The hash is stable across two calls.
  - It changes when any of CMAKE_ARGS, VERSION, RECIPE_REV or TOOLCHAIN changes.
  - It does not change when SPDX changes, or when another entry changes.
  - `tools/separate-prefix-hash.cmake` prints the same hash as the function.
  - The finish script copies globbed LICENSE_FILES with their relative paths and writes the marker.
  - The presets test expects the two-element `CMAKE_PREFIX_PATH`.
  - Run them. Expected: FAIL.
- [ ] **Step 2: Implement the superbuild branch.** For an entry with SEPARATE_PREFIX `s`:
  - `sibling = ${prefix}-s`, and the hash is computed with toolchain = the prefix folder's name.
  - If `${sibling}/dvv-inputs.sha256` equals the hash: an `ExternalProject_Add` with an empty download, configure, build and install (as the shared-archive branch), and `SOURCE_DIR ${sibling}/dvv-licences` for the notices.
  - Otherwise, at configure time:
    - `file(REMOVE_RECURSE ${sibling})` and that entry's explicit `STAMP_DIR` (`${CMAKE_BINARY_DIR}/${name}-stamp`);
    - the normal build branch with `INSTALL_DIR ${sibling}`, plus `-DDVV_SEPARATE_PREFIX_HASH=<hash>` appended to its CMAKE_ARGS;
    - an extra install `COMMAND` running `separate-prefix-finish.cmake`;
    - `CMAKE_PREFIX_PATH` stays `${prefix}` for its dependencies.
  - Always: `file(REMOVE_RECURSE ${prefix}/lib/cmake/wxWidgets)`. This is the stale root-level wx of a 0.2.0 prefix. Only for name wxWidgets; write the comment so.
- [ ] **Step 3: bootstrap.** Both scripts:
  - The fast path also needs `<prefix>-wx/dvv-inputs.sha256` to equal `cmake -DNAME=wxWidgets -DTOOLCHAIN=<tc> -P tools/separate-prefix-hash.cmake`.
  - `--force` / `-Force` also removes `<prefix>-wx`.
  - Update the usage comments.
- [ ] **Step 4: Run the script tests (expected PASS).** Then, on the scratch root from Task 1:
  - (a) Bootstrap: wx builds into `appleclang-wx`, and a second bootstrap says "up to date".
  - (b) Delete `appleclang-wx`, then bootstrap: wx rebuilds, nothing else does.
  - (c) Touch a comment in `versions.cmake` outside wx, then bootstrap: wx is skipped (log shows no wx build) and the notices still contain wx's licences.
  - (d) "stale root wx": create `<prefix>/lib/cmake/wxWidgets/wxWidgetsConfig.cmake`, then bootstrap: it is gone.
  - (e) "marker mismatch": write a wrong marker, then bootstrap: wx rebuilds.
  - Build and test dvv-components and rawreel against the scratch root with the presets (`ninja-macos`): all pass.
- [ ] **Step 5: Commit** `feat: wxWidgets in its own prefix with an inputs marker`.

### Task 3: CI job graph and caches (dvv-build `dvv-ci.yml`)

**Files:** Modify `.github/workflows/dvv-ci.yml`; also README's CI section.

**Interfaces:** Consumes `tools/separate-prefix-hash.cmake` (Task 2).

- [ ] **Step 1: Implement.**
  - Replace the single `build` matrix with three prime jobs and three build jobs: `prime-windows`, `prime-linux`, `prime-macos`, and `windows` (msvc, release and asan entries), `linux` (clang, asan, tsan) and `macos` (appleclang, asan). Each build job `needs: [dvv-build-min, prime-<os>]` and runs `fail-fast: false`.
  - A shared key step sequence (the same in prime and build jobs, written once per job):
    - wx hash: `cmake -DNAME=wxWidgets -DTOOLCHAIN=<tc> -P $DVV_BUILD/tools/separate-prefix-hash.cmake`, first 16 hex digits.
    - Compiler fingerprint, a SHA256 cut to 16 hex digits of:
      - Linux: `clang-19 --version`;
      - Windows: `vswhere -latest -property catalog_buildVersion` and the newest `VC/Tools/MSVC` folder name;
      - macOS: `xcodebuild -version` and `xcrun --show-sdk-version`.
    - wx key: `wx-<runner.os>-<tc>-<wxhash>-<fingerprint>`, path `<DVV_ROOT>/_third_party/<tc>-wx`.
    - Prefix key: unchanged formula, path `<DVV_ROOT>/_third_party/<tc>`.
  - Prime job:
    - `actions/cache/restore` both with `lookup-only: true`; if both hit, stop (later steps `if:` false).
    - Else restore both for real, install tools, then bootstrap and save each missed one with `actions/cache/save`.
    - Install only what bootstrap needs: Linux compiler and packages; `brew install ninja` on macOS; the VS check on Windows.
  - Build job:
    - Restore both (no save), then bootstrap (the fast path on a hit; builds on a miss and still works), then the existing configure/build/test steps unchanged.
  - Lint split:
    - `lint` (privacy lint, Format, check-pins) needs only `dvv-build-min`;
    - `tidy` (restore clang caches, bootstrap, build, clang-tidy) needs `prime-linux`.
  - Keep the header comment and step comments accurate.
- [ ] **Step 2: Verify on dvv-build's own CI.**
  - Push branch `ci-speedup` of dvv-build. Its `ci.yml` self layout uses `./.github/workflows/dvv-ci.yml`, so it tests the branch.
  - Run 1 is cold: the caches miss, because the new path keys are new. Run 2 (`gh workflow run ci.yml --ref ci-speedup`) is warm.
  - Expected: both green; run 2's prime jobs finish in under 1 minute; run 2's build jobs show "up to date".
  - Record the job times in `<scratchpad>/timings.md`.
- [ ] **Step 3: Commit** `ci: prime jobs and a wxWidgets cache` (squashed later).

### Task 4: rawreel on the new dvv-build (side branch)

**Files:**
- Modify:
  - `packaging/dry-run.cmake`: the prefix path is the two-element list.
  - `.github/workflows/ci.yml`: `@ci-speedup` for testing only; `@v0.3.0` before the PR.
  - `libs/dvv-build` submodule: to the branch commit.
  - `dvv-deps.txt`: dvv-build 0.3.0 at release.
- grep rawreel for any other single `_third_party/<tc>` prefix use (release.yml, packaging) that resolves wx, and fix those too.

- [ ] **Step 1: Implement** the changes above on rawreel branch `ci-speedup`, based on `feat/macos-0.10.0`.
- [ ] **Step 2: Local.** Run `cmake --workflow --preset ninja-macos` and the macOS dry run (`packaging/dry-run.cmake` as in README). Expected: pass.
- [ ] **Step 3: CI.** Push, then `gh workflow run ci.yml --ref ci-speedup -f test_libs=true`, twice (cold, warm). Then `gh workflow run release.yml --ref ci-speedup -f version=0.10.0 -f dry_run=true` once. Expected:
  - all green except check-pins (untagged pins on a side branch, expected; recorded);
  - Linux GPU tests with sync validation report 0 messages.
  Record the timings.
- [ ] **Step 4: Commit.**

### Task 5: Docs and release preparation (dvv-build)

**Files:**
- `CHANGELOG.md`: `[0.3.0]`.
- `VERSION`: `0.3.0`.
- `README.md`: CI section, the prefix layout, local-upgrade notes (spec §9).
- rawreel spec §6: replace the estimate table with the measured one.

- [ ] **Step 1:** Write them. The changelog states the behaviour difference: LunarG's layer build.
- [ ] **Step 2:** Squash each side branch to one commit on top of its base: dvv-build on `main`, rawreel on `feat/macos-0.10.0`. Run the script tests. Push the branches.
- [ ] **Step 3 (user-gated, not autonomous):** the release order.
  1. dvv-build PR, merge (user), tag v0.3.0.
  2. Each library's PR (dvv-deps.txt `dvv-build 0.3.0`, `dvv-ci.yml@v0.3.0`), in dependency order: merge (user).
  3. rawreel PR after PR #5.
