#pragma once

// rawreel-gui's wx application (spec §10.2; design M1b §5.6, M3 v2 §5.5 and §7): the log sinks,
// the config and the settings claim (Log pane and Settings design §5.3, §6.3, §7.1), appearance,
// pools, GPU registry, the Session and its bridge, the main frame and the exit code.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "core/config.h"
#include "core/config_writer.h"
#include "core/gui_arguments.h"
#include "core/log_file_switch.h"
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
    // main() passes the parsed arguments before wxEntry().
    static void setArguments(GuiArguments arguments);
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
    // created; the default folder with its parents) and opens rawreel.log there. Nothing changes
    // in the Logger.
    [[nodiscard]] dvv::Result<std::shared_ptr<dvv::RotatingFileSink>> prepareLogFile(
        const std::filesystem::path& folder);
    // Spec §5.3 steps 3 and 4: the old file (if any) gets one Info line, "log file moves to
    // '<folder>/rawreel.log'", then the switch takes `sink`, whose folder is `folder`
    // (logFolderInUse()); the old sink is dropped outside the switch's lock.
    void useLogFile(std::shared_ptr<dvv::RotatingFileSink> sink, std::filesystem::path folder);
    // Log file off (spec §5.3): the switch drops its file; logFolderInUse() becomes empty.
    void stopLogFile();
    // The values in effect of log.file.enabled and log.file.folder (empty: the default folder),
    // after the Settings window applied them; the log file itself is changed by the calls above.
    void setLogFileConfig(bool enabled, std::filesystem::path folder);
    // File reads (spec §5.3): the Session's mode for every later open and config().io.fileReads.
    void useFileReads(dvv::platform::FileReadMode mode);
    // The absolute config file of this run (--config, the selftest's own, or the default); empty
    // when no default could be found.
    [[nodiscard]] const std::filesystem::path& configPath() const noexcept { return config_path_; }
    // The values in effect for this run.
    [[nodiscard]] const Config& config() const noexcept { return config_; }
    // This process holds the settings claim of configPath() (spec §6.3): it alone may save.
    [[nodiscard]] bool ownsSettings() const noexcept { return settings_claim_ != nullptr; }
    // The only way rawreel-gui writes its config (spec §5.4, §6.3): fails with "not the settings
    // owner" unless ownsSettings(); otherwise saveConfigValues(configPath(), values). A failure is
    // logged as one warning, "config '<path>' not saved: <reason>"; the run's first failure also
    // shows an error message (showModal(wxMessageDialog&)) unless `closing` (no dialog while the
    // main window closes).
    dvv::Result<void> saveSettings(std::span<const ConfigValue> values, bool closing);
    // The gui.showLogPane value the main window's close saved (spec §2); nullopt when nothing was
    // saved. A selftest's OnExit checks it against the file.
    [[nodiscard]] std::optional<bool> savedLogPane() const noexcept { return saved_log_pane_; }
    void setSavedLogPane(bool shown) noexcept { saved_log_pane_ = shown; }
    // A --selftest run without --config: its own config folder,
    // <temp>/rawreel-selftest-config-<pid>-<16 hex>/ (spec §7.1); empty otherwise and after
    // OnExit().
    [[nodiscard]] const std::filesystem::path& selftestConfigFolder() const noexcept {
        return selftest_config_dir_;
    }
    // The folder of the open rawreel.log; empty: no log file.
    [[nodiscard]] std::filesystem::path logFolderInUse() const { return log_folder_; }
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
    // OnInit() steps 1 to 6 of spec §5.3 "At start" and §7.1: the sinks, the selftest config, the
    // config, the system log, the log file and the settings claim. False (after finish()) when a
    // selftest's own config folder cannot be made.
    [[nodiscard]] bool startLogAndConfig();
    // A --selftest run without --config: <temp>/rawreel-selftest-config-<pid>/ with its logs/
    // and a rawreel.json naming them; the file, or an error.
    [[nodiscard]] dvv::Result<std::filesystem::path> makeSelftestConfig();
    void setAppearance();
    // The first log file (spec §5.3 "At start" step 4): the saved folder, else the default one;
    // the switch first gets the lines the store already holds.
    void startLogFile();
    // The end of OnExit() and of a failed OnInit(): flushes the Logger, closes the log file and
    // releases the settings claim, then removes the selftest's config folder (a failure is one
    // stderr line, the exit code stays).
    void closeLogAndConfig();
    // OnExit step 1 of a selftest: the config file holds saved_log_pane_; a mismatch records
    // exit 6.
    void checkSavedLogPane();
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
