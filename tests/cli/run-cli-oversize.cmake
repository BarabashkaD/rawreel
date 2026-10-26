# A frame over the GPU image limit (M4 Task 11-pre): dump --via gpu
# of the planes view needs one image per plane, so a plane wider or taller than the device's
# largest image is refused with exit 3 (UNSUPPORTED) and a clear message, without a Vulkan
# validation message or a leak. 65532 (synthetic: allows up to 65535) is over
# maxImageDimension2D of every current consumer device (NVIDIA 32768, AMD and Intel 16384). The
# original view goes through buffers, so it still works and writes the bytes of --via cpu.
include("${CMAKE_CURRENT_LIST_DIR}/cli_common.cmake")

# Without a usable device the CLI exits 4 with "no usable GPU device"; that is a skip (77) unless
# DVV_REQUIRE_GPU=1.
execute_process(COMMAND "${CLI}" dump "synthetic:noise?fmt=gray&size=2x2" --via gpu
                        --out probe.raw WORKING_DIRECTORY "${WORK_DIR}"
                RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(code EQUAL 4 AND err MATCHES "no usable GPU device")
    if("$ENV{DVV_REQUIRE_GPU}" STREQUAL "1")
        message(FATAL_ERROR "DVV_REQUIRE_GPU=1, but: ${err}")
    endif()
    message(STATUS "skipped: ${err}")
    cmake_language(EXIT 77)
endif()
if(NOT code EQUAL 0)
    message(FATAL_ERROR "probe dump failed with ${code}:\n${err}")
endif()

foreach(case IN ITEMS "gray 65532x8 R8Uint" "v210 65532x2 R16Uint" "yuv420p 8x65532 R8Uint")
    string(REPLACE " " ";" case "${case}")
    list(GET case 0 format)
    list(GET case 1 size)
    list(GET case 2 image)
    set(uri "synthetic:noise?fmt=${format}&size=${size}&seed=3")
    # A device that allows the image cannot run this case; say so instead of "exit 0, expected 3"
    # (never a silent skip: that would hide a broken limit check). v1.x: info --json reports the
    # limit, and the case skips with 77 on such a device.
    execute_process(COMMAND "${CLI}" dump "${uri}" --view planes --via gpu
                            --out ${format}-planes.bin WORKING_DIRECTORY "${WORK_DIR}"
                    RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(code EQUAL 0)
        message(FATAL_ERROR "the device allows a ${size} image (limit >= 65536?); this case needs "
                            "a device limit below 65532; see the M4 Task 11-pre deviation")
    endif()
    run_cli(3 out err dump "${uri}" --view planes --via gpu --out ${format}-planes.bin)
    expect_match("${err}"
                 "Unsupported: image '[^']+': ${size} exceeds the largest ${image} image of device"
                 "${format} ${size} planes")
    expect_no_validation("${err}" "${format} ${size} planes")
    run_cli(3 json err dump "${uri}" --view planes --via gpu --out ${format}-planes.bin --json)
    expect_json("${json}" "Unsupported" error code)
    expect_no_validation("${err}" "${format} ${size} planes --json")

    run_cli(0 out err dump "${uri}" --view original --via cpu --out ${format}-cpu.bin)
    run_cli(0 out err dump "${uri}" --view original --via gpu --out ${format}-gpu.bin)
    expect_no_validation("${err}" "${format} ${size} original")
    execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${format}-cpu.bin ${format}-gpu.bin
                    WORKING_DIRECTORY "${WORK_DIR}" RESULT_VARIABLE differ)
    if(differ)
        message(FATAL_ERROR "${format} ${size}: original --via gpu differs from --via cpu")
    endif()
endforeach()
