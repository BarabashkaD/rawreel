#pragma once

// The log level's names and the texts around it (log level and files design §2.1, §3.1, §5.2,
// §5.3): the shown and config names, the level marker lines, the log file header and the size
// warning. wx-free.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "dvv/base/log.h"

namespace rawreel {

// "Errors", "Warnings", "Info", "Debug", "Trace" (Off and anything else: "Off").
[[nodiscard]] std::string_view levelShownName(dvv::LogLevel level) noexcept;
// "error", "warn", "info", "debug", "trace": the log.level value rawreel writes.
[[nodiscard]] std::string_view levelConfigValue(dvv::LogLevel level) noexcept;
// A config value in any letter case; nullopt for anything else.
[[nodiscard]] std::optional<dvv::LogLevel> parseLevel(std::string_view text) noexcept;

// What caused a level change, as the marker names it.
enum class LevelSource : std::uint8_t { Settings, Default, ThisWindow, Menu };

// "===== log level: Info (settings) =====" ("(default)" when `saved` is false).
[[nodiscard]] std::string levelStartMarker(dvv::LogLevel level, bool saved);
// The marker after a log file open (addendum §3.1, §5.2): "===== log level: Trace (this run, Log
// pane menu) =====", naming where the active level came from.
[[nodiscard]] std::string levelMarker(dvv::LogLevel level, LevelSource source);
// "===== log level changed: Info -> Debug (settings) =====", "(default)", "(this window only)"
// or "(this run, Log pane menu)".
[[nodiscard]] std::string levelChangeMarker(dvv::LogLevel from, dvv::LogLevel to,
                                            LevelSource source);
// "===== log file: level Info =====", or with a non-empty `previous_name`
// "===== log file: level Info, continues <previous_name> =====".
[[nodiscard]] std::string logFileHeader(dvv::LogLevel level, std::string_view previous_name);

// The sum of the log files' sizes over which rawreel warns once per run (1 GiB).
inline constexpr std::uintmax_t LOG_SIZE_WARNING_BYTES = std::uintmax_t{1} << 30U;
// "log files in '<folder>' use <N> MiB (over 1 GiB); rawreel never deletes them".
[[nodiscard]] std::string logSizeWarning(std::string_view folder_utf8, std::uintmax_t bytes);

}  // namespace rawreel
