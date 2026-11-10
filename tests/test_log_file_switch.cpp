// The log file switch (core): folder preparation, timestamped files, the lines it writes and
// replacing the file under concurrent writes.
#include <doctest/doctest.h>

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
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
#include "dvv/media/types.h"
#include "temporary_directory.h"

namespace fs = std::filesystem;
using rawreel::FolderCreation;
using rawreel::LogEntry;
using rawreel::LogFileSwitch;

namespace {

std::shared_ptr<dvv::SplitFileSink> openIn(const fs::path& folder) {
    auto sink = rawreel::openLogFile(folder, FolderCreation::LastOnly, false);
    REQUIRE_MESSAGE(sink.has_value(), (sink ? "" : sink.error().describe()));
    return std::move(*sink);
}

dvv::LogRecord recordOf(std::string_view text,
                        std::source_location where = std::source_location::current()) {
    return {.level = dvv::LogLevel::Info,
            .message = text,
            .where = where,
            .time = std::chrono::system_clock::now()};
}

void writeLine(LogFileSwitch& log, std::string_view text) {
    const dvv::LogRecord record = recordOf(text);
    log.write(record, dvv::formatLogRecord(record));
}

// The files in `folder` (names only).
std::vector<std::string> namesIn(const fs::path& folder) {
    std::vector<std::string> names;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder)) {
        names.push_back(dvv::media::utf8FromPath(entry.path().filename()));
    }
    return names;
}

// The lines of the only file in `folder`.
std::vector<std::string> linesOf(const fs::path& folder) {
    const std::vector<std::string> names = namesIn(folder);
    REQUIRE(names.size() == 1);
    std::vector<std::string> lines;
    std::ifstream in(folder / names.front());
    for (std::string line; std::getline(in, line);) {
        lines.push_back(line);
    }
    return lines;
}

// The message of a line formatted without its source: the text after "INFO  ".
std::string messageOf(const std::string& line) {
    constexpr std::string_view LEVEL = " INFO  ";
    const std::size_t at = line.find(LEVEL);
    return at == std::string::npos ? line : line.substr(at + LEVEL.size());
}

