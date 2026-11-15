# The macOS bundle identifiers of rawreel.app: the one place of both.
#
#   RAWREEL_RELEASE_BUNDLE_ID  The published app (the .dmg). make-packages.cmake writes it into the
#                              staged Info.plist before signing; package-smoke.cmake checks it.
#   RAWREEL_DEV_BUNDLE_ID      Every build-tree rawreel-gui.app (CMakeLists.txt). The build tree's
#                              app has no Vulkan driver of its own, so it must never share the
#                              release's identity: LaunchServices (Spotlight, Open With, open -b)
#                              picks any registered bundle of an identifier, and would start a build
#                              tree's app for the release.
#
#   rawreel_set_release_bundle_id(PLUTIL <path> PLIST <Info.plist> ERRORS <var>)
#     Writes RAWREEL_RELEASE_BUNDLE_ID as CFBundleIdentifier and reads it back; ERRORS receives one
#     message per problem (empty list = pass). Nothing else in the file changes.
include_guard(GLOBAL)

set(RAWREEL_RELEASE_BUNDLE_ID "io.github.barabashkad.rawreel")
set(RAWREEL_DEV_BUNDLE_ID "${RAWREEL_RELEASE_BUNDLE_ID}.dev")

function(rawreel_set_release_bundle_id)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "PLUTIL;PLIST;ERRORS" "")
    set(errors "")
    execute_process(COMMAND "${arg_PLUTIL}" -replace CFBundleIdentifier -string
                            "${RAWREEL_RELEASE_BUNDLE_ID}" "${arg_PLIST}"
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
        list(APPEND errors "plutil -replace CFBundleIdentifier: exit ${result}: ${output}")
    else()
        execute_process(COMMAND "${arg_PLUTIL}" -extract CFBundleIdentifier raw "${arg_PLIST}"
                        RESULT_VARIABLE result OUTPUT_VARIABLE value ERROR_VARIABLE value)
        string(STRIP "${value}" value)
        if(NOT result EQUAL 0)
            list(APPEND errors "plutil -extract CFBundleIdentifier: exit ${result}: ${value}")
        elseif(NOT value STREQUAL RAWREEL_RELEASE_BUNDLE_ID)
            list(APPEND errors "CFBundleIdentifier is '${value}', not '${RAWREEL_RELEASE_BUNDLE_ID}'")
        endif()
    endif()
    set(${arg_ERRORS} "${errors}" PARENT_SCOPE)
endfunction()
