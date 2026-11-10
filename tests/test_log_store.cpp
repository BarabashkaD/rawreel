#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <thread>
#include <vector>

#include "core/log_store.h"
#include "dvv/base/log.h"

using rawreel::LOG_STORE_RECORDS;
using rawreel::LogCounts;
using rawreel::LogStore;

namespace {

void put(LogStore& store, dvv::LogLevel level, std::string_view message,
         std::source_location where = std::source_location::current()) {
    const dvv::LogRecord record{
        .level = level,
        .message = message,
        .where = where,
        .time = std::chrono::system_clock::now(),
    };
    store.write(record, dvv::formatLogRecord(record));
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("log store: order and sequence numbers") {
        LogStore store;
        put(store, dvv::LogLevel::Info, "a");
        put(store, dvv::LogLevel::Info, "b");
        auto batch = store.take(0);
        REQUIRE(batch.entries.size() == 2);
        CHECK(batch.entries[0].sequence == 0);
        CHECK(batch.entries[1].sequence == 1);
        CHECK(batch.entries[0].message == "a");
        CHECK(batch.entries[1].message == "b");
        CHECK(batch.next == 2);
        CHECK_FALSE(batch.gap);

        batch = store.take(1);
        REQUIRE(batch.entries.size() == 1);
        CHECK(batch.entries[0].message == "b");
        batch = store.take(2);
        CHECK(batch.entries.empty());
        CHECK_FALSE(batch.gap);
    }

    TEST_CASE("log store: keeps at most 5000 records") {
        LogStore store;
        for (std::size_t i = 0; i < LOG_STORE_RECORDS + 3; ++i) {
            put(store, dvv::LogLevel::Info, "x");
        }
        auto batch = store.take(0);
        CHECK(batch.entries.size() == LOG_STORE_RECORDS);
        CHECK(batch.entries.front().sequence == 3);
        CHECK(batch.gap);
        CHECK(store.entries().size() == LOG_STORE_RECORDS);

        batch = store.take(4000);
        CHECK(batch.entries.size() == 1003);
        CHECK_FALSE(batch.gap);
    }

    TEST_CASE("log store: counts warnings and errors") {
        LogStore store;
        for (const auto level : {dvv::LogLevel::Trace, dvv::LogLevel::Debug, dvv::LogLevel::Info,
                                 dvv::LogLevel::Warn, dvv::LogLevel::Warn, dvv::LogLevel::Error}) {
            put(store, level, "m");
        }
        CHECK(store.take(0).counts == LogCounts{.warnings = 2, .errors = 1});
    }

    TEST_CASE("log store: notifies once until take") {
        LogStore store;
        int calls = 0;
        store.setNotify([&calls] { ++calls; });
        put(store, dvv::LogLevel::Info, "a");
        put(store, dvv::LogLevel::Info, "b");
        put(store, dvv::LogLevel::Info, "c");
        CHECK(calls == 1);
        (void)store.take(0);
        put(store, dvv::LogLevel::Info, "d");
        CHECK(calls == 2);
    }

    TEST_CASE("log store: a notify set after records") {
        LogStore store;
        put(store, dvv::LogLevel::Warn, "a");
        put(store, dvv::LogLevel::Info, "b");
        int calls = 0;
        store.setNotify([&calls] { ++calls; });
        CHECK(calls == 0);
        CHECK(store.take(0).counts.warnings == 1);
        put(store, dvv::LogLevel::Info, "c");
        CHECK(calls == 1);
    }

    TEST_CASE("log store: an empty notify stops notifications") {
        LogStore store;
        int calls = 0;
        store.setNotify([&calls] { ++calls; });
        put(store, dvv::LogLevel::Info, "a");
        (void)store.take(0);
        store.setNotify({});
        put(store, dvv::LogLevel::Info, "b");
        CHECK(calls == 1);
    }

    TEST_CASE("log store: source and line") {
        LogStore store;
        const auto where = std::source_location::current();
        const dvv::LogRecord record{
            .level = dvv::LogLevel::Warn,
            .message = "hello",
            .where = where,
            .time = std::chrono::system_clock::now(),
        };
        const std::string line = dvv::formatLogRecord(record);
        store.write(record, line);
        const auto batch = store.take(0);
        REQUIRE(batch.entries.size() == 1);
        CHECK(batch.entries[0].source == std::format("test_log_store.cpp:{}", where.line()));
        CHECK(batch.entries[0].line == line);
        CHECK(batch.entries[0].level == dvv::LogLevel::Warn);
        CHECK(batch.entries[0].message == "hello");
        CHECK(batch.entries[0].time == record.time);
    }

    TEST_CASE("log store: writes from 4 threads") {
        LogStore store;
        std::atomic<int> calls{0};
        store.setNotify([&calls] { calls.fetch_add(1); });
        {
            std::vector<std::jthread> threads;
            for (int t = 0; t < 4; ++t) {
                threads.emplace_back([&store] {
                    for (int i = 0; i < 1000; ++i) {
                        put(store, dvv::LogLevel::Info, "m");
                    }
                });
            }
        }
        CHECK(store.next() == 4000);
        CHECK(calls.load() >= 1);
    }
}
