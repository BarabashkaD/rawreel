# Builds the packages from a Release build tree:
#   cmake -DBUILD_DIR=<rawreel build dir> [-DCONFIG=Release] -DOUT_DIR=<dir> [-DWORK_DIR=<dir>]
#         -P packaging/make-packages.cmake
# Runs CPack (component rawreel), names the package rawreel-<v>-windows-x64.zip or
# rawreel-<v>-linux-x64.tar.xz, builds the symbols archive (-symbols.zip or -symbols.tar.xz),
# checks that a bundled FFmpeg's SOURCE.txt in the package names the custom_ffmpeg tag of the
# third-party prefix, and writes <OUT_DIR>/package-info-<platform>.cmake (read by
# package-smoke.cmake and check-release-ffmpeg.cmake; no absolute paths).
# macOS: installs the component into a folder (rawreel.app and the documents), gives the exes the
# one LC_RPATH of the app, signs ad hoc from the inside out (the libraries in Contents/Frameworks
# except a vendor-signed one, which keeps its signature, then the exes, then the app; never
# --deep), verifies the signature and builds
# rawreel-<v>-macos-arm64.dmg (APFS, UDZO) from the folder; the symbols archive
# -symbols.zip holds the two .dSYM bundles.
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

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}" "${OUT_DIR}")
file(REMOVE "${OUT_DIR}/${package}" "${OUT_DIR}/${symbols}"
     "${OUT_DIR}/package-info-${RAWREEL_PLATFORM}.cmake")

if(RAWREEL_PLATFORM STREQUAL "macos")
    # A tool of the Xcode Command Line Tools; its output goes to <var>.
    function(run check out)
        execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output
                        ERROR_VARIABLE output)
        if(NOT result EQUAL 0)
            list(JOIN ARGN " " command)
            fail("${check}" "${command}: exit ${result}:\n${output}")
        endif()
        set(${out} "${output}" PARENT_SCOPE)
    endfunction()
    # The system's tools in /usr/bin (otool and install_name_tool: the Command Line Tools' shims),
    # never another one on PATH such as Homebrew LLVM's.
    foreach(tool IN ITEMS otool install_name_tool codesign hdiutil plutil)
        find_program(${tool}_path ${tool} PATHS /usr/bin NO_DEFAULT_PATH NO_CACHE)
        if(NOT ${tool}_path)
            fail("tools" "/usr/bin/${tool} is missing (install the Xcode Command Line Tools)")
        endif()
    endforeach()
    # The LC_RPATH entries of a Mach-O file.
    function(rpaths file out)
        run("rpath" output "${otool_path}" -l "${file}")
        rawreel_otool_load_commands(TEXT "${output}" RPATHS found MINOS minos)
        set(${out} "${found}" PARENT_SCOPE)
    endfunction()

    # The component as the .dmg holds it.
    set(stage "${WORK_DIR}/dmg")
    run("install" output "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --config "${CONFIG}"
        --component rawreel --prefix "${stage}")
    set(app "${stage}/rawreel.app")
    set(exes "${app}/Contents/MacOS/rawreel-cli" "${app}/Contents/MacOS/rawreel-gui")
    # The build tree's rpaths (the prefix's lib folder) go; the app's one rpath comes in.
    foreach(exe IN LISTS exes)
        rpaths("${exe}" old)
        set(edits "")
        foreach(path IN LISTS old)
            list(APPEND edits -delete_rpath "${path}")
        endforeach()
        run("rpath" output "${install_name_tool_path}" ${edits}
            -add_rpath "${RAWREEL_MACOS_EXE_RPATH}" "${exe}")
        rpaths("${exe}" new)
        if(NOT new STREQUAL RAWREEL_MACOS_EXE_RPATH)
            fail("rpath" "${exe} has the rpaths '${new}', not ${RAWREEL_MACOS_EXE_RPATH}")
        endif()
    endforeach()
    # Ad-hoc signatures from the inside out, each after the file's last change: the libraries
    # (FFmpeg's, the Vulkan loader), the exes, then the app, which seals the nested code,
    # Info.plist and the resources. --deep is never used: each file is named. A library that
    # carries a vendor's signature (an Authority, i.e. not ad hoc: KosmicKrisp, signed by LunarG)
    # is an unchanged vendor file and keeps it; that signature must verify. Identifiers: a
    # library's file name without .dylib, the app's bundle id for the app and its main exe, and
    # <bundle id>.cli for rawreel-cli (codesign would otherwise derive a name with a hash).
    run("plist" bundle_id "${plutil_path}" -extract CFBundleIdentifier raw
        "${app}/Contents/Info.plist")
    string(STRIP "${bundle_id}" bundle_id)
    file(GLOB_RECURSE libraries LIST_DIRECTORIES false "${app}/Contents/Frameworks/*.dylib")
    list(SORT libraries)
    foreach(file IN LISTS libraries)
        execute_process(COMMAND "${codesign_path}" -dv --verbose=2 "${file}"
                        RESULT_VARIABLE result OUTPUT_VARIABLE signature ERROR_VARIABLE signature)
        if(result EQUAL 0 AND signature MATCHES "\nAuthority=([^\n]+)")
            set(authority "${CMAKE_MATCH_1}")
            run("codesign" output "${codesign_path}" --verify --strict "${file}")
            message(STATUS "make-packages: ${file}: kept the signature of ${authority}")
            continue()
        endif()
        cmake_path(GET file STEM LAST_ONLY identifier)
        run("codesign" output "${codesign_path}" --force --sign - --identifier "${identifier}"
            "${file}")
    endforeach()
    run("codesign" output "${codesign_path}" --force --sign - --identifier "${bundle_id}.cli"
        "${app}/Contents/MacOS/rawreel-cli")
    run("codesign" output "${codesign_path}" --force --sign - --identifier "${bundle_id}"
        "${app}/Contents/MacOS/rawreel-gui")
    run("codesign" output "${codesign_path}" --force --sign - "${app}")
    # Verification may descend: it changes nothing.
    run("codesign" output "${codesign_path}" --verify --strict --deep "${app}")
    message(STATUS "make-packages: rawreel.app: the signature verifies (codesign --verify --strict)")
    # The usual Applications link beside the app (drag the app onto it). A symbolic link, not a
    # file: the package checks and the manifest skip it (check-package.cmake).
    file(CREATE_LINK "/Applications" "${stage}/Applications" SYMBOLIC)
    if(NOT IS_SYMLINK "${stage}/Applications")
        fail("applications" "${stage}/Applications is no symbolic link")
    endif()
    # hdiutil create fails now and then while the system still holds a disk image ("Resource
    # busy"): up to three attempts.
    set(hdiutil_create "${hdiutil_path}" create -volname "rawreel-${version}" -srcfolder "${stage}"
                       -fs APFS -format UDZO -ov "${OUT_DIR}/${package}")
    foreach(attempt RANGE 1 3)
        execute_process(COMMAND ${hdiutil_create} RESULT_VARIABLE result OUTPUT_VARIABLE output
                        ERROR_VARIABLE output)
        if(result EQUAL 0)
            break()
        endif()
        if(attempt LESS 3)
            message(STATUS "make-packages: hdiutil create, attempt ${attempt}: exit ${result}: "
                           "${output}")
            execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 5)
        endif()
    endforeach()
    if(NOT result EQUAL 0)
        fail("hdiutil" "hdiutil create failed three times; the last: exit ${result}:\n${output}")
    endif()
