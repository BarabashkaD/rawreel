# Packages of rawreel: install rules for the component
# "rawreel" (Release only), the bundled FFmpeg libraries when the third-party prefix ships them, and
# CPack. make-packages.cmake drives it. Included by CMakeLists.txt only without DVV_SANITIZE
# (rawreel-gui is not built there).
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/check-package.cmake")

# The zip ships no VC++ runtime; users install the Microsoft Visual C++ Redistributable
# (x64), at least as recent as the build tools (Microsoft's rule). This is the one place of that
# minimum: it fails when the toolset is newer or when README.md or the release notes are missing or
# do not name it. It also covers the custom_ffmpeg DLLs (MSVC STL of VS 2022 17.14, runtime
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

# v1.0 ships a Windows zip and a Linux tar.xz. Other platforms configure without package rules.
if(WIN32)
    set(rawreel_platform windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(rawreel_platform linux)
else()
    message(STATUS "rawreel: packages are built on Windows and Linux only")
    return()
endif()

# The packaging path on Windows: the Release install of the package component stops on a redist
# error before it installs anything.
if(WIN32 AND rawreel_redist_errors)
    string(REPLACE "\"" "\\\"" rawreel_redist_quoted "${rawreel_redist_errors}")
    install(CODE "message(FATAL_ERROR \"rawreel packaging: ${rawreel_redist_quoted}\")"
            COMPONENT rawreel CONFIGURATIONS Release)
endif()

# The Linux package's runtime requirements. The exes may need only these system libraries
# (package-smoke fails on any other NEEDED soname); RAWREEL_LINUX_APT_PACKAGES is the package side
# of the same list, named in README.md. Both were derived once from the NEEDED entries of the
# Release exes, resolved on the build host with dpkg -S. The Vulkan loader is not linked
# (dvv-gpu loads libvulkan.so.1 at run time), so libvulkan1 and a Vulkan driver are named here.
set(RAWREEL_LINUX_SYSTEM_SONAMES
    # libc6, libgcc-s1, libstdc++6
    ld-linux-x86-64.so.2 libc.so.6 libm.so.6 libgcc_s.so.1 libstdc++.so.6
    # libgtk-3-0t64 and the libraries it depends on
    libgtk-3.so.0 libgdk-3.so.0 libatk-1.0.so.0 libcairo.so.2 libcairo-gobject.so.2
    libgdk_pixbuf-2.0.so.0 libgio-2.0.so.0 libglib-2.0.so.0 libgobject-2.0.so.0 libharfbuzz.so.0
    libpango-1.0.so.0 libpangocairo-1.0.so.0 libpng16.so.16 libz.so.1 libX11.so.6 libXext.so.6
    libxkbcommon.so.0
    # libsm6, libice6 (libgtk-3-0t64 does not pull them in without Recommends)
    libSM.so.6 libICE.so.6)
set(RAWREEL_LINUX_APT_PACKAGES libgtk-3-0t64 libsm6 libice6 libvulkan1 mesa-vulkan-drivers)
# README.md and the release notes name the same list in their apt install line; the packaging
# path on Linux stops before it installs anything when they differ.
if(rawreel_platform STREQUAL "linux")
    rawreel_apt_packages_doc_errors(PACKAGES ${RAWREEL_LINUX_APT_PACKAGES}
                                    DOCS ${rawreel_redist_docs} ERRORS rawreel_apt_errors)
    if(rawreel_apt_errors)
        list(JOIN rawreel_apt_errors "; " rawreel_apt_errors)
        string(CONCAT rawreel_apt_errors "${rawreel_apt_errors} (update RAWREEL_LINUX_APT_PACKAGES "
                      "in packaging/packaging.cmake, README.md and the release notes together)")
        message(WARNING "rawreel packaging: ${rawreel_apt_errors}; packaging will fail")
        string(REPLACE "\"" "\\\"" rawreel_apt_quoted "${rawreel_apt_errors}")
        install(CODE "message(FATAL_ERROR \"rawreel packaging: ${rawreel_apt_quoted}\")"
                COMPONENT rawreel CONFIGURATIONS Release)
    endif()
endif()
# The newest GLIBC and GLIBCXX symbol versions any ELF file of the Linux package may need:
# the maxima of the release runner (Ubuntu 24.04), so on that runner the check cannot fail; it
# guards against a runner or toolchain change. The current need is GLIBC_2.38 and GLIBCXX_3.4.32.
# CXXABI_ and GCC_ versions are not checked here: the release workflow's clean-container run
# (a stock Ubuntu 24.04) fails on any version its libstdc++ or libgcc_s lacks.
set(RAWREEL_LINUX_GLIBC_MAX 2.39)
set(RAWREEL_LINUX_GLIBCXX_MAX 3.4.33)

rawreel_package_files(VERSION "${PROJECT_VERSION}" PLATFORM "${rawreel_platform}"
                      PACKAGE rawreel_package_file SYMBOLS rawreel_symbols_file
                      CPACK rawreel_cpack_file EXTENSION rawreel_extension
                      GENERATOR rawreel_generator ERRORS rawreel_errors)
set(rawreel_package_name "rawreel-${PROJECT_VERSION}-${rawreel_platform}-x64")
set(rawreel_ffmpeg_dir "ffmpeg")

install(TARGETS rawreel_gui rawreel_cli
        RUNTIME DESTINATION . COMPONENT rawreel CONFIGURATIONS Release)
# No VC++ runtime DLL in the zip, so its notice paragraph goes too (the prefix's notices
# may still carry it; the Linux prefix's notices have no such paragraph, so nothing changes).
set(rawreel_notices "${CMAKE_CURRENT_BINARY_DIR}/packaging/THIRD_PARTY_NOTICES.txt")
rawreel_strip_msvc_notice(IN "${rawreel_prefix}/THIRD_PARTY_NOTICES.txt" OUT "${rawreel_notices}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${rawreel_prefix}/THIRD_PARTY_NOTICES.txt")
install(FILES README.md LICENSE "${rawreel_notices}"
        DESTINATION . COMPONENT rawreel CONFIGURATIONS Release)

set(rawreel_ffmpeg_version "")
set(rawreel_supplement_pins "")
if(DVV_BUNDLE_FFMPEG)
    rawreel_ffmpeg_library_pattern(PLATFORM "${rawreel_platform}" LIBRARY avutil
                                   GLOB rawreel_glob REGEX rawreel_regex)
    file(GLOB_RECURSE rawreel_avutil LIST_DIRECTORIES false
         "${DVV_CUSTOM_FFMPEG_DIR}/${rawreel_glob}")
    list(FILTER rawreel_avutil INCLUDE REGEX "${rawreel_regex}")
    list(LENGTH rawreel_avutil rawreel_count)
    if(NOT rawreel_count EQUAL 1)
        message(FATAL_ERROR "rawreel packaging: expected one avutil library below "
                            "${DVV_CUSTOM_FFMPEG_DIR}, found ${rawreel_count}")
    endif()
    cmake_path(GET rawreel_avutil PARENT_PATH rawreel_ffmpeg_libdir)
    cmake_path(GET rawreel_ffmpeg_libdir PARENT_PATH rawreel_ffmpeg_root)
    set(rawreel_library_versions "")
    foreach(library IN ITEMS avutil avcodec avformat avfilter swscale swresample)
        rawreel_ffmpeg_library_pattern(PLATFORM "${rawreel_platform}" LIBRARY ${library}
                                       GLOB rawreel_glob REGEX rawreel_regex)
        file(GLOB found LIST_DIRECTORIES false "${rawreel_ffmpeg_libdir}/${rawreel_glob}")
        list(FILTER found INCLUDE REGEX "${rawreel_regex}")
        list(LENGTH found rawreel_count)
        if(NOT rawreel_count EQUAL 1)
            message(FATAL_ERROR "rawreel packaging: expected one ${library} library in "
                                "${rawreel_ffmpeg_libdir}, found ${rawreel_count}")
        endif()
        string(REGEX MATCH "${rawreel_regex}" ignored "${found}")
        set(file_major "${CMAKE_MATCH_1}")
        if(rawreel_platform STREQUAL "linux")
            # One real file named by its SONAME (the name the loader opens and the siblings'
            # NEEDED entries use), no symbolic links; the libraries keep their RUNPATH $ORIGIN.
            cmake_path(GET found FILENAME soname)
            file(REAL_PATH "${found}" real)
            install(PROGRAMS "${real}" DESTINATION "${rawreel_ffmpeg_dir}" RENAME "${soname}"
                    COMPONENT rawreel CONFIGURATIONS Release)
        else()
            install(FILES "${found}" DESTINATION "${rawreel_ffmpeg_dir}"
                    COMPONENT rawreel CONFIGURATIONS Release)
        endif()
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
        # The loader opens the library by the headers' major, so the file name must carry it.
        if(NOT file_major STREQUAL major)
            message(FATAL_ERROR "rawreel packaging: ${found} has the major version "
                                "${file_major}, the headers ${major}")
        endif()
        list(APPEND rawreel_library_versions "${library} ${major}.${minor}")
    endforeach()
    list(JOIN rawreel_library_versions ", " FFMPEG_LIBRARY_VERSIONS)
    install(DIRECTORY "${rawreel_ffmpeg_root}/share/licenses/"
            DESTINATION "${rawreel_ffmpeg_dir}/licenses" COMPONENT rawreel CONFIGURATIONS Release)
    # build-info.txt is share/custom_ffmpeg/build-info.txt below the FFmpeg root.
    install(FILES "${rawreel_ffmpeg_root}/share/custom_ffmpeg/build-info.txt"
            DESTINATION "${rawreel_ffmpeg_dir}" COMPONENT rawreel CONFIGURATIONS Release)

    # The licence supplements of custom_ffmpeg (texts its archive lacks):
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
set(CPACK_GENERATOR ${rawreel_generator})
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};rawreel;rawreel;/")
set(CPACK_ARCHIVE_THREADS 0)
set(CPACK_OUTPUT_CONFIG_FILE "${CMAKE_BINARY_DIR}/rawreel-CPackConfig.cmake")
set(CPACK_SOURCE_OUTPUT_CONFIG_FILE "${CMAKE_BINARY_DIR}/rawreel-CPackSourceConfig.cmake")
include(CPack)

