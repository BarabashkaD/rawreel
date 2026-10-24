#pragma once

// A file drop on the viewport or the player panel (design M3 v2 D21, R5): the dropped names go to
// the handler as paths (UTF-8, never the ANSI code page); MainFrame::openFiles routes them.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <filesystem>
#include <functional>
#include <vector>

#include <wx/dnd.h>

namespace rawreel {

class DropTarget final : public wxFileDropTarget {
public:
    using Handler = std::function<void(std::vector<std::filesystem::path>)>;
    explicit DropTarget(Handler handler);
    bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& names) override;

private:
    Handler handler_;
};

}  // namespace rawreel
