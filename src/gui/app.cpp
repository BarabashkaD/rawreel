// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/app.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "core/timer.h"
#include "dvv/base/environment.h"
#include "dvv/base/executors.h"
#include "dvv/base/log.h"
#include "dvv/base/paths.h"
#include "dvv/base/task.h"
#include "dvv/base/uri.h"
#include "dvv/gpu/gpu_info.h"
#include "dvv/platform/native_window.h"
#include "dvv/registry/capabilities.h"
#include "gui/log_panel.h"
#include "gui/main_frame.h"
#include "gui/selftest.h"
#include "gui/session_bridge.h"
#include "gui/viewport.h"

namespace rawreel {

namespace {

// Written by main() before wxEntry() and by the GUI thread afterwards; read by main() after
// wxEntry() returns. No other thread touches them.
GuiArguments the_arguments;
std::optional<int> decided_exit_code;

// The log file (plan choice): <user data dir>/rawreel.log, 4 MiB, 3 backups.
constexpr std::uintmax_t LOG_FILE_BYTES = std::uintmax_t{4} << 20U;
constexpr int LOG_FILE_BACKUPS = 3;
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

}  // namespace

void App::setArguments(GuiArguments arguments) {
    the_arguments = std::move(arguments);
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
        DVV_LOG_INFO("{}", message);
    } else {
        DVV_LOG_ERROR("{}", message);
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
    if (selftest()) {
        // wx's default GUI log target shows modal dialogs; the selftest runs unattended.
        delete wxLog::SetActiveTarget(new wxLogStderr());
    }
    setAppearance();  // 1. before any window
    openLogFile();    // 2. the log file and the Log pane's sink (before the Session exists)
    log_sink_ = std::make_shared<LogPanelSink>();
    dvv::Logger::instance().addSink(log_sink_);
    if (!startPools() || !startGpu()) {  // 3., 4.
        return false;
    }
    // 5. The source providers.
    components_ = dvv::ComponentRegistry::create(dvv::Capabilities{});
    if (!components_) {
        finish(exit_code::BAD_ARGUMENTS, "rawreel-gui: the component registry could not start");
        return false;
    }
    // The media clock's timer thread (not a GPU failure: exit 1 like the pools).
    auto timer = makeThreadTimer();
    if (!timer) {
        finish(exit_code::BAD_ARGUMENTS,
               "rawreel-gui: the media clock timer could not start: " + timer.error().describe());
        return false;
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
    StackFactory make_stack{};
    if (fault == "stack") {
        make_stack = [](std::optional<dvv::gpu::DeviceId> /*device*/) -> dvv::Result<MediaStack> {
            return dvv::makeError(dvv::Errc::Internal, "injected stack failure");
        };
    }
    dvv::Result<std::unique_ptr<Session>> session =
        Session::create({.components = components_,
                         .cpu = cpu_,
                         .io = io_,
                         .gpus = registry_,
                         .device = device_,
                         .presentation = true,
                         .inflight = the_arguments.inflight,
                         .clock = steadyClock(),
                         .timer = std::move(*timer),
                         .sink = std::move(sink),
                         .events = bridge_,
                         .makeStack = std::move(make_stack)});
    if (!session) {
        finish(exit_code::GPU_ERROR, "Vulkan could not start: " + session.error().describe());
        bridge_->detach();
        frame->Destroy();  // never shown; wx deletes it with the remaining windows
        return false;
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
    frame->Show();
    // The native window exists and is shown once the queued call runs.
    frame->viewport().CallAfter([viewport = &frame->viewport()] { viewport->markReady(); });

    // 9. The stream of the command line, at frame 0.
    if (the_arguments.uri) {
        if (dvv::Result<dvv::Uri> uri = dvv::parseUri(*the_arguments.uri); uri) {
            session_->open(0, dvv::media::SourceRequest{.uri = std::move(*uri), .hint = {}});
        } else {
            frame->onError(uri.error());
        }
    }

    // 10. A scenario without a script yet finishes with 6 (SelfTest::start).
    if (selftest()) {
        frame->startSelfTest(the_arguments.selftest);
    }
    return true;
}

void App::setAppearance() {
    const AppearanceResult result = SetAppearance(wxAppearance(the_arguments.appearance));
    appearance_result_ = result;
    DVV_LOG_INFO("appearance: {} -> {}", appearanceName(the_arguments.appearance),
                 appearanceResultName(result));
}

void App::openLogFile() {
    // Spec §11: without a log file the program goes on with a warning.
    const dvv::Result<std::filesystem::path> directory = dvv::paths::userDataDir("rawreel");
    if (!directory) {
        DVV_LOG_WARN("no log file: {}", directory.error().describe());
        return;
    }
    if (const dvv::Result<void> made = dvv::paths::ensureDirectory(*directory); !made) {
        DVV_LOG_WARN("no log file: {}", made.error().describe());
        return;
    }
    auto sink =
        dvv::RotatingFileSink::open(*directory / "rawreel.log", LOG_FILE_BYTES, LOG_FILE_BACKUPS);
    if (!sink) {
        DVV_LOG_WARN("no log file: {}", sink.error().describe());
        return;
    }
    dvv::Logger::instance().addSink(std::move(*sink));
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
    auto created = dvv::gpu::GpuRegistry::create(
        {.applicationName = "rawreel", .windowSystem = dvv::platform::defaultWindowSystem()});
    if (!created) {
        // M1b: no loader (Unsupported) is a skip for a selftest; every other failure is 4.
        const bool no_device = created.error().code == dvv::Errc::Unsupported;
        finish(no_device && selftest() ? skipExitCode() : exit_code::GPU_ERROR,
               "Vulkan could not start: " + created.error().describe());
        return false;
    }
    registry_ = std::move(*created);

    const std::span<const dvv::gpu::GpuInfo> devices = registry_->devices();
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
        std::ranges::any_of(registry_->rankedDevices(), [&](dvv::gpu::DeviceId id) {
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
    // Every viewport, and so every presenter, is gone: MainFrame::onClose destroyed it.
    if (session_) {
        shutdownSession();
    }
    bridge_.reset();
    show_history_.reset();
    display_.reset();
    components_.reset();
    if (selftest() && !decided_exit_code && registry_) {
        if (const std::uint64_t messages = registry_->validationMessageCount(); messages != 0) {
            record(exit_code::SELFTEST_FAILED,
                   std::format("rawreel-gui --selftest: {} (close) failed: {} validation messages",
                               scenarioName(the_arguments.selftest), messages));
        } else {
            record(exit_code::SUCCESS, "rawreel-gui --selftest: passed");
        }
    }
    registry_.reset();
    io_.reset();
    cpu_.reset();
    dvv::Logger::instance().flush();
    return wxApp::OnExit();
}

}  // namespace rawreel
