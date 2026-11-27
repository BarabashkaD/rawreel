// Texts of the pixel inspector (design §4.6, §4.7).

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/inspector_text.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/sample_grid.h"

namespace {

using namespace rawreel;

dvv::media::FormatId idOf(std::string_view name) {
    const auto id = dvv::media::findFormat(name);
    REQUIRE(id.has_value());
    return id.value_or(dvv::media::FormatId{});
}

const dvv::media::PixelFormatDesc& descOf(std::string_view name) {
    return dvv::media::formatDesc(idOf(name));
}

// The pixel (0, 0) of a 6 x 2 frame of `name` with each component's word set to `words`.
PixelValues valuesOf(std::string_view name, std::array<std::uint32_t, 4> words,
                     std::array<std::uint32_t, 4> codes) {
    const auto& desc = descOf(name);
    PixelValues values;
    values.pixel = {.x = 1236, .y = 565};
    values.frame = 42;
    values.count = desc.componentCount;
    values.letters = componentLetters(desc, true);
    values.codes = codes;
    values.words = words;
    values.bits = desc.components[0].bits;
    for (std::size_t c = 0; c < desc.componentCount; ++c) {
        const auto word = dvv::media::storedWord(idOf(name), 6, 2, c, {});
        REQUIRE(word.has_value());
        values.stored[c] = *word;
    }
    return values;
}

PixelValues p010(std::uint32_t y_word) {
    return valuesOf("p010le", {y_word, 0x7D80, 0x84C0, 0}, {630, 502, 527, 0});
}

}  // namespace

TEST_CASE("inspector text: storageText") {
    CHECK(storageText(descOf("yuyv422")) == "packed: Y0 U Y1 V per 2 pixels");
    CHECK(storageText(descOf("nv12")) == "semi-planar: Y + interleaved UV");
    CHECK(storageText(descOf("p010le")) == "semi-planar: Y + interleaved UV");
    CHECK(storageText(descOf("rgb24")) == "packed: R G B per pixel");
    CHECK(storageText(descOf("yuv420p")) == "planar: Y, U, V planes");
    CHECK(storageText(descOf("gray")) == "one plane");
}

TEST_CASE("inspector text: letters and cells") {
    CHECK(letterCell('0') == 0);
    CHECK(letterCell('G') == 14);
    CHECK(letterCell('A') == 16);
    auto yuv = descOf("yuv444p");
    CHECK(componentLetters(yuv, true) == std::array<char, 4>{'Y', 'U', 'V', '\0'});
    CHECK(componentLetters(yuv, false) == std::array<char, 4>{'G', 'B', 'R', '\0'});
    CHECK(componentLetters(descOf("rgb24"), true) == std::array<char, 4>{'R', 'G', 'B', '\0'});
}

TEST_CASE("inspector text: header of a p010le pixel with garbage 45") {
    const PixelValues values = p010(0x9DAD);
    const auto lines =
        headerLines(values, "p010le", descOf("p010le"), {.first = 1220, .last = 1251},
                    {.first = 549, .last = 580}, 32, false);
    CHECK(lines[0] == "pinned x 1236 y 565 · frame 42");
    CHECK(lines[1] == "Y 630 U 502 V 527 (10-bit)  ⚠ Y padding 45 (must be 0)");
    CHECK(lines[2] == "p010le · semi-planar: Y + interleaved UV");
    CHECK(lines[3] == "x 1220–1251  y 549–580 · zoom ×32");

    PixelValues live = values;
    live.live = true;
    const auto planes = headerLines(live, "p010le", descOf("p010le"), {.first = 1, .last = 2},
                                    {.first = 3, .last = 4}, 8, true);
    CHECK(planes[0] == "cursor x 1236 y 565 · frame 42");
    CHECK(planes[3] == "x 1–2  y 3–4 · zoom ×8 · panels Y | U | V");
}

TEST_CASE("inspector text: status texts") {
    PixelValues values = p010(0x9DC0);
    CHECK(pixelStatusText(values) == "pinned 1236 565: Y 630 U 502 V 527");
    values.live = true;
    CHECK(pixelStatusText(values) == "cursor 1236 565: Y 630 U 502 V 527");
    CHECK(pixelStatusText(std::nullopt).empty());
    CHECK(paddingStatusText(std::nullopt).empty());
    CHECK(paddingStatusText(0).empty());
    CHECK(paddingStatusText(3) == "⚠ padding ≠ 0: 3 samples in this frame");
}

TEST_CASE("inspector text: copyText") {
    const PixelValues values = valuesOf("p010le", {0x9DAD, 0x7D80, 0x84C0, 0}, {630, 502, 527, 0});
    CHECK(copyText(values) ==
          "frame 42 x 1236 y 565 Y 630 U 502 V 527\nraw Y 0x9DAD U 0x7D80 V 0x84C0");
}

TEST_CASE("inspector text: rawWordLines of p010le") {
    const auto lines = rawWordLines(p010(0x9DAD));
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].text == "Y 0x9DAD  1001 1101 1010 1101  → value 630, padding 45");
    REQUIRE(lines[0].runs.size() == 2);
    CHECK(lines[0].runs[0].begin == 0);
    CHECK(lines[0].runs[0].end == 6);
    CHECK(lines[0].runs[0].kind == BitKind::PaddingSet);
    CHECK(lines[0].runs[1].begin == 6);
    CHECK(lines[0].runs[1].end == 16);
    CHECK(lines[0].runs[1].kind == BitKind::Value);
    CHECK(lines[0].runs[1].own);
}

TEST_CASE("inspector text: rawWordLines of a v210 word") {
    const PixelValues values =
        valuesOf("v210", {0x2AB6B67F, 0x2AB6B67F, 0x2AB6B67F, 0}, {1, 2, 3, 0});
    const auto lines = rawWordLines(values);
    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0].runs.size() == 4);
    std::size_t samples = 0;
    for (const BitRun& run : lines[0].runs) {
        samples += run.kind == BitKind::Value ? 1 : 0;
    }
    CHECK(samples == 3);
    const BitRun& padding = lines[0].runs.back();
    CHECK(padding.kind == BitKind::Padding);
    CHECK(padding.begin == 30);
    CHECK(padding.end == 32);
}

TEST_CASE("inspector text: padding is the padding field of a v210 word") {
    const PixelValues values =
        valuesOf("v210", {0xEAB6B67F, 0xEAB6B67F, 0xEAB6B67F, 0}, {1, 2, 3, 0});
    const auto lines = rawWordLines(values);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].text.ends_with("padding 3"));
}
