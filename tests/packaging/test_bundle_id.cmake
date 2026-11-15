# The bundle identifiers of the built rawreel-gui.app (CTest rawreel.bundle_id, macOS). Run as
# cmake -DAPP=<rawreel-gui.app> -DWORK_DIR=<dir> -P test_bundle_id.cmake.
#   1. The build tree's app has RAWREEL_DEV_BUNDLE_ID, never the release's: it has no Vulkan driver
#      of its own, and Spotlight would start it for an installed release.
#   2. rawreel_set_release_bundle_id (make-packages.cmake's step) turns a copy of its Info.plist into
#      the release's and changes no other key.
# Each case prints "PASS <case>"; the first failed assertion stops the script.
cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS APP WORK_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "test_bundle_id: ${required} is required")
    endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/../../packaging/bundle-id.cmake")
find_program(plutil plutil PATHS /usr/bin NO_DEFAULT_PATH NO_CACHE REQUIRED)
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

# The value of a key of a plist ("" when the key is missing).
function(plist_value plist key out)
    execute_process(COMMAND "${plutil}" -extract "${key}" raw "${plist}"
                    RESULT_VARIABLE result OUTPUT_VARIABLE value ERROR_QUIET)
    string(STRIP "${value}" value)
    if(NOT result EQUAL 0)
        set(value "")
    endif()
    set(${out} "${value}" PARENT_SCOPE)
endfunction()

function(expect_equal case actual expected)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR "FAIL ${case}: expected '${expected}', got '${actual}'")
    endif()
endfunction()

set(built "${APP}/Contents/Info.plist")
if(NOT EXISTS "${built}")
    message(FATAL_ERROR "FAIL: ${built} does not exist")
endif()

set(case "the build tree's app has the dev identifier")
if(RAWREEL_DEV_BUNDLE_ID STREQUAL RAWREEL_RELEASE_BUNDLE_ID)
    message(FATAL_ERROR "FAIL ${case}: the dev and release identifiers are equal")
endif()
plist_value("${built}" CFBundleIdentifier id)
expect_equal("${case}" "${id}" "${RAWREEL_DEV_BUNDLE_ID}")
message(STATUS "PASS ${case}")

set(case "the release step writes the release identifier and nothing else")
set(copy "${WORK_DIR}/Info.plist")
file(COPY_FILE "${built}" "${copy}")
rawreel_set_release_bundle_id(PLUTIL "${plutil}" PLIST "${copy}" ERRORS errors)
expect_equal("${case}: errors" "${errors}" "")
plist_value("${copy}" CFBundleIdentifier id)
expect_equal("${case}" "${id}" "${RAWREEL_RELEASE_BUNDLE_ID}")
# Every other key (version, executable, document types) is unchanged: both files without the
# identifier, as XML, are equal.
set(before "${WORK_DIR}/built.plist")
file(COPY_FILE "${built}" "${before}")
foreach(file IN ITEMS before copy)
    execute_process(COMMAND "${plutil}" -remove CFBundleIdentifier "${${file}}"
                    COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${plutil}" -convert xml1 -o - "${${file}}"
                    OUTPUT_VARIABLE ${file}_text COMMAND_ERROR_IS_FATAL ANY)
endforeach()
if(NOT copy_text STREQUAL before_text)
    message(FATAL_ERROR "FAIL ${case}: keys other than CFBundleIdentifier changed")
endif()
message(STATUS "PASS ${case}")
