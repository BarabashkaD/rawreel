#pragma once

// Text of the Log pane (Log pane design): the notice, the copied/saved text, a row's cells and the
// save file name. wx-free.

#include <chrono>
#include <concepts>
#include <cstdint>
#include <ctime>
#include <ranges>
#include <string>
#include <string_view>

#include "core/log_store.h"

namespace rawreel {

// "" | "1 warning in the log" | "2 warnings in the log" | "1 error in the log" |
// "2 errors, 3 warnings in the log"
[[nodiscard]] std::string logNoticeText(std::uint64_t errors, std::uint64_t warnings);

// Each entry's `line`, then '\n' (a line that already ends in '\n' gets none); "" for none.
template <std::ranges::input_range R>
    requires std::same_as<std::ranges::range_value_t<R>, LogEntry>
[[nodiscard]] std::string logRecordsText(const R& entries) {
    std::string text;
    for (const LogEntry& entry : entries) {
        text += entry.line;
        if (entry.line.empty() || entry.line.back() != '\n') {
            text += '\n';
        }
    }
    return text;
}

struct LocalTime {
    std::tm tm{};
    int millis = 0;
};

[[nodiscard]] LocalTime localTime(std::chrono::system_clock::time_point time);  // localtime_r/_s
[[nodiscard]] std::string logRowTime(const LocalTime& time);                    // "HH:MM:SS.mmm"
[[nodiscard]] std::string logRowMessage(std::string_view message);  // first line + " …" if more
[[nodiscard]] std::string logSaveFileName(
    const LocalTime& time);  // "rawreel-log-YYYYMMDD-HHMMSS.txt"

}  // namespace rawreel
