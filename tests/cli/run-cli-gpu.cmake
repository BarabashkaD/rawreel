# rawreel-cli dump --via gpu writes the bytes of dump --via cpu (M2 exit criterion 2).
include("${CMAKE_CURRENT_LIST_DIR}/cli_common.cmake")

# Without a usable device the CLI exits 4 with "no usable GPU device"; that is a skip (77) unless
# DVV_REQUIRE_GPU=1 (CI and local runs that must not skip).
execute_process(COMMAND "${CLI}" dump "synthetic:noise?fmt=gray&size=2x2" --via gpu
                        --out probe.raw WORKING_DIRECTORY "${WORK_DIR}"
                RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(code EQUAL 4 AND err MATCHES "no usable GPU device")
    if("$ENV{DVV_REQUIRE_GPU}" STREQUAL "1")
        message(FATAL_ERROR "DVV_REQUIRE_GPU=1, but: ${err}")
    endif()
    message(STATUS "skipped: ${err}")
    cmake_language(EXIT 77)
endif()
if(NOT code EQUAL 0)
    message(FATAL_ERROR "probe dump failed with ${code}:\n${err}")
endif()
expect_no_validation("${err}" "the probe dump")

foreach(format IN ITEMS v210 p010le rgb48be nv12)
    set(uri "synthetic:noise?fmt=${format}&size=34x17&frames=3&seed=5")
    foreach(view IN ITEMS planes original)
        run_cli(0 cpu_json err dump "${uri}" --count 3 --view ${view} --via cpu
                --out ${format}-${view}-cpu.bin --json)
        run_cli(0 gpu_json err dump "${uri}" --count 3 --view ${view} --via gpu
                --out ${format}-${view}-gpu.bin --json)
        expect_no_validation("${err}" "${format} ${view}")
        execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files
                                ${format}-${view}-cpu.bin ${format}-${view}-gpu.bin
                        WORKING_DIRECTORY "${WORK_DIR}" RESULT_VARIABLE differ)
        if(differ)
            message(FATAL_ERROR "${format} ${view}: --via gpu differs from --via cpu")
        endif()
        if(view STREQUAL "planes")
            converter_runs("${gpu_json}" GpuUnpack runs)
        else()
            converter_runs("${gpu_json}" Upload runs)
        endif()
        if(NOT runs GREATER 0)
            message(FATAL_ERROR "${format} ${view}: --via gpu ran no GPU converter")
        endif()
    endforeach()
endforeach()
