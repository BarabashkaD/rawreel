// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/info_panel.h"

namespace rawreel {

InfoPanel::InfoPanel(wxWindow* parent)
    : wxPanel(parent),
      text_(new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                           wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP)) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(text_, wxSizerFlags(1).Expand());
    SetSizer(sizer);
}

void InfoPanel::setText(const std::string& text) {
    const wxString value = wxString::FromUTF8(text);
    if (text_->GetValue() != value) {
        text_->ChangeValue(value);  // no wxEVT_TEXT
    }
}

wxString InfoPanel::text() const {
    return text_->GetValue();
}

}  // namespace rawreel
