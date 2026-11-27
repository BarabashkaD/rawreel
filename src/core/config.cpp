#include "core/config.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/config_writer.h"
#include "core/inspector.h"
#include "core/layout.h"
#include "core/log_level.h"
#include "core/overlay.h"
#include "dvv/base/error.h"
#include "dvv/base/json.h"
#include "dvv/base/log.h"
#include "dvv/base/paths.h"
#include "dvv/base/utf8.h"
#include "dvv/platform/file_reader.h"

namespace rawreel {

namespace {

using dvv::platform::FileReadMode;

constexpr std::array<std::pair<std::string_view, FileReadMode>, 3> FILE_READS{{
    {"auto", FileReadMode::Auto},
    {"unbuffered", FileReadMode::Unbuffered},
    {"buffered", FileReadMode::Buffered},
}};

LoadedConfig ignored(LoadedConfig loaded, std::string reason) {
    loaded.config = Config{};
    loaded.warning = std::move(reason);
    DVV_LOG_WARN("config '{}' ignored: {}; using the defaults", dvv::pathDisplayText(loaded.path),
                 loaded.warning);
    return loaded;
}

// The object at `section[key]`, nullptr when absent; an error when present but not an object.
// `name` is the dotted name of the result, for messages.
dvv::Result<const dvv::json::Value*> objectAt(const dvv::json::Value& section, std::string_view key,
                                              std::string_view name) {
    const auto found = section.find(key);
    if (found == section.end()) {
        return nullptr;
    }
    if (!found->is_object()) {
        return dvv::makeError(dvv::Errc::Format, std::format("{}: expected an object", name));
    }
    return &*found;
}

// Sets `target` from section[key] when present; it must be true or false.
dvv::Result<void> readBool(const dvv::json::Value& section, std::string_view key,
                           std::string_view name, bool& target) {
    const auto found = section.find(key);
    if (found == section.end()) {
        return {};
    }
    if (!found->is_boolean()) {
        return dvv::makeError(dvv::Errc::Format, std::format("{}: expected true or false, not {}",
                                                             name, dvv::json::dump(*found)));
    }
    target = found->get<bool>();
    return {};
}

// "" (the default folder) or an absolute folder.
dvv::Result<void> readFolder(const dvv::json::Value& section, std::string_view key,
                             std::string_view name, std::filesystem::path& target) {
    const auto found = section.find(key);
    if (found == section.end()) {
        return {};
    }
    std::filesystem::path folder;
    if (found->is_string()) {
        folder = dvv::pathFromText(found->get_ref<const std::string&>());
    }
    if (!found->is_string() || (!folder.empty() && !folder.is_absolute())) {
        return dvv::makeError(dvv::Errc::Format,
                              std::format(R"({}: expected an absolute folder or "", not {})", name,
                                          dvv::json::dump(*found)));
    }
    target = std::move(folder);
    return {};
}

// "error", "warn", "info", "debug" or "trace", in any letter case.
dvv::Result<void> readLevel(const dvv::json::Value& log, LogConfig& target) {
    const auto found = log.find("level");
    if (found == log.end()) {
        return {};
    }
    const std::optional<dvv::LogLevel> level =
        found->is_string() ? parseLevel(found->get_ref<const std::string&>()) : std::nullopt;
    if (!level) {
        return dvv::makeError(
            dvv::Errc::Format,
            std::format(R"({}: expected "error", "warn", "info", "debug" or "trace", not {})",
                        config_key::LOG_LEVEL, dvv::json::dump(*found)));
    }
    target.level = *level;
    target.levelSaved = true;
    return {};
}

dvv::Result<void> readFileReads(const dvv::json::Value& io, IoConfig& target) {
    if (const auto reads = io.find("fileReads"); reads != io.end()) {
        const std::optional<FileReadMode> mode =
            reads->is_string() ? parseFileReads(reads->get_ref<const std::string&>())
                               : std::nullopt;
        if (!mode) {
            return dvv::makeError(
                dvv::Errc::Format,
                std::format(R"({}: expected "auto", "unbuffered" or "buffered", not {})",
                            config_key::IO_FILE_READS, dvv::json::dump(*reads)));
        }
        target.fileReads = *mode;
    }
    return {};
}

// The last part of a dotted key, its name in the parent object.
std::string_view memberName(std::string_view key) {
    return key.substr(key.rfind('.') + 1);
}

// A dotted key without its last part.
std::string_view parentKey(std::string_view key) {
    return key.substr(0, key.rfind('.'));
}

// A JSON integer from `low` to `high`; nullopt for anything else (2.0 too).
std::optional<std::uint32_t> wholeNumber(const dvv::json::Value& value, std::uint32_t low,
                                         std::uint32_t high) {
    if (!value.is_number_integer()) {
        return std::nullopt;
    }
    std::uint64_t number = 0;
    if (value.is_number_unsigned()) {
        number = value.get<std::uint64_t>();
    } else if (const auto signed_number = value.get<std::int64_t>(); signed_number >= 0) {
        number = static_cast<std::uint64_t>(signed_number);
    } else {
        return std::nullopt;
    }
    if (number < low || number > high) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(number);
}

// "#rrggbb" in either letter case.
std::optional<Rgb8> parseColour(std::string_view text) noexcept {
    constexpr std::size_t LENGTH = 7;
    constexpr int HEX = 16;
    if (text.size() != LENGTH || text[0] != '#') {
        return std::nullopt;
    }
    std::array<std::uint8_t, 3> parts{};
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const char* const first = text.data() + 1 + (2 * i);
        const char* const last = first + 2;
        const auto [end, error] = std::from_chars(first, last, parts[i], HEX);
        if (error != std::errc{} || end != last) {
            return std::nullopt;
        }
    }
    return Rgb8{.r = parts[0], .g = parts[1], .b = parts[2]};
}

std::string colourText(Rgb8 colour) {
    return std::format("#{:02x}{:02x}{:02x}", colour.r, colour.g, colour.b);
}

// The last value's name in the config file: "pixel" or "edge".
std::string_view lastValueName(LastValue value) noexcept {
    return value == LastValue::Edge ? "edge" : "pixel";
}

// "pixel" or "edge", in lower case only.
std::optional<LastValue> parseLastValue(std::string_view text) noexcept {
    for (const LastValue value : {LastValue::Pixel, LastValue::Edge}) {
        if (text == lastValueName(value)) {
            return value;
        }
    }
    return std::nullopt;
}

// The overlay leaf readers: the value at parent[memberName(key)], or `fallback` when it is
// missing; a bad value gives `fallback` and one warning.
class OverlayReader {
public:
    explicit OverlayReader(std::vector<std::string>& warnings) noexcept : warnings_(warnings) {}

