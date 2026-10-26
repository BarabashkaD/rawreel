# Packages of rawreel v1.0 (design M4 v2 D16-D19, D27, §8.6): install rules for the component
# "rawreel" (Release only), the bundled FFmpeg libraries when the third-party prefix ships them, and
# CPack. make-packages.cmake drives it. Included by CMakeLists.txt only without DVV_SANITIZE
# (rawreel-gui is not built there).
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/check-package.cmake")

# The zip ships no VC++ runtime; users install the Microsoft Visual C++ Redistributable
# (x64), at least as recent as the build tools (Microsoft's rule). This is the one place of that
# minimum: it fails when the toolset is newer or when README.md or the release notes are missing or
# do not name it. It also covers the custom_ffmpeg DLLs (Task 0: MSVC STL of VS 2022 17.14, runtime
# 14.44). Every configure only warns; the packaging path (the Release install of the
# component "rawreel", which CPack runs) fails, so Debug and PR builds stay green.
set(RAWREEL_MIN_VCRT_VERSION "14.51")
set(rawreel_redist_docs "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
                        "${CMAKE_CURRENT_SOURCE_DIR}/docs/release-notes-${PROJECT_VERSION}.md")
set(rawreel_msvc_version "")
if(MSVC)
    set(rawreel_msvc_version MSVC_VERSION "${CMAKE_CXX_COMPILER_VERSION}")
endif()
rawreel_redist_minimum_errors(MINIMUM "${RAWREEL_MIN_VCRT_VERSION}" ${rawreel_msvc_version}
                              DOCS ${rawreel_redist_docs} ERRORS rawreel_redist_errors)
foreach(rawreel_doc IN LISTS rawreel_redist_docs)
    if(EXISTS "${rawreel_doc}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${rawreel_doc}")
    endif()
endforeach()
if(rawreel_redist_errors)
    list(JOIN rawreel_redist_errors "; " rawreel_redist_errors)
    set(rawreel_redist_errors
        "${rawreel_redist_errors} (update RAWREEL_MIN_VCRT_VERSION in packaging/packaging.cmake, "
        "README.md and the release notes together)")
    string(JOIN "" rawreel_redist_errors ${rawreel_redist_errors})
    message(WARNING "rawreel packaging: ${rawreel_redist_errors}; packaging will fail")
endif()

find_file(RAWREEL_THIRD_PARTY_STATE dvv-third-party-state.cmake
          PATHS ${CMAKE_PREFIX_PATH} NO_DEFAULT_PATH NO_CACHE)
if(NOT RAWREEL_THIRD_PARTY_STATE)
    message(STATUS "rawreel: the third-party prefix has no dvv-third-party-state.cmake; "
                   "no package rules")
    return()
endif()
include("${RAWREEL_THIRD_PARTY_STATE}")
cmake_path(GET RAWREEL_THIRD_PARTY_STATE PARENT_PATH rawreel_prefix)

# v1.0 ships a Windows zip only; the Linux .tar.xz is v1.x. Other platforms
# configure without package rules.
if(NOT WIN32)
    message(STATUS "rawreel: packages are built on Windows only in v1.0 (the Linux package is v1.x)")
    return()
endif()

# The packaging path: the Release install of the package component stops on a redist
# error before it installs anything.
if(rawreel_redist_errors)
    string(REPLACE "\"" "\\\"" rawreel_redist_quoted "${rawreel_redist_errors}")
    install(CODE "message(FATAL_ERROR \"rawreel packaging: ${rawreel_redist_quoted}\")"
            COMPONENT rawreel CONFIGURATIONS Release)
endif()

set(rawreel_platform windows)
set(rawreel_package_name "rawreel-${PROJECT_VERSION}-windows-x64")
set(rawreel_extension zip)
set(rawreel_ffmpeg_dir "ffmpeg")

install(TARGETS rawreel_gui rawreel_cli
        RUNTIME DESTINATION . COMPONENT rawreel CONFIGURATIONS Release)
# No VC++ runtime DLL in the zip, so its notice paragraph goes too (the prefix's notices
# may still carry it).
set(rawreel_notices "${CMAKE_CURRENT_BINARY_DIR}/packaging/THIRD_PARTY_NOTICES.txt")
rawreel_strip_msvc_notice(IN "${rawreel_prefix}/THIRD_PARTY_NOTICES.txt" OUT "${rawreel_notices}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${rawreel_prefix}/THIRD_PARTY_NOTICES.txt")
install(FILES README.md LICENSE "${rawreel_notices}"
        DESTINATION . COMPONENT rawreel CONFIGURATIONS Release)

