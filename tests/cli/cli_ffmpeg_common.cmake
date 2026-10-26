# Included by the FFmpeg run-cli-*.cmake scripts after cli_common.cmake. Needs -DFIXTURES=<folder>
# (the output of make-ffmpeg-fixtures.cmake).

# ffmpeg_skip(<why>): FATAL_ERROR when DVV_REQUIRE_FFMPEG=1 (CI and local runs that must not skip),
# else exit 77 (skip).
function(ffmpeg_skip why)
    if("$ENV{DVV_REQUIRE_FFMPEG}" STREQUAL "1")
        message(FATAL_ERROR "DVV_REQUIRE_FFMPEG=1, but: ${why}")
    endif()
    message(STATUS "skipped: ${why}")
    cmake_language(EXIT 77)
endfunction()

# require_ffmpeg(): skips (ffmpeg_skip) when rawreel-cli --version reports no FFmpeg.
function(require_ffmpeg)
    execute_process(COMMAND "${CLI}" --version WORKING_DIRECTORY "${WORK_DIR}"
                    RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "rawreel-cli --version: exit ${code}\nstdout:\n${out}\nstderr:\n${err}")
    endif()
    string(REGEX MATCH "FFmpeg: not available[^\n]*" missing "${out}")
    if(missing)
        ffmpeg_skip("${missing}")
    endif()
endfunction()

# use_fixture(<name>): copies FIXTURES/<name> into WORK_DIR; a missing fixture skips (ffmpeg_skip).
function(use_fixture name)
    if(NOT FIXTURES OR NOT EXISTS "${FIXTURES}/${name}")
        ffmpeg_skip("the fixture ${name} is missing in '${FIXTURES}'")
    endif()
    file(COPY "${FIXTURES}/${name}" DESTINATION "${WORK_DIR}")
endfunction()

# expect_same_file(<a> <b>): fails unless WORK_DIR/<a> and WORK_DIR/<b> are byte-identical.
function(expect_same_file a b)
    execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files "${a}" "${b}"
                    WORKING_DIRECTORY "${WORK_DIR}" RESULT_VARIABLE differ)
    if(differ)
        file(SIZE "${WORK_DIR}/${a}" size_a)
        file(SIZE "${WORK_DIR}/${b}" size_b)
        message(FATAL_ERROR "${a} (${size_a} bytes) differs from ${b} (${size_b} bytes)")
    endif()
endfunction()
