# rawreel-cli's crash handler on every OS, in the test's own home folder (test-home-crash of the
# build tree, so no other test shares or renames its files). Each RAWREEL_TEST_CRASH mode must give
# one "rawreel: crash log <path>" line naming a .dvvlog that starts with DVVLOGS1, and:
#   macOS, Linux  a death by a crash signal and one "rawreel: crash signal <n>" line
#   Windows       a non-empty .dmp named rawreel-cli-<date>-<time>-<pid>.dmp and the cause line
#                 "rawreel: crash exception 0x<8 hex digits>" with the exception code as the exit
#                 result; "abort" and "terminate" exit with 3 and give "rawreel: crash abort"
# The modes:
#   1          an access violation or a fault on the main thread
#   stack      a stack overflow on the main thread
#   threads    four threads crash at the same moment (the first wins, the others wait)
#   exit       a fault in a static destructor after main()
#   abort      abort()
#   terminate  an uncaught exception
# After a run the script deletes that run's files: each rawreel-cli start installs the handler,
# which renames the earlier snapshots of the same process to .reported.dvvlog, so files left from
# one mode would break the counts of the next.
#
# The crashes folder is read from the OS ($APPDATA, $HOME/Library/Application Support or
# $XDG_DATA_HOME, then rawreel/crashes) and refused when it is not under the test's own home.
# macOS keeps a system crash report per mode in the user's DiagnosticReports folder (ReportCrash
# writes there whatever HOME is). On macOS and Linux the CLI runs through a shell that sets the core
# size limit to 0, so no core file is written.
include("${CMAKE_CURRENT_LIST_DIR}/cli_common.cmake")

if(CMAKE_HOST_WIN32)
    set(data_root "$ENV{APPDATA}")
    set(root_variable "APPDATA")
elseif(CMAKE_HOST_APPLE)
    set(data_root "$ENV{HOME}")
    set(root_variable "HOME")
else()
    set(data_root "$ENV{XDG_DATA_HOME}")
    set(root_variable "XDG_DATA_HOME")
endif()
if("${data_root}" STREQUAL "")
    message(FATAL_ERROR "${root_variable} is not set (the test environment points it into the test home)")
endif()
cmake_path(CONVERT "${data_root}" TO_CMAKE_PATH_LIST data_root NORMALIZE)
if(NOT data_root MATCHES "/test-home-crash(/|$)")
    message(FATAL_ERROR "the data folder ${data_root} is not under the test's own home (test-home-crash)")
endif()
if(CMAKE_HOST_APPLE)
    set(data_root "${data_root}/Library/Application Support")
endif()
set(crashes "${data_root}/rawreel/crashes")
file(REMOVE_RECURSE "${crashes}")

# CMake's regular expressions have no {n} repeat.
string(REPEAT "[0-9]" 8 digits_8)
string(REPEAT "[0-9]" 6 digits_6)
string(REPEAT "[0-9a-f]" 8 hex_8)
set(snapshot_regex "^rawreel-cli-${digits_8}-${digits_6}-[0-9]+\\.dvvlog$")
set(dump_regex "^rawreel-cli-${digits_8}-${digits_6}-[0-9]+\\.dmp$")

set(problems "")

# count_lines(<out> <text> <regex>): the lines of <text> that match <regex>; the first match's
# CMAKE_MATCH_1 goes to <out>_1.
function(count_lines out text regex)
    string(REPLACE "\r\n" "\n" text "${text}")
    string(REPLACE "\n" ";" lines "${text}")
    set(count 0)
    set(first "")
    foreach(line IN LISTS lines)
        if(line MATCHES "${regex}")
            if(count EQUAL 0)
                set(first "${CMAKE_MATCH_1}")
            endif()
            math(EXPR count "${count} + 1")
        endif()
    endforeach()
    set(${out} ${count} PARENT_SCOPE)
    set(${out}_1 "${first}" PARENT_SCOPE)
endfunction()

