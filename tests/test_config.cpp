// The config file loader (M3.1): wx-free, so it is tested in rawreel_core.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/config.h"
#include "dvv/base/error.h"
#include "dvv/base/json.h"
#include "dvv/base/log.h"
#include "dvv/base/paths.h"
#include "dvv/media/types.h"
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
            dvv::json::dump(dvv::json::Value(dvv::media::utf8FromPath(folder))));
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
            rawreel::loadConfig(std::optional<std::string>(dvv::media::utf8FromPath(path)));
        CHECK(loaded.found);
        CHECK(loaded.path == path);
        CHECK(loaded.config.io.fileReads == FileReadMode::Unbuffered);
    }

    // A file the user named must exist: a missing one is one warning (the default file is silent).
    TEST_CASE("config: a missing --config file gives one warning and the defaults") {
        const TemporaryDirectory directory("rawreel-config");
        const WarningLog log;
        const LoadedConfig missing = rawreel::loadConfig(
            std::optional<std::string>(dvv::media::utf8FromPath(directory.path() / "none.json")));
        CHECK_FALSE(missing.found);
        CHECK(missing.config.io.fileReads == FileReadMode::Auto);
        CHECK(missing.warning == "not found");
        const std::vector<std::string> lines = log.lines();
        REQUIRE(lines.size() == 1);
        CHECK(contains(lines[0], "WARN"));
        CHECK(contains(lines[0], "none.json' ignored: not found; using the defaults"));
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
