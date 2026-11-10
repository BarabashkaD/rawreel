#pragma once

// The records the Log pane shows (Log pane design): a bounded, thread-safe copy of what the Logger
// delivered. wx-free. LogStore is a Logger sink: write() runs under the Logger's lock on any thread
// and neither logs nor calls back into the Logger. The notify function is called under the store's
// own lock, so it must only wake the consumer (no take(), no logging).

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

#include "dvv/base/log.h"

namespace rawreel {

inline constexpr std::size_t LOG_STORE_RECORDS = 5000;

struct LogEntry {
    std::uint64_t sequence = 0;  // from 0, in write order, for the whole run
    dvv::LogLevel level = dvv::LogLevel::Info;
    std::chrono::system_clock::time_point time;
    std::string message;
    std::source_location where;  // the record's call site (its strings are static)
    bool mark = false;           // a marker line (Logger::mark), never counted
    // formatLogRecord(record, with_source) with the store's flag: what copy, save and the log
    // file's backfill use.
    std::string line;
};

struct LogCounts {
    std::uint64_t warnings = 0;
    std::uint64_t errors = 0;
    bool operator==(const LogCounts&) const = default;
};

struct LogBatch {
    std::vector<LogEntry> entries;  // sequence >= since, oldest first
    LogCounts counts;               // totals so far
    bool gap = false;               // records from `since` on were already dropped
    std::uint64_t next = 0;         // the sequence the next record will get
};

class LogStore final : public dvv::ILogSink {
public:
    // Formats each record itself (the Logger's `line` is not kept); a mark is not counted.
    void write(const dvv::LogRecord& record, std::string_view line) override;
    // Whether the lines keep " [file.cpp:42]" (log.source); false by default. Set at start,
    // before any record.
    void setWithSource(bool with_source) noexcept;
    // Called after the first record that follows a take() (or the call itself), until the next
    // take(). Empty: none.
    void setNotify(std::function<void()> notify);
    // The records with sequence >= since; also clears pending, in one lock hold.
    [[nodiscard]] LogBatch take(std::uint64_t since);
    [[nodiscard]] std::vector<LogEntry> entries() const;  // every kept record
    [[nodiscard]] std::uint64_t next() const;

private:
    mutable std::mutex mutex_;
    std::deque<LogEntry> entries_;
    std::uint64_t next_ = 0;
    LogCounts counts_;
    bool pending_ = false;  // true only after notify_ was called, until take()
    std::function<void()> notify_;
    std::atomic<bool> with_source_{false};
};

}  // namespace rawreel
