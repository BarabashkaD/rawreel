// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <variant>

#include "native_window.h"

namespace rawreel {

// No native-window path for this toolkit yet: wxGTK/X11 follows with Linux support. The viewport
// then reports Unsupported. Built so that the GUI code keeps compiling on every platform.
dvv::platform::NativeWindow nativeWindowOf(wxWindow& /*window*/) {
    return std::monostate{};
}

}  // namespace rawreel
