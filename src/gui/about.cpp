// clang-format off
#include <wx/wx.h>  // first (M1b R11)
// clang-format on

#include "gui/about.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// clang-format off
#include <wx/aboutdlg.h>  // wxAboutDialogInfo, before the generic dialog
#include <wx/clipbrd.h>
#include <wx/collpane.h>
#include <wx/filename.h>
#include <wx/generic/aboutdlgg.h>
#include <wx/hyperlink.h>
#include <wx/settings.h>
#include <wx/stdpaths.h>
// clang-format on

#include "core/startup.h"
#include "core/version_text.h"
#include "gui/app.h"
#include "gui/main_frame.h"
#include "gui/selftest.h"

namespace rawreel {

namespace {

wxString fromUtf8(std::string_view text) {
    return wxString::FromUTF8(text.data(), text.size());
}

// "<name> <version>" per line, for the collapsed pane.
wxString linesOf(const std::vector<std::pair<std::string, std::string>>& lines,
                 std::string_view separator) {
    std::string text;
    for (const auto& [first, second] : lines) {
        if (!text.empty()) {
            text += '\n';
        }
        text += first;
        text += separator;
        text += second;
    }
    return wxString::FromUTF8(text);
}

// The text stays on the clipboard after rawreel exits.
void copyToClipboard(const std::string& text) {
    if (wxTheClipboard->Open()) {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the clipboard takes ownership
        if (wxTheClipboard->SetData(new wxTextDataObject(fromUtf8(text)))) {
            static_cast<void>(wxTheClipboard->Flush());
        }
        wxTheClipboard->Close();
    }
}

// One line of texts and links in a row, on a panel with the background of `parent`.
wxWindow* lineRow(wxWindow* parent, const TextLine& line) {
    auto* row = new wxPanel(parent);
    row->SetBackgroundColour(parent->GetBackgroundColour());
    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    for (const TextPart& part : line) {
        wxWindow* control = nullptr;
        if (part.url.empty()) {
            control = new wxStaticText(row, wxID_ANY, fromUtf8(part.text));
        } else {
            control = new wxHyperlinkCtrl(row, wxID_ANY, fromUtf8(part.text), fromUtf8(part.url));
        }
        sizer->Add(control, wxSizerFlags().CentreVertical());
    }
    row->SetSizer(sizer);
    return row;
}

// The About layout: the stock generic dialog with the heading only; below it the description,
// the copyright line, the website, the FFmpeg and wxWidgets lines, the Components pane, and two
// buttons in the OK row. Create() runs here so DoAddCustomControls is ours.
class AboutDialog final : public wxGenericAboutDialog {
public:
    AboutDialog(const wxAboutDialogInfo& info, wxWindow* parent, AboutContent content,
                std::string text)
        : content_(std::move(content)), text_(std::move(text)) {
        if (Create(info, parent)) {
            addButtons();
        }
    }

protected:
    void DoAddCustomControls() override {
        wxWindow* const parent = GetCustomControlParent();
        // Two lines, broken after the colon, so the description does not set the dialog's width.
        std::string description = content_.description;
        if (const std::size_t colon = description.find(": "); colon != std::string::npos) {
            description.replace(colon, 2, ":\n");
        }
        AddText(fromUtf8(description));
        AddControl(lineRow(parent, content_.copyright));
        const std::string_view site = content_.website;
        AddControl(new wxHyperlinkCtrl(parent, wxID_ANY, fromUtf8(site.substr(site.find("//") + 2)),
                                       fromUtf8(site)));
        for (const TextLine& line : content_.system) {
            AddControl(lineRow(parent, line));
        }
        addComponentsPane();
    }

    // The stock AddCollapsiblePane() sizes its text for a third of the display, so opening the
    // pane widened the dialog to that. This pane is as wide as its longest line (at most 60
    // characters, longer lines wrap) and as tall as its lines. The dialog follows the pane in
    // fitToPane(), not in wx (see there).
    void addComponentsPane() {
        auto* pane = new wxCollapsiblePane(GetCustomControlParent(), wxID_ANY, "Components",
                                           wxDefaultPosition, wxDefaultSize,
                                           wxCP_DEFAULT_STYLE | wxCP_NO_TLW_RESIZE);
        pane->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED, [this, pane](wxCollapsiblePaneEvent& event) {
            event.Skip();
            fitToPane(*pane);
        });
        wxWindow* const inner = pane->GetPane();
        const wxString text = linesOf(content_.components, " ");
        const wxArrayString lines = wxSplit(text, '\n', '\0');
        int width = 0;
        for (const wxString& line : lines) {
            width = std::max(width, inner->GetTextExtent(line).x);
        }
        const int cap = 60 * inner->GetCharWidth();
        long style = wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE;
        if (width <= cap) {
            style |= wxTE_NO_VSCROLL;
        }
        auto* lines_text =
            new wxTextCtrl(inner, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, style);
        const int rows = static_cast<int>(lines.size()) + (width > cap ? 1 : 0);
        lines_text->SetInitialSize(lines_text->GetSizeFromTextSize(
            std::min(width, cap), rows * lines_text->GetCharHeight()));
        auto* sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(lines_text, wxSizerFlags(1).Expand());
        inner->SetSizer(sizer);
        AddControl(pane, wxSizerFlags(0).Expand().Border(wxBOTTOM));
    }

