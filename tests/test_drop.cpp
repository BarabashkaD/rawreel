#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "core/drop.h"
#include "core/native_os.h"
#include "dvv/base/uri.h"
#include "dvv/base/utf8.h"

using rawreel::DropPlan;
using rawreel::fileUri;
using rawreel::routeDrop;

namespace {

using Paths = std::vector<std::filesystem::path>;

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("drop: fileUri round trips") {
#if defined(_WIN32)
        const std::filesystem::path absolute(std::string("C:") + "\\clips\\x.yuv");
#else
        const std::filesystem::path absolute("/data/x.yuv");
#endif
        const Paths paths{std::filesystem::path(u8"clip ä.yuv"),
                          std::filesystem::path("a?b#c%d&e.yuv"), absolute};
        for (const auto& path : paths) {
            CAPTURE(dvv::pathDisplayText(path));
            const std::string text = fileUri(path);
            const auto uri = dvv::parseUri(text);
            REQUIRE(uri);
            CHECK(uri->scheme == "file");
            CHECK(uri->path == dvv::pathText(path));
            REQUIRE(text.starts_with("file:"));
            CHECK(text.find_first_of("?#", 5) == std::string::npos);
        }
    }

    TEST_CASE("drop: a non-UTF-8 name round-trips through the URI") {
        if constexpr (rawreel::NATIVE_OS != rawreel::Os::Windows) {
            const auto path = dvv::pathFromText(
                "/tmp/a\xFF"
                "b.yuv");
            const auto uri = dvv::parseUri(fileUri(path));
            CHECK(uri.has_value());
            if (uri) {
                CHECK(dvv::pathFromText(uri->path) == path);
            }
        }
    }

    TEST_CASE("drop: one file opens") {
        const std::filesystem::path a("clip_16x16_gray.yuv");
        CHECK(routeDrop(Paths{a}) == DropPlan{fileUri(a), 0});
    }

    TEST_CASE("drop: the first of several opens, the rest are ignored") {
        const std::filesystem::path a("a.yuv");
        const std::filesystem::path b("b.yuv");
        const std::filesystem::path c("c.yuv");
        CHECK(routeDrop(Paths{a, b, c}) == DropPlan{fileUri(a), 2});
        const DropPlan none = routeDrop(Paths{});
        CHECK(none == DropPlan{});
        CHECK_FALSE(none.uri);
        CHECK(none.ignored == 0);
    }
}
