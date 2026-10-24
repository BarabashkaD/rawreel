#include "cli/utf8_arguments.h"

// The whole body is Windows only; the file is compiled everywhere, so it stays in the compile
// database.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstddef>
#include <cwchar>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

namespace rawreel::cli {

dvv::Result<std::vector<std::string>> utf8Arguments(int argc, wchar_t** argv) {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        const wchar_t* wide = argv[i];
        const int length = static_cast<int>(std::wcslen(wide));
        if (length == 0) {
            arguments.emplace_back();
            continue;
        }
        const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, length, nullptr,
                                              0, nullptr, nullptr);
        if (bytes <= 0) {
            return dvv::makeError(dvv::Errc::Format,
                                  std::format("argument {} is not valid Unicode", i));
        }
        std::string text(static_cast<std::size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, length, text.data(), bytes,
                            nullptr, nullptr);
        arguments.push_back(std::move(text));
    }
    return arguments;
}

}  // namespace rawreel::cli
#endif
