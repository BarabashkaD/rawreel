#include <doctest/doctest.h>

// The icon resources of rawreel-gui.exe and rawreel-cli.exe (app icon plan D4, D8): Explorer shows
// an exe's first RT_GROUP_ICON, so that group must be rawreel's 10-image ICO. CTest passes the exe
// paths in RAWREEL_GUI_EXE and RAWREEL_CLI_EXE; an unset variable (no rawreel-gui under the
// sanitizer presets, or a run outside CTest) skips that exe.
#if defined(_WIN32)
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>

#include <windows.h>

#include "dvv/media/types.h"

namespace {

constexpr std::uint16_t ICON_IMAGES = 10;      // 16, 20, 24, 32, 40, 48, 64, 96, 128 and 256 px
constexpr std::size_t GROUP_HEADER_BYTES = 6;  // GRPICONDIR: idReserved, idType, idCount
constexpr std::size_t GROUP_ENTRY_BYTES = 14;  // GRPICONDIRENTRY, packed

struct LibraryCloser {
    void operator()(HMODULE library) const noexcept { FreeLibrary(library); }
};
using Library = std::unique_ptr<std::remove_pointer_t<HMODULE>, LibraryCloser>;

// The value of an environment variable; nullopt when it is unset or empty.
std::optional<std::wstring> environmentValue(const wchar_t* name) {
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);  // includes the terminator
    if (needed == 0) {
        return std::nullopt;
    }
    std::wstring value(needed, L'\0');
    const DWORD length = GetEnvironmentVariableW(name, value.data(), needed);
    if (length == 0 || length >= needed) {
        return std::nullopt;
    }
    value.resize(length);
    return value;
}

// A resource name as text: its string, or "#<id>" for an integer name. Resource names are ASCII
// (rc.exe upper-cases them); any other character shows as '?'.
std::string resourceName(LPCWSTR name) {
    if (IS_INTRESOURCE(name)) {
        return "#" + std::to_string(reinterpret_cast<ULONG_PTR>(name) & 0xFFFFU);
    }
    std::string text;
    for (const wchar_t* character = name; *character != L'\0'; ++character) {
        text.push_back(*character < 0x80 ? static_cast<char>(*character) : '?');
    }
    return text;
}

// EnumResourceNamesW callback: records the first name, then stops the enumeration.
BOOL CALLBACK recordFirstName(HMODULE /*module*/, LPCWSTR /*type*/, LPWSTR name, LONG_PTR context) {
    *reinterpret_cast<std::optional<std::string>*>(context) = resourceName(name);
    return FALSE;
}

void checkFirstIconGroup(const wchar_t* variable, const char* variable_text) {
    CAPTURE(variable_text);
    const std::optional<std::wstring> value = environmentValue(variable);
    if (!value) {
        MESSAGE(variable_text << " is unset: skipped");
        return;
    }
    // LoadLibraryExW wants backslashes; $<TARGET_FILE> gives forward slashes.
    std::filesystem::path exe(*value);
    exe.make_preferred();
    const std::string exe_text = dvv::media::utf8FromPath(exe);
    CAPTURE(exe_text);

    const Library library(LoadLibraryExW(
        exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE));
    const DWORD load_error = library ? DWORD{0} : GetLastError();
    REQUIRE_MESSAGE(library.get() != nullptr, "LoadLibraryExW failed, error " << load_error);

    // Named entries come first in a resource directory, sorted by their upper-case name, then the
    // integer ones; EnumResourceNamesW keeps that order, the order Explorer picks from.
    std::optional<std::string> first;
    // FALSE both when the callback stops the walk and when the exe has no RT_GROUP_ICON.
    static_cast<void>(EnumResourceNamesW(library.get(), RT_GROUP_ICON, &recordFirstName,
                                         reinterpret_cast<LONG_PTR>(&first)));
    REQUIRE_MESSAGE(first.has_value(), "the exe has no RT_GROUP_ICON");
    CHECK(*first == "RAWREEL");

    const HRSRC resource = FindResourceW(library.get(), L"RAWREEL", RT_GROUP_ICON);
    REQUIRE_MESSAGE(resource != nullptr, "no RT_GROUP_ICON named RAWREEL");
    const HGLOBAL loaded = LoadResource(library.get(), resource);
    REQUIRE(loaded != nullptr);
    const auto* const bytes = static_cast<const unsigned char*>(LockResource(loaded));
    REQUIRE(bytes != nullptr);
    const std::size_t size = SizeofResource(library.get(), resource);
    REQUIRE(size >= GROUP_HEADER_BYTES);
    std::uint16_t type = 0;
    std::uint16_t count = 0;
    std::memcpy(&type, bytes + 2, sizeof type);  // little-endian, as is Windows
    std::memcpy(&count, bytes + 4, sizeof count);
    CHECK(type == 1);  // icons, not cursors
    CHECK(count == ICON_IMAGES);
    CHECK(size == GROUP_HEADER_BYTES + GROUP_ENTRY_BYTES * count);
}

}  // namespace

TEST_CASE("exe icon: first group RAWREEL with 10 images") {
    SUBCASE("rawreel-gui") {
        checkFirstIconGroup(L"RAWREEL_GUI_EXE", "RAWREEL_GUI_EXE");
    }
    SUBCASE("rawreel-cli") {
        checkFirstIconGroup(L"RAWREEL_CLI_EXE", "RAWREEL_CLI_EXE");
    }
}
#endif
