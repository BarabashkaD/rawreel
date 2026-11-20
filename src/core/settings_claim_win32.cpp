#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <system_error>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "core/settings_claim.h"
#include "dvv/base/utf8.h"

namespace rawreel {

namespace fs = std::filesystem;

std::unique_ptr<SettingsClaim> SettingsClaim::tryClaim(const fs::path& config) {
    std::error_code error;
    const fs::path canonical = fs::weakly_canonical(config, error);
    if (error) {
        return nullptr;
    }
    const std::wstring name =
        std::format(L"Local\\rawreel-settings-{:016x}", fnv1a64(dvv::pathText(canonical)));
    const HANDLE mutex = CreateMutexW(nullptr, FALSE, name.c_str());
    if (mutex == nullptr) {
        return nullptr;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return nullptr;
    }
    return std::unique_ptr<SettingsClaim>(
        new SettingsClaim(reinterpret_cast<std::intptr_t>(mutex)));
}

SettingsClaim::~SettingsClaim() {
    // NOLINTNEXTLINE(performance-no-int-to-ptr): native_ holds the mutex HANDLE.
    CloseHandle(reinterpret_cast<HANDLE>(native_));
}

}  // namespace rawreel
