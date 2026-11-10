#include "core/settings_claim.h"

#include <filesystem>
#include <memory>
#include <system_error>

#if defined(_WIN32)
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "dvv/media/types.h"
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include "core/log_file_switch.h"
#endif

namespace rawreel {

namespace fs = std::filesystem;

#if defined(_WIN32)

namespace {

std::uint64_t fnv1a64(std::string_view text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char byte : text) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

}  // namespace

std::unique_ptr<SettingsClaim> SettingsClaim::tryClaim(const fs::path& config) {
    std::error_code error;
    const fs::path canonical = fs::weakly_canonical(config, error);
    if (error) {
        return nullptr;
    }
    const std::wstring name = std::format(L"Local\\rawreel-settings-{:016x}",
                                          fnv1a64(dvv::media::utf8FromPath(canonical)));
    const HANDLE mutex = CreateMutexW(nullptr, FALSE, name.c_str());
    if (mutex == nullptr) {
        return nullptr;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return nullptr;
    }
    return std::unique_ptr<SettingsClaim>(new SettingsClaim(mutex));
}

SettingsClaim::~SettingsClaim() {
    CloseHandle(mutex_);
}

#else

std::unique_ptr<SettingsClaim> SettingsClaim::tryClaim(const fs::path& config) {
    std::error_code error;
    fs::path lock = fs::weakly_canonical(config, error);
    if (error) {
        return nullptr;
    }
    // A --config file in a new folder: its last component is made here (as the writer would), so
    // this process can own it.
    if (!prepareLogFolder(lock.parent_path(), FolderCreation::LastOnly)) {
        return nullptr;
    }
    lock += ".lock";
    // Opened for writing: a folder or lock file this user cannot write makes no owner.
    const int file = open(lock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (file < 0) {
        return nullptr;
    }
    // A lock of this open file description: a second open, in this process too, conflicts.
    if (flock(file, LOCK_EX | LOCK_NB) != 0) {
        close(file);
        return nullptr;
    }
    return std::unique_ptr<SettingsClaim>(new SettingsClaim(file));
}

SettingsClaim::~SettingsClaim() {
    close(lock_file_);  // releases the flock
}

#endif

}  // namespace rawreel
