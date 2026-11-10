// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/settings.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <wx/choice.h>
#include <wx/dirdlg.h>
#include <wx/preferences.h>
#include <wx/radiobut.h>

#include "core/config.h"
#include "core/config_writer.h"
#include "core/log_file_switch.h"
#include "core/log_level.h"
#include "dvv/base/log.h"
#include "dvv/media/types.h"
#include "gui/app.h"
#include "gui/main_frame.h"
#include "gui/native_controls.h"
#include "gui/selftest.h"

namespace rawreel {

namespace {

// Spec §6.3: the line at the top of each page in a second copy.
constexpr std::string_view SECOND_COPY_TEXT =
    "Another rawreel window owns the settings. Changes here apply to this window only and are "
    "not saved.";

// Spec §5.2: the labels of each OS.
#if defined(__WXOSX__)
constexpr std::string_view REVEAL_LABEL = "Show in Finder";
constexpr std::string_view SYSTEM_LOG_LABEL =
    "Send warnings and errors to the system log (Console)";
#elif defined(__WXMSW__)
constexpr std::string_view REVEAL_LABEL = "Show in Explorer";
// SystemLogSink writes with OutputDebugStringW there, which is not the Event Log.
constexpr std::string_view SYSTEM_LOG_LABEL = "Send warnings and errors to the debugger output";
#else
constexpr std::string_view REVEAL_LABEL = "Open Folder";
constexpr std::string_view SYSTEM_LOG_LABEL =
    "Send warnings and errors to the system log (journal/syslog)";
#endif

// The File reads buttons, in order.
constexpr std::array<dvv::platform::FileReadMode, 3> READ_MODES{
    dvv::platform::FileReadMode::Auto, dvv::platform::FileReadMode::Unbuffered,
    dvv::platform::FileReadMode::Buffered};
constexpr std::array<std::string_view, 3> READ_LABELS{"Automatic", "Unbuffered (no OS cache)",
                                                      "Buffered (OS cache)"};

// Log level design §2.2: the Level choice's items, in this order.
constexpr std::array<dvv::LogLevel, 5> LEVELS{dvv::LogLevel::Error, dvv::LogLevel::Warn,
                                              dvv::LogLevel::Info, dvv::LogLevel::Debug,
                                              dvv::LogLevel::Trace};

wxString fromUtf8(std::string_view text) {
    return wxString::FromUTF8(text.data(), text.size());
}

wxString pathText(const std::filesystem::path& path) {
    return wxString::FromUTF8(dvv::media::utf8FromPath(path));
}

// The same folder: the same path, or one std::filesystem::equivalent to it (both exist).
bool sameFolder(const std::filesystem::path& a, const std::filesystem::path& b) {
    if (a.empty() || b.empty()) {
        return false;
    }
    std::error_code error;
    return a == b || (std::filesystem::equivalent(a, b, error) && !error);
}

bool isFolder(const std::filesystem::path& path) {
    std::error_code error;
    return !path.empty() && std::filesystem::is_directory(path, error) && !error;
}

// The second-copy line at the top of a page (spec §6.3), when this process does not own the
// settings.
void addSecondCopyLine(wxWindow& page, wxSizer& sizer) {
    if (wxGetApp().ownsSettings()) {
        return;
    }
    auto* line = new wxStaticText(&page, wxID_ANY, fromUtf8(SECOND_COPY_TEXT));
    line->Wrap(page.FromDIP(420));
    sizer.Add(line, wxSizerFlags().Expand().Border(wxALL));
}

}  // namespace

wxString settingsTitle() {
#if defined(__WXGTK__)
    return "Preferences";
#else
    return "Settings";
#endif
}

// --- the Log page -------------------------------------------------------------------------------

LogSettingsPage::LogSettingsPage(wxWindow* parent, MainFrame& frame, SettingsWindow& owner)
    : wxPanel(parent), frame_(&frame), owner_(&owner) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    addSecondCopyLine(*this, *sizer);

    // Log level design §2.2: first on the page.
    auto* level_row = new wxBoxSizer(wxHORIZONTAL);
    level_row->Add(new wxStaticText(this, wxID_ANY, "Level:"),
                   wxSizerFlags().CenterVertical().Border(wxRIGHT));
    level_ = new wxChoice(this, wxID_ANY);
    for (const dvv::LogLevel level : LEVELS) {
        level_->Append(fromUtf8(levelShownName(level)));
    }
    level_->Bind(wxEVT_CHOICE, [this](wxCommandEvent& event) {
        const int index = event.GetSelection();
        if (index >= 0 && static_cast<std::size_t>(index) < LEVELS.size()) {
            setLevel(LEVELS[static_cast<std::size_t>(index)]);
        }
    });
    level_row->Add(level_, wxSizerFlags().CenterVertical());
    sizer->Add(level_row, wxSizerFlags().Border(wxALL));

