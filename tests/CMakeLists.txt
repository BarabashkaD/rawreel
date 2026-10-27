find_package(doctest 2.5 REQUIRED CONFIG)
# The FFmpeg of the third-party prefix (M4 Task 1): DVV_FFMPEG_PROGRAM makes the CLI fixtures,
# DVV_FFMPEG_RUNTIME_DIR is the DVV_FFMPEG_DIR of the FFmpeg tests. dvv-components' own
# find_package() sets these in its directory scope only.
find_package(dvv_ffmpeg_sdk CONFIG REQUIRED)

add_executable(rawreel_tests
    main.cpp test_composite_reference.cpp test_display_state.cpp)
target_link_libraries(rawreel_tests PRIVATE rawreel_render doctest::doctest)
target_compile_definitions(rawreel_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
dvv_set_compiler_options(rawreel_tests)
add_test(NAME rawreel.unit COMMAND rawreel_tests)
set_tests_properties(rawreel.unit PROPERTIES
    TIMEOUT 300
    ENVIRONMENT DVV_LEAK_CHECK=fail
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
# Cycle label: `sanity` marks the fast inner-loop subset (ctest --preset vs2026-sanity). rawreel
# has no package test, so no entry carries the `package` label that vs2026-small excludes.
set_property(TEST rawreel.unit APPEND PROPERTY LABELS sanity)

# rawreel-cli unit tests (M2 Task 13): arguments, output forms and the Windows argument helper.
add_executable(rawreel_cli_tests
    cli_main.cpp test_cli_arguments.cpp test_cli_output.cpp test_cli_utf8_arguments.cpp)
target_link_libraries(rawreel_cli_tests PRIVATE rawreel_cli_core doctest::doctest)
target_compile_definitions(rawreel_cli_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
dvv_set_compiler_options(rawreel_cli_tests)
add_test(NAME rawreel.cli_unit COMMAND rawreel_cli_tests)
set_tests_properties(rawreel.cli_unit PROPERTIES
    TIMEOUT 300
    ENVIRONMENT DVV_LEAK_CHECK=fail
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")

# rawreel_core's unit tests, the headless Session suite and the ThreadTimer suite (doctest suites
# "session" and "timer", each its own CTest entry, so the race checks can repeat them), every preset.
# The slow:/fail: test provider is a component of this executable.
add_executable(rawreel_core_tests
    core_main.cpp session_fixture.cpp test_commands.cpp test_config.cpp test_drop.cpp
    test_format_choice.cpp test_format_presets.cpp
    test_gui_arguments.cpp test_info_text.cpp test_layout.cpp test_perf_stats.cpp test_player.cpp test_session.cpp
    test_session_storm.cpp test_session_types.cpp
    test_open_text.cpp test_source_provider.cpp test_startup.cpp
    test_status_text.cpp test_timer.cpp test_view.cpp)
target_link_libraries(rawreel_core_tests PRIVATE rawreel_core doctest::doctest)
dvv_link_components(rawreel_core_tests dvv::media_sources)
# RAWREEL_VERSION: the version packageVersions() must report for rawreel.
target_compile_definitions(rawreel_core_tests PRIVATE
    DOCTEST_CONFIG_USE_STD_HEADERS "RAWREEL_VERSION=\"${rawreel_VERSION_STRING}\"")
dvv_set_compiler_options(rawreel_core_tests)
add_test(NAME rawreel.core_unit COMMAND rawreel_core_tests --test-suite-exclude=session,timer)
set_tests_properties(rawreel.core_unit PROPERTIES
    TIMEOUT 300
    ENVIRONMENT DVV_LEAK_CHECK=fail
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
set_property(TEST rawreel.core_unit APPEND PROPERTY LABELS sanity)
add_test(NAME rawreel.core_session COMMAND rawreel_core_tests --test-suite=session)
set_tests_properties(rawreel.core_session PROPERTIES
    TIMEOUT 600
    ENVIRONMENT DVV_LEAK_CHECK=fail
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
add_test(NAME rawreel.core_timer COMMAND rawreel_core_tests --test-suite=timer)
set_tests_properties(rawreel.core_timer PROPERTIES
    TIMEOUT 300
    ENVIRONMENT DVV_LEAK_CHECK=fail
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")

# GPU tests (spec §12): exit 77 without a usable Vulkan 1.4 device unless DVV_REQUIRE_GPU=1. The
# loader is pointed at the validation layer of the third-party prefix, as in dvv-gpu.
find_file(DVV_VK_VALIDATION_LAYER_MANIFEST VkLayer_khronos_validation.json
    PATHS ${CMAKE_PREFIX_PATH} PATH_SUFFIXES bin share/vulkan/explicit_layer.d NO_DEFAULT_PATH)
set(gpu_test_environment DVV_LEAK_CHECK=fail)
if(DVV_VK_VALIDATION_LAYER_MANIFEST)
    cmake_path(GET DVV_VK_VALIDATION_LAYER_MANIFEST PARENT_PATH layer_dir)
    list(APPEND gpu_test_environment "VK_ADD_LAYER_PATH=${layer_dir}")
endif()
# The software driver is not instrumented; see tsan.supp.
if(DVV_SANITIZE STREQUAL "thread")
    list(APPEND gpu_test_environment
         "TSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tsan.supp")
endif()

# The Session cases (gpu_test_session.cpp) reuse the headless suite's fixture and test sources.
add_executable(rawreel_gpu_tests
    gpu_main.cpp gpu_support.cpp gpu_test_compositor.cpp gpu_test_session.cpp
    session_fixture.cpp test_source_provider.cpp)
target_link_libraries(rawreel_gpu_tests PRIVATE rawreel_render rawreel_core doctest::doctest)
dvv_link_components(rawreel_gpu_tests dvv::media_sources)
target_compile_definitions(rawreel_gpu_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
dvv_set_compiler_options(rawreel_gpu_tests)
add_test(NAME rawreel.gpu COMMAND rawreel_gpu_tests)
set_tests_properties(rawreel.gpu PROPERTIES
    LABELS gpu
    SKIP_RETURN_CODE 77
    TIMEOUT 300
    ENVIRONMENT "${gpu_test_environment}"
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")

# GUI selftests (spec §12; design M3 v2 §8.4): rawreel-gui --selftest <scenario>. They exit 77
# without a usable device or display unless DVV_REQUIRE_GPU=1. Only where rawreel_gui exists (no
# sanitizer builds). They load the prefix's FFmpeg through DVV_FFMPEG_DIR (M4 Task 7b): smoke opens
# a lavfi: URI, player drops the FFV1 MKV of the rawreel_ffmpeg fixture.
# Windows and Linux: on Linux they need an X11 display (WSLg or Xvfb). Every GUI
# test fails on an X error (FAIL_REGULAR_EXPRESSION here, the same check in the two scripts).
set(gui_x_error "X Window System error|BadWindow|BadDrawable|BadMatch")
if(TARGET rawreel_gui)
    add_test(NAME rawreel.gui_selftest.smoke
             COMMAND rawreel_gui --selftest smoke --appearance dark)
    set_tests_properties(rawreel.gui_selftest.smoke PROPERTIES
        FAIL_REGULAR_EXPRESSION "${gui_x_error}"
        LABELS "gpu;display;gui"
        SKIP_RETURN_CODE 77
        TIMEOUT 180
        ENVIRONMENT "${gpu_test_environment};NO_AT_BRIDGE=1"
        ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT};DVV_FFMPEG_DIR=set:${DVV_FFMPEG_RUNTIME_DIR}")
    set_property(TEST rawreel.gui_selftest.smoke APPEND PROPERTY LABELS sanity)
    # Device loss (design v2 §8.4 lost, E3): injected loss -> infobar with Reopen -> the kept frame
    # presented again on the new stack.
    add_test(NAME rawreel.gui_selftest.lost
             COMMAND rawreel_gui --selftest lost "synthetic:gradient?fmt=yuv420p10le&size=64x48&frames=40")
    set_tests_properties(rawreel.gui_selftest.lost PROPERTIES
        FAIL_REGULAR_EXPRESSION "${gui_x_error}"
        LABELS "gpu;display;gui"
        SKIP_RETURN_CODE 77
        TIMEOUT 180
        ENVIRONMENT "${gpu_test_environment};NO_AT_BRIDGE=1"
        ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT};DVV_FFMPEG_DIR=set:${DVV_FFMPEG_RUNTIME_DIR}")
    # GUI scenario player (design §8.4). The URI opens at start, like a user's command line.
    add_test(NAME rawreel.gui_selftest.player
             COMMAND rawreel_gui --selftest player "synthetic:gradient?fmt=yuv420p10le&size=64x48&frames=60")
    set_tests_properties(rawreel.gui_selftest.player PROPERTIES
        FAIL_REGULAR_EXPRESSION "${gui_x_error}"
        LABELS "gpu;display;gui"
        SKIP_RETURN_CODE 77
        TIMEOUT 180
        ENVIRONMENT "${gpu_test_environment};NO_AT_BRIDGE=1;RAWREEL_TEST_MKV=${CMAKE_BINARY_DIR}/cli-ffmpeg-fixtures/clip.mkv"
        ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT};DVV_FFMPEG_DIR=set:${DVV_FFMPEG_RUNTIME_DIR}"
        FIXTURES_REQUIRED rawreel_ffmpeg)
    # Without FFmpeg (M4 Task 11): smoke's skip path with no FFmpeg, then with the
    # FFmpeg libraries but no avfilter and a lavfi: URI on the command line (its open error names
    # the FFmpeg status). The script sets DVV_FFMPEG_DIR for each run and unsets
    # DVV_REQUIRE_FFMPEG itself, after failing when it is 1 and there is no FFmpeg runtime dir.
    add_test(NAME rawreel.gui_selftest.smoke_noffmpeg
             COMMAND ${CMAKE_COMMAND} -DGUI=$<TARGET_FILE:rawreel_gui>
                     -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/gui-noffmpeg
                     "-DFFMPEG_DIR=${DVV_FFMPEG_RUNTIME_DIR}"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/gui/run-gui-noffmpeg.cmake)
    set_tests_properties(rawreel.gui_selftest.smoke_noffmpeg PROPERTIES
        LABELS "gpu;display;gui"
        SKIP_RETURN_CODE 77
        TIMEOUT 300
        ENVIRONMENT "${gpu_test_environment};NO_AT_BRIDGE=1"
        ENVIRONMENT_MODIFICATION
            "${DVV_TEST_ENVIRONMENT};DVV_FFMPEG_DIR=unset:")
    # Injected faults (test only, RAWREEL_GUI_FAULT with --selftest): each run must exit 4.
    # presenter: every presenter fails (D25 path); stack: Session::create fails at startup.
    set(gui_fault_presenter "the presenter failed: .*injected presenter failure")
    set(gui_fault_stack "Vulkan could not start: .*injected stack failure")
    foreach(fault IN ITEMS presenter stack)
        add_test(NAME rawreel.gui_fault.${fault}
                 COMMAND ${CMAKE_COMMAND} -DGUI=$<TARGET_FILE:rawreel_gui>
                         "-DEXPECT=${gui_fault_${fault}}"
                         -P ${CMAKE_CURRENT_SOURCE_DIR}/gui/run-gui-fault.cmake)
        set_tests_properties(rawreel.gui_fault.${fault} PROPERTIES
            LABELS "gpu;display;gui"
            SKIP_RETURN_CODE 77
            TIMEOUT 180
            ENVIRONMENT "${gpu_test_environment};NO_AT_BRIDGE=1;RAWREEL_GUI_FAULT=${fault}"
            ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
    endforeach()
endif()
# Linux start-up without a usable X11 display: DISPLAY and WAYLAND_DISPLAY are unset by
# the script, so no window opens (label gui, not display).
if(TARGET rawreel_gui AND NOT WIN32)
    add_test(NAME rawreel.gui_start.no_display
             COMMAND ${CMAKE_COMMAND} -DGUI=$<TARGET_FILE:rawreel_gui>
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/gui/run-gui-no-display.cmake)
    set_tests_properties(rawreel.gui_start.no_display PROPERTIES
        LABELS "gui"
        TIMEOUT 300
        ENVIRONMENT "NO_AT_BRIDGE=1"
        ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
endif()

# rawreel-cli end to end (M2 Task 13): each script runs the built CLI in its own scratch folder.
foreach(name IN ITEMS info dump errors unicode)
    add_test(NAME rawreel.cli_e2e.${name}
             COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:rawreel_cli>
                     -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-e2e-${name}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/cli/run-cli-${name}.cmake)
    set_tests_properties(rawreel.cli_e2e.${name} PROPERTIES
        TIMEOUT 300
        ENVIRONMENT DVV_LEAK_CHECK=fail
        ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
endforeach()
set_property(TEST rawreel.cli_e2e.info APPEND PROPERTY LABELS sanity)

# GPU equals CPU (M2 exit criterion 2): dump --via gpu must write the bytes of dump --via cpu.
# bench (M2 Task 14): frames through produce, staging, upload and unpack. Exit 77 without a usable
# device unless DVV_REQUIRE_GPU=1. oversize (M4 Task 11-pre): a plane over the GPU image limit
# exits 3 with Unsupported and no validation message.
foreach(name IN ITEMS gpu bench oversize)
    add_test(NAME rawreel.cli_e2e.${name}
             COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:rawreel_cli>
                     -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-e2e-${name}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/cli/run-cli-${name}.cmake)
    set_tests_properties(rawreel.cli_e2e.${name} PROPERTIES
        LABELS gpu
        SKIP_RETURN_CODE 77
        TIMEOUT 600
        ENVIRONMENT "${gpu_test_environment};DVV_VK_VALIDATION=sync;DVV_VK_VALIDATION_REQUIRED=1;DVV_LEAK_CHECK=fail"
        ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
endforeach()

# rawreel-cli on FFmpeg inputs (M4 Task 7a; design v2 §8.4). The fixtures are made by the prefix's
# ffmpeg program on every run (64x48, a few frames); the tests use the prefix's FFmpeg libraries
# through DVV_FFMPEG_DIR, and fail instead of skipping with DVV_REQUIRE_FFMPEG=1.
set(cli_ffmpeg_fixtures ${CMAKE_BINARY_DIR}/cli-ffmpeg-fixtures)
add_test(NAME rawreel.cli_ffmpeg_fixtures
         COMMAND ${CMAKE_COMMAND} "-DFFMPEG=${DVV_FFMPEG_PROGRAM}" "-DOUT=${cli_ffmpeg_fixtures}"
                 "-DLIBDIR=${DVV_FFMPEG_RUNTIME_DIR}"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/cli/make-ffmpeg-fixtures.cmake)
set_tests_properties(rawreel.cli_ffmpeg_fixtures PROPERTIES
    LABELS ffmpeg
    FIXTURES_SETUP rawreel_ffmpeg
    TIMEOUT 120)
# rawreel_cli_e2e(<name> <script> <labels>): the test and its common properties; each caller sets
# its environment (lists cannot pass through a function's arguments).
function(rawreel_cli_e2e name script labels)
    add_test(NAME rawreel.cli_e2e.${name}
             COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:rawreel_cli>
                     -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-e2e-${name}
                     "-DFIXTURES=${cli_ffmpeg_fixtures}" "-DVERSION=${rawreel_VERSION_STRING}"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/cli/${script})
    set_tests_properties(rawreel.cli_e2e.${name} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
    if(labels)
        set_tests_properties(rawreel.cli_e2e.${name} PROPERTIES LABELS "${labels}")
    endif()
endfunction()
set(ffmpeg_environment
    ${gpu_test_environment} DVV_LEAK_CHECK=fail "DVV_FFMPEG_DIR=${DVV_FFMPEG_RUNTIME_DIR}")
rawreel_cli_e2e(ffmpeg run-cli-ffmpeg.cmake ffmpeg)
rawreel_cli_e2e(ffmpeg_gpu run-cli-ffmpeg-gpu.cmake "gpu;ffmpeg")
rawreel_cli_e2e(version run-cli-version.cmake ffmpeg)
set_tests_properties(rawreel.cli_e2e.ffmpeg rawreel.cli_e2e.ffmpeg_gpu PROPERTIES
    FIXTURES_REQUIRED rawreel_ffmpeg)
set_tests_properties(rawreel.cli_e2e.ffmpeg rawreel.cli_e2e.version PROPERTIES
    ENVIRONMENT "${ffmpeg_environment}"
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
set_tests_properties(rawreel.cli_e2e.ffmpeg_gpu PROPERTIES
    ENVIRONMENT "${ffmpeg_environment};DVV_VK_VALIDATION=sync;DVV_VK_VALIDATION_REQUIRED=1"
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
# Without FFmpeg: an empty (missing) or a relative DVV_FFMPEG_DIR, set by the scripts.
rawreel_cli_e2e(ffmpeg_missing run-cli-ffmpeg-missing.cmake ffmpeg)
rawreel_cli_e2e(ffmpeg_relative run-cli-ffmpeg-relative.cmake ffmpeg)
set_tests_properties(rawreel.cli_e2e.ffmpeg_missing rawreel.cli_e2e.ffmpeg_relative PROPERTIES
    ENVIRONMENT "${gpu_test_environment}"
    ENVIRONMENT_MODIFICATION
        "${DVV_TEST_ENVIRONMENT};DVV_REQUIRE_FFMPEG=unset:;DVV_FFMPEG_DIR=unset:")
# The crash handler (design v2 D22): Windows only, not under MSVC ASan (it installs its own
# exception handling). Linux has no crash handler in v1.0: a crash leaves a core dump.
if(WIN32 AND NOT DVV_SANITIZE)
    rawreel_cli_e2e(crash run-cli-crash.cmake "")
    set_tests_properties(rawreel.cli_e2e.crash PROPERTIES
        ENVIRONMENT DVV_LEAK_CHECK=fail
        ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
endif()

# Our binaries import no FFmpeg library (design v2 D3 c, E1); fails if a later change links it.
if(TARGET rawreel_gui)
    dvv_check_no_ffmpeg_imports(rawreel.no_ffmpeg_imports rawreel_cli rawreel_gui)
else()
    dvv_check_no_ffmpeg_imports(rawreel.no_ffmpeg_imports rawreel_cli)
endif()
set_tests_properties(rawreel.no_ffmpeg_imports PROPERTIES LABELS unit)

# The package checks and the artifact privacy scan (packaging/check-package.cmake and
# packaging/scan-artifact-privacy.cmake; design M4 v2 D16, §8.6).
add_test(NAME rawreel.packaging_scripts
         COMMAND ${CMAKE_COMMAND} -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/packaging-scripts-test
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/packaging/test_packaging_scripts.cmake)
set_tests_properties(rawreel.packaging_scripts PROPERTIES LABELS unit)
