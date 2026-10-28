# Local dry run of the release build, on Windows or on Linux:
#   cmake [-DWORKSPACE_ROOT=<root>] -DBUNDLE=ON|OFF [-DOWN_PREFIX=ON|OFF] [-DSOURCES=ON]
#         [-DGUI_SMOKE=off|windows|linux] [-DEXTRA_TERMS=<term>;...] [-DDRIVE=<letter>:]
#         [-DDRYRUN_ROOT=<dir>] [-DWINDOWS_PREFIX=<dir>] -P <clone>/packaging/dry-run.cmake
# <clone> is the rawreel clone (any folder name) with its submodules in libs/; WORKSPACE_ROOT is
# the folder that holds it (default: its parent). The developer prefix is
# <clone>/libs/_third_party/<toolchain>, the superbuild source <clone>/libs/dvv-build/deps.
# Windows: maps WORKSPACE_ROOT to a free drive letter with subst (a neutral root, as a CI runner
# has) and works below <drive>/_dryrun. The subst drive is removed even when the inner run fails;
# after a Ctrl+C or a killed run, remove it with subst <drive> /D.
# Linux: no subst; the build, package and smoke trees live below DRYRUN_ROOT (default
# /tmp/rawreel-dryrun), a neutral folder on a Linux file system, so the file modes are real (a
# Windows drive mounted in WSL reports every file as 777) and no home folder path enters the build.
# Both build rawreel Release in a separate tree, run the unit and CLI tests of release.yml, build
# the packages and run package-smoke.cmake on them. On Linux with BUNDLE=ON the FFmpeg archive of
# the clang prefix and the one of the Windows prefix (WINDOWS_PREFIX, default
# <clone>/libs/_third_party/msvc) are verified, and both must name the same custom_ffmpeg commit.
# The packages are local test artifacts; only the release workflow builds published ones.
cmake_minimum_required(VERSION 3.25)

if(NOT CMAKE_HOST_WIN32 AND NOT CMAKE_HOST_LINUX)
    message(FATAL_ERROR "dry-run: Windows or Linux only")
endif()
# The clone is the folder above packaging/; its name is not fixed.
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH clone_dir)
cmake_path(GET clone_dir FILENAME clone)
if(NOT INNER)
    if(NOT WORKSPACE_ROOT)
        cmake_path(GET clone_dir PARENT_PATH WORKSPACE_ROOT)
    endif()
    cmake_path(ABSOLUTE_PATH WORKSPACE_ROOT NORMALIZE)
    set(clone_real "")
    if(IS_DIRECTORY "${WORKSPACE_ROOT}/${clone}")
        file(REAL_PATH "${WORKSPACE_ROOT}/${clone}" clone_real)
    endif()
    file(REAL_PATH "${clone_dir}" clone_dir_real)
    if(NOT clone_real STREQUAL clone_dir_real)
        message(FATAL_ERROR "dry-run: WORKSPACE_ROOT ${WORKSPACE_ROOT} is not the folder holding "
                            "this clone (${clone_dir}); omit it or pass the clone's parent folder")
    endif()
endif()
if(NOT IS_DIRECTORY "${clone_dir}/libs/dvv-build/deps")
    message(FATAL_ERROR "dry-run: ${clone_dir}/libs/dvv-build is missing: "
                        "run git submodule update --init in the clone")
endif()
if(CMAKE_HOST_WIN32 AND NOT INNER)
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
                            -P "${DRIVE}/${clone}/packaging/dry-run.cmake"
                    RESULT_VARIABLE inner_result)
    execute_process(COMMAND subst "${DRIVE}" /D)
    if(NOT inner_result EQUAL 0)
        message(FATAL_ERROR "dry-run: failed (${inner_result})")
    endif()
    return()
endif()

# Inner part (Windows) or the whole run (Linux).
function(step name)
    message(STATUS "dry-run: ${name}")
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "dry-run: ${name} failed (${result})")
    endif()
endfunction()

set(N "${WORKSPACE_ROOT}")
string(REGEX REPLACE "/+$" "" N "${N}")
if(CMAKE_HOST_WIN32)
    set(platform windows)
    set(toolchain msvc)
    set(preset vs2026)
    set(area "${N}/_dryrun")
    set(area_name "_dryrun")
