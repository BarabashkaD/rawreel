# Builds the v1.0 packages from a Release build tree (design M4 v2 D16-D19, D27):
#   cmake -DBUILD_DIR=<rawreel build dir> [-DCONFIG=Release] -DOUT_DIR=<dir> [-DWORK_DIR=<dir>]
#         [-DSOURCES=ON -DFFMPEG_COMMIT_FILE=<file>] -P packaging/make-packages.cmake
# Runs CPack (component rawreel), names the zip rawreel-<v>-windows-x64.zip, builds the symbols zip
# and, with SOURCES=ON when FFmpeg is bundled, the third-party sources archive; writes
# <OUT_DIR>/package-info-windows.cmake (read by package-smoke.cmake; no absolute paths).
cmake_minimum_required(VERSION 3.25)

function(fail check)
    list(JOIN ARGN "" text)
    message(FATAL_ERROR "make-packages: ${check}: ${text}")
endfunction()

foreach(required IN ITEMS BUILD_DIR OUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        fail("${required}" "required")
    endif()
endforeach()
if(NOT CONFIG)
    set(CONFIG Release)
endif()
cmake_path(ABSOLUTE_PATH BUILD_DIR NORMALIZE)
cmake_path(ABSOLUTE_PATH OUT_DIR NORMALIZE)
if(NOT WORK_DIR)
    set(WORK_DIR "${OUT_DIR}/_work")
endif()
cmake_path(ABSOLUTE_PATH WORK_DIR NORMALIZE)
include("${CMAKE_CURRENT_LIST_DIR}/check-package.cmake")
rawreel_work_dir_errors(DIR "${WORK_DIR}" PROTECT "${OUT_DIR}" "${BUILD_DIR}" "${CMAKE_CURRENT_LIST_DIR}"
                        "${CMAKE_CURRENT_BINARY_DIR}" ERRORS errors)
if(errors)
    fail("WORK_DIR" "${errors}")
endif()

set(info "${BUILD_DIR}/packaging-info-${CONFIG}.cmake")
if(NOT EXISTS "${info}")
    fail("packaging-info" "packaging-info-${CONFIG}.cmake is missing in BUILD_DIR; build ${CONFIG} "
         "of a non-sanitizer configuration whose prefix has dvv-third-party-state.cmake first")
endif()
include("${info}")
# packaging-info is generated for every configuration; the exe proves that ${CONFIG} was built.
if(NOT EXISTS "${RAWREEL_GUI_EXE}")
    fail("packaging-info" "${RAWREEL_GUI_EXE} is missing; build ${CONFIG} first")
endif()

set(version "${RAWREEL_VERSION}")
set(package "${RAWREEL_PACKAGE_NAME}.${RAWREEL_EXTENSION}")
set(symbols "rawreel-${version}-${RAWREEL_PLATFORM}-x64-symbols.zip")
set(sources "rawreel-${version}-third-party-sources.tar.xz")

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}" "${OUT_DIR}")
file(REMOVE "${OUT_DIR}/${package}" "${OUT_DIR}/${symbols}" "${OUT_DIR}/${sources}"
     "${OUT_DIR}/package-info-${RAWREEL_PLATFORM}.cmake")

# CPack, found next to the running cmake.
cmake_path(GET CMAKE_COMMAND PARENT_PATH cmake_bin)
set(suffix "")
if(CMAKE_HOST_WIN32)
    set(suffix ".exe")
endif()
set(cpack "${cmake_bin}/cpack${suffix}")
if(NOT EXISTS "${cpack}")
    fail("cpack" "cpack${suffix} is not next to ${CMAKE_COMMAND}")
