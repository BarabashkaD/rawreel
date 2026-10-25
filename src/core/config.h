#pragma once

// The application config file (M3.1 U-IO2): an optional rawreel.json in the directory of the
// running executable (rawreel-gui, rawreel-cli), or the file that --config names. A typed loader,
// wx-free, so a GUI editor can use it later. The keys:
//
//   { "io": { "fileReads": "auto" } }   // "auto" (default), "unbuffered" or "buffered"
//
// A missing file next to the executable means the defaults; a missing --config file, an
// unreadable or invalid file, or a bad value, means one warning in the log and the defaults.
// Unknown keys are ignored, so a future save must merge into the original JSON (keep unknown
// keys) and report a folder it cannot write (U-IO2-config).

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "dvv/base/error.h"
#include "dvv/platform/file_reader.h"

namespace rawreel {

inline constexpr std::string_view CONFIG_FILE_NAME = "rawreel.json";

struct IoConfig {
    // How file sources read frames (SourceRequest::fileReads).
    dvv::platform::FileReadMode fileReads = dvv::platform::FileReadMode::Auto;
};

struct Config {
    IoConfig io;
};

// "auto", "buffered", "unbuffered".
[[nodiscard]] std::string_view fileReadsName(dvv::platform::FileReadMode mode) noexcept;
[[nodiscard]] std::optional<dvv::platform::FileReadMode> parseFileReads(
    std::string_view text) noexcept;

// The JSON text as a Config; a missing key keeps its default. Errc::Format for malformed JSON, a
// top level or "io" that is not an object, and a "fileReads" that is not one of the names, e.g.
// "io.fileReads: expected \"auto\", \"unbuffered\" or \"buffered\", not \"fast\"".
[[nodiscard]] dvv::Result<Config> parseConfig(std::string_view text);

// <directory of the running executable>/rawreel.json (dvv::paths::executablePath, never the
// working directory).
[[nodiscard]] dvv::Result<std::filesystem::path> defaultConfigPath();

struct LoadedConfig {
    Config config;  // the defaults unless the file was read and valid
    std::filesystem::path path;
    bool found = false;   // the file exists
    std::string warning;  // why an existing file was not used (also logged); empty otherwise
};

// Loads `path`. A missing file gives the defaults (one info line); an unreadable, too large (over
// 1 MiB) or invalid file gives the defaults and one DVV_LOG_WARN line "config '<path>' ignored:
// <reason>; using the defaults".
[[nodiscard]] LoadedConfig loadConfig(const std::filesystem::path& path);
// The file --config names (UTF-8) when given, else defaultConfigPath(). A missing --config file
// gives the defaults and one warning "config '<path>' ignored: not found; using the defaults"; an
// unknown executable path gives the defaults with a warning.
[[nodiscard]] LoadedConfig loadConfig(const std::optional<std::string>& config_utf8);

}  // namespace rawreel
