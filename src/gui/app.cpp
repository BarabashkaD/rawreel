// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/app.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <wx/clipbrd.h>  // OnExit's Flush()

#include "core/config.h"
#include "core/config_writer.h"
#include "core/exit_code.h"
#include "core/log_file_switch.h"
#include "core/log_level.h"
#include "core/log_store.h"
#include "core/native_os.h"
#include "core/recent.h"
#include "core/settings_claim.h"
#include "core/timer.h"
#include "dvv/base/environment.h"
#include "dvv/base/executors.h"
#include "dvv/base/log.h"
#include "dvv/base/paths.h"
#include "dvv/base/task.h"
#include "dvv/base/utf8.h"
#include "dvv/components/ffmpeg.h"
#include "dvv/gpu/gpu_info.h"
#include "dvv/platform/crash_handler.h"
#include "dvv/platform/file_writer.h"
#include "dvv/platform/window_handle.h"
#include "gui/main_frame.h"
#include "gui/native_policy.h"
#include "gui/selftest.h"
#include "gui/session_bridge.h"
#include "gui/transport_bar.h"
#include "gui/viewport.h"

namespace rawreel {

namespace {

// Written by main() before wxEntry() and by the GUI thread afterwards; read by main() after
// wxEntry() returns. No other thread touches them.
GuiArguments the_arguments;
// main()'s stderr sink (in the Logger since before wxEntry()); its level follows the active level.
std::shared_ptr<dvv::StderrSink> the_stderr_sink;
// What applyBundledDriver() returned, logged by OnInit once the sinks exist.
dvv::Result<dvv::gpu::DriverChoice> the_driver;
std::optional<int> decided_exit_code;

constexpr std::size_t IO_THREADS = 4;

bool selftest() noexcept {
    return the_arguments.selftest != Scenario::None;
}

// Test only: RAWREEL_GUI_FAULT, honoured only with --selftest. "stack": the Session's first stack
// fails (Session::create fails at startup); "presenter": every presenter fails (D25 path).
std::string injectedFault() {
    return selftest() ? dvv::getEnvironment("RAWREEL_GUI_FAULT").value_or("") : std::string();
}

std::string_view appearanceName(Appearance appearance) noexcept {
    switch (appearance) {
        case Appearance::System: return "system";
        case Appearance::Light: return "light";
        case Appearance::Dark: return "dark";
    }
    return "system";
}

std::string_view appearanceResultName(wxApp::AppearanceResult result) noexcept {
    switch (result) {
        case wxApp::AppearanceResult::Ok: return "Ok";
        case wxApp::AppearanceResult::Failure: return "Failure";
        case wxApp::AppearanceResult::CannotChange: return "CannotChange";
    }
    return "Failure";
}

wxApp::Appearance wxAppearance(Appearance appearance) noexcept {
    switch (appearance) {
        case Appearance::System: return wxApp::Appearance::System;
        case Appearance::Light: return wxApp::Appearance::Light;
        case Appearance::Dark: return wxApp::Appearance::Dark;
    }
    return wxApp::Appearance::System;
}

// Spec §7.1: true when this call created `folder`; false when the path already exists (a stale
// or planted folder, or a symbolic link), which the caller never adopts.
dvv::Result<bool> createNewFolder(const std::filesystem::path& folder) {
    std::error_code error;
    const bool created = std::filesystem::create_directory(folder, error);
    if (error) {
        return dvv::makeError(
            dvv::Errc::Io,
            std::format("cannot create {}: {}", dvv::pathDisplayText(folder), error.message()));
    }
    return created;
}

// A folder createNewFolder() made: mode 0700 on POSIX, and still a real folder, not a link.
dvv::Result<void> secureNewFolder(const std::filesystem::path& folder) {
    std::error_code error;
    if constexpr (NATIVE_OS != Os::Windows) {
        std::filesystem::permissions(folder, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace, error);
        if (error) {
            return dvv::makeError(
                dvv::Errc::Io, std::format("cannot restrict {}: {}", dvv::pathDisplayText(folder),
                                           error.message()));
        }
    }
    const std::filesystem::file_status status = std::filesystem::symlink_status(folder, error);
    if (error || status.type() != std::filesystem::file_type::directory) {
        return dvv::makeError(dvv::Errc::Io, std::format("{} is not a folder of its own",
                                                         dvv::pathDisplayText(folder)));
    }
    return {};
}

// 16 hex digits from the OS's random source.
std::string randomSuffix() {
    std::random_device device;
    const std::uint64_t value =
        (static_cast<std::uint64_t>(device()) << 32U) | static_cast<std::uint64_t>(device());
    return std::format("{:016x}", value);
}

}  // namespace

void App::setArguments(GuiArguments arguments, std::shared_ptr<dvv::StderrSink> stderr_sink,
                       dvv::Result<dvv::gpu::DriverChoice> driver) {
    the_arguments = std::move(arguments);
    the_stderr_sink = std::move(stderr_sink);
    the_driver = std::move(driver);
}

const GuiArguments& App::arguments() noexcept {
    return the_arguments;
}

std::optional<int> App::exitCode() noexcept {
    return decided_exit_code;
}

void App::record(int code, const std::string& message) {
    if (!decided_exit_code) {
        decided_exit_code = code;
    }
    if (code == exit_code::SUCCESS) {
        DVV_LOG_TEXT(dvv::LogLevel::Info, message);
    } else {
        DVV_LOG_TEXT(dvv::LogLevel::Error, message);
    }
}

void App::finish(int code, const std::string& message) {
    record(code, message);
    if (!selftest() && code != exit_code::SUCCESS) {
        wxMessageBox(wxString::FromUTF8(message), "rawreel", wxOK | wxICON_ERROR);
    }
    CallAfter([] {
        if (wxWindow* top = wxTheApp->GetTopWindow(); top != nullptr) {
            top->Close();
        }
    });
}

bool App::OnInit() {
    // Spec §5.3 "At start" step 1: the sinks, added once for the whole run and never removed (the
    // stderr sink is main()'s). The log file switch holds no file yet; the system log is off until
    // the config says otherwise. The Logger itself is at Trace for the whole run, so only the
    // sinks' levels filter (addendum §2.1). Until the config gives the active level, the store
    // takes every level (the first file gets only those at or above it, §2.3); the switch and
    // main()'s stderr sink are at Info, the default level.
    dvv::Logger& logger = dvv::Logger::instance();
    logger.setLevel(dvv::LogLevel::Trace);
    log_store_ = std::make_shared<LogStore>();
    logger.addSink(log_store_, dvv::LogLevel::Trace);
    log_file_ = std::make_shared<LogFileSwitch>();
    logger.addSink(log_file_, dvv::LogLevel::Info);
    system_sink_ = std::make_shared<dvv::SystemLogSink>("rawreel");
    logger.addSink(system_sink_, dvv::LogLevel::Off);
    if (!the_driver) {
        DVV_LOG_WARN("the bundled Vulkan driver is not used: {}", the_driver.error().message);
    }
    dvv::platform::installCrashHandler({.app = "rawreel", .process = "rawreel-gui"});
    // Every sink is in the Logger now: from here on the log thread delivers the records.
    dvv::Logger::instance().start();
    // A failed start after the selftest's config folder exists removes it here: wx calls no
    // OnExit() after a failed OnInit().
    const auto failed = [this] {
        closeLogAndConfig();
        return false;
    };
    setApplicationMenuName();
    if (selftest()) {
        // wx's default GUI log target shows modal dialogs; the selftest runs unattended.
        delete wxLog::SetActiveTarget(new wxLogStderr());
    }
    // 1. The config, the system log, the log file and the settings claim, before the Session
    // exists (spec §5.3 steps 2 to 4, §6.3, §7.1).
    if (!startLogAndConfig()) {
        return failed();
    }
    setAppearance();                     // 2. before any window
    if (!startPools() || !startGpu()) {  // 3., 4.
        return failed();
    }
    // 5. The component registry: every module (FFmpeg among them) and the source providers (design
    // M4 v2 §4.6).
    startup_ = startComponents();
    if (!startup_.components) {
        finish(exit_code::BAD_ARGUMENTS, "rawreel-gui: the component registry could not start");
        return failed();
    }
    DVV_LOG_INFO("ffmpeg: {}",
                 ffmpegStatusLine(startup_.components->module(dvv::components::FFMPEG_MODULE)));
    // The media clock's timer thread (not a GPU failure: exit 1 like the pools).
    auto timer = makeThreadTimer();
    if (!timer) {
        finish(exit_code::BAD_ARGUMENTS,
               "rawreel-gui: the media clock timer could not start: " + timer.error().describe());
        return failed();
    }
    // 6. wx owns top-level windows; the frame is shown once the Session exists.
    display_ = std::make_shared<DisplayState>();
    auto* frame = new MainFrame(display_);
    bridge_ = std::make_shared<SessionBridge>(*frame);
    const std::string fault = injectedFault();
    if (fault == "presenter") {
        frame->viewport().failPresentersForTest();
    }

    // 7. The Session; its empty makeStack selects gpuStackFactory(gpus, cpu, io, presentation).
    // A selftest records every shown FrameSet (ShowHistory) on its way to the display.
    std::shared_ptr<IDisplaySink> sink = display_;
    if (selftest()) {
        show_history_ = std::make_shared<ShowHistory>(display_);
        sink = show_history_;
    }
    if (the_arguments.debugSeekDelay > 0 || selftest()) {
        produce_delay_ = std::make_shared<ProduceDelay>();
        produce_delay_->set(the_arguments.debugSeekDelay);
    }
    StackFactory make_stack{};
    if (fault == "stack") {
        make_stack = [](std::optional<dvv::gpu::DeviceId> /*device*/) -> dvv::Result<MediaStack> {
            return dvv::makeError(dvv::Errc::Internal, "injected stack failure");
        };
    }
    dvv::Result<std::unique_ptr<Session>> session =
        Session::create({.components = startup_.components,
                         .cpu = cpu_,
                         .io = io_,
                         .gpus = gpu_instance_,
                         .device = device_,
                         .presentation = true,
                         .inflight = the_arguments.inflight,
                         .fileReads = config_.io.fileReads,
                         .clock = steadyClock(),
                         .timer = std::move(*timer),
                         .sink = std::move(sink),
                         .events = bridge_,
                         .makeStack = std::move(make_stack),
                         .produceDelay = produce_delay_});
    if (!session) {
        finish(exit_code::GPU_ERROR, "Vulkan could not start: " + session.error().describe());
        bridge_->detach();
        frame->Destroy();  // never shown; wx deletes it with the remaining windows
        return failed();
    }
    session_ = std::move(*session);

    // 8. A lost device goes to the Session; any other presenter failure to the frame (D25).
    frame->viewport().setDeviceLostHandler([](std::uint64_t epoch) {
        if (Session* s = wxGetApp().session()) {
            s->reportDeviceLost(epoch);
        }
    });
    frame->viewport().setFailureHandler(
        [frame](const dvv::Error& error) { frame->onPresenterFailed(error); });
    frame->setDebugTransport(the_arguments.debugTransport);
    frame->startStatusUpdates(*session_);
    frame->Show();
    // The frame may never become active (launched from a terminal): no skim until it does.
    frame->transportBar().frameActivated(frame->IsActive());
    // The native window exists and is shown once the queued call runs.
    frame->viewport().CallAfter([viewport = &frame->viewport()] { viewport->markReady(); });

    // 9. The stream of the command line, at frame 0, through the path of File -> Open URI... (its
    // open error carries the FFmpeg status hint for a lavfi: URI, design v2 §4.6), and its Open
    // Recent entry (Open Recent spec §4).
    if (the_arguments.uri) {
        frame->openUri(0, *the_arguments.uri, std::nullopt, typedRecentEntry(*the_arguments.uri));
    }

    // 10. A scenario without a script yet finishes with 6 (SelfTest::start).
    if (selftest()) {
        frame->startSelfTest(the_arguments.selftest);
    }
    return true;
}

MainFrame* App::mainFrame() const {
    for (wxWindow* window : wxTopLevelWindows) {
        if (auto* frame = dynamic_cast<MainFrame*>(window);
            frame != nullptr && !frame->IsBeingDeleted()) {
            return frame;
        }
    }
    return nullptr;
}

void App::setAppearance() {
    const AppearanceResult result = SetAppearance(wxAppearance(the_arguments.appearance));
    appearance_result_ = result;
    DVV_LOG_INFO("appearance: {} -> {}", appearanceName(the_arguments.appearance),
                 appearanceResultName(result));
}

bool App::startLogAndConfig() {
    // §7.1: a selftest never reads or writes the user's config or log file. With --config, its own
    // config starts as a copy of that file (log level design §8: smoke_source), which it never
    // writes.
    if (selftest()) {
        const std::filesystem::path seed = the_arguments.config
                                               ? dvv::pathFromText(*the_arguments.config)
                                               : std::filesystem::path();
        dvv::Result<std::filesystem::path> made = makeSelftestConfig(seed);
        if (!made) {
            finish(exit_code::BAD_ARGUMENTS,
                   "rawreel-gui --selftest: its config folder could not be made: " +
                       made.error().describe());
            return false;
        }
        the_arguments.config = dvv::pathText(*made);
    }
    // Its lines reach the store and stderr; the log file gets them when it opens.
    LoadedConfig loaded = loadConfig(the_arguments.config);
    config_ = std::move(loaded.config);
    config_path_ = std::move(loaded.path);
    // Open Recent spec §7, §8: the history keys of the text the config was read from; nothing is
    // written at start.
    if (!loaded.text.empty()) {
        const std::string shown = dvv::pathDisplayText(config_path_);
        RecentConfig history = parseRecent(loaded.text);
        for (const std::string& warning : history.warnings) {
            DVV_LOG_WARN("config '{}': {}", shown, warning);
        }
        DVV_LOG_INFO("config '{}': {} {}, {} {} entries", shown, config_key::GUI_RECENT_SIZE,
                     history.size, config_key::GUI_RECENT, history.entries.size());
        recent_ = RecentList(history.size, std::move(history.entries));
        // The grid and ruler settings: a bad value falls back to its default with a warning.
        overlay_ = parseOverlayConfig(loaded.text);
        for (const std::string& warning : overlay_.warnings) {
            DVV_LOG_WARN("config '{}': {}", shown, warning);
        }
    }
    saved_overlay_ = overlayConfigValues(overlay_.grid, overlay_.rulers, overlay_.gridSavable,
                                         overlay_.rulersSavable);
    // The claim before the first log file: only the settings owner appends to the newest log
    // file (addendum §5.1, D10).
    if (!config_path_.empty()) {
        settings_claim_ = SettingsClaim::tryClaim(config_path_);
    }
    // Addendum §4: log.source for the store's lines and the file's live lines; the lines the
    // store took before this keep the default (no source). The flush delivers the queued ones
    // first, under the old suffix.
    dvv::Logger::instance().flush();
    const dvv::SourceSuffix suffix = sourceSuffix();
    log_store_->setSourceSuffix(suffix);
    log_file_->setSourceSuffix(suffix);
    applyLevel(config_.log.level);  // addendum §2.2: the active level is log.level
    level_source_ = config_.log.levelSaved ? LevelSource::Settings : LevelSource::Default;
    useSystemLog(config_.log.system.enabled);
    if (config_.log.file.enabled) {
        startLogFile();
    }
    // Addendum §3.1: after the file's header and "log file: '<path>'" line.
    dvv::Logger::instance().mark(levelMarker(activeLevel(), level_source_));
    checkLogSize();
    return true;
}

void App::applyLevel(dvv::LogLevel level) {
    // Records logged so far are delivered under the old level (the log thread reads a sink's level
    // when it delivers).
    dvv::Logger::instance().flush();
    // Before the sinks, so a file the switch starts from now on has the new level in its header.
    active_level_->store(level, std::memory_order_relaxed);
    dvv::Logger& logger = dvv::Logger::instance();
    logger.setSinkLevel(*log_file_, level);
    logger.setSinkLevel(*log_store_, level);
    if (the_stderr_sink) {
        logger.setSinkLevel(*the_stderr_sink, level);
    }
}

void App::setActiveLevel(dvv::LogLevel level, LevelSource source) {
    const dvv::LogLevel from = activeLevel();
    level_source_ = source;  // also for the level already active: Settings ends a run-only level
    if (level == from) {
        return;  // addendum §3.1: no marker for the level already active
    }
    applyLevel(level);  // §2.1: the sinks' levels, then the marker
    dvv::Logger::instance().mark(levelChangeMarker(from, level, source));
}

void App::checkLogSplits() {
    const std::uint64_t splits = log_file_ ? log_file_->splits() : 0;
    if (splits == checked_splits_) {
        return;
    }
    checked_splits_ = splits;
    checkLogSize();
}

void App::checkLogSize() {
    if (size_warned_ || log_folder_.empty()) {
        return;
    }
    const std::uintmax_t bytes = log_file_->totalBytes();
    if (bytes <= LOG_SIZE_WARNING_BYTES) {
        return;
    }
    size_warned_ = true;  // once per run
    // Posted: the caller may be on the Log pane's refresh path, where a line logged would post
    // another refresh (spec §3.3).
    CallAfter([text = logSizeWarning(dvv::pathDisplayText(log_folder_), bytes)] {
        DVV_LOG_TEXT(dvv::LogLevel::Warn, text);
    });
}

dvv::Result<std::filesystem::path> App::makeSelftestConfig(const std::filesystem::path& seed) {
    std::error_code error;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(error);
    if (error) {
        return dvv::makeError(dvv::Errc::Io,
                              std::format("no temporary folder: {}", error.message()));
    }
    // A name of its own, created exclusively: an existing path is never reused, a few random
    // names are tried before giving up.
    constexpr int ATTEMPTS = 8;
    for (int attempt = 0; attempt < ATTEMPTS && selftest_config_dir_.empty(); ++attempt) {
        const std::filesystem::path candidate =
            temp / std::format("rawreel-selftest-config-{}-{}", wxGetProcessId(), randomSuffix());
        DVV_TRY(const bool created, createNewFolder(candidate));
        if (created) {
            selftest_config_dir_ = candidate;  // made by this run: removed at the end from here on
        }
    }
    if (selftest_config_dir_.empty()) {
        return dvv::makeError(dvv::Errc::Io, std::format("no new folder in {} after {} names",
                                                         dvv::pathDisplayText(temp), ATTEMPTS));
    }
    const std::filesystem::path& folder = selftest_config_dir_;
    DVV_TRY_VOID(secureNewFolder(folder));
    const std::filesystem::path logs = folder / "logs";
    DVV_TRY(const bool logs_created, createNewFolder(logs));
    if (!logs_created) {
        return dvv::makeError(dvv::Errc::Io,
                              std::format("{} already exists", dvv::pathDisplayText(logs)));
    }
    DVV_TRY_VOID(secureNewFolder(logs));
    std::filesystem::path file = folder / CONFIG_FILE_NAME;
    if (!seed.empty()) {
        DVV_TRY(const std::string text, readConfigText(seed));
        DVV_TRY_VOID(dvv::platform::replaceFile(file, std::as_bytes(std::span(text))));
    }
    const std::array<ConfigValue, 2> values{{
        {.key = config_key::LOG_FILE_FOLDER, .value = dvv::pathText(logs)},
        {.key = config_key::LOG_SYSTEM_ENABLED, .value = false},
    }};
    DVV_TRY_VOID(saveConfigValues(file, values));
    return file;
}

bool App::saveSettings(std::span<const ConfigValue> values, bool closing) {
    if (!ownsSettings()) {
        return false;  // spec §6.3
    }
    const dvv::Result<void> saved = saveConfigValues(config_path_, values);
    if (saved) {
        return true;
    }
    // Spec §5.4: one warning; the setting stays applied for the session.
    const std::string text = std::format("config '{}' not saved: {}",
                                         dvv::pathDisplayText(config_path_), saved.error().message);
    DVV_LOG_TEXT(dvv::LogLevel::Warn, text);
    // The run's first failure also shows a native message, but never while the main window
    // closes (it would block quitting and logging out). A selftest's goes to its hook.
    if (!std::exchange(save_failed_, true) && !closing) {
        wxMessageDialog dialog(GetTopWindow(), "The settings could not be saved.", "rawreel",
                               wxOK | wxICON_ERROR);
        dialog.SetExtendedMessage(wxString::FromUTF8(text));
        (void)showModal(dialog);
    }
    return false;
}

void App::refreshOverlaySavable() {
    if (config_path_.empty()) {
        return;
    }
    std::error_code error;
    if (!std::filesystem::exists(config_path_, error)) {
        overlay_.gridSavable = overlay_.rulersSavable = true;  // the writer creates the file
        return;
    }
    const dvv::Result<std::string> text = readConfigText(config_path_);
    if (!text) {
        return;  // the save reports the file's failure itself
    }
    const OverlayConfig now = parseOverlayConfig(*text);
    const std::string shown = dvv::pathDisplayText(config_path_);
    const auto follow = [&shown](bool& savable, bool savable_now, std::string_view part,
                                 std::string_view key) {
        if (savable && !savable_now) {
            DVV_LOG_WARN(
                "config '{}': {} or a part of it is not an object now; the {} settings "
                "are not saved",
                shown, key, part);
        } else if (!savable && savable_now) {
            DVV_LOG_INFO("config '{}': {} can be written again; the {} settings are saved", shown,
                         key, part);
        }
        savable = savable_now;
    };
    follow(overlay_.gridSavable, now.gridSavable, "grid", "gui.grid");
    follow(overlay_.rulersSavable, now.rulersSavable, "ruler", "gui.rulers");
}

void App::reloadOverlayForTest() {
    const dvv::Result<std::string> text = readConfigText(config_path_);
    overlay_ = parseOverlayConfig(text ? *text : std::string());
    for (const std::string& warning : overlay_.warnings) {
        DVV_LOG_WARN("config '{}': {}", dvv::pathDisplayText(config_path_), warning);
    }
    saved_overlay_ = overlayConfigValues(overlay_.grid, overlay_.rulers, overlay_.gridSavable,
                                         overlay_.rulersSavable);
}

bool App::saveOverlay(const GridConfig& grid, const RulerConfig& rulers, bool closing) {
    refreshOverlaySavable();
    if (!overlay_.gridSavable && !overlay_.rulersSavable) {
        return false;
    }
    std::vector<ConfigValue> values =
        overlayConfigValues(grid, rulers, overlay_.gridSavable, overlay_.rulersSavable);
    const auto same = [](const ConfigValue& a, const ConfigValue& b) {
        return a.key == b.key && a.value == b.value;
    };
    if (std::ranges::equal(values, saved_overlay_, same)) {
        return false;
    }
    if (!saveSettings(values, closing)) {
        return false;
    }
    saved_overlay_ = std::move(values);
    return true;
}

bool App::saveHistory(std::span<const ConfigValue> values) {
    if (!ownsSettings()) {
        return false;  // spec §7: only the settings owner saves
    }
    const dvv::Result<void> saved = saveConfigValues(config_path_, values);
    if (saved) {
        return true;
    }
    // Spec §7: never a dialog; one warning on the run's first history failure only. The list
    // stays in effect for the run.
    if (!std::exchange(history_save_failed_, true)) {
        DVV_LOG_WARN("config '{}' not saved: {}", dvv::pathDisplayText(config_path_),
                     saved.error().message);
    }
    return false;
}

void App::checkSavedOverlay(const OverlayConfig& expected) {
    std::string found;
    if (dvv::Result<std::string> text = readConfigText(config_path_); !text) {
        found = text.error().message;
    } else if (const OverlayConfig saved = parseOverlayConfig(*text);
               (overlay_.gridSavable && saved.grid != expected.grid) ||
               (overlay_.rulersSavable && saved.rulers != expected.rulers)) {
        found = "other values";
    } else {
        return;
    }
    // One line, exit 6, unless an earlier failure decided the code (as checkSavedLogPane).
    record(exit_code::SELFTEST_FAILED,
           std::format("rawreel-gui --selftest: the close did not save the grid and ruler "
                       "settings: '{}' has {}",
                       dvv::pathDisplayText(config_path_), found));
}

void App::checkSavedLogPane(bool saved) {
    std::string found = "no value";
    bool same = false;
    if (dvv::Result<std::string> text = readConfigText(config_path_); !text) {
        found = text.error().message;
    } else if (dvv::Result<Config> parsed = parseConfig(*text); !parsed) {
        found = parsed.error().message;
    } else {
        found = parsed->gui.showLogPane ? "true" : "false";
        same = parsed->gui.showLogPane == saved;
    }
    if (!same) {
        // One line (the log's stderr sink), exit 6 like the other selftest checks, unless an
        // earlier failure decided the code.
        record(exit_code::SELFTEST_FAILED,
               std::format("rawreel-gui --selftest: gui.showLogPane {} was saved at close, but "
                           "'{}' has {}",
                           saved, dvv::pathDisplayText(config_path_), found));
    }
}

void App::useSystemLog(bool on) {
    // Records logged so far reach the system log as it was.
    dvv::Logger::instance().flush();
    dvv::Logger::instance().setSinkLevel(*system_sink_,
                                         on ? dvv::LogLevel::Warn : dvv::LogLevel::Off);
    system_log_enabled_ = on;
    config_.log.system.enabled = on;
}

dvv::Result<std::filesystem::path> App::defaultLogFolder() const {
    if (!selftest_config_dir_.empty()) {
        return selftest_config_dir_ / "logs";  // §7.1
    }
    return dvv::paths::userDataDir("rawreel");
}

bool App::isDefaultLogFolder(const std::filesystem::path& folder) const {
    const dvv::Result<std::filesystem::path> fallback = defaultLogFolder();
    if (!fallback || folder.empty()) {
        return false;
    }
    std::error_code error;
    return folder == *fallback || (std::filesystem::equivalent(folder, *fallback, error) && !error);
}

dvv::Result<std::shared_ptr<dvv::SplitFileSink>> App::prepareLogFile(
    const std::filesystem::path& folder) {
    return openLogFileIn(folder, isDefaultLogFolder(folder) ? FolderCreation::WithParents
                                                            : FolderCreation::LastOnly);
}

dvv::Result<std::shared_ptr<dvv::SplitFileSink>> App::openLogFileIn(
    const std::filesystem::path& folder, FolderCreation creation) {
    DVV_TRY(std::shared_ptr<dvv::SplitFileSink> sink,
            rawreel::openLogFile(
                folder, creation,
                ownsSettings() ? dvv::SplitOpen::AppendNewest : dvv::SplitOpen::NewFile));
    // Runs under the sink's mutex, on the thread whose line starts a new file (the log thread once
    // it runs): it only builds the text (no logging, no lock of ours); the level comes from the
    // shared atomic. A new file from open() gets its header here at once; an appended one gets
    // none.
    sink->setHeader([level = active_level_](const std::filesystem::path& previous) {
        return logFileHeader(level->load(std::memory_order_relaxed),
                             dvv::pathDisplayText(previous.filename()));
    });
    return sink;
}

void App::useLogFile(std::shared_ptr<dvv::SplitFileSink> sink, std::filesystem::path folder) {
    const std::filesystem::path file = sink->currentFile();
    if (!log_folder_.empty()) {
        // Step 3: the last line of the old file names the new one (addendum §5.2: its full path).
        DVV_LOG_INFO("log file moves to '{}'", dvv::pathDisplayText(file));
        dvv::Logger::instance().flush();  // in the old file before the swap, even when queued
    }
    std::shared_ptr<dvv::ILogSink> previous = log_file_->replace(std::move(sink));
    previous.reset();  // outside the switch's lock
    log_folder_ = std::move(folder);
    // Addendum §5.2: after its header, the new file names itself, then the level marker follows.
    // The marker comes after every open, because an appended file (the settings owner reopening a
    // folder whose newest file is below the limit) gets no header to carry the level.
    DVV_LOG_INFO("log file: '{}'", dvv::pathDisplayText(file));
    // The marker names where the active level came from: "(this run, Log pane menu)" for a
    // run-only level.
    dvv::Logger::instance().mark(levelMarker(activeLevel(), level_source_));
    // Addendum §5.3: the files of the new folder; a new count of splits.
    checked_splits_ = log_file_->splits();
    checkLogSize();
}

void App::stopLogFile() {
    if (log_folder_.empty()) {
        return;
    }
    DVV_LOG_INFO("log file turned off");
    dvv::Logger::instance().flush();
    std::shared_ptr<dvv::ILogSink> closed = log_file_->replace(nullptr);
    closed.reset();  // the file closes here, outside the switch's lock
    log_folder_.clear();
}

void App::setLogFileConfig(LogFileConfig file) {
    config_.log.file = std::move(file);
}

void App::setLevelConfig(dvv::LogLevel level) {
    config_.log.level = level;
    config_.log.levelSaved = config_.log.levelSaved || ownsSettings();
}

void App::useFileReads(dvv::platform::FileReadMode mode) {
    config_.io.fileReads = mode;
    if (session_) {
        session_->setFileReads(mode);
    }
}

void App::startLogFile() {
    // Spec §11: without a log file the program goes on with a warning.
    const std::filesystem::path& saved = config_.log.file.folder;
    std::shared_ptr<dvv::SplitFileSink> file;
    std::filesystem::path folder;
    std::optional<std::string> saved_failure;  // why the saved folder cannot be used
    if (!saved.empty()) {
        dvv::Result<std::shared_ptr<dvv::SplitFileSink>> opened =
            openLogFileIn(saved, FolderCreation::LastOnly);
        if (opened) {
            file = std::move(*opened);
            folder = saved;
        } else {
            saved_failure = opened.error().message;
        }
    }
    if (!file && !selftest_config_dir_.empty()) {
        // §7.1: a selftest never writes the user's log file, so no default folder here.
        DVV_LOG_WARN("log folder '{}' cannot be used: {}; no log file in this selftest",
                     dvv::pathDisplayText(saved), saved_failure.value_or("no folder"));
        return;
    }
    if (!file) {
        // §5.3: the default folder; a saved one that failed stays in the config.
        const dvv::Result<std::filesystem::path> directory = dvv::paths::userDataDir("rawreel");
        if (!directory) {
            if (saved_failure) {
                DVV_LOG_WARN("log folder '{}' cannot be used: {}", dvv::pathDisplayText(saved),
                             *saved_failure);
            }
            DVV_LOG_WARN("no log file: {}", directory.error().describe());
            return;
        }
        if (saved_failure) {
            DVV_LOG_WARN("log folder '{}' cannot be used: {}; writing to '{}'",
                         dvv::pathDisplayText(saved), *saved_failure,
                         dvv::pathDisplayText(*directory));
        }
        dvv::Result<std::shared_ptr<dvv::SplitFileSink>> opened =
            openLogFileIn(*directory, FolderCreation::WithParents);
        if (!opened) {
            DVV_LOG_WARN("no log file: {}", opened.error().describe());
            return;
        }
        file = std::move(*opened);
        folder = *directory;
    }
    log_folder_ = std::move(folder);
    // One call: the file first gets the lines the store holds (the config's among them), then
    // takes every later one. Nothing was in the switch before (a null old sink). The log thread
    // delivers records at any time, so the flush first puts every line logged so far into the
    // store. Only the GUI thread logs until the replace (no pool runs yet), so no line falls
    // between the two calls.
    // Addendum §2.3: the store took the lines before the config at every level; the file gets
    // only those at or above the active level (and marks, which pass every level).
    dvv::Logger::instance().flush();
    std::vector<LogEntry> held = log_store_->entries();
    const dvv::LogLevel level = activeLevel();
    std::erase_if(held,
                  [level](const LogEntry& entry) { return !entry.mark && entry.level < level; });
    std::shared_ptr<dvv::ILogSink> previous = log_file_->replace(std::move(file), held);
    previous.reset();  // outside the switch's lock
    // Addendum §5.2: after the header (and the held lines), the file names itself.
    DVV_LOG_INFO("log file: '{}'", dvv::pathDisplayText(currentLogFile()));
    checked_splits_ = log_file_->splits();
}

void App::closeLogAndConfig() {
    dvv::Logger::instance().flush();
    if (log_file_) {
        // At the very end, so the shutdown lines above reached the log file.
        std::shared_ptr<dvv::ILogSink> closed = log_file_->replace(nullptr);
        closed.reset();  // the file closes here, outside the switch's lock
    }
    log_folder_.clear();
    settings_claim_.reset();  // its lock file may sit in the selftest's folder
    if (!selftest_config_dir_.empty()) {
        std::error_code error;
        std::filesystem::remove_all(selftest_config_dir_, error);
        if (error) {
            std::fprintf(
                stderr, "rawreel-gui --selftest: the config folder '%s' was not removed: %s\n",
                dvv::pathDisplayText(selftest_config_dir_).c_str(), error.message().c_str());
        }
        selftest_config_dir_.clear();
    }
}

bool App::startPools() {
    auto cpu = dvv::ExecutorFactory::makeThreadPool(0);
    if (!cpu) {
        finish(exit_code::BAD_ARGUMENTS,
               "rawreel-gui: the thread pools could not start: " + cpu.error().describe());
        return false;
    }
    auto io = dvv::ExecutorFactory::makeThreadPool(IO_THREADS);
    if (!io) {
        finish(exit_code::BAD_ARGUMENTS,
               "rawreel-gui: the thread pools could not start: " + io.error().describe());
        return false;
    }
    cpu_ = std::move(*cpu);
    io_ = std::move(*io);
    return true;
}

bool App::startGpu() {
    auto created = dvv::gpu::GpuInstance::create(
        {.applicationName = "rawreel", .windowSystem = dvv::platform::defaultWindowSystem()});
    if (!created) {
        // M1b: no loader (Unsupported) is a skip for a selftest; every other failure is 4.
        const bool no_device = created.error().code == dvv::Errc::Unsupported;
        finish(no_device && selftest() ? skipExitCode() : exit_code::GPU_ERROR,
               "Vulkan could not start: " + created.error().describe());
        return false;
    }
    gpu_instance_ = std::move(*created);

    const std::span<const dvv::gpu::GpuInfo> devices = gpu_instance_->devices();
    if (the_arguments.gpu) {
        // A bad argument found after enumeration (plan choice): exit 1. A listed device that
        // cannot present fails Session::create: exit 4.
        if (*the_arguments.gpu >= devices.size()) {
            finish(exit_code::BAD_ARGUMENTS,
                   std::format("rawreel-gui: GPU {} does not exist ({} devices)",
                               *the_arguments.gpu, devices.size()));
            return false;
        }
        device_ = dvv::gpu::DeviceId(*the_arguments.gpu);
        return true;
    }
    // gpuStackFactory picks the same device: the first ranked usable one that can present.
    const bool presentable =
        std::ranges::any_of(gpu_instance_->rankedDevices(), [&](dvv::gpu::DeviceId id) {
            const dvv::gpu::GpuInfo& info = devices[id.index()];
            return info.usable() && info.optional.swapchain;
        });
    if (!presentable) {
        finish(selftest() ? skipExitCode() : exit_code::GPU_ERROR,
               "no Vulkan 1.4 device that can present to a window");
        return false;
    }
    return true;
}

void App::shutdownSession() {
    if (produce_delay_) {
        produce_delay_->stop();  // first: the shutdown drain never waits out a delay
    }
    if (bridge_) {
        bridge_->detach();  // no event is queued onto the frame any more
    }
    if (session_) {
        // No presenter exists any more and events are only queued, so nothing it waits for
        // needs the GUI thread.
        dvv::syncWait(session_->shutdown());
        session_.reset();
    }
}

int App::OnExit() {
    // 1. Selftest only (spec §2, §7.3): the gui.showLogPane value the close saved is in the
    // selftest's config file. Nothing is checked when nothing was saved. So are the grid and ruler
    // settings a step left for the close to save.
    if (selftest() && saved_log_pane_) {
        checkSavedLogPane(*saved_log_pane_);
    }
    if (selftest() && expected_overlay_) {
        checkSavedOverlay(*expected_overlay_);
    }
    // Every viewport, and so every presenter, is gone: MainFrame::onClose destroyed it.
    if (session_) {
        shutdownSession();
    }
    bridge_.reset();
    show_history_.reset();
    display_.reset();
    startup_ = {};
    if (selftest() && !decided_exit_code && gpu_instance_) {
        if (const std::uint64_t messages = gpu_instance_->validationMessageCount(); messages != 0) {
            record(exit_code::SELFTEST_FAILED,
                   std::format("rawreel-gui --selftest: {} (close) failed: {} validation messages",
                               scenarioName(the_arguments.selftest), messages));
        } else {
            record(exit_code::SUCCESS, "rawreel-gui --selftest: passed");
        }
    }
    gpu_instance_.reset();
    io_.reset();
    cpu_.reset();
    // Log pane and Settings design §4.3: text copied in rawreel stays on the clipboard after it
    // quits (GTK hands it to the clipboard manager). A selftest's copies went to its hook.
    if (native_policy::flushesClipboardOnExit() && !selftest()) {
        (void)wxTheClipboard->Flush();
    }
    // Last: flush, close the log file (every line above reached it) and remove a selftest's
    // config folder.
    closeLogAndConfig();
    return wxApp::OnExit();
}

}  // namespace rawreel
