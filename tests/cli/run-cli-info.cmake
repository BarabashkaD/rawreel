# rawreel-cli info (M2 Task 13): text and JSON, for a synthetic, a raw and a Y4M source.
include("${CMAKE_CURRENT_LIST_DIR}/cli_common.cmake")

set(gradient "synthetic:gradient?fmt=v210&size=34x17")
run_cli(0 out err info "${gradient}")
expect_match("${out}" "format: v210" "info text")
expect_match("${out}" "size: 34x17" "info text")

run_cli(0 json err info "${gradient}" --json)
expect_json("${json}" synthetic provider)
expect_json("${json}" v210 format name)
expect_json("${json}" 34 width)
expect_json("${json}" 10 frames)

# A raw file: its name carries the size and the format.
run_cli(0 out err dump "synthetic:noise?fmt=yuv420p&size=4x2" --count 2 --out a_4x2_yuv420p.yuv)
run_cli(0 json err info a_4x2_yuv420p.yuv --json)
expect_json("${json}" raw provider)
expect_json("${json}" 2 frames)

# A Y4M file of two 4x2 gray frames; file(CONFIGURE) writes LF line ends and appends one LF, which
# the provider reports as a trailing byte.
file(CONFIGURE OUTPUT "${WORK_DIR}/y.y4m"
     CONTENT "YUV4MPEG2 W4 H2 F25:1 Cmono\nFRAME\nABCDEFGHFRAME\nIJKLMNOP" NEWLINE_STYLE LF)
run_cli(0 json err info y.y4m --json)
expect_json("${json}" y4m provider)
expect_json("${json}" gray format name)
expect_json("${json}" 2 frames)
expect_json("${json}" "1 trailing bytes ignored" warnings 0)

run_cli(0 out err dump y.y4m --frame 1 --view original --via cpu --out f1.raw)
expect_file(f1.raw "IJKLMNOP")