    bool flag(const dvv::json::Value& parent, std::string_view key, bool fallback) {
        const auto found = parent.find(memberName(key));
        if (found == parent.end()) {
            return fallback;
        }
        if (found->is_boolean()) {
            return found->get<bool>();
        }
        warn(key, "true or false", *found, std::format("{}", fallback));
        return fallback;
    }

    std::uint32_t number(const dvv::json::Value& parent, std::string_view key, GridRange range,
                         std::uint32_t fallback) {
        const auto found = parent.find(memberName(key));
        if (found == parent.end()) {
            return fallback;
        }
        if (const std::optional<std::uint32_t> value =
                wholeNumber(*found, range.minimum, range.maximum)) {
            return *value;
        }
        warn(key, std::format("a whole number from {} to {}", range.minimum, range.maximum), *found,
             std::format("{}", fallback));
        return fallback;
    }

    std::uint32_t step(const dvv::json::Value& parent, std::string_view key,
                       std::uint32_t fallback) {
        const auto found = parent.find(memberName(key));
        if (found == parent.end()) {
            return fallback;
        }
        const std::optional<std::uint32_t> value =
            wholeNumber(*found, GRID_STEP_VALUES.front(), GRID_STEP_VALUES.back());
        if (value && std::ranges::contains(GRID_STEP_VALUES, *value)) {
            return *value;
        }
        std::string values;
        for (const std::uint32_t step_value : GRID_STEP_VALUES) {
            values += std::format("{}{}", values.empty() ? "" : ", ", step_value);
        }
        warn(key, "one of " + values, *found, std::format("{}", fallback));
        return fallback;
    }

