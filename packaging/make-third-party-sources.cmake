# Builds the third-party sources archive of a bundled-FFmpeg package:
#   cmake -DSTATE=<dvv-third-party-state.cmake> -DCOMMIT=<attested commit> -DVERSION=<rawreel v>
#         -DOUT_DIR=<dir> -DWORK_DIR=<dir> -P packaging/make-third-party-sources.cmake
# Clones custom_ffmpeg at the tag v<DVV_CUSTOM_FFMPEG_VERSION> with every submodule (shallow, no
# history), checks the commit against the attestation, removes the git data, adds the LAME source
# release when the state names one and a README, and writes
# <OUT_DIR>/rawreel-<v>-third-party-sources.tar.xz. Runs on Windows.
cmake_minimum_required(VERSION 3.25)

function(fail text)
    message(FATAL_ERROR "make-third-party-sources: ${text}")
endfunction()

foreach(required IN ITEMS STATE COMMIT VERSION OUT_DIR WORK_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        fail("${required} is required")
    endif()
endforeach()
cmake_path(ABSOLUTE_PATH OUT_DIR NORMALIZE)
cmake_path(ABSOLUTE_PATH WORK_DIR NORMALIZE)
include("${CMAKE_CURRENT_LIST_DIR}/check-package.cmake")
rawreel_work_dir_errors(DIR "${WORK_DIR}" PROTECT "${OUT_DIR}" "${CMAKE_CURRENT_LIST_DIR}"
                        "${CMAKE_CURRENT_BINARY_DIR}" ERRORS errors)
if(errors)
    fail("${errors}")
endif()
include("${STATE}")
if(NOT DVV_CUSTOM_FFMPEG_URL MATCHES "^https://github\\.com/([^/]+/[^/]+)/releases/")
    fail("cannot take <owner>/<repo> from ${DVV_CUSTOM_FFMPEG_URL}")
endif()
set(REPO_URL "https://github.com/${CMAKE_MATCH_1}")
set(CF_VERSION "${DVV_CUSTOM_FFMPEG_VERSION}")
find_program(GIT git)
if(NOT GIT)
    fail("git is required")
endif()

set(name "rawreel-${VERSION}-third-party-sources")
set(stage "${WORK_DIR}/${name}")
set(clone "${stage}/custom_ffmpeg-${CF_VERSION}")
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${stage}" "${OUT_DIR}")
set(archive "${OUT_DIR}/${name}.tar.xz")
file(REMOVE "${archive}")

function(git_run out)
    execute_process(COMMAND "${GIT}" -c core.quotePath=false ${ARGN} WORKING_DIRECTORY "${clone}"
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        list(JOIN ARGN " " command)
        fail("git ${command} failed (${result}):\n${error}")
    endif()
    set(${out} "${output}" PARENT_SCOPE)
endfunction()

# core.symlinks=false: symbolic links are checked out as text files holding the target (the
# Windows default, made explicit so every host writes the same archive).
execute_process(COMMAND "${GIT}" -c core.autocrlf=false -c core.quotePath=false
                        -c core.longpaths=true -c core.symlinks=false clone --branch "v${CF_VERSION}" --depth 1 --recurse-submodules
                        --shallow-submodules "${REPO_URL}.git" "${clone}"
                RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    fail("git clone of ${REPO_URL} v${CF_VERSION} failed (${result}):\n${error}")
endif()
git_run(head rev-parse HEAD)
string(STRIP "${head}" head)
if(NOT head STREQUAL COMMIT)
    fail("the tag v${CF_VERSION} is ${head}, the attestation names ${COMMIT}")
endif()

git_run(status submodule status --recursive)
string(REGEX REPLACE "\n$" "" status "${status}")
set(SUBMODULES "")
if(status STREQUAL "")
    set(SUBMODULES "    none")
else()
    string(REPLACE "\n" ";" lines "${status}")
    foreach(line IN LISTS lines)
        string(STRIP "${line}" line)
        string(APPEND SUBMODULES "    ${line}\n")
    endforeach()
    string(REGEX REPLACE "\n$" "" SUBMODULES "${SUBMODULES}")
endif()

git_run(index ls-files -s --recurse-submodules)
string(REPLACE "\n" ";" lines "${index}")
set(SYMLINKS "")
foreach(line IN LISTS lines)
    if(line MATCHES "^[0-9]+ [0-9a-f]+ [0-9]+\t\"")
        fail("git quotes the path in '${line}' (a newline, tab, quote or backslash)")
    endif()
    if(line MATCHES "^120000 [0-9a-f]+ [0-9]+\t(.+)$")
        set(path "${CMAKE_MATCH_1}")
        file(READ "${clone}/${path}" target)
        string(STRIP "${target}" target)
        string(APPEND SYMLINKS "    ${path} -> ${target}\n")
    endif()
endforeach()
if(SYMLINKS STREQUAL "")
    set(SYMLINKS "    none")
else()
    string(REGEX REPLACE "\n$" "" SYMLINKS "${SYMLINKS}")
endif()

# Every submodule must be checked out (git prints each path on its own line, spaces included).
git_run(displaypaths submodule foreach --recursive --quiet "echo $displaypath")
string(REGEX REPLACE "\n$" "" displaypaths "${displaypaths}")
if(NOT displaypaths STREQUAL "")
    string(REPLACE "\n" ";" displaypaths "${displaypaths}")
endif()
foreach(path IN LISTS displaypaths)
    file(GLOB entries "${clone}/${path}/*")
    if(entries STREQUAL "")
        fail("the submodule ${path} is empty after the clone")
    endif()
endforeach()

# No history in the archive: every entry named exactly .git goes (the clone's .git folder and
# each submodule's .git file; git tracks no file of that name). Matched on the file name, so a
# folder such as .github stays.
file(GLOB_RECURSE entries LIST_DIRECTORIES true "${clone}/*")
list(FILTER entries INCLUDE REGEX "/\\.git$")
foreach(entry IN LISTS entries)
    file(REMOVE_RECURSE "${entry}")
endforeach()
file(REMOVE_RECURSE "${clone}/.git")
file(GLOB_RECURSE entries LIST_DIRECTORIES true "${clone}/*")
list(FILTER entries INCLUDE REGEX "/\\.git(/|$)")
if(entries)
    fail("git data is left in the clone: ${entries}")
endif()

set(LAME_LINE "")
set(LGPL_COMPONENTS "FFmpeg")
if(NOT "${DVV_LAME_SOURCE_URL}" STREQUAL "")
    set(LGPL_COMPONENTS "FFmpeg and LAME")
    cmake_path(GET DVV_LAME_SOURCE_URL FILENAME LAME_FILE)
    if(NOT LAME_FILE MATCHES "^lame-([0-9][0-9.]*[0-9])\\.tar\\.gz$")
        fail("DVV_LAME_SOURCE_URL does not name a lame-<version>.tar.gz file: ${LAME_FILE}")
    endif()
    set(LAME_VERSION "${CMAKE_MATCH_1}")
    file(DOWNLOAD "${DVV_LAME_SOURCE_URL}" "${stage}/${LAME_FILE}"
         EXPECTED_HASH SHA256=${DVV_LAME_SOURCE_SHA256} TLS_VERIFY ON STATUS lame_status)
    list(GET lame_status 0 code)
    if(NOT code EQUAL 0)
        fail("download of ${LAME_FILE} failed: ${lame_status}")
    endif()
    string(TOLOWER "${DVV_LAME_SOURCE_SHA256}" LAME_SHA256)
    string(CONCAT LAME_LINE "- ${LAME_FILE}: the LAME ${LAME_VERSION} source release that "
                  "custom_ffmpeg builds (SHA256 ${LAME_SHA256}).\n")
endif()

file(CONFIGURE OUTPUT "${stage}/README.txt" @ONLY NEWLINE_STYLE LF CONTENT
"Third-party sources for rawreel @VERSION@

The complete corresponding source of the LGPL components (@LGPL_COMPONENTS@) of the FFmpeg
libraries in the rawreel @VERSION@ packages (folder ffmpeg/ in the Windows zip and the Linux
tar.xz; both are built from the same custom_ffmpeg commit). Not included: the AMD AMF public
headers (MIT) that avcodec's AMF support is built against; their URL and SHA256 are in
custom_ffmpeg-@CF_VERSION@/versions.cmake.
- custom_ffmpeg-@CF_VERSION@/: custom_ffmpeg @CF_VERSION@, tag v@CF_VERSION@, commit @COMMIT@, with
  every submodule and the build scripts (the configure line included). Submodules:
@SUBMODULES@
  Symbolic links of the tree, stored here as text files that hold the link target:
@SYMLINKS@
@LAME_LINE@Upstream: @REPO_URL@
")

execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cJf "${archive}" "${name}"
                WORKING_DIRECTORY "${WORK_DIR}" RESULT_VARIABLE result)
if(NOT result EQUAL 0 OR NOT EXISTS "${archive}")
    fail("cmake -E tar failed (${result})")
endif()
file(SIZE "${archive}" bytes)
message(STATUS "make-third-party-sources: ${name}.tar.xz ${bytes} bytes")
