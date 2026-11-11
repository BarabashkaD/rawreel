// The config writer (core): merging key values into rawreel.json safely (spec §6.4).
#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/config.h"
#include "core/config_writer.h"
#include "dvv/base/error.h"
#include "dvv/media/types.h"
#include "temporary_directory.h"

namespace fs = std::filesystem;
using rawreel::ConfigValue;

namespace {

void writeText(const fs::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    file << text;
    REQUIRE(file.good());
}

std::string readText(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

rawreel::Config parsed(const fs::path& path) {
    dvv::Result<rawreel::Config> config = rawreel::parseConfig(readText(path));
    REQUIRE_MESSAGE(config.has_value(), (config ? "" : config.error().describe()));
    return *config;
}

void save(const fs::path& path, const ConfigValue& value) {
    const std::array values{value};
    const dvv::Result<void> saved = rawreel::saveConfigValues(path, values);
    REQUIRE_MESSAGE(saved.has_value(), (saved ? "" : saved.error().describe()));
}

std::vector<fs::path> temporaryFiles(const fs::path& folder) {
    std::vector<fs::path> found;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder)) {
        if (entry.path().extension() == ".tmp") {
            found.push_back(entry.path());
        }
    }
    return found;
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("config writer: a number and a string array") {
        const TemporaryDirectory temporary("rawreel-config-writer");
        const fs::path file = temporary.path() / rawreel::CONFIG_FILE_NAME;
        writeText(file, R"({"x":1,"gui":{"showLogPane":true}})");
        writeText(file, R"({"x":1,"y":2,"gui":{"showLogPane":true}})");  // edited before the save
        const std::vector<std::string> recent{"/data/a.yuv", "lavfi:testsrc2"};
        const std::array values{ConfigValue{.key = "gui.recentSize", .value = std::int64_t{3}},
                                ConfigValue{.key = "gui.recent", .value = recent}};
        const dvv::Result<void> saved = rawreel::saveConfigValues(file, values);
        REQUIRE_MESSAGE(saved.has_value(), (saved ? "" : saved.error().describe()));
        const nlohmann::ordered_json root = nlohmann::ordered_json::parse(readText(file));
        CHECK(root["gui"]["recentSize"] == 3);
        CHECK(root["gui"]["recent"] ==
              nlohmann::ordered_json::array({"/data/a.yuv", "lavfi:testsrc2"}));
        CHECK(root["gui"]["showLogPane"] == true);
        CHECK(root["x"] == 1);
        CHECK(root["y"] == 2);
    }

    TEST_CASE("config writer: bad history values never refuse a save") {
        const TemporaryDirectory temporary("rawreel-config-writer");
        const fs::path file = temporary.path() / rawreel::CONFIG_FILE_NAME;
        writeText(file, R"({"gui":{"recentSize":"x","recent":3}})");
        save(file, {.key = "gui.showLogPane", .value = true});
        const nlohmann::ordered_json root = nlohmann::ordered_json::parse(readText(file));
        CHECK(root["gui"]["recentSize"] == "x");
        CHECK(root["gui"]["showLogPane"] == true);
    }

    TEST_CASE("config writer: refuses a text over 1 MiB") {
        const TemporaryDirectory temporary("rawreel-config-writer");
        const fs::path file = temporary.path() / rawreel::CONFIG_FILE_NAME;
        const std::string original = R"({"gui":{"showLogPane":true}})";
        writeText(file, original);
        const std::vector<std::string> recent(300, std::string(4000, 'a'));
        const std::array values{ConfigValue{.key = "gui.recent", .value = recent}};
        const dvv::Result<void> saved = rawreel::saveConfigValues(file, values);
        REQUIRE_FALSE(saved.has_value());
        CHECK(saved.error().code == dvv::Errc::Format);
        CHECK(saved.error().message.find("bytes is too large") != std::string::npos);
        CHECK(readText(file) == original);
        CHECK(temporaryFiles(temporary.path()).empty());
    }

