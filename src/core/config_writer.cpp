#include "core/config_writer.h"

#include <cstddef>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

#include <nlohmann/json.hpp>

#include "core/config.h"
#include "core/log_file_switch.h"
#include "dvv/base/error.h"
#include "dvv/platform/file_writer.h"

namespace rawreel {

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

// More links than this in a chain is a loop.
constexpr int MAX_LINKS = 40;

// The file a symbolic link chain ends at (it may not exist); `path` itself when it is no link.
dvv::Result<fs::path> resolveTarget(fs::path path) {
    for (int links = 0;; ++links) {
        std::error_code error;
        const fs::file_status status = fs::symlink_status(path, error);
        if (status.type() == fs::file_type::none) {
            return dvv::makeError(dvv::Errc::Io, error.message());
        }
        if (!fs::is_symlink(status)) {  // a file, or nothing (not_found)
            return path;
        }
        if (links == MAX_LINKS) {
            return dvv::makeError(dvv::Errc::Io, "too many symbolic links");
        }
        const fs::path next = fs::read_symlink(path, error);
        if (error) {
            return dvv::makeError(dvv::Errc::Io, error.message());
        }
        path = next.is_absolute() ? next : path.parent_path() / next;
    }
}

// Sets each value in `root`, creating objects on the way; Errc::Format when a parent on the way
// is not an object.
dvv::Result<void> merge(Json& root, std::span<const ConfigValue> values) {
    for (const ConfigValue& value : values) {
        const std::string_view key = value.key;
        Json* node = &root;
        std::size_t start = 0;
        for (;;) {
            const std::size_t dot = key.find('.', start);
            const std::string name(key.substr(start, dot - start));
            if (name.empty()) {
                return dvv::makeError(dvv::Errc::Internal,
                                      std::format("bad config key \"{}\"", key));
            }
            if (dot == std::string_view::npos) {
                std::visit([&](const auto& leaf) { (*node)[name] = leaf; }, value.value);
                break;
            }
            Json& child = (*node)[name];  // null when new
            if (child.is_null()) {
                child = Json::object();
            }
            if (!child.is_object()) {
                return dvv::makeError(dvv::Errc::Format,
                                      std::format("{}: expected an object", key.substr(0, dot)));
            }
            node = &child;
            start = dot + 1;
        }
    }
    return {};
}

// The text to write: the file's JSON (an empty object for a missing file) with `values` set.
dvv::Result<std::string> mergedText(const std::string* original,
                                    std::span<const ConfigValue> values) {
    try {
        Json root = Json::object();
        if (original != nullptr) {
            root = Json::parse(*original, nullptr, false);
            if (root.is_discarded() || !root.is_object()) {
                return dvv::makeError(dvv::Errc::Format, "the file is not a JSON object");
            }
        }
        DVV_TRY_VOID(merge(root, values));
        return root.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
    } catch (const nlohmann::json::exception& error) {
        return dvv::makeError(dvv::Errc::Internal, error.what());
    }
}

}  // namespace

dvv::Result<void> saveConfigValues(const fs::path& path, std::span<const ConfigValue> values) {
    std::error_code error;
    const fs::path absolute = fs::absolute(path, error);
    if (error) {
        return dvv::makeError(dvv::Errc::Io, error.message());
    }
    // 1. The target: a link stays a link.
    DVV_TRY(const fs::path target, resolveTarget(absolute));

    // 2. Refuse what must not be written.
    const fs::file_status status = fs::status(target, error);
    if (status.type() == fs::file_type::none) {
        return dvv::makeError(dvv::Errc::Io, error.message());
    }
    const bool exists = fs::exists(status);  // not_found: a new file
    std::string original;
    if (exists) {
        if (!fs::is_regular_file(status)) {
            return dvv::makeError(dvv::Errc::Io, "not a file");
        }
        // From the bits, so it holds for root too; on Windows the read-only attribute clears them.
        constexpr fs::perms WRITE =
            fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
        if ((status.permissions() & WRITE) == fs::perms::none) {
            return dvv::makeError(dvv::Errc::Io, "read-only");
        }
        DVV_TRY(original, readConfigText(target));
        DVV_TRY_VOID(parseConfig(original));
    } else {
        // 5. A missing file: only the folder's last component is created, as for a chosen log
        // folder.
        DVV_TRY_VOID(prepareLogFolder(target.parent_path(), FolderCreation::LastOnly));
    }

    // 3. Merge, then check the result as the loader will read it.
    DVV_TRY(const std::string text, mergedText(exists ? &original : nullptr, values));
    if (text.size() > CONFIG_MAX_BYTES) {
        return dvv::makeError(dvv::Errc::Format, std::format("{} bytes is too large", text.size()));
    }
    DVV_TRY_VOID(parseConfig(text));

    // 4. Write safely.
    return dvv::platform::replaceFile(target, std::as_bytes(std::span(text)));
}

}  // namespace rawreel
