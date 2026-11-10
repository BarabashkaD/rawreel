#include "core/log_text.h"

#include <format>

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

LocalTime localTime(std::chrono::system_clock::time_point time) {
    LocalTime result;
    const auto whole = std::chrono::floor<std::chrono::seconds>(time);
    result.millis = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(time - whole).count());
    const std::time_t seconds = std::chrono::system_clock::to_time_t(whole);
#ifdef _WIN32
    localtime_s(&result.tm, &seconds);
#else
    localtime_r(&seconds, &result.tm);
#endif
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

std::string logSaveFileName(const LocalTime& time) {
    return std::format("rawreel-log-{:04}{:02}{:02}-{:02}{:02}{:02}.txt", time.tm.tm_year + 1900,
                       time.tm.tm_mon + 1, time.tm.tm_mday, time.tm.tm_hour, time.tm.tm_min,
                       time.tm.tm_sec);
}

}  // namespace rawreel
