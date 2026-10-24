// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/format_dialog.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <wx/filedlg.h>

#include "dvv/media/pixel_format.h"
#include "dvv/media/types.h"

namespace rawreel {

namespace {

using dvv::media::ColourMatrix;
using dvv::media::ColourRange;
using dvv::media::Primaries;
using dvv::media::TransferFn;

template <class E>
struct Item {
    E value;
    const char* label;
};
// The choices in dialog order (the transfer order differs from the enum's).
constexpr std::array<Item<ColourMatrix>, 6> MATRICES{{{ColourMatrix::Unspecified, "Unspecified"},
                                                      {ColourMatrix::Bt601, "BT.601"},
                                                      {ColourMatrix::Bt709, "BT.709"},
                                                      {ColourMatrix::Bt2020Ncl, "BT.2020 NCL"},
                                                      {ColourMatrix::Rgb, "RGB"},
                                                      {ColourMatrix::Custom, "Custom"}}};
constexpr std::array<Item<ColourRange>, 3> RANGES{{{ColourRange::Unspecified, "Unspecified"},
                                                   {ColourRange::Limited, "Limited"},
                                                   {ColourRange::Full, "Full"}}};
constexpr std::array<Item<TransferFn>, 6> TRANSFERS{{{TransferFn::Unspecified, "Unspecified"},
                                                     {TransferFn::Srgb, "sRGB"},
                                                     {TransferFn::Bt1886, "BT.1886"},
                                                     {TransferFn::Linear, "Linear"},
                                                     {TransferFn::Pq, "PQ"},
                                                     {TransferFn::Hlg, "HLG"}}};
constexpr std::array<Item<Primaries>, 5> PRIMARIES{{{Primaries::Unspecified, "Unspecified"},
                                                    {Primaries::Bt601_525, "BT.601-525"},
                                                    {Primaries::Bt601_625, "BT.601-625"},
                                                    {Primaries::Bt709, "BT.709"},
                                                    {Primaries::Bt2020, "BT.2020"}}};

constexpr int MAX_SIZE = 65535;
// BT.709's weights: what the Kr and Kb fields show until a custom matrix is chosen.
constexpr std::string_view DEFAULT_KR = "0.2126";
constexpr std::string_view DEFAULT_KB = "0.0722";

template <class E, std::size_t N>
wxChoice* makeChoice(wxWindow* parent, const std::array<Item<E>, N>& items) {
    auto* choice = new wxChoice(parent, wxID_ANY);
    for (const Item<E>& item : items) {
        choice->Append(item.label);
    }
    choice->SetSelection(0);
    return choice;
}

template <class E, std::size_t N>
void select(wxChoice& choice, const std::array<Item<E>, N>& items, E value) {
    for (std::size_t i = 0; i < N; ++i) {
        if (items[i].value == value) {
            choice.SetSelection(static_cast<int>(i));
            return;
        }
    }
    choice.SetSelection(0);
}

template <class E, std::size_t N>
E selected(const wxChoice& choice, const std::array<Item<E>, N>& items) {
    const int index = choice.GetSelection();
    return index >= 0 && static_cast<std::size_t>(index) < N
               ? items[static_cast<std::size_t>(index)].value
               : items[0].value;
}

// The whole text as a float; NaN when it is not one (validateColour then refuses it).
float parseFloat(const wxString& text) {
    const std::string utf8 = text.utf8_string();
    float value = 0;
    const auto [end, error] = std::from_chars(utf8.data(), utf8.data() + utf8.size(), value);
    if (utf8.empty() || error != std::errc{} || end != utf8.data() + utf8.size()) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return value;
}

std::string fpsText(dvv::media::Rational fps) {
    return fps.den == 1 ? std::format("{}", fps.num) : std::format("{}/{}", fps.num, fps.den);
}

wxString candidateText(const dvv::media::Candidate& candidate) {
    return wxString::FromUTF8(
        std::format("{} % {}: {}", candidate.confidence, candidate.component, candidate.why));
}

}  // namespace

FormatDialog::FormatDialog(wxWindow* parent, const wxString& title, const FormatChoice& initial,
                           std::span<const dvv::media::Candidate> candidates,
                           const std::string& reason)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      candidates_(candidates.begin(), candidates.end()) {
    auto* top = new wxBoxSizer(wxVERTICAL);
    if (!reason.empty()) {
        auto* label = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(reason));
        label->Wrap(FromDIP(460));
        top->Add(label, wxSizerFlags().Expand().Border());
    }

