#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "core/log_mirror.h"

using rawreel::ListPlace;
using rawreel::LogBatch;
using rawreel::LogEntry;
using rawreel::LogMirror;
using rawreel::MirrorChange;

namespace {

LogBatch batchOf(std::uint64_t first, std::size_t count, bool gap = false) {
    LogBatch batch;
    for (std::size_t i = 0; i < count; ++i) {
        LogEntry entry;
        entry.sequence = first + i;
        batch.entries.push_back(entry);
    }
    batch.gap = gap;
    batch.next = first + count;
    return batch;
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("log mirror: appends and drops") {
        LogMirror mirror;
        const MirrorChange first = mirror.apply(batchOf(0, 4000));
        CHECK(first.dropped == 0);
        CHECK(first.appended == 4000);
        const MirrorChange second = mirror.apply(batchOf(4000, 2000));
        CHECK(second.dropped == 1000);
        CHECK(second.appended == 2000);
        CHECK_FALSE(second.reset);
        CHECK(mirror.size() == 5000);
        CHECK(mirror.at(0).sequence == 1000);
        CHECK_FALSE(mirror.rowOf(999).has_value());
        CHECK(mirror.rowOf(1000) == 0);
        CHECK(mirror.next() == 6000);
    }

    TEST_CASE("log mirror: a gap resets") {
        LogMirror mirror;
        mirror.apply(batchOf(0, 100));
        const MirrorChange change = mirror.apply(batchOf(500, 30, true));
        CHECK(change.reset);
        CHECK(change.dropped == 0);
        CHECK(mirror.size() == 30);
        CHECK(mirror.at(0).sequence == 500);
    }

    TEST_CASE("log mirror: rows after a batch") {
        LogMirror mirror;
        mirror.apply(batchOf(0, 5000));
        const ListPlace before{
            .selected = {10, 2000, 4999}, .anchor = 10, .focus = 2000, .top = 1500, .tail = false};
        mirror.apply(batchOf(5000, 100));
        const auto rows = rowsAfterBatch(before, mirror);
        CHECK(rows.selected == std::vector<std::size_t>{1900, 4899});
        CHECK_FALSE(rows.anchor.has_value());
        CHECK(rows.focus == 1900);
        CHECK(rows.top == 1400);
        CHECK_FALSE(rows.tail);
    }

    TEST_CASE("log mirror: tail stays at the end") {
        LogMirror mirror;
        mirror.apply(batchOf(0, 100));
        const ListPlace before{.selected = {}, .top = 50, .tail = true};
        mirror.apply(batchOf(100, 10));
        const auto rows = rowsAfterBatch(before, mirror);
        CHECK(rows.tail);
        CHECK_FALSE(rows.top.has_value());
    }

    TEST_CASE("log mirror: top dropped") {
        LogMirror mirror;
        mirror.apply(batchOf(0, 5000));
        const ListPlace before{.selected = {}, .top = 50};
        mirror.apply(batchOf(5000, 100));
        const auto rows = rowsAfterBatch(before, mirror);
        CHECK(rows.top == 0);
    }

    TEST_CASE("log mirror: range") {
        LogMirror mirror;
        mirror.apply(batchOf(0, 100));
        const auto inside = mirror.range(10, 20);
        REQUIRE(inside.size() == 11);
        CHECK(inside.front().sequence == 10);
        CHECK(inside.back().sequence == 20);

        LogMirror full;
        full.apply(batchOf(0, 5050));
        const auto partial = full.range(40, 55);  // 0..49 were dropped
        REQUIRE(partial.size() == 6);
        CHECK(partial.front().sequence == 50);
    }
}
