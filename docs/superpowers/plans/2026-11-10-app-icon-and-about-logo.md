# App Icon and About Logo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:**
- rawreel shows its approved owl icon wherever each OS shows an app icon:
  - **macOS:** the Finder, the Dock and the app switcher.
  - **Windows:** Explorer (both exes), the title bar, the taskbar and Alt+Tab.
  - **Linux:** the window icon, which the dock, Alt+Tab and the overview show.
- Help → About shows the rawreel logo lockup (the icon and the "rawreel" wordmark) as a banner at the top.
  - It uses the light or the dark lockup to match the app's appearance, and switches when the appearance changes while About is open.
  - It is sharp at every display scale, on all three OSes.

**Architecture:**
- `packaging/icons/` holds:
  - the art inputs;
  - two dev-time Swift scripts (macOS only);
  - the SVG masters the scripts write;
  - a `sources.sha256` of those inputs;
  - the committed rendered files: `rawreel.icns`, `rawreel.ico`, the Linux window-icon PNGs, and the lockup PNGs at 1×, 2× and 3×.
- CMake only consumes the committed files:
  - The `.icns` goes into the bundle's `Resources`, named by `CFBundleIconFile`.
  - The `.ico` is the first icon group of both Windows `.rc` files.
  - The PNGs are embedded into `rawreel-gui` by a build-time CMake script.
- A new `src/gui/app_icon.{h,cpp}` provides:
  - the frame's window icons (`wxFrame::SetIcons`);
  - About's lockup as a `wxBitmapBundle`, light or dark.
- `about.cpp` adds the banner above the dialog's existing contents. It removes the frame icon that wx would otherwise put left of the text, and refreshes the banner on `wxEVT_SYS_COLOUR_CHANGED`.

**Tech Stack:**
- C++23.
- wxWidgets 3.3: `wxIconBundle`, `wxBitmapBundle` and a custom `wxBitmapBundleImpl`, `wxGenericAboutDialog`, `wxSystemSettings::GetAppearance`, `wxApp::SetAppearance`.
- CMake 4.2, including `-P` scripts.
- Swift with AppKit and `iconutil` (dev-time, macOS only).
- doctest.
- The selftest runner is unchanged.

**Spec:** this document's Decisions section.

**Base:** `main` after PR #12 (`feat/log-pane-settings`) merges. New branch: `feat/app-icon`. The code was read as it stands on `feat/log-pane-settings`; that branch does not touch the icon, About or packaging files named below.

---

## Decisions

The user decided D2, D3, D5 and D6 on 2026-11-10; D6 is the user's own lockup-banner design. The user also decided on 2026-11-10:
- **(a)** Windows and Linux use a new unplated small variant at 16–32 px; macOS keeps the plated small one.
- **(b)** The 20 and 24 px images may be anti-aliased.
- **(c)** A contact sheet of every ×2 variant goes to the user for OK before any icon file is committed (Task 1, gate).
- **(d)** A mid-size unplated variant without the play and wrench keys, `icon-mid-unplated-64`, is used for Windows 40, 48 and 64 px and for Linux 48 and 64 px. The small unplated variant covers 16–32 px, and the full unplated icon covers 96 px and up.
- **(e)** The About banner is 320×100 DIP with a bigger owl tile, chosen per OS:
  - macOS: the plated-tile lockups `logo-lockup-macos-{light,dark}-320x100-x2.svg`;
  - Windows and Linux: `logo-lockup-unplated-{light,dark}-320x100-x2.svg`.
  - They are rendered at 1×, 2× and 3× (320×100, 640×200, 960×300).
- **Where the new art comes from.** The designer is making the files for (d) and (e) in the approved `owl-v4-final/` folder.
  - Task 1 ports the designer's generator code for them into `gen-svg.swift`.
  - Task 1 checks that the regenerated SVGs have the same fills as the designer's files.
  - In the repository, the `-x2` suffix and the size suffix are dropped from the file names, because `CHROMA_GAIN` (D2) already sets the gain.

### D1. The source of truth: art, generators, SVG masters, a source hash and the rendered binaries are committed; CMake only consumes them

**Layout:**
- `packaging/icons/art/`: `owl.svgfrag`, `owl.palette` and `owl.small16.txt`. These are the approved owl-v4-final inputs, renamed without the version suffix.
- `packaging/icons/tools/gen-svg.swift`: the approved `gen4f.swift`, reduced to the deliverables. It drops the plane sheets and the value table, and adds the unplated small variant.
- `packaging/icons/tools/render-icons.swift`:
  - rasterises with AppKit;
  - writes the `.iconset` for `iconutil`;
  - writes the ICO;
  - writes the review contact sheet, outside the repository;
  - writes `sources.sha256`.
- `packaging/icons/svg/`, ten masters:
  - `icon-1024.svg` (plated);
  - `icon-unplated-1024.svg`;
  - `icon-mid-unplated-64.svg` (no keys; decision (d));
  - `icon-small-32.svg` (plated, for macOS);
  - `icon-small-unplated-32.svg` (Windows and Linux; decision (a));
  - `mark.svg`;
  - `logo-lockup-macos-light.svg` and `logo-lockup-macos-dark.svg` (320×100, plated tile);
  - `logo-lockup-unplated-light.svg` and `logo-lockup-unplated-dark.svg` (320×100; decision (e)).
- `packaging/icons/sources.sha256`: one `<sha256>  <path>` line for each file of `art/` and `svg/` and for both scripts, sorted by path.
- `packaging/icons/rawreel.icns` and `packaging/icons/rawreel.ico`.
- `packaging/icons/png/linux/rawreel-{16,24,32,48,64,128,256}.png`.
- `packaging/icons/png/logo/`:
  - `logo-macos-{light,dark}-{320x100,640x200}.png`;
  - `logo-unplated-{light,dark}-{320x100,640x200,960x300}.png`.
  - In `light`, the wordmark is dark (`#141821`), for light backgrounds.
  - In `dark`, the wordmark is light (`#EEF1F6`), for dark backgrounds.

**Why the rendered binaries are committed:**
- Windows and Linux CI cannot run AppKit.
- The rules exclude python and ImageMagick.
- `rsvg-convert` is not on the Windows runner.
- wx's SVG rasteriser cannot draw the art. nanosvg has no `clipPath`, and the Y/U/V panels and the lens use six clip paths, so `wxBitmapBundle::FromSVG` is out.
- Committed outputs can be reviewed in Quick Look, are identical on every OS and cost no build time. They total about 400 KB.

**Regeneration:**
- A person runs it on a Mac after an art change, then commits:
  1. `swift packaging/icons/tools/gen-svg.swift packaging/icons`
  2. `swift packaging/icons/tools/render-icons.swift packaging/icons`
- CMake never regenerates.
- `rawreel.icons` (D8) runs on every OS and checks:
  - the structure of every committed binary;
  - that `sources.sha256` still matches the art, the SVGs and the scripts (`file(SHA256)`). An edit without a render therefore fails CI.

**Licence:** the artwork is rawreel's own, covered by the repository's MIT `LICENSE`. There is no new third-party notice.

