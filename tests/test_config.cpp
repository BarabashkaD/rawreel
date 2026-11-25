// The config file loader (M3.1): wx-free, so it is tested in rawreel_core.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "core/config.h"
#include "core/config_writer.h"
#include "core/native_os.h"
#include "core/overlay.h"
#include "core/recent.h"
#include "dvv/base/error.h"
#include "dvv/base/json.h"
#include "dvv/base/log.h"
#include "dvv/base/paths.h"
#include "dvv/base/utf8.h"
#include "dvv/platform/file_reader.h"
#include "temporary_directory.h"

namespace fs = std::filesystem;
using dvv::platform::FileReadMode;
using rawreel::Config;
using rawreel::LoadedConfig;

namespace {

void writeText(const fs::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    file << text;
    REQUIRE(file.good());
}

// Captures warnings for one test; restores the logger afterwards (the tests add no other sinks).
class WarningLog {
public:
    explicit WarningLog(dvv::LogLevel level = dvv::LogLevel::Warn)
        : level_(dvv::Logger::instance().level()) {
        dvv::Logger::instance().clearSinks();
        dvv::Logger::instance().addSink(sink_);
        dvv::Logger::instance().setLevel(level);
    }
    WarningLog(const WarningLog&) = delete;
    WarningLog& operator=(const WarningLog&) = delete;
    WarningLog(WarningLog&&) = delete;
    WarningLog& operator=(WarningLog&&) = delete;
    ~WarningLog() {
        dvv::Logger::instance().clearSinks();
        dvv::Logger::instance().setLevel(level_);
    }

