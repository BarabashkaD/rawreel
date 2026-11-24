// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/info_window.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <wx/simplebook.h>

#include "core/commands.h"
#include "core/info_text.h"
#include "core/session_types.h"
#include "gui/floating_window.h"
#include "gui/main_frame.h"
#include "gui/native_controls.h"
#include "gui/native_policy.h"
#include "gui/selftest.h"  // setClipboardText

namespace rawreel {

namespace {

constexpr int COLUMN_GAP_DIP = 12;  // §11: between the label and the value column
constexpr int ROW_GAP_DIP = 4;      // between rows, and below a heading

}  // namespace

InfoWindow::InfoWindow(MainFrame& owner) : FloatingWindow(owner, FloatingKind::Info, "Info") {
    book_ = new wxSimplebook(&content());

    auto* const rows_page = new wxPanel(book_);
    auto* const rows_sizer = new wxBoxSizer(wxVERTICAL);
    const std::vector<InfoRow> rows = infoRows(StreamInfo{});  // the labels, in order
    const int gap = FromDIP(ROW_GAP_DIP);
    for (std::size_t group = 0; group < 2; ++group) {
        if (group > 0) {
            rows_sizer->AddSpacer(GetCharHeight());  // §11: one line between the groups
        }
        auto* const heading =
            new wxStaticText(rows_page, wxID_ANY, group == 0 ? "Picture" : "Colour");
        heading->SetFont(heading->GetFont().Bold());
        rows_sizer->Add(heading);
        rows_sizer->AddSpacer(gap);
        auto* const grid = new wxFlexGridSizer(2, gap, FromDIP(COLUMN_GAP_DIP));
        const std::size_t first = group == 0 ? 0 : INFO_PICTURE_ROWS;
        const std::size_t end = group == 0 ? INFO_PICTURE_ROWS : rows.size();
        for (std::size_t i = first; i < end; ++i) {
            auto* const label = new wxStaticText(
                rows_page, wxID_ANY, wxString::FromUTF8(rows[i].label), wxDefaultPosition,
                wxDefaultSize, native_policy::labelAlignment() | wxST_NO_AUTORESIZE);
            auto* const value = new wxStaticText(rows_page, wxID_ANY, wxString(), wxDefaultPosition,
                                                 wxDefaultSize, wxST_NO_AUTORESIZE);
            labels_.push_back(label);
            values_.push_back(value);
            grid->Add(label, wxSizerFlags().Expand());
            grid->Add(value, wxSizerFlags().Expand());
        }
        rows_sizer->Add(grid);
    }
    rows_page->SetSizer(rows_sizer);
    book_->AddPage(rows_page, "rows");

    auto* const empty_page = new wxPanel(book_);
    auto* const empty_sizer = new wxBoxSizer(wxVERTICAL);
    no_stream_ = new wxStaticText(empty_page, wxID_ANY, "No stream");
    empty_sizer->AddStretchSpacer();
    empty_sizer->Add(no_stream_, wxSizerFlags().Center());
    empty_sizer->AddStretchSpacer();
    empty_page->SetSizer(empty_sizer);
    book_->AddPage(empty_page, "no stream");

    auto* const content_sizer = new wxBoxSizer(wxVERTICAL);
    content_sizer->Add(book_, wxSizerFlags(1).Expand().Border());  // the OS's dialog margins
    content().SetSizer(content_sizer);

    applyColours();
    measure();
    showFor(nullptr);
    Fit();  // once: the size is fixed from now on (§11)
    const std::vector<StreamInfo>& streams = owner.streams();
    showFor(streams.empty() ? nullptr : &streams.front());
}

void InfoWindow::measure() {
    // §11: the value column is as wide as the widest sample (the catalogue's formats among them),
    // so no value is cut and the window never changes size; the label columns of both groups are
    // as wide as the widest label.
    int value_width = 0;
    for (const std::string& sample : infoValueSamples()) {
        value_width = std::max(
            value_width, values_.front()->GetTextExtent(wxString::FromUTF8(sample)).GetWidth());
    }
    int label_width = 0;
    for (const wxStaticText* const label : labels_) {
        label_width = std::max(label_width, label->GetTextExtent(label->GetLabel()).GetWidth());
    }
    for (wxStaticText* const value : values_) {
        value->SetMinSize(wxSize(value_width, -1));
    }
    for (wxStaticText* const label : labels_) {
        label->SetMinSize(wxSize(label_width, -1));
    }
}

void InfoWindow::applyColours() {
    for (wxStaticText* const label : labels_) {
        setSecondaryText(*label);
    }
    setSecondaryText(*no_stream_);
}

void InfoWindow::showFor(const StreamInfo* stream) {
    if (stream == nullptr) {
        book_->ChangeSelection(1);
        return;
    }
    const std::vector<InfoRow> rows = infoRows(*stream);
    for (std::size_t i = 0; i < rows.size() && i < values_.size(); ++i) {
        const wxString label = wxString::FromUTF8(rows[i].label);
        const wxString value = wxString::FromUTF8(rows[i].value);
        values_[i]->SetLabelText(value);                       // no mnemonics: a value may hold '&'
        setAccessibleName(*values_[i], label + ", " + value);  // §11: read as one item
    }
    book_->ChangeSelection(0);
    book_->GetPage(0)->Layout();
}

std::vector<std::pair<std::string, std::string>> InfoWindow::rowsForTest() const {
    std::vector<std::pair<std::string, std::string>> rows;
    if (book_->GetSelection() != 0) {
        return rows;
    }
    for (std::size_t i = 0; i < labels_.size() && i < values_.size(); ++i) {
        rows.emplace_back(labels_[i]->GetLabel().utf8_string(),
                          values_[i]->GetLabelText().utf8_string());
    }
    return rows;
}

// Copy: on macOS Edit -> Copy does it (MainFrame::onEditMenu).
bool InfoWindow::handleKey(const KeyChord& key) {
    if (native_policy::infoCopiesOnCtrlC() && key == KeyChord{.key = 'C', .ctrl = true}) {
        const std::vector<StreamInfo>& streams = owner().streams();
        setClipboardText(infoCopyText(streams.empty() ? nullptr : &streams.front()));
        return true;
    }
    return false;
}

}  // namespace rawreel