    file_box_ = new wxCheckBox(this, wxID_ANY, "Write a log file");
    file_box_->Bind(wxEVT_CHECKBOX,
                    [this](wxCommandEvent& event) { setFileEnabled(event.IsChecked()); });
    sizer->Add(file_box_, wxSizerFlags().Border(wxALL));

    auto* folder_row = new wxBoxSizer(wxHORIZONTAL);
    folder_row->Add(new wxStaticText(this, wxID_ANY, "Folder:"),
                    wxSizerFlags().CenterVertical().Border(wxRIGHT));
    folder_ = new wxTextCtrl(this, wxID_ANY, wxString(), wxDefaultPosition,
                             wxSize(FromDIP(360), -1), wxTE_READONLY);
    folder_row->Add(folder_, wxSizerFlags(1).CenterVertical());
    sizer->Add(folder_row, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT));

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* choose = new wxButton(this, wxID_ANY, "Choose...");
    choose->Bind(wxEVT_BUTTON, [this](wxCommandEvent& /*event*/) { chooseFolder(); });
    use_default_ = new wxButton(this, wxID_ANY, "Use Default");
    use_default_->Bind(wxEVT_BUTTON, [this](wxCommandEvent& /*event*/) { useDefault(); });
    reveal_ = new wxButton(this, wxID_ANY, fromUtf8(REVEAL_LABEL));
    reveal_->Bind(wxEVT_BUTTON, [this](wxCommandEvent& /*event*/) { reveal(); });
    buttons->Add(choose, wxSizerFlags().Border(wxRIGHT));
    buttons->Add(use_default_, wxSizerFlags().Border(wxRIGHT));
    buttons->Add(reveal_);
    sizer->Add(buttons, wxSizerFlags().Align(wxALIGN_RIGHT).Border(wxALL));

    unavailable_ = new wxStaticText(this, wxID_ANY, wxString());
    sizer->Add(unavailable_, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM));

    system_box_ = new wxCheckBox(this, wxID_ANY, fromUtf8(SYSTEM_LOG_LABEL));
    system_box_->Bind(wxEVT_CHECKBOX,
                      [this](wxCommandEvent& event) { setSystemLog(event.IsChecked()); });
    sizer->Add(system_box_, wxSizerFlags().Border(wxALL));

    SetSizerAndFit(sizer);
#if defined(__WXGTK__)
    // Spec §5.1: the window is only hidden between shows; wx unbinds when the page goes.
    if (wxWindow* top = wxGetTopLevelParent(this); top != nullptr) {
        top->Bind(wxEVT_SHOW, &LogSettingsPage::onTopLevelShown, this);
    }
#endif
    (void)TransferDataToWindow();  // spec §5.1: the values when created
}

bool LogSettingsPage::active() const {
    return frame_ && !frame_->closing();
}

LogSettingsPage::Choice LogSettingsPage::applied() const {
    const App& app = wxGetApp();
    return {.level = app.savedLevel(),
            .fileEnabled = app.config().log.file.enabled,
            .folder = app.config().log.file.folder,
            .systemLog = app.systemLogEnabled()};
}

dvv::Result<std::filesystem::path> LogSettingsPage::folderOf(const Choice& choice) const {
    if (choice.folder.empty()) {
        return wxGetApp().defaultLogFolder();
    }
    return choice.folder;
}

void LogSettingsPage::chooseFolder() {
    if (!active()) {
        return;
    }
    const dvv::Result<std::filesystem::path> start = folderOf(wanted_);
    wxDirDialog dialog(wxGetTopLevelParent(this), "Choose the log folder",
                       start ? pathText(*start) : wxString());
    if (showModal(dialog) != wxID_OK || !active()) {
        return;
    }
    const std::filesystem::path chosen = dvv::media::pathFromUtf8(chosenFolder(dialog));
    if (chosen.empty()) {
        return;
    }
    // Spec §5.2: the default folder's own path is stored as "".
    wanted_.folder = wxGetApp().isDefaultLogFolder(chosen) ? std::filesystem::path() : chosen;
    folder_chosen_ = true;
    changed();
}

