# rawreel-cli with a relative DVV_FFMPEG_DIR (M4 Task 7a): FFmpeg stays off and --version says why.
include("${CMAKE_CURRENT_LIST_DIR}/cli_common.cmake")

set(ENV{DVV_FFMPEG_DIR} "relative/ffmpeg")

run_cli(0 out err --version)
expect_match("${out}" "DVV_FFMPEG_DIR must be absolute" "--version with a relative DVV_FFMPEG_DIR")
