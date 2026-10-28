# Builds the packages from a Release build tree:
#   cmake -DBUILD_DIR=<rawreel build dir> [-DCONFIG=Release] -DOUT_DIR=<dir> [-DWORK_DIR=<dir>]
#         [-DSOURCES=ON -DFFMPEG_COMMIT_FILE=<file>] -P packaging/make-packages.cmake
# Runs CPack (component rawreel), names the package rawreel-<v>-windows-x64.zip or
# rawreel-<v>-linux-x64.tar.xz, builds the symbols archive (-symbols.zip or -symbols.tar.xz) and,
# with SOURCES=ON (Windows only) when FFmpeg is bundled, the third-party sources archive; writes
# <OUT_DIR>/package-info-<platform>.cmake (read by package-smoke.cmake; no absolute paths).
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
rawreel_package_files(VERSION "${version}" PLATFORM "${RAWREEL_PLATFORM}" PACKAGE package
                      SYMBOLS symbols CPACK cpack_name EXTENSION extension GENERATOR generator
                      ERRORS errors)
if(errors)
    fail("packaging-info" "${errors}")
endif()
set(sources "rawreel-${version}-third-party-sources.tar.xz")
# The Windows job builds the one sources archive; it serves both packages (the same custom_ffmpeg
# commit).
if(SOURCES AND NOT RAWREEL_PLATFORM STREQUAL "windows")
    fail("sources" "SOURCES=ON is for the Windows package only (its sources archive also covers "
         "the ${RAWREEL_PLATFORM} package)")
endif()

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
set(cpack_output "${WORK_DIR}/cpack/${cpack_name}")
if(NOT result EQUAL 0 OR NOT EXISTS "${cpack_output}")
    fail("cpack" "exit ${result}, no ${cpack_name}:\n${output}")
endif()
file(RENAME "${cpack_output}" "${OUT_DIR}/${package}")
set(outputs "${package}")

# The symbols archive: the exes' symbol files (the PDBs, or the .debug files on Linux) at its root.
set(symbol_names "")
foreach(symbol_file IN LISTS RAWREEL_SYMBOL_FILES)
    if(NOT EXISTS "${symbol_file}")
        fail("symbols" "${symbol_file} is missing (build ${CONFIG} with packaging/symbols.cmake)")
    endif()
    if(extension STREQUAL "zip")
        file(COPY "${symbol_file}" DESTINATION "${WORK_DIR}/symbols")
    else()
        # Mode 644 in the tar, whatever the build tree's file system reports.
        file(COPY "${symbol_file}" DESTINATION "${WORK_DIR}/symbols"
             FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
    endif()
    cmake_path(GET symbol_file FILENAME name)
    list(APPEND symbol_names "${name}")
endforeach()
if(extension STREQUAL "zip")
    set(tar_mode cf)
    set(tar_format --format=zip)
else()
    set(tar_mode cJf)
    set(tar_format "")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar ${tar_mode} "${OUT_DIR}/${symbols}" ${tar_format}
                        ${symbol_names}
                WORKING_DIRECTORY "${WORK_DIR}/symbols" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    fail("symbols" "cmake -E tar failed (${result})")
endif()
list(APPEND outputs "${symbols}")

# The third-party sources archive, only for a bundled FFmpeg.
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
# The ELF rules of package-smoke.cmake (Linux only, so the Windows file stays as it was).
if(RAWREEL_PLATFORM STREQUAL "linux")
    file(APPEND "${OUT_DIR}/package-info-${RAWREEL_PLATFORM}.cmake"
"set(RAWREEL_LINUX_SYSTEM_SONAMES \"${RAWREEL_LINUX_SYSTEM_SONAMES}\")
set(RAWREEL_LINUX_GLIBC_MAX \"${RAWREEL_LINUX_GLIBC_MAX}\")
set(RAWREEL_LINUX_GLIBCXX_MAX \"${RAWREEL_LINUX_GLIBCXX_MAX}\")
")
endif()

foreach(name IN LISTS outputs)
    file(SIZE "${OUT_DIR}/${name}" bytes)
    file(SHA256 "${OUT_DIR}/${name}" hash)
    message(STATUS "make-packages: ${name} ${bytes} bytes SHA256 ${hash}")
endforeach()
