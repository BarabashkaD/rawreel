#include <doctest/doctest.h>

#include "core/stream_name.h"

using rawreel::PathStyle;
using rawreel::streamFullPath;
using rawreel::streamName;
using rawreel::TitleStyle;
using rawreel::windowTitle;

TEST_CASE("stream name: a file URI gives the base name without the query") {
    CHECK(streamName("file:clip.yuv?fmt=v210&size=1920x1080") == "clip.yuv");
    CHECK(streamName("file:/data/in/clip.yuv") == "clip.yuv");
    CHECK(streamName("file:X:\\data\\clip.yuv") == "clip.yuv");
}
TEST_CASE("stream name keeps UTF-8") {
    CHECK(streamName("file:/data/\xD0\xBA\xD0\xB0\xD0\xB4\xD1\x80.yuv") ==
          "\xD0\xBA\xD0\xB0\xD0\xB4\xD1\x80.yuv");
}
TEST_CASE("stream name: another URI gives the scheme and the path") {
    CHECK(streamName("lavfi:testsrc2=size=1280x720") == "lavfi:testsrc2=size=1280x720");
    CHECK(streamName("synthetic:gradient?fmt=yuv420p&size=64x48") == "synthetic:gradient");
}
TEST_CASE("stream name: empty and unparsable") {
    CHECK(streamName("") == "");
    CHECK(streamName("not a uri") == "not a uri");  // the text as it is (as fileName did)
}
TEST_CASE("full path: POSIX") {
    CHECK(streamFullPath("file:clip.yuv?fmt=v210", "/data/u/v", PathStyle::Posix) ==
          "/data/u/v/clip.yuv");
    CHECK(streamFullPath("file:../in/./clip.yuv", "/data/u/v", PathStyle::Posix) ==
          "/data/u/in/clip.yuv");
    CHECK(streamFullPath("file:/abs/clip.yuv", "/data/u", PathStyle::Posix) == "/abs/clip.yuv");
}
TEST_CASE("full path: Windows") {
    CHECK(streamFullPath("file:clip.yuv", "X:\\work\\v", PathStyle::Windows) ==
          "X:\\work\\v\\clip.yuv");
    CHECK(streamFullPath("file:X:/in/clip.yuv", "X:\\u", PathStyle::Windows) == "X:\\in\\clip.yuv");
    CHECK(streamFullPath("file:\\\\server\\share\\clip.yuv", "X:\\u", PathStyle::Windows) ==
          "\\\\server\\share\\clip.yuv");
}
TEST_CASE("full path: another URI is the resolved URI with its query") {
    CHECK(streamFullPath("lavfi:testsrc2=size=1280x720", "/x", PathStyle::Posix) ==
          "lavfi:testsrc2=size=1280x720");
    CHECK(streamFullPath("synthetic:gradient?fmt=yuv420p", "/x", PathStyle::Posix) ==
          "synthetic:gradient?fmt=yuv420p");
    CHECK(streamFullPath("", "/x", PathStyle::Posix) == "");
}
TEST_CASE("window title per OS") {
    CHECK(windowTitle("clip.yuv", TitleStyle::MacOs) == "clip.yuv");
    CHECK(windowTitle("clip.yuv", TitleStyle::Windows) == "clip.yuv - rawreel");
    CHECK(windowTitle("clip.yuv", TitleStyle::Linux) == "clip.yuv \xE2\x80\x94 rawreel");
    for (auto style : {TitleStyle::MacOs, TitleStyle::Windows, TitleStyle::Linux}) {
        CHECK(windowTitle("", style) == "rawreel");
    }
}