    [[nodiscard]] std::vector<std::string> lines() const { return sink_->lines(); }

private:
    dvv::LogLevel level_;
    std::shared_ptr<dvv::MemorySink> sink_ = std::make_shared<dvv::MemorySink>();
};

bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

// The parse must fail with Errc::Format and a message containing `part`.
void checkInvalid(std::string_view text, std::string_view part) {
    CAPTURE(text);
    const dvv::Result<Config> config = rawreel::parseConfig(text);
    REQUIRE_FALSE(config.has_value());
    CHECK(config.error().code == dvv::Errc::Format);
    CHECK_MESSAGE(contains(config.error().message, part), config.error().message);
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("config: the fileReads names") {
        for (const FileReadMode mode :
             {FileReadMode::Auto, FileReadMode::Unbuffered, FileReadMode::Buffered}) {
            CHECK(rawreel::parseFileReads(rawreel::fileReadsName(mode)) == mode);
        }
        CHECK(rawreel::fileReadsName(FileReadMode::Auto) == "auto");
        CHECK(rawreel::fileReadsName(FileReadMode::Unbuffered) == "unbuffered");
        CHECK(rawreel::fileReadsName(FileReadMode::Buffered) == "buffered");
        CHECK_FALSE(rawreel::parseFileReads("Auto").has_value());
        CHECK_FALSE(rawreel::parseFileReads("").has_value());
    }

    TEST_CASE("config: parse") {
        CHECK(Config{}.io.fileReads == FileReadMode::Auto);
        const std::vector<std::pair<std::string_view, FileReadMode>> cases{
            {R"({"io": {"fileReads": "auto"}})", FileReadMode::Auto},
            {R"({"io": {"fileReads": "unbuffered"}})", FileReadMode::Unbuffered},
            {R"({"io": {"fileReads": "buffered"}})", FileReadMode::Buffered},
            {R"({})", FileReadMode::Auto},
            {R"({"io": {}})", FileReadMode::Auto},
            // Unknown keys are ignored: a newer file still loads.
            {R"({"io": {"fileReads": "buffered", "other": 1}, "ui": {"theme": "dark"}})",
             FileReadMode::Buffered},
        };
        for (const auto& [text, mode] : cases) {
            CAPTURE(text);
            const dvv::Result<Config> config = rawreel::parseConfig(text);
            REQUIRE_MESSAGE(config.has_value(), config.error().message);
            CHECK(config->io.fileReads == mode);
        }
    }

    TEST_CASE("config: the gui and log keys") {
        const auto parse = [](std::string_view text) {
            CAPTURE(text);
            dvv::Result<Config> config = rawreel::parseConfig(text);
            REQUIRE_MESSAGE(config.has_value(), config.error().message);
            return *config;
        };
        const Config defaults = parse("{}");
        CHECK_FALSE(defaults.gui.showLogPane);
        CHECK(defaults.log.file.enabled);
        CHECK(defaults.log.file.folder.empty());
        CHECK_FALSE(defaults.log.system.enabled);

        CHECK(parse(R"({"gui":{"showLogPane":true}})").gui.showLogPane);

        const fs::path folder = fs::temp_directory_path() / "rawreel logs";
        const std::string text = std::format(
            R"({{"log":{{"file":{{"enabled":false,"folder":{}}},"system":{{"enabled":true}}}}}})",
            dvv::json::dump(dvv::json::Value(dvv::pathText(folder))));
        const Config set = parse(text);
        CHECK_FALSE(set.log.file.enabled);
        CHECK(set.log.file.folder == folder);
        CHECK(set.log.system.enabled);

        // "" is the default folder (Use Default writes it).
        const Config empty = parse(R"({"log":{"file":{"folder":""}}})");
        CHECK(empty.log.file.folder.empty());
        CHECK(empty.log.file.enabled);
    }

    TEST_CASE("config: log.level and log.source") {
        const auto parse = [](std::string_view text) {
            CAPTURE(text);
            dvv::Result<Config> config = rawreel::parseConfig(text);
            REQUIRE_MESSAGE(config.has_value(), config.error().message);
            return *config;
        };
        const Config set = parse(R"({"log":{"level":"Debug","source":true}})");
        CHECK(set.log.level == dvv::LogLevel::Debug);
        CHECK(set.log.levelSaved);
        CHECK(set.log.source);
        const Config defaults = parse("{}");
        CHECK(defaults.log.level == dvv::LogLevel::Info);
        CHECK_FALSE(defaults.log.levelSaved);
        CHECK_FALSE(defaults.log.source);
        CHECK_FALSE(parse(R"({"log":{"source":false}})").log.levelSaved);
        checkInvalid(
            R"({"log":{"level":"verbose"}})",
            R"(log.level: expected "error", "warn", "info", "debug" or "trace", not "verbose")");
        checkInvalid(R"({"log":{"level":3}})", "log.level: expected");
        checkInvalid(R"({"log":{"source":1}})", "log.source: expected true or false, not 1");
    }

    TEST_CASE("config: the info line names log.level") {
        const TemporaryDirectory directory("rawreel-config");
        const fs::path path = directory.path() / "rawreel.json";
        writeText(path, R"({"log":{"level":"debug"}})");
        const WarningLog log(dvv::LogLevel::Info);
        const LoadedConfig loaded = rawreel::loadConfig(path);
        CHECK(loaded.found);
        const std::vector<std::string> lines = log.lines();
        REQUIRE_FALSE(lines.empty());
        CHECK(contains(lines.back(), "log.level debug"));
    }

    TEST_CASE("config: invalid text and values") {
        checkInvalid("", "");
        checkInvalid(R"({"io": )", "");
        checkInvalid("[1, 2]", "the top level is not a JSON object");
        checkInvalid(R"({"io": "buffered"})", "io: expected an object");
        checkInvalid(R"({"io": {"fileReads": "fast"}})",
                     R"(io.fileReads: expected "auto", "unbuffered" or "buffered", not "fast")");
        checkInvalid(R"({"io": {"fileReads": 1}})", "io.fileReads: expected");
        checkInvalid(R"({"io": {"fileReads": null}})", "io.fileReads: expected");
        checkInvalid(R"({"gui":1})", "gui: expected an object");
        checkInvalid(R"({"gui":{"showLogPane":"yes"}})",
                     R"(gui.showLogPane: expected true or false, not "yes")");
        checkInvalid(R"({"log":1})", "log: expected an object");
        checkInvalid(R"({"log":{"file":{"folder":"rel/dir"}}})",
                     R"(log.file.folder: expected an absolute folder or "", not "rel/dir")");
        checkInvalid(R"({"log":{"file":{"enabled":1}}})",
                     "log.file.enabled: expected true or false, not 1");
        checkInvalid(R"({"log":{"system":{"enabled":0}}})",
                     "log.system.enabled: expected true or false, not 0");
    }

    TEST_CASE("config: a missing file gives the defaults without a warning") {
        const TemporaryDirectory directory("rawreel-config");
        const WarningLog log;
        const LoadedConfig loaded = rawreel::loadConfig(directory.path() / "rawreel.json");
        CHECK_FALSE(loaded.found);
        CHECK(loaded.warning.empty());
        CHECK(loaded.config.io.fileReads == FileReadMode::Auto);
        CHECK(loaded.path == directory.path() / "rawreel.json");
        CHECK(log.lines().empty());
    }

    TEST_CASE("config: a valid file") {
        const TemporaryDirectory directory("rawreel-config");
        const fs::path path = directory.path() / "rawreel.json";
        writeText(path, "{\n  \"io\": { \"fileReads\": \"buffered\" }\n}\n");
        const WarningLog log;
        const LoadedConfig loaded = rawreel::loadConfig(path);
        CHECK(loaded.found);
        CHECK(loaded.warning.empty());
        CHECK(loaded.config.io.fileReads == FileReadMode::Buffered);
        CHECK(log.lines().empty());
    }

    TEST_CASE("config: an invalid file gives one warning and the defaults") {
        const TemporaryDirectory directory("rawreel-config");
        const fs::path path = directory.path() / "rawreel.json";
        std::string reason;
        SUBCASE("invalid JSON") {
            writeText(path, R"({"io": {"fileReads": "buffered")");
            reason = "";
        }
        SUBCASE("an unknown value") {
            writeText(path, R"({"io": {"fileReads": "fast"}})");
            reason = R"(not "fast")";
        }
        SUBCASE("a wrong type") {
            writeText(path, R"({"io": ["buffered"]})");
            reason = "io: expected an object";
        }
        SUBCASE("a directory") {
            fs::create_directory(path);
            reason = "";
        }
        CAPTURE(reason);
        const WarningLog log;
        const LoadedConfig loaded = rawreel::loadConfig(path);
        CHECK(loaded.found);
        CHECK(loaded.config.io.fileReads == FileReadMode::Auto);
        CHECK_FALSE(loaded.warning.empty());
        CHECK(loaded.text.empty());
        CHECK(contains(loaded.warning, reason));
        const std::vector<std::string> lines = log.lines();
        REQUIRE(lines.size() == 1);
        CHECK(contains(lines[0], "WARN"));
        CHECK(contains(lines[0], "rawreel.json' ignored: "));
        CHECK(contains(lines[0], "using the defaults"));
    }

    TEST_CASE("config: the history keys are not parseConfig's") {
        const TemporaryDirectory directory("rawreel-config");
        const fs::path path = directory.path() / "rawreel.json";
        const std::string text =
            R"({"io":{"fileReads":"buffered"},"gui":{"showLogPane":true,"recentSize":"x","recent":3}})";
        writeText(path, text);
        const WarningLog log;
        const LoadedConfig loaded = rawreel::loadConfig(path);
        CHECK(loaded.found);
        CHECK(loaded.warning.empty());
        CHECK(loaded.config.io.fileReads == FileReadMode::Buffered);
        CHECK(loaded.config.gui.showLogPane);
        CHECK(loaded.text == text);
        CHECK(log.lines().empty());
    }

    // --config <file> (UTF-8) replaces the file next to the executable.
    TEST_CASE("config: the file --config names") {
        const TemporaryDirectory directory("rawreel-config");
        const fs::path path = directory.path() / fs::path(u8"my config ü.json");
        writeText(path, R"({"io": {"fileReads": "unbuffered"}})");
        const LoadedConfig loaded =
            rawreel::loadConfig(std::optional<std::string>(dvv::pathText(path)));
        CHECK(loaded.found);
        CHECK(loaded.path == path);
        CHECK(loaded.config.io.fileReads == FileReadMode::Unbuffered);
    }

    // A file the user named must exist: a missing one is one warning (the default file is silent).
    TEST_CASE("config: a missing --config file gives one warning and the defaults") {
        const TemporaryDirectory directory("rawreel-config");
        const WarningLog log;
        const LoadedConfig missing = rawreel::loadConfig(
            std::optional<std::string>(dvv::pathText(directory.path() / "none.json")));
        CHECK_FALSE(missing.found);
        CHECK(missing.config.io.fileReads == FileReadMode::Auto);
        CHECK(missing.warning == "not found");
        const std::vector<std::string> lines = log.lines();
        REQUIRE(lines.size() == 1);
        CHECK(contains(lines[0], "WARN"));
        CHECK(contains(lines[0], "none.json' ignored: not found; using the defaults"));
    }

    TEST_CASE("config: a message about a non-UTF-8 config file shows U+FFFD") {
        if constexpr (rawreel::NATIVE_OS != rawreel::Os::Windows) {
            const WarningLog log;
            const LoadedConfig missing =
                rawreel::loadConfig(std::optional<std::string>{"/tmp/rawreel-missing-\xFF"
                                                               "x.json"});
            CHECK_FALSE(missing.found);
            const std::vector<std::string> lines = log.lines();
            REQUIRE(lines.size() == 1);
            CHECK(contains(lines[0], "\xEF\xBF\xBD"));
            CHECK(lines[0].find('\xFF') == std::string::npos);
        }
    }

    // The portable file next to the executable wins when it exists (not in a macOS app bundle);
    // otherwise the user data folder. An app bundle is read-only and signed: always user data.
    TEST_CASE("config: the default file of an executable in an app bundle") {
        const fs::path bundled = fs::path("/Applications/rawreel 2.app/Contents/MacOS/rawreel-gui");
        const fs::path build_tree = fs::path("/work/build/Debug/rawreel-cli");
        const fs::path user_data = fs::path("/user data/rawreel");
        CHECK(rawreel::inAppBundle(bundled));
        CHECK(rawreel::inAppBundle(fs::path("/x/rawreel.app/Contents/MacOS/rawreel-cli")));
        CHECK_FALSE(rawreel::inAppBundle(build_tree));
        CHECK_FALSE(
            rawreel::inAppBundle(fs::path("/x/rawreel.app/Contents/Resources/rawreel-cli")));
        CHECK_FALSE(rawreel::inAppBundle(fs::path("/x/rawreel/Contents/MacOS/rawreel-cli")));
        CHECK_FALSE(rawreel::inAppBundle(fs::path("/x/rawreel.app/MacOS/rawreel-cli")));

        int asked = 0;
        const auto user_data_dir = [&]() -> dvv::Result<fs::path> {
            ++asked;
            return user_data;
        };
        int existed = 0;
        const auto exists = [&](const fs::path&) {
            ++existed;
            return true;
        };
        // In a bundle: the user data folder, the portable file not even asked.
        const dvv::Result<fs::path> mac_bundled =
            rawreel::defaultConfigPath(bundled, true, user_data_dir, exists);
        REQUIRE(mac_bundled.has_value());
        CHECK(*mac_bundled == user_data / "rawreel.json");
        CHECK(asked == 1);
        CHECK(existed == 0);

        // Not macOS: a bundle-shaped path is an ordinary place, next to the executable.
        const dvv::Result<fs::path> other = rawreel::defaultConfigPath(
            bundled, false, user_data_dir, [](const fs::path&) { return true; });
        REQUIRE(other.has_value());
        CHECK(*other == bundled.parent_path() / "rawreel.json");
        CHECK(asked == 1);

        // No user data folder: its error (loadConfig warns and uses the defaults).
        const dvv::Result<fs::path> failed = rawreel::defaultConfigPath(
            bundled, true,
            []() -> dvv::Result<fs::path> {
                return dvv::makeError(dvv::Errc::Io, "HOME is not set");
            },
            exists);
        REQUIRE_FALSE(failed.has_value());
        CHECK(failed.error().code == dvv::Errc::Io);
    }

    TEST_CASE(
        "config: the default file is the portable one next to the executable, else user data") {
        const fs::path build_tree = fs::path("/work/build/Debug/rawreel-cli");
        const fs::path user_data = fs::path("/user data/rawreel");
        int asked = 0;
        const auto user_data_dir = [&]() -> dvv::Result<fs::path> {
            ++asked;
            return user_data;
        };
        const fs::path portable = build_tree.parent_path() / "rawreel.json";
        // Outside a bundle with the portable file: next to the executable, user data not called.
        const dvv::Result<fs::path> with = rawreel::defaultConfigPath(
            build_tree, true, user_data_dir, [&](const fs::path& p) { return p == portable; });
        REQUIRE(with.has_value());
        CHECK(*with == portable);
        CHECK(asked == 0);
        // Without it: the user data folder.
        const dvv::Result<fs::path> without = rawreel::defaultConfigPath(
            build_tree, true, user_data_dir, [](const fs::path&) { return false; });
        REQUIRE(without.has_value());
        CHECK(*without == user_data / "rawreel.json");
        CHECK(asked == 1);
    }

    // The running executable: with no rawreel.json next to the test executable, the user data
    // folder (the test environment points it into the build tree).
    TEST_CASE("config: the default file of the running executable") {
        const dvv::Result<fs::path> executable = dvv::paths::executablePath();
        REQUIRE_MESSAGE(executable.has_value(), executable.error().describe());
        REQUIRE_FALSE(fs::exists(executable->parent_path() / "rawreel.json"));
        const dvv::Result<fs::path> user_data = dvv::paths::userDataDir("rawreel");
        REQUIRE_MESSAGE(user_data.has_value(), user_data.error().describe());
        const dvv::Result<fs::path> path = rawreel::defaultConfigPath();
        REQUIRE_MESSAGE(path.has_value(), path.error().describe());
        CHECK(*path == *user_data / "rawreel.json");
        CHECK(path->is_absolute());

        const WarningLog log;
        const LoadedConfig loaded = rawreel::loadConfig(std::optional<std::string>{});
        CHECK(loaded.path == *path);
        CHECK_FALSE(loaded.found);
        CHECK(loaded.warning.empty());
        CHECK(loaded.config.io.fileReads == FileReadMode::Auto);
        CHECK(log.lines().empty());  // a missing default file is silent
    }

    TEST_CASE("config: a relative --config is made absolute") {
        const WarningLog log;
        const LoadedConfig loaded = rawreel::loadConfig(std::optional<std::string>("rel.json"));
        CHECK(loaded.path.is_absolute());
        CHECK(loaded.path == fs::absolute(fs::path("rel.json")));
    }
}

TEST_CASE("config keys: written and read back") {
    namespace key = rawreel::config_key;
    const TemporaryDirectory dir("rawreel-config");
    const fs::path file = dir.path() / "rawreel.json";
    const std::vector<rawreel::ConfigValue> values{
        {.key = key::IO_FILE_READS, .value = std::string("buffered")},
        {.key = key::GUI_SHOW_LOG_PANE, .value = true},
        {.key = key::LOG_LEVEL, .value = std::string("debug")},
        {.key = key::LOG_SOURCE, .value = true},
        {.key = key::LOG_FILE_ENABLED, .value = false},
        {.key = key::LOG_FILE_FOLDER, .value = dvv::pathText(dir.path() / "logs")},
        {.key = key::LOG_SYSTEM_ENABLED, .value = true},
        {.key = key::GUI_RECENT, .value = std::vector<std::string>{"file:a.yuv"}},
        {.key = key::GUI_RECENT_SIZE, .value = std::int64_t{7}},
    };
    REQUIRE(rawreel::saveConfigValues(file, values));
    const LoadedConfig loaded = rawreel::loadConfig(file);
    CHECK(loaded.warning.empty());
    CHECK(loaded.config.io.fileReads == FileReadMode::Buffered);
    CHECK(loaded.config.gui.showLogPane);
    CHECK(loaded.config.log.level == dvv::LogLevel::Debug);
    CHECK(loaded.config.log.source);
    CHECK(!loaded.config.log.file.enabled);
    CHECK(loaded.config.log.file.folder == dir.path() / "logs");
    CHECK(loaded.config.log.system.enabled);
    const rawreel::RecentConfig recent = rawreel::parseRecent(loaded.text);
    CHECK(recent.size == 7);
    CHECK(recent.entries == std::vector<std::string>{"file:a.yuv"});
}

TEST_SUITE("core") {
    using rawreel::GridConfig;
    using rawreel::OverlayConfig;
    using rawreel::overlayConfigValues;
    using rawreel::parseOverlayConfig;
    using rawreel::Rgb8;
    using rawreel::RulerConfig;

    TEST_CASE("config: overlay keys round trip through the writer") {
        const TemporaryDirectory dir("rawreel-config");
        const fs::path file = dir.path() / "rawreel.json";
        writeText(file, R"({"gui":{"showLogPane":true,"grid":{"major":null}}})");
        GridConfig g;
        g.levels[0] = {.show = true,
                       .step = 64,
                       .opacity = 90,
                       .colour = {255, 159, 26},
                       .thickness = 3,
                       .adaptive = true,
                       .halo = false,
                       .minimumSpacing = 20};
        g.levels[2].colour = {0, 10, 171};
        const RulerConfig r{.show = true, .numbers = false};
        const std::vector<rawreel::ConfigValue> values = overlayConfigValues(g, r, true, true);
        REQUIRE(rawreel::saveConfigValues(file, values));
        const dvv::Result<std::string> text = rawreel::readConfigText(file);
        REQUIRE(text);
        const OverlayConfig c = parseOverlayConfig(*text);
        CHECK(c.grid == g);
        CHECK(c.rulers == r);
        CHECK(c.warnings.empty());
        CHECK(c.gridSavable);
        CHECK(c.rulersSavable);
        const dvv::Result<Config> config = rawreel::parseConfig(*text);
        REQUIRE(config);
        CHECK(config->gui.showLogPane);
    }

    TEST_CASE("config: a missing or empty gui gives the overlay defaults") {
        for (const char* text : {R"({"io":{}})", R"({"gui":{}})", R"({"gui":{"grid":{}}})", "[]",
                                 "not json", R"({"gui":5})"}) {
            const OverlayConfig c = parseOverlayConfig(text);
            CHECK(c.grid == GridConfig{});
            CHECK(c.rulers == RulerConfig{});
            CHECK(c.warnings.empty());
            CHECK(c.gridSavable);
            CHECK(c.rulersSavable);
        }
    }

    TEST_CASE("config: a bad overlay leaf falls back alone") {
        const OverlayConfig c = parseOverlayConfig(
            R"({"gui":{"grid":{"major":{"step":24,"opacity":101,"colour":"#fff","thickness":0,)"
            R"("minimumSpacing":-1,"halo":true}},"rulers":{"show":"yes","numbers":false}}})");
        CHECK(c.grid.levels[0].step == 32);
        CHECK(c.grid.levels[0].opacity == 85);
        CHECK(c.grid.levels[0].colour == Rgb8{});
        CHECK(c.grid.levels[0].thickness == 2);
        CHECK(c.grid.levels[0].minimumSpacing == 2);
        CHECK(c.grid.levels[0].halo);
        CHECK_FALSE(c.rulers.show);
        CHECK_FALSE(c.rulers.numbers);
        REQUIRE(c.warnings.size() == 6);
        CHECK(c.gridSavable);
        CHECK(c.rulersSavable);
        CHECK(c.warnings[0] ==
              "gui.grid.major.step: expected one of 2, 4, 8, 16, 32, 64, 128, 256, not 24; "
              "using 32");
        CHECK(c.warnings[1] ==
              "gui.grid.major.opacity: expected a whole number from 0 to 100, not 101; using 85");
        CHECK(c.warnings[2] ==
              R"(gui.grid.major.colour: expected "#rrggbb", not "#fff"; using "#ffffff")");
        CHECK(c.warnings[3] ==
              "gui.grid.major.thickness: expected a whole number from 1 to 4, not 0; using 2");
        CHECK(c.warnings[4] ==
              "gui.grid.major.minimumSpacing: expected a whole number from 1 to "
              "100, not -1; using 2");
        CHECK(c.warnings[5] ==
              R"(gui.rulers.show: expected true or false, not "yes"; using false)");
    }

    TEST_CASE("config: other bad overlay leaves") {
        const OverlayConfig c = parseOverlayConfig(
            R"({"gui":{"grid":{"major":{"step":512,"colour":"red","minimumSpacing":12.5},)"
            R"("minor":{"colour":"#2BD2fF","opacity":0,"show":1,"adaptive":null},)"
            R"("secondMinor":{"thickness":4,"minimumSpacing":100,"step":4.0}}}})");
        CHECK(c.grid.levels[0].step == 32);
        CHECK(c.grid.levels[0].colour == Rgb8{});
        CHECK(c.grid.levels[0].minimumSpacing == 2);
        CHECK(c.grid.levels[1].colour == Rgb8{43, 210, 255});
        CHECK(c.grid.levels[1].opacity == 0);
        CHECK(c.grid.levels[1].show);
        CHECK_FALSE(c.grid.levels[1].adaptive);
        CHECK(c.grid.levels[2].thickness == 4);
        CHECK(c.grid.levels[2].minimumSpacing == 100);
        CHECK(c.grid.levels[2].step == 2);
        REQUIRE(c.warnings.size() == 6);
        CHECK(c.warnings[0].starts_with("gui.grid.major.step: expected"));
        CHECK(c.warnings[1].starts_with("gui.grid.major.colour: expected"));
        CHECK(c.warnings[2].starts_with("gui.grid.major.minimumSpacing: expected"));
        CHECK(c.warnings[3] == "gui.grid.minor.show: expected true or false, not 1; using true");
        CHECK(c.warnings[4].starts_with("gui.grid.minor.adaptive: expected"));
        CHECK(c.warnings[5].starts_with("gui.grid.secondMinor.step: expected"));
        CHECK(c.gridSavable);
    }

    TEST_CASE("config: steps that break the rules fall back together") {
        const OverlayConfig c = parseOverlayConfig(
            R"({"gui":{"grid":{"major":{"step":32},"minor":{"step":32,"opacity":10}}}})");
        CHECK(c.grid.levels[0].step == 32);
        CHECK(c.grid.levels[1].step == 8);
        CHECK(c.grid.levels[2].step == 2);
        CHECK(c.grid.levels[1].opacity == 10);
        REQUIRE(c.warnings.size() == 1);
        CHECK(c.warnings[0] == std::format("gui.grid: the steps 32 / 32 / 2 break {}; using 32 / "
                                           "8 / 2",
                                           rawreel::GRID_STEP_RULE));
        CHECK(c.gridSavable);
        const OverlayConfig valid = parseOverlayConfig(
            R"({"gui":{"grid":{"major":{"step":256},"minor":{"step":4},"secondMinor":{"step":2}}}})");
        CHECK(valid.grid.levels[0].step == 256);
        CHECK(valid.grid.levels[1].step == 4);
        CHECK(valid.warnings.empty());
    }

    TEST_CASE("config: an overlay parent that is not an object") {
        for (const char* text : {R"({"gui":{"grid":5}})", R"({"gui":{"grid":[]}})",
                                 R"({"gui":{"grid":{"major":true}}})"}) {
            const OverlayConfig c = parseOverlayConfig(text);
            CHECK_FALSE(c.gridSavable);
            CHECK(c.rulersSavable);
            CHECK(c.grid == GridConfig{});
            REQUIRE(c.warnings.size() == 1);
            CHECK(c.warnings[0].ends_with("; the defaults are used and not saved"));
            CHECK(rawreel::parseConfig(text).has_value());  // never a format error
        }
        CHECK(parseOverlayConfig(R"({"gui":{"grid":[]}})").warnings[0] ==
              "gui.grid: expected an object, not []; the defaults are used and not saved");
        // A level that is not an object keeps the other levels' values.
        const OverlayConfig level = parseOverlayConfig(
            R"({"gui":{"grid":{"major":true,"minor":{"opacity":10}},"rulers":{"show":true}}})");
        CHECK_FALSE(level.gridSavable);
        CHECK(level.grid.levels[1].opacity == 10);
        CHECK(level.rulers.show);
        CHECK(level.warnings[0] ==
              "gui.grid.major: expected an object, not true; the defaults "
              "are used and not saved");
        // null: the writer replaces it by an object, so the level's defaults are used and saving
        // works.
        const OverlayConfig null_major = parseOverlayConfig(R"({"gui":{"grid":{"major":null}}})");
        CHECK(null_major.gridSavable);
        CHECK(null_major.grid == GridConfig{});
        REQUIRE(null_major.warnings.size() == 1);
        CHECK(null_major.warnings[0] ==
              "gui.grid.major: expected an object, not null; the defaults are used");
        const OverlayConfig null_rulers = parseOverlayConfig(R"({"gui":{"rulers":null}})");
        CHECK(null_rulers.rulersSavable);
        CHECK(null_rulers.warnings.size() == 1);
        const OverlayConfig rulers = parseOverlayConfig(R"({"gui":{"rulers":"yes"}})");
        CHECK_FALSE(rulers.rulersSavable);
        CHECK(rulers.rulers.cursorColour == RulerConfig{}.cursorColour);
        CHECK(rulers.gridSavable);
        CHECK(rulers.warnings.size() == 1);
        CHECK(rawreel::parseConfig(R"({"gui":{"rulers":"yes"}})").has_value());
    }

    TEST_CASE("config: the grid numbers are read in the overlay ranges") {
        struct Case {
            const char* key;
            rawreel::GridRange range;
        };
        for (const Case c : {Case{"opacity", rawreel::GRID_OPACITY_RANGE},
                             Case{"thickness", rawreel::GRID_THICKNESS_RANGE},
                             Case{"minimumSpacing", rawreel::GRID_MINIMUM_SPACING_RANGE}}) {
            CAPTURE(c.key);
            const auto level = [&](std::int64_t value) {
                const OverlayConfig read = parseOverlayConfig(
                    std::format(R"({{"gui":{{"grid":{{"minor":{{"{}":{}}}}}}}}})", c.key, value));
                return std::pair{read.grid.levels[1], read.warnings};
            };
            const auto number = [&](const rawreel::GridLevelConfig& l) {
                return std::string_view{c.key} == "opacity"     ? l.opacity
                       : std::string_view{c.key} == "thickness" ? l.thickness
                                                                : l.minimumSpacing;
            };
            const rawreel::GridLevelConfig fallback = GridConfig{}.levels[1];
            for (const std::uint32_t end : {c.range.minimum, c.range.maximum}) {
                const auto [read, warnings] = level(end);
                CHECK(number(read) == end);
                CHECK(warnings.empty());
            }
            for (const std::int64_t outside :
                 {std::int64_t{c.range.minimum} - 1, std::int64_t{c.range.maximum} + 1}) {
                const auto [read, warnings] = level(outside);
                CHECK(number(read) == number(fallback));
                REQUIRE(warnings.size() == 1);
                CHECK(warnings[0] ==
                      std::format("gui.grid.minor.{}: expected a whole number from {} to {}, not "
                                  "{}; using {}",
                                  c.key, c.range.minimum, c.range.maximum, outside,
                                  number(fallback)));
            }
        }
    }

    TEST_CASE("config: a colour that is not a string falls back") {
        for (const char* colour : {"5", "null", "[]", "true"}) {
            CAPTURE(colour);
            const OverlayConfig c = parseOverlayConfig(std::format(
                R"({{"gui":{{"grid":{{"minor":{{"colour":{},"opacity":10}}}}}}}})", colour));
            CHECK(c.grid.levels[1].colour == Rgb8{});
            CHECK(c.grid.levels[1].opacity == 10);
            REQUIRE(c.warnings.size() == 1);
            CHECK(c.warnings[0] == std::format(R"(gui.grid.minor.colour: expected "#rrggbb", not )"
                                               R"({}; using "#ffffff")",
                                               colour));
            CHECK(c.gridSavable);
        }
    }

    TEST_CASE("config: savable means the writer can merge the overlay keys") {
        const TemporaryDirectory dir("rawreel-config");
        const fs::path file = dir.path() / "rawreel.json";
        GridConfig g;
        g.levels[1].opacity = 10;
        const RulerConfig r{.show = true, .numbers = false};
        // A parent that is not an object: not savable, and the writer refuses the grid keys and
        // the ruler keys.
        for (const char* text :
             {R"({"gui":{"grid":{"major":true}}})", R"({"gui":{"grid":5}})",
              R"({"gui":{"rulers":"yes"}})", R"({"gui":{"rulers":{"font":5}}})"}) {
            CAPTURE(text);
            writeText(file, text);
            const OverlayConfig c = parseOverlayConfig(text);
            CHECK(c.gridSavable != c.rulersSavable);
            CHECK_FALSE(rawreel::saveConfigValues(
                file, overlayConfigValues(g, r, !c.gridSavable, !c.rulersSavable)));
            CHECK(rawreel::saveConfigValues(
                file, overlayConfigValues(g, r, c.gridSavable, c.rulersSavable)));
        }
        // Bad leaves never block a save: the saved values replace them, with no warning left.
        const char* const leaves =
            R"({"gui":{"grid":{"major":{"step":24,"opacity":101,"colour":"#fff","thickness":0,)"
            R"("minimumSpacing":-1},"minor":{"colour":5}},)"
            R"("rulers":{"show":"yes","font":null,"lastValue":1}}})";
        writeText(file, leaves);
        const OverlayConfig before = parseOverlayConfig(leaves);
        REQUIRE(before.gridSavable);
        REQUIRE(before.rulersSavable);
        REQUIRE(rawreel::saveConfigValues(file, overlayConfigValues(g, r, true, true)));
        const dvv::Result<std::string> text = rawreel::readConfigText(file);
        REQUIRE(text);
        const OverlayConfig after = parseOverlayConfig(*text);
        CHECK(after.grid == g);
        CHECK(after.rulers == r);
        CHECK(after.warnings.empty());
    }

    TEST_CASE("config: gui.rulers.cursorColour") {
        const OverlayConfig c = parseOverlayConfig(
            R"({"gui":{"rulers":{"show":true,"numbers":false,"cursorColour":"#34C759"}}})");
        CHECK(c.warnings.empty());
        CHECK(c.rulers.cursorColour == Rgb8{0x34, 0xc7, 0x59});
        const std::vector<rawreel::ConfigValue> values =
            overlayConfigValues(GridConfig{}, c.rulers, false, true);
        REQUIRE(values.size() == 6);
        CHECK(values[1].key == "gui.rulers.numbers");
        CHECK(values[2].key == "gui.rulers.cursorColour");
        CHECK(std::get<std::string>(values[2].value) == "#34c759");
    }

    TEST_CASE("config: a bad cursorColour falls back alone") {
        for (const std::string_view bad : {R"("red")", R"("#fff")", "5"}) {
            CAPTURE(bad);
            const OverlayConfig c = parseOverlayConfig(std::format(
                R"({{"gui":{{"rulers":{{"show":true,"numbers":false,"cursorColour":{}}}}}}})",
                bad));
            CHECK(c.rulers.cursorColour == RulerConfig{}.cursorColour);
            CHECK(c.rulers.show);
            CHECK_FALSE(c.rulers.numbers);
            CHECK(c.rulersSavable);
            REQUIRE(c.warnings.size() == 1);
            CHECK(c.warnings[0] ==
                  std::format(R"(gui.rulers.cursorColour: expected "#rrggbb", not {}; using )"
                              R"("#ff3b30")",
                              bad));
        }
    }

    TEST_CASE("config: ruler font and last value") {
        using rawreel::LastValue;
        using rawreel::RulerFont;
        // The other ruler keys around the one under test, so a fallback is seen to stay alone.
        const auto rulers = [](std::string_view font, std::string_view last_value) {
            return parseOverlayConfig(std::format(
                R"({{"gui":{{"rulers":{{"show":true,"numbers":false,"cursorColour":"#34c759",)"
                R"("font":{},"lastValue":{}}}}}}})",
                font, last_value));
        };
        const auto others_kept = [](const OverlayConfig& c) {
            CHECK(c.rulers.show);
            CHECK_FALSE(c.rulers.numbers);
            CHECK(c.rulers.cursorColour == Rgb8{0x34, 0xc7, 0x59});
            CHECK(c.rulersSavable);
        };
        SUBCASE("font.size") {
            for (const std::uint32_t good : {0U, 6U, 11U, 24U}) {
                CAPTURE(good);
                const OverlayConfig c =
                    rulers(std::format(R"({{"family":"Menlo","size":{}}})", good), R"("edge")");
                CHECK(c.warnings.empty());
                CHECK(c.rulers.font == RulerFont{.family = "Menlo", .size = good});
                CHECK(c.rulers.lastValue == LastValue::Edge);
                others_kept(c);
            }
            for (const std::string_view bad : {"5", "25", "11.5", R"("11")", "-1", "true"}) {
                CAPTURE(bad);
                const OverlayConfig c =
                    rulers(std::format(R"({{"family":"Menlo","size":{}}})", bad), R"("edge")");
                CHECK(c.rulers.font == RulerFont{.family = "Menlo", .size = 0});
                CHECK(c.rulers.lastValue == LastValue::Edge);
                others_kept(c);
                REQUIRE(c.warnings.size() == 1);
                CHECK(c.warnings[0] == std::format("gui.rulers.font.size: expected 0 or a whole "
                                                   "number from 6 to 24, not {}; using 0",
                                                   bad));
            }
        }
        SUBCASE("font.family") {
            // "\xC3\x9C" is U+00DC and "\xE5\xAD\x97\xE4\xBD\x93" U+5B57 U+4F53 in UTF-8.
            for (const std::string_view good :
                 {"Menlo", "", "\xC3\x9Cnknown \xE5\xAD\x97\xE4\xBD\x93"}) {
                CAPTURE(good);
                const OverlayConfig c =
                    rulers(std::format(R"({{"family":"{}","size":11}})", good), R"("edge")");
                CHECK(c.warnings.empty());
                CHECK(c.rulers.font == RulerFont{.family = std::string(good), .size = 11});
                others_kept(c);
            }
            for (const std::string_view bad : {"5", "null", R"(["Menlo"])"}) {
                CAPTURE(bad);
                const OverlayConfig c =
                    rulers(std::format(R"({{"family":{},"size":11}})", bad), R"("edge")");
                CHECK(c.rulers.font == RulerFont{.family = "", .size = 11});
                CHECK(c.rulers.lastValue == LastValue::Edge);
                others_kept(c);
                REQUIRE(c.warnings.size() == 1);
                CHECK(c.warnings[0] ==
                      std::format(R"(gui.rulers.font.family: expected a string, not {}; using "")",
                                  bad));
            }
        }
        SUBCASE("lastValue") {
            CHECK(rulers("{}", R"("pixel")").rulers.lastValue == LastValue::Pixel);
            const OverlayConfig edge = rulers("{}", R"("edge")");
            CHECK(edge.warnings.empty());
            CHECK(edge.rulers.lastValue == LastValue::Edge);
            for (const std::string_view bad : {R"("EDGE")", "1", R"("")"}) {
                CAPTURE(bad);
                const OverlayConfig c = rulers(R"({"family":"Menlo","size":11})", bad);
                CHECK(c.rulers.lastValue == LastValue::Pixel);
                CHECK(c.rulers.font == RulerFont{.family = "Menlo", .size = 11});
                others_kept(c);
                REQUIRE(c.warnings.size() == 1);
                CHECK(c.warnings[0] == std::format(R"(gui.rulers.lastValue: expected "pixel" or )"
                                                   R"("edge", not {}; using "pixel")",
                                                   bad));
            }
        }
        SUBCASE("every bad key falls back alone") {
            const OverlayConfig c = rulers(R"({"family":5,"size":25})", "1");
            CHECK(c.rulers.font == RulerFont{});
            CHECK(c.rulers.lastValue == LastValue::Pixel);
            others_kept(c);
            // One warning per bad key, in the order they are read.
            REQUIRE(c.warnings.size() == 3);
            CHECK(c.warnings[0].starts_with("gui.rulers.font.family:"));
            CHECK(c.warnings[1].starts_with("gui.rulers.font.size:"));
            CHECK(c.warnings[2].starts_with("gui.rulers.lastValue:"));
        }
        SUBCASE("the font parent") {
            const OverlayConfig not_object = rulers("5", R"("edge")");
            CHECK(not_object.rulers.font == RulerFont{});
            CHECK(not_object.rulers.lastValue == LastValue::Edge);
            CHECK(not_object.rulers.show);
            CHECK_FALSE(not_object.rulersSavable);
            REQUIRE(not_object.warnings.size() == 1);
            CHECK(not_object.warnings[0] ==
                  "gui.rulers.font: expected an object, not 5; the defaults are used and not "
                  "saved");
            const OverlayConfig null_font = rulers("null", R"("edge")");
            CHECK(null_font.rulers.font == RulerFont{});
            CHECK(null_font.rulers.lastValue == LastValue::Edge);
            others_kept(null_font);
            REQUIRE(null_font.warnings.size() == 1);
            CHECK(null_font.warnings[0] ==
                  "gui.rulers.font: expected an object, not null; the defaults are used");
        }
    }

    TEST_CASE("config: overlayConfigValues writes only the asked parts") {
        GridConfig g;
        g.levels[0].colour = {255, 159, 26};
        const std::vector<rawreel::ConfigValue> grid_only = overlayConfigValues(g, {}, true, false);
        CHECK(grid_only.size() == 24);
        CHECK(std::ranges::all_of(grid_only, [](const rawreel::ConfigValue& v) {
            return v.key.starts_with("gui.grid.");
        }));
        CHECK(grid_only[3].key == "gui.grid.major.colour");
        CHECK(std::get<std::string>(grid_only[3].value) == "#ff9f1a");
        CHECK(std::get<std::int64_t>(grid_only[1].value) == 32);
        CHECK(std::get<bool>(grid_only[8].value));  // gui.grid.minor.show
        const std::vector<rawreel::ConfigValue> rulers_only =
            overlayConfigValues(g, {}, false, true);
        REQUIRE(rulers_only.size() == 6);
        CHECK(rulers_only[0].key == "gui.rulers.show");
        CHECK(rulers_only[1].key == "gui.rulers.numbers");
        CHECK(rulers_only[2].key == "gui.rulers.cursorColour");
        CHECK(std::get<std::string>(rulers_only[2].value) == "#ff3b30");
        CHECK(rulers_only[3].key == "gui.rulers.font.family");
        CHECK(std::get<std::string>(rulers_only[3].value).empty());
        CHECK(rulers_only[4].key == "gui.rulers.font.size");
        CHECK(std::get<std::int64_t>(rulers_only[4].value) == 0);
        CHECK(rulers_only[5].key == "gui.rulers.lastValue");
        CHECK(std::get<std::string>(rulers_only[5].value) == "pixel");
        CHECK(overlayConfigValues(g, {}, false, false).empty());
    }
}
