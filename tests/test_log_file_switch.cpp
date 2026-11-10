// The log file switch (core): folder preparation and replacing the file under concurrent writes.
#include <doctest/doctest.h>

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <random>
#include <source_location>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "core/log_file_switch.h"
#include "core/log_store.h"
#include "dvv/base/log.h"
#include "temporary_directory.h"

namespace fs = std::filesystem;
using rawreel::FolderCreation;
using rawreel::LogEntry;
using rawreel::LogFileSwitch;

namespace {

std::shared_ptr<dvv::ILogSink> openIn(const fs::path& folder) {
    auto sink = rawreel::openLogFile(folder, FolderCreation::LastOnly);
    REQUIRE_MESSAGE(sink.has_value(), (sink ? "" : sink.error().describe()));
    return std::move(*sink);
}

void writeLine(LogFileSwitch& log, std::string_view text) {
    const dvv::LogRecord record{.level = dvv::LogLevel::Info,
                                .message = text,
                                .where = std::source_location::current(),
                                .time = std::chrono::system_clock::now()};
    log.write(record, text);
}

std::vector<std::string> linesOf(const fs::path& folder) {
    std::vector<std::string> lines;
    std::ifstream in(folder / std::string(rawreel::LOG_FILE_NAME));
    for (std::string line; std::getline(in, line);) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("log file switch: prepare creates the last folder only") {
        const TemporaryDirectory tmp("rawreel-logswitch");
        REQUIRE(fs::create_directory(tmp.path() / "a"));
        CHECK(rawreel::prepareLogFolder(tmp.path() / "a" / "b", FolderCreation::LastOnly));
        CHECK(fs::is_directory(tmp.path() / "a" / "b"));
        CHECK_FALSE(rawreel::prepareLogFolder(tmp.path() / "x" / "y", FolderCreation::LastOnly));
        CHECK_FALSE(fs::exists(tmp.path() / "x"));
        CHECK(rawreel::prepareLogFolder(tmp.path() / "p" / "q", FolderCreation::WithParents));
        CHECK(fs::is_directory(tmp.path() / "p" / "q"));
    }

    TEST_CASE("log file switch: writes go to the current file") {
        const TemporaryDirectory tmp("rawreel-logswitch");
        LogFileSwitch log;
        (void)log.replace(openIn(tmp.path() / "one"));
        writeLine(log, "r1");
        (void)log.replace(openIn(tmp.path() / "two"));
        writeLine(log, "r2");
        (void)log.replace(nullptr);
        CHECK(linesOf(tmp.path() / "one") == std::vector<std::string>{"r1"});
        CHECK(linesOf(tmp.path() / "two") == std::vector<std::string>{"r2"});
    }

    TEST_CASE("log file switch: no file, no write") {
        LogFileSwitch log;
        (void)log.replace(nullptr);
        CHECK_NOTHROW(writeLine(log, "nothing"));
        log.batchEnd();
        log.flush();
    }

    TEST_CASE("log file switch: backfill") {
        const TemporaryDirectory tmp("rawreel-logswitch");
        LogFileSwitch log;
        std::vector<LogEntry> entries(2);
        entries[0].message = "m0";
        entries[0].line = "line zero";
        entries[1].message = "m1";
        entries[1].line = "line one";
        (void)log.replace(openIn(tmp.path() / "f"), entries);
        writeLine(log, "after");
        (void)log.replace(nullptr);
        CHECK(linesOf(tmp.path() / "f") ==
              std::vector<std::string>{"line zero", "line one", "after"});
    }

    TEST_CASE("log file switch: no record lost during replace") {
        const TemporaryDirectory tmp("rawreel-logswitch");
        LogFileSwitch log;
        constexpr int RECORDS = 2000;
        constexpr int FOLDERS = 20;
        (void)log.replace(openIn(tmp.path() / "f0"));
        std::atomic<bool> started{false};
        std::thread writer([&] {
            for (int i = 0; i < RECORDS; ++i) {
                writeLine(log, std::format("rec {}", i));
                if (i == 0) {
                    started = true;
                    started.notify_all();
                }
            }
        });
        started.wait(false);
        for (int i = 1; i < FOLDERS; ++i) {
            std::shared_ptr<dvv::ILogSink> old =
                log.replace(openIn(tmp.path() / std::format("f{}", i)));
            old.reset();
        }
        writer.join();
        (void)log.replace(nullptr);
        std::vector<int> seen(RECORDS, 0);
        std::size_t total = 0;
        for (int i = 0; i < FOLDERS; ++i) {
            for (const std::string& line : linesOf(tmp.path() / std::format("f{}", i))) {
                constexpr std::string_view PREFIX = "rec ";
                REQUIRE(line.starts_with(PREFIX));
                int number = -1;
                const char* const end = line.data() + line.size();
                const auto parsed = std::from_chars(line.data() + PREFIX.size(), end, number);
                REQUIRE(parsed.ec == std::errc{});
                REQUIRE(number >= 0);
                REQUIRE(number < RECORDS);
                ++seen[static_cast<std::size_t>(number)];
                ++total;
            }
        }
        CHECK(total == static_cast<std::size_t>(RECORDS));
        for (const int count : seen) {
            CHECK(count == 1);
        }
    }
}