    auto* grid = new wxFlexGridSizer(2, FromDIP(wxSize(8, 6)));
    grid->AddGrowableCol(1);
    const auto row = [this, grid](const wxString& label, wxWindow* control) {
        grid->Add(new wxStaticText(this, wxID_ANY, label), wxSizerFlags().CenterVertical());
        grid->Add(control, wxSizerFlags().Expand());
    };
    const auto row_sizer = [this, grid](const wxString& label, wxSizer* sizer) {
        grid->Add(new wxStaticText(this, wxID_ANY, label), wxSizerFlags().CenterVertical());
        grid->Add(sizer, wxSizerFlags().Expand());
    };

    // Format: every catalogue name, then the "@<path>" entry once one is loaded.
    format_ = new wxChoice(this, wxID_ANY);
    for (const dvv::media::FormatId id : dvv::media::catalogue()) {
        format_->Append(wxString::FromUTF8(dvv::media::descriptor(id).name));
    }
    auto* load = new wxButton(this, wxID_ANY, "Load JSON...");
    auto* format_row = new wxBoxSizer(wxHORIZONTAL);
    format_row->Add(format_, wxSizerFlags(1).CenterVertical());
    format_row->Add(load, wxSizerFlags().CenterVertical().Border(wxLEFT));
    row_sizer("Format:", format_row);

    width_ = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                            wxSP_ARROW_KEYS, 1, MAX_SIZE, 1920);
    height_ = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                             wxSP_ARROW_KEYS, 1, MAX_SIZE, 1080);
    auto* size_row = new wxBoxSizer(wxHORIZONTAL);
    size_row->Add(width_, wxSizerFlags(1).CenterVertical());
    size_row->Add(new wxStaticText(this, wxID_ANY, "x"),
                  wxSizerFlags().CenterVertical().Border(wxLEFT | wxRIGHT));
    size_row->Add(height_, wxSizerFlags(1).CenterVertical());
    row_sizer("Size:", size_row);

    fps_ = new wxTextCtrl(this, wxID_ANY, "25");
    fps_->SetHint("N, N/D or a decimal");
    row("Frame rate:", fps_);

    matrix_ = makeChoice(this, MATRICES);
    row("Matrix:", matrix_);
    kr_ = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(DEFAULT_KR));
    kb_ = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(DEFAULT_KB));
    auto* k_row = new wxBoxSizer(wxHORIZONTAL);
    k_row->Add(new wxStaticText(this, wxID_ANY, "Kr"), wxSizerFlags().CenterVertical());
    k_row->Add(kr_, wxSizerFlags(1).CenterVertical().Border(wxLEFT | wxRIGHT));
    k_row->Add(new wxStaticText(this, wxID_ANY, "Kb"), wxSizerFlags().CenterVertical());
    k_row->Add(kb_, wxSizerFlags(1).CenterVertical().Border(wxLEFT));
    row_sizer("Custom matrix:", k_row);
    range_ = makeChoice(this, RANGES);
    row("Range:", range_);
    transfer_ = makeChoice(this, TRANSFERS);
    row("Transfer:", transfer_);
    primaries_ = makeChoice(this, PRIMARIES);
    row("Primaries:", primaries_);
    top->Add(grid, wxSizerFlags().Expand().Border());

    if (!candidates_.empty()) {
        top->Add(new wxStaticText(this, wxID_ANY, "Candidates:"),
                 wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP));
        list_ = new wxListBox(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(460, 90)));
        for (const dvv::media::Candidate& candidate : candidates_) {
            list_->Append(candidateText(candidate));
        }
        top->Add(list_, wxSizerFlags().Expand().Border());
        list_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& /*event*/) { onCandidate(); });
    }

    error_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    error_->SetForegroundColour(*wxRED);
    top->Add(error_, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT));
    top->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), wxSizerFlags().Expand().Border());
    ok_ = dynamic_cast<wxButton*>(FindWindow(wxID_OK));

    const auto changed = [this](wxCommandEvent& /*event*/) { validate(); };
    format_->Bind(wxEVT_CHOICE, changed);
    width_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent& /*event*/) { validate(); });
    height_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent& /*event*/) { validate(); });
    width_->Bind(wxEVT_TEXT, changed);
    height_->Bind(wxEVT_TEXT, changed);
    fps_->Bind(wxEVT_TEXT, changed);
    matrix_->Bind(wxEVT_CHOICE, [this](wxCommandEvent& /*event*/) { onMatrix(); });
    kr_->Bind(wxEVT_TEXT, changed);
    kb_->Bind(wxEVT_TEXT, changed);
    range_->Bind(wxEVT_CHOICE, changed);
    transfer_->Bind(wxEVT_CHOICE, changed);
    primaries_->Bind(wxEVT_CHOICE, changed);
    load->Bind(wxEVT_BUTTON, [this](wxCommandEvent& /*event*/) { onLoadJson(); });

    SetSizerAndFit(top);
    setChoice(initial);
    // Room for the longest validation message without a relayout.
    SetMinSize(GetSize());
    CentreOnParent();
}

