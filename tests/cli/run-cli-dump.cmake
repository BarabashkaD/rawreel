# rawreel-cli dump --via cpu (M2 Task 13): the original bytes reopen as a raw file, the planes of the
# raw file equal those of the synthetic source, and the CPU path runs no GPU converter.
include("${CMAKE_CURRENT_LIST_DIR}/cli_common.cmake")

set(uri "synthetic:noise?fmt=v210&size=34x17&seed=2")
run_cli(0 out err dump "${uri}" --count 3 --view original --via cpu --out clip_34x17_v210.yuv)

run_cli(0 json err info clip_34x17_v210.yuv --json)
expect_json("${json}" raw provider)
expect_json("${json}" v210 format name)
expect_json("${json}" 3 frames)

run_cli(0 a_json err dump "${uri}" --count 3 --view planes --via cpu --out a.planes --json)
run_cli(0 b_json err dump clip_34x17_v210.yuv --count 3 --view planes --via cpu --out b.planes)
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files a.planes b.planes
                WORKING_DIRECTORY "${WORK_DIR}" RESULT_VARIABLE differ)
if(differ)
    message(FATAL_ERROR "the planes of the raw file differ from those of the synthetic source")
endif()

foreach(name IN ITEMS HostToStaging Upload GpuUnpack Readback)
    converter_runs("${a_json}" ${name} runs)
    if(NOT runs EQUAL 0)
        message(FATAL_ERROR "dump --via cpu ran ${name} ${runs} times:\n${a_json}")
    endif()
endforeach()
converter_runs("${a_json}" CpuUnpack runs)
if(NOT runs GREATER 0)
    message(FATAL_ERROR "dump --view planes --via cpu ran no CpuUnpack:\n${a_json}")
endif()

# Y plus two half-width chroma planes at 16 bits, 3 frames.
file(SIZE "${WORK_DIR}/a.planes" size)
math(EXPR expected "3 * 34 * 17 * 2 * 2")
if(NOT size EQUAL expected)
    message(FATAL_ERROR "a.planes has ${size} bytes, expected ${expected}")
endif()
