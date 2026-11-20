#pragma once

// The Windows entries' arguments (M2 design D19): wmain receives UTF-16, rawreel works in UTF-8.
// Other platforms receive UTF-8 in main and need no conversion.

#include <string>
#include <vector>

#include "dvv/base/error.h"

namespace rawreel {

// Every argument (the program name included) converted to UTF-8. Errc::Format "argument <i> is not
// valid Unicode" for an unpaired surrogate.
[[nodiscard]] dvv::Result<std::vector<std::string>> utf8Arguments(int argc, wchar_t** argv);

}  // namespace rawreel
