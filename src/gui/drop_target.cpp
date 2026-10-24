// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/drop_target.h"

#include <utility>

#include "dvv/media/types.h"

namespace rawreel {

DropTarget::DropTarget(Handler handler) : handler_(std::move(handler)) {}

bool DropTarget::OnDropFiles(wxCoord /*x*/, wxCoord /*y*/, const wxArrayString& names) {
    std::vector<std::filesystem::path> files;
    files.reserve(names.size());
    for (const wxString& name : names) {
        files.push_back(dvv::media::pathFromUtf8(name.utf8_string()));  // never the ANSI code page
    }
    handler_(std::move(files));
    return true;
}

}  // namespace rawreel
