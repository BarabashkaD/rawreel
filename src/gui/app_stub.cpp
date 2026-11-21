// App's members that only wxOSX has a body for (app_cocoa.mm), on the other toolkits: the
// application keeps wx's display name.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/app.h"

namespace rawreel {

void App::setApplicationMenuName() {}

}  // namespace rawreel
