#include <cstdint>
#include <filesystem>
#include <memory>
#include <system_error>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include "core/log_file_switch.h"
#include "core/settings_claim.h"

namespace rawreel {

namespace fs = std::filesystem;

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
    close(static_cast<int>(native_));  // releases the flock
}

}  // namespace rawreel
