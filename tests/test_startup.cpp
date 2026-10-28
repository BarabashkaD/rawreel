// Startup and version lines (M4 Task 7a): the FFmpeg status line, the open-error text, the Vulkan
// line, the package list and the --version text and JSON. No FFmpeg is loaded here.

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/startup.h"
#include "core/version_text.h"
#include "dvv/base/error.h"
#include "dvv/base/json.h"
#include "dvv/components/ffmpeg.h"
#include "dvv/media/types.h"

namespace {

using dvv::components::FfmpegStatus;
namespace fs = std::filesystem;

std::string utf8(const fs::path& path) {
    return dvv::media::utf8FromPath(path);
}

dvv::Error errorOf(dvv::Errc code, std::string message) {
    return dvv::Error{.code = code, .message = std::move(message), .where = {}, .cause = nullptr};
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("startup: FFmpeg status line") {
        using rawreel::StatusPaths;
        const fs::path app = fs::path("/") / "opt" / "rawreel";
        const rawreel::FfmpegOrigin origin{
            .appFolder = app, .ffmpegDirVariable = {}, .homeFolder = {}};
        FfmpegStatus status{.core = true,
                            .lavfi = true,
                            .reason = {},
                            .version = "n8.1.3",
                            .licence = "LGPL version 2.1 or later",
                            .directory = (app / "ffmpeg").lexically_normal(),
                            .libraryVersions = {}};
        CHECK(rawreel::ffmpegStatusLine(status, StatusPaths::Shown, origin) ==
              "FFmpeg n8.1.3 (LGPL version 2.1 or later) from "
              "https://github.com/BarabashkaD/custom_ffmpeg");
        status.lavfi = false;
        CHECK(rawreel::ffmpegStatusLine(status, StatusPaths::Shown, origin) ==
              "FFmpeg n8.1.3 (LGPL version 2.1 or later) from "
              "https://github.com/BarabashkaD/custom_ffmpeg, "
              "lavfi not available");
        FfmpegStatus missing;
        missing.reason = "DVV_FFMPEG_DIR must be absolute";
        CHECK(rawreel::ffmpegStatusLine(missing, StatusPaths::Shown, origin) ==
              "FFmpeg: not available (DVV_FFMPEG_DIR must be absolute)");

        // About and the GUI infobar: the status only, no directory and no reason.
        CHECK(rawreel::ffmpegStatusLine(status, StatusPaths::Hidden, origin) ==
              "FFmpeg n8.1.3 (LGPL version 2.1 or later) from "
              "https://github.com/BarabashkaD/custom_ffmpeg, "
              "lavfi not available");
        status.lavfi = true;
        CHECK(rawreel::ffmpegStatusLine(status, StatusPaths::Hidden, origin) ==
              "FFmpeg n8.1.3 (LGPL version 2.1 or later) from "
              "https://github.com/BarabashkaD/custom_ffmpeg");
        missing.reason = "FFmpeg not found (looked in some/dir)";
        CHECK(rawreel::ffmpegStatusLine(missing, StatusPaths::Hidden, origin) ==
              "FFmpeg: not available (details in the log)");
    }

    TEST_CASE("startup: FFmpeg line names the origin only when it is known") {
        using rawreel::StatusPaths;
        const fs::path app = fs::path("/") / "opt" / "rawreel" / "bin";
        rawreel::FfmpegOrigin origin{.appFolder = app, .ffmpegDirVariable = {}, .homeFolder = {}};
        FfmpegStatus status{.core = true,
                            .lavfi = true,
                            .reason = {},
                            .version = "n8.1.3",
                            .licence = "LGPL",
                            .directory = (app / ".." / "lib" / "ffmpeg").lexically_normal(),
                            .libraryVersions = {}};
        const std::string bundled =
            "FFmpeg n8.1.3 (LGPL) from https://github.com/BarabashkaD/custom_ffmpeg";
        CHECK(rawreel::ffmpegStatusLine(status, StatusPaths::Shown, origin) == bundled);

        // Another folder (an explicit directory): no claim about where the build comes from.
        status.directory = (fs::path("/") / "elsewhere" / "ffmpeg").lexically_normal();
        CHECK(rawreel::ffmpegStatusLine(status, StatusPaths::Shown, origin) ==
              "FFmpeg n8.1.3 (LGPL)");

        // DVV_FFMPEG_DIR is final for the loader, so it wins even over a bundled folder; the line
        // names the variable, never its value.
        origin.ffmpegDirVariable = utf8(status.directory);
        CHECK(rawreel::ffmpegStatusLine(status, StatusPaths::Shown, origin) ==
              "FFmpeg n8.1.3 (LGPL) from DVV_FFMPEG_DIR");
        status.directory = (app / "ffmpeg").lexically_normal();
        origin.ffmpegDirVariable = utf8(status.directory);
        CHECK(rawreel::ffmpegStatusLine(status, StatusPaths::Hidden, origin) ==
              "FFmpeg n8.1.3 (LGPL) from DVV_FFMPEG_DIR");

        // The executable's folder unknown: no claim either.
        CHECK(rawreel::ffmpegStatusLine(status, StatusPaths::Shown, rawreel::FfmpegOrigin{}) ==
              "FFmpeg n8.1.3 (LGPL)");
    }

    TEST_CASE("startup: FFmpeg failure reason without machine folders") {
        using rawreel::StatusPaths;
        const fs::path home = fs::path("/") / "home" / "someone";
        const fs::path app = home / "rawreel-0.9.0";
        rawreel::FfmpegOrigin origin{.appFolder = app, .ffmpegDirVariable = {}, .homeFolder = home};
        const std::string hint = "set DVV_FFMPEG_DIR to the folder of an LGPL FFmpeg shared build";

        // Not found: the loader's "looked in" list of the bundled folders.
        FfmpegStatus missing;
        missing.reason =
            "FFmpeg not found (looked in " + utf8((app / "ffmpeg").lexically_normal()) + " and " +
            utf8((app / ".." / "lib" / "ffmpeg").lexically_normal()) + "; " + hint + ")";
        const std::string looked = rawreel::ffmpegStatusLine(missing, StatusPaths::Shown, origin);
        CHECK(looked == "FFmpeg: not available (FFmpeg not found (looked in " +
                            utf8(fs::path("<app>") / "ffmpeg") + " and " +
                            utf8(fs::path("<app>") / ".." / "lib" / "ffmpeg") + "; " + hint + "))");
        CHECK(looked.find("someone") == std::string::npos);

        // A refused build from DVV_FFMPEG_DIR whose error names a library below the home folder.
        const fs::path variable = home / "builds" / "ffmpeg";
        origin.ffmpegDirVariable = utf8(variable);
        const fs::path where = variable.lexically_normal();
        missing.reason = "FFmpeg in " + utf8(where) +
                         " (DVV_FFMPEG_DIR): " + utf8(where / "libavutil.so.60") + ": needs " +
                         utf8(home / "lib" / "libz.so.1");
        const std::string refused = rawreel::ffmpegStatusLine(missing, StatusPaths::Shown, origin);
        CAPTURE(refused);
        CHECK(refused.starts_with(
            "FFmpeg: not available (FFmpeg in $DVV_FFMPEG_DIR (DVV_FFMPEG_DIR): $DVV_FFMPEG_DIR"));
        CHECK(refused.find(utf8(fs::path("~") / "lib" / "libz.so.1")) != std::string::npos);
        CHECK(refused.find("someone") == std::string::npos);

        // A relative variable is masked too; a root folder is never replaced.
        origin.ffmpegDirVariable = "relative/ffmpeg";
        origin.homeFolder = fs::path("/");
        missing.reason = "DVV_FFMPEG_DIR must be absolute (got 'relative/ffmpeg'); /usr/lib";
        CHECK(rawreel::maskFolders(missing.reason, origin) ==
              "DVV_FFMPEG_DIR must be absolute (got '$DVV_FFMPEG_DIR'); /usr/lib");
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
              unsupported.describe() + "; " +
                  rawreel::ffmpegStatusLine(core_only, rawreel::StatusPaths::Hidden));

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
                                            .ffmpeg =
                                                "FFmpeg n8.1.3 (LGPL version 2.1 or later) from "
                                                "https://github.com/BarabashkaD/custom_ffmpeg"};
        const rawreel::AboutContent content = rawreel::aboutContent(report);
        CHECK(content.heading == "rawreel 0.4.0");
        CHECK(content.description ==
              "Play and inspect raw and Y4M video frame by frame: pixel formats and colour "
              "mapping.");
        using Line = rawreel::TextLine;
        CHECK(content.copyright ==
              Line{{"Copyright (c) 2026 BarabashkaD \xC2\xB7 ", ""},
                   {"MIT licence", "https://github.com/BarabashkaD/rawreel/blob/main/LICENSE"}});
        CHECK(content.website == "https://github.com/BarabashkaD/rawreel");
        // No GPU line: Help -> GPU information shows it.
        CHECK(content.system ==
              std::vector<Line>{
                  {{"FFmpeg n8.1.3", "https://github.com/BarabashkaD/custom_ffmpeg"},
                   {" (", ""},
                   {"LGPL 2.1 or later", "https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html"},
                   {")", ""}},
                  {{"wxWidgets 3.3.1", "https://www.wxwidgets.org"},
                   {" (", ""},
                   {"wxWindows Licence", "https://www.wxwidgets.org/about/licence/"},
                   {")", ""}}});
        using Lines = std::vector<std::pair<std::string, std::string>>;
        CHECK(content.components == Lines{{"dvv-base", "0.4.1"}, {"dvv-gpu", "0.4.2"}});
        const std::string text = rawreel::aboutText(content);
        CHECK(text ==
              "rawreel 0.4.0\n"
              "Play and inspect raw and Y4M video frame by frame: pixel formats and colour "
              "mapping.\n"
              "Copyright (c) 2026 BarabashkaD \xC2\xB7 MIT licence "
              "(https://github.com/BarabashkaD/rawreel/blob/main/LICENSE)\n"
              "https://github.com/BarabashkaD/rawreel\n"
              "FFmpeg n8.1.3 (https://github.com/BarabashkaD/custom_ffmpeg) (LGPL 2.1 or later "
              "(https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html))\n"
              "wxWidgets 3.3.1 (https://www.wxwidgets.org) (wxWindows Licence "
              "(https://www.wxwidgets.org/about/licence/))\n"
              "Components:\n"
              "  dvv-base 0.4.1\n"
              "  dvv-gpu 0.4.2\n");
        // The name and version once; no GPU and no appearance.
        CHECK(text.find("rawreel 0.4.0") == text.rfind("rawreel 0.4.0"));
        CHECK_FALSE(text.contains("Vulkan"));
        CHECK_FALSE(text.contains("appearance"));
    }

    TEST_CASE("version: About FFmpeg line of other builds") {
        const auto ffmpeg_line = [](std::string line) {
            const rawreel::VersionReport report{
                .packages = {{"rawreel", "0.4.0"}}, .vulkan = {}, .ffmpeg = std::move(line)};
            return rawreel::plainText(rawreel::aboutContent(report).system.front());
        };
        CHECK(ffmpeg_line("FFmpeg n8.1.3 (LGPL version 3 or later) from DVV_FFMPEG_DIR, lavfi not "
                          "available") ==
              "FFmpeg n8.1.3 (https://github.com/BarabashkaD/custom_ffmpeg) (LGPL 3 or later "
              "(https://www.gnu.org/licenses/lgpl-3.0.html)) from DVV_FFMPEG_DIR, lavfi not "
              "available");
        // An unknown licence stays plain text.
        CHECK(ffmpeg_line("FFmpeg 7.1 (nonfree and unredistributable)") ==
              "FFmpeg 7.1 (https://github.com/BarabashkaD/custom_ffmpeg) (nonfree and "
              "unredistributable)");
    }

    TEST_CASE("version: About content when FFmpeg is not available") {
        const rawreel::VersionReport report{.packages = {{"rawreel", "0.4.0"}},
                                            .vulkan = "Vulkan: not available (no device)",
                                            .ffmpeg = "FFmpeg: not available (details in the log)"};
        const rawreel::AboutContent content = rawreel::aboutContent(report);
        using Line = rawreel::TextLine;
        CHECK(content.system ==
              std::vector<Line>{{{"FFmpeg not available (details in the log)", ""}},
                                {{"wxWidgets not part of this build", "https://www.wxwidgets.org"},
                                 {" (", ""},
                                 {"wxWindows Licence", "https://www.wxwidgets.org/about/licence/"},
                                 {")", ""}}});
        CHECK(content.components.empty());
        CHECK(rawreel::aboutText(content).ends_with("Components:\n  none\n"));
    }
}
