// Open helpers of the GUI (M4 GUI D6, D7, D4b): the folder of an opened file, the Open URI
// value without line breaks, the no-scheme hint and the Format... offer on an open error.

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "core/drop.h"
#include "core/open_text.h"
#include "dvv/base/error.h"

namespace {

dvv::Error error(dvv::Errc code) {
    dvv::Error result;
    result.code = code;
    result.message = "'x' could not be opened: missing";
    return result;
}

constexpr const char* HINT =
    "; no scheme: opened as a file path; a URI needs e.g. synthetic: or lavfi:";

}  // namespace

TEST_CASE("open text: uriFolder") {
    const std::filesystem::path folder = std::filesystem::path("clips") / "day 1";
    const std::filesystem::path file = folder / "clip_1920x1080.yuv";
    // Absolute: a relative path is read against the working directory.
    const std::filesystem::path cwd = std::filesystem::current_path();
    CHECK(rawreel::uriFolder(rawreel::fileUri(file)) == cwd / folder);
    CHECK(rawreel::uriFolder(rawreel::fileUri(file) + "?fmt=yuv420p") == cwd / folder);
    CHECK(rawreel::uriFolder("clips/clip.yuv") == cwd / "clips");
    CHECK(rawreel::uriFolder("clip.yuv") == cwd);
    CHECK(rawreel::uriFolder("clip.yuv").is_absolute());
    CHECK(rawreel::uriFolder("synthetic:gradient?fmt=yuv420p").empty());
    CHECK(rawreel::uriFolder("lavfi:testsrc2").empty());
    CHECK(rawreel::uriFolder("").empty());
#if defined(_WIN32)
    CHECK(rawreel::uriFolder("file:///X:/clips/x.yuv") == std::filesystem::path("X:/clips"));
    CHECK(rawreel::uriFolder("X:\\clips\\x.yuv") == std::filesystem::path("X:\\clips"));
#endif
}

TEST_CASE("open text: withoutLineBreaks") {
    CHECK(rawreel::withoutLineBreaks("synthetic:gradient?fmt=yuv420p\r\n") ==
          "synthetic:gradient?fmt=yuv420p");
    CHECK(rawreel::withoutLineBreaks("lavfi:testsrc2\n=size=64x64\r") ==
          "lavfi:testsrc2=size=64x64");
    CHECK(rawreel::withoutLineBreaks("a b") == "a b");
    CHECK(rawreel::withoutLineBreaks("").empty());
}

TEST_CASE("open text: cleanUriText") {
    // Line breaks go, and so does the white space around the URI.
    CHECK(rawreel::cleanUriText(" synthetic:gradient?fmt=yuv420p \t\r\n") ==
          "synthetic:gradient?fmt=yuv420p");
    CHECK(rawreel::cleanUriText("\t lavfi:testsrc2\n=size=64x64\r") == "lavfi:testsrc2=size=64x64");
    CHECK(rawreel::cleanUriText("a b") == "a b");
    CHECK(rawreel::cleanUriText(" \t\r\n").empty());
    CHECK(rawreel::cleanUriText("").empty());
}

TEST_CASE("open text: hasScheme") {
    CHECK(rawreel::hasScheme("synthetic:gradient"));
    CHECK(rawreel::hasScheme("lavfi:testsrc2"));
    CHECK(rawreel::hasScheme("file:clip.yuv"));
    CHECK_FALSE(rawreel::hasScheme("fmt=yuv420p10le&size=64x64"));
    CHECK_FALSE(rawreel::hasScheme("X:\\clips\\x.yuv"));
    CHECK_FALSE(rawreel::hasScheme("X:"));
    CHECK_FALSE(rawreel::hasScheme("x:y"));  // one letter: a drive-like name, no scheme
    CHECK_FALSE(rawreel::hasScheme("1ab:x"));
    CHECK_FALSE(rawreel::hasScheme("a b:x"));
    CHECK_FALSE(rawreel::hasScheme(""));
}

