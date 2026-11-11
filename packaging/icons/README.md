# rawreel icons

Two icon families, each in three variants (`square-background`, `rounded-background`,
`transparent-background`), stored as delivered (file names and bytes unchanged). Each variant
folder holds `<name>-1024.png`, `macOS/<name>.icns`, `Windows/<name>.ico` (10 sizes, 16 to
256 px) and `Linux/hicolor/<N>x<N>/apps/<name>.png` (N = 16 to 512), where `<name>` is
`<family>-<variant>-background`.

- `owl-on-film-reel/`: the build uses
  - `square-background/` for the app icons: the `.icns` is installed as the app bundle's
    `Contents/Resources/rawreel.icns` (the `Info.plist` names `rawreel` as `CFBundleIconFile`);
    the `.ico` is the first icon group (`rawreel`) of `rawreel-gui.exe` and `rawreel-cli.exe` and
    the GUI's window icons on Windows; the hicolor PNGs are embedded in `rawreel-gui` as the Linux
    window icons (the build copies them into the build tree as `<name>-<N>.png` first).
  - `transparent-background/owl-on-film-reel-transparent-background-1024.png`: Help → About's
    logo, embedded in `rawreel-gui` on every platform and scaled at run time to 128 x 128 DIP.
  - Every other file in it is kept and not used.
- `owl-with-film-frame/`: kept, not used.
- `v1/`: the first design; kept, not used. On a Mac, `swift v1/tools/gen-svg.swift v1` writes
  `v1/svg/` from `v1/art/`, and `swift v1/tools/render-icons.swift v1` renders `v1/rawreel.icns`,
  `v1/rawreel.ico`, the PNGs in `v1/png/` and `v1/sources.sha256` (run from this folder).
- `embed-png.cmake`: build tooling; writes the embedded PNGs as C++ arrays.

The test `rawreel.icons` (`tests/packaging/test_icons.cmake`) checks that all six sets are
complete, the structure of the `square-background` `.ico`, `.icns` and PNGs of
`owl-on-film-reel`, that About's logo is transparent at its corners and edge middles (on a macOS
host), the `v1` sources against `v1/sources.sha256`, and that no committed icon file holds private
data.