std::vector<std::string> messagesOf(const fs::path& folder) {
    std::vector<std::string> messages;
    for (const std::string& line : linesOf(folder)) {
        messages.push_back(messageOf(line));
    }
    return messages;
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

    TEST_CASE("log file switch: files are timestamped") {
        const TemporaryDirectory tmp("rawreel-logswitch");
        LogFileSwitch log;
        CHECK(log.currentFile().empty());
        (void)log.replace(openIn(tmp.path() / "f"));
        writeLine(log, "r1");
        const std::vector<std::string> names = namesIn(tmp.path() / "f");
        REQUIRE(names.size() == 1);
        CHECK(rawreel::isLogFileName(names.front(), false));
        CHECK(log.currentFile() == tmp.path() / "f" / names.front());
        (void)log.replace(nullptr);
        CHECK(log.currentFile().empty());
    }

    TEST_CASE("log file switch: total bytes and splits") {
        const TemporaryDirectory tmp("rawreel-logswitch");
        LogFileSwitch log;
        CHECK(log.totalBytes() == 0);
        CHECK(log.splits() == 0);
        (void)log.replace(openIn(tmp.path() / "f"));
        writeLine(log, "r1");
        CHECK(log.totalBytes() > 0);
        CHECK(log.splits() == 0);
        // Lines of 64 KiB past LOG_FILE_BYTES start a second file.
        const std::string big(std::size_t{64} << 10U, 'x');
        const std::uintmax_t lines = rawreel::LOG_FILE_BYTES / big.size() + 1;
        for (std::uintmax_t i = 0; i < lines; ++i) {
            writeLine(log, big);
        }
        CHECK(log.splits() == 1);
        CHECK(log.totalBytes() > rawreel::LOG_FILE_BYTES);
        CHECK(namesIn(tmp.path() / "f").size() == 2);
        (void)log.replace(nullptr);
        CHECK(log.totalBytes() == 0);
        CHECK(log.splits() == 0);
    }

    TEST_CASE("log file switch: writes go to the current file") {
        const TemporaryDirectory tmp("rawreel-logswitch");
        LogFileSwitch log;
        (void)log.replace(openIn(tmp.path() / "one"));
        writeLine(log, "r1");
        (void)log.replace(openIn(tmp.path() / "two"));
        writeLine(log, "r2");
        (void)log.replace(nullptr);
        CHECK(messagesOf(tmp.path() / "one") == std::vector<std::string>{"r1"});
        CHECK(messagesOf(tmp.path() / "two") == std::vector<std::string>{"r2"});
    }

    TEST_CASE("log file switch: live records without source") {
        const TemporaryDirectory tmp("rawreel-logswitch");
        const dvv::LogRecord record = recordOf("live");
        LogFileSwitch log;
        (void)log.replace(openIn(tmp.path() / "plain"));
        log.write(record, dvv::formatLogRecord(record));
        (void)log.replace(nullptr);
        CHECK(linesOf(tmp.path() / "plain") ==
              std::vector<std::string>{dvv::formatLogRecord(record, false)});

        LogFileSwitch sourced;
        sourced.setWithSource(true);
        (void)sourced.replace(openIn(tmp.path() / "sourced"));
        sourced.write(record, "ignored");
        (void)sourced.replace(nullptr);
        CHECK(linesOf(tmp.path() / "sourced") ==
              std::vector<std::string>{dvv::formatLogRecord(record, true)});
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
        entries[1].mark = true;
        (void)log.replace(openIn(tmp.path() / "f"), entries);
        writeLine(log, "after");
        (void)log.replace(nullptr);
        const std::vector<std::string> lines = linesOf(tmp.path() / "f");
        REQUIRE(lines.size() == 3);
        CHECK(lines[0] == "line zero");
        CHECK(lines[1] == "line one");
        CHECK(messageOf(lines[2]) == "after");
    }

    TEST_CASE("log file switch: backfill keeps each line's source") {
        const TemporaryDirectory tmp("rawreel-logswitch");
        std::vector<LogEntry> entries;
        for (const std::string_view text : {"early one", "early two"}) {
            const dvv::LogRecord record = recordOf(text);
            LogEntry entry;
            entry.level = record.level;
            entry.time = record.time;
            entry.message = std::string(text);
            entry.where = record.where;
            entry.line = dvv::formatLogRecord(record, true);
            entries.push_back(std::move(entry));
        }
        LogFileSwitch log;  // without source: the backfill is written as the entries hold it
        (void)log.replace(openIn(tmp.path() / "f"), entries);
        (void)log.replace(nullptr);
        CHECK(linesOf(tmp.path() / "f") ==
              std::vector<std::string>{entries[0].line, entries[1].line});
        CHECK(entries[0].line.ends_with(
            std::format(" [test_log_file_switch.cpp:{}]", entries[0].where.line())));
    }

    TEST_CASE("log file name: pattern") {
        CHECK(rawreel::isLogFileName("rawreel-20261110-153126.log", false));
        CHECK(rawreel::isLogFileName("rawreel-20261110-153126-2.log", false));
        CHECK(rawreel::isLogFileName("rawreel-20261110-153126-1.log", false));  // as dvv-base
        CHECK(rawreel::isLogFileName("rawreel-20261110-153126-10.log", false));
        CHECK_FALSE(rawreel::isLogFileName("rawreel.log", false));
        CHECK_FALSE(rawreel::isLogFileName("rawreel-old.log", false));
        CHECK_FALSE(rawreel::isLogFileName("rawreel-20261110-1.log", false));
        CHECK_FALSE(rawreel::isLogFileName("rawreel-20261110-153126-02.log", false));
        CHECK_FALSE(rawreel::isLogFileName("rawreel-20261110-153126-.log", false));
        CHECK_FALSE(rawreel::isLogFileName("rawreel-2026111a-153126.log", false));
        CHECK_FALSE(rawreel::isLogFileName("rawreel-20261110-153126.log.1", false));
        CHECK_FALSE(rawreel::isLogFileName("xrawreel-20261110-153126.log", false));
        CHECK_FALSE(rawreel::isLogFileName("RawReel-20261110-153126.LOG", false));
        CHECK(rawreel::isLogFileName("RawReel-20261110-153126.LOG", true));
        CHECK_FALSE(rawreel::isLogFileName("rawreel-old.log", true));
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
            std::shared_ptr<dvv::SplitFileSink> old =
                log.replace(openIn(tmp.path() / std::format("f{}", i)));
            old.reset();
        }
        writer.join();
        (void)log.replace(nullptr);
        std::vector<int> seen(RECORDS, 0);
        std::size_t total = 0;
        for (int i = 0; i < FOLDERS; ++i) {
            for (const std::string& line : messagesOf(tmp.path() / std::format("f{}", i))) {
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