else()
    set(platform linux)
    set(toolchain clang)
    set(preset ninja-clang)
    set(REAL_ROOT "${N}")
    if(NOT DRYRUN_ROOT)
        set(DRYRUN_ROOT "/tmp/rawreel-dryrun")
    endif()
    cmake_path(ABSOLUTE_PATH DRYRUN_ROOT NORMALIZE)
    string(REGEX REPLACE "/+$" "" area "${DRYRUN_ROOT}")
    set(area_name "${area}")
    if(SOURCES)
        message(FATAL_ERROR "dry-run: SOURCES=ON is for the Windows dry run (its sources archive "
                            "covers the Linux package too)")
    endif()
endif()
if(BUNDLE)
    set(BUNDLE ON)
    set(variant ${toolchain}-on)
else()
    set(BUNDLE OFF)
    set(variant ${toolchain}-off)
endif()
if(platform STREQUAL "linux" AND BUNDLE)
    # The FFmpeg archive of the Windows prefix is verified too (below): stop before the build when
    # that prefix is not there, as in a Linux-only clone.
    if(NOT WINDOWS_PREFIX)
        set(WINDOWS_PREFIX "${N}/${clone}/libs/_third_party/msvc")
    endif()
    if(NOT EXISTS "${WINDOWS_PREFIX}/dvv-third-party-state.cmake")
        message(FATAL_ERROR "dry-run: with BUNDLE=ON on Linux the FFmpeg archive of the Windows "
                            "prefix is verified too, but ${WINDOWS_PREFIX} is not a bootstrapped "
                            "prefix: pass -DWINDOWS_PREFIX=<a bootstrapped msvc prefix> or use "
                            "BUNDLE=OFF")
    endif()
endif()
if(CMAKE_HOST_WIN32)
    # The linker writes temporary objects (lnk{GUID}.tmp, the embedded manifest) to TMP and the PDB
    # records their paths: a temporary folder below the neutral root keeps user-profile paths out of
    # the symbols (release.yml sets TMP and TEMP to runner.temp for the same reason).
    file(MAKE_DIRECTORY "${area}/tmp")
    cmake_path(NATIVE_PATH area area_native)
    set(ENV{TMP} "${area_native}\\tmp")
    set(ENV{TEMP} "${area_native}\\tmp")
endif()
# A check of the MSVC prefix found no user-home paths in its wx libraries: the developer prefix
# is reused by default.
if(NOT DEFINED OWN_PREFIX)
    set(OWN_PREFIX OFF)
endif()
if(NOT GUI_SMOKE)
    set(GUI_SMOKE off)
endif()

# Prefix.
set(developer_state
    "${REAL_ROOT}/${clone}/libs/_third_party/${toolchain}/dvv-third-party-state.cmake")
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
    set(prefix "${N}/${clone}/libs/_third_party/${toolchain}")
    message(STATUS "dry-run: prefix: the developer prefix "
                   "(${clone}/libs/_third_party/${toolchain})")
else()
    set(prefix "${area}/third_party/${variant}")
    set(prefix_build "${area}/build/third-party-${variant}")
    message(STATUS "dry-run: prefix: its own (${area_name}/third_party/${variant})")
    if(NOT EXISTS "${prefix}/dvv-third-party-state.cmake")
        file(REMOVE_RECURSE "${prefix_build}")
    endif()
    if(CMAKE_HOST_WIN32)
        set(generator -G "Visual Studio 18 2026" -A x64)
    else()
        # The compilers of bootstrap.sh --toolchain clang.
        set(generator -G Ninja -DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19)
    endif()
    # Configured on every run, so the state file follows deps/; the build is incremental. The
    # project include gives the short dependency trees of bootstrap.
    set(dvv_build "${N}/${clone}/libs/dvv-build")
    step("superbuild configure" "${CMAKE_COMMAND}" -S "${dvv_build}/deps"
         -B "${prefix_build}" "-DDVV_THIRD_PARTY_PREFIX=${prefix}"
         "-DCMAKE_PROJECT_INCLUDE=${dvv_build}/cmake/DvvEpBase.cmake"
         "-DDVV_BUNDLE_FFMPEG=${BUNDLE}" ${generator})
    step("superbuild build" "${CMAKE_COMMAND}" --build "${prefix_build}" --config Release
         --parallel)
endif()