void FormatDialog::selectFormat(const std::string& format) {
    const wxString name = wxString::FromUTF8(format);
    if (format.starts_with('@')) {
        if (json_item_ == wxNOT_FOUND) {
            json_item_ = format_->Append(name);
        } else {
            format_->SetString(static_cast<unsigned int>(json_item_), name);
        }
        format_->SetSelection(json_item_);
        return;
    }
    int index = format_->FindString(name, true);
    if (index == wxNOT_FOUND) {
        index = format_->Append(name);  // not in the catalogue: validation names the problem
    }
    format_->SetSelection(index);
}

void FormatDialog::setChoice(const FormatChoice& choice) {
    selectFormat(choice.format);
    width_->SetValue(static_cast<int>(std::min<std::uint32_t>(choice.width, MAX_SIZE)));
    height_->SetValue(static_cast<int>(std::min<std::uint32_t>(choice.height, MAX_SIZE)));
    fps_->ChangeValue(wxString::FromUTF8(fpsText(choice.fps)));
    select(*matrix_, MATRICES, choice.colour.matrix);
    if (choice.colour.matrix == ColourMatrix::Custom) {
        kr_->ChangeValue(wxString::FromUTF8(std::format("{}", choice.colour.kr)));
        kb_->ChangeValue(wxString::FromUTF8(std::format("{}", choice.colour.kb)));
    }
    select(*range_, RANGES, choice.colour.range);
    select(*transfer_, TRANSFERS, choice.colour.transfer);
    select(*primaries_, PRIMARIES, choice.colour.primaries);
    onMatrix();  // enables Kr and Kb, then validates
}

FormatChoice FormatDialog::choice() const {
    FormatChoice choice;
    choice.format = format_->GetStringSelection().utf8_string();
    choice.width = static_cast<std::uint32_t>(std::max(width_->GetValue(), 0));
    choice.height = static_cast<std::uint32_t>(std::max(height_->GetValue(), 0));
    // Unparseable: 0/1, which validateChoice refuses (validate() names the fps rule first).
    choice.fps = parseFpsText(fps_->GetValue().utf8_string()).value_or(dvv::media::Rational{0, 1});
    choice.colour.matrix = selected(*matrix_, MATRICES);
    if (choice.colour.matrix == ColourMatrix::Custom) {
        choice.colour.kr = parseFloat(kr_->GetValue());
        choice.colour.kb = parseFloat(kb_->GetValue());
    }
    choice.colour.range = selected(*range_, RANGES);
    choice.colour.transfer = selected(*transfer_, TRANSFERS);
    choice.colour.primaries = selected(*primaries_, PRIMARIES);
    return choice;
}

void FormatDialog::validate() {
    wxString message;
    if (!parseFpsText(fps_->GetValue().utf8_string())) {
        message = "fps must be N, N/D or a decimal";
    } else if (const dvv::Result<void> valid = validateChoice(choice()); !valid) {
        message = wxString::FromUTF8(valid.error().message);
    }
    if (error_->GetLabel() != message) {
        error_->SetLabel(message);
        error_->Wrap(std::max(GetClientSize().x - FromDIP(20), FromDIP(200)));
        Layout();
    }
    if (ok_ != nullptr) {
        ok_->Enable(message.empty());
    }
}

void FormatDialog::onMatrix() {
    const bool custom = selected(*matrix_, MATRICES) == ColourMatrix::Custom;
    kr_->Enable(custom);
    kb_->Enable(custom);
    validate();
}

void FormatDialog::onLoadJson() {
    wxFileDialog dialog(this, "Load a custom pixel format", wxEmptyString, wxEmptyString,
                        "Pixel format (*.json)|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    selectFormat("@" + dialog.GetPath().utf8_string());
    validate();
}

void FormatDialog::onCandidate() {
    const int index = list_ != nullptr ? list_->GetSelection() : wxNOT_FOUND;
    if (index < 0 || static_cast<std::size_t>(index) >= candidates_.size()) {
        return;
    }
    FormatChoice next = choiceFromCandidate(candidates_[static_cast<std::size_t>(index)]);
    next.colour = choice().colour;  // a candidate names no colour: keep the fields
    setChoice(next);
}

}  // namespace rawreel
