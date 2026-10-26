#include "core/version_text.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>
#include <string_view>

namespace rawreel {

std::vector<std::pair<std::string, std::string>> packageVersions() {
    return {
        {"rawreel", RAWREEL_VERSION},
        {"dvv-base", RAWREEL_DVV_BASE_VERSION},
        {"dvv-registry", RAWREEL_DVV_REGISTRY_VERSION},
        {"dvv-platform", RAWREEL_DVV_PLATFORM_VERSION},
        {"dvv-gpu", RAWREEL_DVV_GPU_VERSION},
        {"dvv-media", RAWREEL_DVV_MEDIA_VERSION},
        {"dvv-components", RAWREEL_DVV_COMPONENTS_VERSION},
        {"wxWidgets", RAWREEL_WX_VERSION},
    };
}

std::string vulkanStatusLine(const VulkanStatus& status) {
    if (status.available) {
        return std::format("Vulkan: {}, API {}", status.device, status.api);
    }
    return std::format("Vulkan: not available ({})", status.reason);
}

std::string versionText(const VersionReport& report) {
    std::string text;
    for (const auto& [name, version] : report.packages) {
        text += std::format("{} {}\n", name, version);
    }
    text += report.vulkan + "\n";
    text += report.ffmpeg + "\n";
    return text;
}

namespace {

// U+00B7 MIDDLE DOT in UTF-8, spelled as bytes so the execution character set does not matter.
constexpr std::string_view MIDDLE_DOT = "\xC2\xB7";

std::string withoutPrefix(std::string_view line, std::string_view prefix) {
    if (line.starts_with(prefix)) {
        line.remove_prefix(prefix.size());
    }
    return std::string(line);
}

}  // namespace

AboutContent aboutContent(const VersionReport& report, std::string_view appearance) {
    AboutContent content;
    std::string version;
    std::string wx_version = "not part of this build";
    for (const auto& [name, value] : report.packages) {
        if (name == "rawreel") {
            version = value;
        } else if (name == "wxWidgets") {
            wx_version = value;
        } else if (name.starts_with("dvv-")) {
            content.components.emplace_back(name, value);
        }
    }
    content.heading = std::format("rawreel {}", version);
    content.description = "Raw video player (Vulkan)";
    content.copyright = std::format("Copyright (c) 2026 BarabashkaD {} MIT licence", MIDDLE_DOT);
    content.website = "https://github.com/BarabashkaD/rawreel";
    const std::string_view ffmpeg_prefix =
        report.ffmpeg.starts_with("FFmpeg: ") ? "FFmpeg: " : "FFmpeg ";
    content.system = {
        {"GPU:", withoutPrefix(report.vulkan, "Vulkan: ")},
        {"FFmpeg:", withoutPrefix(report.ffmpeg, ffmpeg_prefix)},
        {"wxWidgets:", std::format("{} {} appearance: {}", wx_version, MIDDLE_DOT,
                                   appearance.empty() ? std::string_view("unknown") : appearance)},
    };
    return content;
}

std::string aboutText(const AboutContent& content) {
    std::string text = std::format("{}\n{}\n{}\n{}\n", content.heading, content.description,
                                   content.copyright, content.website);
    std::size_t width = 0;
    for (const auto& [label, value] : content.system) {
        width = std::max(width, label.size() + 1);
    }
    for (const auto& [label, value] : content.system) {
        text += std::format("{:<{}}{}\n", label, width, value);
    }
    text += "Components:\n";
    for (const auto& [name, version] : content.components) {
        text += std::format("  {} {}\n", name, version);
    }
    if (content.components.empty()) {
        text += "  none\n";
    }
    return text;
}

dvv::json::Value versionJson(const VersionReport& report) {
    dvv::json::Value value = dvv::json::Value::object();
    for (const auto& [name, version] : report.packages) {
        std::string key = name;
        std::ranges::transform(key, key.begin(), [](char c) {
            return c == '-' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        });
        value[key] = version;
    }
    value["vulkan"] = report.vulkan;
    value["ffmpeg"] = report.ffmpeg;
    return value;
}

}  // namespace rawreel