TEST_CASE("open text: withSchemeHint") {
    const std::string text = "'x' could not be opened: missing";
    SUBCASE("a scheme-less text with '=' or '&' that fails as a missing file gets the hint") {
        CHECK(rawreel::withSchemeHint(text, "fmt=yuv420p10le&size=64x64", error(dvv::Errc::Io)) ==
              text + HINT);
        CHECK(rawreel::withSchemeHint(text, "gradient?fmt=yuv420p", error(dvv::Errc::Io)) ==
              text + HINT);
        CHECK(rawreel::withSchemeHint(text, "a&b", error(dvv::Errc::Io)) == text + HINT);
    }
    SUBCASE("no hint otherwise") {
        CHECK(rawreel::withSchemeHint(text, "clip.yuv", error(dvv::Errc::Io)) == text);
        CHECK(rawreel::withSchemeHint(text, "synthetic:gradient?fmt=x", error(dvv::Errc::Io)) ==
              text);
        CHECK(rawreel::withSchemeHint(text, "file:a=b.yuv", error(dvv::Errc::Io)) == text);
        // A path separator before the first '=', '&' or '?' marks a file path.
        CHECK(rawreel::withSchemeHint(text, "X:\\R&D\\clip.yuv", error(dvv::Errc::Io)) == text);
        CHECK(rawreel::withSchemeHint(text, "clips/a=b.yuv", error(dvv::Errc::Io)) == text);
        CHECK(rawreel::withSchemeHint(text, "fmt=yuv420p", error(dvv::Errc::Format)) == text);
        CHECK(rawreel::withSchemeHint(text, "fmt=yuv420p", error(dvv::Errc::Unsupported)) == text);
    }
}

TEST_CASE("open text: offersFormat") {
    SUBCASE("a raw file the raw provider refused") {
        CHECK(rawreel::offersFormat("file:clip_64x64.yuv", error(dvv::Errc::Format)));
        CHECK(rawreel::offersFormat("clip_64x64.yuv", error(dvv::Errc::Format)));
        CHECK(rawreel::offersFormat("file:clip.yuv?fmt=yuv420p&size=64x64",
                                    error(dvv::Errc::Format)));
        CHECK(rawreel::offersFormat("file:clip.yuv?impl=raw", error(dvv::Errc::Format)));
    }
    SUBCASE("no offer") {
        // A missing or unreadable file, a refused codec (D4), a lost device.
        CHECK_FALSE(rawreel::offersFormat("file:clip.yuv", error(dvv::Errc::Io)));
        CHECK_FALSE(rawreel::offersFormat("file:clip.mkv", error(dvv::Errc::Unsupported)));
        CHECK_FALSE(rawreel::offersFormat("file:clip.yuv", error(dvv::Errc::DeviceLost)));
        // Not a raw file: another scheme or a container provider named by impl.
        CHECK_FALSE(rawreel::offersFormat("synthetic:gradient?fmt=bad", error(dvv::Errc::Format)));
        CHECK_FALSE(rawreel::offersFormat("lavfi:testsrc2", error(dvv::Errc::Format)));
        CHECK_FALSE(rawreel::offersFormat("file:clip.y4m?impl=y4m", error(dvv::Errc::Format)));
        CHECK_FALSE(rawreel::offersFormat("file:clip.mkv?impl=ffmpeg", error(dvv::Errc::Format)));
        CHECK_FALSE(rawreel::offersFormat("", error(dvv::Errc::Format)));
        // A recognised Y4M or container file (the raw dialog cannot fix it).
        CHECK_FALSE(rawreel::offersFormat("file:clip.y4m", error(dvv::Errc::Format)));
        CHECK_FALSE(rawreel::offersFormat("clips/Clip.MKV", error(dvv::Errc::Format)));
        CHECK_FALSE(rawreel::offersFormat("file:clip.mov?fmt=yuv420p", error(dvv::Errc::Format)));
    }
    SUBCASE("impl=raw is always the raw provider's error") {
        CHECK(rawreel::offersFormat("file:clip.y4m?impl=raw", error(dvv::Errc::Format)));
    }
}

TEST_CASE("open text: isContainerName and withRawImpl") {
    CHECK(rawreel::isContainerName("clip.y4m"));
    CHECK(rawreel::isContainerName("a/b/Clip.Mp4"));
    // The other common FFmpeg containers and elementary streams.
    for (const char* name :
         {"a.mpg", "a.mpeg", "a.vob", "a.3gp", "a.3g2", "a.ogv", "a.ogg", "a.ivf", "a.dv", "a.f4v",
          "a.wtv", "a.h264", "a.264", "a.hevc", "a.265"}) {
        CAPTURE(name);
        CHECK(rawreel::isContainerName(name));
    }
    CHECK_FALSE(rawreel::isContainerName("clip_64x64.yuv"));
    CHECK_FALSE(rawreel::isContainerName("dir.mkv/clip"));
    CHECK_FALSE(rawreel::isContainerName("clip"));
    CHECK(rawreel::withRawImpl("file:clip.y4m?impl=y4m&fmt=yuv420p") ==
          "file:clip.y4m?fmt=yuv420p&impl=raw");
    CHECK(rawreel::withRawImpl("file:clip.yuv") == "file:clip.yuv?impl=raw");
    CHECK(rawreel::withRawImpl("synthetic:gradient") == "synthetic:gradient");
}