    Rgb8 colour(const dvv::json::Value& parent, std::string_view key, Rgb8 fallback) {
        const auto found = parent.find(memberName(key));
        if (found == parent.end()) {
            return fallback;
        }
        if (found->is_string()) {
            if (const std::optional<Rgb8> value =
                    parseColour(found->get_ref<const std::string&>())) {
                return *value;
            }
        }
        warn(key, R"("#rrggbb")", *found, std::format(R"("{}")", colourText(fallback)));
        return fallback;
    }

    std::string text(const dvv::json::Value& parent, std::string_view key,
                     std::string_view fallback) {
        const auto found = parent.find(memberName(key));
        if (found == parent.end()) {
            return std::string(fallback);
        }
        if (found->is_string()) {
            return found->get<std::string>();
        }
        warn(key, "a string", *found, std::format(R"("{}")", fallback));
        return std::string(fallback);
    }

    // 0, or a whole number in `range`.
    std::uint32_t zeroOrNumber(const dvv::json::Value& parent, std::string_view key,
                               GridRange range, std::uint32_t fallback) {
        const auto found = parent.find(memberName(key));
        if (found == parent.end()) {
            return fallback;
        }
        if (const std::optional<std::uint32_t> value = wholeNumber(*found, 0, range.maximum);
            value && (*value == 0 || *value >= range.minimum)) {
            return *value;
        }
        warn(key, std::format("0 or a whole number from {} to {}", range.minimum, range.maximum),
             *found, std::format("{}", fallback));
        return fallback;
    }

    LastValue lastValue(const dvv::json::Value& parent, std::string_view key, LastValue fallback) {
        const auto found = parent.find(memberName(key));
        if (found == parent.end()) {
            return fallback;
        }
        if (found->is_string()) {
            if (const std::optional<LastValue> value =
                    parseLastValue(found->get_ref<const std::string&>())) {
                return *value;
            }
        }
        warn(key, R"("pixel" or "edge")", *found, std::format(R"("{}")", lastValueName(fallback)));
        return fallback;
    }

    // The object at parent[memberName(key)]; nullptr when it is missing or not an object (with a
    // warning). Anything but an object or null clears `savable`: the writer cannot merge into it.
    const dvv::json::Value* object(const dvv::json::Value& parent, std::string_view key,
                                   bool& savable) {
        const auto found = parent.find(memberName(key));
        if (found == parent.end()) {
            return nullptr;
        }
        if (found->is_object()) {
            return &*found;
        }
        if (found->is_null()) {
            warnings_.push_back(
                std::format("{}: expected an object, not null; the defaults are used", key));
        } else {
            warnings_.push_back(
                std::format("{}: expected an object, not {}; the defaults are used and not saved",
                            key, dvv::json::dump(*found)));
            savable = false;
        }
        return nullptr;
    }

private:
    void warn(std::string_view key, std::string_view expected, const dvv::json::Value& value,
              std::string_view fallback) {
        warnings_.push_back(std::format("{}: expected {}, not {}; using {}", key, expected,
                                        dvv::json::dump(value), fallback));
    }

    std::vector<std::string>& warnings_;
};

GridLevelConfig readGridLevel(OverlayReader& read, const dvv::json::Value& level,
                              const config_key::GridLevelKeys& keys,
                              const GridLevelConfig& fallback) {
    GridLevelConfig result;
    result.show = read.flag(level, keys.show, fallback.show);
    result.step = read.step(level, keys.step, fallback.step);
    result.opacity = read.number(level, keys.opacity, GRID_OPACITY_RANGE, fallback.opacity);
    result.colour = read.colour(level, keys.colour, fallback.colour);
    result.thickness = read.number(level, keys.thickness, GRID_THICKNESS_RANGE, fallback.thickness);
    result.adaptive = read.flag(level, keys.adaptive, fallback.adaptive);
    result.halo = read.flag(level, keys.halo, fallback.halo);
    result.minimumSpacing = read.number(level, keys.minimumSpacing, GRID_MINIMUM_SPACING_RANGE,
                                        fallback.minimumSpacing);
    return result;
}

void readGrid(OverlayReader& read, const dvv::json::Value& gui, OverlayConfig& result) {
    const std::string_view grid_key = parentKey(parentKey(config_key::GUI_GRID[0].show));
    const dvv::json::Value* const grid = read.object(gui, grid_key, result.gridSavable);
    if (grid == nullptr) {
        return;
    }
    const GridConfig defaults;
    for (std::size_t i = 0; i < GRID_LEVELS; ++i) {
        const config_key::GridLevelKeys& keys = config_key::GUI_GRID[i];
        if (const dvv::json::Value* const level =
                read.object(*grid, parentKey(keys.show), result.gridSavable)) {
            result.grid.levels[i] = readGridLevel(read, *level, keys, defaults.levels[i]);
        }
    }
    std::array<std::uint32_t, GRID_LEVELS> steps{};
    std::ranges::transform(result.grid.levels, steps.begin(),
                           [](const GridLevelConfig& level) { return level.step; });
    if (stepsValid(steps)) {
        return;
    }
    result.warnings.push_back(std::format("{}: the steps {} / {} / {} break {}; using {} / {} / {}",
                                          grid_key, steps[0], steps[1], steps[2], GRID_STEP_RULE,
                                          defaults.levels[0].step, defaults.levels[1].step,
                                          defaults.levels[2].step));
    for (std::size_t i = 0; i < GRID_LEVELS; ++i) {
        result.grid.levels[i].step = defaults.levels[i].step;
    }
}

}  // namespace

std::string_view fileReadsName(FileReadMode mode) noexcept {
    for (const auto& [name, value] : FILE_READS) {
        if (value == mode) {
            return name;
        }
    }
    return "auto";
}

std::optional<FileReadMode> parseFileReads(std::string_view text) noexcept {
    for (const auto& [name, value] : FILE_READS) {
        if (name == text) {
            return value;
        }
    }
    return std::nullopt;
}

dvv::Result<Config> parseConfig(std::string_view text) {
    DVV_TRY(const dvv::json::Value root, dvv::json::parse(text));
    if (!root.is_object()) {
        return dvv::makeError(dvv::Errc::Format, "the top level is not a JSON object");
    }
    Config config;
    DVV_TRY(const dvv::json::Value* const io, objectAt(root, "io", "io"));
    if (io != nullptr) {
        DVV_TRY_VOID(readFileReads(*io, config.io));
    }
    DVV_TRY(const dvv::json::Value* const gui, objectAt(root, "gui", "gui"));
    if (gui != nullptr) {
        DVV_TRY_VOID(
            readBool(*gui, "showLogPane", config_key::GUI_SHOW_LOG_PANE, config.gui.showLogPane));
    }
    DVV_TRY(const dvv::json::Value* const log, objectAt(root, "log", "log"));
    if (log != nullptr) {
        DVV_TRY(const dvv::json::Value* const file, objectAt(*log, "file", "log.file"));
        if (file != nullptr) {
            DVV_TRY_VOID(
                readBool(*file, "enabled", config_key::LOG_FILE_ENABLED, config.log.file.enabled));
            DVV_TRY_VOID(
                readFolder(*file, "folder", config_key::LOG_FILE_FOLDER, config.log.file.folder));
        }
        DVV_TRY_VOID(readLevel(*log, config.log));
        DVV_TRY_VOID(readBool(*log, "source", config_key::LOG_SOURCE, config.log.source));
        DVV_TRY(const dvv::json::Value* const system, objectAt(*log, "system", "log.system"));
        if (system != nullptr) {
            DVV_TRY_VOID(readBool(*system, "enabled", config_key::LOG_SYSTEM_ENABLED,
                                  config.log.system.enabled));
        }
    }
    return config;
}

OverlayConfig parseOverlayConfig(std::string_view text) {
    OverlayConfig result;
    const dvv::Result<dvv::json::Value> root = dvv::json::parse(text);
    if (!root || !root->is_object()) {
        return result;
    }
    const auto gui = root->find("gui");
    if (gui == root->end() || !gui->is_object()) {
        return result;
    }
    OverlayReader read(result.warnings);
    readGrid(read, *gui, result);
    if (const dvv::json::Value* const rulers =
            read.object(*gui, parentKey(config_key::GUI_RULERS_SHOW), result.rulersSavable)) {
        const RulerConfig defaults;
        result.rulers.show = read.flag(*rulers, config_key::GUI_RULERS_SHOW, defaults.show);
        result.rulers.numbers =
            read.flag(*rulers, config_key::GUI_RULERS_NUMBERS, defaults.numbers);
        result.rulers.cursorColour =
            read.colour(*rulers, config_key::GUI_RULERS_CURSOR_COLOUR, defaults.cursorColour);
        if (const dvv::json::Value* const font = read.object(
                *rulers, parentKey(config_key::GUI_RULERS_FONT_FAMILY), result.rulersSavable)) {
            result.rulers.font.family =
                read.text(*font, config_key::GUI_RULERS_FONT_FAMILY, defaults.font.family);
            result.rulers.font.size = read.zeroOrNumber(*font, config_key::GUI_RULERS_FONT_SIZE,
                                                        RULER_FONT_SIZE_RANGE, defaults.font.size);
        }
        result.rulers.lastValue =
            read.lastValue(*rulers, config_key::GUI_RULERS_LAST_VALUE, defaults.lastValue);
    }
    return result;
}

namespace {

std::string_view layoutName(InspectorLayout layout) noexcept {
    switch (layout) {
        case InspectorLayout::Structure: return "structure";
        case InspectorLayout::Planes: return "planes";
    }
    return "structure";
}

std::string_view modeName(InspectorMode mode) noexcept {
    switch (mode) {
        case InspectorMode::Follow: return "follow";
        case InspectorMode::Cell: return "cell";
    }
    return "follow";
}

// A JSON integer that fits an int; nullopt for anything else.
std::optional<int> wholeInt(const dvv::json::Value& value) {
    if (!value.is_number_integer()) {
        return std::nullopt;
    }
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        return number <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())
                   ? std::optional<int>(static_cast<int>(number))
                   : std::nullopt;
    }
    const auto number = value.get<std::int64_t>();
    return number >= std::numeric_limits<int>::min() ? std::optional<int>(static_cast<int>(number))
                                                     : std::nullopt;
}

