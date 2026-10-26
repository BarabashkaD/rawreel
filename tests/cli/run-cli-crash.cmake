# rawreel-cli's crash handler (M4 Task 7a; design v2 D22, §8.4; Windows, not under ASan). Each
# RAWREEL_TEST_CRASH mode must give exactly one "rawreel: crash dump <path>" line, a non-empty dump
# and a process that ends with the exception code (not 0, not a WER or C++ catch path):
#   1        an access violation on the main thread
#   stack    a stack overflow (the filter runs on a thread with almost no stack left)
#   threads  four threads crash at the same moment (the first wins, the others wait)
#   exit     an access violation in a static destructor after main() (the handler's state must
#            still be valid)
#
# dvv::paths::userDataDir() uses the known-folder API (FOLDERID_RoamingAppData), so the dump goes to
# the real user profile: the script reads its path from the stderr line, deletes it in every case and
# removes the "crashes" folder and its "rawreel" parent when it created them and they are empty
# (rmdir, which never removes a folder another test has just written to), so the run leaves nothing
# behind.
include("${CMAKE_CURRENT_LIST_DIR}/cli_common.cmake")

# The folders the dump may create, and whether they existed before the run.
set(candidates "")
if(NOT "$ENV{APPDATA}" STREQUAL "")
    cmake_path(CONVERT "$ENV{APPDATA}" TO_CMAKE_PATH_LIST base NORMALIZE)
    foreach(folder IN ITEMS "${base}/rawreel/crashes" "${base}/rawreel")
        list(APPEND candidates "${folder}")
        string(MD5 id "${folder}")
        if(EXISTS "${folder}")
            set(existed_${id} TRUE)
        else()
            set(existed_${id} FALSE)
        endif()
    endforeach()
endif()

set(problems "")
function(crash_run mode code_regex)
    set(ENV{RAWREEL_TEST_CRASH} "${mode}")
    # TIMEOUT: a filter that hands the process to WER may hang on a dialog.
    execute_process(COMMAND "${CLI}" --version WORKING_DIRECTORY "${WORK_DIR}" TIMEOUT 60
                    RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(problem "")
    set(dump "")
    string(REGEX MATCHALL "rawreel: crash dump [^\r\n]+" lines "${err}")
    list(LENGTH lines count)
    string(REGEX MATCH "rawreel: crash dump ([^\r\n]+)" line "${err}")
    if(line)
        cmake_path(CONVERT "${CMAKE_MATCH_1}" TO_CMAKE_PATH_LIST dump NORMALIZE)
    endif()
    if(NOT code MATCHES "${code_regex}")
        set(problem "the process did not end with the exception code (${code_regex})")
    elseif(NOT count EQUAL 1)
        set(problem "${count} 'rawreel: crash dump <path>' lines on stderr, expected 1")
    elseif(NOT EXISTS "${dump}")
        set(problem "the dump ${dump} does not exist")
    else()
        file(SIZE "${dump}" size)
        cmake_path(GET dump FILENAME name)
        if(NOT size GREATER 0)
            set(problem "the dump ${dump} is empty")
        elseif(NOT name MATCHES "^rawreel-cli-[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9][0-9]-[0-9]+\\.dmp$")
            set(problem "unexpected dump name ${name}")
        endif()
    endif()
    if(dump AND EXISTS "${dump}")
        file(REMOVE "${dump}")
    endif()
    if(problem)
        set(problems "${problems}\n[${mode}] ${problem}\nexit: ${code}\nstderr:\n${err}" PARENT_SCOPE)
    else()
        message(STATUS "[${mode}] crash dump written and removed: exit ${code}")
    endif()
endfunction()

# execute_process reports an exception exit as its text or its code.
crash_run(1 "[Aa]ccess violation|[Cc]0000005|3221225477")
crash_run(stack "[Ss]tack overflow|[Cc]00000[Ff][Dd]|3221225725")
crash_run(threads "[Aa]ccess violation|[Cc]0000005|3221225477")
crash_run(exit "[Aa]ccess violation|[Cc]0000005|3221225477")

# Remove each folder the run created, only if it is empty (cmd's rmdir fails on a non-empty one).
foreach(folder IN LISTS candidates)
    string(MD5 id "${folder}")
    if(NOT existed_${id} AND IS_DIRECTORY "${folder}")
        file(TO_NATIVE_PATH "${folder}" native)
        execute_process(COMMAND cmd /c rmdir "${native}" OUTPUT_QUIET ERROR_QUIET)
    endif()
endforeach()

if(problems)
    message(FATAL_ERROR "crash handler:${problems}")
endif()
