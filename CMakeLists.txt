cmake_minimum_required(VERSION 4.2)
project(rawreel VERSION 0.1.0 DESCRIPTION "rawreel raw video player" LANGUAGES CXX)

option(DVV_BUILD_TESTS "Build the dvv tests" ${PROJECT_IS_TOP_LEVEL})

# The dvv libraries. Inside the workspace build cmake/dvv-versions.cmake has declared them first,
# so these declarations are ignored there (spec §3.1). Stand-alone builds take them from the
# sibling folders (FETCHCONTENT_SOURCE_DIR_<NAME> in the workspace presets), from installed
# packages or from Git.
include(FetchContent)
FetchContent_Declare(dvv_base
    GIT_REPOSITORY "https://github.com/BarabashkaD/dvv-base.git"
    GIT_TAG main
    FIND_PACKAGE_ARGS 0.1)
FetchContent_Declare(dvv_platform
    GIT_REPOSITORY "https://github.com/BarabashkaD/dvv-platform.git"
    GIT_TAG main
    FIND_PACKAGE_ARGS 0.1)
FetchContent_Declare(dvv_gpu
    GIT_REPOSITORY "https://github.com/BarabashkaD/dvv-gpu.git"
    GIT_TAG main
    FIND_PACKAGE_ARGS 0.1)
# A stand-alone build tests only this repository (the normal variable hides the cache entry from
# the dependencies' option()); as a subproject the parent's value is kept, and the workspace
# build tests every repository.
if(PROJECT_IS_TOP_LEVEL)
    set(DVV_BUILD_TESTS OFF)
endif()
FetchContent_MakeAvailable(dvv_base dvv_platform dvv_gpu)
if(PROJECT_IS_TOP_LEVEL)
    unset(DVV_BUILD_TESTS)
endif()
if(NOT DVV_CMAKE_DIR)
    message(FATAL_ERROR "dvv_base did not define DVV_CMAKE_DIR")
endif()
include("${DVV_CMAKE_DIR}/DvvCompilerOptions.cmake")

# rawreel_render: everything of rawreel-gui that needs no wx (test pattern, renderer, arguments),
# so unit, GPU and sanitizer builds use it without the GUI.
add_library(rawreel_render STATIC
    src/arguments.cpp
    src/test_pattern.cpp
    src/test_pattern_renderer.cpp)
target_include_directories(rawreel_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rawreel_render PUBLIC dvv::gpu dvv::base)
dvv_add_shaders(rawreel_render NAMESPACE rawreel_shaders SHADERS shaders/test_pattern.slang)
dvv_set_compiler_options(rawreel_render)

# rawreel-gui (spec §10.2). Not built for sanitizer builds on any platform (design M1b F13): wx and
# GTK are not instrumented, and MSVC ASan cannot link the non-ASan static wx. rawreel_render and its
# tests still build and run there. v1.0 runs it on Windows only; elsewhere it is built with a stub
# native window, so the GUI code stays portable.
if(NOT DVV_SANITIZE)
    find_package(wxWidgets CONFIG REQUIRED COMPONENTS core base)
    add_executable(rawreel_gui WIN32
        src/app.cpp
        src/main.cpp
        src/main_frame.cpp
        src/selftest.cpp
        src/vulkan_viewport.cpp)
    set_target_properties(rawreel_gui PROPERTIES OUTPUT_NAME rawreel-gui)
    # wx is an imported target, so its headers are SYSTEM includes (our /WX does not apply to them).
    target_link_libraries(rawreel_gui PRIVATE rawreel_render dvv::platform dvv::gpu dvv::base
                                              wx::core wx::base)
    dvv_set_compiler_options(rawreel_gui)
    if(WIN32)
        target_sources(rawreel_gui PRIVATE src/native_window_msw.cpp src/rawreel-gui.rc)
        # wmain() in a GUI-subsystem program; the manifest comes from wx.rc (rawreel-gui.rc).
        target_link_options(rawreel_gui PRIVATE /ENTRY:wmainCRTStartup /MANIFEST:NO)
    else()
        # No native viewport outside Windows yet (Linux support follows).
        target_sources(rawreel_gui PRIVATE src/native_window_stub.cpp)
    endif()
endif()

if(DVV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
