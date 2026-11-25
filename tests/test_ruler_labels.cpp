#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <vector>

#include "core/overlay.h"
#include "core/ruler_labels.h"
#include "dvv/media/view_lines.h"

using dvv::media::AxisView;
using rawreel::cursorTagTextBlack;
using rawreel::decimalDigits;
using rawreel::LabelSpacing;
using rawreel::LastValue;
using rawreel::Rgb8;
using rawreel::RulerLabel;
using rawreel::RulerLabelInput;
using rawreel::RulerLabelKind;
using rawreel::rulerLabels;

namespace {

constexpr std::uint32_t ADVANCE = 7;
constexpr LabelSpacing SPACING{.numberGap = 4, .tagPad = 6, .tagInset = 2, .tagMargin = 4};

// A whole 3840-pixel axis at zoom 1 on a ruler of the same span, numbers on, the major step 32.
RulerLabelInput wholeAxis() {
    return {.axis = {.offset = 0, .zoom = 1.0F, .size = 3840, .extent = 3840},
            .pictureStart = 0,
            .rulerBegin = 0,
            .rulerEnd = 3840,
            .advance = ADVANCE,
            .spacing = SPACING,
            .majorStepLog2 = 5,
            .numbers = true,
            .ticksShown = true};
}

std::vector<RulerLabel> ofKind(const std::vector<RulerLabel>& labels, RulerLabelKind kind) {
    std::vector<RulerLabel> out;
    std::ranges::copy_if(labels, std::back_inserter(out),
                         [kind](const RulerLabel& label) { return label.kind == kind; });
    return out;
}

std::optional<RulerLabel> numberOf(const std::vector<RulerLabel>& labels, std::uint32_t value) {
    const auto it = std::ranges::find_if(labels, [value](const RulerLabel& label) {
        return label.kind == RulerLabelKind::Number && label.value == value;
    });
    return it == labels.end() ? std::nullopt : std::optional<RulerLabel>(*it);
}

RulerLabel tag(RulerLabelKind kind, std::uint32_t value, std::int32_t box_begin) {
    const auto len = static_cast<std::int32_t>(decimalDigits(value) * ADVANCE + 2 * SPACING.tagPad);
    return {.kind = kind,
            .value = value,
            .textBegin = box_begin + static_cast<std::int32_t>(SPACING.tagPad),
            .boxBegin = box_begin,
            .boxEnd = box_begin + len};
}

RulerLabel number(std::uint32_t value, std::int32_t text_begin) {
    return {.kind = RulerLabelKind::Number,
            .value = value,
            .textBegin = text_begin,
            .boxBegin = text_begin,
            .boxEnd = text_begin + static_cast<std::int32_t>(decimalDigits(value) * ADVANCE)};
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("ruler labels: decimalDigits") {
        CHECK(decimalDigits(0) == 1);
        CHECK(decimalDigits(9) == 1);
        CHECK(decimalDigits(10) == 2);
        CHECK(decimalDigits(99999) == 5);
        CHECK(decimalDigits(100000) == 6);
        CHECK(decimalDigits(4294967295U) == 10);
    }

    TEST_CASE("ruler labels: end values of a whole axis, Last pixel and Edge") {
        RulerLabelInput input = wholeAxis();
        input.ticksShown = false;
        CHECK(rulerLabels(input) == std::vector<RulerLabel>{
                                        tag(RulerLabelKind::FirstTag, 0, 4),
                                        tag(RulerLabelKind::LastTag, 3839, 3836 - 40),
                                    });
        input.lastValue = LastValue::Edge;
        CHECK(rulerLabels(input) == std::vector<RulerLabel>{
                                        tag(RulerLabelKind::FirstTag, 0, 4),
                                        tag(RulerLabelKind::LastTag, 3840, 3836 - 40),
                                    });
    }

    TEST_CASE("ruler labels: the numbers of a whole axis, in drawing order") {
        // Needed spacing 4 x 7 + 7 = 35: the step is 64 at zoom 1. 0 touches the first tag
        // [4, 23) widened to [-3, 30); 3776 and 3840 touch the last tag [3796, 3836) widened to
        // [3789, 3843); 3840's text also leaves the ruler.
        const std::vector<RulerLabel> labels = rulerLabels(wholeAxis());
        const std::vector<RulerLabel> numbers = ofKind(labels, RulerLabelKind::Number);
        REQUIRE(numbers.size() == 3840 / 64 - 2);
        for (std::size_t i = 0; i < numbers.size(); ++i) {
            const auto k = static_cast<std::uint32_t>(64 * (i + 1));
            CHECK(numbers[i] == number(k, static_cast<std::int32_t>(k) + 4));
        }
        REQUIRE(labels.size() == numbers.size() + 2);
        CHECK(labels[labels.size() - 2] == tag(RulerLabelKind::FirstTag, 0, 4));
        CHECK(labels.back() == tag(RulerLabelKind::LastTag, 3839, 3796));
    }

    TEST_CASE("ruler labels: the picture's start moves the numbers, the ruler moves the tags") {
        RulerLabelInput input = wholeAxis();
        input.pictureStart = 30;
        input.rulerBegin = 30;
        input.rulerEnd = 3870;
        const std::vector<RulerLabel> labels = rulerLabels(input);
        CHECK(numberOf(labels, 64) == number(64, 98));
        CHECK(ofKind(labels, RulerLabelKind::FirstTag) ==
              std::vector<RulerLabel>{tag(RulerLabelKind::FirstTag, 0, 34)});
    }

    TEST_CASE("ruler labels: a number touching the first tag is dropped, one pixel clear is kept") {
        // The picture's start cut by `cut` pixels: the first value `cut` (two digits) gives the
        // first tag [4, 30), widened to [-3, 37); 64's text begins at 68 - cut.
        RulerLabelInput input = wholeAxis();
        input.axis.extent = 1000;
        input.rulerEnd = 1000;
        input.axis.offset = -31;
        std::vector<RulerLabel> labels = rulerLabels(input);
        CHECK(ofKind(labels, RulerLabelKind::FirstTag) ==
              std::vector<RulerLabel>{tag(RulerLabelKind::FirstTag, 31, 4)});
        CHECK(numberOf(labels, 64) == number(64, 37));
        input.axis.offset = -32;
        labels = rulerLabels(input);
        CHECK(ofKind(labels, RulerLabelKind::FirstTag) ==
              std::vector<RulerLabel>{tag(RulerLabelKind::FirstTag, 32, 4)});
        CHECK_FALSE(numberOf(labels, 64).has_value());
        CHECK(numberOf(labels, 128) == number(128, 100));
    }

    TEST_CASE("ruler labels: a number must lie inside the ruler") {
        // Rulers too short for a tag: 64's text [68, 82) alone decides.
        RulerLabelInput input = wholeAxis();
        input.rulerBegin = 68;
        input.rulerEnd = 82;
        CHECK(rulerLabels(input) == std::vector{number(64, 68)});
        input.rulerBegin = 69;
        CHECK(rulerLabels(input).empty());
        input.rulerBegin = 68;
        input.rulerEnd = 81;
        CHECK(rulerLabels(input).empty());
    }

    TEST_CASE("ruler labels: both end tags, only the first, or none on short rulers") {
        // A 100-pixel image: the first tag is 19 long, the last (99) 26; with the 4-pixel margins
        // both need 4 + 19 + 7 + 26 + 4 = 60 pixels, the first alone 4 + 19 + 4 = 27.
        RulerLabelInput input{.axis = {.offset = 0, .zoom = 1.0F, .size = 100, .extent = 100},
                              .rulerBegin = 10,
                              .advance = ADVANCE,
                              .spacing = SPACING,
                              .numbers = true};
        const auto kinds = [&input](std::int32_t length) {
            input.rulerEnd = input.rulerBegin + length;
            std::vector<RulerLabelKind> out;
            for (const RulerLabel& label : rulerLabels(input)) {
                out.push_back(label.kind);
            }
            return out;
        };
        using enum RulerLabelKind;
        CHECK(kinds(60) == std::vector{FirstTag, LastTag});
        input.rulerEnd = 70;
        CHECK(rulerLabels(input).back() == tag(LastTag, 99, 66 - 26));
        CHECK(kinds(59) == std::vector{FirstTag});
        CHECK(kinds(27) == std::vector{FirstTag});
        input.rulerEnd = 37;
        CHECK(rulerLabels(input) == std::vector{tag(FirstTag, 0, 14)});
        CHECK(kinds(26).empty());
        CHECK(kinds(0).empty());
    }

    TEST_CASE("ruler labels: the cursor tag is centred and hides the numbers it touches") {
        // Pixel 1000's centre 1000.5 floors to 1000; the tag is 40 long: [980, 1020), widened
        // [973, 1027). 960's text [964, 978) touches it; 1024's [1028, 1056) is one pixel clear.
        RulerLabelInput input = wholeAxis();
        input.cursor = 1000;
        const std::vector<RulerLabel> labels = rulerLabels(input);
        CHECK(labels.back() == tag(RulerLabelKind::CursorTag, 1000, 980));
        CHECK_FALSE(numberOf(labels, 960).has_value());
        CHECK(numberOf(labels, 896) == number(896, 900));
        CHECK(numberOf(labels, 1024) == number(1024, 1028));
        CHECK(ofKind(labels, RulerLabelKind::FirstTag).size() == 1);
        CHECK(ofKind(labels, RulerLabelKind::LastTag).size() == 1);
    }

    TEST_CASE("ruler labels: a number ending one advance before the cursor tag is kept") {
        // 960's text is [964, 985). Cursor 1012: the tag [992, 1032), widened [985, 1039): clear.
        // Cursor 1011: the tag [991, 1031), widened [984, 1038): 960 touches it.
        RulerLabelInput input = wholeAxis();
        input.cursor = 1012;
        const std::vector<RulerLabel> clear = rulerLabels(input);
        REQUIRE(clear.back() == tag(RulerLabelKind::CursorTag, 1012, 992));
        CHECK(numberOf(clear, 960) == number(960, 964));
        input.cursor = 1011;
        const std::vector<RulerLabel> touching = rulerLabels(input);
        REQUIRE(touching.back() == tag(RulerLabelKind::CursorTag, 1011, 991));
        CHECK_FALSE(numberOf(touching, 960).has_value());
    }

    TEST_CASE("ruler labels: a cursor tag of odd length begins half its length down") {
        // Cursor 500: three digits, 21 + 2 x 6 = 33 long, centred on 500: 500 - 33 / 2 = 484.
        RulerLabelInput input = wholeAxis();
        input.cursor = 500;
        CHECK(rulerLabels(input).back() == tag(RulerLabelKind::CursorTag, 500, 484));
    }

    TEST_CASE("ruler labels: the cursor tag centres on the pixel at a fractional zoom") {
        // Zoom 2.5, offset 3: pixel 10's centre 3 + 10.5 x 2.5 = 29.25 floors to 29; the picture
        // starts at 50. The tag (two digits) is 26 long: [79 - 13, 79 + 13).
        RulerLabelInput input = wholeAxis();
        input.axis = {.offset = 3, .zoom = 2.5F, .size = 3840, .extent = 3840};
        input.pictureStart = 50;
        input.rulerBegin = 0;
        input.rulerEnd = 4000;
        input.cursor = 10;
        CHECK(rulerLabels(input).back() == tag(RulerLabelKind::CursorTag, 10, 66));
    }

    TEST_CASE("ruler labels: the cursor tag is clamped at both ends and hides the end tag") {
        RulerLabelInput input = wholeAxis();
        input.cursor = 3;  // centre 3, 19 long: begins at -6, clamped to L = 4
        std::vector<RulerLabel> labels = rulerLabels(input);
        CHECK(labels.back() == tag(RulerLabelKind::CursorTag, 3, 4));
        CHECK(ofKind(labels, RulerLabelKind::FirstTag).empty());
        CHECK(ofKind(labels, RulerLabelKind::LastTag) ==
              std::vector<RulerLabel>{tag(RulerLabelKind::LastTag, 3839, 3796)});

        input.cursor = 3839;  // centre 3839, 40 long: begins at 3819, clamped to R - 40 = 3796
        labels = rulerLabels(input);
        CHECK(labels.back() == tag(RulerLabelKind::CursorTag, 3839, 3796));
        CHECK(ofKind(labels, RulerLabelKind::LastTag).empty());
        CHECK(ofKind(labels, RulerLabelKind::FirstTag) ==
              std::vector<RulerLabel>{tag(RulerLabelKind::FirstTag, 0, 4)});
        CHECK_FALSE(numberOf(labels, 3776).has_value());
    }

    TEST_CASE("ruler labels: on a ruler shorter than the cursor tag the start margin wins") {
        // Ruler [10, 40): L = 14, R = 36; the tag for 50 is 26 long, R - L = 22.
        RulerLabelInput input{.axis = {.offset = 0, .zoom = 1.0F, .size = 100, .extent = 100},
                              .rulerBegin = 10,
                              .rulerEnd = 40,
                              .advance = ADVANCE,
                              .spacing = SPACING,
                              .numbers = true,
                              .cursor = 50};
        CHECK(rulerLabels(input) == std::vector{tag(RulerLabelKind::CursorTag, 50, 14)});
    }

    TEST_CASE("ruler labels: a cursor centre past 2^63 saturates before it becomes an integer") {
        // Pixel 2 at zoom 2^62 is centred at 2.5 x 2^62, which no int64 holds: the tag is
        // clamped at R like any centre past the ruler's end.
        RulerLabelInput input = wholeAxis();
        input.axis.zoom = 0x1p62F;
        input.ticksShown = false;
        input.cursor = 2;
        const std::vector<RulerLabel> labels = rulerLabels(input);
        REQUIRE_FALSE(labels.empty());
        CHECK(labels.back() == tag(RulerLabelKind::CursorTag, 2, 3836 - 19));
    }

    TEST_CASE("ruler labels: a cursor off the image has no tag") {
        RulerLabelInput input = wholeAxis();
        input.ticksShown = false;
        input.cursor = 3840;
        CHECK(ofKind(rulerLabels(input), RulerLabelKind::CursorTag).empty());
        CHECK(rulerLabels(input).size() == 2);
    }

    TEST_CASE("ruler labels: a disabled ruler keeps the tags and has no numbers") {
        RulerLabelInput input = wholeAxis();
        input.ticksShown = false;
        input.cursor = 1000;
        CHECK(rulerLabels(input) == std::vector<RulerLabel>{
                                        tag(RulerLabelKind::FirstTag, 0, 4),
                                        tag(RulerLabelKind::LastTag, 3839, 3796),
                                        tag(RulerLabelKind::CursorTag, 1000, 980),
                                    });
    }

    TEST_CASE("ruler labels: numbers off, no glyph strip or no image give no labels") {
        RulerLabelInput input = wholeAxis();
        input.cursor = 1000;
        REQUIRE_FALSE(rulerLabels(input).empty());
        input.numbers = false;
        CHECK(rulerLabels(input).empty());
        input = wholeAxis();
        input.advance = 0;
        CHECK(rulerLabels(input).empty());
        input = wholeAxis();
        input.axis.zoom = 0;
        CHECK(rulerLabels(input).empty());
        input = wholeAxis();
        input.axis.size = 0;
        CHECK(rulerLabels(input).empty());
        input = wholeAxis();
        input.axis.offset = 4000;  // the image lies past the viewport
        CHECK(rulerLabels(input).empty());
    }

    TEST_CASE("ruler labels: a 16384-pixel axis has five-digit labels") {
        // Zoom 1/4: needed 5 x 7 + 7 = 42, so the step is 256 (64 viewport pixels).
        RulerLabelInput input = wholeAxis();
        input.axis = {.offset = 0, .zoom = 0.25F, .size = 16384, .extent = 4096};
        input.rulerEnd = 4096;
        const std::vector<RulerLabel> labels = rulerLabels(input);
        CHECK(numberOf(labels, 10240) == number(10240, 2564));
        CHECK(numberOf(labels, 256) == number(256, 68));
        CHECK_FALSE(numberOf(labels, 128).has_value());
        CHECK(ofKind(labels, RulerLabelKind::LastTag) ==
              std::vector<RulerLabel>{tag(RulerLabelKind::LastTag, 16383, 4092 - 47)});
    }

    TEST_CASE("ruler labels: a huge image ends the number loop") {
        RulerLabelInput input = wholeAxis();
        input.axis = {.offset = -1000000, .zoom = 1.0F, .size = 0x80000000U, .extent = 3840};
        const std::vector<RulerLabel> labels = rulerLabels(input);
        const std::vector<RulerLabel> numbers = ofKind(labels, RulerLabelKind::Number);
        // Ten digits: needed 77, so the step is 128; 1000064 touches the first tag [4, 65).
        REQUIRE_FALSE(numbers.empty());
        CHECK(std::ranges::all_of(numbers, [](const RulerLabel& label) {
            return label.value % 128 == 0 && label.textBegin >= 0 && label.boxEnd <= 3840;
        }));
        CHECK(numbers.front().value == 1000192);
        CHECK(ofKind(labels, RulerLabelKind::FirstTag) ==
              std::vector<RulerLabel>{tag(RulerLabelKind::FirstTag, 1000000, 4)});
    }

    TEST_CASE("ruler labels: the numbers equal a check of every multiple") {
        // Off-screen starts, fractional zooms and a ruler that starts inside the picture region.
        for (const float zoom : {0.3F, 0.75F, 1.0F, 1.7F, 3.0F, 8.0F}) {
            for (const std::int32_t offset : {-5000, -777, -64, 0, 13, 900}) {
                RulerLabelInput input = wholeAxis();
                input.axis = {.offset = offset, .zoom = zoom, .size = 3840, .extent = 2000};
                input.pictureStart = 40;
                input.rulerBegin = 140;
                input.rulerEnd = 1900;
                input.cursor = 1234;
                const std::vector<RulerLabel> labels = rulerLabels(input);
                if (labels.empty()) {
                    continue;  // the image lies outside the viewport
                }
                std::vector<RulerLabel> tags;
                std::ranges::copy_if(labels, std::back_inserter(tags), [](const RulerLabel& l) {
                    return l.kind != RulerLabelKind::Number;
                });
                const float needed = 4.0F * ADVANCE + ADVANCE;
                const std::uint32_t step = 1U << dvv::media::numberStepLog2(5, zoom, needed, 3840);
                std::vector<RulerLabel> expected;
                for (std::uint32_t k = 0; k <= 3840; k += step) {
                    const std::int32_t begin = 40 + dvv::media::firstPixelOf(input.axis, k) + 4;
                    const RulerLabel candidate = number(k, begin);
                    const bool clear = std::ranges::none_of(tags, [&](const RulerLabel& t) {
                        return candidate.boxBegin < t.boxEnd + 7 &&
                               t.boxBegin - 7 < candidate.boxEnd;
                    });
                    if (begin >= 140 && candidate.boxEnd <= 1900 && clear) {
                        expected.push_back(candidate);
                    }
                }
                CAPTURE(zoom);
                CAPTURE(offset);
                CHECK(ofKind(labels, RulerLabelKind::Number) == expected);
            }
        }
    }

    TEST_CASE("ruler labels: the cursor tag's text colour") {
        CHECK_FALSE(cursorTagTextBlack(Rgb8{.r = 0xff, .g = 0x3b, .b = 0x30}));
        CHECK_FALSE(cursorTagTextBlack(Rgb8{.r = 0xff, .g = 0x2d, .b = 0xd2}));
        CHECK(cursorTagTextBlack(Rgb8{.r = 0xff, .g = 0xff, .b = 0xff}));
        CHECK_FALSE(cursorTagTextBlack(Rgb8{.r = 0, .g = 0, .b = 0}));
        // Grey decodes to its own luminance: 0xbc is 0.503, 0xbb 0.497.
        CHECK(cursorTagTextBlack(Rgb8{.r = 0xbc, .g = 0xbc, .b = 0xbc}));
        CHECK_FALSE(cursorTagTextBlack(Rgb8{.r = 0xbb, .g = 0xbb, .b = 0xbb}));
        CHECK(cursorTagTextBlack(Rgb8{.r = 0xff, .g = 0xd6, .b = 0x0a}));  // yellow
    }
}
