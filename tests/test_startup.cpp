// Startup and version lines (M4 Task 7a): the FFmpeg status line, the open-error text, the Vulkan
// line, the package list and the --version text and JSON. No FFmpeg is loaded here.

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "core/startup.h"
#include "core/version_text.h"
#include "dvv/base/error.h"
#include "dvv/base/json.h"
#include "dvv/components/ffmpeg.h"

namespace {

using dvv::components::FfmpegStatus;

dvv::Error errorOf(dvv::Errc code, std::string message) {
    return dvv::Error{.code = code, .message = std::move(message), .where = {}, .cause = nullptr};
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("startup: FFmpeg status line") {
        FfmpegStatus status{.core = true,
                            .lavfi = true,
                            .reason = {},
                            .version = "n8.1.3",
                            .licence = "LGPL version 2.1 or later",
                            .directory = "ffmpeg",
                            .libraryVersions = {}};
        CHECK(rawreel::ffmpegStatusLine(status) ==
              "FFmpeg n8.1.3 (LGPL version 2.1 or later) from ffmpeg");
        status.lavfi = false;
        CHECK(rawreel::ffmpegStatusLine(status) ==
              "FFmpeg n8.1.3 (LGPL version 2.1 or later) from ffmpeg, lavfi not available");
        FfmpegStatus missing;
        missing.reason = "DVV_FFMPEG_DIR must be absolute";
        CHECK(rawreel::ffmpegStatusLine(missing) ==
              "FFmpeg: not available (DVV_FFMPEG_DIR must be absolute)");

        // About and the GUI infobar: the status only, no directory and no reason (both may hold a
        // path; the log keeps the full line).
        using rawreel::StatusPaths;
        CHECK(rawreel::ffmpegStatusLine(status, StatusPaths::Hidden) ==
              "FFmpeg n8.1.3 (LGPL version 2.1 or later), lavfi not available");
        status.lavfi = true;
        CHECK(rawreel::ffmpegStatusLine(status, StatusPaths::Hidden) ==
              "FFmpeg n8.1.3 (LGPL version 2.1 or later)");
        missing.reason = "FFmpeg not found (looked in some/dir)";
        CHECK(rawreel::ffmpegStatusLine(missing, StatusPaths::Hidden) ==
              "FFmpeg: not available (details in the log)");
    }

    TEST_CASE("startup: open error text") {
        const dvv::Error unsupported =
            errorOf(dvv::Errc::Unsupported, "no source provider for scheme 'lavfi'");
        FfmpegStatus missing;
        missing.reason = "not found";
        const std::string lavfi_uri = "lavfi:testsrc2";
        CHECK(rawreel::openErrorText(unsupported, lavfi_uri, missing) ==
              unsupported.describe() + "; FFmpeg: not available (not found)");

        const FfmpegStatus loaded{.core = true,
                                  .lavfi = true,
                                  .reason = {},
                                  .version = "n8.1.3",
                                  .licence = "LGPL",
                                  .directory = "ffmpeg",
                                  .libraryVersions = {}};
        CHECK(rawreel::openErrorText(unsupported, lavfi_uri, loaded) == unsupported.describe());

        const dvv::Error io = errorOf(dvv::Errc::Io, "cannot read");
        CHECK(rawreel::openErrorText(io, lavfi_uri, missing) == io.describe());

        FfmpegStatus core_only = loaded;
        core_only.lavfi = false;
        core_only.reason = "avfilter missing";
        CHECK(rawreel::openErrorText(unsupported, lavfi_uri, core_only) ==
              unsupported.describe() + "; " + rawreel::ffmpegStatusLine(core_only));
        CHECK(rawreel::openErrorText(unsupported, "file:x.yuv", core_only) ==
              unsupported.describe());
        CHECK(rawreel::openErrorText(unsupported, lavfi_uri, core_only,
                                     rawreel::StatusPaths::Hidden) ==
              unsupported.describe() + "; FFmpeg n8.1.3 (LGPL), lavfi not available");

        // FFmpeg missing: a file open (not only lavfi) gets the status line too (design D21).
        CHECK(rawreel::openErrorText(unsupported, "file:clip.mkv", missing) ==
              unsupported.describe() + "; FFmpeg: not available (not found)");
    }

    TEST_CASE("startup: open error message for --json") {
        const dvv::Error unsupported =
            errorOf(dvv::Errc::Unsupported, "no source provider for scheme 'lavfi'");
        FfmpegStatus missing;
        missing.reason = "not found";
        CHECK(rawreel::openErrorMessage(unsupported, "lavfi:testsrc2", missing) ==
              "no source provider for scheme 'lavfi'; FFmpeg: not available (not found)");
        CHECK(rawreel::openErrorMessage(unsupported, "file:clip.mkv", missing) ==
              "no source provider for scheme 'lavfi'; FFmpeg: not available (not found)");
        const dvv::Error io = errorOf(dvv::Errc::Io, "cannot read");
        CHECK(rawreel::openErrorMessage(io, "file:x.yuv", missing) == "cannot read");
    }

    TEST_CASE("version: Vulkan line") {
        CHECK(rawreel::vulkanStatusLine(
                  {.available = true, .device = "llvmpipe", .api = "1.4.318", .reason = {}}) ==
              "Vulkan: llvmpipe, API 1.4.318");
        rawreel::VulkanStatus none;
        none.reason = "no Vulkan loader";
        CHECK(rawreel::vulkanStatusLine(none) == "Vulkan: not available (no Vulkan loader)");
    }

    TEST_CASE("version: package list") {
        constexpr std::array<std::string_view, 8> NAMES{
            "rawreel", "dvv-base",  "dvv-registry",   "dvv-platform",
            "dvv-gpu", "dvv-media", "dvv-components", "wxWidgets"};
        const auto packages = rawreel::packageVersions();
        REQUIRE(packages.size() == NAMES.size());
        for (std::size_t i = 0; i < NAMES.size(); ++i) {
            CAPTURE(i);
            CHECK(packages[i].first == NAMES[i]);
            CHECK_FALSE(packages[i].second.empty());
        }
        CHECK(packages[0].second == RAWREEL_VERSION);
    }

    TEST_CASE("version: text and JSON") {
        const rawreel::VersionReport report{
            .packages = {{"rawreel", "0.4.0"}, {"dvv-base", "0.4.0"}},
            .vulkan = "Vulkan: x, API 1.3.0",
            .ffmpeg = "FFmpeg: not available (y)"};
        CHECK(rawreel::versionText(report) ==
              "rawreel 0.4.0\ndvv-base 0.4.0\nVulkan: x, API 1.3.0\nFFmpeg: not available (y)\n");
        const dvv::json::Value json = rawreel::versionJson(report);
        CHECK(dvv::json::get<std::string>(json, "rawreel").value_or("") == "0.4.0");
        CHECK(dvv::json::get<std::string>(json, "dvv_base").value_or("") == "0.4.0");
        CHECK(dvv::json::get<std::string>(json, "vulkan").value_or("") == "Vulkan: x, API 1.3.0");
        CHECK(dvv::json::get<std::string>(json, "ffmpeg").value_or("") ==
              "FFmpeg: not available (y)");
    }

    TEST_CASE("version: About content and text") {
        const rawreel::VersionReport report{.packages = {{"rawreel", "0.4.0"},
                                                         {"dvv-base", "0.4.1"},
                                                         {"dvv-gpu", "0.4.2"},
                                                         {"wxWidgets", "3.3.1"}},
                                            .vulkan = "Vulkan: x, API 1.3.0",
                                            .ffmpeg = "FFmpeg n8.1.3 (LGPL version 2.1 or later)"};
        const rawreel::AboutContent content = rawreel::aboutContent(report, "dark -> Ok");
        CHECK(content.heading == "rawreel 0.4.0");
        CHECK(content.description == "Raw video player (Vulkan)");
        CHECK(content.copyright == "Copyright (c) 2026 BarabashkaD \xC2\xB7 MIT licence");
        CHECK(content.website == "https://github.com/BarabashkaD/rawreel");
        using Lines = std::vector<std::pair<std::string, std::string>>;
        CHECK(content.system == Lines{{"GPU:", "x, API 1.3.0"},
                                      {"FFmpeg:", "n8.1.3 (LGPL version 2.1 or later)"},
                                      {"wxWidgets:", "3.3.1 \xC2\xB7 appearance: dark -> Ok"}});
        CHECK(content.components == Lines{{"dvv-base", "0.4.1"}, {"dvv-gpu", "0.4.2"}});
        const std::string text = rawreel::aboutText(content);
        CHECK(text ==
              "rawreel 0.4.0\n"
              "Raw video player (Vulkan)\n"
              "Copyright (c) 2026 BarabashkaD \xC2\xB7 MIT licence\n"
              "https://github.com/BarabashkaD/rawreel\n"
              "GPU:       x, API 1.3.0\n"
              "FFmpeg:    n8.1.3 (LGPL version 2.1 or later)\n"
              "wxWidgets: 3.3.1 \xC2\xB7 appearance: dark -> Ok\n"
              "Components:\n"
              "  dvv-base 0.4.1\n"
              "  dvv-gpu 0.4.2\n");
        // The name and version once.
        CHECK(text.find("rawreel 0.4.0") == text.rfind("rawreel 0.4.0"));
    }

    TEST_CASE("version: About content when Vulkan and FFmpeg are not available") {
        const rawreel::VersionReport report{.packages = {{"rawreel", "0.4.0"}},
                                            .vulkan = "Vulkan: not available (no device)",
                                            .ffmpeg = "FFmpeg: not available (details in the log)"};
        const rawreel::AboutContent content = rawreel::aboutContent(report, "");
        using Lines = std::vector<std::pair<std::string, std::string>>;
        CHECK(content.system ==
              Lines{{"GPU:", "not available (no device)"},
                    {"FFmpeg:", "not available (details in the log)"},
                    {"wxWidgets:", "not part of this build \xC2\xB7 appearance: unknown"}});
        CHECK(content.components.empty());
        CHECK(rawreel::aboutText(content).ends_with("Components:\n  none\n"));
    }
}
