find_package(doctest 2.5 REQUIRED CONFIG)

add_executable(rawreel_tests main.cpp test_arguments.cpp test_test_pattern.cpp)
target_link_libraries(rawreel_tests PRIVATE rawreel_render doctest::doctest)
target_compile_definitions(rawreel_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
dvv_set_compiler_options(rawreel_tests)
add_test(NAME rawreel.unit COMMAND rawreel_tests)
set_tests_properties(rawreel.unit PROPERTIES
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

add_executable(rawreel_gpu_tests gpu_main.cpp gpu_test_renderer.cpp)
target_link_libraries(rawreel_gpu_tests PRIVATE rawreel_render doctest::doctest)
target_compile_definitions(rawreel_gpu_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
dvv_set_compiler_options(rawreel_gpu_tests)
add_test(NAME rawreel.gpu COMMAND rawreel_gpu_tests)
set_tests_properties(rawreel.gpu PROPERTIES
    LABELS gpu
    SKIP_RETURN_CODE 77
    TIMEOUT 300
    ENVIRONMENT "${gpu_test_environment}"
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")

# GUI smoke test (spec §12; design M1b §5.6): rawreel-gui --selftest. It exits 77 without a usable
# device or display unless DVV_REQUIRE_GPU=1. Only where rawreel_gui exists (no sanitizer builds),
# and on Windows only: elsewhere the viewport has no native window yet.
if(TARGET rawreel_gui AND WIN32)
    add_test(NAME rawreel.gui_selftest COMMAND rawreel_gui --selftest)
    set_tests_properties(rawreel.gui_selftest PROPERTIES
        LABELS "gpu;display;gui"
        SKIP_RETURN_CODE 77
        TIMEOUT 120
        ENVIRONMENT "${gpu_test_environment};NO_AT_BRIDGE=1"
        ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
endif()
