// clang-format off
#include <wx/wx.h>  // first (M1b R11)
// clang-format on

#include "gui/about.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// clang-format off
#include <wx/aboutdlg.h>  // wxAboutDialogInfo, before the generic dialog
#include <wx/clipbrd.h>
#include <wx/filename.h>
#include <wx/generic/aboutdlgg.h>
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

// The About layout: the stock generic dialog plus the system lines, the
// Components pane and two buttons in the OK row. Create() runs here so DoAddCustomControls is ours.
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
        AddText(linesOf(content_.system, " "));
        AddCollapsiblePane("Components", linesOf(content_.components, " "));
    }

private:
    // "Third-party notices" and "Copy to clipboard" left of OK, in OK's sizer.
    void addButtons() {
        auto* notices = new wxButton(this, wxID_ANY, fromUtf8(ABOUT_NOTICES_LABEL));
        auto* copy = new wxButton(this, wxID_ANY, fromUtf8(ABOUT_COPY_LABEL));
        notices->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { openNotices(); });
        copy->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { copyText(); });
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

    void copyText() {
        if (wxTheClipboard->Open()) {
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the clipboard takes ownership
            if (wxTheClipboard->SetData(new wxTextDataObject(fromUtf8(text_)))) {
                // The text stays on the clipboard after rawreel exits.
                static_cast<void>(wxTheClipboard->Flush());
            }
            wxTheClipboard->Close();
        }
    }

    AboutContent content_;
    std::string text_;
};

}  // namespace

void showAbout(MainFrame& frame) {
    const VersionReport report{
        .packages = packageVersions(),
        .vulkan = vulkanStatusLine(frame.vulkanStatus()),
        // No directory or path in About (users share its text).
        .ffmpeg = ffmpegStatusLine(wxGetApp().startup().ffmpeg, StatusPaths::Hidden)};
    AboutContent content = aboutContent(report, wxGetApp().appearanceText());
    std::string text = aboutText(content);
    wxAboutDialogInfo info;
    info.SetName("rawreel");
    info.SetVersion(wxString::FromUTF8(report.packages.front().second));
    // The copyright goes in the description: wx would put SetCopyright below the custom
    // controls. No SetLicence: it would add a second dialog.
    info.SetDescription(wxString::FromUTF8(content.description + "\n" + content.copyright));
    const std::string_view site = content.website;
    info.SetWebSite(fromUtf8(site), fromUtf8(site.substr(site.find("//") + 2)));
    frame.noteAbout(text);  // lastAboutText(), aboutShown()
    AboutDialog dialog(info, &frame, std::move(content), std::move(text));  // parent: the frame
    static_cast<void>(showModal(dialog));  // the selftest hook closes it with EndModal
}

}  // namespace rawreel