void LogSettingsPage::useDefault() {
    if (!active()) {
        return;
    }
    wanted_.folder.clear();
    folder_chosen_ = true;
    changed();
}

void LogSettingsPage::reveal() {
    if (!active() || !revealEnabled()) {
        return;
    }
    std::filesystem::path folder = wxGetApp().logFolderInUse();
    if (folder.empty()) {
        folder = folderOf(wanted_).value_or(std::filesystem::path());
    }
    const std::filesystem::path file = wxGetApp().currentLogFile();
    std::error_code error;
    revealFile(!file.empty() && std::filesystem::exists(file, error) && !error ? file : folder);
}

void LogSettingsPage::setFileEnabled(bool on) {
    if (!active()) {
        return;
    }
    wanted_.fileEnabled = on;
    changed();
}

void LogSettingsPage::setLevel(dvv::LogLevel level) {
    if (!active()) {
        return;
    }
    wanted_.level = level;
    level_chosen_ = true;
    changed();
}

dvv::LogLevel LogSettingsPage::level() const {
    const int index = level_->GetSelection();
    return index >= 0 && static_cast<std::size_t>(index) < LEVELS.size()
               ? LEVELS[static_cast<std::size_t>(index)]
               : dvv::LogLevel::Info;
}

void LogSettingsPage::setSystemLog(bool on) {
    if (!active()) {
        return;
    }
    wanted_.systemLog = on;
    changed();
}

bool LogSettingsPage::useDefaultEnabled() const {
    return !wanted_.folder.empty() && !wxGetApp().isDefaultLogFolder(wanted_.folder);
}

bool LogSettingsPage::revealEnabled() const {
    const std::filesystem::path in_use = wxGetApp().logFolderInUse();
    if (!in_use.empty()) {
        return isFolder(in_use);
    }
    const dvv::Result<std::filesystem::path> folder = folderOf(wanted_);
    return folder && isFolder(*folder);
}

wxString LogSettingsPage::folderText() const {
    return folder_->GetValue();
}

wxString LogSettingsPage::folderHint() const {
    return folder_->GetHint();
}

bool LogSettingsPage::TransferDataToWindow() {
    if (!active()) {
        return true;
    }
    wanted_ = applied();
    folder_chosen_ = false;  // a choice in a cancelled dialog does not linger
    level_chosen_ = false;
    prepared_.reset();
    refresh();
    return wxPanel::TransferDataToWindow();
}

bool LogSettingsPage::Validate() {
    if (!active() || wxPreferencesEditor::ShouldApplyChangesImmediately()) {
        return wxPanel::Validate();
    }
    return prepare() && wxPanel::Validate();
}

bool LogSettingsPage::TransferDataFromWindow() {
    if (active() && !wxPreferencesEditor::ShouldApplyChangesImmediately()) {
        std::vector<ConfigValue> values;
        apply(values);
        owner_->addPending(values);
    }
    return wxPanel::TransferDataFromWindow();
}

void LogSettingsPage::changed() {
    if (!wxPreferencesEditor::ShouldApplyChangesImmediately()) {
        refresh();  // Windows: OK applies (Validate, TransferDataFromWindow)
        return;
    }
    if (!prepare()) {
        wanted_ = applied();  // spec §5.3: the field keeps the old value
        folder_chosen_ = false;
        level_chosen_ = false;
        refresh();
        return;
    }
    std::vector<ConfigValue> values;
    apply(values);
    if (!values.empty()) {
        (void)wxGetApp().saveSettings(values, /*closing=*/false);  // a failure is reported there
    }
    refresh();
}

