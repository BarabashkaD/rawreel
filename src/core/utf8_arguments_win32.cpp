#include "core/utf8_arguments_win32.h"

#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "dvv/base/utf8_win32.h"

namespace rawreel {

dvv::Result<std::vector<std::string>> utf8Arguments(int argc, wchar_t** argv) {
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        std::optional<std::string> text = dvv::utf8FromWide(argv[i]);
        if (!text) {
            return dvv::makeError(dvv::Errc::Format,
                                  std::format("argument {} is not valid Unicode", i));
        }
        result.push_back(std::move(*text));
    }
    return result;
}

}  // namespace rawreel
