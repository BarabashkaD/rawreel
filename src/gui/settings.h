#pragma once

// The Settings window (Log pane and Settings design §5): a wxPreferencesEditor with the Log and the
// Files page, which follows each OS's convention (§5.1). macOS and GTK apply every change at once
// and save it; Windows shows a modal dialog whose OK runs Validate() (a changed log folder is
// opened) and then TransferDataFromWindow() (every page applies), and saves once. The pages save
// only through App::saveSettings, and do nothing once the main window is closing.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <wx/choice.h>
#include <wx/preferences.h>
#include <wx/weakref.h>

#include "core/config_writer.h"
#include "core/recent.h"
#include "dvv/base/log.h"
#include "dvv/platform/file_reader.h"
#include "gui/main_frame.h"  // wxWeakRef<MainFrame> needs the class

namespace rawreel {

class SettingsWindow;

// "Settings" (macOS and Windows) or "Preferences" (GTK): the Windows and GTK title; macOS titles
// the window with the page shown.
[[nodiscard]] wxString settingsTitle();

// The Log page (§5.2): Level (log level design §2.2), Write a log file, the folder (Choose..., Use
// Default, Show in Finder / Explorer, Open Folder) and the system log.
class LogSettingsPage final : public wxPanel {
public:
    LogSettingsPage(wxWindow* parent, MainFrame& frame, SettingsWindow& owner);

    // Choose...: the native folder dialog (showModal(wxDirDialog&)); only its result is used. The
    // default folder's own path is stored as "". macOS and GTK: applied at once; when the folder
    // cannot be used an error message names it and the field keeps the old value.
    void chooseFolder();
    void useDefault();             // Use Default: the value "" (the default folder)
    void reveal();                 // Show in Finder / Explorer, Open Folder (revealFile)
    void setFileEnabled(bool on);  // Write a log file
    void setSystemLog(bool on);    // Send warnings and errors to the system log
    // Level (log level design §2.2): shows the saved log.level. A change saves log.level, sets
    // the active level and ends any run-only level (macOS, GTK: at once; Windows: on OK, only when
    // the control changed). A second copy applies it without saving ("this window only").
    void setLevel(dvv::LogLevel level);
    [[nodiscard]] dvv::LogLevel level() const;  // the selected level
    // Use Default is enabled: the value is a folder that is not the default one.
    [[nodiscard]] bool useDefaultEnabled() const;
    // Show in ... is enabled: the folder in use (or else the chosen one) exists.
    [[nodiscard]] bool revealEnabled() const;
    [[nodiscard]] wxString folderText() const;  // the field: the value, "" for the default
    [[nodiscard]] wxString folderHint() const;  // its placeholder: the default folder
    // The values in effect (on creation, at every show, and on GTK on wxEVT_SHOW).
    bool TransferDataToWindow() override;
    // Windows OK: opens a changed log folder without using it yet; false (after an error message)
    // when it cannot be used, which keeps the dialog open and applies nothing.
    bool Validate() override;
    // Windows OK: applies the values; they are saved once when the dialog has ended.
    bool TransferDataFromWindow() override;

private:
    struct Choice {
        dvv::LogLevel level = dvv::LogLevel::Info;  // the saved log.level
        bool fileEnabled = true;
        std::filesystem::path folder;  // empty: the default folder
        bool systemLog = false;
    };
    struct Prepared {
        std::shared_ptr<dvv::SplitFileSink> sink;
        std::filesystem::path folder;
    };
    [[nodiscard]] bool active() const;  // the main window exists and is not closing
    [[nodiscard]] Choice applied() const;
    // The folder the log file of `choice` goes to: its folder, or the default one.
    [[nodiscard]] dvv::Result<std::filesystem::path> folderOf(const Choice& choice) const;
    // A control changed wanted_: on macOS and GTK it is applied and saved; on Windows only shown.
    void changed();
    // Spec §5.3 steps 1 and 2 for wanted_; false after the error message.
    [[nodiscard]] bool prepare();
    // Spec §5.3 steps 3 and 4 and the values to save.
    void apply(std::vector<ConfigValue>& values);
    void refresh();  // the controls from wanted_
    void onTopLevelShown(wxShowEvent& event);

