#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/format_choice.h"
#include "core/format_presets.h"
#include "core/session_types.h"
#include "dvv/base/uri.h"
#include "dvv/media/colour_preset.h"
#include "dvv/media/display.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/types.h"

using dvv::media::ColourMatrix;
using dvv::media::ColourModel;
using dvv::media::ColourParams;
using dvv::media::ColourPreset;
using dvv::media::ColourRange;
using dvv::media::Primaries;
using dvv::media::TransferFn;
using rawreel::ColourBase;
using rawreel::ColourChoice;
using rawreel::ColourOrigin;
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

// colour=<standard> without a range key.
ColourChoice standard(ColourPreset id) {
    return ColourChoice{.standard = id, .range = ColourRange::Unspecified, .autoDetect = false};
}

// colour=<standard>&range=<range>; nullopt: range=<range> only.
ColourChoice keyed(std::optional<ColourPreset> id, ColourRange range) {
    return ColourChoice{.standard = id, .range = range, .autoDetect = false};
}

const ColourParams& standardValues(ColourPreset id) {
    return dvv::media::colourPresetInfo(id).colour;
}

// The base of an untagged frame of the format and height (BT.709, origin Default).
ColourBase defaultBase(std::string_view format, std::uint32_t height) {
    return ColourBase{.standard = ColourPreset::Bt709,
                      .origin = ColourOrigin::Default,
                      .clipName = {},
                      .values = rawreel::defaultColour(format, height).params,
                      .rangeFromFile = false};
}

// A YUV stream tagged BT.709 limited with the sRGB curve (fileColourMask cases).
const ColourParams HD_TAGGED{.matrix = ColourMatrix::Bt709,
                             .range = ColourRange::Limited,
                             .transfer = TransferFn::Srgb,
                             .primaries = Primaries::Bt709,
                             .kr = 0,
                             .kb = 0};
const ColourParams BT709_LIMITED{.matrix = ColourMatrix::Bt709,
                                 .range = ColourRange::Limited,
                                 .transfer = TransferFn::Unspecified,
                                 .primaries = Primaries::Unspecified,
                                 .kr = 0,
                                 .kb = 0};
// An RGB stream tagged with range, curve and primaries (the GBR matrix).
const ColourParams RGB_TAGGED{.matrix = ColourMatrix::Rgb,
                              .range = ColourRange::Full,
                              .transfer = TransferFn::Srgb,
                              .primaries = Primaries::Bt709,
                              .kr = 0,
                              .kb = 0};

// An RGB or gray colour: no matrix.
ColourParams rgbColour(ColourRange range, TransferFn transfer, Primaries primaries) {
    return ColourParams{.matrix = ColourMatrix::Unspecified,
                        .range = range,
                        .transfer = transfer,
                        .primaries = primaries,
                        .kr = 0,
                        .kb = 0};
}

