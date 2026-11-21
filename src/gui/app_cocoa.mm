// App's members that only wxOSX (Cocoa) has a body for: the application menu's name and the files
// opened from Finder or the Dock.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <filesystem>
#include <utility>
#include <vector>

#include "dvv/base/log.h"
#include "dvv/base/utf8.h"
#include "gui/app.h"
#include "gui/main_frame.h"

namespace rawreel {

void App::setApplicationMenuName() {
    SetAppDisplayName("rawreel");
}

void App::MacOpenFiles(const wxArrayString& names) {
    std::vector<std::filesystem::path> files;
    files.reserve(names.size());
    for (const wxString& name : names) {
        files.push_back(dvv::pathFromText(name.utf8_string()));
    }
    DVV_LOG_INFO("{} files opened from Finder or the Dock", files.size());
    if (MainFrame* frame = mainFrame(); frame != nullptr) {
        frame->openFiles(std::move(files));  // returns at once while the frame closes
    }
}

}  // namespace rawreel
