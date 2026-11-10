#pragma once

// rawreel-gui's wx application (spec §10.2; design M1b §5.6, M3 v2 §5.5 and §7): the log sinks,
// the config and the settings claim (Log pane and Settings design §5.3, §6.3, §7.1), appearance,
// pools, GPU registry, the Session and its bridge, the main frame and the exit code.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "core/config.h"
#include "core/config_writer.h"
#include "core/gui_arguments.h"
#include "core/log_file_switch.h"
#include "core/log_level.h"
#include "core/log_store.h"
#include "core/session.h"
#include "core/settings_claim.h"
#include "core/startup.h"
#include "dvv/base/executor.h"
#include "dvv/base/log.h"
#include "dvv/gpu/device_id.h"
#include "dvv/gpu/gpu_registry.h"
#include "dvv/registry/component_registry.h"
#include "render/display_state.h"

namespace rawreel {

class SessionBridge;
class ShowHistory;

class App final : public wxApp {
public:
    // main() passes the parsed arguments and the stderr sink it added to the Logger before
    // wxEntry(); the App sets that sink's level to the active level (log level design §2.1).
    static void setArguments(GuiArguments arguments, std::shared_ptr<dvv::StderrSink> stderr_sink);
    [[nodiscard]] static const GuiArguments& arguments() noexcept;
    // The exit code the App decided (0 after a passed selftest, 1, 4, 6, 77); nullopt otherwise.
    // main() reads it after wxEntry(), which returns -1 when OnInit() fails.
    [[nodiscard]] static std::optional<int> exitCode() noexcept;
    // Records the exit code (the first one wins) and logs `message`.
    static void record(int code, const std::string& message);

    // Records the exit code (the first one wins), reports `message` (stderr through the log; a
    // modal wxMessageBox as well when not --selftest and code != 0) and closes the top window. The
    // close is queued (CallAfter), so a caller inside a window's or timer's handler returns first.
    void finish(int code, const std::string& message);

    // Design M3 v2 §5.5 startup, binding order. Does not call wxApp::OnInit(), which would parse
    // the command line again.
    bool OnInit() override;
    // A --selftest run first checks that its config file holds the gui.showLogPane value the close
    // saved (a mismatch: exit 6). Then shuts a remaining Session down, checks the validation
    // messages of a --selftest run, and releases the registry and the pools; last, flushes the
    // Logger, closes the log file and removes a selftest's own config folder.
    int OnExit() override;
#if defined(__WXOSX__)
    // Files opened from Finder or the Dock (design C5), also the ones of the launch, which wxOSX
    // passes after OnInit(): the route of a drop on the window (MainFrame::openFiles), so the
    // first file replaces the stream and the others are logged as ignored. Nothing without a main
    // frame (a failed start) or once it is closing.
    void MacOpenFiles(const wxArrayString& names) override;
#endif

    // Called by MainFrame::onClose: detaches the bridge, then syncWait(session->shutdown()) and
    // releases the Session. Nothing it waits for needs the GUI thread. A second call does nothing.
    void shutdownSession();

