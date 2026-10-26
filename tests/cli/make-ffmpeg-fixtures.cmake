# The FFmpeg fixtures of rawreel-cli's end-to-end tests (M4 Task 7a; design v2 §8.4), made with the
# FFmpeg program of the third-party prefix:
#
#   cmake -DFFMPEG=<ffmpeg program> -DOUT=<dir> [-DLIBDIR=<FFmpeg library dir>] -P <this file>
#
# Regenerated on every run (64x48, a few frames). Without the program: prints "ffmpeg program
# missing" and exits 0 (the tests then skip, or fail with DVV_REQUIRE_FFMPEG=1). Outside Windows
# every command runs with LD_LIBRARY_PATH=<LIBDIR>.
#
# Output: clip.mkv (FFV1, yuv420p10le, BT.709, 12 frames), seq.mkv (the lossless Sequential fixture
# Task 0 chose: FFV1 with pairs of equal pts, 30 frames), lossy.mp4 (MPEG-4 Part 2, only for the
# lossy refusal; Task 0 recorded "mpeg4 encoder: yes"), and the references clip_7.raw, seq_29.raw,
# seq_3.raw and lavfi_2.raw (one frame each, as `ffmpeg -f rawvideo` writes it).
cmake_minimum_required(VERSION 4.2)

if(NOT OUT)
    message(FATAL_ERROR "make-ffmpeg-fixtures: OUT is required")
endif()
if(NOT FFMPEG OR NOT EXISTS "${FFMPEG}")
    message("ffmpeg program missing")
    return()
endif()

set(command_prefix "")
if(NOT CMAKE_HOST_WIN32 AND LIBDIR)
    set(command_prefix "${CMAKE_COMMAND}" -E env "LD_LIBRARY_PATH=${LIBDIR}")
endif()

file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")

# ffmpeg(<output> <arguments>...): runs the program in OUT and fails unless it wrote <output>.
function(ffmpeg output)
    execute_process(COMMAND ${command_prefix} "${FFMPEG}" -hide_banner -nostdin -y ${ARGN}
                    WORKING_DIRECTORY "${OUT}" RESULT_VARIABLE result OUTPUT_VARIABLE log
                    ERROR_VARIABLE log)
    if(NOT result EQUAL 0 OR NOT EXISTS "${OUT}/${output}")
        message(FATAL_ERROR "make-ffmpeg-fixtures: ${output} failed (${result}):\n${log}")
    endif()
endfunction()

set(source -f lavfi -i "testsrc2=s=64x48:r=25")
ffmpeg(clip.mkv ${source} -frames:v 12 -pix_fmt yuv420p10le -colorspace bt709 -c:v ffv1
       -level 3 clip.mkv)
ffmpeg(seq.mkv ${source} -frames:v 30 -vf "setpts=floor(N/2)/(25*TB)" -fps_mode passthrough
       -pix_fmt yuv420p -c:v ffv1 -level 3 seq.mkv)
ffmpeg(lossy.mp4 ${source} -frames:v 5 -pix_fmt yuv420p -c:v mpeg4 lossy.mp4)

# The references: `select` counts decoded frames, as the Sequential source does.
foreach(reference IN ITEMS "clip.mkv|7|clip_7.raw" "seq.mkv|29|seq_29.raw" "seq.mkv|3|seq_3.raw")
    string(REPLACE "|" ";" reference "${reference}")
    list(GET reference 0 input)
    list(GET reference 1 frame)
    list(GET reference 2 output)
    ffmpeg(${output} -i ${input} -vf "select=eq(n\\,${frame})" -frames:v 1 -fps_mode passthrough
           -f rawvideo ${output})
endforeach()
ffmpeg(lavfi_2.raw -f lavfi -i "testsrc2=s=64x48:r=25,format=yuv420p" -vf "select=eq(n\\,2)"
       -frames:v 1 -fps_mode passthrough -f rawvideo lavfi_2.raw)
message(STATUS "make-ffmpeg-fixtures: wrote the fixtures to ${OUT}")