set(rawreel_ffmpeg_version "")
set(rawreel_supplement_pins "")
if(DVV_BUNDLE_FFMPEG)
    file(GLOB_RECURSE rawreel_avutil LIST_DIRECTORIES false "${DVV_CUSTOM_FFMPEG_DIR}/avutil-*.dll")
    list(FILTER rawreel_avutil INCLUDE REGEX "/avutil-[0-9]+\\.dll$")
    list(LENGTH rawreel_avutil rawreel_count)
    if(NOT rawreel_count EQUAL 1)
        message(FATAL_ERROR "rawreel packaging: expected one avutil library below "
                            "${DVV_CUSTOM_FFMPEG_DIR}, found ${rawreel_count}")
    endif()
    cmake_path(GET rawreel_avutil PARENT_PATH rawreel_ffmpeg_libdir)
    cmake_path(GET rawreel_ffmpeg_libdir PARENT_PATH rawreel_ffmpeg_root)
    set(rawreel_library_versions "")
    foreach(library IN ITEMS avutil avcodec avformat avfilter swscale swresample)
        file(GLOB found LIST_DIRECTORIES false "${rawreel_ffmpeg_libdir}/${library}-*.dll")
        list(FILTER found INCLUDE REGEX "/${library}-[0-9]+\\.dll$")
        list(LENGTH found rawreel_count)
        if(NOT rawreel_count EQUAL 1)
            message(FATAL_ERROR "rawreel packaging: expected one ${library} library in "
                                "${rawreel_ffmpeg_libdir}, found ${rawreel_count}")
        endif()
        install(FILES "${found}" DESTINATION "${rawreel_ffmpeg_dir}"
                COMPONENT rawreel CONFIGURATIONS Release)
        # The version the loader requires (dvv-components checks.cpp): the same major, at least
        # the minor of the headers rawreel is built with.
        string(TOUPPER "${library}" upper)
        file(GLOB headers "${rawreel_ffmpeg_root}/include/lib${library}/version*.h")
        set(major "")
        set(minor "")
        foreach(header IN LISTS headers)
            file(STRINGS "${header}" defines REGEX "^#define LIB${upper}_VERSION_(MAJOR|MINOR) ")
            foreach(define IN LISTS defines)
                if(define MATCHES "_VERSION_MAJOR +([0-9]+)")
                    set(major "${CMAKE_MATCH_1}")
                elseif(define MATCHES "_VERSION_MINOR +([0-9]+)")
                    set(minor "${CMAKE_MATCH_1}")
                endif()
            endforeach()
        endforeach()
        if(major STREQUAL "" OR minor STREQUAL "")
            message(FATAL_ERROR "rawreel packaging: no version of ${library} in "
                                "${rawreel_ffmpeg_root}/include/lib${library}")
        endif()
        list(APPEND rawreel_library_versions "${library} ${major}.${minor}")
    endforeach()
    list(JOIN rawreel_library_versions ", " FFMPEG_LIBRARY_VERSIONS)
    install(DIRECTORY "${rawreel_ffmpeg_root}/share/licenses/"
            DESTINATION "${rawreel_ffmpeg_dir}/licenses" COMPONENT rawreel CONFIGURATIONS Release)
    # Task 0 layout: build-info.txt is share/custom_ffmpeg/build-info.txt below the FFmpeg root.
    install(FILES "${rawreel_ffmpeg_root}/share/custom_ffmpeg/build-info.txt"
            DESTINATION "${rawreel_ffmpeg_dir}" COMPONENT rawreel CONFIGURATIONS Release)

    # The licence supplements of custom_ffmpeg (texts its archive lacks; Task 1):
    # the superbuild copies the SHA256-pinned files into the prefix unchanged and records
    # "<path>|<sha256>" for each. They ship as they are, as
    # ffmpeg/licenses/<path>; each one must still match its pin.
    if(NOT DEFINED DVV_CUSTOM_FFMPEG_LICENCE_SUPPLEMENTS)
        message(FATAL_ERROR "rawreel packaging: dvv-third-party-state.cmake has no "
                            "DVV_CUSTOM_FFMPEG_LICENCE_SUPPLEMENTS; configure the superbuild again")
    endif()
    rawreel_licence_supplements(DIR "${DVV_CUSTOM_FFMPEG_LICENCE_SUPPLEMENTS_DIR}"
                                SUPPLEMENTS ${DVV_CUSTOM_FFMPEG_LICENCE_SUPPLEMENTS}
                                PATHS rawreel_supplements ERRORS rawreel_errors)
    if(rawreel_errors)
        list(JOIN rawreel_errors "; " rawreel_errors)
        message(FATAL_ERROR "rawreel packaging: ${rawreel_errors}")
    endif()
    set(rawreel_supplement_pins "${DVV_CUSTOM_FFMPEG_LICENCE_SUPPLEMENTS}")
    foreach(path IN LISTS rawreel_supplements)
        cmake_path(GET path PARENT_PATH folder)
        install(FILES "${DVV_CUSTOM_FFMPEG_LICENCE_SUPPLEMENTS_DIR}/${path}"
                DESTINATION "${rawreel_ffmpeg_dir}/licenses/${folder}"
                COMPONENT rawreel CONFIGURATIONS Release)
    endforeach()

    file(STRINGS "${rawreel_ffmpeg_root}/include/libavutil/ffversion.h" line
         REGEX "#define FFMPEG_VERSION \"")
    string(REGEX REPLACE ".*FFMPEG_VERSION \"([^\"]+)\".*" "\\1" rawreel_ffmpeg_version "${line}")
    set(CUSTOM_FFMPEG_VERSION "${DVV_CUSTOM_FFMPEG_VERSION}")
    set(FFMPEG_VERSION "${rawreel_ffmpeg_version}")
    set(RAWREEL_VERSION "${PROJECT_VERSION}")
    set(RAWREEL_SOURCES_ARCHIVE "rawreel-${PROJECT_VERSION}-third-party-sources.tar.xz")
    # The LAME sentence from the pin (empty when the prefix records no LAME source release) and the
    # LGPL components, as in the sources archive's README (make-third-party-sources.cmake).
    set(RAWREEL_LAME_SENTENCE "")
    set(RAWREEL_LGPL_COMPONENTS "FFmpeg")
    if(NOT "${DVV_LAME_SOURCE_URL}" STREQUAL "")
        set(RAWREEL_LGPL_COMPONENTS "FFmpeg and LAME")
        cmake_path(GET DVV_LAME_SOURCE_URL FILENAME rawreel_lame_file)
        if(NOT rawreel_lame_file MATCHES "^lame-([0-9][0-9.]*[0-9])\\.tar\\.gz$")
            message(FATAL_ERROR "rawreel packaging: DVV_LAME_SOURCE_URL names no "
                                "lame-<version>.tar.gz")
        endif()
        string(CONCAT RAWREEL_LAME_SENTENCE " and the LAME ${CMAKE_MATCH_1} source release "
                      "(${rawreel_lame_file})")
    endif()
    configure_file(packaging/SOURCE.txt.in "${CMAKE_CURRENT_BINARY_DIR}/packaging/SOURCE.txt"
                   @ONLY NEWLINE_STYLE LF)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/packaging/SOURCE.txt"
            DESTINATION "${rawreel_ffmpeg_dir}" COMPONENT rawreel CONFIGURATIONS Release)
