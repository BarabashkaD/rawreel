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
//              "system": { "enabled": false },
//              "level": "info",                        // error, warn, info, debug or trace
//              "source": false } }
//   Also under "gui": "recentSize" (1 to 10, default 5) and "recent" (an array of strings, newest
//   first), the Open Recent history. parseConfig does not read them; core/recent.h's parseRecent
//   does, from LoadedConfig::text, and a bad value there is a warning for the history only.
//   Also under "gui", the rulers and the grid (the defaults shown; a level's object holds the
//   same eight keys as "major"):
//     "rulers": { "show": false, "numbers": true, "cursorColour": "#ff3b30" },
//     "grid": { "major": { "show": false, "step": 32, "opacity": 85, "colour": "#ffffff",
//                          "thickness": 2, "adaptive": false, "halo": false,
//                          "minimumSpacing": 2 },
//               "minor": { "show": true, "step": 8, "opacity": 50, "thickness": 1,
//                          "minimumSpacing": 4, ... },
//               "secondMinor": { "show": false, "step": 2, "opacity": 25, "thickness": 1,
//                                "minimumSpacing": 3, ... } }
//   "major.show" is the grid's on/off; steps are image pixels, one of GRID_STEP_VALUES; opacity
//   (%), thickness and minimumSpacing (pt) lie in the GRID_*_RANGE constants of core/overlay.h.
//   parseConfig does not read them either; parseOverlayConfig does, and a bad value there is a
//   warning for that value only.
//
// A missing default file means the defaults; a missing --config file, an
// unreadable or invalid file, or a bad value, means one warning in the log and the defaults.
// Unknown keys are ignored, so a future save must merge into the original JSON (keep unknown
// keys) and report a folder it cannot write.

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/config_writer.h"
#include "core/overlay.h"
#include "dvv/base/error.h"
#include "dvv/base/log.h"
#include "dvv/media/source_factory.h"
#include "dvv/platform/file_reader.h"

namespace rawreel {

inline constexpr std::string_view CONFIG_FILE_NAME = "rawreel.json";
// A config file is a few hundred bytes; anything larger than this is not one.
inline constexpr std::uintmax_t CONFIG_MAX_BYTES = std::uintmax_t{1} << 20U;

struct IoConfig {
    // How file sources read frames (SourceRequest::fileReads).
    dvv::platform::FileReadMode fileReads = dvv::media::DEFAULT_FILE_READS;
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
    dvv::LogLevel level = dvv::LogLevel::Info;
    bool levelSaved = false;  // log.level is present in the file
    bool source = false;      // show the source file and line in each log line (JSON only)
};

struct Config {
    IoConfig io;
    GuiConfig gui;
    LogConfig log;
};

// The dotted keys of rawreel.json: what the GUI writes (saveConfigValues) and what the reader
// names in its messages.
namespace config_key {
inline constexpr std::string_view IO_FILE_READS = "io.fileReads";
inline constexpr std::string_view GUI_SHOW_LOG_PANE = "gui.showLogPane";
inline constexpr std::string_view GUI_RECENT = "gui.recent";
inline constexpr std::string_view GUI_RECENT_SIZE = "gui.recentSize";
inline constexpr std::string_view LOG_LEVEL = "log.level";
inline constexpr std::string_view LOG_SOURCE = "log.source";
inline constexpr std::string_view LOG_FILE_ENABLED = "log.file.enabled";
inline constexpr std::string_view LOG_FILE_FOLDER = "log.file.folder";
inline constexpr std::string_view LOG_SYSTEM_ENABLED = "log.system.enabled";
inline constexpr std::string_view GUI_RULERS_SHOW = "gui.rulers.show";
inline constexpr std::string_view GUI_RULERS_NUMBERS = "gui.rulers.numbers";
inline constexpr std::string_view GUI_RULERS_CURSOR_COLOUR = "gui.rulers.cursorColour";
// The keys of one grid level, in the order they are read and written.
struct GridLevelKeys {
    std::string_view show, step, opacity, colour, thickness, adaptive, halo, minimumSpacing;
};
// Indexed by GridLevel.
inline constexpr std::array<GridLevelKeys, GRID_LEVELS> GUI_GRID{{
    {"gui.grid.major.show", "gui.grid.major.step", "gui.grid.major.opacity",
     "gui.grid.major.colour", "gui.grid.major.thickness", "gui.grid.major.adaptive",
     "gui.grid.major.halo", "gui.grid.major.minimumSpacing"},
    {"gui.grid.minor.show", "gui.grid.minor.step", "gui.grid.minor.opacity",
     "gui.grid.minor.colour", "gui.grid.minor.thickness", "gui.grid.minor.adaptive",
     "gui.grid.minor.halo", "gui.grid.minor.minimumSpacing"},
    {"gui.grid.secondMinor.show", "gui.grid.secondMinor.step", "gui.grid.secondMinor.opacity",
     "gui.grid.secondMinor.colour", "gui.grid.secondMinor.thickness",
     "gui.grid.secondMinor.adaptive", "gui.grid.secondMinor.halo",
     "gui.grid.secondMinor.minimumSpacing"},
}};
}  // namespace config_key

// The grid and ruler settings of a config file.
struct OverlayConfig {
    GridConfig grid;
    RulerConfig rulers;
    bool gridSavable = true;    // gui.grid and each of its levels are objects, null or missing
    bool rulersSavable = true;  // gui.rulers is an object, null or missing
    std::vector<std::string> warnings;  // without the "config '<path>': " prefix
};

// gui.grid and gui.rulers of a config text (like parseRecent: never an error). Leaves are read
// level by level in GridLevelKeys order, then the rulers. A bad leaf (a wrong type, a number out
// of range or not whole, a colour that is not "#rrggbb" in either letter case) falls back to its
// default alone with "<key>: expected <what>, not <json>; using <default>". A step set that breaks
// stepsValid gives "gui.grid: the steps <a> / <b> / <c> break <GRID_STEP_RULE>; using 32 / 8 / 2"
// and the default steps of all three levels. A parent (gui.grid, a level, gui.rulers) that is not
// an object gives "<key>: expected an object, not <json>; the defaults are used and not saved",
// its part's defaults, and clears the matching *Savable flag; a null parent warns "...; the
// defaults are used" and stays savable (the writer replaces null by an object). Text that is not
// a JSON object, or a "gui" that is not one: the defaults, no warning (parseConfig rejects it).
[[nodiscard]] OverlayConfig parseOverlayConfig(std::string_view text);
// Every key of the grid (when `grid_keys`; the levels in order, each in GridLevelKeys order) and
// then of the rulers (when `ruler_keys`) with its value; colours as "#rrggbb" in lower case;
// numbers as std::int64_t.
[[nodiscard]] std::vector<ConfigValue> overlayConfigValues(const GridConfig& grid,
                                                           const RulerConfig& rulers,
                                                           bool grid_keys, bool ruler_keys);

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
    // const std::function&: called during the call only (std::function_ref is C++26).
    const std::function<dvv::Result<std::filesystem::path>()>& user_data_dir,
    const std::function<bool(const std::filesystem::path&)>& exists);
// defaultConfigPath() of the running executable (dvv::paths::executablePath) on this platform.
[[nodiscard]] dvv::Result<std::filesystem::path> defaultConfigPath();

struct LoadedConfig {
    Config config;  // the defaults unless the file was read and valid
    std::filesystem::path path;
    bool found = false;   // the file exists
    std::string warning;  // why an existing file was not used (also logged); empty otherwise
    std::string text;     // the file's text when the config was used; empty otherwise
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
