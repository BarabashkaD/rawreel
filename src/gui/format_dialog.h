#pragma once

// The format dialog (design M3 v2 D21, X7, X8, §7): pixel format (catalogue or a JSON file), size,
// fps, colour with a custom Kr/Kb matrix, and the source factory's candidates. Shown for the
// Session's formatNeeded and for View -> Format...; every ShowModal goes through showModal()
// (selftest.h).

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <span>
#include <string>
#include <vector>

#include <wx/listbox.h>
#include <wx/spinctrl.h>

#include "core/format_choice.h"
#include "dvv/media/source_factory.h"

namespace rawreel {

class FormatDialog final : public wxDialog {
public:
    FormatDialog(wxWindow* parent, const wxString& title, const FormatChoice& initial,
                 std::span<const dvv::media::Candidate> candidates, const std::string& reason);

    void setChoice(const FormatChoice& choice);  // fills every control (hooks use it too)
    [[nodiscard]] FormatChoice choice() const;   // reads the controls
    // The validation message of the current controls; empty when OK is enabled (§7 "OK stays
    // disabled").
    [[nodiscard]] wxString errorText() const { return error_->GetLabel(); }

private:
    void validate();  // after every change
    void onMatrix();
    void onLoadJson();
    void onCandidate();
    void selectFormat(const std::string& format);

    std::vector<dvv::media::Candidate> candidates_;
    // Child windows: wx owns them.
    wxChoice* format_ = nullptr;
    int json_item_ = wxNOT_FOUND;  // the "@<path>" entry of format_, when there is one
    wxSpinCtrl* width_ = nullptr;
    wxSpinCtrl* height_ = nullptr;
    wxTextCtrl* fps_ = nullptr;
    wxChoice* matrix_ = nullptr;
    wxTextCtrl* kr_ = nullptr;
    wxTextCtrl* kb_ = nullptr;
    wxChoice* range_ = nullptr;
    wxChoice* transfer_ = nullptr;
    wxChoice* primaries_ = nullptr;
    wxListBox* list_ = nullptr;
    wxStaticText* error_ = nullptr;
    wxButton* ok_ = nullptr;
};

}  // namespace rawreel
