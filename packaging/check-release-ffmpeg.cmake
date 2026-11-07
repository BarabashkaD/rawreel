# Checks the FFmpeg source pointer of every package of a release:
#   cmake -DDIST=<dir> -DPLATFORMS=<windows;linux;macos> -P packaging/check-release-ffmpeg.cmake
# DIST holds package-info-<platform>.cmake of each platform (make-packages.cmake). Each package
# that bundles FFmpeg must have a SOURCE.txt that names exactly one custom_ffmpeg tag, v<version>
# with <version> the custom_ffmpeg release of that package's third-party prefix
# (RAWREEL_SOURCE_TXT_CUSTOM_FFMPEG against RAWREEL_CUSTOM_FFMPEG_VERSION). Each platform may pin
# its own release. Any mismatch or missing package info fails; release.yml runs this before
# anything is attested.
cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/check-package.cmake")

foreach(required IN ITEMS DIST PLATFORMS)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "check-release-ffmpeg: ${required} is required")
    endif()
endforeach()

# One platform's errors, in its own scope so the package-info variables do not leak.
function(platform_errors platform out)
    set(info "${DIST}/package-info-${platform}.cmake")
    if(NOT EXISTS "${info}")
        set(${out} "${platform}: no package-info-${platform}.cmake" PARENT_SCOPE)
        return()
    endif()
    set(RAWREEL_PACKAGE "")
    set(RAWREEL_BUNDLED OFF)
    set(RAWREEL_CUSTOM_FFMPEG_VERSION "")
    set(RAWREEL_SOURCE_TXT_CUSTOM_FFMPEG "")
    include("${info}")
    if(NOT RAWREEL_BUNDLED)
        message(STATUS "check-release-ffmpeg: ${RAWREEL_PACKAGE}: FFmpeg is not bundled")
        set(${out} "" PARENT_SCOPE)
        return()
    endif()
    rawreel_ffmpeg_source_errors(NAME "${RAWREEL_PACKAGE}"
                                 CUSTOM_FFMPEG_VERSION "${RAWREEL_CUSTOM_FFMPEG_VERSION}"
                                 NAMED ${RAWREEL_SOURCE_TXT_CUSTOM_FFMPEG} ERRORS errors)
    if(NOT errors)
        message(STATUS "check-release-ffmpeg: ${RAWREEL_PACKAGE}: SOURCE.txt names custom_ffmpeg "
                       "v${RAWREEL_CUSTOM_FFMPEG_VERSION}, the release of its prefix")
    endif()
    set(${out} "${errors}" PARENT_SCOPE)
endfunction()

set(all_errors "")
foreach(platform IN LISTS PLATFORMS)
    if(NOT platform MATCHES "^(windows|linux|macos)$")
        list(APPEND all_errors "unknown platform '${platform}'")
        continue()
    endif()
    platform_errors("${platform}" errors)
    list(APPEND all_errors ${errors})
endforeach()
if(all_errors)
    list(JOIN all_errors "\n  " text)
    message(FATAL_ERROR "check-release-ffmpeg:\n  ${text}")
endif()
message(STATUS "check-release-ffmpeg: ok")
