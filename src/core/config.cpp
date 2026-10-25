#include "core/config.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
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

// A config file is a few hundred bytes; anything this large is not one.
constexpr std::uintmax_t MAX_CONFIG_BYTES = std::uintmax_t{1} << 20U;

LoadedConfig ignored(LoadedConfig loaded, std::string reason) {
    loaded.config = Config{};
    loaded.warning = std::move(reason);
    DVV_LOG_WARN("config '{}' ignored: {}; using the defaults",
                 dvv::media::utf8FromPath(loaded.path), loaded.warning);
    return loaded;
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
    const auto io = root.find("io");
    if (io == root.end()) {
        return config;
    }
    if (!io->is_object()) {
        return dvv::makeError(dvv::Errc::Format, "io: expected an object");
    }
    if (const auto reads = io->find("fileReads"); reads != io->end()) {
        const std::optional<FileReadMode> mode =
            reads->is_string() ? parseFileReads(reads->get_ref<const std::string&>())
                               : std::nullopt;
        if (!mode) {
            return dvv::makeError(
                dvv::Errc::Format,
                std::format(R"(io.fileReads: expected "auto", "unbuffered" or "buffered", not {})",
                            dvv::json::dump(*reads)));
        }
        config.io.fileReads = *mode;
    }
    return config;
}

dvv::Result<std::filesystem::path> defaultConfigPath() {
    DVV_TRY(const std::filesystem::path executable, dvv::paths::executablePath());
    return executable.parent_path() / CONFIG_FILE_NAME;
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
    const std::uintmax_t bytes = std::filesystem::file_size(path, error);
    if (error) {
        return ignored(std::move(loaded), error.message());
    }
    if (bytes > MAX_CONFIG_BYTES) {
        return ignored(std::move(loaded), std::format("{} bytes is too large", bytes));
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return ignored(std::move(loaded), "it cannot be opened");
    }
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    dvv::Result<Config> config = parseConfig(text);
    if (!config) {
        return ignored(std::move(loaded), config.error().message);
    }
    loaded.config = *config;
    DVV_LOG_INFO("config '{}': io.fileReads {}", shown, fileReadsName(loaded.config.io.fileReads));
    return loaded;
}

}  // namespace

LoadedConfig loadConfig(const std::filesystem::path& path) {
    return load(path, false);
}

LoadedConfig loadConfig(const std::optional<std::string>& config_utf8) {
    if (config_utf8) {
        return load(dvv::media::pathFromUtf8(*config_utf8), true);
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
