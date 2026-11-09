#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "core/timeline.h"

using namespace rawreel;

TEST_CASE("chooseTicks picks the smallest 1-2-5 interval that is wide enough") {
    CHECK(chooseTicks(0, 99, 400, 4) == TickInterval{1, 10});
    CHECK(chooseTicks(0, 9, 400, 4) == TickInterval{1, 10});
    CHECK(chooseTicks(0, 4566, 400, 4) == TickInterval{50, 100});
    CHECK(chooseTicks(0, 99999, 2000, 4) == TickInterval{200, 1000});
    CHECK(chooseTicks(0, 99999, 400, 4) == TickInterval{1000, 10000});
    CHECK(chooseTicks(0, 0, 400, 4) == TickInterval{0, 0});
    CHECK(chooseTicks(0, 99, 0, 4) == TickInterval{0, 0});
}

TEST_CASE("ticks lists the multiples of the minor interval") {
    const auto t = ticks(0, 25, {5, 10});
    REQUIRE(t.size() == 6);
    const std::int64_t frames[] = {0, 5, 10, 15, 20, 25};
    const bool majors[] = {true, false, true, false, true, false};
    for (std::size_t i = 0; i < t.size(); ++i) {
        CHECK(t[i].frame == frames[i]);
        CHECK(t[i].major == majors[i]);
    }
    CHECK(ticks(0, 0, {0, 0}).empty());
}

TEST_CASE("thumbX maps a value onto the travel") {
    CHECK(thumbX(0, 0, 100, 10, 410) == doctest::Approx(10));
    CHECK(thumbX(50, 0, 100, 10, 410) == doctest::Approx(210));
    CHECK(thumbX(100, 0, 100, 10, 410) == doctest::Approx(410));
    CHECK(thumbX(1, 0, 1, 10, 410) == doctest::Approx(410));
    CHECK(thumbX(0, 0, 0, 10, 410) == doctest::Approx(10));
}

namespace {
TimelineLabelsIn strip() {
    TimelineLabelsIn in;
    in.stripWidth = 400;
    in.gap = 2;
    in.hysteresis = 8;
    return in;
}
}  // namespace

TEST_CASE("placeLabels clamps the current label") {
    auto in = strip();
    in.current = LabelIn{5, 40};
    CHECK(placeLabels(in).current.value_or(-1) == doctest::Approx(0));
    in.current = LabelIn{395, 40};
    CHECK(placeLabels(in).current.value_or(-1) == doctest::Approx(360));
}

TEST_CASE("placeLabels: the current label hides an end label") {
    auto in = strip();
    in.first = LabelIn{10, 10};
    in.current = LabelIn{12, 40};
    in.last = LabelIn{390, 30};
    const auto out = placeLabels(in);
    CHECK_FALSE(out.first.has_value());
    CHECK(out.last.value_or(-1) == doctest::Approx(370));
}

TEST_CASE("placeLabels: the original label's side") {
    auto in = strip();
    SUBCASE("marker at the start hides it") {
        in.original = LabelIn{10, 30};
        in.current = LabelIn{13, 40};
        const auto out = placeLabels(in);
        CHECK_FALSE(out.original.has_value());
        CHECK(out.originalSide == Side::Left);
        CHECK(out.current.value_or(-1) == doctest::Approx(0));
    }
    SUBCASE("preferred side") {
        in.original = LabelIn{100, 30};
        in.current = LabelIn{140, 40};
        const auto out = placeLabels(in);
        CHECK(out.original.value_or(-1) == doctest::Approx(68));
        CHECK(out.originalSide == Side::Left);
    }
    SUBCASE("fallback to the right") {
        in.original = LabelIn{20, 30};
        in.current = LabelIn{200, 40};
        const auto out = placeLabels(in);
        CHECK(out.original.value_or(-1) == doctest::Approx(22));
        CHECK(out.originalSide == Side::Left);
    }
    SUBCASE("fallback to the left at the right edge") {
        in.original = LabelIn{385, 30};
        in.current = LabelIn{300, 40};  // box 280-320
        const auto out = placeLabels(in);
        CHECK(out.original.value_or(-1) == doctest::Approx(353));
        CHECK(out.originalSide == Side::Right);
    }
    SUBCASE("both sides blocked at the right edge hides it") {
        // Right is outside the strip; Left (353-383) overlaps the current box 320-360.
        in.original = LabelIn{385, 30};
        in.current = LabelIn{340, 40};
        const auto out = placeLabels(in);
        CHECK_FALSE(out.original.has_value());
        CHECK(out.originalSide == Side::Right);
    }
    SUBCASE("original hides an end label") {
        in.original = LabelIn{300, 40};
        in.current = LabelIn{100, 40};
        in.last = LabelIn{340, 40};
        const auto out = placeLabels(in);
        CHECK(out.original.value_or(-1) == doctest::Approx(302));
        CHECK_FALSE(out.last.has_value());
    }
    SUBCASE("touching boxes do not overlap") {
        in.original = LabelIn{100, 30};
        in.current = LabelIn{118, 40};  // box 98-138 touches 68-98
        CHECK(placeLabels(in).original.has_value());
    }
    SUBCASE("no current label uses the previous side") {
        in.original = LabelIn{200, 30};
        in.previousSide = Side::Right;
        const auto out = placeLabels(in);
        CHECK(out.original.value_or(-1) == doctest::Approx(202));
        CHECK(out.originalSide == Side::Right);
    }
}

TEST_CASE("placeLabels: hysteresis around the marker") {
    auto in = strip();
    in.original = LabelIn{200, 30};
    const auto side = [&](double cur, Side prev) {
        in.current = LabelIn{cur, 40};
        in.previousSide = prev;
        return placeLabels(in).originalSide;
    };
    CHECK(side(205, Side::Left) == Side::Left);
    CHECK(side(195, Side::Left) == Side::Left);
    CHECK(side(191, Side::Left) == Side::Right);
    CHECK(side(209, Side::Right) == Side::Left);
}

TEST_CASE("frameAt maps a pointer x onto the frame") {
    CHECK(frameAt(10, 0, 100, 10, 410) == 0);
    CHECK(frameAt(210, 0, 100, 10, 410) == 50);
    CHECK(frameAt(410, 0, 100, 10, 410) == 100);
    CHECK(frameAt(211, 0, 100, 10, 410) == 50);
    CHECK(frameAt(213, 0, 100, 10, 410) == 51);
    CHECK(frameAt(0, 0, 100, 10, 410) == 0);
    CHECK(frameAt(500, 0, 100, 10, 410) == 100);
    CHECK(frameAt(409, 0, 1, 10, 410) == 1);
    CHECK(frameAt(200, 0, 1, 10, 410) == 0);
    CHECK(frameAt(5, 0, 100, 10, 10) == 0);
    CHECK(frameAt(200, 7, 7, 10, 410) == 7);
}

TEST_CASE("frameAt inverts thumbX") {
    for (std::int64_t v = 0; v <= 100; ++v) {
        CHECK(frameAt(thumbX(v, 0, 100, 10, 410), 0, 100, 10, 410) == v);
    }
    const std::int64_t values[] = {0, 1, 2283, 4565, 4566};
    for (const std::int64_t v : values) {
        CHECK(frameAt(thumbX(v, 0, 4566, 10, 786), 0, 4566, 10, 786) == v);
    }
}
