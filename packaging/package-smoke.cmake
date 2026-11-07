# Package smoke test: checks the outputs of make-packages.cmake.
#   cmake -DOUT_DIR=<make-packages output> -DPLATFORM=windows|linux|macos -DWORK_DIR=<dir>
#         [-DEXPECTED_VERSION=<v>] [-DGUI_SMOKE=off|windows|linux|macos] [-DALLOW_GUI_SKIP=ON]
#         [-DEXTRA_TERMS=<term>;...] -P packaging/package-smoke.cmake
# Each step prints "-- package-smoke: <step>: ok"; the first failure stops with FATAL_ERROR.
# PLATFORM linux runs on a Linux host with readelf, ldd and GNU tar. GUI_SMOKE=linux needs a display from the
# caller (xvfb-run in CI, the desktop's X server locally); the GUI and CLI runs unset
# LD_LIBRARY_PATH, which the loader searches before RUNPATH and so could hide a broken bundle.
# PLATFORM macos runs on a Mac with the Command Line Tools. The .dmg is mounted read-only at
# "<WORK_DIR>/rawreel 2/Загрузки" (a space and non-ASCII characters, as a downloaded app or
# Gatekeeper's app translocation has; Review Focus 2) and everything runs from there with the
# working folder /. The CLI and GUI runs unset the DYLD_* search variables and the Vulkan
# loader's driver and layer variables, so the app finds FFmpeg, the loader and KosmicKrisp on its
# own, and set HOME to a folder below WORK_DIR, so the app's Application Support (config, log)
# is a fresh one. The loaded-image audit (U-MAC8) runs the CLI and the GUI with
# DYLD_PRINT_LIBRARIES=1 and VK_LOADER_DEBUG=driver: every image is inside the app, /usr/lib or
# /System, the driver is the bundled KosmicKrisp, nothing comes from a Vulkan SDK in the home
# folder; the GUI runs once more through `open` (LaunchServices, as from Finder; Focus 4),
# checked from its output files and its log file. ALLOW_GUI_SKIP=ON also accepts a Mac without a
# usable GPU (rawreel-cli's Vulkan line then says "not available").
cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/check-package.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/scan-artifact-privacy.cmake")

# macOS: whether <path> is a mount point (in mount's list).
function(mounted path out)
    set(${out} OFF PARENT_SCOPE)
    if(NOT IS_DIRECTORY "${path}")
        return()
    endif()
    file(REAL_PATH "${path}" real)
    execute_process(COMMAND /sbin/mount RESULT_VARIABLE result OUTPUT_VARIABLE table ERROR_QUIET)
    string(FIND "${table}" " on ${real} (" at)
    if(result EQUAL 0 AND NOT at EQUAL -1)
        set(${out} ON PARENT_SCOPE)
    endif()
endfunction()
# macOS: ejects the .dmg mounted at <path>, if it is.
function(eject path)
    mounted("${path}" is_mounted)
    if(is_mounted)
        execute_process(COMMAND /usr/sbin/diskutil eject "${path}" RESULT_VARIABLE result
                        OUTPUT_VARIABLE output ERROR_VARIABLE output)
        if(NOT result EQUAL 0)
            message(WARNING "package-smoke: diskutil eject ${path} failed (${result}): ${output}")
        endif()
    endif()
endfunction()

function(fail step)
    list(JOIN ARGN "" text)
    # A failed macOS smoke leaves no mounted image behind.
    get_property(mount GLOBAL PROPERTY rawreel_smoke_mount)
    if(NOT "${mount}" STREQUAL "")
        set_property(GLOBAL PROPERTY rawreel_smoke_mount "")
        eject("${mount}")
    endif()
    message(FATAL_ERROR "package-smoke: ${step}: ${text}")
endfunction()
function(ok step)
    message(STATUS "package-smoke: ${step}: ok")
endfunction()
function(fail_list step errors)
    list(JOIN errors "\n  " text)
    fail("${step}" "\n  ${text}")
endfunction()

# 1. Platform and package information.
if(NOT PLATFORM MATCHES "^(windows|linux|macos)$")
    fail("platform" "PLATFORM must be windows, linux or macos")
