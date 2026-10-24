#pragma once

// Drop routing for one stream (design M3 v2 D2, D21).

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace rawreel {

// "file:" + percentEncode(utf8FromPath(path)) with dvv-base's default keep set "/:\\".
[[nodiscard]] std::string fileUri(const std::filesystem::path& path);

// The first file opens (it replaces the stream); `ignored` = the number of other files (the GUI
// logs "<ignored> files ignored: one stream in v1.0"). No files -> uri nullopt, ignored 0.
struct DropPlan {
    std::optional<std::string> uri;
    std::size_t ignored = 0;
    bool operator==(const DropPlan&) const = default;
};
[[nodiscard]] DropPlan routeDrop(std::span<const std::filesystem::path> files);

}  // namespace rawreel
