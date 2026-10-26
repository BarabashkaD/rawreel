# Local dry run of the Windows release build (design M4 v2 D16; Windows only):
#   cmake -DWORKSPACE_ROOT=<workspace> -DBUNDLE=ON|OFF [-DOWN_PREFIX=ON|OFF] [-DSOURCES=ON]
#         [-DGUI_SMOKE=off|windows] [-DEXTRA_TERMS=<term>;...] [-DDRIVE=<letter>:]
#         -P rawreel/packaging/dry-run.cmake
# Maps the workspace to a free drive letter with subst (a neutral root, as a CI runner has), builds
# rawreel Release in a separate tree below <drive>/_dryrun, runs the unit and CLI tests of
# release.yml, builds the packages and runs package-smoke.cmake on them. The subst drive is removed
# even when the inner run fails; after a Ctrl+C or a killed run, remove it with subst <drive> /D.
cmake_minimum_required(VERSION 3.25)

if(NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR "dry-run: Windows only in v1.0 (the Linux package is v1.x)")
endif()
if(NOT INNER)
    if(NOT WORKSPACE_ROOT)
        message(FATAL_ERROR "dry-run: WORKSPACE_ROOT (the folder holding rawreel/) is required")
    endif()
    cmake_path(ABSOLUTE_PATH WORKSPACE_ROOT NORMALIZE)
    if(NOT IS_DIRECTORY "${WORKSPACE_ROOT}/rawreel")
        message(FATAL_ERROR "dry-run: WORKSPACE_ROOT (the folder holding rawreel/) is required")
    endif()
    if(NOT DRIVE)
        foreach(letter IN ITEMS R S T U V W Y Z)
            if(NOT IS_DIRECTORY "${letter}:/")
                set(DRIVE "${letter}:")
                break()
            endif()
        endforeach()
    endif()
    if(NOT DRIVE)
        message(FATAL_ERROR "dry-run: no free drive letter for subst; pass -DDRIVE=<letter>:")
    endif()
    cmake_path(NATIVE_PATH WORKSPACE_ROOT root_native)
    execute_process(COMMAND subst "${DRIVE}" "${root_native}" RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "dry-run: subst ${DRIVE} failed")
    endif()
    set(forward "")
    foreach(name IN ITEMS BUNDLE OWN_PREFIX SOURCES GUI_SMOKE)
        if(DEFINED ${name})
            list(APPEND forward "-D${name}=${${name}}")
        endif()
    endforeach()
    # EXTRA_TERMS holds ';': passed as one quoted argument, never through a list.
    execute_process(COMMAND "${CMAKE_COMMAND}" -DINNER=ON "-DWORKSPACE_ROOT=${DRIVE}/"
                            "-DREAL_ROOT=${WORKSPACE_ROOT}" ${forward}
                            "-DEXTRA_TERMS=${EXTRA_TERMS}"
                            -P "${DRIVE}/rawreel/packaging/dry-run.cmake"
                    RESULT_VARIABLE inner_result)
    execute_process(COMMAND subst "${DRIVE}" /D)
    if(NOT inner_result EQUAL 0)
        message(FATAL_ERROR "dry-run: failed (${inner_result})")
    endif()
    return()
endif()

# Inner part: every path below the neutral root N.
function(step name)
    message(STATUS "dry-run: ${name}")
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "dry-run: ${name} failed (${result})")
    endif()
endfunction()

set(N "${WORKSPACE_ROOT}")
string(REGEX REPLACE "/+$" "" N "${N}")
set(area "${N}/_dryrun")
if(BUNDLE)
    set(BUNDLE ON)
    set(variant on)
else()
    set(BUNDLE OFF)
    set(variant off)
endif()
# The linker writes temporary objects (lnk{GUID}.tmp, the embedded manifest) to TMP and the PDB
# records their paths: a temporary folder below the neutral root keeps user-profile paths out of the
# symbols (release.yml sets TMP and TEMP to runner.temp for the same reason).
file(MAKE_DIRECTORY "${area}/tmp")
cmake_path(NATIVE_PATH area area_native)
set(ENV{TMP} "${area_native}\\tmp")
set(ENV{TEMP} "${area_native}\\tmp")
# Task 0 found no user-home paths in the MSVC prefix's wx libraries: the developer prefix is reused
# by default.
if(NOT DEFINED OWN_PREFIX)
    set(OWN_PREFIX OFF)
endif()
if(NOT GUI_SMOKE)
    set(GUI_SMOKE off)
endif()