# The symbol files of the exes: the PDBs on Windows, the split .debug files on Linux
# (packaging/symbols.cmake).
if(WIN32)
    set(rawreel_symbol_files "$<TARGET_PDB_FILE:rawreel_gui>;$<TARGET_PDB_FILE:rawreel_cli>")
else()
    set(rawreel_symbol_files "$<TARGET_FILE:rawreel_gui>.debug;$<TARGET_FILE:rawreel_cli>.debug")
endif()
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
set(RAWREEL_SYMBOL_FILES \"${rawreel_symbol_files}\")
set(RAWREEL_GUI_EXE \"$<TARGET_FILE:rawreel_gui>\")
set(RAWREEL_LICENCE_SUPPLEMENTS \"${rawreel_supplement_pins}\")
set(RAWREEL_LINUX_SYSTEM_SONAMES \"${RAWREEL_LINUX_SYSTEM_SONAMES}\")
set(RAWREEL_LINUX_APT_PACKAGES \"${RAWREEL_LINUX_APT_PACKAGES}\")
set(RAWREEL_LINUX_GLIBC_MAX \"${RAWREEL_LINUX_GLIBC_MAX}\")
set(RAWREEL_LINUX_GLIBCXX_MAX \"${RAWREEL_LINUX_GLIBCXX_MAX}\")
")