// A string leaf that is one of two names.
template <typename Enum>
Enum readName(const dvv::json::Value& parent, std::string_view key, std::string_view first_name,
              Enum first, std::string_view second_name, Enum second, Enum fallback,
              std::vector<std::string>& warnings) {
    const auto found = parent.find(memberName(key));
    if (found == parent.end()) {
        return fallback;
    }
    if (found->is_string()) {
        const std::string& text = found->get_ref<const std::string&>();
        if (text == first_name) {
            return first;
        }
        if (text == second_name) {
            return second;
        }
    }
    warnings.push_back(std::format(R"({}: expected "{}" or "{}", not {}; using "{}")", key,
                                   first_name, second_name, dvv::json::dump(*found),
                                   fallback == first ? first_name : second_name));
    return fallback;
}

}  // namespace

InspectorConfig parseInspectorConfig(std::string_view text) {
    InspectorConfig result;
    const dvv::Result<dvv::json::Value> root = dvv::json::parse(text);
    if (!root || !root->is_object()) {
        return result;
    }
    const auto gui = root->find("gui");
    if (gui == root->end() || !gui->is_object()) {
        return result;
    }
    OverlayReader read(result.warnings);
    const InspectorConfig defaults;
    result.paddingCheck = read.flag(*gui, config_key::GUI_PADDING_CHECK, defaults.paddingCheck);
    const dvv::json::Value* const zoom =
        read.object(*gui, parentKey(config_key::GUI_ZOOM_WINDOW_X), result.zoomSavable);
    if (zoom == nullptr) {
        return result;
    }
    std::optional<int> x;
    std::optional<int> y;
    for (const auto& [key, target] : {std::pair{config_key::GUI_ZOOM_WINDOW_X, &x},
                                      std::pair{config_key::GUI_ZOOM_WINDOW_Y, &y}}) {
        if (const auto found = zoom->find(memberName(key)); found != zoom->end()) {
            *target = wholeInt(*found);
            if (!target->has_value()) {
                result.warnings.push_back(
                    std::format("{}: expected a whole number, not {}; using no place", key,
                                dvv::json::dump(*found)));
            }
        }
    }
    if (x && y) {
        result.zoomPlace = ScreenRect{.x = *x, .y = *y, .width = 0, .height = 0};
    } else if (x.has_value() != y.has_value()) {
        result.warnings.push_back(std::format("{}: needs both x and y; using no place",
                                              parentKey(config_key::GUI_ZOOM_WINDOW_X)));
    }
    const GridRange size_range{static_cast<std::uint32_t>(ZOOM_WINDOW_SIZE_MIN),
                               static_cast<std::uint32_t>(ZOOM_WINDOW_SIZE_MAX)};
    result.zoomSizeDip.width =
        static_cast<int>(read.number(*zoom, config_key::GUI_ZOOM_WINDOW_WIDTH, size_range,
                                     static_cast<std::uint32_t>(defaults.zoomSizeDip.width)));
    result.zoomSizeDip.height =
        static_cast<int>(read.number(*zoom, config_key::GUI_ZOOM_WINDOW_HEIGHT, size_range,
                                     static_cast<std::uint32_t>(defaults.zoomSizeDip.height)));
    result.layout =
        readName(*zoom, config_key::GUI_ZOOM_WINDOW_LAYOUT, "structure", InspectorLayout::Structure,
                 "planes", InspectorLayout::Planes, defaults.layout, result.warnings);
    result.mode = readName(*zoom, config_key::GUI_ZOOM_WINDOW_MODE, "follow", InspectorMode::Follow,
                           "cell", InspectorMode::Cell, defaults.mode, result.warnings);
    return result;
}

