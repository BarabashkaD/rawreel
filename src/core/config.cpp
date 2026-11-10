#include "core/config.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "dvv/base/error.h"
#include "dvv/base/json.h"
#include "dvv/base/log.h"
#include "dvv/base/paths.h"
#include "dvv/media/types.h"
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
    DVV_LOG_WARN("config '{}' ignored: {}; using the defaults",
                 dvv::media::utf8FromPath(loaded.path), loaded.warning);
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
        folder = dvv::media::pathFromUtf8(found->get_ref<const std::string&>());
    }
    if (!found->is_string() || (!folder.empty() && !folder.is_absolute())) {
        return dvv::makeError(dvv::Errc::Format,
                              std::format(R"({}: expected an absolute folder or "", not {})", name,
                                          dvv::json::dump(*found)));
    }
    target = std::move(folder);
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
                std::format(R"(io.fileReads: expected "auto", "unbuffered" or "buffered", not {})",
                            dvv::json::dump(*reads)));
        }
        target.fileReads = *mode;
    }
    return {};
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
        DVV_TRY_VOID(readBool(*gui, "showLogPane", "gui.showLogPane", config.gui.showLogPane));
    }
    DVV_TRY(const dvv::json::Value* const log, objectAt(root, "log", "log"));
    if (log != nullptr) {
        DVV_TRY(const dvv::json::Value* const file, objectAt(*log, "file", "log.file"));
        if (file != nullptr) {
            DVV_TRY_VOID(readBool(*file, "enabled", "log.file.enabled", config.log.file.enabled));
            DVV_TRY_VOID(readFolder(*file, "folder", "log.file.folder", config.log.file.folder));
        }
        DVV_TRY(const dvv::json::Value* const system, objectAt(*log, "system", "log.system"));
        if (system != nullptr) {
            DVV_TRY_VOID(
                readBool(*system, "enabled", "log.system.enabled", config.log.system.enabled));
        }
    }
    return config;
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
    const std::function<dvv::Result<std::filesystem::path>()>& user_data_dir,
    const std::function<bool(const std::filesystem::path&)>& exists) {
    if (!(macos && inAppBundle(executable))) {
        const std::filesystem::path portable = executable.parent_path() / CONFIG_FILE_NAME;
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
    LoadedConfig loaded{.config = {}, .path = path, .found = false, .warning = {}};
    const std::string shown = dvv::media::utf8FromPath(path);
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
    const Config& c = loaded.config;
    DVV_LOG_INFO(
        "config '{}': io.fileReads {}, gui.showLogPane {}, log.file {} '{}', log.system {}", shown,
        fileReadsName(c.io.fileReads), c.gui.showLogPane, c.log.file.enabled ? "on" : "off",
        c.log.file.folder.empty() ? std::string("default")
                                  : dvv::media::utf8FromPath(c.log.file.folder),
        c.log.system.enabled ? "on" : "off");
    return loaded;
}

}  // namespace

LoadedConfig loadConfig(const std::filesystem::path& path) {
    return load(path, false);
}

LoadedConfig loadConfig(const std::optional<std::string>& config_utf8) {
    if (config_utf8) {
        std::filesystem::path path = dvv::media::pathFromUtf8(*config_utf8);
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
            .config = {}, .path = {}, .found = false, .warning = path.error().message};
    }
    return loadConfig(*path);
}

}  // namespace rawreel
