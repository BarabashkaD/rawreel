#include "core/config_writer.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "core/config.h"
#include "core/log_file_switch.h"
#include "dvv/base/error.h"
#include "dvv/media/types.h"

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

std::uint64_t randomNumber() {
    std::random_device random;
    return (std::uint64_t{random()} << 32U) ^ std::uint64_t{random()};
}

#if defined(_WIN32)

std::string lastErrorText() {
    return std::system_category().message(static_cast<int>(GetLastError()));
}

// Writes `text` to the new file `temporary` and replaces `target` with it.
dvv::Result<void> writeAndReplace(const fs::path& target, const fs::path& temporary,
                                  bool target_exists, std::string_view text) {
    const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return dvv::makeError(dvv::Errc::Io,
                              std::format("cannot create a temporary file: {}", lastErrorText()));
    }
    std::string failure;
    std::string_view rest = text;
    while (failure.empty() && !rest.empty()) {
        const DWORD chunk =
            rest.size() > 0x40000000U ? 0x40000000U : static_cast<DWORD>(rest.size());
        DWORD written = 0;
        if (WriteFile(file, rest.data(), chunk, &written, nullptr) == FALSE || written == 0) {
            failure = std::format("cannot write: {}", lastErrorText());
        }
        rest.remove_prefix(written);
    }
    if (failure.empty() && FlushFileBuffers(file) == FALSE) {
        failure = std::format("cannot flush: {}", lastErrorText());
    }
    if (CloseHandle(file) == FALSE && failure.empty()) {
        failure = std::format("cannot write: {}", lastErrorText());
    }
    if (failure.empty() && !target_exists &&
        MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) == FALSE) {
        failure = std::format("cannot create the file: {}", lastErrorText());
    }
    // ReplaceFileW keeps the target's attributes and ACL.
    if (failure.empty() && target_exists &&
        ReplaceFileW(target.c_str(), temporary.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS,
                     nullptr, nullptr) == FALSE) {
        const DWORD code = GetLastError();
        // Without a backup file, 1176 means the target is gone and the temporary file still has
        // its own name: the move finishes the save (not a retry). Otherwise (1177: the target
        // still exists under another name), or when that move fails, the temporary file may be
        // the only copy of the config, so it stays and the error names it.
        if (code == ERROR_UNABLE_TO_MOVE_REPLACEMENT &&
            MoveFileExW(temporary.c_str(), target.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE) {
            return {};
        }
        const std::string reason = code == ERROR_UNABLE_TO_MOVE_REPLACEMENT
                                       ? lastErrorText()
                                       : std::system_category().message(static_cast<int>(code));
        if (code == ERROR_UNABLE_TO_MOVE_REPLACEMENT ||
            code == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2) {
            return dvv::makeError(
                dvv::Errc::Io,
                std::format("cannot replace the file: {}; the new settings are in {}", reason,
                            dvv::media::utf8FromPath(temporary)));
        }
        failure = std::format("cannot replace the file: {}", reason);
    }
    if (!failure.empty()) {
        DeleteFileW(temporary.c_str());
        return dvv::makeError(dvv::Errc::Io, std::move(failure));
    }
    return {};
}

std::uint32_t processId() {
    return static_cast<std::uint32_t>(GetCurrentProcessId());
}

#else

std::string errnoText() {
    return std::generic_category().message(errno);
}

// fsync, through F_FULLFSYNC on macOS (fsync alone does not reach the disk there); a file system
// without F_FULLFSYNC gets plain fsync.
bool flushToDisk(int file) {
#if defined(__APPLE__)
    if (fcntl(file, F_FULLFSYNC) == 0) {
        return true;
    }
#endif
    return fsync(file) == 0;
}

dvv::Result<void> writeAndReplace(const fs::path& target, const fs::path& temporary,
                                  bool target_exists, std::string_view text) {
    // The target's permission bits; a new file gets the umask's default.
    mode_t mode = 0666;
    bool copy_mode = false;
    if (struct stat status{}; target_exists && stat(target.c_str(), &status) == 0) {
        mode = status.st_mode & 0777;
        copy_mode = true;
    }
    const int file = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
    if (file < 0) {
        return dvv::makeError(dvv::Errc::Io,
                              std::format("cannot create a temporary file: {}", errnoText()));
    }
    std::string failure;
    if (copy_mode && fchmod(file, mode) != 0) {  // open() applied the umask
        failure = std::format("cannot set the permissions: {}", errnoText());
    }
    std::string_view rest = text;
    while (failure.empty() && !rest.empty()) {
        const ssize_t written = write(file, rest.data(), rest.size());
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            failure = std::format("cannot write: {}", errnoText());
            break;
        }
        rest.remove_prefix(static_cast<std::size_t>(written));
    }
    if (failure.empty() && !flushToDisk(file)) {
        failure = std::format("cannot flush: {}", errnoText());
    }
    if (close(file) != 0 && failure.empty()) {
        failure = std::format("cannot write: {}", errnoText());
    }
    if (failure.empty() && rename(temporary.c_str(), target.c_str()) != 0) {
        failure = std::format("cannot replace the file: {}", errnoText());
    }
    if (!failure.empty()) {
        unlink(temporary.c_str());
        return dvv::makeError(dvv::Errc::Io, std::move(failure));
    }
    return {};
}

std::uint32_t processId() {
    return static_cast<std::uint32_t>(getpid());
}

#endif

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
    const fs::path temporary =
        target.parent_path() / dvv::media::pathFromUtf8(std::format(
                                   "{}.{}.{:016x}.tmp", dvv::media::utf8FromPath(target.filename()),
                                   processId(), randomNumber()));
    return writeAndReplace(target, temporary, exists, text);
}

}  // namespace rawreel