bool LogSettingsPage::prepare() {
    prepared_.reset();
    const Choice now = applied();
    // Nothing to open when the file is off, or when neither the box nor the folder changed and no
    // folder was chosen (a system log change never retries a folder that failed at start). A
    // chosen folder that is already in use is not reopened (below).
    const bool chosen = std::exchange(folder_chosen_, false);
    if (!wanted_.fileEnabled ||
        (wanted_.fileEnabled == now.fileEnabled && wanted_.folder == now.folder && !chosen)) {
        return true;
    }
    App& app = wxGetApp();
    const dvv::Result<std::filesystem::path> folder = folderOf(wanted_);
    std::string reason;
    if (folder) {
        // Spec §5.3: a folder already in use (Use Default after a fallback) is not reopened.
        if (sameFolder(*folder, app.logFolderInUse())) {
            return true;
        }
        dvv::Result<std::shared_ptr<dvv::SplitFileSink>> opened = app.prepareLogFile(*folder);
        if (opened) {
            prepared_ = Prepared{.sink = std::move(*opened), .folder = *folder};
            return true;
        }
        reason = opened.error().message;
    } else {
        reason = folder.error().message;
    }
    // The old file stays in use; a native message gives the path and the reason.
    const std::string path = dvv::media::utf8FromPath(folder ? *folder : wanted_.folder);
    DVV_LOG_WARN("log folder '{}' cannot be used: {}", path, reason);
    wxMessageDialog dialog(wxGetTopLevelParent(this), "The log folder cannot be used.", "rawreel",
                           wxOK | wxICON_ERROR);
    dialog.SetExtendedMessage(wxString::FromUTF8(std::format("'{}': {}", path, reason)));
    (void)showModal(dialog);
    // The flag clears only once prepared: a second Windows OK with the same folder retries it.
    folder_chosen_ = chosen;
    return false;
}

void LogSettingsPage::apply(std::vector<ConfigValue>& values) {
    App& app = wxGetApp();
    const Choice before = applied();
    if (!wanted_.fileEnabled) {
        app.stopLogFile();
    } else if (prepared_) {
        app.useLogFile(std::move(prepared_->sink), std::move(prepared_->folder));
    }
    prepared_.reset();
    if (wanted_.fileEnabled != before.fileEnabled) {
        values.push_back({.key = "log.file.enabled", .value = wanted_.fileEnabled});
    }
    if (wanted_.folder != before.folder) {
        values.push_back(
            {.key = "log.file.folder", .value = dvv::media::utf8FromPath(wanted_.folder)});
    }
    app.setLogFileConfig(wanted_.fileEnabled, wanted_.folder);
    if (wanted_.systemLog != before.systemLog) {
        app.useSystemLog(wanted_.systemLog);
        values.push_back({.key = "log.system.enabled", .value = wanted_.systemLog});
    }
    // Log level design §2.2: a changed level is saved (only the owner saves) and becomes the
    // active level, which ends a run-only level. On macOS and GTK choosing the saved level does
    // that too; on Windows OK with the control unchanged leaves a run-only level as it is.
    const bool level_chosen = std::exchange(level_chosen_, false);
    const bool level_changed = wanted_.level != before.level;
    if (level_changed) {
        app.setLevelConfig(wanted_.level);
        values.push_back(
            {.key = "log.level", .value = std::string(levelConfigValue(wanted_.level))});
    }
    if (level_changed || (level_chosen && wxPreferencesEditor::ShouldApplyChangesImmediately())) {
        app.setActiveLevel(wanted_.level,
                           app.ownsSettings() ? LevelSource::Settings : LevelSource::ThisWindow);
    }
}

void LogSettingsPage::refresh() {
    const App& app = wxGetApp();
    const auto level = std::ranges::find(LEVELS, wanted_.level);
    level_->SetSelection(level != LEVELS.end() ? static_cast<int>(level - LEVELS.begin())
                                               : wxNOT_FOUND);
    file_box_->SetValue(wanted_.fileEnabled);
    folder_->ChangeValue(pathText(wanted_.folder));
    const dvv::Result<std::filesystem::path> fallback = app.defaultLogFolder();
    folder_->SetHint(fallback ? pathText(*fallback) : wxString());
    use_default_->Enable(useDefaultEnabled());
    reveal_->Enable(revealEnabled());
    system_box_->SetValue(wanted_.systemLog);
    // Spec §5.2: the folder in use is not the saved one (a fallback at start).
    const std::filesystem::path in_use = app.logFolderInUse();
    const dvv::Result<std::filesystem::path> saved = folderOf(applied());
    const bool unavailable = !in_use.empty() && saved && !sameFolder(*saved, in_use);
    unavailable_->SetLabel(unavailable ? "Not available, writing to " + pathText(in_use)
                                       : wxString());
    unavailable_->Show(unavailable);
    Layout();
}

void LogSettingsPage::onTopLevelShown(wxShowEvent& event) {
    event.Skip();
    if (event.IsShown()) {
        (void)TransferDataToWindow();
    }
}

// --- the Files page ------------------------------------------------------------------------------

