# rawreel-cli without FFmpeg (M4 Task 7a; design v2 §4.6, §7): DVV_FFMPEG_DIR names an empty
# folder (an explicit directory is final), so a lavfi open is Unsupported with the FFmpeg status
# line appended, and --version still exits 0 with "FFmpeg: not available (...)".
include("${CMAKE_CURRENT_LIST_DIR}/cli_common.cmake")

file(MAKE_DIRECTORY "${WORK_DIR}/empty")
set(ENV{DVV_FFMPEG_DIR} "${WORK_DIR}/empty")

run_cli(3 out info_err info "lavfi:testsrc2=s=64x48:r=25,format=yuv420p")
expect_match("${info_err}" "FFmpeg: not available \\(" "info lavfi: without FFmpeg")

# --json: the message is error.message plus the status line, as every other --json error (no
# "<Errc>: " prefix, no "[file:line]" source location).
run_cli(3 out err info "lavfi:testsrc2=s=64x48:r=25,format=yuv420p" --json)
string(JSON message GET "${out}" error message)
expect_match("${message}" "FFmpeg: not available \\(" "info lavfi: --json without FFmpeg")
if(message MATCHES "\\[" OR message MATCHES "^Unsupported: ")
    message(FATAL_ERROR "info lavfi: --json message has describe() details: ${message}")
endif()

run_cli(0 out err --version)
expect_match("${out}" "FFmpeg: not available \\(" "--version without FFmpeg")
expect_match("${out}" "\\$DVV_FFMPEG_DIR" "--version names the variable instead of its folder")

# Users paste these lines into bug reports: the reason names the variable, not the folder (a folder
# names their machine).
file(TO_NATIVE_PATH "${WORK_DIR}/empty" native_empty)
run_cli(0 version_json err --version --json)
string(JSON version_ffmpeg GET "${version_json}" ffmpeg)
foreach(text IN ITEMS "${info_err}" "${message}" "${out}" "${version_ffmpeg}")
    foreach(folder IN ITEMS "${WORK_DIR}/empty" "${native_empty}")
        string(FIND "${text}" "${folder}" at)
        if(NOT at EQUAL -1)
            message(FATAL_ERROR "the FFmpeg reason prints the folder ${folder}:\n${text}")
        endif()
    endforeach()
endforeach()
