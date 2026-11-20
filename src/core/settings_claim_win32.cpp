#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
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

namespace {

// FNV-1a 64 of `text`: the hash in the mutex name.
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
