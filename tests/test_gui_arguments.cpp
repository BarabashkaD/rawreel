// rawreel-gui's argument parser (design M3 v2 §4.5): wx-free, so it is tested in rawreel_core.
#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/gui_arguments.h"
#include "dvv/base/environment.h"
#include "dvv/base/error.h"

using rawreel::Appearance;
using rawreel::GuiArguments;
using rawreel::Scenario;

namespace {

dvv::Result<GuiArguments> parse(const std::vector<std::string_view>& args) {
    return rawreel::parseGuiArguments(std::span<const std::string_view>(args));
}

// Checks that `args` fail with Errc::Format and a message containing `part`.
void checkFormatError(const std::vector<std::string_view>& args, std::string_view part) {
    const auto arguments = parse(args);
    REQUIRE_FALSE(arguments.has_value());
    CHECK(arguments.error().code == dvv::Errc::Format);
    CHECK_MESSAGE(arguments.error().message.find(part) != std::string::npos,
                  arguments.error().message);
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

TEST_CASE("gui arguments: no arguments give the defaults") {
    const auto arguments = parse({});
    REQUIRE(arguments.has_value());
    CHECK(arguments->appearance == Appearance::System);
    CHECK_FALSE(arguments->gpu.has_value());
    CHECK(arguments->selftest == Scenario::None);
    CHECK(arguments->inflight == 4);
    CHECK_FALSE(arguments->uri.has_value());
    CHECK_FALSE(arguments->help);
}

TEST_CASE("gui arguments: --appearance") {
    SUBCASE("dark") {
        const auto arguments = parse({"--appearance", "dark"});
        REQUIRE(arguments.has_value());
        CHECK(arguments->appearance == Appearance::Dark);
    }
    SUBCASE("light") {
        const auto arguments = parse({"--appearance", "light"});
        REQUIRE(arguments.has_value());
        CHECK(arguments->appearance == Appearance::Light);
    }
    SUBCASE("system") {
        const auto arguments = parse({"--appearance", "system"});
        REQUIRE(arguments.has_value());
        CHECK(arguments->appearance == Appearance::System);
    }
    SUBCASE("a bad value") {
        checkFormatError({"--appearance", "blue"},
                         "bad value 'blue' for --appearance (system, light, dark)");
    }
    SUBCASE("without its value") {
        checkFormatError({"--appearance"}, "option --appearance needs a value");
    }
    SUBCASE("twice") {
        checkFormatError({"--appearance", "dark", "--appearance", "light"},
                         "option --appearance given twice");
    }
}

TEST_CASE("gui arguments: --gpu") {
    SUBCASE("a number") {
        const auto arguments = parse({"--gpu", "1"});
        REQUIRE(arguments.has_value());
        REQUIRE(arguments->gpu.has_value());
        CHECK(arguments->gpu == std::optional<std::uint32_t>(1));
    }
    SUBCASE("zero") {
        const auto arguments = parse({"--gpu", "0"});
        REQUIRE(arguments.has_value());
        CHECK(arguments->gpu == 0u);
    }
    SUBCASE("not a number") {
        checkFormatError({"--gpu", "x"}, "bad number 'x' for --gpu");
    }
    SUBCASE("trailing characters") {
        checkFormatError({"--gpu", "1x"}, "bad number '1x' for --gpu");
    }
    SUBCASE("negative") {
        checkFormatError({"--gpu", "-1"}, "bad number '-1' for --gpu");
    }
    SUBCASE("too large") {
        checkFormatError({"--gpu", "99999999999"}, "bad number '99999999999' for --gpu");
    }
    SUBCASE("without its value") {
        checkFormatError({"--gpu"}, "option --gpu needs a value");
    }
}

TEST_CASE("gui arguments: --selftest") {
    SUBCASE("every scenario maps and its name round-trips") {
        const std::array<std::pair<std::string_view, Scenario>, 4> scenarios{{
            {"smoke", Scenario::Smoke},
            {"player", Scenario::Player},
            {"lost", Scenario::Lost},
            {"perf", Scenario::Perf},
        }};
        for (const auto& [name, scenario] : scenarios) {
            CAPTURE(name);
            const auto arguments = parse({"--selftest", name});
            REQUIRE(arguments.has_value());
            CHECK(arguments->selftest == scenario);
            CHECK(rawreel::scenarioName(scenario) == name);
        }
        CHECK(rawreel::scenarioName(Scenario::None).empty());
    }
    SUBCASE("the cut scenarios are bad values") {
        checkFormatError({"--selftest", "tools"},
                         "bad value 'tools' for --selftest (smoke, player, lost, perf)");
        checkFormatError({"--selftest", "recovery"}, "bad value 'recovery' for --selftest");
    }
    SUBCASE("without its value") {
        checkFormatError({"--selftest"}, "option --selftest needs a value");
    }
    SUBCASE("twice") {
        checkFormatError({"--selftest", "smoke", "--selftest", "player"},
                         "option --selftest given twice");
    }
}

TEST_CASE("gui arguments: --inflight") {
    SUBCASE("with --selftest perf") {
        const auto arguments = parse({"--selftest", "perf", "--inflight", "8"});
        REQUIRE(arguments.has_value());
        CHECK(arguments->inflight == 8);
    }
    SUBCASE("before --selftest perf") {
        const auto arguments = parse({"--inflight", "64", "--selftest", "perf"});
        REQUIRE(arguments.has_value());
        CHECK(arguments->inflight == 64);
    }
    SUBCASE("alone") {
        checkFormatError({"--inflight", "8"}, "--inflight is valid only with --selftest perf");
    }
    SUBCASE("with another scenario") {
        checkFormatError({"--selftest", "smoke", "--inflight", "8"},
                         "--inflight is valid only with --selftest perf");
    }
    SUBCASE("out of range") {
        checkFormatError({"--selftest", "perf", "--inflight", "0"},
                         "bad number '0' for --inflight (1 to 64)");
        checkFormatError({"--selftest", "perf", "--inflight", "65"},
                         "bad number '65' for --inflight (1 to 64)");
        checkFormatError({"--selftest", "perf", "--inflight", "x"},
                         "bad number 'x' for --inflight");
    }
    SUBCASE("twice") {
        checkFormatError({"--selftest", "perf", "--inflight", "2", "--inflight", "3"},
                         "option --inflight given twice");
    }
}

TEST_CASE("gui arguments: the URI") {
    SUBCASE("one URI is kept") {
        const auto arguments = parse({"synthetic:gradient?w=64&h=48", "--appearance", "light"});
        REQUIRE(arguments.has_value());
        REQUIRE(arguments->uri.has_value());
        CHECK(arguments->uri == std::optional<std::string>("synthetic:gradient?w=64&h=48"));
        CHECK(arguments->appearance == Appearance::Light);
    }
    SUBCASE("two URIs") {
        checkFormatError({"a.yuv", "b.yuv"}, "at most one URI");
    }
}

TEST_CASE("gui arguments: unknown options") {
    checkFormatError({"--bogus"}, "unknown argument '--bogus'");
    checkFormatError({"--selftest", "smoke", "--bogus"}, "unknown argument '--bogus'");
}

TEST_CASE("gui arguments: --help") {
    SUBCASE("alone") {
        const auto arguments = parse({"--help"});
        REQUIRE(arguments.has_value());
        CHECK(arguments->help);
    }
    SUBCASE("after a bad argument") {
        const auto arguments = parse({"--bogus", "--help"});
        REQUIRE(arguments.has_value());
        CHECK(arguments->help);
    }
    SUBCASE("before a bad argument") {
        const auto arguments = parse({"--help", "--gpu", "x", "a", "b"});
        REQUIRE(arguments.has_value());
        CHECK(arguments->help);
    }
    SUBCASE("as the value of an option") {
        const auto arguments = parse({"--selftest", "--help"});
        REQUIRE(arguments.has_value());
        CHECK(arguments->help);
    }
}

TEST_CASE("gui arguments: skipExitCode") {
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

TEST_CASE("gui arguments: the usage text") {
    const std::string_view usage = rawreel::USAGE;
    REQUIRE_FALSE(usage.empty());
    CHECK(usage.starts_with("Usage: rawreel-gui"));
    CHECK(usage.back() == '\n');
    CHECK(usage.find("0 success") != std::string_view::npos);
    CHECK(usage.find("1 bad arguments") != std::string_view::npos);
    CHECK(usage.find("4 GPU error") != std::string_view::npos);
    CHECK(usage.find("6 a --selftest check failed") != std::string_view::npos);
    CHECK(usage.find("77") != std::string_view::npos);
}