    // Null before OnInit() created it and after shutdownSession().
    [[nodiscard]] Session* session() const noexcept { return session_.get(); }
    [[nodiscard]] const std::shared_ptr<dvv::gpu::GpuRegistry>& registry() const noexcept {
        return registry_;
    }
    [[nodiscard]] const std::shared_ptr<DisplayState>& display() const noexcept { return display_; }
    // The Log pane's records (spec §3.1): a Logger sink from the start of OnInit() on, never
    // removed; null before.
    [[nodiscard]] const std::shared_ptr<LogStore>& logStore() const noexcept { return log_store_; }
    // The log file sink in the Logger (spec §5.3); it holds no file before OnInit() opened one,
    // when log.file.enabled is false or no folder could be used, and after OnExit().
    [[nodiscard]] LogFileSwitch& logFileSwitch() noexcept { return *log_file_; }
    // Whether the system log sink is on (level Warn) or off (level Off).
    [[nodiscard]] bool systemLogEnabled() const noexcept { return system_log_enabled_; }
    // Sets the system log sink's level to Warn (on) or Off; it never adds or removes a sink. The
    // value in effect (config().log.system.enabled) follows.
    void useSystemLog(bool on);
    // The default log folder (spec §5.3): the user data folder; in a selftest with its own config
    // folder (§7.1) that folder's logs/, so a selftest never writes the user's log file.
    [[nodiscard]] dvv::Result<std::filesystem::path> defaultLogFolder() const;
    // `folder` is the default log folder (std::filesystem::equivalent, or the same path).
    [[nodiscard]] bool isDefaultLogFolder(const std::filesystem::path& folder) const;
    // Spec §5.3 steps 1 and 2, outside every lock: prepares `folder` (only its last component is
    // created; the default folder with its parents) and opens a log file there (appending to the
    // newest one only when ownsSettings()). Nothing changes in the Logger.
    [[nodiscard]] dvv::Result<std::shared_ptr<dvv::SplitFileSink>> prepareLogFile(
        const std::filesystem::path& folder);
    // Spec §5.3 steps 3 and 4: the old file (if any) gets one Info line, "log file moves to
    // '<the new file>'", then the switch takes `sink`, whose folder is `folder`
    // (logFolderInUse()); the old sink is dropped outside the switch's lock. The new file then
    // gets "log file: '<path>'" and the level marker (addendum §3.1, §5.2), appended or not.
    void useLogFile(std::shared_ptr<dvv::SplitFileSink> sink, std::filesystem::path folder);
    // Log file off (spec §5.3): the switch drops its file; logFolderInUse() becomes empty.
    void stopLogFile();
    // The values in effect of log.file.enabled and log.file.folder (empty: the default folder),
    // after the Settings window applied them; the log file itself is changed by the calls above.
    void setLogFileConfig(bool enabled, std::filesystem::path folder);
    // The value in effect of log.level (savedLevel()) after the Settings window applied it; it is
    // marked as saved only in the settings owner, which saves it. The active level is set apart.
    void setLevelConfig(dvv::LogLevel level);
    // File reads (spec §5.3): the Session's mode for every later open and config().io.fileReads.
    void useFileReads(dvv::platform::FileReadMode mode);
    // The absolute config file of this run (--config, the selftest's own, or the default); empty
    // when no default could be found.
    [[nodiscard]] const std::filesystem::path& configPath() const noexcept { return config_path_; }
    // The values in effect for this run.
    [[nodiscard]] const Config& config() const noexcept { return config_; }
    // This process holds the settings claim of configPath() (spec §6.3): it alone may save.
    [[nodiscard]] bool ownsSettings() const noexcept { return settings_claim_ != nullptr; }
    // The only way rawreel-gui writes its config (spec §5.4, §6.3): false, and nothing written,
    // unless ownsSettings(); otherwise saveConfigValues(configPath(), values), true when it saved.
    // A failure to save is reported here, so a caller may ignore the result: one warning,
    // "config '<path>' not saved: <reason>"; the run's first failure also shows an error
    // message (showModal(wxMessageDialog&)) unless `closing` (no dialog while the main window
    // closes).
    bool saveSettings(std::span<const ConfigValue> values, bool closing);
    // The gui.showLogPane value the main window's close saved (spec §2); nullopt when nothing was
    // saved. A selftest's OnExit checks it against the file.
    [[nodiscard]] std::optional<bool> savedLogPane() const noexcept { return saved_log_pane_; }
    void setSavedLogPane(bool shown) noexcept { saved_log_pane_ = shown; }
    // A --selftest run: its own config folder, <temp>/rawreel-selftest-config-<pid>-<16 hex>/
    // (spec §7.1; with --config its rawreel.json starts as a copy of that file); empty otherwise
    // and after OnExit().
    [[nodiscard]] const std::filesystem::path& selftestConfigFolder() const noexcept {
        return selftest_config_dir_;
    }
    // The folder of the open log file; empty: no log file.
    [[nodiscard]] std::filesystem::path logFolderInUse() const { return log_folder_; }
    // The log file in use (the switch's currentFile()); empty: no log file.
    [[nodiscard]] std::filesystem::path currentLogFile() const {
        return log_file_ ? log_file_->currentFile() : std::filesystem::path();
    }
    // The active log level (log level design §2.1): the level of the log file switch, the store
    // and the stderr sink; the Logger itself stays at Trace. log.level at start.
    [[nodiscard]] dvv::LogLevel activeLevel() const noexcept {
        return active_level_->load(std::memory_order_relaxed);
    }
    // The saved log.level (Info when the key is missing).
    [[nodiscard]] dvv::LogLevel savedLevel() const noexcept { return config_.log.level; }
    // Where the active level came from: the menu (a run-only level), Settings (also at start with
    // a saved log.level), a second copy's Settings, or the default. The marker after each log
    // file open names it (addendum §3.1).
    [[nodiscard]] LevelSource activeLevelSource() const noexcept { return level_source_; }
    // Sets the three sinks' levels to `level`, then writes one change marker (Logger::mark,
    // levelChangeMarker(old, level, source)); no marker for the level already active, but
    // `source` becomes activeLevelSource() either way (a Settings change ends a run-only level).
    // GUI thread. Nothing is saved.
    void setActiveLevel(dvv::LogLevel level, LevelSource source);
    // MainFrame::onLogChanged, so never logs (spec §3.3): when the log file started a new file
    // since the last check, checks the log files' total size (addendum §5.3).
    void checkLogSplits();
    // --selftest only: the Session's sink, recording every shown FrameSet; null otherwise.
    [[nodiscard]] const std::shared_ptr<ShowHistory>& showHistory() const noexcept {
        return show_history_;
    }
    // The developer delay of every produced frame (--debug-seek-delay, set to its value); created
    // for that option and for --selftest; null otherwise.
    [[nodiscard]] ProduceDelay* produceDelay() const noexcept { return produce_delay_.get(); }
    // What SetAppearance() returned (nullopt before OnInit()); Failure and CannotChange are not
    // errors (design §7).
    [[nodiscard]] std::optional<AppearanceResult> appearanceResult() const noexcept {
        return appearance_result_;
    }
    // The objects given to the Session.
    [[nodiscard]] const std::shared_ptr<dvv::ComponentRegistry>& components() const noexcept {
        return startup_.components;
    }
    // The FFmpeg status, capabilities and component registry of OnInit step 5 (design M4 v2 §4.6).
    [[nodiscard]] const Startup& startup() const noexcept { return startup_; }
    [[nodiscard]] const std::shared_ptr<dvv::IExecutor>& cpu() const noexcept { return cpu_; }
    [[nodiscard]] const std::shared_ptr<dvv::IExecutor>& io() const noexcept { return io_; }

private:
    // OnInit() steps 2 to 4 of spec §5.3 "At start" and §7.1: the selftest config, the config,
    // the settings claim (before the log file, whose append_newest is ownsSettings()), the
    // source flag and the active level (addendum §2.1, §4), the system log, the log file and the
    // start marker (addendum §3.1). False (after finish()) when a selftest's own config folder
    // cannot be made.
    [[nodiscard]] bool startLogAndConfig();
    // A --selftest run: <temp>/rawreel-selftest-config-<pid>-<16 hex>/ with its logs/ and a
    // rawreel.json naming them, which starts as a copy of `seed` (--config) when that is not
    // empty; the file, or an error.
    [[nodiscard]] dvv::Result<std::filesystem::path> makeSelftestConfig(
        const std::filesystem::path& seed);
    void setAppearance();
    // The first log file (spec §5.3 "At start" step 4): the saved folder, else the default one;
    // the switch first gets the lines the store already holds at or above the active level
    // (addendum §2.3), then the file's "log file: '<path>'" line follows.
    void startLogFile();
    // openLogFile(folder, creation, ownsSettings()) with the header of each new file (addendum
    // §5.2): logFileHeader(active level, the previous file's name).
    [[nodiscard]] dvv::Result<std::shared_ptr<dvv::SplitFileSink>> openLogFileIn(
        const std::filesystem::path& folder, FolderCreation creation);
    // The levels of the log file switch, the store and the stderr sink, and activeLevel().
    void applyLevel(dvv::LogLevel level);
    // Addendum §5.3: the first time in a run the log files in use pass LOG_SIZE_WARNING_BYTES,
    // posts one warning (CallAfter, so a caller on the refresh path never logs). Never logs itself.
    void checkLogSize();
    // The end of OnExit() and of a failed OnInit(): flushes the Logger, closes the log file and
    // releases the settings claim, then removes the selftest's config folder (a failure is one
    // stderr line, the exit code stays).
    void closeLogAndConfig();
    // OnExit step 1 of a selftest: the config file holds `saved`, the value the close saved; a
    // mismatch records exit 6.
    void checkSavedLogPane(bool saved);
    [[nodiscard]] bool startPools();
    // The registry and the device of --gpu; false (after finish()) when there is none to use.
    [[nodiscard]] bool startGpu();