FilesSettingsPage::FilesSettingsPage(wxWindow* parent, MainFrame& frame, SettingsWindow& owner)
    : wxPanel(parent), frame_(&frame), owner_(&owner) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    addSecondCopyLine(*this, *sizer);

    sizer->Add(new wxStaticText(this, wxID_ANY, "File reads:"),
               wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP));
    for (std::size_t i = 0; i < modes_.size(); ++i) {
        modes_[i] = new wxRadioButton(this, wxID_ANY, fromUtf8(READ_LABELS[i]), wxDefaultPosition,
                                      wxDefaultSize, i == 0 ? wxRB_GROUP : 0);
        modes_[i]->Bind(wxEVT_RADIOBUTTON,
                        [this, i](wxCommandEvent& /*event*/) { setFileReads(READ_MODES[i]); });
        sizer->Add(modes_[i], wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP));
    }
    auto* help = new wxStaticText(this, wxID_ANY,
                                  "Applies to files opened after the change, and to reopens.");
    help->SetFont(help->GetFont().Smaller());
    sizer->Add(help, wxSizerFlags().Border(wxALL));

    auto* file_row = new wxBoxSizer(wxHORIZONTAL);
    file_row->Add(new wxStaticText(this, wxID_ANY, "Settings file:"),
                  wxSizerFlags().CenterVertical().Border(wxRIGHT));
    const std::filesystem::path& config = wxGetApp().configPath();
    config_path_ =
        new wxTextCtrl(this, wxID_ANY, config.empty() ? wxString("none") : pathText(config),
                       wxDefaultPosition, wxSize(FromDIP(360), -1), wxTE_READONLY);
    file_row->Add(config_path_, wxSizerFlags(1).CenterVertical());
    sizer->AddStretchSpacer();
    sizer->Add(file_row, wxSizerFlags().Expand().Border(wxALL));

    SetSizerAndFit(sizer);
#if defined(__WXGTK__)
    if (wxWindow* top = wxGetTopLevelParent(this); top != nullptr) {
        top->Bind(wxEVT_SHOW, &FilesSettingsPage::onTopLevelShown, this);
    }
#endif
    (void)TransferDataToWindow();
}

bool FilesSettingsPage::active() const {
    return frame_ && !frame_->closing();
}

void FilesSettingsPage::setFileReads(dvv::platform::FileReadMode mode) {
    if (!active()) {
        return;
    }
    wanted_ = mode;
    for (std::size_t i = 0; i < modes_.size(); ++i) {
        modes_[i]->SetValue(READ_MODES[i] == mode);
    }
    changed();
}

dvv::platform::FileReadMode FilesSettingsPage::fileReads() const {
    for (std::size_t i = 0; i < modes_.size(); ++i) {
        if (modes_[i]->GetValue()) {
            return READ_MODES[i];
        }
    }
    return dvv::platform::FileReadMode::Auto;
}

wxString FilesSettingsPage::configPathText() const {
    return config_path_->GetValue();
}

bool FilesSettingsPage::TransferDataToWindow() {
    if (active()) {
        wanted_ = wxGetApp().config().io.fileReads;
        for (std::size_t i = 0; i < modes_.size(); ++i) {
            modes_[i]->SetValue(READ_MODES[i] == wanted_);
        }
    }
    return wxPanel::TransferDataToWindow();
}

bool FilesSettingsPage::TransferDataFromWindow() {
    if (active() && !wxPreferencesEditor::ShouldApplyChangesImmediately()) {
        std::vector<ConfigValue> values;
        apply(values);
        owner_->addPending(values);
    }
    return wxPanel::TransferDataFromWindow();
}

void FilesSettingsPage::changed() {
    if (!wxPreferencesEditor::ShouldApplyChangesImmediately()) {
        return;  // Windows: OK applies
    }
    std::vector<ConfigValue> values;
    apply(values);
    if (!values.empty()) {
        (void)wxGetApp().saveSettings(values, /*closing=*/false);
    }
}

void FilesSettingsPage::apply(std::vector<ConfigValue>& values) {
    App& app = wxGetApp();
    if (wanted_ == app.config().io.fileReads) {
        return;
    }
    app.useFileReads(wanted_);  // spec §5.3: every later open, reopens included
    values.push_back({.key = "io.fileReads", .value = std::string(fileReadsName(wanted_))});
}

void FilesSettingsPage::onTopLevelShown(wxShowEvent& event) {
    event.Skip();
    if (event.IsShown()) {
        (void)TransferDataToWindow();
    }
}

