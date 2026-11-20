#include <doctest/doctest.h>

#include <deque>
#include <format>
#include <source_location>
#include <vector>

#include "core/log_text.h"

using rawreel::LocalTime;
using rawreel::LogEntry;

namespace {

LogEntry lineEntry(const char* line) {
    LogEntry entry;
    entry.line = line;
    return entry;
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("log text: notice") {
        CHECK(rawreel::logNoticeText(0, 0).empty());
        CHECK(rawreel::logNoticeText(0, 1) == "1 warning in the log");
        CHECK(rawreel::logNoticeText(0, 2) == "2 warnings in the log");
        CHECK(rawreel::logNoticeText(1, 0) == "1 error in the log");
        CHECK(rawreel::logNoticeText(2, 3) == "2 errors, 3 warnings in the log");
    }

    TEST_CASE("log text: records") {
        const std::vector<LogEntry> two{lineEntry("a"), lineEntry("b\nc")};
        CHECK(rawreel::logRecordsText(two) == "a\nb\nc\n");
        const std::vector<LogEntry> ends{lineEntry("d\n")};
        CHECK(rawreel::logRecordsText(ends) == "d\n");
        const std::vector<LogEntry> none;
        CHECK(rawreel::logRecordsText(none).empty());
        const std::deque<LogEntry> queue{lineEntry("a"), lineEntry("b\nc")};
        CHECK(rawreel::logRecordsText(queue) == "a\nb\nc\n");
    }

    TEST_CASE("log text: source") {
        LogEntry entry;
        entry.where = std::source_location::current();
        CHECK(rawreel::logSourceText(entry) ==
              std::format("test_log_text.cpp:{}", entry.where.line()));
    }

    TEST_CASE("log text: row message") {
        CHECK(rawreel::logRowMessage("one") == "one");
        CHECK(rawreel::logRowMessage("one\ntwo") == "one …");
    }

    TEST_CASE("log text: row time and file name") {
        LocalTime time;
        time.tm.tm_year = 2026 - 1900;
        time.tm.tm_mon = 10;
        time.tm.tm_mday = 10;
        time.tm.tm_hour = 9;
        time.tm.tm_min = 5;
        time.tm.tm_sec = 3;
        time.millis = 7;
        CHECK(rawreel::logRowTime(time) == "09:05:03.007");
        CHECK(rawreel::logSaveFileName(time) == "rawreel-log-20261110-090503.txt");
    }

    TEST_CASE("log text: the UTC breakdown") {
        using namespace std::chrono;
        struct Expected {
            sys_seconds time;
            int year, mon, mday, hour, min, sec, wday, yday;
        };
        const Expected cases[] = {
            {sys_seconds{0s}, 70, 0, 1, 0, 0, 0, 4, 0},
            {sys_days{2024y / 2 / 29} + 12h + 34min + 56s, 124, 1, 29, 12, 34, 56, 4, 59},
            {sys_days{2023y / 12 / 31} + 23h + 59min + 59s, 123, 11, 31, 23, 59, 59, 0, 364},
            {sys_seconds{-1s}, 69, 11, 31, 23, 59, 59, 3, 364},
            {sys_days{2100y / 3 / 1}, 200, 2, 1, 0, 0, 0, 1, 59},
            {sys_days{1900y / 1 / 1}, 0, 0, 1, 0, 0, 0, 1, 0},
        };
        for (const Expected& expected : cases) {
            const std::tm tm = rawreel::utcBreakdown(expected.time);
            CHECK(tm.tm_year == expected.year);
            CHECK(tm.tm_mon == expected.mon);
            CHECK(tm.tm_mday == expected.mday);
            CHECK(tm.tm_hour == expected.hour);
            CHECK(tm.tm_min == expected.min);
            CHECK(tm.tm_sec == expected.sec);
            CHECK(tm.tm_wday == expected.wday);
            CHECK(tm.tm_yday == expected.yday);
            CHECK(tm.tm_isdst == 0);
        }
    }
}
