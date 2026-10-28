# rawreel-cli on FFmpeg inputs (M4 Task 7a; design v2 §8.4): info, dump and bench on an FFV1 clip,
# the lossless Sequential fixture and lavfi graphs, the colour keys and their Reopen round trip, and
# the refusal of a lossy codec (lossless codecs only).
include("${CMAKE_CURRENT_LIST_DIR}/cli_common.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cli_ffmpeg_common.cmake")

require_ffmpeg()
foreach(fixture IN ITEMS clip.mkv seq.mkv clip_7.raw seq_29.raw seq_3.raw lavfi_2.raw)
    use_fixture(${fixture})
endforeach()
if(EXISTS "${FIXTURES}/lossy.mp4")
    use_fixture(lossy.mp4)
endif()

# 1. The FFV1 clip.
run_cli(0 out err info clip.mkv --json)
expect_json("${out}" 64 width)
expect_json("${out}" 48 height)
expect_json("${out}" yuv420p10le format name)
expect_json("${out}" 12 frames)
expect_json("${out}" bt709 colour matrix)
string(JSON uri GET "${out}" uri)
expect_match("${uri}" "impl=ffmpeg" "info clip.mkv: uri")

# 2. A colour key and its Reopen round trip through the resolved URI.
run_cli(0 out err info "clip.mkv?colour=bt601-pal" --json)
expect_json("${out}" bt601 colour matrix)
expect_json("${out}" bt601-pal standard)
string(JSON uri GET "${out}" uri)
expect_match("${uri}" "colour=bt601-pal" "info clip.mkv?colour=bt601-pal: uri")
run_cli(0 out err info "${uri}" --json)
expect_json("${out}" bt601 colour matrix)
expect_json("${out}" bt601-pal standard)

# 3. fmt is the container's.
run_cli(2 out err info "clip.mkv?fmt=gray")
expect_match("${err}" "fmt is set by the container" "info clip.mkv?fmt=gray")

# 4. One frame of the clip equals FFmpeg's.
run_cli(0 out err dump clip.mkv --frame 7 --out f7.raw --via cpu)
expect_same_file(f7.raw clip_7.raw)

# 5. The Sequential fixture: frames 29 and 3 in two processes (no backward claim).
run_cli(0 out err dump seq.mkv --frame 29 --out s29.raw --via cpu)
expect_same_file(s29.raw seq_29.raw)
run_cli(0 out err dump seq.mkv --frame 3 --out s3.raw --via cpu)
expect_same_file(s3.raw seq_3.raw)
run_cli(0 out err info seq.mkv --json)
expect_json("${out}" sequential access)
expect_json("${out}" 30 frames)

# 6. A lavfi graph: no frame count, sequential; dump and bench.
set(lavfi "lavfi:testsrc2=s=64x48:r=25,format=yuv420p")
run_cli(0 out err info "${lavfi}" --json)
string(JSON frames_type TYPE "${out}" frames)
if(NOT frames_type STREQUAL "NULL")
    message(FATAL_ERROR "info ${lavfi}: frames is ${frames_type}, expected NULL:\n${out}")
endif()
expect_json("${out}" sequential access)
run_cli(0 out err dump "${lavfi}" --frame 2 --out l2.raw --via cpu)
expect_same_file(l2.raw lavfi_2.raw)
# bench always uses the GPU: it runs in run-cli-ffmpeg-gpu.cmake (labels gpu;ffmpeg, the 77 rule).

# 7. setparams sets the colour of a lavfi graph.
run_cli(0 out err info "${lavfi},setparams=colorspace=bt709:range=pc" --json)
expect_json("${out}" bt709 colour matrix)
expect_json("${out}" full colour range)

# 8. A lossy codec is refused while FFmpeg is loaded (Task 0: "mpeg4 encoder: yes").
if(EXISTS "${WORK_DIR}/lossy.mp4")
    run_cli(3 out err info lossy.mp4)
    expect_match("${err}" "codec mpeg4 is not supported \\(lossless codecs only\\)"
                 "info lossy.mp4")
    if(err MATCHES "FFmpeg: not available")
        message(FATAL_ERROR "info lossy.mp4: FFmpeg is loaded, but:\n${err}")
    endif()
endif()