// --- the window ----------------------------------------------------------------------------------

// The editor owns the page factories; each records the page window it creates.
class SettingsWindow::LogFactory final : public wxPreferencesPage {
public:
    explicit LogFactory(SettingsWindow& owner) : owner_(&owner) {}
    [[nodiscard]] wxString GetName() const override { return "Log"; }
    [[nodiscard]] wxBitmapBundle GetIcon() const override { return settingsPageIcon("doc.text"); }
    wxWindow* CreateWindow(wxWindow* parent) override {
        auto* page = new LogSettingsPage(parent, *owner_->frame_, *owner_);
        owner_->log_page_ = page;
        return page;
    }

private:
    SettingsWindow* owner_;
};

class SettingsWindow::FilesFactory final : public wxPreferencesPage {
public:
    explicit FilesFactory(SettingsWindow& owner) : owner_(&owner) {}
    [[nodiscard]] wxString GetName() const override { return "Files"; }
    [[nodiscard]] wxBitmapBundle GetIcon() const override { return settingsPageIcon("folder"); }
    wxWindow* CreateWindow(wxWindow* parent) override {
        auto* page = new FilesSettingsPage(parent, *owner_->frame_, *owner_);
        owner_->files_page_ = page;
        return page;
    }

private:
    SettingsWindow* owner_;
};

SettingsWindow::SettingsWindow(MainFrame& frame)
    : frame_(&frame), editor_(std::make_unique<wxPreferencesEditor>(settingsTitle())) {
    editor_->AddPage(new LogFactory(*this));  // the editor owns its pages
    editor_->AddPage(new FilesFactory(*this));
}

SettingsWindow::~SettingsWindow() = default;

void SettingsWindow::show(wxWindow* parent) {
    pending_.clear();
    editor_->Show(parent);  // Windows: returns when the modal dialog has ended
    if (!pending_.empty() && !frame_->closing()) {
        // Spec §5.4: on Windows OK saves every changed value, once.
        (void)wxGetApp().saveSettings(pending_, /*closing=*/false);
    }
    pending_.clear();
}

void SettingsWindow::dismiss() {
    editor_->Dismiss();
}

void SettingsWindow::ensurePages() {
    if (pagesCreated()) {
        return;
    }
    if (!wxPreferencesEditor::ShownModally()) {
        show(frame_);  // the editor creates both pages
    }
    if (pagesCreated()) {
        return;
    }
    // Windows outside its modal dialog: a hidden copy, which no OK ever applies.
    if (!holder_) {
        holder_ = new wxDialog(frame_, wxID_ANY, settingsTitle());
    }
    if (!log_page_) {
        log_page_ = new LogSettingsPage(holder_.get(), *frame_, *this);
    }
    if (!files_page_) {
        files_page_ = new FilesSettingsPage(holder_.get(), *frame_, *this);
    }
}

LogSettingsPage& SettingsWindow::logPage() {
    ensurePages();
    return *log_page_;
}

FilesSettingsPage& SettingsWindow::filesPage() {
    ensurePages();
    return *files_page_;
}

bool SettingsWindow::pagesCreated() const {
    return log_page_ && files_page_;
}

wxWindow* SettingsWindow::window() const {
    wxWindow* page = log_page_ ? static_cast<wxWindow*>(log_page_.get())
                               : static_cast<wxWindow*>(files_page_.get());
    wxWindow* top = page != nullptr ? wxGetTopLevelParent(page) : nullptr;
    return top != holder_.get() ? top : nullptr;
}

bool SettingsWindow::shown() const {
    const wxWindow* top = window();
    return top != nullptr && top->IsShown();
}

void SettingsWindow::pressOk() {
    if (!wxPreferencesEditor::ShownModally()) {
        return;  // macOS and GTK: every change is applied already
    }
    wxWindow* dialog = window();
    if (dialog == nullptr) {
        return;
    }
    // The dialog's own OK handling: Validate(), TransferDataFromWindow(), EndModal(wxID_OK).
    wxCommandEvent ok(wxEVT_BUTTON, wxID_OK);
    ok.SetEventObject(dialog);
    dialog->GetEventHandler()->ProcessEvent(ok);
}

void SettingsWindow::addPending(std::span<const ConfigValue> values) {
    pending_.insert(pending_.end(), values.begin(), values.end());
}

}  // namespace rawreel
