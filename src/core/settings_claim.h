#pragma once

// The settings claim (spec §6.3): of several rawreel-gui processes, only the one holding the
// claim of the config file owns the settings and may write the file. Keyed by the file's
// canonical path (weakly_canonical), so two spellings of one file conflict. The OS releases the
// claim when the process ends, so a crash leaves no stale owner. wx-free.

#include <filesystem>
#include <memory>

namespace rawreel {

class SettingsClaim {
public:
    // An exclusive claim of `config` for this process, released by the destructor or by the
    // process's end. Null when another claim (in any process, this one included) holds it, or
    // when the claim cannot be made.
    //
    // POSIX: an exclusive non-blocking flock on "<config>.lock", an empty file next to the config
    // (it may stay behind; it is harmless). A missing config folder is created first when only
    // its last component is missing (as the writer does), so a --config file in a new folder can
    // be owned. Null when that folder cannot be made or the lock file cannot be created (the
    // folder is not writable, so the file could not be saved anyway).
    //
    // Windows: a named mutex "Local\rawreel-settings-<16 hex of FNV-1a 64 over the UTF-8 of
    // weakly_canonical(config)>". It does not look at the folder: neither creates nor checks it.
    [[nodiscard]] static std::unique_ptr<SettingsClaim> tryClaim(
        const std::filesystem::path& config);

    SettingsClaim(const SettingsClaim&) = delete;
    SettingsClaim& operator=(const SettingsClaim&) = delete;
    SettingsClaim(SettingsClaim&&) = delete;
    SettingsClaim& operator=(SettingsClaim&&) = delete;
    ~SettingsClaim();

private:
#if defined(_WIN32)
    explicit SettingsClaim(void* mutex) noexcept : mutex_(mutex) {}
    void* mutex_;  // HANDLE
#else
    explicit SettingsClaim(int lock_file) noexcept : lock_file_(lock_file) {}
    int lock_file_;  // the open "<config>.lock", holding the flock
#endif
};

}  // namespace rawreel
