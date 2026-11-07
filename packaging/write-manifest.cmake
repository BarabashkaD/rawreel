# Maintenance script: writes the manifest lines of an extracted bundled (ON) package, for review
# before packaging/manifest-<platform>.txt is committed:
#   cmake -DROOT=<extracted package dir> -DVERSION=<v> -DOUT=<file> [-DPLATFORM=windows|linux|macos]
#         -P packaging/write-manifest.cmake
# Windows and Linux: ROOT holds rawreel-<v>/. THIRD_PARTY_NOTICES.txt -> notices; ffmpeg/SOURCE.txt
# -> ours-bundled; any other file in ffmpeg/ -> ffmpeg <sha256>; everything else -> ours. A VC++
# runtime DLL (Windows) or a C/C++ runtime, Vulkan loader or stray shared library (Linux) stops it
# (none ships).
# macOS: ROOT is the root of the .dmg (rawreel.app and the documents); the paths have no version.
# THIRD_PARTY_NOTICES.txt -> notices; SOURCE.txt -> ours-bundled; the six FFmpeg libraries in
# rawreel.app/Contents/Frameworks/ffmpeg/ and every file in Contents/Resources/ffmpeg/ -> ffmpeg
# <sha256>; the other libraries in Contents/Frameworks/ (the Vulkan loader and KosmicKrisp) ->
# vulkan <sha256>; everything else -> ours. MoltenVK and any library elsewhere stop it.
# PLATFORM defaults to windows.
cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/check-package.cmake")

foreach(required IN ITEMS ROOT VERSION OUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "write-manifest: ${required}: required")
    endif()
endforeach()
if(NOT PLATFORM)
    set(PLATFORM windows)
endif()
if(NOT PLATFORM MATCHES "^(windows|linux|macos)$")
    message(FATAL_ERROR "write-manifest: PLATFORM must be windows, linux or macos")
endif()

if(PLATFORM STREQUAL "macos")
    set(top "")
    set(source "SOURCE.txt")
else()
    set(top "rawreel-${VERSION}/")
    set(source "${top}ffmpeg/SOURCE.txt")
endif()
if(NOT EXISTS "${ROOT}/${source}")
    message(FATAL_ERROR "write-manifest: ${ROOT} is no bundled (ON) package (no ${source})")
endif()
if(PLATFORM STREQUAL "linux")
    rawreel_linux_runtime_files(ROOT "${ROOT}" FILES runtime)
    if(runtime)
        message(FATAL_ERROR "write-manifest: runtime or stray shared libraries: ${runtime}")
    endif()
