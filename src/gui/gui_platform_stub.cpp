// rawreel-gui's start-up on Windows and macOS (see gui_platform.h): nothing to prepare.
#include <optional>
#include <string>

#include "gui/gui_platform.h"

namespace rawreel {

std::optional<std::string> prepareGuiStartUp() {
    return std::nullopt;
}

}  // namespace rawreel