# run_cli_with_mode(<mode>): runs --version with RAWREEL_TEST_CRASH=<mode> and sets code, err.
# TIMEOUT: a filter that hands the process to WER may hang on a dialog.
macro(run_cli_with_mode mode)
    set(ENV{RAWREEL_TEST_CRASH} "${mode}")
    if(CMAKE_HOST_WIN32)
        execute_process(COMMAND "${CLI}" --version WORKING_DIRECTORY "${WORK_DIR}" TIMEOUT 60
                        RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    else()
        execute_process(COMMAND /bin/sh -c "ulimit -c 0; exec \"$0\" \"$@\"" "${CLI}" --version
                        WORKING_DIRECTORY "${WORK_DIR}" TIMEOUT 60
                        RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    endif()
    unset(ENV{RAWREEL_TEST_CRASH})
endmacro()

# crash_run(<mode>): one crashing run, its checks, then the cleanup of its files.
function(crash_run mode)
    run_cli_with_mode("${mode}")
    set(found "")
    # The death and the cause line.
    if(CMAKE_HOST_WIN32 AND mode MATCHES "^(abort|terminate)$")
        if(NOT code STREQUAL "3")
            list(APPEND found "exit result ${code}, expected 3")
        endif()
        set(cause_regex "^rawreel: crash abort$")
    elseif(CMAKE_HOST_WIN32)
        if(mode STREQUAL "stack")
            set(code_regex "[Ss]tack overflow|[Cc]00000[Ff][Dd]|3221225725|-1073741571")
        else()
            set(code_regex
                "[Aa]ccess violation|[Ss]egmentation fault|[Cc]0000005|3221225477|-1073741819")
        endif()
        if(NOT code MATCHES "${code_regex}")
            list(APPEND found "exit result ${code}, expected ${code_regex}")
        endif()
        set(cause_regex "^rawreel: crash exception 0x${hex_8}$")
    else()
        string(CONCAT death_regex
            "^(Segmentation fault|Bus error|Illegal instruction|Floating-point exception|"
            "SIGTRAP|Trace/breakpoint trap|Subprocess aborted|Child aborted|Abort trap)$")
        if(NOT code MATCHES "${death_regex}")
            list(APPEND found "ended as \"${code}\", not by a crash signal")
        endif()
        set(cause_regex "^rawreel: crash signal ([0-9]+)$")
    endif()
    count_lines(causes "${err}" "${cause_regex}")
    if(NOT causes EQUAL 1)
        list(APPEND found "${causes} cause lines on stderr matching ${cause_regex}, expected 1")
    endif()
    # The crash log line names the one .dvvlog.
    count_lines(logs "${err}" "^rawreel: crash log (.+)$")
    if(NOT logs EQUAL 1)
        list(APPEND found "${logs} 'rawreel: crash log <path>' lines on stderr, expected 1")
    endif()
    file(GLOB snapshots LIST_DIRECTORIES false "${crashes}/*.dvvlog")
    list(LENGTH snapshots snapshot_count)
    if(NOT snapshot_count EQUAL 1)
        list(APPEND found "${snapshot_count} .dvvlog files in ${crashes}, expected 1")
    else()
        list(GET snapshots 0 snapshot)
        cmake_path(GET snapshot FILENAME snapshot_name)
        if(NOT snapshot_name MATCHES "${snapshot_regex}")
            list(APPEND found "unexpected snapshot name ${snapshot_name}")
        endif()
        # HEX: in text mode, CMake can return some binary files' first 8 bytes with a newline.
        file(READ "${snapshot}" magic LIMIT 8 HEX)
        if(NOT magic STREQUAL "4456564c4f475331")  # "DVVLOGS1"
            list(APPEND found "${snapshot_name} does not start with DVVLOGS1")
        endif()
        if(logs EQUAL 1)
            file(TO_CMAKE_PATH "${logs_1}" logged_path)
            cmake_path(GET logged_path FILENAME logged_name)
            if(NOT logged_name STREQUAL snapshot_name)
                list(APPEND found "the crash log line names ${logs_1}, not ${snapshot_name}")
            endif()
        endif()
    endif()
    # Windows: one non-empty .dmp named after the process, and its crash dump line.
    if(CMAKE_HOST_WIN32)
        file(GLOB dumps LIST_DIRECTORIES false "${crashes}/*.dmp")
        list(LENGTH dumps dump_count)
        if(NOT dump_count EQUAL 1)
            list(APPEND found "${dump_count} .dmp files in ${crashes}, expected 1")
        else()
            list(GET dumps 0 dump)
            cmake_path(GET dump FILENAME dump_name)
            file(SIZE "${dump}" dump_size)
            if(NOT dump_name MATCHES "${dump_regex}")
                list(APPEND found "unexpected dump name ${dump_name}")
            endif()
            if(NOT dump_size GREATER 0)
                list(APPEND found "the dump ${dump_name} is empty")
            endif()
        endif()
        count_lines(dump_lines "${err}" "^rawreel: crash dump (.+)$")
        if(NOT dump_lines EQUAL 1)
            list(APPEND found "${dump_lines} 'rawreel: crash dump <path>' lines on stderr, expected 1")
        elseif(dump_count EQUAL 1)
            file(TO_CMAKE_PATH "${dump_lines_1}" dumped_path)
            cmake_path(GET dumped_path FILENAME dumped_name)
            if(NOT dumped_name STREQUAL dump_name)
                list(APPEND found "the crash dump line names ${dump_lines_1}, not ${dump_name}")
            endif()
        endif()
    endif()
    # Cleanup, whatever the checks found.
    file(GLOB leftovers LIST_DIRECTORIES false "${crashes}/*")
    if(leftovers)
        file(REMOVE ${leftovers})
    endif()
    if(found)
        string(REPLACE ";" "\n  " text "${found}")
        set(problems "${problems}\n[${mode}] ${text}\nexit: ${code}\nstderr:\n${err}" PARENT_SCOPE)
    else()
        message(STATUS "[${mode}] crash files checked and removed: exit ${code}")
    endif()
endfunction()

foreach(mode IN ITEMS 1 stack threads exit abort terminate)
    crash_run("${mode}")
endforeach()

# No crash requested: an empty value and "none" run normally and leave the crashes folder empty.
foreach(mode IN ITEMS "" "none")
    run_cli_with_mode("${mode}")
    set(found "")
    if(NOT code STREQUAL "0")
        list(APPEND found "exit ${code}, expected 0")
    endif()
    if(err MATCHES "rawreel: crash")
        list(APPEND found "a 'rawreel: crash' line on stderr")
    endif()
    file(GLOB leftovers LIST_DIRECTORIES true "${crashes}/*")
    if(leftovers)
        list(APPEND found "the crashes folder is not empty: ${leftovers}")
        file(REMOVE_RECURSE ${leftovers})
    endif()
    if(found)
        string(REPLACE ";" "\n  " text "${found}")
        set(problems "${problems}\n[mode '${mode}'] ${text}\nstderr:\n${err}")
    else()
        message(STATUS "[mode '${mode}'] no crash, no files")
    endif()
endforeach()

if(problems)
    message(FATAL_ERROR "crash handler:${problems}")
endif()
