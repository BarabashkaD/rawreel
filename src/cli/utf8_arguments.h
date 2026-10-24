#pragma once

// rawreel-cli's arguments on Windows (M2 design D19): wmain receives UTF-16, the CLI works in
// UTF-8. Other platforms receive UTF-8 in main and need no conversion.

#if defined(_WIN32)
#include <string>
#include <vector>

#include "dvv/base/error.h"

namespace rawreel::cli {

// Every argument (the program name included) converted to UTF-8. Errc::Format "argument <i> is not
// valid Unicode" for an unpaired surrogate.
[[nodiscard]] dvv::Result<std::vector<std::string>> utf8Arguments(int argc, wchar_t** argv);

}  // namespace rawreel::cli
#endif