std::vector<ConfigValue> inspectorConfigValues(const InspectorConfig& config) {
    std::vector<ConfigValue> values;
    values.push_back({.key = config_key::GUI_PADDING_CHECK, .value = config.paddingCheck});
    if (config.zoomPlace) {
        values.push_back(
            {.key = config_key::GUI_ZOOM_WINDOW_X, .value = std::int64_t{config.zoomPlace->x}});
        values.push_back(
            {.key = config_key::GUI_ZOOM_WINDOW_Y, .value = std::int64_t{config.zoomPlace->y}});
    }
    values.push_back({.key = config_key::GUI_ZOOM_WINDOW_WIDTH,
                      .value = std::int64_t{config.zoomSizeDip.width}});
    values.push_back({.key = config_key::GUI_ZOOM_WINDOW_HEIGHT,
                      .value = std::int64_t{config.zoomSizeDip.height}});
    values.push_back({.key = config_key::GUI_ZOOM_WINDOW_LAYOUT,
                      .value = std::string(layoutName(config.layout))});
    values.push_back(
        {.key = config_key::GUI_ZOOM_WINDOW_MODE, .value = std::string(modeName(config.mode))});
    return values;
}

std::vector<ConfigValue> overlayConfigValues(const GridConfig& grid, const RulerConfig& rulers,
                                             bool grid_keys, bool ruler_keys) {
    std::vector<ConfigValue> values;
    if (grid_keys) {
        for (std::size_t i = 0; i < GRID_LEVELS; ++i) {
            const config_key::GridLevelKeys& keys = config_key::GUI_GRID[i];
            const GridLevelConfig& level = grid.levels[i];
            values.push_back({.key = keys.show, .value = level.show});
            values.push_back({.key = keys.step, .value = std::int64_t{level.step}});
            values.push_back({.key = keys.opacity, .value = std::int64_t{level.opacity}});
            values.push_back({.key = keys.colour, .value = colourText(level.colour)});
            values.push_back({.key = keys.thickness, .value = std::int64_t{level.thickness}});
            values.push_back({.key = keys.adaptive, .value = level.adaptive});
            values.push_back({.key = keys.halo, .value = level.halo});
            values.push_back(
                {.key = keys.minimumSpacing, .value = std::int64_t{level.minimumSpacing}});
        }
    }
    if (ruler_keys) {
        values.push_back({.key = config_key::GUI_RULERS_SHOW, .value = rulers.show});
        values.push_back({.key = config_key::GUI_RULERS_NUMBERS, .value = rulers.numbers});
        values.push_back({.key = config_key::GUI_RULERS_CURSOR_COLOUR,
                          .value = colourText(rulers.cursorColour)});
        values.push_back({.key = config_key::GUI_RULERS_FONT_FAMILY, .value = rulers.font.family});
        values.push_back(
            {.key = config_key::GUI_RULERS_FONT_SIZE, .value = std::int64_t{rulers.font.size}});
        values.push_back({.key = config_key::GUI_RULERS_LAST_VALUE,
                          .value = std::string(lastValueName(rulers.lastValue))});
    }
    return values;
}

