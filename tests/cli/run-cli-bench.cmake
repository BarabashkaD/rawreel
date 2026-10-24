# rawreel-cli bench runs frames through produce, staging, upload and unpack (M2 Task 14).
include("${CMAKE_CURRENT_LIST_DIR}/cli_common.cmake")

# Without a usable device the CLI exits 4 with "no usable GPU device"; that is a skip (77) unless
# DVV_REQUIRE_GPU=1 (CI and local runs that must not skip).
execute_process(COMMAND "${CLI}" bench "synthetic:noise?fmt=gray&size=2x2&frames=1" --frames 1
                        --inflight 1 WORKING_DIRECTORY "${WORK_DIR}"
                RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(code EQUAL 4 AND err MATCHES "no usable GPU device")
    if("$ENV{DVV_REQUIRE_GPU}" STREQUAL "1")
        message(FATAL_ERROR "DVV_REQUIRE_GPU=1, but: ${err}")
    endif()
    message(STATUS "skipped: ${err}")
    cmake_language(EXIT 77)
endif()
if(NOT code EQUAL 0)
    message(FATAL_ERROR "probe bench failed with ${code}:\n${err}")
endif()
expect_no_validation("${err}" "the probe bench")

# stage_runs(<json> <name> <out var>): the runs of one stage in bench --json (0 if absent).
function(stage_runs json name out_var)
    string(JSON count LENGTH "${json}" stages)
    set(runs 0)
    if(count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(i RANGE ${last})
            string(JSON entry_name GET "${json}" stages ${i} name)
            if(entry_name STREQUAL name)
                string(JSON runs GET "${json}" stages ${i} runs)
            endif()
        endforeach()
    endif()
    set(${out_var} ${runs} PARENT_SCOPE)
endfunction()

run_cli(0 json err bench "synthetic:noise?fmt=yuv420p10le&size=64x64&frames=8" --frames 8
        --inflight 4 --json)
expect_no_validation("${err}" "bench yuv420p10le")
string(JSON fps GET "${json}" fps)
if(NOT fps GREATER 0)
    message(FATAL_ERROR "fps '${fps}' is not positive in:\n${json}")
endif()
expect_json("${json}" 8 frames)
expect_json("${json}" 4 inflight)
foreach(stage IN ITEMS Produce HostToStaging Upload GpuUnpack)
    stage_runs("${json}" ${stage} runs)
    if(NOT runs EQUAL 8)
        message(FATAL_ERROR "stage ${stage}: ${runs} runs, expected 8 in:\n${json}")
    endif()
endforeach()

# 1: more frames than the source has.
run_cli(1 out err bench "synthetic:noise?fmt=gray&size=2x2&frames=2" --frames 3)
expect_match("${err}" "bench needs --frames <= 2 \\(the source has 2 frames\\)" "--frames 3")
