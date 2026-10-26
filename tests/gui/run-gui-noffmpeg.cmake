# rawreel-gui --selftest smoke without FFmpeg (M4 Task 11). Two runs,
# each must exit 0 without a leak report:
#   A. DVV_FFMPEG_DIR names an empty folder (no FFmpeg): the smoke takes its skip path ("lavfi steps
#      skipped").
#   B. DVV_FFMPEG_DIR names a folder with the FFmpeg libraries but no avfilter (core only), and a
#      lavfi: URI is on the command line: its open error carries the FFmpeg status hint
#      ("lavfi not available"), as a lavfi: URI from File -> Open URI... does. Only with
#      -DFFMPEG_DIR; without it, DVV_REQUIRE_FFMPEG=1 fails the test instead of skipping run B.
# Needs -DGUI=<rawreel-gui> -DWORK_DIR=<scratch folder>; optional -DFFMPEG_DIR=<FFmpeg runtime
# dir>. The script itself unsets DVV_REQUIRE_FFMPEG for both runs (each one is meant to lack
# FFmpeg or avfilter).
if(NOT GUI OR NOT WORK_DIR)
    message(FATAL_ERROR "run with -DGUI=<rawreel-gui> -DWORK_DIR=<dir> [-DFFMPEG_DIR=<dir>]")
endif()
if("$ENV{DVV_REQUIRE_FFMPEG}" STREQUAL "1" AND NOT FFMPEG_DIR)
    message(FATAL_ERROR "DVV_REQUIRE_FFMPEG=1 but no -DFFMPEG_DIR: the core-only run cannot run")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/none")
unset(ENV{DVV_REQUIRE_FFMPEG})

# run(<name> <expect regex> <args>...): one smoke run whose stderr must match the regex and the
# skip line; exit 77 skips the whole test.
function(run name expect)
    execute_process(COMMAND "${GUI}" --selftest smoke ${ARGN}
                    RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(code EQUAL 77)
        message(STATUS "skipped: ${err}")
        cmake_language(EXIT 77)
    endif()
    if(err MATCHES "managed object\\(s\\) still alive at exit")
        message(FATAL_ERROR "${name}: leak report:\n${err}")
    endif()
    if(NOT code STREQUAL "0")
        message(FATAL_ERROR "${name}: exit ${code}, expected 0\nstdout:\n${out}\nstderr:\n${err}")
    endif()
    foreach(regex IN ITEMS "${skipped}" "${expect}")
        if(NOT err MATCHES "${regex}")
            message(FATAL_ERROR "${name}: '${regex}' not found in:\n${err}")
        endif()
    endforeach()
    message(STATUS "${name}: ok")
endfunction()

set(skipped "selftest: FFmpeg not available, lavfi steps skipped")
set(ENV{DVV_FFMPEG_DIR} "${WORK_DIR}/none")
run("no FFmpeg" "FFmpeg: not available")

if(NOT FFMPEG_DIR)
    message(STATUS "core only: not run (no -DFFMPEG_DIR)")
    return()
endif()
file(GLOB libraries LIST_DIRECTORIES false "${FFMPEG_DIR}/*")
set(core "${WORK_DIR}/core-only")
file(MAKE_DIRECTORY "${core}")
foreach(library IN LISTS libraries)
    get_filename_component(name "${library}" NAME)
    if(NOT name MATCHES "avfilter")
        file(CREATE_LINK "${library}" "${core}/${name}" COPY_ON_ERROR)
    endif()
endforeach()
set(ENV{DVV_FFMPEG_DIR} "${core}")
run("core only" "; FFmpeg [^\n]*, lavfi not available \\[main_frame\\.cpp"
    "lavfi:testsrc2=s=64x48:r=25,format=yuv420p")
file(REMOVE_RECURSE "${WORK_DIR}")