endif()
execute_process(COMMAND "${cpack}" --config "${RAWREEL_CPACK_CONFIG}" -C "${CONFIG}"
                        -B "${WORK_DIR}/cpack"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
set(cpack_zip "${WORK_DIR}/cpack/rawreel-${version}.zip")
if(NOT result EQUAL 0 OR NOT EXISTS "${cpack_zip}")
    fail("cpack" "exit ${result}, no rawreel-${version}.zip:\n${output}")
endif()
file(RENAME "${cpack_zip}" "${OUT_DIR}/${package}")
set(outputs "${package}")

# The symbols zip: the two PDBs at its root.
set(symbol_names "")
foreach(pdb IN LISTS RAWREEL_PDBS)
    if(NOT EXISTS "${pdb}")
        fail("symbols" "${pdb} is missing (build ${CONFIG} with packaging/symbols.cmake)")
    endif()
    file(COPY "${pdb}" DESTINATION "${WORK_DIR}/symbols")
    cmake_path(GET pdb FILENAME name)
    list(APPEND symbol_names "${name}")
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${OUT_DIR}/${symbols}" --format=zip
                        ${symbol_names}
                WORKING_DIRECTORY "${WORK_DIR}/symbols" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    fail("symbols" "cmake -E tar failed (${result})")
endif()
list(APPEND outputs "${symbols}")

# The third-party sources archive (D27), only for a bundled FFmpeg.
set(sources_name "")
if(SOURCES)
    if(RAWREEL_BUNDLED)
        if(NOT FFMPEG_COMMIT_FILE OR NOT EXISTS "${FFMPEG_COMMIT_FILE}")
            fail("sources" "FFMPEG_COMMIT_FILE (verify-ffmpeg-archive.cmake's output) is required")
        endif()
        file(READ "${FFMPEG_COMMIT_FILE}" commit)
        string(STRIP "${commit}" commit)
        execute_process(COMMAND "${CMAKE_COMMAND}" "-DSTATE=${RAWREEL_THIRD_PARTY_STATE}"
                                "-DCOMMIT=${commit}" "-DVERSION=${version}" "-DOUT_DIR=${OUT_DIR}"
                                "-DWORK_DIR=${WORK_DIR}/sources"
                                -P "${CMAKE_CURRENT_LIST_DIR}/make-third-party-sources.cmake"
                        RESULT_VARIABLE result)
        if(NOT result EQUAL 0 OR NOT EXISTS "${OUT_DIR}/${sources}")
            fail("sources" "make-third-party-sources.cmake failed (${result})")
        endif()
        set(sources_name "${sources}")
        list(APPEND outputs "${sources}")
    else()
        message(STATUS "make-packages: FFmpeg is not bundled: no sources archive")
    endif()
endif()

# RAWREEL_LAME: the sources archive holds the LAME source release (the state names one).
include("${RAWREEL_THIRD_PARTY_STATE}")
set(lame OFF)
set(lame_file "")
if(RAWREEL_BUNDLED AND NOT "${DVV_LAME_SOURCE_URL}" STREQUAL "")
    set(lame ON)
    cmake_path(GET DVV_LAME_SOURCE_URL FILENAME lame_file)
endif()

file(WRITE "${OUT_DIR}/package-info-${RAWREEL_PLATFORM}.cmake"
"set(RAWREEL_VERSION \"${version}\")
set(RAWREEL_PLATFORM \"${RAWREEL_PLATFORM}\")
set(RAWREEL_PACKAGE \"${package}\")
set(RAWREEL_BUNDLED ${RAWREEL_BUNDLED})
set(RAWREEL_SHIPPED \"${RAWREEL_SHIPPED}\")
set(RAWREEL_FFMPEG_VERSION \"${RAWREEL_FFMPEG_VERSION}\")
set(RAWREEL_CUSTOM_FFMPEG_VERSION \"${RAWREEL_CUSTOM_FFMPEG_VERSION}\")
set(RAWREEL_SYMBOLS \"${symbols}\")
set(RAWREEL_SOURCES \"${sources_name}\")
set(RAWREEL_LAME ${lame})
set(RAWREEL_LAME_FILE \"${lame_file}\")
set(RAWREEL_LICENCE_SUPPLEMENTS \"${RAWREEL_LICENCE_SUPPLEMENTS}\")
")

foreach(name IN LISTS outputs)
    file(SIZE "${OUT_DIR}/${name}" bytes)
    file(SHA256 "${OUT_DIR}/${name}" hash)
    message(STATUS "make-packages: ${name} ${bytes} bytes SHA256 ${hash}")
endforeach()