    // The dialog's height follows the pane; its width stays. A window manager may not shrink
    // the window when the pane closes (WSLg does not), so opening it again can change no window
    // size; then no size event lays out the panel that holds the pane, the pane keeps its closed
    // height and the list stays 1 pixel high. So the pane's parent is laid out here every time,
    // and the window stays resizable (wx's own resize on GTK makes it fixed-size while closed).
    void fitToPane(wxCollapsiblePane& pane) {
        for (wxWindow* window = &pane; window != nullptr && window != this;
             window = window->GetParent()) {
            window->InvalidateBestSize();
        }
        const wxSize fit = GetSizer()->ComputeFittingClientSize(this);
        SetMinClientSize(fit);
        SetClientSize(std::max(GetClientSize().x, fit.x), fit.y);
        Layout();
        pane.GetParent()->Layout();
    }

private:
    // "Third-party notices" and "Copy to clipboard" left of OK, in OK's sizer.
    void addButtons() {
        auto* notices = new wxButton(this, wxID_ANY, fromUtf8(ABOUT_NOTICES_LABEL));
        auto* copy = new wxButton(this, wxID_ANY, fromUtf8(ABOUT_COPY_LABEL));
        notices->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { openNotices(); });
        copy->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { copyToClipboard(text_); });
        wxWindow* const ok = FindWindow(wxID_OK);
        wxSizer* const row = ok != nullptr ? ok->GetContainingSizer() : nullptr;
        if (row != nullptr) {
            // At OK's index, next to it: on MSW the row has a stretch spacer before OK.
            // Tab order: notices, copy, OK.
            std::size_t index = 0;
            const wxSizerItemList& items = row->GetChildren();
            for (const wxSizerItem* item : items) {
                if (item->GetWindow() == ok) {
                    break;
                }
                ++index;
            }
            row->Insert(index, notices, wxSizerFlags().Border(wxRIGHT));
            row->Insert(index + 1, copy, wxSizerFlags().Border(wxRIGHT));
            notices->MoveBeforeInTabOrder(ok);
            copy->MoveBeforeInTabOrder(ok);
        } else {  // no OK row (e.g. macOS): under the text
            AddControl(notices);
            AddControl(copy);
        }
        GetSizer()->SetSizeHints(this);
        CentreOnParent();
    }

    void openNotices() {
        const wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
        const wxFileName file(exe.GetPath(), fromUtf8(ABOUT_NOTICES_FILE));
        if (!file.FileExists() || !wxLaunchDefaultApplication(file.GetFullPath())) {
            wxMessageBox(fromUtf8(ABOUT_NOTICES_FILE) +
                             " could not be opened. It is next to rawreel-gui in the package.",
                         "rawreel", wxOK | wxICON_INFORMATION, this);
        }
    }

    AboutContent content_;
    std::string text_;
};

}  // namespace

void showAbout(MainFrame& frame) {
    const VersionReport report{
        .packages = packageVersions(),
        .vulkan = {},  // Help -> GPU information
        // No directory or path in About (users share its text).
        .ffmpeg = ffmpegStatusLine(wxGetApp().startup().ffmpeg, StatusPaths::Hidden)};
    AboutContent content = aboutContent(report);
    std::string text = aboutText(content);
    wxAboutDialogInfo info;
    info.SetName("rawreel");
    info.SetVersion(wxString::FromUTF8(report.packages.front().second));
    // No description, copyright or website here: wx would put them as plain text or out of
    // order; DoAddCustomControls adds them with their links. No SetLicence: it would add a
    // second dialog.
    frame.noteAbout(text);  // lastAboutText(), aboutShown()
    AboutDialog dialog(info, &frame, std::move(content), std::move(text));  // parent: the frame
    static_cast<void>(showModal(dialog));  // the selftest hook closes it with EndModal
}

}  // namespace rawreel
