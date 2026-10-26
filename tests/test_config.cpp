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
#include "dvv/base/log.h"
#include "dvv/base/paths.h"
#include "dvv/media/types.h"
#include "dvv/platform/file_reader.h"

namespace fs = std::filesystem;
using dvv::platform::FileReadMode;
using rawreel::Config;
using rawreel::LoadedConfig;

namespace {

// A unique directory under the system temporary directory, removed with its contents.
class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::random_device random;
        for (int attempt = 0; attempt < 100; ++attempt) {
            fs::path candidate = fs::temp_directory_path() /
                                 std::format("rawreel-config-{:08x}{:08x}", random(), random());
            std::error_code error;
            if (fs::create_directory(candidate, error)) {
                path_ = std::move(candidate);
                return;
            }
        }
        FAIL("no temporary directory could be created");
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;
    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

void writeText(const fs::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    file << text;
    REQUIRE(file.good());
}

// Captures warnings for one test; restores the logger afterwards (the tests add no other sinks).
class WarningLog {
public:
    WarningLog() : level_(dvv::Logger::instance().level()) {
        dvv::Logger::instance().clearSinks();
        dvv::Logger::instance().addSink(sink_);
        dvv::Logger::instance().setLevel(dvv::LogLevel::Warn);
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

    TEST_CASE("config: invalid text and values") {
        checkInvalid("", "");
        checkInvalid(R"({"io": )", "");
        checkInvalid("[1, 2]", "the top level is not a JSON object");
        checkInvalid(R"({"io": "buffered"})", "io: expected an object");
        checkInvalid(R"({"io": {"fileReads": "fast"}})",
                     R"(io.fileReads: expected "auto", "unbuffered" or "buffered", not "fast")");
        checkInvalid(R"({"io": {"fileReads": 1}})", "io.fileReads: expected");
        checkInvalid(R"({"io": {"fileReads": null}})", "io.fileReads: expected");
    }

    TEST_CASE("config: a missing file gives the defaults without a warning") {
        const TemporaryDirectory directory;
        const WarningLog log;
        const LoadedConfig loaded = rawreel::loadConfig(directory.path() / "rawreel.json");
        CHECK_FALSE(loaded.found);
        CHECK(loaded.warning.empty());
        CHECK(loaded.config.io.fileReads == FileReadMode::Auto);
        CHECK(loaded.path == directory.path() / "rawreel.json");
        CHECK(log.lines().empty());
    }

    TEST_CASE("config: a valid file") {
        const TemporaryDirectory directory;
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
        const TemporaryDirectory directory;
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
        CHECK(contains(loaded.warning, reason));
        const std::vector<std::string> lines = log.lines();
        REQUIRE(lines.size() == 1);
        CHECK(contains(lines[0], "WARN"));
        CHECK(contains(lines[0], "rawreel.json' ignored: "));
        CHECK(contains(lines[0], "using the defaults"));
    }

    // --config <file> (UTF-8) replaces the file next to the executable.
    TEST_CASE("config: the file --config names") {
        const TemporaryDirectory directory;
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
        const TemporaryDirectory directory;
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

    // The default location is the running executable's directory, never the working directory.
    TEST_CASE("config: the default file is rawreel.json next to the executable") {
        const dvv::Result<fs::path> executable = dvv::paths::executablePath();
        REQUIRE_MESSAGE(executable.has_value(), executable.error().describe());
        const dvv::Result<fs::path> path = rawreel::defaultConfigPath();
        REQUIRE_MESSAGE(path.has_value(), path.error().describe());
        CHECK(*path == executable->parent_path() / "rawreel.json");
        CHECK(path->is_absolute());

        const WarningLog log;
        const LoadedConfig loaded = rawreel::loadConfig(std::optional<std::string>{});
        CHECK(loaded.path == *path);
        CHECK_FALSE(loaded.found);  // no rawreel.json is shipped next to the test executable
        CHECK(loaded.warning.empty());
        CHECK(loaded.config.io.fileReads == FileReadMode::Auto);
        CHECK(log.lines().empty());  // a missing default file is silent
    }
}