// Writes `text` to `path` (binary); the test removes its folder at the end.
void writeFile(const std::filesystem::path& path, std::string_view text) {
    std::ofstream(path, std::ios::binary | std::ios::trunc)
        .write(text.data(), static_cast<std::streamsize>(text.size()));
}

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
        info.desc = {formatId("v210"), dvv::media::internColour(BT709_LIMITED), 34, 1080};
        info.fps = {50, 1};
        // The colour keys of the resolved URI, as they are.
        info.resolvedUri = "file:clip.yuv?fmt=v210&size=34x1080&fps=50&range=full&colour=bt709";
        CHECK(
            rawreel::choiceFromInfo(info) ==
            FormatChoice{"v210", 34, 1080, {50, 1}, keyed(ColourPreset::Bt709, ColourRange::Full)});

        info.resolvedUri = "file:clip.yuv?fmt=v210&size=34x1080&fps=50&colour=bt2100-pq";
        CHECK(rawreel::choiceFromInfo(info).colour == standard(ColourPreset::Bt2100Pq));

        info.resolvedUri = "file:clip.yuv?fmt=@my.json&size=34x1080&fps=50";
        const FormatChoice json = rawreel::choiceFromInfo(info);
        CHECK(json.format == "@my.json");
        CHECK(json.colour == ColourChoice{});  // no colour key: the base entry
    }

    // D5R §4.4 "Reading a URI": colour= and range= as they are.
    TEST_CASE("format_choice: colourChoiceFromUri") {
        const auto read = [](std::string_view uri) { return rawreel::colourChoiceFromUri(uri); };
        CHECK(read("file:a.yuv?fmt=yuv420p&clip=@c.json") == ColourChoice{});
        CHECK(read("file:a.yuv?colour=bt709") == standard(ColourPreset::Bt709));
        CHECK(read("file:a.yuv?colour=bt601-ntsc&range=full") ==
              keyed(ColourPreset::Bt601Ntsc, ColourRange::Full));
        CHECK(read("file:a.yuv?range=limited") == keyed(std::nullopt, ColourRange::Limited));
        // The removed keys give no colour here (the open refuses them).
        CHECK(read("file:a.yuv?transfer=pq&matrix=bt601") == ColourChoice{});
        CHECK(read("file:a.yuv?colour=nope") == ColourChoice{});  // the open names the error
        CHECK(read("file:a.yuv?colour=jpeg") == ColourChoice{});  // jpeg is no standard any more
        CHECK(read("bad:%zz") == ColourChoice{});
    }

    TEST_CASE("format_choice: keyedColour") {
        const ColourBase base = defaultBase("yuv420p", 1080);
        REQUIRE(base.values.range == ColourRange::Limited);
        // The base's standard and range: no colour key.
        CHECK(rawreel::keyedColour(ColourPreset::Bt709, ColourRange::Limited, base) ==
              ColourChoice{});
        CHECK(rawreel::keyedColour(ColourPreset::Bt709, ColourRange::Full, base) ==
              keyed(ColourPreset::Bt709, ColourRange::Full));
        CHECK(rawreel::keyedColour(ColourPreset::Bt601Pal, ColourRange::Limited, base) ==
              standard(ColourPreset::Bt601Pal));
        CHECK(rawreel::keyedColour(ColourPreset::Bt2100Hlg, ColourRange::Full, base) ==
              keyed(ColourPreset::Bt2100Hlg, ColourRange::Full));
        // A base without a standard: "From file (no standard)" writes nothing.
        ColourBase none = base;
        none.standard = std::nullopt;
        none.origin = ColourOrigin::File;
        CHECK(rawreel::keyedColour(std::nullopt, ColourRange::Limited, none) == ColourChoice{});
        CHECK(rawreel::keyedColour(ColourPreset::Bt709, ColourRange::Limited, none) ==
              standard(ColourPreset::Bt709));
    }

    TEST_CASE("format_choice: colourValues") {
        const ColourBase yuv = defaultBase("yuv420p", 1080);
        CHECK(rawreel::colourValues(ColourChoice{}, yuv, ColourModel::Yuv) == yuv.values);
        // Every standard on YUV: its four values (range limited from the base).
        for (const auto& info : dvv::media::colourPresets()) {
            CHECK_MESSAGE(
                rawreel::colourValues(standard(info.id), yuv, ColourModel::Yuv) == info.colour,
                info.uriName);
        }
        ColourParams full = standardValues(ColourPreset::Bt2100Pq);
        full.range = ColourRange::Full;
        CHECK(rawreel::colourValues(keyed(ColourPreset::Bt2100Pq, ColourRange::Full), yuv,
                                    ColourModel::Yuv) == full);
        // RGB and gray: the base's matrix and range stay; the standard gives curve and primaries.
        const ColourBase rgb = defaultBase("rgb24", 8);
        ColourParams expected = rgb.values;
        expected.transfer = standardValues(ColourPreset::Bt2100Hlg).transfer;
        expected.primaries = standardValues(ColourPreset::Bt2100Hlg).primaries;
        CHECK(rawreel::colourValues(standard(ColourPreset::Bt2100Hlg), rgb, ColourModel::Rgb) ==
              expected);
        expected.range = ColourRange::Limited;
        CHECK(rawreel::colourValues(keyed(ColourPreset::Bt2100Hlg, ColourRange::Limited), rgb,
                                    ColourModel::Gray) == expected);
        // The GBR matrix of a YUV-model stream stays (design §3.3 rule 5).
        ColourBase gbr = yuv;
        gbr.values.matrix = ColourMatrix::Rgb;
        const ColourParams gbr_values =
            rawreel::colourValues(standard(ColourPreset::Bt2020), gbr, ColourModel::Yuv);
        CHECK(gbr_values.matrix == ColourMatrix::Rgb);
        CHECK(gbr_values.primaries == Primaries::Bt2020);
    }

    TEST_CASE("format_choice: autoBase and mismatchHint") {
        const std::vector<std::pair<std::uint32_t, ColourPreset>> guesses{
            {480, ColourPreset::Bt601Ntsc},
            {576, ColourPreset::Bt601Pal},
            {1080, ColourPreset::Bt709}};
        for (const auto& [height, guess] : guesses) {
            const ColourBase base = rawreel::autoBase(defaultBase("yuv420p", height), height);
            CHECK(base.standard == guess);
            CHECK(base.origin == ColourOrigin::Auto);
            CHECK(base.values.matrix == standardValues(guess).matrix);
            CHECK(base.values.range == ColourRange::Limited);
        }
        // Not the default: unchanged.
        ColourBase named = defaultBase("yuv420p", 576);
        named.origin = ColourOrigin::FileName;
        CHECK(rawreel::autoBase(named, 576) == named);

        CHECK(rawreel::mismatchHint(ColourPreset::Bt709, false, 576) ==
              "SD size: BT.601 is common for SD");
        CHECK(rawreel::mismatchHint(ColourPreset::Bt709, true, 576).empty());
        CHECK(rawreel::mismatchHint(std::nullopt, false, 576).empty());
        CHECK(rawreel::mismatchHint(ColourPreset::Bt601Pal, false, 1080) ==
              "HD size: BT.709 is common");
        CHECK(rawreel::mismatchHint(ColourPreset::Bt709, false, 1080).empty());
    }

    TEST_CASE("format_choice: applyChoice") {
        FormatChoice v210{"v210", 34, 1080, {50, 1}, {}};
        // No colour key; clip, impl and the other keys stay in order; the old keys go.
        CHECK(applied("file:clip.yuv?impl=raw&w=3&h=4&range=full&colour=bt709&clip=@c.json&"
                      "matrix=bt601&kr=1&kb=2&transfer=pq&primaries=bt2020&x=1",
                      v210, "clip.yuv") == Params{{"impl", "raw"},
                                                  {"clip", "@c.json"},
                                                  {"x", "1"},
                                                  {"fmt", "v210"},
                                                  {"size", "34x1080"},
                                                  {"fps", "50"}});

        v210.colour = standard(ColourPreset::Bt2100Pq);
        CHECK(applied("file:clip.yuv?colour=bt709&range=full", v210, "clip.yuv") ==
              Params{{"fmt", "v210"}, {"size", "34x1080"}, {"fps", "50"}, {"colour", "bt2100-pq"}});

        v210.colour = keyed(ColourPreset::Bt601Pal, ColourRange::Full);
        v210.fps = {30000, 1001};
        CHECK(applied("file:clip.yuv", v210, "clip.yuv") == Params{{"fmt", "v210"},
                                                                   {"size", "34x1080"},
                                                                   {"fps", "30000/1001"},
                                                                   {"colour", "bt601-pal"},
                                                                   {"range", "full"}});
        // A range key alone (the base without a standard).
        v210.colour = keyed(std::nullopt, ColourRange::Limited);
        CHECK(applied("file:clip.yuv", v210, "clip.yuv") == Params{{"fmt", "v210"},
                                                                   {"size", "34x1080"},
                                                                   {"fps", "30000/1001"},
                                                                   {"range", "limited"}});

        FormatChoice gray{"gray", 8, 8, {25, 1}, standard(ColourPreset::Bt2020)};
        CHECK(applied("file:a.gray?range=full", gray, "a.gray") ==
              Params{{"fmt", "gray"}, {"size", "8x8"}, {"fps", "25"}, {"colour", "bt2020"}});

        FormatChoice json{"@my format.json", 34, 17, {50, 1}, {}};
        CHECK(applied("file:clip.yuv", json, "clip.yuv") ==
              Params{{"fmt", "@my format.json"}, {"size", "34x17"}, {"fps", "50"}});
        CHECK(applied("clip.yuv", json, "clip.yuv") ==
              Params{{"fmt", "@my format.json"}, {"size", "34x17"}, {"fps", "50"}});

        const auto bad = rawreel::applyChoice("bad:%zz", json);
        REQUIRE_FALSE(bad);
        CHECK(bad.error().code == dvv::Errc::Format);

        const auto invalid = rawreel::applyChoice("file:clip.yuv", FormatChoice{"nope"});
        REQUIRE_FALSE(invalid);
        CHECK(invalid.error().code == dvv::Errc::Format);
    }

    // Every standard with and without a range key reads back as written (D5R §4.4).
    TEST_CASE("format_choice: round trip") {
        std::vector<std::pair<std::string_view, ColourChoice>> cases{
            {"yuv420p", ColourChoice{}},
            {"yuv420p", keyed(std::nullopt, ColourRange::Full)},
            {"gray", ColourChoice{}},
        };
        for (const auto& info : dvv::media::colourPresets()) {
            for (const std::string_view format : {"yuv420p", "rgb24", "gray"}) {
                cases.emplace_back(format, standard(info.id));
                cases.emplace_back(format, keyed(info.id, ColourRange::Full));
                cases.emplace_back(format, keyed(info.id, ColourRange::Limited));
            }
        }
        for (const auto& [format, colour] : cases) {
            const FormatChoice choice{std::string(format), 64, 32, {30000, 1001}, colour};
            const auto uri = rawreel::applyChoice("file:a.bin?clip=@c.json", choice);
            REQUIRE_MESSAGE(uri.has_value(), format);
            const std::string text = uri.value_or("");
            CHECK_MESSAGE(rawreel::colourChoiceFromUri(text) == colour, text);
            const FormatChoice back = rawreel::choiceFromUri(text, FormatChoice{});
            CHECK_MESSAGE(back == choice, text);
        }
    }

    TEST_CASE("format_choice: validateChoice") {
        checkInvalid(FormatChoice{"v210", 33, 17}, "multiple of 2");
        checkInvalid(FormatChoice{"nope"});
        checkInvalid(FormatChoice{"yuv420p", 33, 17, {0, 1}});
        checkInvalid(FormatChoice{"yuv420p", 33, 17, {25, 0}});
        checkInvalid(FormatChoice{"@"});
        checkInvalid(FormatChoice{"@x.json", 0, 5});
        checkInvalid(FormatChoice{"yuv420p", 33, 0});
        CHECK(rawreel::validateChoice(FormatChoice{"yuv420p", 33, 17, {25, 1}}));
        CHECK(rawreel::validateChoice(FormatChoice{
            "yuv420p", 33, 17, {25, 1}, keyed(ColourPreset::Bt601Ntsc, ColourRange::Full)}));
        CHECK(rawreel::validateChoice(FormatChoice{"@x.json", 33, 17}));
    }

    // The Format... prefill of a failed open uses the file's own name and query.
    TEST_CASE("format choice: choiceFromUri") {
        const FormatChoice fallback{.format = "nv12",
                                    .width = 64,
                                    .height = 32,
                                    .fps = {30, 1},
                                    .colour = standard(ColourPreset::Bt709)};
        FormatChoice expected = fallback;
        expected.colour = {};  // no colour key: the base entry, not the last choice's colour
        const FormatChoice named =
            rawreel::choiceFromUri("file:clips/clip_1922x1080_yuv422p.yuv", fallback);
        CHECK(named.format == "yuv422p");
        CHECK(named.width == 1922);
        CHECK(named.height == 1080);
        CHECK(named.fps == fallback.fps);
        CHECK(named.colour == ColourChoice{});
        const FormatChoice size_only = rawreel::choiceFromUri("clip_33x17.bin", fallback);
        CHECK(size_only.format == "nv12");
        CHECK(size_only.width == 33);
        CHECK(size_only.height == 17);
        const FormatChoice query = rawreel::choiceFromUri(
            "file:clip_33x17_nv12.yuv?fmt=yuv420p&size=48x24&fps=50", fallback);
        CHECK(query.format == "yuv420p");
        CHECK(query.width == 48);
        CHECK(query.height == 24);
        CHECK(query.fps == dvv::media::Rational{50, 1});
        CHECK(rawreel::choiceFromUri("file:clip.yuv", fallback) == expected);
        CHECK(rawreel::choiceFromUri("file:clip_0x17_bad.yuv", fallback) == expected);
        CHECK(rawreel::choiceFromUri("synthetic:gradient?fmt=yuv420p", fallback) == expected);
        CHECK(rawreel::choiceFromUri("file:clip_720x576.yuv?colour=bt601-pal", fallback).colour ==
              standard(ColourPreset::Bt601Pal));
        CHECK(rawreel::choiceFromUri("file:clip_720x576.yuv?range=full", fallback).colour ==
              keyed(std::nullopt, ColourRange::Full));
        // The name's colour token is the base, not a key.
        CHECK(rawreel::choiceFromUri("file:clip_720x576_bt601-pal.yuv", fallback).colour ==
              ColourChoice{});
    }

    TEST_CASE("format choice: fps text") {
        using dvv::media::Rational;
        CHECK(rawreel::parseFpsText("25") == Rational{25, 1});
        CHECK(rawreel::parseFpsText("30000/1001") == Rational{30000, 1001});
        CHECK(rawreel::parseFpsText("2997/100") == Rational{2997, 100});
        CHECK(rawreel::parseFpsText("50.000") == Rational{50, 1});
        CHECK(rawreel::parseFpsText("25.5") == Rational{51, 2});
        // The NTSC decimals give N*1000/1001 (D5 design §1.4).
        CHECK(rawreel::parseFpsText("23.976") == Rational{24000, 1001});
        CHECK(rawreel::parseFpsText("23.98") == Rational{24000, 1001});
        CHECK(rawreel::parseFpsText("29.97") == Rational{30000, 1001});
        CHECK(rawreel::parseFpsText("47.952") == Rational{48000, 1001});
        CHECK(rawreel::parseFpsText("59.94") == Rational{60000, 1001});
        // Trailing zeros map too.
        CHECK(rawreel::parseFpsText("29.970") == Rational{30000, 1001});
        CHECK(rawreel::parseFpsText("59.940") == Rational{60000, 1001});
        CHECK(rawreel::parseFpsText("23.9760") == Rational{24000, 1001});
        CHECK(rawreel::parseFpsText("119.88") == Rational{120000, 1001});
        CHECK(rawreel::parseFpsText("29.971") == Rational{29971, 1000});
        for (const std::string_view bad : {"", "0", "-25", "1/0", "25fps", "2,5"}) {
            CHECK_MESSAGE(!rawreel::parseFpsText(bad), bad);
        }
    }

    TEST_CASE("format choice: lavfi refusal") {
        const auto refusal = rawreel::formatRefusal("lavfi:testsrc2=s=64x48");
        REQUIRE(refusal);
        const std::string text = refusal.value_or("");
        CHECK_MESSAGE(text.find("set the colour in the graph (setparams=") != std::string::npos,
                      text);
        CHECK_FALSE(
            rawreel::formatRefusal("file:clip.mkv?fmt=yuv420p&size=64x48&fps=25&impl=ffmpeg"));
        CHECK_FALSE(rawreel::formatRefusal("synthetic:gradient?fmt=yuv420p&size=8x8"));
    }

    TEST_CASE("format choice: Load JSON") {
        // Files of this test in the working folder (the build tree), removed at the end.
        const std::filesystem::path dir =
            std::filesystem::current_path() / "rawreel-test-load-json";
        std::filesystem::create_directories(dir);
        const auto write = [&dir](std::string_view name, std::string_view text) {
            const std::filesystem::path path = dir / name;
            writeFile(path, text);
            return dvv::media::utf8FromPath(path);
        };
        SUBCASE("a clip description fills only its groups") {
            const std::string path =
                write("clip.json", R"({"rawreelClip": 1, "format": "yuv422p10le",
                    "size": {"width": 3840, "height": 2160}, "frameRate": "60000/1001",
                    "colour": {"standard": "bt2100-pq"}})");
            const auto loaded = rawreel::loadFormatJson(path);
            REQUIRE_MESSAGE(loaded, (loaded ? std::string() : loaded.error().describe()));
            CHECK(loaded->clip);
            CHECK(loaded->format == std::optional<std::string>("yuv422p10le"));
            CHECK(loaded->size ==
                  std::optional(std::pair<std::uint32_t, std::uint32_t>(3840, 2160)));
            CHECK(loaded->fps == std::optional(dvv::media::Rational{60000, 1001}));
            CHECK(loaded->colour == std::optional(standard(ColourPreset::Bt2100Pq)));
            const std::string only_colour =
                write("colour.json",
                      R"({"rawreelClip": 1, "colour": {"standard": "bt709", "range": "full"}})");
            const auto colour = rawreel::loadFormatJson(only_colour);
            REQUIRE(colour);
            CHECK_FALSE(colour->format);
            CHECK_FALSE(colour->size);
            CHECK_FALSE(colour->fps);
            CHECK(colour->colour == std::optional(keyed(ColourPreset::Bt709, ColourRange::Full)));
        }
        SUBCASE("a pixel-format JSON becomes the @<path> format") {
            const std::string path = write("v210_custom.json", R"({"model": "yuv"})");
            const auto loaded = rawreel::loadFormatJson(path);
            REQUIRE(loaded);
            CHECK_FALSE(loaded->clip);
            CHECK(loaded->format == std::optional("@" + path));
            CHECK_FALSE(loaded->size);
            CHECK_FALSE(loaded->colour);
        }
        SUBCASE("errors") {
            const std::string text = write("text.json", "not json");
            const auto not_json = rawreel::loadFormatJson(text);
            REQUIRE_FALSE(not_json);
            CHECK_MESSAGE(not_json.error().message.starts_with(text + ": not JSON: "),
                          not_json.error().message);
            const std::string bad = write("bad.json", R"({"rawreelClip": 2, "format": "gray"})");
            const auto clip = rawreel::loadFormatJson(bad);
            REQUIRE_FALSE(clip);
            CHECK_MESSAGE(clip.error().message.starts_with("clip description '"),
                          clip.error().message);
            CHECK_FALSE(rawreel::loadFormatJson(dvv::media::utf8FromPath(dir / "missing.json")));
        }
        SUBCASE("choiceFromUri reads clip=@ before the query") {
            const std::string path =
                write("sizeless.json",
                      R"({"rawreelClip": 1, "format": "gray", "frameRate": "30000/1001"})");
            const FormatChoice fallback{
                .format = "yuv420p", .width = 1920, .height = 1080, .fps = {25, 1}, .colour = {}};
            const std::string uri = "file:x.yuv?clip=@" + path;
            const FormatChoice clip = rawreel::choiceFromUri(uri, fallback);
            CHECK(clip.format == "gray");
            CHECK(clip.fps == dvv::media::Rational{30000, 1001});
            CHECK(clip.width == 1920);
            CHECK(clip.height == 1080);
            CHECK(clip.colour == ColourChoice{});
            // The query outranks the clip; the clip outranks the file name.
            const FormatChoice query =
                rawreel::choiceFromUri("file:x_64x32_nv12.yuv?clip=@" + path + "&fps=50", fallback);
            CHECK(query.format == "gray");
            CHECK(query.fps == dvv::media::Rational{50, 1});
            CHECK(query.width == 64);
            // OK keeps clip= and writes the clip's format and fps back, not the fallback's.
            const auto applied_uri = rawreel::applyChoice(uri, clip);
            REQUIRE(applied_uri);
            const std::string written = applied_uri.value_or("");
            CHECK_MESSAGE(written.find("fmt=gray") != std::string::npos, written);
            CHECK_MESSAGE(written.find("fps=30000/1001") != std::string::npos, written);
            CHECK_MESSAGE(written.find("clip=") != std::string::npos, written);
            // A clip that cannot be read is ignored (the open names the error).
            const std::string missing =
                "file:x.yuv?clip=@" + dvv::media::utf8FromPath(dir / "missing.json");
            CHECK(rawreel::choiceFromUri(missing, fallback) == fallback);
        }
        std::error_code error;
        std::filesystem::remove_all(dir, error);
    }

    // The open path's base (D5R §4.4): clip=@, the sidecar, the name token, the default.
    TEST_CASE("format choice: colourBaseFromUri") {
        // Relative paths: the working folder is the build tree; removed at the end.
        const std::filesystem::path dir =
            std::filesystem::current_path() / "rawreel-test-colour-base";
        std::filesystem::create_directories(dir);
        writeFile(dir / "c.json",
                  R"({"rawreelClip": 1, "colour": {"standard": "bt2020", "range": "full"}})");
        writeFile(dir / "side_720x576.yuv.json",
                  R"({"rawreelClip": 1, "colour": {"standard": "bt601-ntsc"}})");
        writeFile(dir / "bad_720x576.yuv.json",
                  R"({"rawreelClip": 1, "format": "@no.json", "colour": {"standard": "bt2020"}})");

        // Nothing gives a colour: BT.709 with the default values.
        CHECK(rawreel::colourBaseFromUri("file:a.yuv", "yuv420p", 1080) ==
              defaultBase("yuv420p", 1080));
        CHECK(rawreel::colourBaseFromUri("synthetic:gradient?colour=bt2020", "yuv420p", 576) ==
              defaultBase("yuv420p", 576));
        // The name token.
        const ColourBase name =
            rawreel::colourBaseFromUri("file:clip_720x576_bt601-pal.yuv", "yuv420p", 576);
        CHECK(name.standard == ColourPreset::Bt601Pal);
        CHECK(name.origin == ColourOrigin::FileName);
        CHECK(name.values.matrix == standardValues(ColourPreset::Bt601Pal).matrix);
        CHECK(name.values.primaries == standardValues(ColourPreset::Bt601Pal).primaries);
        CHECK(name.values.range == ColourRange::Limited);
        CHECK_FALSE(name.rangeFromFile);
        // clip=@ outranks the name token; its range is "from file".
        const ColourBase clip = rawreel::colourBaseFromUri(
            "file:clip_720x576_bt601-pal.yuv?clip=@rawreel-test-colour-base/c.json", "yuv420p",
            576);
        CHECK(clip.standard == ColourPreset::Bt2020);
        CHECK(clip.origin == ColourOrigin::Clip);
        CHECK(clip.clipName == "c.json");
        CHECK(clip.values.range == ColourRange::Full);
        CHECK(clip.rangeFromFile);
        // The sidecar <path>.json; no range: the default range.
        const ColourBase side = rawreel::colourBaseFromUri(
            "file:rawreel-test-colour-base/side_720x576.yuv", "yuv420p", 576);
        CHECK(side.standard == ColourPreset::Bt601Ntsc);
        CHECK(side.origin == ColourOrigin::Clip);
        CHECK(side.clipName == "side_720x576.yuv.json");
        CHECK(side.values.range == ColourRange::Limited);
        CHECK_FALSE(side.rangeFromFile);
        // A sidecar whose "@" format cannot be read is ignored (as the provider
        // does), unless fmt= gives the format.
        const ColourBase bad = rawreel::colourBaseFromUri(
            "file:rawreel-test-colour-base/bad_720x576.yuv", "yuv420p", 576);
        CHECK(bad.standard == ColourPreset::Bt709);
        CHECK(bad.origin == ColourOrigin::Default);
        const ColourBase keyed_fmt = rawreel::colourBaseFromUri(
            "file:rawreel-test-colour-base/bad_720x576.yuv?fmt=yuv420p", "yuv420p", 576);
        CHECK(keyed_fmt.standard == ColourPreset::Bt2020);
        CHECK(keyed_fmt.origin == ColourOrigin::Clip);
        // RGB: the matrix is not the standard's.
        const ColourBase rgb =
            rawreel::colourBaseFromUri("file:shot_64x32_rgb24_bt2020.rgb", "rgb24", 32);
        CHECK(rgb.standard == ColourPreset::Bt2020);
        CHECK(rgb.values.matrix == defaultBase("rgb24", 32).values.matrix);
        CHECK(rgb.values.primaries == Primaries::Bt2020);

        std::error_code error;
        std::filesystem::remove_all(dir, error);
    }

    TEST_CASE("format choice: fileColourMask") {
        using dvv::media::AssumedMatrix;
        using dvv::media::AssumedPrimaries;
        using dvv::media::AssumedRange;
        using dvv::media::AssumedTransfer;
        const ColourParams limited_gray =
            rgbColour(ColourRange::Limited, TransferFn::Unspecified, Primaries::Unspecified);
        // A standard gives no range, so the file's range stays "from file" (D5R §1.3).
        CHECK(rawreel::fileColourMask("file:g.y4m?colour=bt709", limited_gray, ColourModel::Gray) ==
              AssumedRange);
        CHECK(rawreel::fileColourMask("file:g.y4m?colour=bt709&range=full", limited_gray,
                                      ColourModel::Gray) == 0);
        // RGB/gray: a container's matrix tag never sets the matrix bit.
        CHECK(rawreel::fileColourMask("file:g.mkv", RGB_TAGGED, ColourModel::Rgb) ==
              (AssumedRange | AssumedTransfer | AssumedPrimaries));
        // YUV: a standard gives matrix, curve and primaries; range= only the range.
        CHECK(rawreel::fileColourMask("file:a.mkv?colour=bt709", HD_TAGGED, ColourModel::Yuv) ==
              AssumedRange);
        CHECK(rawreel::fileColourMask("file:a.mkv?range=full", HD_TAGGED, ColourModel::Yuv) ==
              (AssumedMatrix | AssumedTransfer | AssumedPrimaries));
        CHECK(rawreel::fileColourMask("file:a.mkv", BT709_LIMITED, ColourModel::Yuv) ==
              (AssumedMatrix | AssumedRange));
        CHECK(rawreel::fileColourMask("file:a.mkv?colour=nope", BT709_LIMITED, ColourModel::Yuv) ==
              (AssumedMatrix | AssumedRange));
    }

    TEST_CASE("format choice: FileColourMemory") {
        const ColourParams pq = standardValues(ColourPreset::Bt2100Pq);  // the file's tags
        const ColourParams bt709 = standardValues(ColourPreset::Bt709);
        const auto stream_of = [](std::string_view format, const ColourParams& own,
                                  std::string_view uri) {
            dvv::media::SourceInfo info;
            info.desc = {formatId(format), dvv::media::internColour(own), 64, 48};
            info.fps = {25, 1};
            info.resolvedUri = std::string(uri);
            info.provider = "ffmpeg";
            return rawreel::StreamInfo{.slot = 0,
                                       .resolvedUri = std::string(uri),
                                       .info = info,
                                       .colour = dvv::media::resolveColour(info.desc)};
        };
        constexpr std::uint8_t ALL = dvv::media::AssumedMatrix | dvv::media::AssumedRange |
                                     dvv::media::AssumedTransfer | dvv::media::AssumedPrimaries;
        rawreel::FileColourMemory memory;
        // First opened with a colour key: no entry; the base is the BT.709 default.
        const rawreel::StreamInfo keyed_first =
            stream_of("yuv420p", bt709, "file:hdr.mkv?impl=ffmpeg&colour=bt709&range=limited");
        CHECK(memory.base(keyed_first).fromFile == 0);
        const ColourBase unknown = memory.colourBase(keyed_first);
        CHECK(unknown.standard == ColourPreset::Bt709);
        CHECK(unknown.origin == ColourOrigin::Default);
        // Opened without colour keys, then View > Format writes colour=bt709.
        const rawreel::StreamInfo plain = stream_of("yuv420p", pq, "file:hdr.mkv?impl=ffmpeg");
        const ColourBase file_base = memory.colourBase(plain);
        CHECK(file_base.standard == ColourPreset::Bt2100Pq);
        CHECK(file_base.origin == ColourOrigin::File);
        CHECK(file_base.rangeFromFile);
        memory.remember(plain);
        memory.remember(keyed_first);  // colour keys: not remembered
        const rawreel::StreamInfo keyed_later =
            stream_of("yuv420p", bt709,
                      "file:hdr.mkv?impl=ffmpeg&fmt=yuv420p&size=64x48&fps=25&colour=bt709");
        const rawreel::FileColour base = memory.base(keyed_later);
        CHECK(base.fromFile == ALL);
        CHECK(base.colour.params == pq);
        CHECK(memory.colourBase(keyed_later) == file_base);
        // Another file keeps its own values.
        CHECK(memory.base(stream_of("yuv420p", bt709, "file:other.mkv?colour=bt709")).fromFile ==
              dvv::media::AssumedRange);
        // Gray: the standard leaves the range to the file; the matrix is never "from file".
        rawreel::FileColourMemory gray;
        const ColourParams limited =
            rgbColour(ColourRange::Limited, TransferFn::Unspecified, Primaries::Unspecified);
        const rawreel::FileColour gray_base =
            gray.base(stream_of("gray", limited, "file:g.y4m?colour=bt709"));
        CHECK(gray_base.fromFile == dvv::media::AssumedRange);
        CHECK(gray_base.colour.params.range == ColourRange::Limited);
    }
}
