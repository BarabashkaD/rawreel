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
FetchContent_Declare(dvv_media
    GIT_REPOSITORY "https://github.com/BarabashkaD/dvv-media.git"
    GIT_TAG main
    FIND_PACKAGE_ARGS 0.1)
# A stand-alone build tests only this repository (the normal variable hides the cache entry from
# the dependencies' option()); as a subproject the parent's value is kept, and the workspace
# build tests every repository.
if(PROJECT_IS_TOP_LEVEL)
    set(DVV_BUILD_TESTS OFF)
endif()
FetchContent_MakeAvailable(dvv_base dvv_platform dvv_gpu dvv_media)
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

# rawreel_dvv_version(<package> <out>): the version of a dvv package, whether it was found
# installed (find_package sets <package>_VERSION) or added in-tree (read from its directory). A
# package made available by another dvv project (dvv_registry) is looked up through FetchContent,
# because <package>_SOURCE_DIR is then only set in that project's scope.
function(rawreel_dvv_version package out)
    set(version "")
    if(NOT DEFINED ${package}_VERSION AND NOT DEFINED ${package}_SOURCE_DIR)
        FetchContent_GetProperties(${package})
    endif()
    if(DEFINED ${package}_VERSION)
        set(version "${${package}_VERSION}")
    elseif(${package}_SOURCE_DIR)
        get_directory_property(version DIRECTORY "${${package}_SOURCE_DIR}"
                               DEFINITION ${package}_VERSION)
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
target_link_libraries(rawreel_cli_core PUBLIC dvv::media)
set(cli_versions "RAWREEL_VERSION=\"${PROJECT_VERSION}\"")
foreach(package IN ITEMS dvv_base dvv_registry dvv_platform dvv_gpu dvv_media)
    rawreel_dvv_version(${package} version)
    string(TOUPPER "${package}" upper)
    list(APPEND cli_versions "RAWREEL_${upper}_VERSION=\"${version}\"")
endforeach()
target_compile_definitions(rawreel_cli_core PRIVATE ${cli_versions})
dvv_set_compiler_options(rawreel_cli_core)

# rawreel-cli (spec §10.3): console, no wx; built under every preset including the sanitizers.
add_executable(rawreel_cli src/cli/main.cpp)
set_target_properties(rawreel_cli PROPERTIES OUTPUT_NAME rawreel-cli)
target_link_libraries(rawreel_cli PRIVATE rawreel_cli_core)
dvv_link_components(rawreel_cli dvv::media_sources)
dvv_set_compiler_options(rawreel_cli)

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
