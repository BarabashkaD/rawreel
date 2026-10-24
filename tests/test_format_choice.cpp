#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/format_choice.h"
#include "dvv/base/uri.h"
#include "dvv/media/pixel_format.h"

using dvv::media::ColourMatrix;
using dvv::media::ColourParams;
using dvv::media::ColourRange;
using dvv::media::Primaries;
using dvv::media::TransferFn;
using rawreel::FormatChoice;

namespace {

using Params = std::vector<std::pair<std::string, std::string>>;

dvv::media::FormatId formatId(std::string_view name) {
    const auto id = dvv::media::findFormat(name);
    REQUIRE(id);
    return id.value_or(dvv::media::FormatId{});
}

// The parameters of applyChoice(uri, choice); the path must be `path`.
Params applied(std::string_view uri, const FormatChoice& choice, std::string_view path) {
    const auto text = rawreel::applyChoice(uri, choice);
    REQUIRE_MESSAGE(text.has_value(), (text ? std::string() : text.error().describe()));
    const auto parsed = dvv::parseUri(*text);
    REQUIRE_MESSAGE(parsed, *text);
    CHECK(parsed->scheme == "file");
    CHECK(parsed->path == path);
    return parsed->params;
}

void checkInvalid(const FormatChoice& choice, std::string_view expected = {}) {
    const auto result = rawreel::validateChoice(choice);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == dvv::Errc::Format);
    CHECK_MESSAGE(result.error().message.find(expected) != std::string::npos,
                  result.error().message);
}