**Git:** `.gitattributes` gains `*.icns binary`. `*.png` and `*.ico` are already binary.

### D2. Chroma gain ×2 for the whole family — confirmed by the user (2026-11-10)

- The approved set has ×2 only for the plated master (`icon-1024-gain2.svg`).
- `gen-svg.swift` gets one constant, `CHROMA_GAIN = 2`, applied to every output:
  - The full-size plates use the `ub2`/`vb2` variants.
  - Both small variants multiply their 4:2:0 block averages by the gain and clip to ±0.5.
  - The mid variant and all four lockups use ×2 too, as the designer's `-x2` files do.
- To switch to ×1: set the constant to 1, regenerate, and repeat the contact-sheet gate.

### D3. macOS: a classic `.icns` named by `CFBundleIconFile`; Liquid Glass `.icon` later — confirmed by the user (2026-11-10)

**What ships now:**
- `iconutil -c icns` builds `rawreel.icns` from an `.iconset`:
  - 16 and 32 px from the plated `icon-small-32.svg` (the 16 and 32 slots, and 16@2x);
  - 64 to 1024 px from the plated `icon-1024.svg`, which is on Apple's grid (824 px body, 100 px inset, about 185 px corner radius).
- `Info.plist.in` gets `CFBundleIconFile = rawreel`.
- The build tree's `rawreel-gui.app` gets the file through `MACOSX_PACKAGE_LOCATION Resources`.
- The `.dmg`'s `rawreel.app` gets it through an `install(FILES …)` next to the existing `Info.plist` rule, plus a manifest line.

**Why not Icon Composer now:**
- The macOS 26+ "Liquid Glass" icon is an Icon Composer `.icon` document, which `actool` compiles into `Assets.car` (named by `CFBundleIconName`).
- The `.icon` must be authored in the Icon Composer GUI. Its JSON format is undocumented.
- The `actool` flags for a build without an Xcode project are known only from community posts: `--compile <dir> --app-icon <name> --include-all-app-icons --platform macosx --minimum-deployment-target 26.0 --output-partial-info-plist <file>`.
- Since Xcode 26.1, a separate `.icns` no longer overrides the asset-catalog icon, so taking that path replaces this one.

**Grey-plate risk and follow-up:**
- macOS 26+ puts a legacy icon on a grey plate when "pixels protrude" from its mask. Our plate follows Apple's grid, so it is expected to fit.
- The manual checklist (Task 7) checks the Dock and the Finder on macOS 27 in the Default, Dark, Clear and Tinted styles.
- If the plate shows, a follow-up adds:
  - `packaging/icons/rawreel.icon`, using the layers `gen-svg.swift` already separates;
  - an `actool` step;
  - `CFBundleIconName`.

No `.dmg` volume icon is added.

### D4. Windows: one ICO, the first icon group of `rawreel-gui.exe` and `rawreel-cli.exe`; window icons from that resource

**Sizes:** 16, 20, 24, 32, 40, 48, 64, 96 and 256.
- 16 to 32 come from the unplated small variant, `icon-small-unplated-32.svg` (decision (a)).
- 40, 48 and 64 come from the mid variant without keys, `icon-mid-unplated-64.svg` (decision (d)).
- 96 and 256 come from `icon-unplated-1024.svg`.
- The 20 and 24 px images come from the 32 px design and are anti-aliased; the user accepts this (decision (b)).

**Format:**
- 256 is PNG-compressed. The others are 32-bit BGRA DIBs with an AND mask, the layout Visual Studio writes.
- Explorer, `LoadImage` and `CreateIconFromResourceEx` read both.
- All multi-byte ICO and DIB fields are little-endian.

