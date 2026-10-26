# rawreel-cli dump --via gpu of an FFmpeg source writes the bytes of --via cpu (M4 Task 7a).
include("${CMAKE_CURRENT_LIST_DIR}/cli_common.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cli_ffmpeg_common.cmake")

require_ffmpeg()
use_fixture(clip.mkv)

# Without a usable device the CLI exits 4 with "no usable GPU device"; that is a skip (77) unless
# DVV_REQUIRE_GPU=1 (the M2 rule of run-cli-gpu.cmake).
execute_process(COMMAND "${CLI}" dump clip.mkv --frame 7 --via gpu --out g.raw
                WORKING_DIRECTORY "${WORK_DIR}"
                RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(code EQUAL 4 AND err MATCHES "no usable GPU device")
    if("$ENV{DVV_REQUIRE_GPU}" STREQUAL "1")
        message(FATAL_ERROR "DVV_REQUIRE_GPU=1, but: ${err}")
    endif()
    message(STATUS "skipped: ${err}")
    cmake_language(EXIT 77)
endif()
if(NOT code EQUAL 0)
    message(FATAL_ERROR "dump --via gpu failed with ${code}:\n${err}")
endif()
expect_no_validation("${err}" "dump clip.mkv --via gpu")
if(err MATCHES "managed object\\(s\\) still alive at exit")
    message(FATAL_ERROR "dump --via gpu: leak report\n${err}")
endif()

run_cli(0 out err dump clip.mkv --frame 7 --via cpu --out c.raw)
expect_same_file(c.raw g.raw)

# bench always sets up the GPU, so it belongs here (after the 77 probe), with the validation check.
run_cli(0 out err bench "lavfi:testsrc2=s=64x48:r=25,format=yuv420p" --frames 30)
expect_no_validation("${err}" "bench lavfi")