endif()
string(LENGTH "${top}" top_length)
file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE "${ROOT}" "${ROOT}/*")
set(lines "")
set(contents "rawreel\\.app/Contents")
set(ffmpeg_libraries "(avutil|avcodec|avformat|avfilter|swscale|swresample)")
foreach(file IN LISTS files)
    string(SUBSTRING "${file}" 0 ${top_length} head)
    if(NOT head STREQUAL top)
        message(FATAL_ERROR "write-manifest: layout: ${file} is not below ${top}")
    endif()
    string(SUBSTRING "${file}" ${top_length} -1 inner)
    cmake_path(GET file FILENAME name)
    string(TOLOWER "${name}" lower)
    if(PLATFORM STREQUAL "macos")
        set(path "${file}")
        if(IS_SYMLINK "${ROOT}/${file}")
            file(READ_SYMLINK "${ROOT}/${file}" target)
            list(APPEND lines "link ${path} ${target}")
        elseif(lower MATCHES "moltenvk")
            message(FATAL_ERROR "write-manifest: ${file} is MoltenVK; the app ships KosmicKrisp only")
        elseif(file STREQUAL "THIRD_PARTY_NOTICES.txt")
            list(APPEND lines "notices ${path}")
        elseif(file STREQUAL "SOURCE.txt")
            list(APPEND lines "ours-bundled ${path}")
        elseif(file MATCHES "^${contents}/Frameworks/ffmpeg/lib${ffmpeg_libraries}\\.[0-9]+\\.dylib$"
               OR file MATCHES "^${contents}/Resources/ffmpeg/")
            file(SHA256 "${ROOT}/${file}" hash)
            list(APPEND lines "ffmpeg ${path} ${hash}")
        elseif(file MATCHES "^${contents}/Frameworks/[^/]+\\.dylib$")
            file(SHA256 "${ROOT}/${file}" hash)
            list(APPEND lines "vulkan ${path} ${hash}")
        elseif(lower MATCHES "\\.(dylib|so)(\\.[0-9]+)*$" OR file MATCHES "^${contents}/Frameworks/")
            message(FATAL_ERROR "write-manifest: ${file} is a stray library: the app's libraries "
                                "are the Vulkan loader and driver in Contents/Frameworks and the "
                                "six FFmpeg libraries in Contents/Frameworks/ffmpeg")
        else()
            list(APPEND lines "ours ${path}")
        endif()
        continue()
    endif()
    set(path "rawreel-@V@/${inner}")
    if(name STREQUAL "THIRD_PARTY_NOTICES.txt")
        list(APPEND lines "notices ${path}")
    elseif(inner STREQUAL "ffmpeg/SOURCE.txt")
        list(APPEND lines "ours-bundled ${path}")
    elseif(inner MATCHES "^ffmpeg/")
        file(SHA256 "${ROOT}/${file}" hash)
        list(APPEND lines "ffmpeg ${path} ${hash}")
    elseif(lower MATCHES "^(vcruntime|msvcp|concrt|vccorlib)140.*\\.dll$")
        message(FATAL_ERROR "write-manifest: ${file} is a VC++ runtime DLL; the zip ships none")
    else()
        list(APPEND lines "ours ${path}")
    endif()
endforeach()
list(SORT lines)
list(JOIN lines "\n" body)
if(PLATFORM STREQUAL "linux")
    set(header
"# Manifest of the Linux tar.xz (packaging/check-package.cmake, rawreel_check_manifest). Format:
# <kind> <path> [<sha256>]; the path is relative to the archive root, @V@ is the version. Kinds:
# ours (built here), ours-bundled (only when bundled), notices, ffmpeg (a bundled third-party file
# with its SHA256; only when bundled). No C/C++ runtime or Vulkan loader library ships; the FFmpeg
# libraries are named by their SONAME. Generated by packaging/write-manifest.cmake from a bundled
# package and reviewed.")
elseif(PLATFORM STREQUAL "macos")
    set(header
"# Manifest of the macOS .dmg (packaging/check-package.cmake, rawreel_check_manifest). Format:
# <kind> <path> [<sha256>]; the path is relative to the root of the mounted .dmg. Kinds: ours
# (built here), ours-bundled (only when bundled), notices, ffmpeg (a bundled third-party file with
# its SHA256; only when bundled), vulkan (the Vulkan loader and the KosmicKrisp driver with their
# SHA256, as signed in the app; in both variants), link (a symbolic link and its target: the
# Applications link). The FFmpeg libraries are named by their major (the install names their
# siblings use); no MoltenVK ships. Generated by
# packaging/write-manifest.cmake from a bundled package and reviewed.")
else()
    set(header
"# Manifest of the Windows zip (packaging/check-package.cmake, rawreel_check_manifest).
# Format: <kind> <path> [<sha256>]; the path is relative to the archive root, @V@ is the
# version. Kinds: ours (built here), ours-bundled (only when bundled), notices, ffmpeg (a bundled
# third-party file with its SHA256; only when bundled). No VC++ runtime DLL ships. Generated by
# packaging/write-manifest.cmake from a bundled package and reviewed.")
endif()
file(WRITE "${OUT}" "${header}
${body}
")
message(STATUS "write-manifest: ${OUT}")
