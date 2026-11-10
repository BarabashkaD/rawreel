// The settings claim (core): one owner per config file (spec §6.3).
#include <doctest/doctest.h>

#include <filesystem>
#include <memory>

#include "core/config.h"
#include "core/settings_claim.h"
#include "temporary_directory.h"

namespace fs = std::filesystem;
using rawreel::SettingsClaim;

TEST_SUITE("core") {
    TEST_CASE("settings claim: a second claim fails") {
        const TemporaryDirectory temporary("rawreel-settings-claim");
        const fs::path config = temporary.path() / rawreel::CONFIG_FILE_NAME;
        const std::unique_ptr<SettingsClaim> first = SettingsClaim::tryClaim(config);
        CHECK(first != nullptr);
        CHECK(SettingsClaim::tryClaim(config) == nullptr);
    }

    TEST_CASE("settings claim: released by the destructor") {
        const TemporaryDirectory temporary("rawreel-settings-claim");
        const fs::path config = temporary.path() / rawreel::CONFIG_FILE_NAME;
        std::unique_ptr<SettingsClaim> first = SettingsClaim::tryClaim(config);
        REQUIRE(first != nullptr);
        first.reset();
        CHECK(SettingsClaim::tryClaim(config) != nullptr);
    }

    TEST_CASE("settings claim: different files") {
        const TemporaryDirectory temporary("rawreel-settings-claim");
        const std::unique_ptr<SettingsClaim> one =
            SettingsClaim::tryClaim(temporary.path() / "one.json");
        const std::unique_ptr<SettingsClaim> two =
            SettingsClaim::tryClaim(temporary.path() / "two.json");
        CHECK(one != nullptr);
        CHECK(two != nullptr);
    }

#if !defined(_WIN32)
    TEST_CASE("settings claim: creates the last folder") {
        const TemporaryDirectory temporary("rawreel-settings-claim");
        // A --config file in a new folder: the claim makes the folder, so the writer can save.
        CHECK(SettingsClaim::tryClaim(temporary.path() / "new" / rawreel::CONFIG_FILE_NAME) !=
              nullptr);
        CHECK(fs::is_directory(temporary.path() / "new"));
        // Only the last component: with no <tmp>/a there is no claim, and nothing is created.
        CHECK(SettingsClaim::tryClaim(temporary.path() / "a" / "b" / rawreel::CONFIG_FILE_NAME) ==
              nullptr);
        CHECK_FALSE(fs::exists(temporary.path() / "a"));
    }

    TEST_CASE("settings claim: one file, two spellings") {
        const TemporaryDirectory temporary("rawreel-settings-claim");
        const fs::path config = temporary.path() / rawreel::CONFIG_FILE_NAME;
        const std::unique_ptr<SettingsClaim> first = SettingsClaim::tryClaim(config);
        REQUIRE(first != nullptr);
        CHECK(SettingsClaim::tryClaim(temporary.path() / "." / rawreel::CONFIG_FILE_NAME) ==
              nullptr);
    }
#endif
}