    wxWeakRef<MainFrame> frame_;
    SettingsWindow* owner_;
    Choice wanted_;  // what the controls show
    std::optional<Prepared> prepared_;
    // Choose... or Use Default ran since the last prepare(): the folder is opened even when the
    // value did not change (the saved folder chosen again after a fallback at start).
    bool folder_chosen_ = false;
    // setLevel() ran since the last apply: on macOS and GTK the chosen level is applied even when
    // it is the saved one (it ends a run-only level).
    bool level_chosen_ = false;
    wxChoice* level_ = nullptr;
    wxCheckBox* file_box_ = nullptr;
    wxTextCtrl* folder_ = nullptr;
    wxButton* use_default_ = nullptr;
    wxButton* reveal_ = nullptr;
    wxStaticText* unavailable_ = nullptr;  // "Not available, writing to <folder in use>"
    wxCheckBox* system_box_ = nullptr;
};

// The Files page (§5.2): File reads, Items in Open Recent (Open Recent spec §9) and the Settings
// file.
class FilesSettingsPage final : public wxPanel {
public:
    FilesSettingsPage(wxWindow* parent, MainFrame& frame, SettingsWindow& owner);

    // Selects the mode's button; macOS and GTK: applied to the Session and saved at once.
    void setFileReads(dvv::platform::FileReadMode mode);
    [[nodiscard]] dvv::platform::FileReadMode fileReads() const;  // the selected button
    // Items in Open Recent (1 to 10): selects the size; macOS and GTK: applied to the list (a
    // smaller size cuts it for good) and saved at once, Windows on OK.
    void setRecentSize(std::size_t size);
    [[nodiscard]] std::size_t recentSize() const;   // the selection
    [[nodiscard]] wxString configPathText() const;  // the Settings file line
    bool TransferDataToWindow() override;
    bool TransferDataFromWindow() override;  // Windows OK

private:
    [[nodiscard]] bool active() const;
    void changed();
    void apply(std::vector<ConfigValue>& values);
    void onTopLevelShown(wxShowEvent& event);

    wxWeakRef<MainFrame> frame_;
    SettingsWindow* owner_;
    dvv::platform::FileReadMode wanted_ = dvv::platform::FileReadMode::Auto;
    std::size_t wanted_size_ = RECENT_DEFAULT_SIZE;  // Items in Open Recent
    std::array<wxRadioButton*, 3> modes_{};          // Automatic, Unbuffered, Buffered
    wxChoice* recent_size_ = nullptr;
    wxTextCtrl* config_path_ = nullptr;
};

// The editor and its two pages, owned by MainFrame. One window at a time: show() raises it.
class SettingsWindow {
public:
    explicit SettingsWindow(MainFrame& frame);
    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;
    SettingsWindow(SettingsWindow&&) = delete;
    SettingsWindow& operator=(SettingsWindow&&) = delete;
    ~SettingsWindow();

    // macOS and GTK: shows or raises the window and returns. Windows: the modal dialog; after an
    // OK, one App::saveSettings with every value the pages applied.
    void show(wxWindow* parent);
    // Hides (macOS), closes (GTK) or cancels (Windows) the window; MainFrame::onClose calls it
    // before the Session shuts down.
    void dismiss();
    // The pages, created on demand: on macOS and GTK by showing the window; on Windows outside
    // its modal dialog as a hidden copy that never applies anything.
    [[nodiscard]] LogSettingsPage& logPage();
    [[nodiscard]] FilesSettingsPage& filesPage();
    // Both pages exist (the editor created them).
    [[nodiscard]] bool pagesCreated() const;
    // The window is on screen.
    [[nodiscard]] bool shown() const;
    // The top-level window of the pages (the macOS window, the GTK or Windows dialog); null before
    // the pages exist and for the hidden Windows copy.
    [[nodiscard]] wxWindow* window() const;
    // Windows: OK on the dialog (wxEVT_BUTTON wxID_OK, which runs Validate() and
    // TransferDataFromWindow()); elsewhere nothing, changes apply at once.
    void pressOk();
    // Windows OK: values a page applied, saved when the dialog has ended.
    void addPending(std::span<const ConfigValue> values);

private:
    class LogFactory;
    class FilesFactory;
    void ensurePages();

    MainFrame* frame_;
    std::unique_ptr<wxPreferencesEditor> editor_;
    wxWeakRef<LogSettingsPage> log_page_;
    wxWeakRef<FilesSettingsPage> files_page_;
    wxWeakRef<wxDialog> holder_;  // Windows: the hidden parent of pages made outside the dialog
    std::vector<ConfigValue> pending_;
};

}  // namespace rawreel
