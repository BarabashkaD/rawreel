cmake_minimum_required(VERSION 4.2)
project(rawreel VERSION 1.0.0 DESCRIPTION "rawreel raw video player" LANGUAGES CXX)
# Version string; a pre-release build adds its release-stage label.
set(rawreel_VERSION_STRING "${PROJECT_VERSION}")

option(DVV_BUILD_TESTS "Build the dvv tests" ${PROJECT_IS_TOP_LEVEL})

# The dvv libraries. Inside the workspace build cmake/dvv-versions.cmake has declared them first,
# so these declarations are ignored there (spec §3.1). Stand-alone builds take them from the
# sibling folders (FETCHCONTENT_SOURCE_DIR_<NAME> in the workspace presets), from installed
# packages or from Git.
include(FetchContent)
FetchContent_Declare(dvv_base
    GIT_REPOSITORY "https://github.com/BarabashkaD/dvv-base.git"
    GIT_TAG main
    FIND_PACKAGE_ARGS 1.0)
FetchContent_Declare(dvv_platform
    GIT_REPOSITORY "https://github.com/BarabashkaD/dvv-platform.git"
    GIT_TAG main
    FIND_PACKAGE_ARGS 1.0)
FetchContent_Declare(dvv_gpu
    GIT_REPOSITORY "https://github.com/BarabashkaD/dvv-gpu.git"
    GIT_TAG main
    FIND_PACKAGE_ARGS 1.0)
FetchContent_Declare(dvv_media
    GIT_REPOSITORY "https://github.com/BarabashkaD/dvv-media.git"
    GIT_TAG main
    FIND_PACKAGE_ARGS 1.0)
FetchContent_Declare(dvv_components
    GIT_REPOSITORY "https://github.com/BarabashkaD/dvv-components.git"
    GIT_TAG main
    FIND_PACKAGE_ARGS 1.0)
# A stand-alone build tests only this repository (the normal variable hides the cache entry from
# the dependencies' option()); as a subproject the parent's value is kept, and the workspace
# build tests every repository.
if(PROJECT_IS_TOP_LEVEL)
    set(DVV_BUILD_TESTS OFF)
endif()
# Release debug symbols for every target of the build (packaging/symbols.cmake, D16).
include(packaging/symbols.cmake)
FetchContent_MakeAvailable(dvv_base dvv_platform dvv_gpu dvv_media dvv_components)
if(PROJECT_IS_TOP_LEVEL)
    unset(DVV_BUILD_TESTS)
endif()
if(NOT DVV_CMAKE_DIR)
    message(FATAL_ERROR "dvv_base did not define DVV_CMAKE_DIR")
endif()
include("${DVV_CMAKE_DIR}/DvvCompilerOptions.cmake")

# rawreel_render: the rendering of rawreel-gui that needs no wx (the display state and the viewport
# compositor), so unit, GPU and sanitizer builds use it without the GUI.
add_library(rawreel_render STATIC
    src/render/composite_reference.cpp
    src/render/display_state.cpp
    src/render/viewport_renderer.cpp)
target_include_directories(rawreel_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rawreel_render PUBLIC rawreel_core dvv::gpu dvv::base)
dvv_add_shaders(rawreel_render NAMESPACE rawreel_shaders SHADERS shaders/composite.slang)
dvv_set_compiler_options(rawreel_render)

# rawreel_core (design M3 §3): the wx-free application core (fit view math, player logic,
# commands and keys, format choice, drop routing, the Session and its timer, rawreel-gui's
# argument parser, the config file). Built under every preset, sanitizers included.
add_library(rawreel_core STATIC
    src/core/commands.cpp
    src/core/config.cpp
    src/core/drop.cpp
    src/core/format_choice.cpp
    src/core/format_presets.cpp
    src/core/open_text.cpp
    src/core/gui_arguments.cpp
    src/core/info_text.cpp
    src/core/layout.cpp
    src/core/perf_stats.cpp
    src/core/player.cpp
    src/core/session.cpp
    src/core/status_text.cpp
    src/core/timer.cpp
    src/core/view.cpp)
target_include_directories(rawreel_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rawreel_core PUBLIC dvv::media)
dvv_set_compiler_options(rawreel_core)

# rawreel_dvv_version(<package> <out>): the version of a dvv package with its release stage
# (<package>_VERSION_STRING, e.g. 1.0.0), whether it was found installed (its package
# configuration sets the variable) or added in-tree (read from its directory). A package made
# available by another dvv project (dvv_registry) is looked up through FetchContent, because
# <package>_SOURCE_DIR is then only set in that project's scope.
function(rawreel_dvv_version package out)
    set(version "")
    if(NOT DEFINED ${package}_VERSION_STRING AND NOT DEFINED ${package}_SOURCE_DIR)
        FetchContent_GetProperties(${package})
    endif()
    if(DEFINED ${package}_VERSION_STRING)
        set(version "${${package}_VERSION_STRING}")
    elseif(${package}_SOURCE_DIR)
        get_directory_property(version DIRECTORY "${${package}_SOURCE_DIR}"
                               DEFINITION ${package}_VERSION_STRING)
    endif()
    if(NOT version)
        message(FATAL_ERROR "rawreel: cannot find the version of ${package}")
    endif()
    set(${out} "${version}" PARENT_SCOPE)
endfunction()

# rawreel_cli_core: everything of rawreel-cli but its entry point, so the unit tests use it.
add_library(rawreel_cli_core STATIC
    src/cli/arguments.cpp src/cli/commands.cpp src/cli/output.cpp src/cli/utf8_arguments.cpp)
