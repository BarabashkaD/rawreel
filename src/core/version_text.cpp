#include "core/version_text.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "core/startup.h"

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

// FFmpeg's licence text (avutil_license()) -> its short name linked to the licence text on the
// web. Only LGPL builds are loaded; any other licence stays plain text.
TextPart licencePart(std::string_view licence) {
    if (licence == "LGPL version 2.1 or later") {
        return {"LGPL 2.1 or later", "https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html"};
    }
    if (licence == "LGPL version 3 or later") {
        return {"LGPL 3 or later", "https://www.gnu.org/licenses/lgpl-3.0.html"};
    }
    return {std::string(licence), {}};
}

// The FFmpeg line ("FFmpeg <version> (<licence>)<rest>" or "FFmpeg: not available (...)") with
// links. The bundled build's " from <CUSTOM_FFMPEG_URL>" goes: the name links there.
TextLine ffmpegLine(std::string_view line) {
    const std::size_t open = line.find(" (");
    const std::size_t close = line.find(')', open);
    if (line.starts_with("FFmpeg: ") || !line.starts_with("FFmpeg ") ||
        open == std::string_view::npos || close == std::string_view::npos) {
        return {{"FFmpeg " + withoutPrefix(withoutPrefix(line, "FFmpeg"), ": "), {}}};
    }
    std::string rest(line.substr(close + 1));
    const std::string bundled = std::format(" from {}", CUSTOM_FFMPEG_URL);
    if (rest.starts_with(bundled)) {
        rest.erase(0, bundled.size());
    }
    return {{std::string(line.substr(0, open)), std::string(CUSTOM_FFMPEG_URL)},
            {" (", {}},
            licencePart(line.substr(open + 2, close - open - 2)),
            {")" + rest, {}}};
}

}  // namespace

std::string plainText(const TextLine& line) {
    std::string text;
    for (const TextPart& part : line) {
        text += part.text;
        if (!part.url.empty()) {
            text += std::format(" ({})", part.url);
        }
    }
    return text;
}

AboutContent aboutContent(const VersionReport& report) {
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
    content.description =
        "Play and inspect raw and Y4M video frame by frame: pixel formats and colour mapping.";
    content.copyright = {
        {std::format("Copyright (c) 2026 BarabashkaD {} ", MIDDLE_DOT), {}},
        {"MIT licence", "https://github.com/BarabashkaD/rawreel/blob/main/LICENSE"}};
    content.website = "https://github.com/BarabashkaD/rawreel";
    content.system = {
        ffmpegLine(report.ffmpeg),
        {{std::format("wxWidgets {}", wx_version), "https://www.wxwidgets.org"},
         {" (", {}},
         {"wxWindows Licence", "https://www.wxwidgets.org/about/licence/"},
         {")", {}}},
    };
    return content;
}

std::string aboutText(const AboutContent& content) {
    std::string text = std::format("{}\n{}\n{}\n{}\n", content.heading, content.description,
                                   plainText(content.copyright), content.website);
    for (const TextLine& line : content.system) {
        text += plainText(line) + "\n";
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