# Prefix.
set(developer_state "${REAL_ROOT}/_third_party/msvc/dvv-third-party-state.cmake")
set(developer_bundle "")
if(EXISTS "${developer_state}")
    include("${developer_state}")
    if(DVV_BUNDLE_FFMPEG)
        set(developer_bundle ON)
    else()
        set(developer_bundle OFF)
    endif()
endif()
if(NOT OWN_PREFIX AND developer_bundle STREQUAL BUNDLE)
    set(prefix "${N}/_third_party/msvc")
    message(STATUS "dry-run: prefix: the developer prefix (_third_party/msvc)")
else()
    set(prefix "${area}/third_party/msvc-${variant}")
    set(prefix_build "${area}/build/third-party-msvc-${variant}")
    message(STATUS "dry-run: prefix: its own (_dryrun/third_party/msvc-${variant})")
    if(NOT EXISTS "${prefix}/dvv-third-party-state.cmake")
        file(REMOVE_RECURSE "${prefix_build}")
    endif()
    # Configured on every run, so the state file follows deps/; the build is incremental.
    step("superbuild configure" "${CMAKE_COMMAND}" -S "${N}/rawreel-workspace/deps"
         -B "${prefix_build}" "-DDVV_THIRD_PARTY_PREFIX=${prefix}"
         "-DDVV_BUNDLE_FFMPEG=${BUNDLE}" -G "Visual Studio 18 2026" -A x64)
    step("superbuild build" "${CMAKE_COMMAND}" --build "${prefix_build}" --config Release
         --parallel)
endif()

# rawreel Release in a tree of its own (the developer tree's cache names the real path). --fresh:
# the tree is shared by both prefixes and by every drive letter, and a cached find_package result
# of an earlier run must not mix two prefixes.
set(build "${area}/build/rawreel-msvc-${variant}")
message(STATUS "dry-run: rawreel configure")
execute_process(COMMAND "${CMAKE_COMMAND}" --preset vs2026 --fresh -B "${build}"
                        "-DCMAKE_PREFIX_PATH=${prefix}"
                WORKING_DIRECTORY "${N}/rawreel" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "dry-run: rawreel configure failed (${result})")
endif()
step("rawreel build" "${CMAKE_COMMAND}" --build "${build}" --config Release --parallel)
step("rawreel tests" "${CMAKE_COMMAND}" -E env DVV_LEAK_CHECK=fail DVV_REQUIRE_FFMPEG=1
     ctest --test-dir "${build}" -C Release -LE "gpu|display|gui|oracle" --output-on-failure)

# Packages.
set(out "${area}/out/msvc-${variant}")
set(packaging "${N}/rawreel/packaging")
if(SOURCES)
    set(check "${area}/ffmpeg-check")
    step("verify-ffmpeg-archive" "${CMAKE_COMMAND}" "-DPREFIX=${prefix}" "-DWORK_DIR=${check}"
         -P "${packaging}/verify-ffmpeg-archive.cmake")
    step("make-packages" "${CMAKE_COMMAND}" "-DBUILD_DIR=${build}" -DCONFIG=Release
         "-DOUT_DIR=${out}" -DSOURCES=ON "-DFFMPEG_COMMIT_FILE=${check}/custom_ffmpeg-commit.txt"
         -P "${packaging}/make-packages.cmake")
    set(require_sources ON)
else()
    step("make-packages" "${CMAKE_COMMAND}" "-DBUILD_DIR=${build}" -DCONFIG=Release
         "-DOUT_DIR=${out}" -P "${packaging}/make-packages.cmake")
    set(require_sources OFF)
endif()
# Called directly, not through step(): EXTRA_TERMS holds ';' and must stay one argument.
message(STATUS "dry-run: package-smoke")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DOUT_DIR=${out}" -DPLATFORM=windows
                        "-DWORK_DIR=${area}/smoke/msvc-${variant}" "-DGUI_SMOKE=${GUI_SMOKE}"
                        "-DEXTRA_TERMS=${EXTRA_TERMS}" "-DREQUIRE_SOURCES=${require_sources}"
                        -P "${packaging}/package-smoke.cmake"
                RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "dry-run: package-smoke failed (${result})")
endif()

file(GLOB outputs "${out}/rawreel-*")
foreach(file IN LISTS outputs)
    cmake_path(GET file FILENAME name)
    file(SIZE "${file}" bytes)
    file(SHA256 "${file}" hash)
    message(STATUS "dry-run: ${name} ${bytes} bytes SHA256 ${hash}")
endforeach()
