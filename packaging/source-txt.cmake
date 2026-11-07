# The text of SOURCE.txt (the offer of the corresponding source of the bundled LGPL libraries),
# rendered from packaging/SOURCE.txt.in. A function, so that
# tests/packaging/test_packaging_scripts.cmake renders it for each platform.
include_guard(GLOBAL)

# rawreel_render_source_txt(OUT <file> PLATFORM <windows|linux|macos>
#                           CUSTOM_FFMPEG_VERSION <v> CUSTOM_FFMPEG_URL <release archive URL>
#                           FFMPEG_VERSION <v> LIBRARY_VERSIONS <text>
#                           [LAME_URL <url> LAME_SHA256 <sha>]
#                           FFMPEG_DIR <dir> TEXTS_DIR <dir>)
#   The tag v<CUSTOM_FFMPEG_VERSION> of the repository CUSTOM_FFMPEG_URL comes from is the
#   corresponding source of FFmpeg (with its submodules and build scripts). LAME's source is not in
#   it: LAME is its own source release, so its URL and SHA256 are printed. FFMPEG_DIR and TEXTS_DIR
#   are where the libraries and their licence texts are, relative to the app (macOS only).
function(rawreel_render_source_txt)
    cmake_parse_arguments(PARSE_ARGV 0 arg ""
        "OUT;PLATFORM;CUSTOM_FFMPEG_VERSION;CUSTOM_FFMPEG_URL;FFMPEG_VERSION;LIBRARY_VERSIONS;LAME_URL;LAME_SHA256;FFMPEG_DIR;TEXTS_DIR"
        "")
    set(CUSTOM_FFMPEG_VERSION "${arg_CUSTOM_FFMPEG_VERSION}")
    set(FFMPEG_VERSION "${arg_FFMPEG_VERSION}")
    set(FFMPEG_LIBRARY_VERSIONS "${arg_LIBRARY_VERSIONS}")
    # Each platform may pin its own release of custom_ffmpeg, in the repository its archive is from.
    if(NOT arg_CUSTOM_FFMPEG_URL MATCHES "^https://github\\.com/([^/]+/[^/]+)/releases/")
        message(FATAL_ERROR "rawreel packaging: cannot take <owner>/<repo> from "
                            "${arg_CUSTOM_FFMPEG_URL}")
    endif()
    set(RAWREEL_CUSTOM_FFMPEG_TAG_URL
        "https://github.com/${CMAKE_MATCH_1}/tree/v${arg_CUSTOM_FFMPEG_VERSION}")
    # LAME is inside avcodec, but the tag holds no LAME source: it pins LAME's source release.
    set(RAWREEL_LAME_NOTE "")
    if(NOT "${arg_LAME_URL}" STREQUAL "")
        cmake_path(GET arg_LAME_URL FILENAME lame_file)
        if(NOT lame_file MATCHES "^lame-([0-9][0-9.]*[0-9])\\.tar\\.gz$")
            message(FATAL_ERROR "rawreel packaging: DVV_LAME_SOURCE_URL names no "
                                "lame-<version>.tar.gz")
        endif()
        string(CONCAT RAWREEL_LAME_NOTE
               "\n\nLAME ${CMAKE_MATCH_1} (LGPL-2.0-or-later) is built into avcodec. "
               "That tag does not hold its source.\nThe corresponding source of LAME is its own "
               "source release "
               "${lame_file}:\n${arg_LAME_URL}")
        if(NOT "${arg_LAME_SHA256}" STREQUAL "")
            string(APPEND RAWREEL_LAME_NOTE
                   "\nSHA256 ${arg_LAME_SHA256}\n(the tag's versions.cmake pins it)")
        endif()
    endif()
    # The macOS build of custom_ffmpeg has no AMF support, so its SOURCE.txt has no AMF note.
    set(RAWREEL_AMF_NOTE "")
    set(RAWREEL_FFMPEG_HEADING "in this folder")
    set(RAWREEL_FFMPEG_AT "")
    set(RAWREEL_FFMPEG_THERE "")
    set(RAWREEL_FFMPEG_FOLDER "this folder")
    set(RAWREEL_FFMPEG_RESIGN "")
    if(arg_PLATFORM STREQUAL "macos")
        # SOURCE.txt is at the .dmg root, the libraries are in the app.
        set(RAWREEL_FFMPEG_HEADING "in ${arg_FFMPEG_DIR}")
        set(RAWREEL_FFMPEG_AT " of ${arg_TEXTS_DIR}")
        set(RAWREEL_FFMPEG_THERE " there")
        set(RAWREEL_FFMPEG_FOLDER "that folder")
        # The app's signature seals its libraries, and codesign refuses to sign the app while one
        # of them is unsigned (an unsigned library does not load on Apple Silicon either).
        string(CONCAT RAWREEL_FFMPEG_RESIGN
               "\nAfter such a change, sign again in the folder that holds rawreel.app: first each "
               "library you put\nin (codesign --force --sign - <library>), then the app "
               "(codesign --force --sign - rawreel.app).\nAfter deleting the folder, sign only "
               "the app.")
    else()
        string(CONCAT RAWREEL_AMF_NOTE
               "\n\nNot included: the AMD AMF public headers (MIT) that avcodec's AMF support is "
               "built against; their\nURL and SHA256 are in versions.cmake of that tag.")
    endif()
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/SOURCE.txt.in" "${arg_OUT}"
                   @ONLY NEWLINE_STYLE LF)
endfunction()
