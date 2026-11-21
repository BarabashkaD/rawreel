#include "core/log_text.h"

#include <format>
#include <optional>

#include "dvv/base/error.h"
#include "dvv/base/local_time.h"

namespace rawreel {

std::string logNoticeText(std::uint64_t errors, std::uint64_t warnings) {
    if (errors == 0 && warnings == 0) {
        return {};
    }
    std::string text;
    if (errors > 0) {
        text = std::format("{} {}", errors, errors == 1 ? "error" : "errors");
    }
    if (warnings > 0) {
        if (!text.empty()) {
            text += ", ";
        }
        text += std::format("{} {}", warnings, warnings == 1 ? "warning" : "warnings");
    }
    return text + " in the log";
}

std::tm utcBreakdown(std::chrono::sys_seconds time) noexcept {
    using namespace std::chrono;
    const sys_days day = floor<days>(time);
    const year_month_day date{day};
    const hh_mm_ss<seconds> clock{time - day};
    std::tm result{};
    result.tm_year = static_cast<int>(date.year()) - 1900;
    result.tm_mon = static_cast<int>(static_cast<unsigned>(date.month())) - 1;
    result.tm_mday = static_cast<int>(static_cast<unsigned>(date.day()));
    result.tm_hour = static_cast<int>(clock.hours().count());
    result.tm_min = static_cast<int>(clock.minutes().count());
    result.tm_sec = static_cast<int>(clock.seconds().count());
    result.tm_wday = static_cast<int>(weekday{day}.c_encoding());
    result.tm_yday = static_cast<int>((day - sys_days{date.year() / January / 1}).count());
    result.tm_isdst = 0;
    return result;
}

LocalTime localTime(std::chrono::system_clock::time_point time) {
    LocalTime result;
    const auto whole = std::chrono::floor<std::chrono::seconds>(time);
    result.millis = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(time - whole).count());
    if (const std::optional<std::tm> local = dvv::localTime(whole)) {
        result.tm = *local;
    } else {
        result.tm = utcBreakdown(whole);  // year_month_day + hh_mm_ss of the UTC time
    }
    return result;
}

std::string logRowTime(const LocalTime& time) {
    return std::format("{:02}:{:02}:{:02}.{:03}", time.tm.tm_hour, time.tm.tm_min, time.tm.tm_sec,
                       time.millis);
}

std::string logRowMessage(std::string_view message) {
    const auto newline = message.find('\n');
    if (newline == std::string_view::npos) {
        return std::string(message);
    }
    return std::string(message.substr(0, newline)) + " …";
}

std::string logSourceText(const LogEntry& entry) {
    return std::format("{}:{}", dvv::sourceFileName(entry.where), entry.where.line());
}

std::string logSaveFileName(const LocalTime& time) {
    return std::format("rawreel-log-{:04}{:02}{:02}-{:02}{:02}{:02}.txt", time.tm.tm_year + 1900,
                       time.tm.tm_mon + 1, time.tm.tm_mday, time.tm.tm_hour, time.tm.tm_min,
                       time.tm.tm_sec);
}

}  // namespace rawreel
