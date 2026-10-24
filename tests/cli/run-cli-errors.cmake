# rawreel-cli exit codes and error output (M2 Task 13; spec §10.3).
include("${CMAKE_CURRENT_LIST_DIR}/cli_common.cmake")

# 2: input error.
run_cli(2 out err info missing.yuv)
run_cli(2 json err info missing.yuv --json)
expect_json("${json}" Io error code)

# 3: ambiguous (only the raw provider could open it, with low confidence).
file(WRITE "${WORK_DIR}/clip_33x17.yuv" "x")
run_cli(3 out err info clip_33x17.yuv)
expect_match("${err}" "raw: confidence 30" "ambiguous candidates")

# 1: bad arguments, including a frame range outside the source.
run_cli(1 out err dump x)
run_cli(1 out err play)
run_cli(1 out err dump "synthetic:noise?fmt=gray&size=2x2&frames=2" --frame 2 --out o)

run_cli(0 out err --help)
expect_match("${out}" "rawreel-cli dump" "--help")
run_cli(0 out err --version)
expect_match("${out}" "dvv-media [0-9]" "--version")

# 2: the synthetic provider rejects an unknown format.
run_cli(2 out err info "synthetic:noise?fmt=nope&size=2x2")