dvv::Result<std::string> readConfigText(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t bytes = std::filesystem::file_size(path, error);
    if (error) {
        return dvv::makeError(dvv::Errc::Io, error.message());
    }
    if (bytes > CONFIG_MAX_BYTES) {
        return dvv::makeError(dvv::Errc::Format, std::format("{} bytes is too large", bytes));
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return dvv::makeError(dvv::Errc::Io, "it cannot be opened");
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad()) {
        return dvv::makeError(dvv::Errc::Io, "it cannot be read");
    }
    return text;
}

bool inAppBundle(const std::filesystem::path& executable) {
    const std::filesystem::path macos = executable.parent_path();
    const std::filesystem::path contents = macos.parent_path();
    const std::filesystem::path bundle = contents.parent_path();
    return macos.filename() == "MacOS" && contents.filename() == "Contents" &&
           bundle.extension() == ".app";
}

dvv::Result<std::filesystem::path> defaultConfigPath(
    const std::filesystem::path& executable, bool macos,
    // const std::function&: called during the call only (std::function_ref is C++26).
    const std::function<dvv::Result<std::filesystem::path>()>& user_data_dir,
    const std::function<bool(const std::filesystem::path&)>& exists) {
    if (!(macos && inAppBundle(executable))) {
        std::filesystem::path portable = executable.parent_path() / CONFIG_FILE_NAME;
        if (exists(portable)) {
            return portable;
        }
    }
    DVV_TRY(const std::filesystem::path directory, user_data_dir());
    return directory / CONFIG_FILE_NAME;
}

