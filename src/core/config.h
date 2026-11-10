#pragma once

// The application config file (M3.1): the file that --config names, else rawreel.json next to the
// running executable (rawreel-gui, rawreel-cli) when it exists (the portable form; never for an
// executable inside a macOS app bundle), else rawreel.json in the user data folder
// (dvv::paths::userDataDir("rawreel")). A typed loader, wx-free, so the Settings window can use
// it. The keys:
//
//   { "io":  { "fileReads": "auto" },          // "auto" (default), "unbuffered" or "buffered"
//     "gui": { "showLogPane": false },
//     "log": { "file":   { "enabled": true, "folder": "" },  // "" or an absolute folder; ""
//                                                            // is the default folder
//              "system": { "enabled": false } } }
//
// A missing default file means the defaults; a missing --config file, an
// unreadable or invalid file, or a bad value, means one warning in the log and the defaults.
// Unknown keys are ignored, so a future save must merge into the original JSON (keep unknown
// keys) and report a folder it cannot write.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "dvv/base/error.h"
#include "dvv/platform/file_reader.h"

namespace rawreel {

inline constexpr std::string_view CONFIG_FILE_NAME = "rawreel.json";
// A config file is a few hundred bytes; anything larger than this is not one.
inline constexpr std::uintmax_t CONFIG_MAX_BYTES = std::uintmax_t{1} << 20U;

struct IoConfig {
    // How file sources read frames (SourceRequest::fileReads).
    dvv::platform::FileReadMode fileReads = dvv::platform::FileReadMode::Auto;
};

struct GuiConfig {
    bool showLogPane = false;
};

struct LogFileConfig {
    bool enabled = true;
    std::filesystem::path folder;  // empty: the default folder
};

struct SystemLogConfig {
    bool enabled = false;
};

struct LogConfig {
    LogFileConfig file;
    SystemLogConfig system;
};

struct Config {
    IoConfig io;
    GuiConfig gui;
    LogConfig log;
};

// "auto", "buffered", "unbuffered".
[[nodiscard]] std::string_view fileReadsName(dvv::platform::FileReadMode mode) noexcept;
[[nodiscard]] std::optional<dvv::platform::FileReadMode> parseFileReads(
    std::string_view text) noexcept;

// The JSON text as a Config; a missing key keeps its default. Errc::Format for malformed JSON, a
// top level or "io" that is not an object, and a "fileReads" that is not one of the names, e.g.
// "io.fileReads: expected \"auto\", \"unbuffered\" or \"buffered\", not \"fast\"".
[[nodiscard]] dvv::Result<Config> parseConfig(std::string_view text);

// The text of the existing file `path`, for the loader and the writer. Errc::Format "<n> bytes is
// too large" over CONFIG_MAX_BYTES; Errc::Io "it cannot be opened", "it cannot be read" or the
// size query's reason.
[[nodiscard]] dvv::Result<std::string> readConfigText(const std::filesystem::path& path);

// True for an executable inside a macOS app bundle: <name>.app/Contents/MacOS/<executable>.
[[nodiscard]] bool inAppBundle(const std::filesystem::path& executable);

// The default config file of `executable` (never the working directory): when `macos` and the
// executable is inside an app bundle (inAppBundle; read-only and signed), <user_data_dir()>/
// rawreel.json, `exists` not asked; otherwise the portable <directory of the executable>/
// rawreel.json when `exists` says it does, else <user_data_dir()>/rawreel.json. `user_data_dir`
// (dvv::paths::userDataDir("rawreel")) is called only for the user data folder, and its error is
// the result.
[[nodiscard]] dvv::Result<std::filesystem::path> defaultConfigPath(
    const std::filesystem::path& executable, bool macos,
    const std::function<dvv::Result<std::filesystem::path>()>& user_data_dir,
    const std::function<bool(const std::filesystem::path&)>& exists);
// defaultConfigPath() of the running executable (dvv::paths::executablePath) on this platform.
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
// The file --config names (UTF-8; made absolute against the working folder, and LoadedConfig::path
// is that absolute path) when given, else defaultConfigPath(). A missing --config file
// gives the defaults and one warning "config '<path>' ignored: not found; using the defaults"; an
// unknown executable path (or user data folder) gives the defaults with a warning.
[[nodiscard]] LoadedConfig loadConfig(const std::optional<std::string>& config_utf8);

}  // namespace rawreel