else()
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
endif()
set(outputs "${package}")

# The symbols archive: the exes' symbol files (the PDBs, the .debug files on Linux, the .dSYM
# bundles on macOS) at its root.
set(symbol_names "")
foreach(symbol_file IN LISTS RAWREEL_SYMBOL_FILES)
    if(NOT EXISTS "${symbol_file}")
        fail("symbols" "${symbol_file} is missing (build ${CONFIG} with packaging/symbols.cmake)")
    endif()
    if(symbols MATCHES "\\.zip$")
        file(COPY "${symbol_file}" DESTINATION "${WORK_DIR}/symbols")
    else()
        # Mode 644 in the tar, whatever the build tree's file system reports.
        file(COPY "${symbol_file}" DESTINATION "${WORK_DIR}/symbols"
             FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
    endif()
    cmake_path(GET symbol_file FILENAME name)
    list(APPEND symbol_names "${name}")
endforeach()
if(symbols MATCHES "\\.zip$")
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

# A bundled FFmpeg: SOURCE.txt as the package holds it names the corresponding source, the tag of
# the custom_ffmpeg release the prefix pins (the macOS .dmg is built from the stage folder).
set(source_txt_versions "")
if(RAWREEL_BUNDLED)
    if(RAWREEL_PLATFORM STREQUAL "macos")
        set(source_txt "${stage}/SOURCE.txt")
    else()
        set(member "rawreel-${version}/ffmpeg/SOURCE.txt")
        file(MAKE_DIRECTORY "${WORK_DIR}/source-txt")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${OUT_DIR}/${package}" "${member}"
                        WORKING_DIRECTORY "${WORK_DIR}/source-txt" RESULT_VARIABLE result)
        set(source_txt "${WORK_DIR}/source-txt/${member}")
    endif()
    if(NOT EXISTS "${source_txt}")
        fail("SOURCE.txt" "the package has no SOURCE.txt")
    endif()
    file(READ "${source_txt}" text)
    rawreel_source_txt_versions(TEXT "${text}" VERSIONS source_txt_versions)
    rawreel_ffmpeg_source_errors(NAME "${package}" CUSTOM_FFMPEG_VERSION
                                 "${RAWREEL_CUSTOM_FFMPEG_VERSION}" NAMED ${source_txt_versions}
                                 ERRORS errors)
    if(errors)
        fail("SOURCE.txt" "${errors}")
    endif()
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
set(RAWREEL_SOURCE_TXT_CUSTOM_FFMPEG \"${source_txt_versions}\")
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