const ColourParams BT709_LIMITED{ColourMatrix::Bt709, ColourRange::Limited};

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("format_choice: choiceFromCandidate") {
        dvv::media::Candidate candidate{.component = "raw",
                                        .confidence = 50,
                                        .guess = {formatId("yuv420p"), 33, 17, std::nullopt},
                                        .why = {}};
        CHECK(rawreel::choiceFromCandidate(candidate) ==
              FormatChoice{"yuv420p", 33, 17, {25, 1}, {}});

        CHECK(rawreel::choiceFromCandidate(dvv::media::Candidate{}) ==
              FormatChoice{"yuv420p", 1920, 1080, {25, 1}, {}});

        candidate.guess = {formatId("v210"), 0, 17, dvv::media::Rational{50, 1}};
        CHECK(rawreel::choiceFromCandidate(candidate) ==
              FormatChoice{"v210", 1920, 1080, {50, 1}, {}});
    }

    TEST_CASE("format_choice: choiceFromInfo") {
        dvv::media::SourceInfo info;
        info.desc = {formatId("v210"), dvv::media::internColour(BT709_LIMITED), 34, 17};
        info.fps = {50, 1};
        info.resolvedUri = "file:clip.yuv?fmt=v210&size=34x17&fps=50&range=limited&matrix=bt709";
        CHECK(rawreel::choiceFromInfo(info) ==
              FormatChoice{"v210", 34, 17, {50, 1}, BT709_LIMITED});

        info.resolvedUri = "file:clip.yuv?fmt=@my.json&size=34x17&fps=50";
        CHECK(rawreel::choiceFromInfo(info).format == "@my.json");
    }

    TEST_CASE("format_choice: applyChoice") {
        const FormatChoice v210{"v210", 34, 17, {50, 1}, BT709_LIMITED};
        CHECK(applied("file:clip.yuv?impl=raw&w=3&h=4&range=full", v210, "clip.yuv") ==
              Params{{"impl", "raw"},
                     {"fmt", "v210"},
                     {"size", "34x17"},
                     {"fps", "50"},
                     {"matrix", "bt709"},
                     {"range", "limited"}});

        FormatChoice custom = v210;
        custom.colour = {.matrix = ColourMatrix::Custom, .kr = 0.212F, .kb = 0.087F};
        CHECK(applied("file:clip.yuv", custom, "clip.yuv") == Params{{"fmt", "v210"},
                                                                     {"size", "34x17"},
                                                                     {"fps", "50"},
                                                                     {"matrix", "custom"},
                                                                     {"kr", "0.212"},
                                                                     {"kb", "0.087"}});

        FormatChoice ntsc = v210;
        ntsc.fps = {30000, 1001};
        ntsc.colour = {
            .range = ColourRange::Full, .transfer = TransferFn::Pq, .primaries = Primaries::Bt2020};
        CHECK(applied("file:clip.yuv?kr=1&kb=2&transfer=srgb", ntsc, "clip.yuv") ==
              Params{{"fmt", "v210"},
                     {"size", "34x17"},
                     {"fps", "30000/1001"},
                     {"range", "full"},
                     {"transfer", "pq"},
                     {"primaries", "bt2020"}});

        FormatChoice plain = v210;
        plain.colour = {};
        CHECK(applied("file:clip.yuv?matrix=bt601&transfer=pq&primaries=bt709&x=1", plain,
                      "clip.yuv") ==
              Params{{"x", "1"}, {"fmt", "v210"}, {"size", "34x17"}, {"fps", "50"}});

        CHECK(applied("clip.yuv", plain, "clip.yuv") ==
              Params{{"fmt", "v210"}, {"size", "34x17"}, {"fps", "50"}});

        FormatChoice json = plain;
        json.format = "@my format.json";
        CHECK(applied("file:clip.yuv", json, "clip.yuv") ==
              Params{{"fmt", "@my format.json"}, {"size", "34x17"}, {"fps", "50"}});

        const auto bad = rawreel::applyChoice("bad:%zz", plain);
        REQUIRE_FALSE(bad);
        CHECK(bad.error().code == dvv::Errc::Format);

        const auto invalid = rawreel::applyChoice("file:clip.yuv", FormatChoice{"nope"});
        REQUIRE_FALSE(invalid);
        CHECK(invalid.error().code == dvv::Errc::Format);
    }

    TEST_CASE("format_choice: validateChoice") {
        checkInvalid(FormatChoice{"v210", 33, 17}, "multiple of 2");
        checkInvalid(FormatChoice{"nope"});
        checkInvalid(FormatChoice{"yuv420p", 33, 17, {0, 1}});
        checkInvalid(FormatChoice{"yuv420p", 33, 17, {25, 0}});
        checkInvalid(FormatChoice{"@"});
        checkInvalid(FormatChoice{"@x.json", 0, 5});
        checkInvalid(FormatChoice{"yuv420p", 33, 0});
        FormatChoice custom{"yuv420p", 33, 17};
        custom.colour = {.matrix = ColourMatrix::Custom, .kr = 0.6F, .kb = 0.5F};
        checkInvalid(custom);
        CHECK(rawreel::validateChoice(FormatChoice{"yuv420p", 33, 17, {25, 1}}));
        CHECK(rawreel::validateChoice(FormatChoice{"@x.json", 33, 17}));
    }

    TEST_CASE("format choice: fps text") {
        using dvv::media::Rational;
        CHECK(rawreel::parseFpsText("25") == Rational{25, 1});
        CHECK(rawreel::parseFpsText("30000/1001") == Rational{30000, 1001});
        CHECK(rawreel::parseFpsText("29.97") == Rational{2997, 100});
        CHECK(rawreel::parseFpsText("50.000") == Rational{50, 1});
        for (const std::string_view bad : {"", "0", "-25", "1/0", "25fps", "2,5"}) {
            CHECK_MESSAGE(!rawreel::parseFpsText(bad), bad);
        }
    }

    TEST_CASE("format choice: custom matrix") {
        FormatChoice custom{"yuv420p", 8, 8, {25, 1}, {}};
        custom.colour = {ColourMatrix::Custom,
                         ColourRange::Unspecified,
                         TransferFn::Unspecified,
                         Primaries::Unspecified,
                         0.212F,
                         0.087F};
        const auto text = rawreel::applyChoice("synthetic:gradient?fmt=yuv420p&size=8x8", custom);
        REQUIRE_MESSAGE(text.has_value(), (text ? std::string() : text.error().describe()));
        const auto parsed = dvv::parseUri(*text);
        REQUIRE_MESSAGE(parsed, *text);
        const Params& params = parsed->params;
        const auto has = [&params](std::string_view key, std::string_view value) {
            return std::ranges::find(params, std::pair<std::string, std::string>(key, value)) !=
                   params.end();
        };
        CHECK_MESSAGE(has("matrix", "custom"), *text);
        CHECK_MESSAGE(has("kr", "0.212"), *text);
        CHECK_MESSAGE(has("kb", "0.087"), *text);

        custom.colour.kr = 0.7F;
        custom.colour.kb = 0.4F;
        const auto invalid = rawreel::validateChoice(custom);
        REQUIRE_FALSE(invalid);
        CHECK(invalid.error().code == dvv::Errc::Format);
    }
}