    TEST_CASE("config writer: creates the file") {
        const TemporaryDirectory temporary("rawreel-config-writer");
        const fs::path folder = temporary.path() / "u";
        REQUIRE(fs::create_directory(folder));
        const fs::path file = folder / rawreel::CONFIG_FILE_NAME;
        save(file, {.key = "gui.showLogPane", .value = true});
        CHECK(parsed(file).gui.showLogPane);
        CHECK(temporaryFiles(folder).empty());
    }

    TEST_CASE("config writer: creates the last folder") {
        const TemporaryDirectory temporary("rawreel-config-writer");
        REQUIRE(fs::create_directory(temporary.path() / "v"));
        const fs::path file = temporary.path() / "v" / "w" / rawreel::CONFIG_FILE_NAME;
        save(file, {.key = "gui.showLogPane", .value = true});
        CHECK(parsed(file).gui.showLogPane);

        const fs::path deep = temporary.path() / "x" / "y" / rawreel::CONFIG_FILE_NAME;
        const std::array values{ConfigValue{.key = "gui.showLogPane", .value = true}};
        const dvv::Result<void> saved = rawreel::saveConfigValues(deep, values);
        REQUIRE_FALSE(saved.has_value());
        CHECK(saved.error().code == dvv::Errc::Io);
        CHECK_FALSE(fs::exists(temporary.path() / "x"));
    }

    TEST_CASE("config writer: keeps other keys and the key order") {
        const TemporaryDirectory temporary("rawreel-config-writer");
        const fs::path file = temporary.path() / rawreel::CONFIG_FILE_NAME;
        writeText(file, R"({"zeta":{"x":1},"io":{"fileReads":"buffered"},"gui":{"other":2}})");
        save(file, {.key = "gui.showLogPane", .value = false});
        CHECK(readText(file) == R"({
  "zeta": {
    "x": 1
  },
  "io": {
    "fileReads": "buffered"
  },
  "gui": {
    "other": 2,
    "showLogPane": false
  }
}
)");
        const nlohmann::ordered_json root = nlohmann::ordered_json::parse(readText(file));
        std::vector<std::string> keys;
        for (const auto& item : root.items()) {
            keys.push_back(item.key());
        }
        CHECK(keys == std::vector<std::string>{"zeta", "io", "gui"});
        CHECK(root["zeta"]["x"] == 1);
        CHECK(root["io"]["fileReads"] == "buffered");
        CHECK(root["gui"]["other"] == 2);
        CHECK(temporaryFiles(temporary.path()).empty());
    }

    TEST_CASE("config writer: re-reads the file") {
        const TemporaryDirectory temporary("rawreel-config-writer");
        const fs::path file = temporary.path() / rawreel::CONFIG_FILE_NAME;
        writeText(file, R"({"gui":{"showLogPane":false}})");
        const rawreel::LoadedConfig loaded = rawreel::loadConfig(file);
        REQUIRE(loaded.found);
        REQUIRE(loaded.warning.empty());
        writeText(file, R"({"gui":{"showLogPane":false},"io":{"fileReads":"unbuffered"}})");
        save(file, {.key = "log.system.enabled", .value = true});
        const rawreel::Config config = parsed(file);
        CHECK(config.io.fileReads == dvv::platform::FileReadMode::Unbuffered);
        CHECK(config.log.system.enabled);
        CHECK_FALSE(config.gui.showLogPane);
    }

    TEST_CASE("config writer: leaves an invalid file alone") {
        const TemporaryDirectory temporary("rawreel-config-writer");
        const fs::path file = temporary.path() / rawreel::CONFIG_FILE_NAME;
        const std::array values{ConfigValue{.key = "gui.showLogPane", .value = true}};
        for (const std::string_view text : {std::string_view("{not json"), std::string_view("[1]"),
                                            std::string_view(R"({"gui":{"showLogPane":"yes"}})")}) {
            CAPTURE(text);
            writeText(file, text);
            const dvv::Result<void> saved = rawreel::saveConfigValues(file, values);
            REQUIRE_FALSE(saved.has_value());
            CHECK(saved.error().code == dvv::Errc::Format);
            CHECK(saved.error().message == rawreel::parseConfig(text).error().message);
            CHECK(readText(file) == text);
            CHECK(temporaryFiles(temporary.path()).empty());
        }
    }

    TEST_CASE("config writer: a parent that is not an object, a bad value") {
        const TemporaryDirectory temporary("rawreel-config-writer");
        const fs::path file = temporary.path() / rawreel::CONFIG_FILE_NAME;
        const std::string_view text = R"({"log":{"file":3}})";
        writeText(file, text);
        const std::array folder{ConfigValue{.key = "log.file.folder", .value = std::string()}};
        const dvv::Result<void> saved = rawreel::saveConfigValues(file, folder);
        REQUIRE_FALSE(saved.has_value());
        CHECK(saved.error().code == dvv::Errc::Format);
        CHECK(saved.error().message == "log.file: expected an object");
        CHECK(readText(file) == text);

        // The merged text is checked as the loader reads it before anything is written.
        writeText(file, "{}");
        const std::array reads{ConfigValue{.key = "io.fileReads", .value = std::string("fast")}};
        const dvv::Result<void> bad = rawreel::saveConfigValues(file, reads);
        REQUIRE_FALSE(bad.has_value());
        CHECK(bad.error().code == dvv::Errc::Format);
        CHECK(readText(file) == "{}");
        CHECK(temporaryFiles(temporary.path()).empty());
    }

    TEST_CASE("config writer: refuses a read-only file") {
        const TemporaryDirectory temporary("rawreel-config-writer");
        const fs::path file = temporary.path() / rawreel::CONFIG_FILE_NAME;
        const std::string_view text = R"({"gui":{"showLogPane":false}})";
        writeText(file, text);
        fs::permissions(file,
                        fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read,
                        fs::perm_options::replace);
        const std::array values{ConfigValue{.key = "gui.showLogPane", .value = true}};
        const dvv::Result<void> saved = rawreel::saveConfigValues(file, values);
        fs::permissions(file, fs::perms::owner_write, fs::perm_options::add);
        REQUIRE_FALSE(saved.has_value());
        CHECK(saved.error().message.find("read-only") != std::string::npos);
        CHECK(readText(file) == text);
        CHECK(temporaryFiles(temporary.path()).empty());
    }