**`rawreel-gui.rc`:** `rawreel ICON "rawreel.ico"` comes before `#include "wx/msw/wx.rc"`.
- Explorer uses the exe's first `RT_GROUP_ICON`. Named groups sort by their upper-case name, and `RAWREEL` comes before wx.rc's `WXICON_AAA` (wx.rc's own comment states the rule).
- CMake passes the icon folder to the resource compiler only: `target_compile_options(<t> PRIVATE $<$<COMPILE_LANGUAGE:RC>:/I${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons>)`.
  - This is the default because `rc.exe` searches `/I` for `ICON` files.
  - The fallback, if the generator drops the option, is `target_include_directories`, which adds the folder to the C++ compiles too.
- The `.ico` is the `.rc` source's `OBJECT_DEPENDS`, so a changed icon rebuilds the `.res`.

**`rawreel-cli.exe`:** gets the same group from a new `src/cli/rawreel-cli.rc`. It builds under every Windows preset, ASan included.

**Window icons:** `SetIcons(wxIconBundle("rawreel", nullptr))`.
- wx loads every image of the group.
- `DoSelectAndSetIcon` picks the sizes for the window's DPI, and picks again on a DPI change (`MSWUpdateFontOnDPIChange` → `DoSetIcons`).
- Before any icon is loaded, `app_icon.cpp` stops the build with `#error` if `wxUSE_ICO_CUR` is 0.

**No MSIX assets:** rawreel ships a zip.

### D5. Linux: window icons only, unplated; no `.desktop`, AppStream or hicolor files in the tar.xz — confirmed by the user (2026-11-10)

- `wxFrame::SetIcons` with a 16/24/32/48/64/128/256 bundle calls `gtk_window_set_icon_list`, which sets `_NET_WM_ICON`.
  - GNOME Shell, KDE and WSLg show that icon for an X11 or XWayland window that matches no `.desktop` file.
  - 16 to 32 px come from the unplated small variant, 48 and 64 px from the mid variant (decision (d)), and 128 and 256 px from the unplated icon.
- A later `.deb` or Flatpak would use:
  - the scalable `icon-unplated-1024.svg` and the PNGs;
  - the app id `io.github.barabashkad.rawreel`;
  - `StartupWMClass=Rawreel-gui`, because wxGTK's `WM_CLASS` class is `GetAppDisplayName()`, the capitalised exe name on Linux.

### D6. About: a lockup banner at the top, light or dark by the app's appearance, on all three OSes — decided by the user (2026-11-10)

**The dialog stays the generic one.** The native macOS panel cannot hold:
- the "Third-party notices" and "Copy to clipboard" buttons;
- the links;
- the Components pane;
- the selftest hook (design D23/D29).

**The banner** is a `wxStaticBitmap` showing the lockup at **320×100 DIP** (decision (e)), centred above the existing icon-and-text row. It sits inside the contents panel, so it shares the panel's background.

**The lockup per OS:**
- macOS uses the plated-tile lockup (`logo-macos-*`).
- Windows and Linux use the unplated lockup (`logo-unplated-*`).

**Bitmaps per scale:**
- **macOS:** `wxBitmapBundle::FromBitmaps` of the 320×100 and 640×200 PNGs. macOS only uses the exact scales 1 and 2.
- **Windows and GTK:** a custom `LogoBundleImpl : wxBitmapBundleImpl` over the 320×100, 640×200 and 960×300 PNGs.
  - `GetDefaultSize()` is 320×100.
  - `GetPreferredBitmapSizeAtScale(s)` is the default size × s, rounded.
  - `GetBitmap(size)` takes the smallest of the three entries that is at least `size`, or the largest entry if none is. It rescales it with `wxIMAGE_QUALITY_HIGH` when the sizes differ, and caches the result per size.
  - Why not `FromBitmaps`: its preferred size at 125, 150 and 175 % snaps to an existing entry, so the banner would be the wrong logical size there.
  - `wxBitmapBundle::GetBitmap` sets the scale factor, so the logical size is right on GTK HiDPI.

**Building the dialog.** `wxGenericAboutDialog::Create` gives `m_contents` (`GetCustomControlParent()`) a horizontal row sizer, `[spacer, (icon, spacer,) text column, spacer]`. In our constructor, after `Create()` and before `addButtons()` (which calls `SetSizeHints`):

1. **`removeStockIcon()`.** `wxAboutDialogInfo::GetIcon` falls back to the main frame's icons, which exist on Windows and Linux after Task 4. wx then puts a 16 px icon left of the text.
   - Find index `i` of the row item whose window is a `wxStaticBitmap`. If there is none, do nothing; this is the case on macOS, where the frame has no icons.
   - Destroy the item at `i` and its window. Then remove the spacer now at `i`, which was `i + 1`.
   - Set the last item's spacer to `wxSize(2 * wxSizerFlags::GetDefaultBorder(), 0)`. The result is exactly wx's layout without an icon.
   - `info.SetIcon` is not called.
2. **`addBanner()`.**
   - Give the panel a vertical sizer with `SetSizer(column, false)`, so the old row sizer is kept.
   - Add the banner with `CentreHorizontal().DoubleBorder(wxTOP)`, then the row with `wxSizerFlags(1).Expand()`.
   - Call `showLogo()`.

**The layout rules stay:**
- `fitToPane`, `addButtons` and `SetSizeHints` work through the dialog's sizer as before.
- The banner must not set the dialog's width. Task 5 Step 1 measures today's width first.
- The heading "rawreel <version>" stays under the banner. The about selftest checks it once in the copied text.

**Light or dark: `showLogo()`.**
- It uses `const bool dark = wxSystemSettings::GetAppearance().IsDark()`. In wx 3.3 this follows the app's own appearance, so it honours `--appearance`:
  - **macOS:** `NSApp.effectiveAppearance`.
  - **Windows:** `wxMSWDarkMode::IsActive()`, then the window colours.
  - **GTK:** whether the window background is dark.
- It sets the banner's bundle and its name, `about-logo-dark` or `about-logo-light`. The selftest reads the name.

**Live switching:**
- The dialog binds `wxEVT_SYS_COLOUR_CHANGED`. The handler:
  - calls `event.Skip()`;
  - sets the contents panel's background again to `wxSYS_COLOUR_LISTBOX` (the colour `Create` used);
  - sets the same colour on the line-row panels, which `lineRow` now records in a member vector when it creates them;
  - calls `showLogo()`;
  - calls `Refresh()`.
- The two lockups are the same size, so nothing is laid out again.
- wx's 1-px separator above the buttons keeps its colour until the next opening. This is accepted.
- wx sends the event:
  - **macOS:** per window, on an `effectiveAppearance` change;
  - **Windows:** on `WM_SYSCOLORCHANGE`. A real theme change reaches every top-level window. After `wxApp::SetAppearance`, wx 3.3.4 sends it to the top window (the main frame) only, and `wxEVT_SYS_COLOUR_CHANGED` does not propagate to other top-level windows;
  - **GTK:** on `notify::gtk-theme-name`.
  - A GTK colour-scheme switch that keeps the theme name may send no event. The banner then follows at the next opening; the manual checklist covers this.
- No timers and no polling.

### D7. Embedding the PNGs: a build-time CMake script, the PNG handler, and no empty table

**`packaging/icons/embed-png.cmake` (`-P`):**
- It reads each PNG as hex and writes `app_icon_data.cpp`:
  - one `constexpr unsigned char` array per file, named in `UPPER_CASE` after the file stem (`LOGO_DARK_480X160`), at about 32 bytes per line;
  - a table of `{key, bytes}`, where `key` is the file stem.
- It writes on every run. The `add_custom_command` reruns only when a listed PNG or the script changes.
- MSVC has no `#embed`, so this script is how the bytes get into the binary.

**What is embedded (from Task 4 on):**
- **The lockups, every OS:**
  - macOS: the four `logo-macos-*` PNGs.
  - Windows and Linux: the six `logo-unplated-*` PNGs.
  - The table is therefore never empty, and the generated file builds under MSVC `/W4 /WX` and clang `-Werror` from Task 4 on.
- **The window icons, Linux only:** the seven window-icon PNGs. Windows uses the resource; macOS uses the bundle.

**Decoding:**
- wxOSX decodes PNG natively.
- On wxMSW and wxGTK, `NewFromPNGData` goes through `wxImage`, so `app_icon.cpp` adds `wxPNGHandler` once, if `wxImage::FindHandler(wxBITMAP_TYPE_PNG)` is null.
- `#if !wxUSE_LIBPNG` → `#error`.

**Failures:**
- Every icon and PNG load runs under `wxLogNull`, so wx shows no error dialog.
- A failure gives one Warn line through `DVV_LOG_TEXT`, never on a refresh path, and an empty bundle.
- `SetIcons` is called only for a non-empty bundle, because wx asserts when icons are unset.

### D8. Tests

**`rawreel.icons` (label `unit`; automated on every OS, no display).** `tests/packaging/test_icons.cmake` checks:

- **ICO:**
  - reserved 0, type 1, count 9 (little-endian);
  - the sizes {16,20,24,32,40,48,64,96,256}, every one at 32 bpp;
  - each entry's offset + size lies inside the file;
  - the 256 entry is a PNG whose IHDR is 256×256;
  - every other entry is a 40-byte BITMAPINFOHEADER with height twice its width.
- **PNGs:**
  - each has the signature, the IHDR size of its name and colour type 6;
  - the file sets are exact.
- **icns:**
  - the magic, and a big-endian length equal to the file size;
  - the chunks, walked one by one, are exactly {`ic12`, `ic07`, `ic13`, `ic08`, `ic04`, `ic14`, `ic09`, `ic05`, `ic10`, `ic11`, `info`}, the set `iconutil` writes;
  - `ic04` and `ic05` start with `ARGB`; the others start with the PNG signature.
- **Sources:** `sources.sha256` equals the `file(SHA256)` of the files it lists, and lists exactly `art/*`, `svg/*` and both scripts.
- **References:**
  - `Info.plist.in` has `CFBundleIconFile`/`rawreel`;
  - both `.rc` files declare `rawreel ICON "rawreel.ico"`, before the `wx.rc` include in `rawreel-gui.rc`;
  - `manifest-macos.txt` lists the `.icns`.
- **Privacy:** `rawreel_scan_privacy(FILE <f> MODE full BINARY FINDINGS …)` (`packaging/scan-artifact-privacy.cmake`) finds nothing in any committed binary: the `.icns`, the `.ico` and every PNG.

**`rawreel.cli_unit`, Windows only.** `tests/test_exe_icons.cpp` in `rawreel_cli_tests` opens `RAWREEL_GUI_EXE` and `RAWREEL_CLI_EXE` (environment, set from `$<TARGET_FILE:…>`) with `LoadLibraryExW(…, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)` and checks:
- the first `RT_GROUP_ICON` name, through `EnumResourceNamesW`, is `RAWREEL`;
- that group's `GRPICONDIR::idCount` is 9.

A variable that is unset (no `rawreel_gui` under the sanitizer presets) skips that exe with a doctest `MESSAGE`.

**Smoke selftest (CTest, label `gui`), new step "window icons":**
- **Windows:** the frame's icon widths are the nine ICO sizes.
- **Linux:** they are the seven sizes.
- **macOS:**
  - the frame has no icons;
  - `CFBundleGetValueForInfoDictionaryKey(CFBundleGetMainBundle(), CFSTR("CFBundleIconFile"))` is `rawreel`;
  - `<Resources>/rawreel.icns` exists.

**`--selftest about` (opt-in, not in CTest; run because About changes):**
- It runs three times: with no `--appearance`, with `--appearance light` and with `--appearance dark`.
- **The `aboutStep` hook checks:**
  - exactly one `wxStaticBitmap` (so the stock icon was removed);
  - its `GetBitmap().GetLogicalSize()` equals `dialog.FromDIP(wxSize(320, 100))`;
  - the variant (its name) matches `IsDark()`.
- **A new `appearanceStep` in `aboutScenario`:** when `App::appearanceResult()` is `Ok` and `--appearance` is `light` or `dark`, the variant must match the requested appearance. This is not circular. With `system`, it matches `IsDark()`.
- **A real switch in the hook,** where `wxTheApp->SetAppearance(opposite)` returns `Ok` (macOS, Windows, GTK3; wx 3.3.4 returns `Ok` or `Failure`, never `CannotChange`):
  - on Windows the hook first sends `WM_SYSCOLORCHANGE` to the dialog, as a real theme change would (after the switch and after the restore);
  - in a later tick, before the hook's clicks, the banner's variant has flipped;
  - then the original appearance is restored, and the variant must be back.
- **Where `SetAppearance` does not return `Ok` (`Failure`: a Windows without dark-mode support, a GTK without the settings portal):**
  - the hook clears the banner's name and sends a synthetic `wxSysColourChangedEvent` to the dialog;
  - the name must come back, which shows that the handler is wired.
- An unchanged dialog size after a synthetic event is recorded as a regression guard, not as evidence.
- Every existing About check still passes.

**macOS package dry run:** `manifest-macos.txt` requires `rawreel.app/Contents/Resources/rawreel.icns`.

**Manual (Task 7, mandatory):**
- the Retina and 1× look of the icon and of the banner;
- the Finder, Dock, Explorer, taskbar, GNOME and WSLg;
- no grey plate on macOS 27;
- the live banner switch.
- Icon caches can show stale icons:
  - macOS: touch the app and `lsregister -f` it;
  - Windows: refresh the Explorer icon cache, e.g. `ie4uinit -show`, or use a new folder.

### D9. README and release notes: facts only

**README:**
- **The macOS paragraph:** `Contents/Resources/` also has `rawreel.icns`, the app icon.
- **The Packages section:** `rawreel-gui.exe` and `rawreel-cli.exe` carry the rawreel icon.
- **The `about` selftest bullet (about line 325):** "Help → About, its layout, logo and links".
- **The rawreel-gui section:** Help → About shows the rawreel logo, light or dark to match the appearance.

**Release notes:** there is no next-version notes file yet. Task 6 adds the bullet if `docs/release-notes-<next>.md` exists; otherwise the PR description records it. The bullet:

> rawreel has an app icon: the `.app` (Finder, Dock), both Windows exes (Explorer, taskbar) and the window on Linux; Help → About shows the rawreel logo, light or dark to match the appearance.

### Sources

- **Apple:**
  - [HIG: App icons](https://developer.apple.com/design/human-interface-guidelines/app-icons)
  - [Icon Composer](https://developer.apple.com/icon-composer/)
  - [Creating your app icon using Icon Composer](https://developer.apple.com/documentation/xcode/creating-your-app-icon-using-icon-composer)
- **Community and press (not Apple documentation):**
  - The `actool` command line without an Xcode project: [Successful Software, Sep 2025](https://successfulsoftware.net/2025/09/26/updating-application-icons-for-macos-26-tahoe-and-liquid-glass/).
  - Xcode 26.1 dropped the separate-`.icns` trick: [Michael Tsai, Aug 2025](https://mjtsai.com/blog/2025/08/08/separate-icons-for-macos-tahoe-vs-earlier/).
  - The grey plate: [heise, "Icons in macOS 26: Fighting the Squircle Prison"](https://heise.de/-11075561).
- **Microsoft:** [App icon construction](https://learn.microsoft.com/en-us/windows/apps/design/iconography/app-icon-construction).
- **freedesktop and GNOME:**
  - [Icon Theme Spec](https://specifications.freedesktop.org/icon-theme-spec/latest/)
  - [GNOME HIG app icons](https://developer.gnome.org/hig/guidelines/app-icons.html)
- **wxWidgets 3.3.4 sources (third-party prefix):**
  - `src/generic/aboutdlgg.cpp`: `Create`, with the `m_contents` panel, its `wxSYS_COLOUR_LISTBOX` background and the row; `GetIcon` and its frame-icon fallback.
  - `src/common/settcmn.cpp`, `src/msw/settings.cpp`, `src/osx/cocoa/settings.mm`: `IsDark`.
  - `src/osx/cocoa/nonownedwnd.mm`: the `effectiveAppearance` observer.
  - `src/gtk/toplevel.cpp`: `notify::gtk-theme-name`, `gtk_window_set_icon_list` and `gdk_set_program_class`.
  - `src/msw/window.cpp`: `wxSysColourChangedEvent`.
  - `src/msw/toplevel.cpp`: `DoSelectAndSetIcon` and `DoSetIcons` on a DPI change.
  - `src/common/iconbndl.cpp`: `AddIcon(resourceName, module)`.
  - `include/wx/msw/wx.rc`: the `wxICON_AAA` ordering comment.
  - `src/common/bmpbndl.cpp`: sorted entries, the first giving the default size; `GetBitmap` sets the scale factor.
  - `3rdparty/nanosvg`: no `clipPath`.
- **The approved art:** owl-v4-final, with `research.md` §2 for the sizes.

---

## Global Constraints

- **Native per OS.**
  - Each OS has its own icon form:
    - **macOS:** the plated `.icns`, with the plated small icon;
    - **Windows:** the unplated ICO, with the unplated small icon;
    - **Linux:** unplated window icons.
  - About's banner uses the plated-tile lockup on macOS and the unplated lockup on Windows and Linux (decision (e)).
- **No timers or polling.**
  - The banner follows the appearance through `wxEVT_SYS_COLOUR_CHANGED` only.
  - The selftest checks use the existing runner's `passed()` callbacks, the About hook and its existing later-tick mechanism.
  - `src/core` stays wx-free.
- **Selftests.**
  - They never show native dialogs.
  - They never change system settings: the app's own appearance (`wxApp::SetAppearance`) is switched and restored, and the system setting is not.
  - They never touch the user's files.
  - The About check runs only through `--selftest about`.
- **Build.**
  - MSVC `/W4 /WX`, which now applies to the generated `app_icon_data.cpp` as well.
  - clang-format 19.1.7 on the hand-written sources.
  - clang-tidy naming: `camelBack` functions, `UPPER_CASE` constants and arrays.
- **Committed files.**
  - No machine paths: no home-folder paths of macOS, Linux or Windows, and no temp-folder paths. This covers the scripts, comments, SVG `<title>` texts and `sources.sha256`, which holds relative paths only.
  - No real names and no e-mail addresses.
  - After `git add`, run `cmake -DREPO=. -P libs/dvv-build/tools/privacy-lint.cmake` (tree mode reads the tracked files).
- **Commits and PR.**
  - Identity BarabashkaD with the GitHub noreply address, and a `Co-Authored-By` trailer.
  - The task commits are squashed into one for the PR.
  - Push and open the PR only after the user says so. The user merges.
- **Tests per task.** Run only the task's tests. The full suite runs serially (`-j1`) at the end; GUI tests never run in parallel.
- **Commands (macOS dev machine).**
  - Configure once with `cmake --preset ninja-macos`.
  - Build with `cmake --build --preset ninja-macos-release --target <t>`.
  - Test with `ctest --preset ninja-macos-release -R <regex> --output-on-failure`.
  - The GUI binary is `libs/_build/rawreel/ninja-macos/Release/rawreel-gui.app/Contents/MacOS/rawreel-gui`.
  - On Windows use `vs2026-release`, and on Linux `ninja-clang-release`.

## Review Focus

1. **Explorer and the taskbar use the rawreel icon, not wx's `std.ico`.** Pinned by `test_exe_icons.cpp` (both exes: first group `RAWREEL`, 9 images) and by the manual Explorer check.
2. **About shows exactly one picture: the banner at 320×100 DIP at every scale (100, 125, 150 and 175 % on Windows; GTK HiDPI; Retina), in the variant of the requested appearance.** Pinned by `--selftest about` under three `--appearance` values, by `appearanceStep`, and by the 125 % and 150 % Windows runs.
3. **A real appearance change while About is open swaps the banner and recolours the panel.** Pinned by the hook's `SetAppearance` switch on macOS and GTK3, and by the synthetic event on Windows (wiring only). The live system switch is a manual check.
4. **The packaged `.app` has the icon.** Pinned by the manifest line and the macOS dry run.
5. **The committed binaries match their names and their sources, and hold no private strings.** Pinned by `rawreel.icons` (structure, `sources.sha256`, binary privacy scan) on all three CI OSes. The generated `.cpp` is clean under `/W4 /WX` and `-Werror` from Task 4 on.

---

## Task 1: Icon sources, generators, contact-sheet gate, rendered files and their check

**Files:**
- New:
  - `packaging/icons/art/{owl.svgfrag,owl.palette,owl.small16.txt}`
  - `packaging/icons/tools/gen-svg.swift`
  - `packaging/icons/tools/render-icons.swift`
  - `packaging/icons/svg/*.svg` (10)
  - `packaging/icons/sources.sha256`
  - `packaging/icons/rawreel.icns`
  - `packaging/icons/rawreel.ico`
  - `packaging/icons/png/linux/*.png` (7)
  - `packaging/icons/png/logo/*.png` (10)
  - `tests/packaging/test_icons.cmake`
- Changed:
  - `tests/CMakeLists.txt`
  - `.gitattributes`

**Interfaces (produces):**
- **`gen-svg.swift <icons dir>`:**
  - It reads `art/` and writes `svg/`, with `CHROMA_GAIN` (D2) applied to every variant.
  - New: `icon-small-unplated-32.svg`. It is the same 12×7 Y map and 5×4 chroma blocks as the plated small icon, but:
    - it has no dark rounded plate and no rim;
    - the three panels carry the unplated icon's `#161C29` keyline, 1 px at 16 px;
    - the panels are enlarged to fill the 32 grid with a 1-unit margin.
  - The `<title>` texts say "rawreel …" and carry no version or path.
- **`render-icons.swift <icons dir> [<sheet.png>]`:**
  - It renders with `NSImage(contentsOf:)` into sRGB-tagged `NSBitmapImageRep`s, as `render4f.swift` does.
  - It writes the PNG sets. The macOS lockups are rendered at 320×100 and 640×200, and the unplated lockups at 320×100, 640×200 and 960×300.
  - It writes the `.iconset` under `FileManager.default.temporaryDirectory`, runs `/usr/bin/iconutil -c icns`, and deletes the folder.
  - It writes the ICO. Each DIB entry is:
    - un-premultiplied;
    - BGRA, bottom-up;
    - `biHeight` = 2·h;
    - followed by an AND mask with rows padded to 32 bits, set where alpha is 0.
    - The 256 entry has width and height bytes of 0.
  - It writes `sources.sha256`.
  - With `<sheet.png>`, which is always outside the repository, it also writes the contact sheet. Each variant appears on light and on dark backgrounds:
    - the plated icon at 1024, 512, 256, 128, 64, 32 and 16;
    - the plated small icon at 32, at 16, and at 16 ×8 nearest-neighbour;
    - the unplated icon at 256, 128, 64, 48 and 40;
    - the unplated small icon at 32, 24, 20 and 16, and at 16 ×8;
    - the mid variant (no keys) at 64, 48 and 40;
    - the four lockups at each of their scales;
    - the 9 ICO images and the 10 `.icns` images, read back from the written files.
- **`tests/packaging/test_icons.cmake`:**
  - It takes `-DICONS_DIR=<dir> -DREPO_DIR=<dir> -DWORK_DIR=<dir>`.
  - It prints `PASS <case>` for each case and stops with `FAIL <case>: <why>`.
  - It reads binary fields with `file(READ … OFFSET … LIMIT … HEX)` and `math(EXPR … "0x…")`.
  - ICO and DIB fields are little-endian; icns and PNG fields are big-endian.
  - It includes `packaging/scan-artifact-privacy.cmake`.

- [ ] **Step 1: Write the check.**
  - The cases are all of D8's `rawreel.icons` list except `references`, which Tasks 2 and 3 add.
  - The `icns` case walks the chunks from offset 8, by their lengths, to the end of the file. It pins the exact set and the `ARGB`/PNG payload starts.
- [ ] **Step 2: Register it** next to `rawreel.packaging_scripts`:
  ```cmake
  add_test(NAME rawreel.icons
           COMMAND ${CMAKE_COMMAND} -DICONS_DIR=${PROJECT_SOURCE_DIR}/packaging/icons
                   -DREPO_DIR=${PROJECT_SOURCE_DIR}
                   -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/icons-test
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/packaging/test_icons.cmake)
  set_tests_properties(rawreel.icons PROPERTIES LABELS unit)
  ```
  Add `*.icns binary` to `.gitattributes`, next to `*.ico binary`.
- [ ] **Step 3: Run it; see it fail.**
  - Run `cmake --preset ninja-macos && ctest --preset ninja-macos-release -R '^rawreel\.icons$' --output-on-failure`.
  - Expected: `FAIL ico directory: … rawreel.ico not found`.
- [ ] **Step 4: Add the art and the generators; run them** with the sheet:
  1. Copy the three `owl-v4-final.*` art inputs, renamed.
  2. Port `gen4f.swift` to `gen-svg.swift`:
     - keep the BT.709 conversion, the plate, the clips, the keys, the unplated keyline, the lockups with the wordmark, the mark, and the small map;
     - drop the plane sheets and `yuv-values.txt`;
     - add `CHROMA_GAIN`, the unplated small variant, the mid variant and the four 320×100 lockups. Port the last two from the designer's generator code for `icon-mid-unplated-64-x2.svg` and `logo-lockup-{macos,unplated}-{light,dark}-320x100-x2.svg` in `owl-v4-final/`.
  - Each regenerated mid and lockup SVG must have the same fills as the designer's `-x2` file (diff all but the `<title>`).
  3. Write `render-icons.swift`.
  4. Run `swift packaging/icons/tools/gen-svg.swift packaging/icons`.
  5. Run `swift packaging/icons/tools/render-icons.swift packaging/icons "<scratch>/rawreel-icons-sheet.png"`, where `<scratch>` is any folder outside the repository.
  - Expected:
    - every output of D1 is written;
    - with `CHROMA_GAIN = 2`, the fills of `svg/icon-1024.svg` equal those of the approved `icon-1024-gain2.svg` (diff all but the `<title>`).
- [ ] **Step 5: GATE. Stop for the user's OK (decision (c)).**
  - Send the user the contact sheet path. Nothing under `packaging/icons/` is staged or committed before the user says OK.
  - On requested changes: change the art or the script, regenerate, and send a new sheet.
  - On a gain change to ×1: set `CHROMA_GAIN = 1` and repeat this step.
- [ ] **Step 6: Run** `ctest --preset ninja-macos-release -R '^rawreel\.icons$' --output-on-failure`. Expected: every case prints `PASS`; the test passes.
- [ ] **Step 7: Determinism.**
  - Run both scripts again, without the sheet, then `git status --porcelain packaging/icons`.
  - Expected: no change against the files from Step 4.
  - If AppKit's PNG bytes vary between runs, fix the encoding: no time chunk, fixed compression.
- [ ] **Step 8: Privacy.** Run `git add` on the new files, then `cmake -DREPO=. -P libs/dvv-build/tools/privacy-lint.cmake`. Expected: no findings. The binaries are also scanned by `rawreel.icons`.
- [ ] **Step 9: Commit** `feat(icons): rawreel app icon sources, generators and rendered icon sets`.

## Task 2: macOS bundle icon (build tree and `.dmg`)

**Files:**
- `packaging/Info.plist.in`
- `CMakeLists.txt` (the `elseif(APPLE)` block of `rawreel_gui`)
- `packaging/packaging.cmake` (the macOS install block)
- `packaging/manifest-macos.txt`
- `tests/packaging/test_icons.cmake`

**Interfaces (produces):**
- **`Info.plist.in`:** `<key>CFBundleIconFile</key><string>rawreel</string>` after `CFBundlePackageType`.
- **The build tree:** `rawreel-gui.app/Contents/Resources/rawreel.icns`.
- **The package:** `rawreel.app/Contents/Resources/rawreel.icns`. The manifest line `ours rawreel.app/Contents/Resources/rawreel.icns` goes in sorted order.

- [ ] **Step 1: Add the plist and manifest `references` cases** to `test_icons.cmake`. Run `rawreel.icons`. Expected: FAIL `plist icon`.
- [ ] **Step 2: CMake.** In the `elseif(APPLE)` block:
  ```cmake
  set(rawreel_icns "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/rawreel.icns")
  target_sources(rawreel_gui PRIVATE "${rawreel_icns}")
  set_source_files_properties("${rawreel_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  ```
  In `packaging.cmake`, after the `Info.plist` install:
  ```cmake
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/rawreel.icns"
          DESTINATION "${rawreel_contents}/Resources" COMPONENT rawreel CONFIGURATIONS Release)
  ```
  Mention `rawreel.icns` in the comment above the macOS install block.
- [ ] **Step 3: Edit the plist and the manifest; run.**
  - Run `rawreel.icons`. Expected: PASS.
  - Run `cmake --build --preset ninja-macos-release --target rawreel_gui`.
  - Run `plutil -extract CFBundleIconFile raw libs/_build/rawreel/ninja-macos/Release/rawreel-gui.app/Contents/Info.plist`. Expected: `rawreel`.
  - Expected: `…/Contents/Resources/rawreel.icns` exists.
- [ ] **Step 4: Look.**
  - Touch the bundle and run LaunchServices' `lsregister -f` on it, so Finder drops a cached icon.
  - Open the build tree's `rawreel-gui.app` in Finder. Expected: the owl.
- [ ] **Step 5: Commit** `feat(macos): rawreel.icns as the app bundle's icon`.
- The package is checked once, in Task 7's macOS dry run.

## Task 3: Windows resources (`rawreel-gui.exe`, `rawreel-cli.exe`)

**Files:**
- `src/gui/rawreel-gui.rc`
- New: `src/cli/rawreel-cli.rc`
- `CMakeLists.txt`
- `tests/CMakeLists.txt`
- New: `tests/test_exe_icons.cpp`
- `tests/packaging/test_icons.cmake`

**Interfaces (produces):**
- **Both `.rc` files:** the line `rawreel ICON "rawreel.ico"`. In `rawreel-gui.rc` it comes before `#include "wx/msw/wx.rc"`, with a comment on the first-group rule (D4).
- **CMake, for `rawreel_gui` and `rawreel_cli` on WIN32:**
  ```cmake
  target_compile_options(<t> PRIVATE
      $<$<COMPILE_LANGUAGE:RC>:/I${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons>)
  set_property(SOURCE <rc> APPEND PROPERTY OBJECT_DEPENDS
               "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/rawreel.ico")
  ```
  plus `target_sources(rawreel_cli PRIVATE src/cli/rawreel-cli.rc)`.
- **`tests/test_exe_icons.cpp`** (WIN32 only, in `rawreel_cli_tests`):
  - Case `"exe icon: first group RAWREEL with 9 images"`, run for `RAWREEL_GUI_EXE` and for `RAWREEL_CLI_EXE`.
  - `tests/CMakeLists.txt` appends to `rawreel.cli_unit`'s `ENVIRONMENT`:
    - `RAWREEL_CLI_EXE=$<TARGET_FILE:rawreel_cli>`;
    - `RAWREEL_GUI_EXE=$<TARGET_FILE:rawreel_gui>`, only `if(TARGET rawreel_gui)`.

- [ ] **Step 1: Write the failing checks.**
  - The `rc icon` case in `test_icons.cmake`.
  - `test_exe_icons.cpp`.
  - Run:
    - `ctest --preset vs2026-release -R '^rawreel\.(icons|cli_unit)$' --output-on-failure`
  - Expected:
    - `rawreel.icons` fails on `rc icon`;
    - `rawreel.cli_unit` fails with first name `WXICON_AAA` for the GUI exe and no group for the CLI exe.
- [ ] **Step 2: Implement.**
  - Build: `cmake --build --preset vs2026-release --target rawreel_gui rawreel_cli rawreel_cli_tests`.
  - Expected: no warnings and no RC errors.
  - If the RC `/I` option is dropped by the generator, use `target_include_directories`.
- [ ] **Step 3: Run Step 1's tests.** Expected: PASS.
  - Without a Windows machine, this task's Windows steps and the Windows parts of Tasks 4, 5 and 7 run on the user's Windows machine, or in CI after the user allows a push. Record which.
- [ ] **Step 4: Check Explorer.** Both exes show the owl, in a new folder (the icon cache, D8).
- [ ] **Step 5: Commit** `feat(windows): rawreel icon as the first icon group of rawreel-gui and rawreel-cli`.

## Task 4: Window icons and the embedded PNGs

**Files:**
- New:
  - `src/gui/app_icon.h`
  - `src/gui/app_icon.cpp`
  - `src/gui/app_icon_data.h`
  - `packaging/icons/embed-png.cmake`
- Changed:
  - `CMakeLists.txt`
  - `src/gui/main_frame.cpp` (the constructor)
  - `src/gui/selftest.cpp` (the smoke scenario)

**Interfaces (produces):**
```cpp
// app_icon_data.h: the PNGs embedded for this platform (D7), keyed by file stem
// ("rawreel-16", "logo-unplated-dark-640x200", ...); never empty (the lockups are on every platform).
namespace rawreel {
struct EmbeddedPng {
    std::string_view key;
    std::span<const unsigned char> png;
};
[[nodiscard]] std::span<const EmbeddedPng> embeddedPngs();
}

// app_icon.h
namespace rawreel {
// The frame's icons: MSW the "rawreel" ICO group (9 sizes); GTK the embedded rawreel-<N> set;
// macOS none (the Dock and the title use the bundle's rawreel.icns). Empty on a load failure.
[[nodiscard]] wxIconBundle windowIcons();
#if defined(__WXOSX__)
// The main bundle's CFBundleIconFile ("" if absent), for the smoke check.
[[nodiscard]] std::string bundleIconFile();
#endif
}
```
- **`embed-png.cmake`:** `-DOUT=<cpp> -DFILES=<a;b;…>`, written as in D7.
- **CMake:**
  - `add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/app_icon_data.cpp COMMAND ${CMAKE_COMMAND} -DOUT=… -DFILES=… -P …/embed-png.cmake DEPENDS <pngs> packaging/icons/embed-png.cmake)`.
  - The file list:
    - the four `png/logo/logo-macos-*` files `if(APPLE)`, otherwise the six `png/logo/logo-unplated-*` files;
    - plus the seven `png/linux` files `if(UNIX AND NOT APPLE)`.
  - The generated source and `app_icon.cpp` are added to `rawreel_gui` on every platform.
- **`app_icon.cpp`:**
  - Every load runs under `wxLogNull` and is followed by one `DVV_LOG_TEXT` warning on failure.
  - `#if !wxUSE_LIBPNG` and, on MSW, `#if !wxUSE_ICO_CUR` stop the build with `#error`.
  - The GTK path is under `#if defined(__WXGTK__)`.

- [ ] **Step 1: Write the failing smoke step `window icons`** (D8). Run `ctest --preset ninja-macos-release -R '^rawreel\.gui_selftest\.smoke$' --output-on-failure`.
  - Expected on macOS: FAIL until Step 3, because `bundleIconFile` does not exist yet; write the step against the declared interface.
  - Expected on Windows and Linux: FAIL with `icons []`.
- [ ] **Step 2: Write `embed-png.cmake` and the CMake wiring.**
  - Build `rawreel_gui` on macOS.
  - Expected: the generated `.cpp` (four macOS lockup arrays) compiles clean with clang `-Werror`.
  - A second build does not run the custom command again.
- [ ] **Step 3: Write `app_icon.cpp`.**
  - In `MainFrame::MainFrame`, right after the `wxFrame` base:
    ```cpp
    if (const wxIconBundle icons = windowIcons(); !icons.IsEmpty()) { SetIcons(icons); }
    ```
  - **MSW:** `windowIcons()` returns `wxIconBundle(wxS("rawreel"), nullptr)`.
  - **GTK:**
    - register `wxPNGHandler` once;
    - for each `rawreel-<N>` entry: `wxBitmap::NewFromPNGData` → `wxIcon::CopyFromBitmap` → `AddIcon`.
  - **macOS:** `windowIcons()` returns an empty bundle; `bundleIconFile()` reads `CFBundleGetValueForInfoDictionaryKey`.
- [ ] **Step 4: Run** the smoke selftest on every OS available. Expected: PASS, with `window icons` naming the sizes (or, on macOS, the plist value).
- [ ] **Step 5: Commit** `feat(gui): window icons from the ICO resource (Windows) and the embedded set (Linux)`.

## Task 5: About's lockup banner, following the appearance

**Files:**
- `src/gui/app_icon.h`
- `src/gui/app_icon.cpp`
- `src/gui/about.h` (the header comment and the name constants)
- `src/gui/about.cpp` (`AboutDialog`, `lineRow`)
- `src/gui/selftest.cpp` (`aboutStep`, the new `appearanceStep`, `aboutScenario`, `SmokeState`)

**Interfaces (produces):**
```cpp
// app_icon.h
inline constexpr int ABOUT_LOGO_WIDTH_DIP = 320;
inline constexpr int ABOUT_LOGO_HEIGHT_DIP = 100;
// The lockup for a dark (true) or light (false) background, default size 320 x 100 DIP (D6):
// macOS the plated-tile lockup, FromBitmaps(1x, 2x); MSW and GTK the unplated lockup,
// LogoBundleImpl over 1x/2x/3x (any scale, high-quality rescale, cached). Empty, with one warning, if a PNG does not load.
[[nodiscard]] wxBitmapBundle aboutLogo(bool dark);

// about.h
inline constexpr std::string_view ABOUT_LOGO_LIGHT = "about-logo-light";  // the banner's name
inline constexpr std::string_view ABOUT_LOGO_DARK = "about-logo-dark";
```
`AboutDialog` gets:
- `removeStockIcon()`, `addBanner()` and `showLogo()`, exactly as in D6;
- `std::vector<wxWindow*> rows_`, filled by `lineRow`;
- the `wxEVT_SYS_COLOUR_CHANGED` binding.

The constructor order is: `Create()` → `removeStockIcon()` → `addBanner()` → `addButtons()`.

- [ ] **Step 1: Measure today's About width.**
  - Run `--selftest about` on the current build and read `width N` in `seen`.
  - Expected: N is greater than `FromDIP(320)` plus `4 * wxSizerFlags::GetDefaultBorder()`, on every OS where it can be run.
  - If not, stop and ask the user. The size is the user's decision (e). The choices are:
    - a smaller banner, which means regenerating the lockups through Task 1's gate and changing the constants;
    - accepting that the banner sets the dialog's width. Then the `width_kept` check must still pass.
- [ ] **Step 2: Write the failing checks** (D8). In `SmokeState` record:
  - `aboutBitmaps`;
  - `aboutLogoSize` and `aboutLogoWant`;
  - `aboutLogoName`;
  - `aboutDark`;
  - `aboutSwitch`: the `SetAppearance` result;
  - `aboutLogoNameSwitched` and `aboutLogoNameRestored`;
  - `aboutSizeKept`, a regression guard only.

  The hook's switch:
  - Where the switch is `Ok`, the hook calls `wxTheApp->SetAppearance(opposite)`, waits for the next tick through the hook's existing later-tick mechanism (no new timer), reads the name, then calls `SetAppearance(original)` and reads the name again on the following tick.
  - Otherwise it sends the synthetic `wxSysColourChangedEvent` after clearing the name.

  `appearanceStep` is added after `aboutStep` in `aboutScenario`. It compares the recorded variant with `the_arguments.appearance` when `test->app().appearanceResult() == Ok` and the appearance is `light` or `dark`.

  `seen` adds: `logo {n} {w}x{h} (want {W}x{H}) {name} dark {b}, switch {result}: {name} -> {name}, size kept {b}`.

  Run `libs/_build/rawreel/ninja-macos/Release/rawreel-gui.app/Contents/MacOS/rawreel-gui --selftest about`. Expected: exit 6, with `logo 0 0x0`.
- [ ] **Step 3: Implement.**
  - `aboutLogo`, with `LogoBundleImpl` on MSW and GTK and the PNG handler there.
  - The `AboutDialog` members.
  - `about.h`'s comment: "a banner with the rawreel logo lockup, light or dark by the appearance (D6)".
- [ ] **Step 4: Run `--selftest about`** plain, with `--appearance light` and with `--appearance dark`.
  - Expected: exit 0 each time.
  - The variant is light for `light`, dark for `dark`, and the system's for plain.
  - `switch Ok`, with the variant flipped and then restored.
  - The existing checks hold: steady open, width kept with the Components pane open, height closed and reopened, links, and the heading once.
  - Run on Retina, and on a 1× display if one is available.
- [ ] **Step 5: Run** the smoke selftest. Expected: PASS. This guards the shared `SmokeState` and the frame icons.
- [ ] **Step 6: Windows and Linux** (on those machines, or in CI after an approved push).
  - `cmake --build --preset vs2026-release --target rawreel_gui`. Expected: no warnings.
  - `--selftest about` with the three `--appearance` values:
    - on Windows at 100 %, 125 % and 150 % (the 125 % run pins `LogoBundleImpl`);
    - on Linux at scale 1, and at scale 2 if available.
  - Expected:
    - exit 0;
    - `logo 1` (the frame icon removed);
    - the logical size equals `FromDIP(320×100)` at each scale;
    - on Windows `switch Ok`, with the variant flipped and then restored;
    - on GTK3 `switch Ok`.
- [ ] **Step 7: Commit** `feat(gui): About shows the rawreel logo, light or dark by the appearance`.

## Task 6: README (and release notes if present)

**Files:**
- `README.md`
- `docs/release-notes-<next>.md`, only if it exists

- [ ] **Step 1: Write the facts of D9.**
  - Include the `about` selftest bullet (about line 325).
  - Add one sentence to Building (macOS):
    - the icon and logo files are generated on a Mac from `packaging/icons/svg/` by the two Swift scripts in `packaging/icons/tools/`;
    - the build uses the committed files;
    - `rawreel.icons` checks them against `sources.sha256`.
- [ ] **Step 2: Check.**
  - No machine path and no real name: run `privacy-lint.cmake` after `git add`.
  - Reconfigure. Expected: no new warning from the redist and apt-package checks, which read README.md.
- [ ] **Step 3: Commit** `docs: README for the app icon and the About logo`.

## Task 7: Full verification, manual checks, PR (stops for the user)

- [ ] **Step 1: Full suite, serially,** on the dev Mac: `DVV_REQUIRE_GPU=1 DVV_REQUIRE_FFMPEG=1 ctest --preset ninja-macos-release -j1 --output-on-failure`. Expected: all pass, including `rawreel.icons` and `rawreel.gui_selftest.smoke`.
- [ ] **Step 2: `--selftest about`** on the final build: plain, `--appearance light` and `--appearance dark`. Expected: exit 0 each time.
- [ ] **Step 3: macOS package dry run,** in the `rawreel` folder: `cmake -DBUNDLE=ON -P packaging/dry-run.cmake`.
  - Expected: the package checks pass with the new manifest line, and the dry run's signature check still passes.
  - Then touch and `lsregister -f` the dry run's `rawreel.app` and open it in Finder. Expected: the owl.
- [ ] **Step 4: Windows and Linux** (the user's machines, or CI after an approved push):
  - `ctest --preset vs2026-release -j1` / `ctest --preset ninja-clang-release -j1`. These include `rawreel.cli_unit`'s exe-icon case on Windows.
  - `--selftest about` as in Task 5 Step 6.
- [ ] **Step 5: Manual checklist (mandatory).** Clear the icon caches first (D8). Record the results in the PR description.
  - **macOS 27:**
    - The Finder icon of `rawreel.app` in the `.dmg` and in `/Applications`, the Dock, ⌘-Tab and Get Info.
    - Icon styles Default, Dark, Clear and Tinted: no grey plate in Default (D3).
    - The Retina and 1× look of the icon and the banner.
    - About: the light lockup in Light mode and the dark one in Dark mode.
    - Switching System Settings → Appearance while About is open swaps the banner and the panel colour at once.
  - **Windows 11:**
    - Explorer, in a new folder: both exes in the Details, Medium, Large and Extra large views.
    - The title bar, the taskbar and Alt+Tab at 100 %, 125 %, 150 % and 200 %, and with the window moved between monitors of different scale.
    - Light and dark mode.
    - At 16–32 px the unplated small icon reads as the owl panels; 20 and 24 px are anti-aliased, as accepted (decision (b)).
    - About: no small icon beside the text, the banner variant matches the app's mode, and the banner is sharp at 125 % and 150 %.
  - **Linux (Ubuntu 24.04 GNOME on XWayland, and WSLg):**
    - The dock, Alt+Tab, the overview icon and WSLg's taskbar icon.
    - About with the default theme and with `--appearance dark`.
    - A theme switch while About is open: note whether it swaps live or only at the next opening (D6).
- [ ] **Step 6: Stop and ask the user.**
  - Report the results.
  - Ask whether to squash the task commits into one, push `feat/app-icon` and open the PR. Its description carries D9's release-notes bullet and the manual results.
  - The user merges.
