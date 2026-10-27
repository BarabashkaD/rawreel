// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/native_window.h"

namespace rawreel {

// No native-window path for this toolkit yet (MSW and wxGTK have theirs): the viewport reports a
// failure and paints its background. Keeps the GUI code compiling on other platforms.
dvv::Result<ViewportWindow> nativeWindowOf(wxWindow& /*window*/) {
    return dvv::makeError(dvv::Errc::Internal,
                          "the viewport has no native window path for this GUI toolkit");
}

void syncNativeWindow(wxWindow& /*window*/) {}

}  // namespace rawreel