target_include_directories(rawreel_cli_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# rawreel_core: the config file loader (core/config.h).
target_link_libraries(rawreel_cli_core PUBLIC rawreel_core dvv::media)
dvv_set_compiler_options(rawreel_cli_core)

# Versions for rawreel-cli --version and About (design M4 v2 D23), on rawreel_core so both use them.
set(rawreel_versions "RAWREEL_VERSION=\"${rawreel_VERSION_STRING}\"")
foreach(package IN ITEMS dvv_base dvv_registry dvv_platform dvv_gpu dvv_media dvv_components)
    rawreel_dvv_version(${package} version)
    string(TOUPPER "${package}" upper)
    list(APPEND rawreel_versions "RAWREEL_${upper}_VERSION=\"${version}\"")
endforeach()
target_compile_definitions(rawreel_core PRIVATE ${rawreel_versions})
# The startup shared by both executables (design M4 v2 §4.6), the version lines and the crash
# handler (wx-free, so rawreel-cli uses it too).
target_sources(rawreel_core PRIVATE src/core/startup.cpp src/core/version_text.cpp
                                    src/gui/crash_handler.cpp)
# dvv::components: dvv/components/ffmpeg.h and loadFfmpeg (dvv::ffmpeg); no FFmpeg is linked.
target_link_libraries(rawreel_core PUBLIC dvv::components PRIVATE dvv::platform)

# rawreel-cli (spec §10.3): console, no wx; built under every preset including the sanitizers.
add_executable(rawreel_cli src/cli/main.cpp)
set_target_properties(rawreel_cli PROPERTIES OUTPUT_NAME rawreel-cli)
target_link_libraries(rawreel_cli PRIVATE rawreel_cli_core rawreel_core)
dvv_link_components(rawreel_cli dvv::media_sources)
# The dvv-components libraries (the ffmpeg component: providers ffmpeg and lavfi).
dvv_components_link(rawreel_cli)
dvv_set_compiler_options(rawreel_cli)

# rawreel-gui (spec §10.2). Not built for sanitizer builds on any platform (design M1b F13): wx and
# GTK are not instrumented, and MSVC ASan cannot link the non-ASan static wx. rawreel_render and its
# tests still build and run there. Windows and Linux (X11, also through XWayland).
if(NOT DVV_SANITIZE)
    find_package(wxWidgets CONFIG REQUIRED COMPONENTS core base aui)
    add_executable(rawreel_gui WIN32
        src/gui/about.cpp
        src/gui/app.cpp
        src/gui/dock_manager.cpp
        src/gui/drop_target.cpp
        src/gui/format_dialog.cpp
        src/gui/info_panel.cpp
        src/gui/log_panel.cpp
        src/gui/main.cpp
        src/gui/main_frame.cpp
        src/gui/player_panel.cpp
        src/gui/selftest.cpp
        src/gui/session_bridge.cpp
        src/gui/transport_bar.cpp
        src/gui/viewport.cpp)
    set_target_properties(rawreel_gui PROPERTIES OUTPUT_NAME rawreel-gui)
    # wx is an imported target, so its headers are SYSTEM includes (our /WX does not apply to them).
    target_link_libraries(rawreel_gui PRIVATE rawreel_render rawreel_core dvv::platform dvv::gpu
                                              dvv::base wx::aui wx::core wx::base)
    # The source providers (file, synthetic) are components linked into the program.
    dvv_link_components(rawreel_gui dvv::media_sources)
    dvv_set_compiler_options(rawreel_gui)
    if(WIN32)
        target_sources(rawreel_gui PRIVATE src/gui/viewport_msw.cpp src/gui/rawreel-gui.rc)
        # wmain() in a GUI-subsystem program; the manifest comes from wx.rc (rawreel-gui.rc).
        target_link_options(rawreel_gui PRIVATE /ENTRY:wmainCRTStartup /MANIFEST:NO)
    elseif(UNIX AND NOT APPLE)
        # Linux (wxGTK): the X11 start-up (gui_platform_gtk.cpp) and the viewport's X11 window.
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
        find_package(X11 REQUIRED)
        target_sources(rawreel_gui PRIVATE src/gui/gui_platform_gtk.cpp src/gui/viewport_gtk.cpp)
        target_link_libraries(rawreel_gui PRIVATE PkgConfig::GTK3 X11::X11)
    else()
        # Other toolkits: no native viewport yet (the viewport reports a failure).
        target_sources(rawreel_gui PRIVATE src/gui/viewport_stub.cpp)
    endif()
endif()
# The wxWidgets line of --version: the build's wx package version, also in rawreel-cli, which does
# not link wx; builds without wx (the sanitizer presets) say so.
if(DEFINED wxWidgets_VERSION)
    target_compile_definitions(rawreel_core PRIVATE RAWREEL_WX_VERSION="${wxWidgets_VERSION}")
else()
    target_compile_definitions(rawreel_core PRIVATE RAWREEL_WX_VERSION="not part of this build")
endif()
if(TARGET rawreel_gui)
    dvv_components_link(rawreel_gui)
endif()
# Release symbols: the PDB named by file name only on Windows, a split .debug file on Linux
# (packaging/symbols.cmake).
rawreel_release_symbols(rawreel_cli)
if(TARGET rawreel_gui)
    rawreel_release_symbols(rawreel_gui)
endif()

if(DVV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# The Windows and Linux packages (packaging/packaging.cmake); rawreel-gui is not built under the
# sanitizers.
if(NOT DVV_SANITIZE)
    include(packaging/packaging.cmake)
endif()
