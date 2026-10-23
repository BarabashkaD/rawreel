#include <doctest/doctest.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "arguments.h"
#include "dvv/base/environment.h"
#include "dvv/base/error.h"

namespace {

dvv::Result<rawreel::Arguments> parse(const std::vector<std::string_view>& args) {
    return rawreel::parseArguments(std::span<const std::string_view>(args));
}

// Sets DVV_REQUIRE_GPU for one test and restores the previous value afterwards.
class RequireGpuVariable {
public:
    RequireGpuVariable() : saved_(dvv::getEnvironment(NAME)) {}
    RequireGpuVariable(const RequireGpuVariable&) = delete;
    RequireGpuVariable& operator=(const RequireGpuVariable&) = delete;
    RequireGpuVariable(RequireGpuVariable&&) = delete;
    RequireGpuVariable& operator=(RequireGpuVariable&&) = delete;
    ~RequireGpuVariable() { dvv::setEnvironment(NAME, saved_.value_or("")); }

    // An empty value removes the variable.
    static void set(std::string_view value) { REQUIRE(dvv::setEnvironment(NAME, value)); }

private:
    static constexpr std::string_view NAME = "DVV_REQUIRE_GPU";
    std::optional<std::string> saved_;
};

}  // namespace

TEST_CASE("parseArguments") {
    SUBCASE("no arguments give the defaults") {
        const auto arguments = parse({});
        REQUIRE(arguments.has_value());
        CHECK_FALSE(arguments->selftest);
        CHECK_FALSE(arguments->help);
    }
    SUBCASE("--selftest") {
        const auto arguments = parse({"--selftest"});
        REQUIRE(arguments.has_value());
        CHECK(arguments->selftest);
        CHECK_FALSE(arguments->help);
    }
    SUBCASE("--help") {
        const auto arguments = parse({"--help"});
        REQUIRE(arguments.has_value());
        CHECK_FALSE(arguments->selftest);
        CHECK(arguments->help);
    }
    SUBCASE("both") {
        const auto arguments = parse({"--selftest", "--help"});
        REQUIRE(arguments.has_value());
        CHECK(arguments->selftest);
        CHECK(arguments->help);
    }
    SUBCASE("an unknown option") {
        const auto arguments = parse({"--bogus"});
        REQUIRE_FALSE(arguments.has_value());
        CHECK(arguments.error().code == dvv::Errc::Format);
        CHECK(arguments.error().message.find("--bogus") != std::string::npos);
    }
    SUBCASE("an option without its dashes") {
        const auto arguments = parse({"selftest"});
        REQUIRE_FALSE(arguments.has_value());
        CHECK(arguments.error().code == dvv::Errc::Format);
    }
}

TEST_CASE("skipExitCode") {
    const RequireGpuVariable variable;
    RequireGpuVariable::set("");
    CHECK(rawreel::skipExitCode() == rawreel::exit_code::SKIPPED);
    CHECK(rawreel::skipExitCode() == 77);
    RequireGpuVariable::set("1");
    CHECK(rawreel::skipExitCode() == rawreel::exit_code::GPU_ERROR);
    CHECK(rawreel::skipExitCode() == 4);
    RequireGpuVariable::set("0");
    CHECK(rawreel::skipExitCode() == 77);
}

TEST_CASE("the usage text ends with a newline") {
    const std::string_view usage = rawreel::USAGE;
    REQUIRE_FALSE(usage.empty());
    CHECK(usage.starts_with("Usage: rawreel-gui"));
    CHECK(usage.back() == '\n');
}

TEST_CASE("USAGE names every exit code") {
    const std::string_view usage = rawreel::USAGE;
    CHECK(usage.find("0 success") != std::string_view::npos);
    CHECK(usage.find("1 bad arguments") != std::string_view::npos);
    CHECK(usage.find("4 GPU error") != std::string_view::npos);
    CHECK(usage.find("6 a --selftest check failed") != std::string_view::npos);
    CHECK(usage.find("77") != std::string_view::npos);
}