endif()
foreach(required IN ITEMS OUT_DIR WORK_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        fail("platform" "${required} is required")
    endif()
endforeach()
cmake_path(ABSOLUTE_PATH OUT_DIR NORMALIZE)
cmake_path(ABSOLUTE_PATH WORK_DIR NORMALIZE)
rawreel_work_dir_errors(DIR "${WORK_DIR}" PROTECT "${OUT_DIR}" "${CMAKE_CURRENT_LIST_DIR}"
                        "${CMAKE_CURRENT_BINARY_DIR}" ERRORS errors)
if(errors)
    fail_list("platform" "${errors}")
endif()
if(NOT GUI_SMOKE)
    set(GUI_SMOKE off)
endif()
if(NOT GUI_SMOKE STREQUAL "off" AND NOT GUI_SMOKE STREQUAL PLATFORM)
    fail("platform" "GUI_SMOKE must be off or ${PLATFORM}")
endif()
set(info "${OUT_DIR}/package-info-${PLATFORM}.cmake")
if(NOT EXISTS "${info}")
    fail("platform" "package-info-${PLATFORM}.cmake is missing in OUT_DIR (run make-packages.cmake)")
endif()
include("${info}")
set(version "${RAWREEL_VERSION}")
if(DEFINED EXPECTED_VERSION AND NOT EXPECTED_VERSION STREQUAL version)
    fail("platform" "the package version is ${version}, expected ${EXPECTED_VERSION}")
endif()
set(exe_suffix ".exe")
set(no_ld_path "")
if(PLATFORM STREQUAL "linux")
    set(exe_suffix "")
    set(no_ld_path --unset=LD_LIBRARY_PATH)
    foreach(required IN ITEMS RAWREEL_LINUX_SYSTEM_SONAMES RAWREEL_LINUX_GLIBC_MAX
                              RAWREEL_LINUX_GLIBCXX_MAX)
        if("${${required}}" STREQUAL "")
            fail("platform" "package-info-linux.cmake has no ${required} (run make-packages.cmake)")
        endif()
    endforeach()
    find_program(readelf NAMES readelf llvm-readelf llvm-readelf-19)
    find_program(ldd NAMES ldd)
    find_program(gnu_tar NAMES tar)
    if(NOT readelf OR NOT ldd OR NOT gnu_tar)
        fail("platform" "readelf, ldd and tar are required on PATH (binutils, libc-bin, tar)")
    endif()
elseif(PLATFORM STREQUAL "macos")
    set(exe_suffix "")
    # The system's tools (Command Line Tools shims for otool), never another one on PATH.
    foreach(tool IN ITEMS otool codesign plutil open touch pkill diskutil)
        find_program(${tool}_path ${tool} PATHS /usr/bin /usr/sbin NO_DEFAULT_PATH NO_CACHE)
        if(NOT ${tool}_path)
            fail("platform" "${tool} is missing in /usr/bin and /usr/sbin (install the Xcode "
                 "Command Line Tools)")
        endif()
    endforeach()
    # The counterpart of LD_LIBRARY_PATH (dyld's search variables) and the Vulkan loader's driver
    # and layer variables (a VK_DRIVER_FILES of the shell would be respected and hide the bundled
    # driver); HOME is a fresh folder, so no rawreel.json of the developer applies.
    set(no_ld_path --unset=DYLD_LIBRARY_PATH --unset=DYLD_FALLBACK_LIBRARY_PATH
                   --unset=DYLD_FRAMEWORK_PATH --unset=DYLD_FALLBACK_FRAMEWORK_PATH
                   --unset=DYLD_INSERT_LIBRARIES --unset=DYLD_PRINT_LIBRARIES
                   --unset=VK_DRIVER_FILES --unset=VK_ICD_FILENAMES --unset=VK_ADD_DRIVER_FILES
                   --unset=VK_LAYER_PATH --unset=VK_ADD_LAYER_PATH --unset=VK_INSTANCE_LAYERS
                   --unset=VK_LOADER_LAYERS_ENABLE --unset=VK_LOADER_DEBUG
                   "HOME=${WORK_DIR}/home")
    # Where a user-level LunarG Vulkan SDK lives (its installer's default, or VULKAN_SDK): the
    # audit also names it when anything comes from there.
    set(vulkan_sdk_roots "$ENV{HOME}/VulkanSDK")
    if(NOT "$ENV{VULKAN_SDK}" STREQUAL "")
        list(APPEND vulkan_sdk_roots "$ENV{VULKAN_SDK}")
    endif()
endif()
ok("platform")

# 2. Extract (macOS: mount read-only).
set(extract "${WORK_DIR}/extract")
set(mount "${WORK_DIR}/rawreel 2/Загрузки")
if(PLATFORM STREQUAL "macos")
    eject("${mount}") # left mounted by a run that was killed
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
if(PLATFORM STREQUAL "macos")
    file(MAKE_DIRECTORY "${mount}")
    # diskutil image attach: hdiutil attach still works on macOS 27 but is deprecated in its
    # favour. --nobrowse keeps the volume out of Finder.
    execute_process(COMMAND "${diskutil_path}" image attach --readOnly --nobrowse
                            --mountPoint "${mount}" "${OUT_DIR}/${RAWREEL_PACKAGE}"
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    mounted("${mount}" is_mounted)
    if(NOT result EQUAL 0 OR NOT is_mounted)
        fail("mount" "diskutil image attach ${RAWREEL_PACKAGE} failed (${result}):\n${output}")
    endif()
    set_property(GLOBAL PROPERTY rawreel_smoke_mount "${mount}")
    # dyld and the Vulkan loader print real paths (/private/tmp, not /tmp).
    file(REAL_PATH "${mount}" pkg_root)
    # Nothing can be written into the app (Review Focus 2).
    execute_process(COMMAND "${touch_path}" "${pkg_root}/rawreel.app/Contents/.write-test"
                    RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
    if(result EQUAL 0)
        fail("mount" "the mounted .dmg is writable")
    endif()
    set(app "${pkg_root}/rawreel.app")
    set(contents "${app}/Contents")
    set(exe_dir "${contents}/MacOS")
    set(licences_dir "${contents}/Resources/ffmpeg/licenses")
    set(cwd_args WORKING_DIRECTORY /)
    ok("mount")
else()
    file(MAKE_DIRECTORY "${extract}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${OUT_DIR}/${RAWREEL_PACKAGE}"
                    WORKING_DIRECTORY "${extract}" RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        fail("extract" "cmake -E tar xf ${RAWREEL_PACKAGE} failed (${result})")
    endif()
    file(GLOB top LIST_DIRECTORIES true RELATIVE "${extract}" "${extract}/*")
    if(NOT top STREQUAL "rawreel-${version}")
        fail("extract" "the archive must hold exactly one top-level entry rawreel-${version}, has: ${top}")
    endif()
    set(app "${extract}/rawreel-${version}")
    set(pkg_root "${extract}")
    set(exe_dir "${app}")
    set(licences_dir "${app}/ffmpeg/licenses")
    set(cwd_args "")
    ok("extract")
endif()

# 2b. No VC++ runtime DLL: users install the Microsoft Visual C++ Redistributable; the
# GUI smoke below runs on the installed one.
# On Linux: no C/C++ runtime or Vulkan loader library (system packages provide them) and no
# shared library outside ffmpeg/.
if(PLATFORM STREQUAL "windows")
    rawreel_vc_runtime_files(ROOT "${extract}" FILES vc_runtime)
    if(vc_runtime)
        list(JOIN vc_runtime ", " vc_runtime)
        fail("no-vcrt" "the package holds VC++ runtime DLLs: ${vc_runtime}")
    endif()
    ok("no-vcrt")
elseif(PLATFORM STREQUAL "linux")
    rawreel_linux_runtime_files(ROOT "${extract}" FILES runtime)
    if(runtime)
        list(JOIN runtime ", " runtime)
        fail("no-runtime" "the package holds runtime or stray shared libraries: ${runtime}")
    endif()
    ok("no-runtime")
else()
    # macOS: no MoltenVK (U-MAC2) and no C++ runtime (the system's libc++ is used).
    file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE "${pkg_root}" "${pkg_root}/*")
    set(runtime "")
    foreach(file IN LISTS files)
        cmake_path(GET file FILENAME name)
        string(TOLOWER "${name}" lower)
        if(lower MATCHES "moltenvk|^libc\\+\\+")
            list(APPEND runtime "${file}")
        endif()
    endforeach()
    if(runtime)
        list(JOIN runtime ", " runtime)
        fail("no-runtime" "the package holds MoltenVK or a C++ runtime: ${runtime}")
    endif()
    ok("no-runtime")
endif()

# 3. Manifest.
rawreel_check_manifest(ROOT "${pkg_root}" MANIFEST "${CMAKE_CURRENT_LIST_DIR}/manifest-${PLATFORM}.txt"
                       VERSION "${version}" BUNDLED ${RAWREEL_BUNDLED} ERRORS errors
                       CLASSES classes)
if(errors)
    fail_list("manifest" "${errors}")
endif()
# The licence supplements ship as the pinned files.
if(RAWREEL_BUNDLED)
    if(RAWREEL_LICENCE_SUPPLEMENTS STREQUAL "")
        fail("manifest" "package-info names no licence supplements")
    endif()
    rawreel_licence_supplements(DIR "${licences_dir}" SUPPLEMENTS ${RAWREEL_LICENCE_SUPPLEMENTS}
                                PATHS supplements ERRORS errors)
    if(errors)
        fail_list("manifest" "${errors}")
    endif()
endif()
ok("manifest")

set(ours "")
set(notices "")
set(third_party "")
foreach(item IN LISTS classes)
    string(REGEX MATCH "^([^|]+)\\|(.+)$" _ "${item}")
    set(kind "${CMAKE_MATCH_1}")
    set(path "${pkg_root}/${CMAKE_MATCH_2}")
    if(kind MATCHES "^ours(-bundled)?$")
        list(APPEND ours "${path}")
    elseif(kind STREQUAL "notices")
        set(notices "${path}")
    elseif(kind MATCHES "^(ffmpeg|vulkan)$")
        list(APPEND third_party "${path}")
    endif()
endforeach()

function(readelf_text step file out)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C "${readelf}" ${ARGN} "${file}"
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_QUIET)
    if(NOT result EQUAL 0)
        cmake_path(GET file FILENAME name)
        fail("${step}" "readelf ${ARGN} ${name} failed (${result})")
    endif()
    set(${out} "${output}" PARENT_SCOPE)
endfunction()

# 3b. ELF rules (Linux): the exes have no RUNPATH and need only the listed system libraries; the
# FFmpeg libraries find their siblings through RUNPATH $ORIGIN and are named by their SONAME (the
# name the loader opens); ldd resolves everything; no symbol version is newer than the baseline.
if(PLATFORM STREQUAL "linux")
    file(GLOB_RECURSE files LIST_DIRECTORIES false "${app}/*")
    set(errors "")
    set(elf_files "")
    foreach(file IN LISTS files)
        rawreel_is_elf(FILE "${file}" RESULT elf)
        if(NOT elf)
            continue()
        endif()
        list(APPEND elf_files "${file}")
        cmake_path(GET file FILENAME name)
        cmake_path(RELATIVE_PATH file BASE_DIRECTORY "${app}" OUTPUT_VARIABLE relative)
        if(relative MATCHES "^rawreel-(gui|cli)$")
            set(kind exe)
        elseif(relative MATCHES "^ffmpeg/[^/]+$")
            set(kind library)
        else()
            list(APPEND errors "unexpected ELF file ${relative}")
            continue()
        endif()
        readelf_text("elf" "${file}" dynamic -d)
        readelf_text("elf" "${file}" versions -V)
        rawreel_elf_errors(NAME "${name}" KIND ${kind} DYNAMIC "${dynamic}" VERSIONS "${versions}"
                           ALLOWED ${RAWREEL_LINUX_SYSTEM_SONAMES}
                           GLIBC_MAX "${RAWREEL_LINUX_GLIBC_MAX}"
                           GLIBCXX_MAX "${RAWREEL_LINUX_GLIBCXX_MAX}" ERRORS found)
        list(APPEND errors ${found})
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C ${no_ld_path} "${ldd}" "${file}"
                        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
        rawreel_ldd_errors(NAME "${name}" TEXT "${output}${error}" ERRORS found)
        list(APPEND errors ${found})
        if(NOT result EQUAL 0 AND NOT found)
            list(APPEND errors "${name}: ldd exited ${result}")
        endif()
    endforeach()
    foreach(exe IN ITEMS rawreel-gui rawreel-cli)
        if(NOT "${app}/${exe}" IN_LIST elf_files)
            list(APPEND errors "${exe} is no ELF file")
        endif()
    endforeach()
    if(errors)
        fail_list("elf" "${errors}")
    endif()
    ok("elf")

    # 3c. Modes and owners (Linux), read from the archives as stored: the ELF files of the package
    # are 755, every other file 644 (the .debug files too), no folder is world-writable. A CI build
    # records only root or the account and primary group that run this smoke (read with id, so no
    # guess about the runner image); a local build records the developer's account, so the owners
    # are only printed.
    set(executables "")
    foreach(file IN LISTS elf_files)
        cmake_path(RELATIVE_PATH file BASE_DIRECTORY "${extract}" OUTPUT_VARIABLE relative)
        list(APPEND executables "${relative}")
    endforeach()
    set(owner_rule "")
    if(DEFINED ENV{CI} AND NOT "$ENV{CI}" STREQUAL "")
        execute_process(COMMAND id -un RESULT_VARIABLE user_result OUTPUT_VARIABLE run_user
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        execute_process(COMMAND id -gn RESULT_VARIABLE group_result OUTPUT_VARIABLE run_group
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(NOT user_result EQUAL 0 OR NOT group_result EQUAL 0 OR run_user STREQUAL ""
           OR run_group STREQUAL "")
            fail("modes" "id -un / id -gn failed (${user_result}, ${group_result})")
        endif()
        set(owner_rule OWNERS root ${run_user} GROUPS root ${run_group})
    endif()
    set(errors "")
    set(archives "${RAWREEL_PACKAGE}")
    if(EXISTS "${OUT_DIR}/${RAWREEL_SYMBOLS}")
        list(APPEND archives "${RAWREEL_SYMBOLS}")
    endif()
    foreach(archive IN LISTS archives)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C "${gnu_tar}" tvf
                                "${OUT_DIR}/${archive}"
                        RESULT_VARIABLE result OUTPUT_VARIABLE listing ERROR_VARIABLE error)
        if(NOT result EQUAL 0)
            fail("modes" "tar tvf ${archive} failed (${result}): ${error}")
        endif()
        set(archive_executables "")
        if(archive STREQUAL RAWREEL_PACKAGE)
            set(archive_executables ${executables})
        endif()
        rawreel_tar_listing_errors(TEXT "${listing}" EXECUTABLES ${archive_executables}
                                   ${owner_rule} ERRORS found OWNER_NAMES owners)
        foreach(item IN LISTS found)
            list(APPEND errors "${archive}: ${item}")
        endforeach()
        if(NOT owner_rule)
            list(JOIN owners ", " owners)
            message(STATUS "package-smoke: modes: note: ${archive} records the owners ${owners} "
                           "(checked in CI only); local packages record the local account, "
                           "do not share them")
        endif()
    endforeach()
    if(errors)
        fail_list("modes" "${errors}")
    endif()
    ok("modes")
endif()

# macOS: the output (stdout and stderr) of a tool; a non-zero exit fails <step>, unless RESULT
# <var> takes the exit code.
function(tool_text step out)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "RESULT" "")
    execute_process(COMMAND ${arg_UNPARSED_ARGUMENTS} RESULT_VARIABLE result
                    OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(DEFINED arg_RESULT)
        set(${arg_RESULT} "${result}" PARENT_SCOPE)
    elseif(NOT result EQUAL 0)
        list(JOIN arg_UNPARSED_ARGUMENTS " " command)
        fail("${step}" "${command}: exit ${result}:\n${output}")
    endif()
    set(${out} "${output}" PARENT_SCOPE)
endfunction()

# 3d. The app (macOS): Info.plist and the KosmicKrisp manifest; the Mach-O rules (minos, rpaths,
# dependencies; rawreel_macho_errors); the signatures: every Mach-O file ad hoc except the
# vendor-signed KosmicKrisp, which keeps LunarG's Developer ID signature, and the app's seal.
if(PLATFORM STREQUAL "macos")
    set(errors "")
    set(plist "${contents}/Info.plist")
    foreach(pair IN ITEMS "CFBundleExecutable|rawreel-gui" "LSMinimumSystemVersion|27.0"
                          "CFBundleShortVersionString|${version}" "CFBundleIdentifier|")
        string(REGEX MATCH "^([^|]+)\\|(.*)$" _ "${pair}")
        set(key "${CMAKE_MATCH_1}")
        set(expected "${CMAKE_MATCH_2}")
        tool_text("bundle" value "${plutil_path}" -extract "${key}" raw "${plist}")
        string(STRIP "${value}" value)
        if(key STREQUAL "CFBundleIdentifier")
            set(bundle_id "${value}")
        elseif(NOT value STREQUAL expected)
            list(APPEND errors "Info.plist: ${key} is '${value}', expected '${expected}'")
        endif()
    endforeach()
    if(NOT bundle_id STREQUAL "io.github.barabashkad.rawreel")
        list(APPEND errors "Info.plist: CFBundleIdentifier is '${bundle_id}'")
    endif()
    # The one driver manifest, its library in Frameworks (plan Global Constraints).
    file(GLOB icd_files LIST_DIRECTORIES false "${contents}/Resources/vulkan/icd.d/*.json")
    list(LENGTH icd_files icd_count)
    set(kosmickrisp "")
    if(NOT icd_count EQUAL 1)
        list(APPEND errors "Resources/vulkan/icd.d holds ${icd_count} manifests, not one")
    else()
        list(GET icd_files 0 icd)
        file(READ "${icd}" icd_json)
        string(JSON library_path ERROR_VARIABLE json_error GET "${icd_json}" ICD library_path)
        set(library_regex "^\\.\\./\\.\\./\\.\\./Frameworks/([^/]+\\.dylib)$")
        if(json_error OR NOT library_path MATCHES "${library_regex}")
            string(CONCAT error "the driver manifest's library_path '${library_path}' is not "
                                "../../../Frameworks/<dylib>")
            list(APPEND errors "${error}")
        else()
            set(kosmickrisp "${CMAKE_MATCH_1}")
            if(NOT EXISTS "${contents}/Frameworks/${kosmickrisp}")
                list(APPEND errors "the driver manifest names ${kosmickrisp}, not in Frameworks")
            endif()
        endif()
    endif()
    if(errors)
        fail_list("bundle" "${errors}")
    endif()
    ok("bundle")

    file(GLOB_RECURSE files LIST_DIRECTORIES false "${app}/*")
    set(macho_files "")
    foreach(file IN LISTS files)
        rawreel_is_macho(FILE "${file}" RESULT macho)
        if(NOT macho)
            continue()
        endif()
        list(APPEND macho_files "${file}")
        cmake_path(GET file FILENAME name)
        cmake_path(RELATIVE_PATH file BASE_DIRECTORY "${contents}" OUTPUT_VARIABLE relative)
        # Prebuilt third-party files: the SDK's KosmicKrisp and the custom_ffmpeg release.
        set(prebuilt "")
        if(relative MATCHES "^MacOS/rawreel-(gui|cli)$")
            set(kind exe)
        elseif(relative MATCHES "^Frameworks/(ffmpeg/)?[^/]+\\.dylib$")
            set(kind library)
            if(relative MATCHES "^Frameworks/ffmpeg/" OR name STREQUAL kosmickrisp)
                set(prebuilt PREBUILT)
            endif()
        else()
            list(APPEND errors "unexpected Mach-O file ${relative}")
            continue()
        endif()
        tool_text("macho" dependencies "${otool_path}" -L "${file}")
        tool_text("macho" load_commands "${otool_path}" -l "${file}")
        rawreel_otool_dependencies(TEXT "${dependencies}" DEPS deps)
        rawreel_otool_load_commands(TEXT "${load_commands}" RPATHS rpaths MINOS minos)
        rawreel_macho_errors(NAME "${relative}" KIND ${kind} DEPS ${deps} RPATHS ${rpaths}
                             MINOS "${minos}" ${prebuilt} ERRORS found)
        list(APPEND errors ${found})
    endforeach()
    foreach(exe IN ITEMS rawreel-gui rawreel-cli)
        if(NOT "${exe_dir}/${exe}" IN_LIST macho_files)
            list(APPEND errors "MacOS/${exe} is no Mach-O file")
        endif()
    endforeach()
    if(errors)
        fail_list("macho" "${errors}")
    endif()
    ok("macho")

    foreach(file IN LISTS macho_files)
        cmake_path(GET file FILENAME name)
        cmake_path(RELATIVE_PATH file BASE_DIRECTORY "${contents}" OUTPUT_VARIABLE relative)
        tool_text("signatures" signature "${codesign_path}" -dv --verbose=2 "${file}"
                  RESULT result)
        if(name STREQUAL kosmickrisp)
            rawreel_codesign_errors(NAME "${relative}" TEXT "${signature}" KIND vendor
                                    ERRORS found)
        else()
            if(name STREQUAL "rawreel-gui")
                set(identifier "${bundle_id}")
            elseif(name STREQUAL "rawreel-cli")
                set(identifier "${bundle_id}.cli")
            else()
                cmake_path(GET file STEM LAST_ONLY identifier)
            endif()
            rawreel_codesign_errors(NAME "${relative}" TEXT "${signature}" KIND adhoc
                                    IDENTIFIER "${identifier}" ERRORS found)
        endif()
        list(APPEND errors ${found})
        tool_text("signatures" output "${codesign_path}" --verify --strict "${file}" RESULT result)
        if(NOT result EQUAL 0)
            list(APPEND errors "${relative}: codesign --verify --strict failed: ${output}")
        endif()
    endforeach()
    # The app: signed ad hoc with its bundle id, its seal over the nested code and the resources
    # verifies (verification may descend; signing never did).
    tool_text("signatures" signature "${codesign_path}" -dv --verbose=2 "${app}" RESULT result)
    rawreel_codesign_errors(NAME rawreel.app TEXT "${signature}" KIND adhoc
                            IDENTIFIER "${bundle_id}" ERRORS found)
    list(APPEND errors ${found})
    if(NOT signature MATCHES "\nSealed Resources version=2 ")
        list(APPEND errors "rawreel.app has no sealed resources")
    endif()
    tool_text("signatures" output "${codesign_path}" --verify --strict --deep "${app}"
              RESULT result)
    if(NOT result EQUAL 0)
        list(APPEND errors "rawreel.app: codesign --verify --strict --deep failed: ${output}")
    endif()
    if(errors)
        fail_list("signatures" "${errors}")
    endif()
    ok("signatures")
endif()

# 4. Shipped notices.
rawreel_check_notices(FILE "${notices}" SHIPPED ${RAWREEL_SHIPPED} ERRORS errors)
if(errors)
    fail_list("notices" "${errors}")
endif()
ok("notices")

# 6. Privacy scan.
set(findings "")
foreach(file IN LISTS ours)
    set(binary "")
    rawreel_is_elf(FILE "${file}" RESULT elf)
    rawreel_is_macho(FILE "${file}" RESULT macho)
    if(file MATCHES "\\.(exe|dll)$" OR elf OR macho)
        set(binary BINARY)
    endif()
    rawreel_scan_privacy(FILE "${file}" MODE full ${binary} EXTRA_TERMS ${EXTRA_TERMS}
                         FINDINGS found)
    list(APPEND findings ${found})
endforeach()
# macOS: the third-party libraries of the app, the Vulkan loader the superbuild builds among them,
# hold no machine or home-folder path either (their e-mail addresses and version numbers are
# their authors'; the path rules only).
if(PLATFORM STREQUAL "macos")
    foreach(file IN LISTS third_party)
        rawreel_is_macho(FILE "${file}" RESULT macho)
        if(macho)
            rawreel_scan_privacy(FILE "${file}" MODE paths BINARY EXTRA_TERMS ${EXTRA_TERMS}
                                 FINDINGS found)
            list(APPEND findings ${found})
        endif()
    endforeach()
endif()
rawreel_scan_privacy(FILE "${notices}" MODE notices EXTRA_TERMS ${EXTRA_TERMS} FINDINGS found)
list(APPEND findings ${found})
set(pdb_exes "")
if(PLATFORM STREQUAL "windows")
    set(pdb_exes rawreel-gui rawreel-cli)
endif()
foreach(exe IN LISTS pdb_exes)
    rawreel_check_pdb_reference(FILE "${app}/${exe}.exe" ERRORS errors)
    foreach(error IN LISTS errors)
        list(APPEND findings "${exe}.exe: ${error}")
    endforeach()
    # The CodeView record must name exactly <exe>.pdb (a mangled name such as %x.pdb% is a
    # finding too, which the directory check above cannot see).
    file(STRINGS "${app}/${exe}.exe" pdb_names REGEX "\\.[Pp][Dd][Bb]")
    if(NOT "${exe}.pdb" IN_LIST pdb_names)
        list(APPEND findings "${exe}.exe: the exe does not name its PDB as ${exe}.pdb")
    endif()
endforeach()
if(findings)
    fail_list("privacy" "${findings}")
endif()
ok("privacy")

# 7. CLI.
set(cli "${exe_dir}/rawreel-cli${exe_suffix}")
set(cli_env "${CMAKE_COMMAND}" -E env --unset=DVV_FFMPEG_DIR --unset=DVV_REQUIRE_FFMPEG
            DVV_LEAK_CHECK=fail ${no_ld_path})
function(run_cli out_result out_output out_error)
    execute_process(COMMAND ${cli_env} ${ARGN} TIMEOUT 120 RESULT_VARIABLE result
                    OUTPUT_VARIABLE output ERROR_VARIABLE error ${cwd_args})
    set(${out_result} "${result}" PARENT_SCOPE)
    set(${out_output} "${output}" PARENT_SCOPE)
    set(${out_error} "${error}" PARENT_SCOPE)
endfunction()
function(ffmpeg_line text out)
    rawreel_ffmpeg_version_line(TEXT "${text}" LINE line ERRORS errors)
    if(errors)
        fail("cli" "${errors}")
    endif()
    set(${out} "${line}" PARENT_SCOPE)
endfunction()

run_cli(result output error "${cli}" --version)
if(NOT result EQUAL 0)
    fail("cli" "rawreel-cli --version exited ${result}:\n${error}")
endif()
if(NOT output MATCHES "(^|\n)rawreel ${version}(\r?\n|$)")
    fail("cli" "--version has no line \"rawreel ${version}\"")
endif()
ffmpeg_line("${output}" line)
set(lavfi "lavfi:testsrc2=s=64x48:r=25,format=yuv420p")
if(RAWREEL_BUNDLED)
    string(REPLACE "." "\\." ffmpeg_regex "${RAWREEL_FFMPEG_VERSION}")
    # The line names the bundled version and the repository it is built from, never a folder (a
    # folder names the user's machine); the bundled version shows the package's ffmpeg/ was loaded.
    if(NOT line MATCHES "^FFmpeg ${ffmpeg_regex} \\(LGPL[^/\\\\]*\\) from https://github\\.com/[^ ]+$")
        fail("cli" "unexpected FFmpeg line: ${line}")
    endif()
    run_cli(result output error "${cli}" info "${lavfi}" --json)
    if(NOT result EQUAL 0)
        fail("cli" "info ${lavfi} exited ${result}:\n${error}")
    endif()
    string(JSON width ERROR_VARIABLE json_error GET "${output}" width)
    string(JSON height ERROR_VARIABLE json_error GET "${output}" height)
    if(NOT width EQUAL 64 OR NOT height EQUAL 48)
        fail("cli" "info reports ${width}x${height}, expected 64x48")
    endif()
    set(frame "${WORK_DIR}/frame2.yuv")
    run_cli(result output error "${cli}" dump "${lavfi}" --frame 2 --via cpu --out "${frame}")
    if(NOT result EQUAL 0 OR NOT EXISTS "${frame}")
        fail("cli" "dump ${lavfi} exited ${result}:\n${error}")
    endif()
    file(SIZE "${frame}" bytes)
    if(NOT bytes EQUAL 4608)
        fail("cli" "dump wrote ${bytes} bytes, expected 4608")
    endif()
    file(MAKE_DIRECTORY "${WORK_DIR}/empty-ffmpeg")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=DVV_REQUIRE_FFMPEG ${no_ld_path}
                            DVV_LEAK_CHECK=fail "DVV_FFMPEG_DIR=${WORK_DIR}/empty-ffmpeg"
                            "${cli}" --version
                    TIMEOUT 120 RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
                    ${cwd_args})
    if(NOT result EQUAL 0)
        fail("cli" "--version with an empty DVV_FFMPEG_DIR exited ${result}:\n${error}")
    endif()
    ffmpeg_line("${output}" line)
    if(NOT line MATCHES "^FFmpeg: not available")
        fail("cli" "with an empty DVV_FFMPEG_DIR the FFmpeg line is: ${line}")
    endif()
elseif(NOT line MATCHES "^FFmpeg: not available")
    fail("cli" "FFmpeg is not bundled, but the FFmpeg line is: ${line}")
endif()
ok("cli")

# 7b. Config defaults: no rawreel.json in the package, the built-in defaults apply (macOS: the
# app reads <HOME>/Library/Application Support/rawreel/rawreel.json, a fresh HOME here).
if(EXISTS "${exe_dir}/rawreel.json")
    fail("config" "rawreel.json must not be shipped")
endif()
set(yuv "${WORK_DIR}/config-check.yuv")
string(REPEAT "A" 4608 bytes)
file(WRITE "${yuv}" "${bytes}")
run_cli(result output error "${cli}" info "file:${yuv}?fmt=yuv420p&size=64x48" --json)
if(NOT result EQUAL 0)
    fail("config" "info on ${yuv} exited ${result}:\n${error}")
endif()
string(JSON frames ERROR_VARIABLE json_error GET "${output}" frames)
string(JSON reader ERROR_VARIABLE json_error GET "${output}" reader)
if(NOT frames EQUAL 1 OR reader STREQUAL "" OR reader MATCHES "NOTFOUND$")
    fail("config" "info reports frames ${frames} and reader '${reader}', expected 1 and a mode")
endif()
string(REPLACE "config-check" "" error_text "${error}")
if(error_text MATCHES "config")
    fail("config" "a missing rawreel.json must stay silent; stderr:\n${error}")
endif()
ok("config")

# macOS, the loaded-image audit (U-MAC8) of one run's DYLD_PRINT_LIBRARIES=1 and
# VK_LOADER_DEBUG=driver output: every image inside the app, /usr/lib or /System; nothing from a
# Vulkan SDK in the home folder; with DRIVER, the loader finds only the bundled manifest and the
# bundled KosmicKrisp is loaded; with FFMPEG, the six FFmpeg libraries come from the app.
function(audit step name text)
    cmake_parse_arguments(PARSE_ARGV 3 arg "DRIVER;FFMPEG" "" "")
    rawreel_loaded_image_errors(NAME "${name}" TEXT "${text}" BUNDLE "${app}" ERRORS errors
                                IMAGES images)
    foreach(root IN LISTS vulkan_sdk_roots)
        string(FIND "${text}" "${root}/" at)
        if(NOT at EQUAL -1)
            list(APPEND errors "${name}: its output names the Vulkan SDK at ${root}")
        endif()
    endforeach()
    if(arg_DRIVER)
        rawreel_vulkan_driver_errors(NAME "${name}" TEXT "${text}" MANIFEST "${icd}"
                                     ERRORS found)
        list(APPEND errors ${found})
        if(NOT "${contents}/Frameworks/${kosmickrisp}" IN_LIST images)
            list(APPEND errors "${name} does not load the bundled ${kosmickrisp}")
        endif()
    endif()
    if(arg_FFMPEG)
        file(GLOB libraries LIST_DIRECTORIES false "${contents}/Frameworks/ffmpeg/*.dylib")
        foreach(library IN LISTS libraries)
            if(NOT library IN_LIST images)
                cmake_path(GET library FILENAME library_name)
                list(APPEND errors "${name} does not load the bundled ${library_name}")
            endif()
        endforeach()
    endif()
    if(errors)
        fail_list("${step}" "${errors}")
    endif()
    list(LENGTH images count)
    message(STATUS "package-smoke: ${step}: ${name}: ${count} images, all inside the app, "
                   "/usr/lib or /System")
endfunction()

# 7c. The CLI's loaded images (macOS): --version opens Vulkan and FFmpeg and names the driver,
# info reads the lavfi source through the bundled FFmpeg.
if(PLATFORM STREQUAL "macos")
    set(trace_env DYLD_PRINT_LIBRARIES=1 VK_LOADER_DEBUG=driver)
    set(ffmpeg_audit "")
    if(RAWREEL_BUNDLED)
        set(ffmpeg_audit FFMPEG)
    endif()
    run_cli(result output error ${trace_env} "${cli}" --version)
    if(NOT result EQUAL 0)
        fail("audit" "rawreel-cli --version exited ${result}:\n${error}")
    endif()
    audit("audit" "rawreel-cli --version" "${error}" DRIVER ${ffmpeg_audit})
    # "Vulkan: <device>, API <api>, driver <name> <info>" (version_text.cpp).
    if(output MATCHES "(^|\n)(Vulkan: [^\n]*)")
        set(vulkan_line "${CMAKE_MATCH_2}")
    else()
        fail("audit" "rawreel-cli --version prints no Vulkan line")
    endif()
    if(vulkan_line MATCHES "^Vulkan: not available" AND ALLOW_GUI_SKIP)
        message(STATUS "package-smoke: audit: no usable GPU (${vulkan_line}); the driver name is "
                       "not checked")
    elseif(NOT vulkan_line MATCHES "^Vulkan: .+, API [^,]+, driver KosmicKrisp ")
        fail("audit" "rawreel-cli --version does not name the KosmicKrisp driver: ${vulkan_line}")
    endif()
    if(RAWREEL_BUNDLED)
        run_cli(result output error ${trace_env} "${cli}" info "${lavfi}")
        if(NOT result EQUAL 0)
            fail("audit" "rawreel-cli info ${lavfi} exited ${result}:\n${error}")
        endif()
        audit("audit" "rawreel-cli info" "${error}" FFMPEG)
    endif()
    # Review Focus 3 holds only where a Vulkan SDK (with MoltenVK) is installed: say whether one is.
    file(GLOB sdk_versions LIST_DIRECTORIES true "$ENV{HOME}/VulkanSDK/*")
    if(sdk_versions)
        list(TRANSFORM sdk_versions REPLACE "^.*/" "")
        list(JOIN sdk_versions ", " sdk_versions)
        message(STATUS "package-smoke: audit: a Vulkan SDK is installed in ~/VulkanSDK "
                       "(${sdk_versions}); nothing was loaded from it")
    else()
        message(STATUS "package-smoke: audit: no Vulkan SDK in ~/VulkanSDK")
    endif()
    ok("audit")
endif()

# 8. GUI.
if(NOT GUI_SMOKE STREQUAL "off")
    set(gui_env DVV_LEAK_CHECK=fail --unset=DVV_VK_VALIDATION --unset=DVV_VK_VALIDATION_REQUIRED
                --unset=DVV_FFMPEG_DIR)
    if(PLATFORM STREQUAL "linux")
        # NO_AT_BRIDGE: no accessibility bus in a test session (its warning is noise there).
        list(APPEND gui_env ${no_ld_path} NO_AT_BRIDGE=1)
    elseif(PLATFORM STREQUAL "macos")
        list(APPEND gui_env ${no_ld_path} ${trace_env})
    endif()
    if(ALLOW_GUI_SKIP)
        list(APPEND gui_env --unset=DVV_REQUIRE_GPU)
    else()
        list(APPEND gui_env DVV_REQUIRE_GPU=1)
    endif()
    if(RAWREEL_BUNDLED)
        list(APPEND gui_env DVV_REQUIRE_FFMPEG=1)
    else()
        list(APPEND gui_env --unset=DVV_REQUIRE_FFMPEG)
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${gui_env}
                            "${exe_dir}/rawreel-gui${exe_suffix}" --selftest smoke
                    TIMEOUT 300 RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
                    ${cwd_args})
    set(gui_skipped OFF)
    if(result EQUAL 77 AND ALLOW_GUI_SKIP)
        message(STATUS "package-smoke: GUI smoke skipped (no GPU)")
        set(gui_skipped ON)
    elseif(NOT result EQUAL 0)
        fail("gui" "rawreel-gui --selftest smoke exited ${result}:\n${output}${error}")
    endif()
    # macOS: the audit of the run, and the device line of its log names the bundled driver
    # (Review Focus 3: also with a Vulkan SDK installed).
    set(device_regex "(^|\n)[^\n]* Vulkan device: [^\n]*, driver KosmicKrisp ")
    if(PLATFORM STREQUAL "macos" AND NOT gui_skipped)
        # The two streams joined at a line end, so no dyld line is glued to a log line.
        set(gui_text "${output}\n${error}")
        audit("gui" "rawreel-gui --selftest smoke" "${gui_text}" DRIVER ${ffmpeg_audit})
        if(NOT gui_text MATCHES "${device_regex}")
            fail("gui" "the log does not name the KosmicKrisp driver (Vulkan device: ...)")
        endif()
    endif()
    ok("gui")

    # 8b. macOS: once through LaunchServices, as Finder starts the app (Review Focus 4): no
    # terminal environment (only the --env values), the working folder /; the result is read
    # from the output files and from the log in <HOME>/Library/Application Support/rawreel.
    if(PLATFORM STREQUAL "macos" AND NOT gui_skipped)
        # A variable that launchd hands to every app would change this run.
        foreach(name IN ITEMS DYLD_LIBRARY_PATH DYLD_FALLBACK_LIBRARY_PATH DYLD_INSERT_LIBRARIES
                              VK_DRIVER_FILES VK_ICD_FILENAMES VK_ADD_DRIVER_FILES
                              VK_INSTANCE_LAYERS VK_LOADER_LAYERS_ENABLE DVV_FFMPEG_DIR
                              DVV_VK_VALIDATION)
            execute_process(COMMAND /bin/launchctl getenv ${name} OUTPUT_VARIABLE value
                            ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(NOT value STREQUAL "")
                fail("open" "launchd sets ${name} for every app (launchctl unsetenv ${name})")
            endif()
        endforeach()
        set(open_home "${WORK_DIR}/home-open")
        set(open_out "${WORK_DIR}/open-stdout.txt")
        set(open_err "${WORK_DIR}/open-stderr.txt")
        file(MAKE_DIRECTORY "${open_home}")
        set(open_env --env DYLD_PRINT_LIBRARIES=1 --env VK_LOADER_DEBUG=driver
                     --env "HOME=${open_home}" --env DVV_LEAK_CHECK=fail)
        if(NOT ALLOW_GUI_SKIP)
            list(APPEND open_env --env DVV_REQUIRE_GPU=1)
        endif()
        if(RAWREEL_BUNDLED)
            list(APPEND open_env --env DVV_REQUIRE_FFMPEG=1)
        endif()
        # -W waits for the app to quit, -n starts a new instance even when one runs; open's own
        # exit code says only whether the app started.
        execute_process(COMMAND "${open_path}" -W -n ${open_env} --stdout "${open_out}"
                                --stderr "${open_err}" -a "${app}" --args --selftest smoke
                        TIMEOUT 300 RESULT_VARIABLE result OUTPUT_VARIABLE output
                        ERROR_VARIABLE output)
        if(NOT result EQUAL 0)
            # A hung app outlives the timeout of open -W.
            execute_process(COMMAND "${pkill_path}" -f "${exe_dir}/rawreel-gui" OUTPUT_QUIET
                            ERROR_QUIET)
            fail("open" "open -W -n -a rawreel.app exited ${result}: ${output}")
        endif()
        set(open_text "")
        foreach(file IN ITEMS "${open_out}" "${open_err}")
            if(NOT EXISTS "${file}")
                fail("open" "open wrote no ${file}")
            endif()
            file(READ "${file}" part)
            string(APPEND open_text "${part}\n")
        endforeach()
        if(NOT open_text MATCHES "rawreel-gui --selftest: passed" OR open_text MATCHES
           "rawreel-gui --selftest: [^\n]* failed:|still alive at exit")
            fail("open" "the selftest did not pass:\n${open_text}")
        endif()
        audit("open" "open -a rawreel.app --selftest smoke" "${open_text}" DRIVER
              ${ffmpeg_audit})
        set(open_log "${open_home}/Library/Application Support/rawreel/rawreel.log")
        if(NOT EXISTS "${open_log}")
            fail("open" "no log in <HOME>/Library/Application Support/rawreel/rawreel.log")
        endif()
        file(READ "${open_log}" log)
        if(NOT log MATCHES "${device_regex}" OR NOT log MATCHES "rawreel-gui --selftest: passed")
            fail("open" "the log names no KosmicKrisp device or no passed selftest:\n${log}")
        endif()
        ok("open")
    endif()
else()
    message(STATUS "package-smoke: gui: off")
endif()

# 9. Symbols.
set(symbol_ext pdb)
set(archive_kind zip)
if(PLATFORM STREQUAL "linux")
    set(symbol_ext debug)
    set(archive_kind tar.xz)
endif()
set(expected_symbols "rawreel-cli.${symbol_ext};rawreel-gui.${symbol_ext}")
if(PLATFORM STREQUAL "macos")
    # Two .dSYM bundles, each with its Info.plist and its DWARF file only (no Relocations map:
    # it names the build folder).
    set(symbol_ext dSYM)
    set(expected_symbols "")
    foreach(exe IN ITEMS rawreel-cli rawreel-gui)
        set(dsym "${exe}.dSYM")
        list(APPEND expected_symbols "${dsym}" "${dsym}/Contents" "${dsym}/Contents/Info.plist"
             "${dsym}/Contents/Resources" "${dsym}/Contents/Resources/DWARF"
             "${dsym}/Contents/Resources/DWARF/${exe}")
    endforeach()
    list(SORT expected_symbols)
endif()
if(RAWREEL_SYMBOLS STREQUAL "" OR NOT EXISTS "${OUT_DIR}/${RAWREEL_SYMBOLS}")
    fail("symbols" "the symbols ${archive_kind} is missing")
endif()
set(symbols "${WORK_DIR}/symbols")
file(MAKE_DIRECTORY "${symbols}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${OUT_DIR}/${RAWREEL_SYMBOLS}"
                WORKING_DIRECTORY "${symbols}" RESULT_VARIABLE result)
file(GLOB_RECURSE pdbs LIST_DIRECTORIES true RELATIVE "${symbols}" "${symbols}/*")
list(SORT pdbs)
if(NOT result EQUAL 0 OR NOT pdbs STREQUAL expected_symbols)
    fail("symbols" "the symbols ${archive_kind} must hold exactly rawreel-gui.${symbol_ext} and "
         "rawreel-cli.${symbol_ext}, has: ${pdbs}")
endif()
set(findings "")
foreach(pdb IN LISTS pdbs)
    if(IS_DIRECTORY "${symbols}/${pdb}")
        continue()
    endif()
    rawreel_scan_privacy(FILE "${symbols}/${pdb}" MODE paths BINARY EXTRA_TERMS ${EXTRA_TERMS}
                         FINDINGS found)
    list(APPEND findings ${found})
endforeach()
# macOS: each exe and its DWARF file share the UUID (lldb and atos match them by it).
if(PLATFORM STREQUAL "macos")
    foreach(exe IN ITEMS rawreel-gui rawreel-cli)
        tool_text("symbols" exe_commands "${otool_path}" -l "${exe_dir}/${exe}")
        tool_text("symbols" dwarf_commands "${otool_path}" -l
                  "${symbols}/${exe}.dSYM/Contents/Resources/DWARF/${exe}")
        rawreel_otool_load_commands(TEXT "${exe_commands}" RPATHS ignored MINOS ignored
                                    UUID exe_uuid)
        rawreel_otool_load_commands(TEXT "${dwarf_commands}" RPATHS ignored MINOS ignored
                                    UUID dwarf_uuid)
        if(exe_uuid STREQUAL "" OR NOT exe_uuid STREQUAL dwarf_uuid)
            list(APPEND findings
                 "${exe}: UUID '${exe_uuid}' differs from ${exe}.dSYM '${dwarf_uuid}'")
        endif()
    endforeach()
endif()
# Linux: each exe names its .debug file and shares its build ID; no debug section is compressed
# (the privacy scan above reads the sections as stored).
if(PLATFORM STREQUAL "linux")
    foreach(exe IN ITEMS rawreel-gui rawreel-cli)
        readelf_text("symbols" "${app}/${exe}" exe_notes -n -p .gnu_debuglink)
        readelf_text("symbols" "${symbols}/${exe}.debug" debug_notes -n)
        readelf_text("symbols" "${symbols}/${exe}.debug" debug_sections -S -W)
        rawreel_readelf_debuglink(TEXT "${exe_notes}" NAME link)
        if(NOT link STREQUAL "${exe}.debug")
            list(APPEND findings "${exe}: the debuglink names '${link}', expected ${exe}.debug")
        endif()
        rawreel_readelf_build_id(TEXT "${exe_notes}" ID exe_id)
        rawreel_readelf_build_id(TEXT "${debug_notes}" ID debug_id)
        if(exe_id STREQUAL "" OR NOT exe_id STREQUAL debug_id)
            list(APPEND findings "${exe}: build ID '${exe_id}' differs from ${exe}.debug '${debug_id}'")
        endif()
        rawreel_readelf_compressed_sections(TEXT "${debug_sections}" SECTIONS compressed)
        if(compressed)
            list(JOIN compressed ", " compressed)
            list(APPEND findings "${exe}.debug: compressed sections ${compressed}")
        endif()
        if(NOT debug_sections MATCHES "\\.debug_info ")
            list(APPEND findings "${exe}.debug: no .debug_info section")
        endif()
    endforeach()
endif()
if(findings)
    fail_list("symbols" "${findings}")
endif()
ok("symbols")

# 10. A bundled FFmpeg's SOURCE.txt (ffmpeg/SOURCE.txt; on macOS at the top of the .dmg) names
# the corresponding source: the tag of the custom_ffmpeg release the package's prefix pins.
if(RAWREEL_BUNDLED)
    set(source_txt "${app}/ffmpeg/SOURCE.txt")
    if(PLATFORM STREQUAL "macos")
        set(source_txt "${pkg_root}/SOURCE.txt")
    endif()
    if(NOT EXISTS "${source_txt}")
        fail("source" "the package has no SOURCE.txt")
    endif()
    file(READ "${source_txt}" text)
    rawreel_source_txt_versions(TEXT "${text}" VERSIONS named)
    rawreel_ffmpeg_source_errors(NAME "${RAWREEL_PACKAGE}" CUSTOM_FFMPEG_VERSION
                                 "${RAWREEL_CUSTOM_FFMPEG_VERSION}" NAMED ${named} ERRORS errors)
    if(errors)
        fail_list("source" "${errors}")
    endif()
    ok("source")
endif()

# macOS: the .dmg is ejected again.
if(PLATFORM STREQUAL "macos")
    set_property(GLOBAL PROPERTY rawreel_smoke_mount "")
    eject("${mount}")
    mounted("${mount}" is_mounted)
    if(is_mounted)
        fail("unmount" "the .dmg is still mounted at ${mount}")
    endif()
    ok("unmount")
endif()