dvv::Result<std::filesystem::path> defaultConfigPath() {
    DVV_TRY(const std::filesystem::path executable, dvv::paths::executablePath());
#if defined(__APPLE__)
    constexpr bool MACOS = true;
#else
    constexpr bool MACOS = false;
#endif
    return defaultConfigPath(
        executable, MACOS, [] { return dvv::paths::userDataDir("rawreel"); },
        [](const std::filesystem::path& path) {
            std::error_code error;
            return std::filesystem::exists(path, error);
        });
}

namespace {

// `named`: the file --config names, which must exist (a missing one is a warning).
LoadedConfig load(const std::filesystem::path& path, bool named) {
    LoadedConfig loaded{.config = {}, .path = path, .found = false, .warning = {}, .text = {}};
    const std::string shown = dvv::pathDisplayText(path);
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) {
        return ignored(std::move(loaded), error.message());
    }
    if (!exists && named) {
        return ignored(std::move(loaded), "not found");
    }
    if (!exists) {
        DVV_LOG_INFO("config '{}' not found; using the defaults", shown);
        return loaded;
    }
    loaded.found = true;
    dvv::Result<std::string> text = readConfigText(path);
    if (!text) {
        return ignored(std::move(loaded), text.error().message);
    }
    dvv::Result<Config> config = parseConfig(*text);
    if (!config) {
        return ignored(std::move(loaded), config.error().message);
    }
    loaded.config = *config;
    loaded.text = std::move(*text);
    const Config& c = loaded.config;
    DVV_LOG_INFO("config '{}': {} {}, {} {}, log.file {} '{}', log.system {}, {} {}, {} {}", shown,
                 config_key::IO_FILE_READS, fileReadsName(c.io.fileReads),
                 config_key::GUI_SHOW_LOG_PANE, c.gui.showLogPane,
                 c.log.file.enabled ? "on" : "off",
                 c.log.file.folder.empty() ? std::string("default")
                                           : dvv::pathDisplayText(c.log.file.folder),
                 c.log.system.enabled ? "on" : "off", config_key::LOG_LEVEL,
                 levelConfigValue(c.log.level), config_key::LOG_SOURCE, c.log.source);
    return loaded;
}

}  // namespace

LoadedConfig loadConfig(const std::filesystem::path& path) {
    return load(path, false);
}

LoadedConfig loadConfig(const std::optional<std::string>& config_utf8) {
    if (config_utf8) {
        std::filesystem::path path = dvv::pathFromText(*config_utf8);
        std::error_code error;
        if (std::filesystem::path absolute = std::filesystem::absolute(path, error); !error) {
            path = std::move(absolute);
        }
        return load(path, true);
    }
    dvv::Result<std::filesystem::path> path = defaultConfigPath();
    if (!path) {
        DVV_LOG_WARN("no config file: {}; using the defaults", path.error().describe());
        return LoadedConfig{
            .config = {}, .path = {}, .found = false, .warning = path.error().message, .text = {}};
    }
    return loadConfig(*path);
}

}  // namespace rawreel
