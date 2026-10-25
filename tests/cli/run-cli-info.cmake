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

# M3.1 U-IO2: the file read mode. Auto (no rawreel.json next to rawreel-cli) reads unbuffered on
# Windows (WORK_DIR is on NTFS) and buffered elsewhere; --config forces a mode; a missing or
# invalid --config file gives one warning and the defaults.
if(CMAKE_HOST_WIN32)
    set(auto_reads "unbuffered")
else()
    set(auto_reads "buffered")
endif()
# A rawreel.json in the working directory is ignored: only the one next to the executable counts.
file(WRITE "${WORK_DIR}/rawreel.json" "{ \"io\": { \"fileReads\": \"buffered\" } }\n")
run_cli(0 out err info a_4x2_yuv420p.yuv)
expect_match("${out}" "\nreads: ${auto_reads}\n" "info text, default config")
file(REMOVE "${WORK_DIR}/rawreel.json")
# A missing --config file gives one warning and the defaults.
run_cli(0 out err info a_4x2_yuv420p.yuv --config missing.json)
expect_match("${out}" "\nreads: ${auto_reads}\n" "info text, missing config")
expect_match("${err}" "config '[^']*missing.json' ignored: not found" "warning, missing config")
file(WRITE "${WORK_DIR}/buffered.json" "{ \"io\": { \"fileReads\": \"buffered\" } }\n")
run_cli(0 out err info a_4x2_yuv420p.yuv --config buffered.json)
expect_match("${out}" "\nreads: buffered\n" "info text, --config buffered")
run_cli(0 json err info a_4x2_yuv420p.yuv --json --config "${WORK_DIR}/buffered.json")
expect_json("${json}" buffered reader)
file(WRITE "${WORK_DIR}/bad.json" "{ \"io\": { \"fileReads\": \"fast\" } }\n")
run_cli(0 out err info a_4x2_yuv420p.yuv --config bad.json)
expect_match("${out}" "\nreads: ${auto_reads}\n" "info text, invalid config")
expect_match("${err}" "config '[^']*bad.json' ignored: io.fileReads" "warning, invalid config")

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