#if !defined(_WIN32)
    TEST_CASE("config writer: keeps a symlink") {
        const TemporaryDirectory temporary("rawreel-config-writer");
        const fs::path real = temporary.path() / "real.json";
        const fs::path link = temporary.path() / "link";
        writeText(real, R"({"io":{"fileReads":"buffered"}})");
        fs::create_symlink("real.json", link);
        save(link, {.key = "gui.showLogPane", .value = true});
        CHECK(fs::is_symlink(fs::symlink_status(link)));
        CHECK(fs::read_symlink(link) == fs::path("real.json"));
        const rawreel::Config config = parsed(real);
        CHECK(config.gui.showLogPane);
        CHECK(config.io.fileReads == dvv::platform::FileReadMode::Buffered);
        CHECK(temporaryFiles(temporary.path()).empty());
    }

    TEST_CASE("config writer: keeps the permission bits") {
        const TemporaryDirectory temporary("rawreel-config-writer");
        const fs::path file = temporary.path() / rawreel::CONFIG_FILE_NAME;
        writeText(file, "{}");
        const fs::perms bits = fs::perms::owner_read | fs::perms::owner_write |
                               fs::perms::group_read | fs::perms::group_write;  // 0660
        fs::permissions(file, bits, fs::perm_options::replace);
        save(file, {.key = "gui.showLogPane", .value = true});
        CHECK((fs::status(file).permissions() & fs::perms::mask) == bits);
        CHECK(parsed(file).gui.showLogPane);
    }
#endif

    TEST_CASE("config writer: a string value") {
        const TemporaryDirectory temporary("rawreel-config-writer");
        const fs::path file = temporary.path() / rawreel::CONFIG_FILE_NAME;
        const fs::path folder = temporary.path() / dvv::media::pathFromUtf8("журнал логов");
        save(file, {.key = "log.file.folder", .value = dvv::media::utf8FromPath(folder)});
        CHECK(parsed(file).log.file.folder == folder);
    }
}
