# rawreel-cli with non-ASCII paths (M2 Task 13, D19): arguments reach the CLI as UTF-8 (wmain on
# Windows) and files open through std::u8string. This script is UTF-8.
include("${CMAKE_CURRENT_LIST_DIR}/cli_common.cmake")

set(clip "${WORK_DIR}/clip_ä日本_4x2_gray.yuv")
file(WRITE "${clip}" "ABCDEFGH")

run_cli(0 out err info "${clip}")
expect_match("${out}" "format: gray" "info of a non-ASCII path")

run_cli(0 out err dump "${clip}" --view original --via cpu --out "${WORK_DIR}/out_ä.raw")
expect_file(out_ä.raw "ABCDEFGH")
