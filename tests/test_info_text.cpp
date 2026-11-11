#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "core/info_text.h"
#include "core/session_types.h"
#include "dvv/base/json.h"
#include "dvv/media/colour_preset.h"
#include "dvv/media/display.h"
#include "dvv/media/format_json.h"
#include "dvv/media/pixel_format.h"

using dvv::media::ColourMatrix;
using dvv::media::ColourParams;
using dvv::media::ColourRange;
using dvv::media::TransferFn;
using rawreel::StreamInfo;

namespace {

constexpr std::string_view URI =
    "synthetic:gradient?fmt=yuv420p&size=64x48&fps=25&frames=40&seed=1";

// yuv420p 64x48 at 25 fps, 40 frames, with `colour`, opened as `uri`.
StreamInfo stream(const ColourParams& colour = {}, std::string_view uri = URI) {
    dvv::media::SourceInfo info;
    info.desc = {dvv::media::findFormat("yuv420p").value_or(0), dvv::media::internColour(colour),
                 64, 48};
    info.fps = {25, 1};
    info.frameCount = 40;
    info.resolvedUri = std::string(uri);
    return StreamInfo{0, std::string(uri), info, dvv::media::resolveColour(info.desc), {}};
}

}  // namespace

// ---- Info window values (spec §11, §13) ----

