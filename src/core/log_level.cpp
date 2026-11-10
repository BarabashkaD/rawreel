#include "core/log_level.h"

#include <array>
#include <cctype>
#include <format>
#include <utility>

namespace rawreel {
namespace {

struct LevelNames {
    dvv::LogLevel level;
    std::string_view shown;
    std::string_view config;
};

constexpr std::array<LevelNames, 5> LEVELS{{
    {.level = dvv::LogLevel::Error, .shown = "Errors", .config = "error"},
    {.level = dvv::LogLevel::Warn, .shown = "Warnings", .config = "warn"},
    {.level = dvv::LogLevel::Info, .shown = "Info", .config = "info"},
    {.level = dvv::LogLevel::Debug, .shown = "Debug", .config = "debug"},
    {.level = dvv::LogLevel::Trace, .shown = "Trace", .config = "trace"},
}};

bool equalsIgnoringCase(std::string_view text, std::string_view lower) noexcept {
    if (text.size() != lower.size()) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) != lower[i]) {
            return false;
        }
    }
    return true;
}

std::string_view sourceText(LevelSource source) noexcept {
    switch (source) {
        case LevelSource::Settings: return "settings";
        case LevelSource::Default: return "default";
        case LevelSource::ThisWindow: return "this window only";
        case LevelSource::Menu: return "this run, Log pane menu";
    }
    return "settings";
}

}  // namespace

std::string_view levelShownName(dvv::LogLevel level) noexcept {
    for (const LevelNames& names : LEVELS) {
        if (names.level == level) {
            return names.shown;
        }
    }
    return "Off";
}

std::string_view levelConfigValue(dvv::LogLevel level) noexcept {
    for (const LevelNames& names : LEVELS) {
        if (names.level == level) {
            return names.config;
        }
    }
    return "off";
}

std::optional<dvv::LogLevel> parseLevel(std::string_view text) noexcept {
    for (const LevelNames& names : LEVELS) {
        if (equalsIgnoringCase(text, names.config)) {
            return names.level;
        }
    }
    return std::nullopt;
}

std::string levelStartMarker(dvv::LogLevel level, bool saved) {
    return levelMarker(level, saved ? LevelSource::Settings : LevelSource::Default);
}

std::string levelMarker(dvv::LogLevel level, LevelSource source) {
    return std::format("===== log level: {} ({}) =====", levelShownName(level), sourceText(source));
}

std::string levelChangeMarker(dvv::LogLevel from, dvv::LogLevel to, LevelSource source) {
    return std::format("===== log level changed: {} -> {} ({}) =====", levelShownName(from),
                       levelShownName(to), sourceText(source));
}

std::string logFileHeader(dvv::LogLevel level, std::string_view previous_name) {
    if (previous_name.empty()) {
        return std::format("===== log file: level {} =====", levelShownName(level));
    }
    return std::format("===== log file: level {}, continues {} =====", levelShownName(level),
                       previous_name);
}

std::string logSizeWarning(std::string_view folder_utf8, std::uintmax_t bytes) {
    return std::format("log files in '{}' use {} MiB (over 1 GiB); rawreel never deletes them",
                       folder_utf8, bytes >> 20U);
}

}  // namespace rawreel
