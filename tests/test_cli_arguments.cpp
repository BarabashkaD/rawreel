#include <doctest/doctest.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cli/arguments.h"
#include "dvv/base/error.h"

using rawreel::cli::Arguments;
using rawreel::cli::Command;
using rawreel::cli::Via;
using rawreel::cli::View;

namespace {

dvv::Result<Arguments> parse(const std::vector<std::string_view>& args) {
    return rawreel::cli::parseArguments(std::span<const std::string_view>(args));
}

// The parse must fail with Errc::Format, and the message must contain `expected`.
void checkError(const std::vector<std::string_view>& args, std::string_view expected) {
    const auto parsed = parse(args);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error().code == dvv::Errc::Format);
    CHECK_MESSAGE(parsed.error().message.find(expected) != std::string::npos,
                  parsed.error().message);
}

}  // namespace

TEST_CASE("arguments: info") {
    const auto plain = parse({"info", "x.yuv"});
    REQUIRE(plain.has_value());
    CHECK(plain->command == Command::Info);
    CHECK(plain->uri == "x.yuv");
    CHECK_FALSE(plain->json);

    const auto json = parse({"info", "x.yuv", "--json"});
    REQUIRE(json.has_value());
    CHECK(json->command == Command::Info);
    CHECK(json->uri == "x.yuv");
    CHECK(json->json);
}

TEST_CASE("arguments: dump with every option") {
    const auto parsed = parse({"dump", "u", "--out", "o", "--frame", "2", "--count", "3", "--view",
                               "planes", "--via", "gpu", "--json"});
    REQUIRE(parsed.has_value());
    CHECK(parsed->command == Command::Dump);
    CHECK(parsed->uri == "u");
    CHECK(parsed->out == "o");
    CHECK(parsed->frame == 2);
    CHECK(parsed->count == 3);
    CHECK(parsed->view == View::Planes);
    CHECK(parsed->via == Via::Gpu);
    CHECK(parsed->json);
}

TEST_CASE("arguments: dump defaults") {
    const auto parsed = parse({"dump", "u", "--out", "o"});
    REQUIRE(parsed.has_value());
    CHECK(parsed->command == Command::Dump);
    CHECK(parsed->frame == 0);
    CHECK(parsed->count == 1);
    CHECK(parsed->view == View::Original);
    CHECK(parsed->via == Via::Auto);
    CHECK_FALSE(parsed->json);
}

TEST_CASE("arguments: bench defaults") {
    const auto parsed = parse({"bench", "u"});
    REQUIRE(parsed.has_value());
    CHECK(parsed->command == Command::Bench);
    CHECK(parsed->uri == "u");
    CHECK(parsed->frames == 60);
    CHECK(parsed->inflight == 4);
}

TEST_CASE("arguments: version and help") {
    const auto version = parse({"--version"});
    REQUIRE(version.has_value());
    CHECK(version->command == Command::Version);

    const auto help = parse({"--help"});
    REQUIRE(help.has_value());
    CHECK(help->command == Command::Help);

    checkError({"--version", "x"}, "unexpected argument 'x'");
}

TEST_CASE("arguments: errors") {
    checkError({}, "missing command");
    checkError({"play"}, "unknown command");
    checkError({"dump", "u"}, "--out");
    checkError({"info"}, "missing");
    checkError({"info", "a", "b"}, "unexpected argument");
    checkError({"info", "u", "--out", "o"}, "not valid for info");
    checkError({"dump", "u", "--out"}, "needs a value");
    checkError({"dump", "u", "--out", "o", "--frame", "-1"}, "--frame");
    checkError({"dump", "u", "--out", "o", "--count", "0"}, "--count");
    checkError({"dump", "u", "--out", "o", "--view", "x"}, "--view");
    checkError({"dump", "u", "--out", "o", "--via", "x"}, "--via");
    checkError({"dump", "u", "--out", "o", "--out", "p"}, "given twice");
    checkError({"info", "u", "--bogus"}, "unknown option");
    checkError({"dump", "u", "--out", "o", "--frame", "x"}, "bad number");
}