namespace {

using rawreel::INFO_PICTURE_ROWS;
using rawreel::infoCopyText;
using rawreel::infoHint;
using rawreel::infoRows;
using rawreel::infoValueSamples;

dvv::media::FormatId formatNamed(std::string_view name) {
    const auto id = dvv::media::findFormat(name);
    REQUIRE_MESSAGE(id.has_value(), name);
    return id.value_or(dvv::media::FormatId{0});  // REQUIRE above stops the test case first
}

// `base` with its format replaced and the colour resolved again for it.
StreamInfo withFormat(StreamInfo base, std::string_view name) {
    base.info.desc.format = formatNamed(name);
    base.colour = dvv::media::resolveColour(base.info.desc);
    return base;
}

StreamInfo withFps(StreamInfo base, std::int64_t num, std::int64_t den) {
    base.info.fps = {num, den};
    return base;
}

std::string row(const StreamInfo& info, std::string_view label) {
    for (const rawreel::InfoRow& entry : infoRows(info)) {
        if (entry.label == label) {
            return entry.value;
        }
    }
    FAIL("no row " << label);
    return {};
}

// A stream with its resolved colour fields replaced.
StreamInfo coloured(const ColourParams& params, std::string_view name = "yuv420p",
                    std::string_view uri = URI) {
    StreamInfo info = withFormat(stream({}, uri), name);
    info.colour.params = params;
    return info;
}

// Custom planar YUV formats that the catalogue lacks: `sub_x` is the chroma's horizontal
// subsampling, `u_bits` the chroma depth (the luma has 8 bits).
dvv::media::FormatId customFormat(std::string_view name, int sub_x, int u_bits) {
    const int word = u_bits > 8 ? 2 : 1;
    const std::string json_text = std::format(
        R"({{"name": "{}", "model": "yuv", "widthMultiple": 1, "components": [
        {{"kind": "Y", "bits": 8, "log2SubX": 0, "log2SubY": 0}},
        {{"kind": "U", "bits": {}, "log2SubX": {}, "log2SubY": 0}},
        {{"kind": "V", "bits": {}, "log2SubX": {}, "log2SubY": 0}}],
        "planes": [
        {{"groupPixels": 1, "groupBytes": 1, "wordBytes": 1, "byteOrder": "little", "rowAlign": 1,
          "log2SubY": 0, "slots": [{{"component": 0, "index": 0, "word": 0, "shift": 0}}]}},
        {{"groupPixels": {}, "groupBytes": {}, "wordBytes": {}, "byteOrder": "little",
          "rowAlign": 1, "log2SubY": 0,
          "slots": [{{"component": 1, "index": 0, "word": 0, "shift": 0}}]}},
        {{"groupPixels": {}, "groupBytes": {}, "wordBytes": {}, "byteOrder": "little",
          "rowAlign": 1, "log2SubY": 0,
          "slots": [{{"component": 2, "index": 0, "word": 0, "shift": 0}}]}}]}})",
        name, u_bits, sub_x, u_bits, sub_x, 1 << sub_x, word, word, 1 << sub_x, word, word);
    const auto parsed = dvv::json::parse(json_text);
    REQUIRE_MESSAGE(parsed.has_value(), name);
    const auto id = dvv::media::formatFromJson(*parsed);
    REQUIRE_MESSAGE(id.has_value(), (id ? std::string() : id.error().message));
    return id.value_or(dvv::media::FormatId{0});
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("info row: order and count") {
        const std::vector<rawreel::InfoRow> rows = infoRows(stream());
        REQUIRE(rows.size() == 8);
        constexpr std::string_view LABELS[] = {"Resolution", "Frame rate",  "Format",
                                               "Standard",   "Matrix",      "Range",
                                               "Transfer",   "Tone mapping"};
        for (std::size_t i = 0; i < rows.size(); ++i) {
            CHECK(rows[i].label == LABELS[i]);
        }
        CHECK(INFO_PICTURE_ROWS == 3);
    }

    TEST_CASE("info row: resolution") {
        StreamInfo info = stream();
        info.info.desc.width = 1920;
        info.info.desc.height = 1080;
        CHECK(row(info, "Resolution") == "1920 \xC3\x97 1080");
    }

    TEST_CASE("info row: frame rate") {
        CHECK(row(withFps(stream(), 50, 1), "Frame rate") == "50 fps");
        CHECK(row(withFps(stream(), 100, 2), "Frame rate") == "50 fps");
        CHECK(row(withFps(stream(), 30000, 1001), "Frame rate") == "29.97 fps");
        CHECK(row(withFps(stream(), 24000, 1001), "Frame rate") == "23.976 fps");
        CHECK(row(withFps(stream(), 0, 0), "Frame rate") == "Unspecified");
        StreamInfo unbounded = withFps(stream(), 25, 1);
        unbounded.info.frameCount = std::nullopt;
        CHECK(row(unbounded, "Frame rate") == "25 fps");
    }

    TEST_CASE("info row: format") {
        const auto format = [](std::string_view name) {
            return row(withFormat(stream(), name), "Format");
        };
        CHECK(format("yuv420p") == "YUV 4:2:0 8-bit (yuv420p)");
        CHECK(format("v210") == "YUV 4:2:2 10-bit (v210)");
        CHECK(format("yuv444p") == "YUV 4:4:4 8-bit (yuv444p)");
        CHECK(format("rgba") == "RGB 8-bit + alpha (rgba)");
        CHECK(format("gray16le") == "Gray 16-bit (gray16le)");
        CHECK(format("rgb24") == "RGB 8-bit (rgb24)");
    }

    // yuv411p and rgb565le are not in dvv-media's catalogue: custom formats stand in.
    TEST_CASE("info row: format name alone") {
        StreamInfo info = stream();
        info.info.desc.format = customFormat("yuv411p", 2, 8);  // other subsampling
        CHECK(row(info, "Format") == "yuv411p");
        info.info.desc.format = customFormat("yuv444p8x10", 0, 10);  // mixed depths
        CHECK(row(info, "Format") == "yuv444p8x10");
    }

    TEST_CASE("info row: standard") {
        // The URI's colour= beats the stream's own standard.
        const ColourParams pq =
            dvv::media::colourPresetInfo(dvv::media::ColourPreset::Bt2100Pq).colour;
        CHECK(row(stream(pq, "file:a.yuv?fmt=yuv420p&size=64x48&fps=25&colour=bt709"),
                  "Standard") == "BT.709");
        CHECK(row(stream(pq, "file:a.mkv?impl=ffmpeg"), "Standard") == "BT.2100 PQ");
        CHECK(row(stream({.matrix = ColourMatrix::Bt709,
                          .range = ColourRange::Unspecified,
                          .transfer = TransferFn::Linear,
                          .primaries = dvv::media::Primaries::Unspecified,
                          .kr = 0,
                          .kb = 0},
                         "file:a.mkv?impl=ffmpeg"),
                  "Standard") == "None");
    }

    TEST_CASE("info row: matrix") {
        const auto matrix = [](ColourMatrix value, std::string_view name = "yuv420p") {
            return row(coloured({.matrix = value}, name), "Matrix");
        };
        CHECK(matrix(ColourMatrix::Bt709) == "BT.709");
        CHECK(matrix(ColourMatrix::Bt601) == "BT.601");
        CHECK(matrix(ColourMatrix::Bt2020Ncl) == "BT.2020");
        CHECK(matrix(ColourMatrix::Unspecified) == "Unspecified");
        CHECK(matrix(ColourMatrix::Rgb) == "Identity");
        CHECK(row(coloured({.matrix = ColourMatrix::Custom, .kr = 0.299F, .kb = 0.114F}),
                  "Matrix") == "Custom (Kr 0.2990, Kb 0.1140)");
        CHECK(matrix(ColourMatrix::Bt709, "rgba") == "Not used");
        CHECK(matrix(ColourMatrix::Bt709, "gray") == "Not used");
    }

    TEST_CASE("info row: range") {
        const auto range = [](ColourRange value, std::string_view name) {
            return row(coloured({.range = value}, name), "Range");
        };
        CHECK(range(ColourRange::Limited, "yuv420p") ==
              "Limited (16\xE2\x80\x93"
              "235)");
        CHECK(range(ColourRange::Limited, "yuv420p10le") ==
              "Limited (64\xE2\x80\x93"
              "940)");
        CHECK(range(ColourRange::Limited, "yuv420p16le") ==
              "Limited (4096\xE2\x80\x93"
              "60160)");
        CHECK(range(ColourRange::Full, "yuv420p10le") ==
              "Full (0\xE2\x80\x93"
              "1023)");
        CHECK(range(ColourRange::Full, "yuv420p") ==
              "Full (0\xE2\x80\x93"
              "255)");
        CHECK(range(ColourRange::Unspecified, "yuv420p") == "Unspecified");
        CHECK(range(ColourRange::Full, "gray10le") ==
              "Full (0\xE2\x80\x93"
              "1023)");
        CHECK(range(ColourRange::Full, "gbrp10le") ==
              "Full (0\xE2\x80\x93"
              "1023)");
    }

    TEST_CASE("info row: transfer and tone mapping") {
        const auto transfer = [](TransferFn value) { return coloured({.transfer = value}); };
        CHECK(row(transfer(TransferFn::Bt1886), "Transfer") == "BT.1886");
        CHECK(row(transfer(TransferFn::Srgb), "Transfer") == "sRGB");
        CHECK(row(transfer(TransferFn::Pq), "Transfer") == "PQ");
        CHECK(row(transfer(TransferFn::Hlg), "Transfer") == "HLG");
        CHECK(row(transfer(TransferFn::Linear), "Transfer") == "Linear");
        CHECK(row(transfer(TransferFn::Unspecified), "Transfer") == "Unspecified");
        constexpr std::string_view MAPPING = "BT.2390, 1000 \xE2\x86\x92 100 nits";
        CHECK(row(transfer(TransferFn::Pq), "Tone mapping") == MAPPING);
        CHECK(row(transfer(TransferFn::Hlg), "Tone mapping") == MAPPING);
        CHECK(row(transfer(TransferFn::Bt1886), "Tone mapping") == "None (SDR)");
        CHECK(row(transfer(TransferFn::Srgb), "Tone mapping") == "None (SDR)");
    }

    TEST_CASE("info hint: summary line") {
        StreamInfo info = withFormat(withFps(stream(), 50, 1), "v210");
        info.info.desc.width = 1920;
        info.info.desc.height = 1080;
        info.colour.params = {.matrix = ColourMatrix::Bt709,
                              .range = ColourRange::Limited,
                              .transfer = TransferFn::Bt1886,
                              .primaries = dvv::media::Primaries::Bt709,
                              .kr = 0,
                              .kb = 0};
        const std::string prefix = "1920 \xC3\x97 1080 \xC2\xB7 50 fps \xC2\xB7 v210 \xC2\xB7 ";
        CHECK(infoHint(info) == prefix + "BT.709 limited");
        info.colour.params.range = ColourRange::Unspecified;
        CHECK(infoHint(info) == prefix + "BT.709");
        // No standard: the range word alone.
        StreamInfo none =
            withFormat(withFps(stream({.matrix = ColourMatrix::Bt709,
                                       .range = ColourRange::Full,
                                       .transfer = TransferFn::Linear,
                                       .primaries = dvv::media::Primaries::Unspecified,
                                       .kr = 0,
                                       .kb = 0},
                                      "file:a.mkv?impl=ffmpeg"),
                               50, 1),
                       "v210");
        none.colour.params.range = ColourRange::Full;
        none.info.desc.width = 1920;
        none.info.desc.height = 1080;
        CHECK(infoHint(none) == prefix + "full");
    }

    TEST_CASE("info copy: lines") {
        CHECK(infoCopyText(nullptr) == "No stream");
        const StreamInfo info = stream();
        const std::string text = infoCopyText(&info);
        CHECK(std::count(text.begin(), text.end(), '\n') == 7);
        CHECK(text.starts_with("Resolution: 64 \xC3\x97 48\nFrame rate: 25 fps\n"));
    }

    TEST_CASE("info samples: value column") {
        const std::vector<std::string> samples = infoValueSamples();
        const auto has = [&](std::string_view text) {
            return std::find(samples.begin(), samples.end(), text) != samples.end();
        };
        CHECK(has("BT.2390, 1000 \xE2\x86\x92 100 nits"));
        CHECK(has("YUV 4:2:2 10-bit (yuv422p10le)"));
        CHECK(has("Custom (Kr 0.2990, Kb 0.1140)"));
        // Every catalogue format's Format value, so none of them is cut (§11).
        for (const dvv::media::FormatId id : dvv::media::catalogue()) {
            StreamInfo info = stream();
            info.info.desc.format = id;
            info.colour = dvv::media::resolveColour(info.info.desc);
            const std::string value = row(info, "Format");
            CHECK_MESSAGE(has(value), value);
        }
    }
}
