find_package(doctest 2.5 REQUIRED CONFIG)

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
    test_format_choice.cpp
    test_gui_arguments.cpp test_info_text.cpp test_perf_stats.cpp test_player.cpp test_session.cpp
    test_session_types.cpp
    test_source_provider.cpp
    test_status_text.cpp test_timer.cpp test_view.cpp)
target_link_libraries(rawreel_core_tests PRIVATE rawreel_core doctest::doctest)
dvv_link_components(rawreel_core_tests dvv::media_sources)
target_compile_definitions(rawreel_core_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
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
# sanitizer builds).
# Windows only (U-WIN, M1b Task 12a): outside Windows the viewport is the stub.
if(TARGET rawreel_gui AND WIN32)
    add_test(NAME rawreel.gui_selftest.smoke
             COMMAND rawreel_gui --selftest smoke --appearance dark)
    set_tests_properties(rawreel.gui_selftest.smoke PROPERTIES
        LABELS "gpu;display;gui"
        SKIP_RETURN_CODE 77
        TIMEOUT 180
        ENVIRONMENT "${gpu_test_environment};NO_AT_BRIDGE=1"
        ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
    set_property(TEST rawreel.gui_selftest.smoke APPEND PROPERTY LABELS sanity)
    # Device loss (design v2 §8.4 lost, E3): injected loss -> infobar with Reopen -> the kept frame
    # presented again on the new stack.
    add_test(NAME rawreel.gui_selftest.lost
             COMMAND rawreel_gui --selftest lost "synthetic:gradient?fmt=yuv420p10le&size=64x48&frames=40")
    set_tests_properties(rawreel.gui_selftest.lost PROPERTIES
        LABELS "gpu;display;gui"
        SKIP_RETURN_CODE 77
        TIMEOUT 180
        ENVIRONMENT "${gpu_test_environment};NO_AT_BRIDGE=1"
        ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
    # GUI scenario player (design §8.4). The URI opens at start, like a user's command line.
    add_test(NAME rawreel.gui_selftest.player
             COMMAND rawreel_gui --selftest player "synthetic:gradient?fmt=yuv420p10le&size=64x48&frames=60")
    set_tests_properties(rawreel.gui_selftest.player PROPERTIES
        LABELS "gpu;display;gui"
        SKIP_RETURN_CODE 77
        TIMEOUT 180
        ENVIRONMENT "${gpu_test_environment};NO_AT_BRIDGE=1"
        ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
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
# device unless DVV_REQUIRE_GPU=1.
foreach(name IN ITEMS gpu bench)
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
