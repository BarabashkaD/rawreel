#pragma once

// The Info pane (design M3 D18): a read-only text of the open stream. Task 10 fills it with
// formatInfo.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <string>

namespace rawreel {

class InfoPanel final : public wxPanel {
public:
    explicit InfoPanel(wxWindow* parent);

    void setText(const std::string& text);  // UTF-8
    [[nodiscard]] wxString text() const;

private:
    wxTextCtrl* text_;  // a child window: wx owns it
};

}  // namespace rawreel
