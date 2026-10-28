# rawreel 0.9.0

rawreel is a player for raw and lossless video on the GPU (Vulkan). Version 0.9.0, the first
public preview, plays one stream at a time:

- any pixel format of the catalogue (YUV and RGB; packed, planar and semi-planar, e.g.
  `yuv420p`, `yuv420p10le`, `nv12`, `v210`), and custom formats from a JSON file;
- colour by standard: BT.601 (NTSC), BT.601 (PAL), BT.709, BT.2020 and BT.2100 (PQ or HLG,
  tone-mapped to SDR), limited or full range; set with `colour=<standard>` and `range=` in a URI,
  a `_<standard>` token at the end of a raw file's name (e.g. `clip_1920x1080_bt709.yuv`), a clip
  description or the format dialog; untagged clips use BT.709, or a standard guessed from the
  height with the dialog's Auto detect;
- the Info pane marks the colour "(default)" or "(from file)", and the status bar shows
  the colour mapping (e.g. "PQ → SDR tone mapping (1000 → 100 nits)");
- size and frame-rate presets in the format dialog;
- clip descriptions: a JSON file (`"rawreelClip": 1`) with a raw file's format, size, frame rate
  and colour (`{"standard": "bt709", "range": "limited"}`), read by Load JSON…, by
  `clip=@<file.json>` and as the sidecar `<file>.<ext>.json` (e.g. `clip.yuv.json`) next to the
  raw file;
- play, pause, step, seek and loop, with every frame shown in order;
- fit to window;
- sources: raw files, Y4M files and, through FFmpeg, any container FFmpeg demuxes (AVI, MKV, MOV,
  MP4, NUT, …) with a lossless video codec, and `lavfi:` filter graphs;
- a GPU device loss shows an error with Reopen instead of ending the program.

rawreel 0.9.0 runs on Windows and on Linux (Ubuntu 24.04, X11 or XWayland).

## Downloads

- `rawreel-0.9.0-windows-x64.zip`: the application for Windows. Unzip it anywhere, keep all its
  files together, and start `rawreel-gui.exe`.
- `rawreel-0.9.0-windows-x64-symbols.zip`: debug symbols for crash dumps.
- `rawreel-0.9.0-linux-x64.tar.xz`: the application for Linux x86-64. Extract it anywhere
  (`tar xf rawreel-0.9.0-linux-x64.tar.xz`), keep all its files together, and start
  `./rawreel-gui` in the extracted folder.
- `rawreel-0.9.0-linux-x64-symbols.tar.xz`: debug symbols for core dumps.
- `rawreel-0.9.0-third-party-sources.tar.xz`: the complete source of the bundled FFmpeg; one
  archive covers both packages.
- `SHA256SUMS`: the checksums.

To verify a download, compare `(Get-FileHash <file> -Algorithm SHA256).Hash` with its line in
`SHA256SUMS` (or run `sha256sum -c --ignore-missing SHA256SUMS`), and check its build
attestation with `gh attestation verify <file> --repo BarabashkaD/rawreel`.

## Requirements

Windows: Windows 10 or newer, 64-bit, with a GPU driver that supports Vulkan 1.4, and the
Microsoft Visual C++ Redistributable (x64) 14.51 or newer, which the zip does not include:
install Microsoft's
latest one from https://aka.ms/vc14/vc_redist.x64.exe. "MSVCP140.dll was not found" at the start
means it is missing or too old.

Linux: Ubuntu 24.04 (x86-64) with its current updates (glibc 2.39); an X11 desktop, or a Wayland
desktop with XWayland; GTK 3; the Vulkan loader `libvulkan1`; and a Vulkan 1.4 driver (Mesa from
`noble-updates` or the GPU vendor's driver). Install the system packages with
`sudo apt install libgtk-3-0t64 libsm6 libice6 libvulkan1 mesa-vulkan-drivers`.

## FFmpeg in the package

The Windows zip and the Linux tar.xz include the FFmpeg libraries avutil, avcodec, avformat,
avfilter, swscale and swresample of custom_ffmpeg 1.0.0 (FFmpeg n8.1.3, LGPL 2.1 or later) in
`ffmpeg/`. rawreel loads
them at run time and is not linked with them; you may replace them with your own LGPL shared build
of FFmpeg 8.1 or a newer 8.x with the same file names, or delete the folder. Each library must have
the major version rawreel was built with and at least its minor version (avutil 60.26, avcodec
62.28, avformat 62.12, avfilter 11.14, swscale 9.5, swresample 6.3); an older one, a GPL build or
a non-free build is refused. Their complete source is the release asset
`rawreel-0.9.0-third-party-sources.tar.xz` (custom_ffmpeg with its submodules and build scripts,
and LAME 3.100); `ffmpeg/SOURCE.txt` says the same. These libraries contain decoders for
codecs such as H.264, HEVC and AAC, which may be covered by patents in some countries; licensing
them is a matter for the user and the distributor. rawreel itself opens only lossless video
codecs. This is not legal advice.

## Known limits

- Only lossless video codecs open (`rawvideo`, `v210`, `v410`, `r210`, `ffv1`, `huffyuv`,
  `ffvhuff`, `utvideo`, `magicyuv`, `png`, `qtrle`); lossy codecs are refused at open.
- A decoder output format outside the pixel-format catalogue is refused; the message names it.
- A damaged frame of a container file (the decoder reports an error) and every frame after it up
  to the next key frame give the error "frame N is damaged" instead of a picture; from the next
  key frame on, the frames are exact again.
- The sample aspect ratio is ignored, so anamorphic files show stretched.
- A step or seek backwards on a stream without usable timestamps or a `lavfi:` graph decodes
  again from the start.
- A `lavfi:` graph with random colours (`gradients` without a fixed seed, or `c=random`) shows
  different frames after a seek back.
- MKV and NUT files are scanned when they open, so large ones open more slowly.
- Very small views of very large frames (below 1:8) can alias.
- FFV1 decoding is CPU-bound, so 4K FFV1 does not reach 60 fps: about 50 fps on a typical 8-core
  desktop CPU for easy content, far less for noisy content. Raw and Y4M files play 4K at 60 fps.
- The zip is not code-signed, so Windows SmartScreen may warn at the first start.
- On Linux, `rawreel-gui` uses X11; on a Wayland desktop it runs through XWayland.

## Bug reports

On Windows a crash writes a minidump to `%APPDATA%\rawreel\crashes\`. Attach it and the log to the
report: `%APPDATA%\rawreel\rawreel.log` for `rawreel-gui`, the console output (with its
`rawreel: crash dump <path>` line) for `rawreel-cli`. The maintainer reads the dump with the
symbols zip.

On Linux rawreel writes no crash report; attach a core dump (`ulimit -c unlimited` before the
start) and the log, `~/.local/share/rawreel/rawreel.log` for `rawreel-gui`. The README says where
the core goes. The maintainer reads it with the Linux symbols tar.xz.

## Licence

rawreel is MIT-licensed. `THIRD_PARTY_NOTICES.txt` in each package lists the licences of the
third-party code it ships. The FFmpeg libraries in `ffmpeg/` are LGPL; they include LAME
(`LGPL-2.0-or-later`) and SVT-AV1 (`BSD-3-Clause-Clear`). wxWidgets includes code under the
`libpng-2.0`, `IJG` and `X11` licences.