    // Declared in the reverse order of their release (the members' destruction order also covers
    // a failed OnInit(), after which wx does not call OnExit()).
    std::filesystem::path selftest_config_dir_;  // empty unless makeSelftestConfig() made it
    // The sinks of spec §5.3, also held by the Logger (never removed).
    std::shared_ptr<LogStore> log_store_;
    std::shared_ptr<LogFileSwitch> log_file_;
    std::shared_ptr<dvv::SystemLogSink> system_sink_;
    bool system_log_enabled_ = false;
    Config config_;
    std::filesystem::path config_path_;
    std::filesystem::path log_folder_;
    // activeLevel(); shared with the log file header callback, which reads it under the file
    // sink's mutex on whatever thread splits the file.
    std::shared_ptr<std::atomic<dvv::LogLevel>> active_level_ =
        std::make_shared<std::atomic<dvv::LogLevel>>(dvv::LogLevel::Info);
    LevelSource level_source_ = LevelSource::Default;  // activeLevelSource()
    std::uint64_t checked_splits_ = 0;  // the switch's splits() at the last size check
    bool size_warned_ = false;          // the 1 GiB warning was posted in this run
    std::unique_ptr<SettingsClaim> settings_claim_;
    bool save_failed_ = false;            // a saveSettings() failed in this run
    std::optional<bool> saved_log_pane_;  // set by the close's save
    std::optional<AppearanceResult> appearance_result_;
    std::shared_ptr<dvv::IExecutor> cpu_;
    std::shared_ptr<dvv::IExecutor> io_;
    std::shared_ptr<dvv::gpu::GpuRegistry> registry_;
    std::optional<dvv::gpu::DeviceId> device_;
    Startup startup_;  // the FFmpeg status and the component registry
    std::shared_ptr<DisplayState> display_;
    std::shared_ptr<ShowHistory> show_history_;
    std::shared_ptr<ProduceDelay> produce_delay_;
    std::shared_ptr<SessionBridge> bridge_;
    std::unique_ptr<Session> session_;
};

}  // namespace rawreel

wxDECLARE_APP(rawreel::App);
