#pragma once

// The config writer (spec §6.4): the only code that writes rawreel.json. It merges a set of key
// values into the file as it is at that moment (the user may have edited it while the app ran)
// and replaces the file safely. wx-free; it never logs and never retries, the caller reports a
// failure. Only the settings owner (core/settings_claim.h) calls it.

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "dvv/base/error.h"

namespace rawreel {

struct ConfigValue {
    std::string_view key;  // dotted, e.g. "gui.showLogPane", "log.file.folder"
    std::variant<bool, std::string, std::int64_t, std::vector<std::string>>
        value;  // strings are UTF-8
};

// Sets each key of `values` in the config file `path` and keeps every other key, unknown ones
// too, in the file's key order (two-space indent, a final newline). A symbolic link stays a link:
// its target is written. A missing file is created, and so is its folder when only the folder's
// last component is missing. The file is replaced through a temporary file
// "<name>.<pid>.<random>.tmp" in the target's folder (created exclusively, with the target's
// permission bits on POSIX, flushed to disk), which is removed on any failure.
//
// Errc::Format with the loader's reason (parseConfig) when the file as it is now would be
// rejected (the file is left unchanged), or when it is over the loader's 1 MiB limit; Errc::Io
// "read-only" when the file's permission bits grant no write (the read-only attribute on
// Windows); Errc::Io for any other failure (a missing folder above the last one, no write access,
// a file held open on Windows).
[[nodiscard]] dvv::Result<void> saveConfigValues(const std::filesystem::path& path,
                                                 std::span<const ConfigValue> values);

}  // namespace rawreel
