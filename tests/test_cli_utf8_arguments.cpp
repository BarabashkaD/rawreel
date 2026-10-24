#include <doctest/doctest.h>

#if defined(_WIN32)
#include <array>
#include <string>

#include "cli/utf8_arguments.h"
#include "dvv/base/error.h"

TEST_CASE("utf8 arguments: conversion") {
    std::wstring program = L"rawreel-cli";
    std::wstring text = L"ä日本\U0001F39E";  // ä日本🎞
    std::array<wchar_t*, 2> argv{program.data(), text.data()};
    const auto converted = rawreel::cli::utf8Arguments(2, argv.data());
    REQUIRE(converted.has_value());
    REQUIRE(converted->size() == 2);
    CHECK((*converted)[0] == "rawreel-cli");
    CHECK((*converted)[1] == reinterpret_cast<const char*>(u8"ä日本\U0001F39E"));

    std::wstring unpaired = L"\xD800";
    std::array<wchar_t*, 1> bad{unpaired.data()};
    const auto failed = rawreel::cli::utf8Arguments(1, bad.data());
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().code == dvv::Errc::Format);
}
#endif