# rawreel Release in a tree of its own (the developer tree's cache names the real path). --fresh:
# the tree is shared by both prefixes and by every drive letter, and a cached find_package result
# of an earlier run must not mix two prefixes.
set(build "${area}/build/rawreel-${variant}")
message(STATUS "dry-run: rawreel configure")
execute_process(COMMAND "${CMAKE_COMMAND}" --preset ${preset} --fresh -B "${build}"
                        "-DCMAKE_PREFIX_PATH=${prefix}"
                WORKING_DIRECTORY "${N}/${clone}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "dry-run: rawreel configure failed (${result})")
endif()
step("rawreel build" "${CMAKE_COMMAND}" --build "${build}" --config Release --parallel)
step("rawreel tests" "${CMAKE_COMMAND}" -E env DVV_LEAK_CHECK=fail DVV_REQUIRE_FFMPEG=1
     ctest --test-dir "${build}" -C Release -LE "gpu|display|gui|oracle" --output-on-failure)

# Packages.
set(out "${area}/out/${variant}")
set(packaging "${N}/${clone}/packaging")
if(SOURCES)
    set(check "${area}/ffmpeg-check")
    step("verify-ffmpeg-archive" "${CMAKE_COMMAND}" "-DPREFIX=${prefix}" "-DWORK_DIR=${check}"
         -P "${packaging}/verify-ffmpeg-archive.cmake")
    step("make-packages" "${CMAKE_COMMAND}" "-DBUILD_DIR=${build}" -DCONFIG=Release
         "-DOUT_DIR=${out}" -DSOURCES=ON "-DFFMPEG_COMMIT_FILE=${check}/custom_ffmpeg-commit.txt"
         -P "${packaging}/make-packages.cmake")
    set(require_sources ON)
else()
    if(platform STREQUAL "linux" AND BUNDLE)
        # One custom_ffmpeg commit for both packages: the Windows job's sources archive covers the
        # Linux package too. WINDOWS_PREFIX is set and checked above.
        set(check "${area}/ffmpeg-check")
        step("verify-ffmpeg-archive (linux)" "${CMAKE_COMMAND}" "-DPREFIX=${prefix}"
             "-DWORK_DIR=${check}/linux" -P "${packaging}/verify-ffmpeg-archive.cmake")
        step("verify-ffmpeg-archive (windows)" "${CMAKE_COMMAND}" "-DPREFIX=${WINDOWS_PREFIX}"
             "-DWORK_DIR=${check}/windows" -P "${packaging}/verify-ffmpeg-archive.cmake")
        file(READ "${check}/linux/custom_ffmpeg-commit.txt" linux_commit)
        file(READ "${check}/windows/custom_ffmpeg-commit.txt" windows_commit)
        string(STRIP "${linux_commit}" linux_commit)
        string(STRIP "${windows_commit}" windows_commit)
        if(NOT linux_commit STREQUAL windows_commit)
            message(FATAL_ERROR "dry-run: the Linux FFmpeg archive is built from ${linux_commit}, "
                                "the Windows one from ${windows_commit}")
        endif()
        message(STATUS "dry-run: custom_ffmpeg commit ${linux_commit} in both prefixes")
    endif()
    step("make-packages" "${CMAKE_COMMAND}" "-DBUILD_DIR=${build}" -DCONFIG=Release
         "-DOUT_DIR=${out}" -P "${packaging}/make-packages.cmake")
    set(require_sources OFF)
endif()
# Called directly, not through step(): EXTRA_TERMS holds ';' and must stay one argument.
message(STATUS "dry-run: package-smoke")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DOUT_DIR=${out}" -DPLATFORM=${platform}
                        "-DWORK_DIR=${area}/smoke/${variant}" "-DGUI_SMOKE=${GUI_SMOKE}"
                        "-DEXTRA_TERMS=${EXTRA_TERMS}" "-DREQUIRE_SOURCES=${require_sources}"
                        -P "${packaging}/package-smoke.cmake"
                RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "dry-run: package-smoke failed (${result})")
endif()

# Only this run's files: the output folder can still hold packages of older runs.
include("${packaging}/check-package.cmake")
rawreel_package_outputs(INFO "${out}/package-info-${platform}.cmake" FILES outputs)
foreach(name IN LISTS outputs)
    file(SIZE "${out}/${name}" bytes)
    file(SHA256 "${out}/${name}" hash)
    message(STATUS "dry-run: ${name} ${bytes} bytes SHA256 ${hash}")
endforeach()
