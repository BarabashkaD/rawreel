# Release symbols for the Windows minidumps (design M4 v2 D16, D22). Included by CMakeLists.txt
# before FetchContent_MakeAvailable, so every target of this build (the dvv libraries included)
# gets a program database in Release too. The exes embed only the PDB file name (/PDBALTPATH with
# the target's PDB file name; the documented %_PDB% form reaches link.exe as %%%%_PDB%%%% through
# the Visual Studio generator's escaping); /OPT:REF and /OPT:ICF keep the Release
# optimisations that /DEBUG would otherwise turn off. Nothing changes under DVV_SANITIZE or on Linux.
include_guard(GLOBAL)

if(MSVC AND NOT DVV_SANITIZE)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
        "$<$<CONFIG:Debug,RelWithDebInfo,Release>:ProgramDatabase>")
endif()

function(rawreel_release_symbols target)
    if(MSVC AND NOT DVV_SANITIZE)
        target_link_options(${target} PRIVATE
            "$<$<CONFIG:Release>:/DEBUG;/PDBALTPATH:$<TARGET_PDB_FILE_NAME:${target}>;/OPT:REF;/OPT:ICF>")
    endif()
endfunction()
