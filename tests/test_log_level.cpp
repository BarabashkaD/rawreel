// The log level's names and the marker, file-header and size-warning texts (wx-free).
#include <doctest/doctest.h>

#include <array>
#include <cstdint>

#include "core/log_level.h"
#include "dvv/base/log.h"

using dvv::LogLevel;
using rawreel::LevelSource;

TEST_SUITE("core") {
    TEST_CASE("log level: names round-trip and parse in any case") {
        constexpr std::array LEVELS{LogLevel::Error, LogLevel::Warn, LogLevel::Info,
                                    LogLevel::Debug, LogLevel::Trace};
        for (const LogLevel level : LEVELS) {
            CAPTURE(static_cast<int>(level));
            CHECK(rawreel::parseLevel(rawreel::levelConfigValue(level)) == level);
        }
        CHECK(rawreel::levelConfigValue(LogLevel::Error) == "error");
        CHECK(rawreel::levelConfigValue(LogLevel::Warn) == "warn");
        CHECK(rawreel::levelShownName(LogLevel::Error) == "Errors");
        CHECK(rawreel::levelShownName(LogLevel::Warn) == "Warnings");
        CHECK(rawreel::levelShownName(LogLevel::Info) == "Info");
        CHECK(rawreel::levelShownName(LogLevel::Debug) == "Debug");
        CHECK(rawreel::levelShownName(LogLevel::Trace) == "Trace");
        CHECK(rawreel::parseLevel("WARN") == LogLevel::Warn);
        CHECK(rawreel::parseLevel("Trace") == LogLevel::Trace);
        CHECK_FALSE(rawreel::parseLevel("verbose").has_value());
        CHECK_FALSE(rawreel::parseLevel("").has_value());
        CHECK_FALSE(rawreel::parseLevel("off").has_value());
    }

    TEST_CASE("log level: marker texts") {
        CHECK(rawreel::levelStartMarker(LogLevel::Info, true) ==
              "===== log level: Info (settings) =====");
        CHECK(rawreel::levelStartMarker(LogLevel::Info, false) ==
              "===== log level: Info (default) =====");
        CHECK(rawreel::levelChangeMarker(LogLevel::Info, LogLevel::Debug, LevelSource::Settings) ==
              "===== log level changed: Info -> Debug (settings) =====");
        CHECK(
            rawreel::levelChangeMarker(LogLevel::Info, LogLevel::Debug, LevelSource::ThisWindow) ==
            "===== log level changed: Info -> Debug (this window only) =====");
        CHECK(rawreel::levelChangeMarker(LogLevel::Info, LogLevel::Debug, LevelSource::Menu) ==
              "===== log level changed: Info -> Debug (this run, Log pane menu) =====");
        CHECK(rawreel::levelChangeMarker(LogLevel::Warn, LogLevel::Error, LevelSource::Default) ==
              "===== log level changed: Warnings -> Errors (default) =====");
    }

    TEST_CASE("log level: the marker after a log file open names the level's source") {
        CHECK(rawreel::levelMarker(LogLevel::Info, LevelSource::Settings) ==
              "===== log level: Info (settings) =====");
        CHECK(rawreel::levelMarker(LogLevel::Info, LevelSource::Default) ==
              "===== log level: Info (default) =====");
        CHECK(rawreel::levelMarker(LogLevel::Debug, LevelSource::ThisWindow) ==
              "===== log level: Debug (this window only) =====");
        CHECK(rawreel::levelMarker(LogLevel::Trace, LevelSource::Menu) ==
              "===== log level: Trace (this run, Log pane menu) =====");
    }

    TEST_CASE("log level: file header and size warning") {
        CHECK(rawreel::logFileHeader(LogLevel::Info, "") == "===== log file: level Info =====");
        CHECK(rawreel::logFileHeader(LogLevel::Info, "rawreel-20261110-140210.log") ==
              "===== log file: level Info, continues rawreel-20261110-140210.log =====");
        CHECK(rawreel::logSizeWarning("/x", std::uintmax_t{1536} << 20U) ==
              "log files in '/x' use 1536 MiB (over 1 GiB); rawreel never deletes them");
        CHECK(rawreel::LOG_SIZE_WARNING_BYTES == (std::uintmax_t{1} << 30U));
    }
}
