# Release symbols for crash analysis. Included by CMakeLists.txt before
# dvv_dependencies(), so every target of this build (the dvv libraries included) gets
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
#
# macOS: -g for every language in Release (a compile option of this folder and the ones below it,
# so OBJCXX, enabled later, gets it too and no *_FLAGS_RELEASE variable shadows its defaults).
# rawreel_release_symbols() runs dsymutil, which gathers the DWARF of the object files into
# <exe>.dSYM next to the exe (next to the .app for a bundle, so the bundle holds no symbols), then
# strip -S drops the debug map from the exe. The exe's UUID ties the two together; strip keeps the
# linker's ad-hoc signature valid.
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
elseif(APPLE AND NOT DVV_SANITIZE)
    add_compile_options("$<$<CONFIG:Release>:-g>")
    # The dsymutil and strip of the compiler's toolchain (/usr/bin with the presets' Apple clang),
    # never others on PATH such as Homebrew LLVM's.
    cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH rawreel_toolchain_bin)
    find_program(rawreel_dsymutil dsymutil PATHS "${rawreel_toolchain_bin}" NO_DEFAULT_PATH
                 NO_CACHE)
    find_program(rawreel_strip strip PATHS "${rawreel_toolchain_bin}" NO_DEFAULT_PATH NO_CACHE)
    if(NOT rawreel_dsymutil OR NOT rawreel_strip)
        message(FATAL_ERROR "rawreel: dsymutil and strip (next to ${CMAKE_CXX_COMPILER}) are "
                            "required for the Release debug symbols (install the Xcode Command "
                            "Line Tools)")
    endif()
endif()

# The symbol file of an exe (a generator expression): <exe>.dSYM next to the exe, or next to the
# .app of a bundle (macOS). Windows and Linux use the PDB and <exe>.debug (packaging.cmake).
function(rawreel_dsym_path target out)
    get_target_property(bundle ${target} MACOSX_BUNDLE)
    if(bundle)
        set(dir "$<PATH:GET_PARENT_PATH,$<TARGET_BUNDLE_DIR:${target}>>")
    else()
        set(dir "$<TARGET_FILE_DIR:${target}>")
    endif()
    set(${out} "${dir}/$<TARGET_FILE_NAME:${target}>.dSYM" PARENT_SCOPE)
endfunction()

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
    elseif(APPLE AND NOT DVV_SANITIZE)
        set(exe "$<TARGET_FILE:${target}>")
        rawreel_dsym_path(${target} dsym)
        set(release "$<CONFIG:Release>")
        set(skip "${CMAKE_COMMAND};-E;true")
        # A fresh .dSYM for every link, so no file of an earlier one stays in it.
        set(clean "${CMAKE_COMMAND};-E;rm;-rf;${dsym}")
        set(keep "${rawreel_dsymutil};${exe};-o;${dsym}")
        # dsymutil also writes Contents/Resources/Relocations/<arch>/<exe>.yml: the exe's absolute
        # build path and its Swift reflection relocations (rawreel has no Swift, so none), which
        # only a later dsymutil run reads; debuggers read the DWARF. It goes (no machine path in
        # the symbols archive).
        set(relocations "${CMAKE_COMMAND};-E;rm;-rf;${dsym}/Contents/Resources/Relocations")
        set(strip "${rawreel_strip};-S;${exe}")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "$<IF:${release},${clean},${skip}>"
            COMMAND "$<IF:${release},${keep},${skip}>"
            COMMAND "$<IF:${release},${relocations},${skip}>"
            COMMAND "$<IF:${release},${strip},${skip}>"
            COMMENT "Writing the dSYM of ${target}"
            COMMAND_EXPAND_LISTS VERBATIM)
    endif()
endfunction()