endif()

set(CPACK_PACKAGE_NAME rawreel)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "rawreel-${PROJECT_VERSION}")  # also the top folder; renamed later
set(CPACK_GENERATOR ZIP)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};rawreel;rawreel;/")
set(CPACK_ARCHIVE_THREADS 0)
set(CPACK_OUTPUT_CONFIG_FILE "${CMAKE_BINARY_DIR}/rawreel-CPackConfig.cmake")
set(CPACK_SOURCE_OUTPUT_CONFIG_FILE "${CMAKE_BINARY_DIR}/rawreel-CPackSourceConfig.cmake")
include(CPack)

set(rawreel_pdbs "$<TARGET_PDB_FILE:rawreel_gui>;$<TARGET_PDB_FILE:rawreel_cli>")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/packaging-info-$<CONFIG>.cmake" CONTENT
"set(RAWREEL_VERSION \"${PROJECT_VERSION}\")
set(RAWREEL_PLATFORM \"${rawreel_platform}\")
set(RAWREEL_PACKAGE_NAME \"${rawreel_package_name}\")
set(RAWREEL_EXTENSION \"${rawreel_extension}\")
set(RAWREEL_BUNDLED ${DVV_BUNDLE_FFMPEG})
set(RAWREEL_SHIPPED \"${DVV_SHIPPED_THIRD_PARTY}\")
set(RAWREEL_FFMPEG_VERSION \"${rawreel_ffmpeg_version}\")
set(RAWREEL_CUSTOM_FFMPEG_VERSION \"${DVV_CUSTOM_FFMPEG_VERSION}\")
set(RAWREEL_CPACK_CONFIG \"${CMAKE_BINARY_DIR}/rawreel-CPackConfig.cmake\")
set(RAWREEL_THIRD_PARTY_STATE \"${RAWREEL_THIRD_PARTY_STATE}\")
set(RAWREEL_PDBS \"${rawreel_pdbs}\")
set(RAWREEL_GUI_EXE \"$<TARGET_FILE:rawreel_gui>\")
set(RAWREEL_LICENCE_SUPPLEMENTS \"${rawreel_supplement_pins}\")
")
