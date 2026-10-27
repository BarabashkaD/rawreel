# Release symbols for crash analysis (design M4 v2 D16, D22). Included by CMakeLists.txt before
# FetchContent_MakeAvailable, so every target of this build (the dvv libraries included) gets
# debug information in Release too. Nothing changes under DVV_SANITIZE.
#
# Windows: a program database per target. The exes embed only the PDB file name (/PDBALTPATH with
# the target's PDB file name; the documented %_PDB% form reaches link.exe as %%%%_PDB%%%% through
# the Visual Studio generator's escaping); /OPT:REF and /OPT:ICF keep the Release optimisations
# that /DEBUG would otherwise turn off.
#
# Linux: -g in the Release flags and a SHA-1 build ID in the exes. rawreel_release_symbols() splits
# the debug information into <exe>.debug next to the exe (as the PDB sits next to the exe on
# Windows), strips it from the exe and adds a debug link. The debug sections stay uncompressed (no
# -gz, no --compress-debug-sections), so the artifact privacy scan can read them. Only objcopy
# flags that GNU objcopy and llvm-objcopy share are used.
include_guard(GLOBAL)

if(MSVC AND NOT DVV_SANITIZE)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
        "$<$<CONFIG:Debug,RelWithDebInfo,Release>:ProgramDatabase>")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT DVV_SANITIZE)
    string(APPEND CMAKE_C_FLAGS_RELEASE " -g")
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " -g")
    if(NOT CMAKE_OBJCOPY)
        message(FATAL_ERROR "rawreel: objcopy is required for the Release debug symbols "
                            "(CMAKE_OBJCOPY is empty)")
    endif()
endif()

function(rawreel_release_symbols target)
    if(MSVC AND NOT DVV_SANITIZE)
        target_link_options(${target} PRIVATE
            "$<$<CONFIG:Release>:/DEBUG;/PDBALTPATH:$<TARGET_PDB_FILE_NAME:${target}>;/OPT:REF;/OPT:ICF>")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT DVV_SANITIZE)
        target_link_options(${target} PRIVATE "$<$<CONFIG:Release>:LINKER:--build-id=sha1>")
        set(exe "$<TARGET_FILE:${target}>")
        set(release "$<CONFIG:Release>")
        set(skip "${CMAKE_COMMAND};-E;true")
        set(keep "${CMAKE_OBJCOPY};--only-keep-debug;${exe};${exe}.debug")
        set(strip "${CMAKE_OBJCOPY};--strip-debug;--add-gnu-debuglink=${exe}.debug;${exe}")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "$<IF:${release},${keep},${skip}>"
            COMMAND "$<IF:${release},${strip},${skip}>"
            COMMENT "Splitting the debug symbols of ${target}"
            COMMAND_EXPAND_LISTS VERBATIM)
    endif()
endfunction()
