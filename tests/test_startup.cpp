// Startup and version lines (M4 Task 7a): the FFmpeg status line, the open-error text, the Vulkan
// line, the package list and the --version text and JSON. No FFmpeg is loaded here: the FFmpeg
// module's status is built by hand.

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/startup.h"
#include "core/version_text.h"
#include "dvv/base/error.h"
#include "dvv/base/executors.h"
#include "dvv/base/json.h"
#include "dvv/base/task.h"
#include "dvv/base/uri.h"
#include "dvv/base/utf8.h"
#include "dvv/components/ffmpeg.h"
#include "dvv/media/source_factory.h"
#include "dvv/registry/capabilities.h"
#include "dvv/registry/component_registry.h"
#include "dvv/registry/module.h"

namespace {

namespace fs = std::filesystem;

constexpr std::array<std::string_view, 2> FFMPEG_PRODUCES{dvv::components::FFMPEG_CAPABILITY,
                                                          dvv::components::FFMPEG_LAVFI_CAPABILITY};
constexpr std::array<std::string_view, 1> OTHER_PRODUCES{"other.thing"};

std::string utf8(const fs::path& path) {
    return dvv::pathDisplayText(path);
}

dvv::Error errorOf(dvv::Errc code, std::string message) {
    return dvv::Error{.code = code, .message = std::move(message), .where = {}, .cause = nullptr};
}

// The FFmpeg module's status with both capabilities, as ComponentRegistry::load() reports it.
dvv::ModuleStatus loadedFfmpeg(std::string version, std::string licence,
                               const fs::path& directory) {
    return dvv::ModuleStatus{
        .info = {.id = dvv::components::FFMPEG_MODULE,
                 .name = "FFmpeg",
                 .produces = FFMPEG_PRODUCES},
        .provides = {std::string(dvv::components::FFMPEG_CAPABILITY),
                     std::string(dvv::components::FFMPEG_LAVFI_CAPABILITY)},
        .withheld = {},
        .reason = {},
        .properties = {dvv::ModuleProperty{.key = std::string(dvv::MODULE_VERSION_PROPERTY),
                                           .value = std::move(version)},
                       dvv::ModuleProperty{.key = std::string(dvv::MODULE_LICENCE_PROPERTY),
                                           .value = std::move(licence)},
                       dvv::ModuleProperty{.key = std::string(dvv::MODULE_DIRECTORY_PROPERTY),
                                           .value = dvv::pathText(directory)}}};
}

// `status` with lavfi withheld (an FFmpeg build without avfilter).
dvv::ModuleStatus coreOnly(dvv::ModuleStatus status, std::string reason) {
    status.provides = {std::string(dvv::components::FFMPEG_CAPABILITY)};
    status.withheld = {std::string(dvv::components::FFMPEG_LAVFI_CAPABILITY)};
    status.reason = std::move(reason);
    return status;
}

// No FFmpeg: both capabilities withheld, no properties.
dvv::ModuleStatus missingFfmpeg(std::string reason) {
    return dvv::ModuleStatus{.info = {.id = dvv::components::FFMPEG_MODULE,
                                      .name = "FFmpeg",
                                      .produces = FFMPEG_PRODUCES},
                             .provides = {},
                             .withheld = {std::string(dvv::components::FFMPEG_CAPABILITY),
                                          std::string(dvv::components::FFMPEG_LAVFI_CAPABILITY)},
                             .reason = std::move(reason),
                             .properties = {}};
}

// `status` as the module reports it when DVV_FFMPEG_DIR chose the folder.
dvv::ModuleStatus fromVariable(dvv::ModuleStatus status) {
    status.properties.push_back(dvv::ModuleProperty{
        .key = std::string(dvv::MODULE_VARIABLE_PROPERTY), .value = "DVV_FFMPEG_DIR"});
    return status;
}

// Another module, which withholds a capability no Source provider needs.
dvv::ModuleStatus otherModule() {
    return dvv::ModuleStatus{.info = {.id = "other", .name = "Other", .produces = OTHER_PRODUCES},
                             .provides = {},
                             .withheld = {"other.thing"},
                             .reason = "other.thing is missing",
                             .properties = {}};
}

// The lavfi provider as the registry lists it without FFmpeg.
dvv::ComponentStatus lavfiUnavailable() {
    return dvv::ComponentStatus{
        .info = {.id = "lavfi", .name = "lavfi"},
        .unavailable =
            dvv::Unavailable{.reason = "missing ffmpeg.lavfi",
                             .capability = std::string(dvv::components::FFMPEG_LAVFI_CAPABILITY)}};
}

// The FFmpeg container provider as the registry lists it without FFmpeg.
dvv::ComponentStatus ffmpegUnavailable() {
    return dvv::ComponentStatus{.info = {.id = "ffmpeg", .name = "ffmpeg"},
                                .unavailable = dvv::Unavailable{
                                    .reason = "missing ffmpeg",
                                    .capability = std::string(dvv::components::FFMPEG_CAPABILITY)}};
}

// The scheme of the request that `text` opens ("file" for a path).
std::string schemeOf(std::string_view text) {
    const dvv::Result<dvv::Uri> uri = dvv::parseUri(text);
    CHECK(uri.has_value());
    return uri ? uri->scheme : std::string();
}

// A real no-match: a SourceFactory on a registry without capabilities opens lavfi:testsrc2.
dvv::Error noProviderMatch() {
    dvv::Error error = errorOf(dvv::Errc::Internal, "the open did not run");
    std::shared_ptr<dvv::ThreadPool> io;
    if (auto pool = dvv::ExecutorFactory::makeThreadPool(1); pool) {
        io = std::move(*pool);
    }
    REQUIRE(io != nullptr);
    const std::unique_ptr<dvv::ComponentRegistry> registry =
        dvv::ComponentRegistry::create(dvv::Capabilities{});
    REQUIRE(registry != nullptr);
    auto factory = dvv::media::SourceFactory::create(*registry, io);
    CHECK(factory.has_value());
    auto uri = dvv::parseUri("lavfi:testsrc2");
    CHECK(uri.has_value());
    if (factory && uri) {
        const auto opened = dvv::syncWait(
            (*factory)->open(dvv::media::SourceRequest{.uri = std::move(*uri), .hint = {}}));
        CHECK_FALSE(opened.has_value());
        if (!opened) {
            error = opened.error();
        }
    }
    CAPTURE(error.describe());
    REQUIRE(dvv::media::isNoProviderMatch(error));
    return error;
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("startup: FFmpeg status line") {
        using rawreel::StatusPaths;
        const fs::path app = fs::path("/") / "opt" / "rawreel";
        const rawreel::FfmpegOrigin origin{
            .appFolder = app, .ffmpegDirVariable = {}, .homeFolder = {}};
        const dvv::ModuleStatus loaded = loadedFfmpeg("n8.1.3", "LGPL version 2.1 or later",
                                                      (app / "ffmpeg").lexically_normal());
        CHECK(rawreel::ffmpegStatusLine(&loaded, StatusPaths::Shown, origin) ==
              "FFmpeg n8.1.3 (LGPL version 2.1 or later) from "
              "https://github.com/BarabashkaD/custom_ffmpeg");
        const dvv::ModuleStatus core_only = coreOnly(loaded, "avfilter missing");
        CHECK(rawreel::ffmpegStatusLine(&core_only, StatusPaths::Shown, origin) ==
              "FFmpeg n8.1.3 (LGPL version 2.1 or later) from "
              "https://github.com/BarabashkaD/custom_ffmpeg, "
              "lavfi not available");
        const dvv::ModuleStatus missing = missingFfmpeg("not found");
        CHECK(rawreel::ffmpegStatusLine(&missing, StatusPaths::Shown, origin) ==
              "FFmpeg: not available (not found)");
        const dvv::ModuleStatus relative = missingFfmpeg("DVV_FFMPEG_DIR must be absolute");
        CHECK(rawreel::ffmpegStatusLine(&relative, StatusPaths::Shown, origin) ==
              "FFmpeg: not available (DVV_FFMPEG_DIR must be absolute)");

        // About and the GUI infobar: the status only, no directory and no reason.
        CHECK(rawreel::ffmpegStatusLine(&core_only, StatusPaths::Hidden, origin) ==
              "FFmpeg n8.1.3 (LGPL version 2.1 or later) from "
              "https://github.com/BarabashkaD/custom_ffmpeg, "
              "lavfi not available");
        CHECK(rawreel::ffmpegStatusLine(&loaded, StatusPaths::Hidden, origin) ==
              "FFmpeg n8.1.3 (LGPL version 2.1 or later) from "
              "https://github.com/BarabashkaD/custom_ffmpeg");
        const dvv::ModuleStatus looked = missingFfmpeg("FFmpeg not found (looked in some/dir)");
        CHECK(rawreel::ffmpegStatusLine(&looked, StatusPaths::Hidden, origin) ==
              "FFmpeg: not available (details in the log)");
    }

    TEST_CASE("startup: status line: no FFmpeg module") {
        using rawreel::StatusPaths;
        CHECK(rawreel::ffmpegStatusLine(nullptr) == "FFmpeg: not available (not in this build)");
        CHECK(rawreel::ffmpegStatusLine(nullptr, StatusPaths::Hidden) ==
              "FFmpeg: not available (not in this build)");
    }

    TEST_CASE("startup: the macOS bundle's Frameworks/ffmpeg folder") {
        using rawreel::StatusPaths;
        const fs::path home = fs::path("/") / "Users" / "someone";
        const fs::path app = home / "Applications" / "rawreel.app" / "Contents" / "MacOS";
        const rawreel::FfmpegOrigin origin{
            .appFolder = app, .ffmpegDirVariable = {}, .homeFolder = home};
        const std::string frameworks =
            utf8((app / ".." / "Frameworks" / "ffmpeg").lexically_normal());

        // Masked like <app>/../lib/ffmpeg.
        CHECK(rawreel::maskFolders("not found in " + frameworks + "/libavutil.dylib", origin) ==
              "not found in " + utf8(fs::path("<app>") / ".." / "Frameworks" / "ffmpeg") +
                  "/libavutil.dylib");

        // And recognised as a bundled folder by the status line.
        const dvv::ModuleStatus status = loadedFfmpeg(
            "n8.1.3", "LGPL", (app / ".." / "Frameworks" / "ffmpeg").lexically_normal());
        CHECK(rawreel::ffmpegStatusLine(&status, StatusPaths::Shown, origin) ==
              "FFmpeg n8.1.3 (LGPL) from https://github.com/BarabashkaD/custom_ffmpeg");
    }

    TEST_CASE("startup: FFmpeg line names the origin only when it is known") {
        using rawreel::StatusPaths;
        const fs::path app = fs::path("/") / "opt" / "rawreel" / "bin";
        rawreel::FfmpegOrigin origin{.appFolder = app, .ffmpegDirVariable = {}, .homeFolder = {}};
        const dvv::ModuleStatus bundled_status =
            loadedFfmpeg("n8.1.3", "LGPL", (app / ".." / "lib" / "ffmpeg").lexically_normal());
        const std::string bundled =
            "FFmpeg n8.1.3 (LGPL) from https://github.com/BarabashkaD/custom_ffmpeg";
        CHECK(rawreel::ffmpegStatusLine(&bundled_status, StatusPaths::Shown, origin) == bundled);

        // Another folder (an explicit directory): no claim about where the build comes from.
        const fs::path elsewhere_folder =
            (fs::path("/") / "elsewhere" / "ffmpeg").lexically_normal();
        const dvv::ModuleStatus elsewhere = loadedFfmpeg("n8.1.3", "LGPL", elsewhere_folder);
        CHECK(rawreel::ffmpegStatusLine(&elsewhere, StatusPaths::Shown, origin) ==
              "FFmpeg n8.1.3 (LGPL)");

        // DVV_FFMPEG_DIR is final for the loader, so it wins even over a bundled folder; the line
        // names the variable, never its value. The module says when the variable chose the folder.
        const dvv::ModuleStatus chosen = fromVariable(elsewhere);
        CHECK(rawreel::ffmpegStatusLine(&chosen, StatusPaths::Shown, origin) ==
              "FFmpeg n8.1.3 (LGPL) from DVV_FFMPEG_DIR");
        const dvv::ModuleStatus chosen_bundled =
            fromVariable(loadedFfmpeg("n8.1.3", "LGPL", (app / "ffmpeg").lexically_normal()));
        CHECK(rawreel::ffmpegStatusLine(&chosen_bundled, StatusPaths::Hidden, origin) ==
              "FFmpeg n8.1.3 (LGPL) from DVV_FFMPEG_DIR");

        // The environment is not read for this: without the module's property, the variable's
        // value in the origin claims nothing.
        origin.ffmpegDirVariable = utf8(elsewhere_folder);
        CHECK(rawreel::ffmpegStatusLine(&elsewhere, StatusPaths::Shown, origin) ==
              "FFmpeg n8.1.3 (LGPL)");

        // The executable's folder unknown: no claim either.
        const dvv::ModuleStatus next_to_app =
            loadedFfmpeg("n8.1.3", "LGPL", (app / "ffmpeg").lexically_normal());
        CHECK(rawreel::ffmpegStatusLine(&next_to_app, StatusPaths::Shown,
                                        rawreel::FfmpegOrigin{}) == "FFmpeg n8.1.3 (LGPL)");
    }

    TEST_CASE("startup: FFmpeg failure reason without machine folders") {
        using rawreel::StatusPaths;
        const fs::path home = fs::path("/") / "home" / "someone";
        const fs::path app = home / "rawreel-0.9.0";
        rawreel::FfmpegOrigin origin{.appFolder = app, .ffmpegDirVariable = {}, .homeFolder = home};
        const std::string hint = "set DVV_FFMPEG_DIR to the folder of an LGPL FFmpeg shared build";

        // Not found: the loader's "looked in" list of the bundled folders.
        const dvv::ModuleStatus missing = missingFfmpeg(
            "FFmpeg not found (looked in " + utf8((app / "ffmpeg").lexically_normal()) + " and " +
            utf8((app / ".." / "lib" / "ffmpeg").lexically_normal()) + "; " + hint + ")");
        const std::string looked = rawreel::ffmpegStatusLine(&missing, StatusPaths::Shown, origin);
        CHECK(looked == "FFmpeg: not available (FFmpeg not found (looked in " +
                            utf8(fs::path("<app>") / "ffmpeg") + " and " +
                            utf8(fs::path("<app>") / ".." / "lib" / "ffmpeg") + "; " + hint + "))");
        CHECK(looked.find("someone") == std::string::npos);

        // A refused build from DVV_FFMPEG_DIR whose error names a library below the home folder.
        const fs::path variable = home / "builds" / "ffmpeg";
        origin.ffmpegDirVariable = utf8(variable);
        const fs::path where = variable.lexically_normal();
        const dvv::ModuleStatus refused_status = missingFfmpeg(
            "FFmpeg in " + utf8(where) + " (DVV_FFMPEG_DIR): " + utf8(where / "libavutil.so.60") +
            ": needs " + utf8(home / "lib" / "libz.so.1"));
        const std::string refused =
            rawreel::ffmpegStatusLine(&refused_status, StatusPaths::Shown, origin);
        CAPTURE(refused);
        CHECK(refused.starts_with(
            "FFmpeg: not available (FFmpeg in $DVV_FFMPEG_DIR (DVV_FFMPEG_DIR): $DVV_FFMPEG_DIR"));
        CHECK(refused.find(utf8(fs::path("~") / "lib" / "libz.so.1")) != std::string::npos);
        CHECK(refused.find("someone") == std::string::npos);

        // A relative variable is masked too; a root folder is never replaced.
        origin.ffmpegDirVariable = "relative/ffmpeg";
        origin.homeFolder = fs::path("/");
        CHECK(rawreel::maskFolders(
                  "DVV_FFMPEG_DIR must be absolute (got 'relative/ffmpeg'); /usr/lib", origin) ==
              "DVV_FFMPEG_DIR must be absolute (got '$DVV_FFMPEG_DIR'); /usr/lib");
    }

#if !defined(_WIN32)
    TEST_CASE("masks: a non-UTF-8 home folder is masked") {
        const rawreel::FfmpegOrigin origin{.appFolder = {},
                                           .ffmpegDirVariable = {},
                                           .homeFolder = dvv::pathFromText("/tmp/h\xFF"
                                                                           "me")};
        const std::string text =
            "not found in " + dvv::pathDisplayText(origin.homeFolder / "ffmpeg");
        CHECK(rawreel::maskFolders(text, origin) == "not found in ~/ffmpeg");
    }
#endif

    TEST_CASE("startup: open error text") {
        const dvv::Error no_match = noProviderMatch();
        const std::vector<dvv::ComponentStatus> providers{lavfiUnavailable()};
        const fs::path folder = fs::path("ffmpeg");
        const std::string lavfi = schemeOf("lavfi:testsrc2");

        // A module withholds the capability the unavailable provider misses, and that provider
        // opens the request's scheme: its line, and only its line, follows the error.
        const std::vector<dvv::ModuleStatus> core_only{
            coreOnly(loadedFfmpeg("n8.1.3", "LGPL", folder), "avfilter missing"), otherModule()};
        CHECK(rawreel::openErrorText(no_match, lavfi, core_only, providers) ==
              no_match.describe() + "; " + rawreel::ffmpegStatusLine(core_only.data()));
        CHECK(rawreel::openErrorText(no_match, lavfi, core_only, providers,
                                     rawreel::StatusPaths::Hidden) ==
              no_match.describe() + "; " +
                  rawreel::ffmpegStatusLine(core_only.data(), rawreel::StatusPaths::Hidden));

        // lavfi withheld and a request of another scheme: no suffix.
        CHECK(rawreel::openErrorText(no_match, schemeOf("synthetc:x"), core_only, providers) ==
              no_match.describe());
        CHECK(rawreel::openErrorText(no_match, schemeOf("notes.txt"), core_only, providers) ==
              no_match.describe());

        // FFmpeg missing: a file or lavfi: request ends with the FFmpeg line, other schemes not.
        const std::vector<dvv::ModuleStatus> missing{missingFfmpeg("not found")};
        const std::vector<dvv::ComponentStatus> both{ffmpegUnavailable(), lavfiUnavailable()};
        CHECK(rawreel::openErrorText(no_match, schemeOf("clip.mkv"), missing, both) ==
              no_match.describe() + "; FFmpeg: not available (not found)");
        CHECK(rawreel::openErrorText(no_match, lavfi, missing, both) ==
              no_match.describe() + "; FFmpeg: not available (not found)");
        CHECK(rawreel::openErrorText(no_match, schemeOf("synthetc:x"), missing, both) ==
              no_match.describe());

        // Not a no-match: no suffix.
        const dvv::Error format = errorOf(dvv::Errc::Format, "bad header");
        CHECK(rawreel::openErrorText(format, lavfi, core_only, providers) == format.describe());
        const dvv::Error unsupported =
            errorOf(dvv::Errc::Unsupported, "no source provider for scheme 'lavfi'");
        CHECK(rawreel::openErrorText(unsupported, lavfi, missing, both) == unsupported.describe());

        // The module provides both: no suffix.
        const std::vector<dvv::ModuleStatus> loaded{loadedFfmpeg("n8.1.3", "LGPL", folder)};
        CHECK(rawreel::openErrorText(no_match, lavfi, loaded, providers) == no_match.describe());
    }

    TEST_CASE("startup: open error message for --json") {
        const dvv::Error no_match = noProviderMatch();
        const std::vector<dvv::ComponentStatus> providers{lavfiUnavailable()};
        const std::string lavfi = schemeOf("lavfi:testsrc2");
        const std::vector<dvv::ModuleStatus> core_only{
            coreOnly(loadedFfmpeg("n8.1.3", "LGPL", fs::path("ffmpeg")), "avfilter missing")};
        CHECK(rawreel::openErrorMessage(no_match, lavfi, core_only, providers) ==
              no_match.message + "; " + rawreel::ffmpegStatusLine(core_only.data()));
        CHECK(rawreel::openErrorMessage(no_match, schemeOf("notes.txt"), core_only, providers) ==
              no_match.message);
        const std::vector<dvv::ModuleStatus> missing{missingFfmpeg("not found")};
        const std::vector<dvv::ComponentStatus> both{ffmpegUnavailable(), lavfiUnavailable()};
        CHECK(rawreel::openErrorMessage(no_match, schemeOf("clip.mkv"), missing, both) ==
              no_match.message + "; FFmpeg: not available (not found)");
        const dvv::Error format = errorOf(dvv::Errc::Format, "bad header");
        CHECK(rawreel::openErrorMessage(format, lavfi, missing, both) == "bad header");
    }

    TEST_CASE("version: Vulkan line") {
        CHECK(rawreel::vulkanStatusLine({.available = true,
                                         .device = "llvmpipe",
                                         .api = "1.4.318",
                                         .driver = {},
                                         .reason = {}}) == "Vulkan: llvmpipe, API 1.4.318");
        CHECK(rawreel::vulkanStatusLine({.available = true,
                                         .device = "Apple M4",
                                         .api = "1.4.363",
                                         .driver = "KosmicKrisp 26.1.0",
                                         .reason = {}}) ==
              "Vulkan: Apple M4, API 1.4.363, driver KosmicKrisp 26.1.0");
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
