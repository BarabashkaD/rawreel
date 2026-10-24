#pragma once

// rawreel-gui's wx application (spec §10.2; design M1b §5.6, M3 v2 §5.5 and §7): appearance, log
// file, pools, GPU registry, the Session and its bridge, the main frame and the exit code.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <memory>
#include <optional>
#include <string>

#include "core/gui_arguments.h"
#include "core/session.h"
#include "dvv/base/executor.h"
#include "dvv/gpu/device_id.h"
#include "dvv/gpu/gpu_registry.h"
#include "dvv/registry/component_registry.h"
#include "render/display_state.h"

namespace rawreel {

class LogPanelSink;
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
    // Shuts a remaining Session down, checks the validation messages of a --selftest run, then
    // releases the registry and the pools.
    int OnExit() override;

    // Called by MainFrame::onClose: detaches the bridge, then syncWait(session->shutdown()) and
    // releases the Session. Nothing it waits for needs the GUI thread. A second call does nothing.
    void shutdownSession();

    // Null before OnInit() created it and after shutdownSession().
    [[nodiscard]] Session* session() const noexcept { return session_.get(); }
    [[nodiscard]] const std::shared_ptr<dvv::gpu::GpuRegistry>& registry() const noexcept {
        return registry_;
    }
    [[nodiscard]] const std::shared_ptr<DisplayState>& display() const noexcept { return display_; }
    // The Log pane's sink (design D18), in the Logger from OnInit step 2 on; null before.
    [[nodiscard]] const std::shared_ptr<LogPanelSink>& logSink() const noexcept {
        return log_sink_;
    }
    // --selftest only: the Session's sink, recording every shown FrameSet; null otherwise.
    [[nodiscard]] const std::shared_ptr<ShowHistory>& showHistory() const noexcept {
        return show_history_;
    }
    // What SetAppearance() returned (nullopt before OnInit()); Failure and CannotChange are not
    // errors (design §7).
    [[nodiscard]] std::optional<AppearanceResult> appearanceResult() const noexcept {
        return appearance_result_;
    }
    // The objects given to the Session.
    [[nodiscard]] const std::shared_ptr<dvv::ComponentRegistry>& components() const noexcept {
        return components_;
    }
    [[nodiscard]] const std::shared_ptr<dvv::IExecutor>& cpu() const noexcept { return cpu_; }
    [[nodiscard]] const std::shared_ptr<dvv::IExecutor>& io() const noexcept { return io_; }

private:
    void setAppearance();
    void openLogFile();
    [[nodiscard]] bool startPools();
    // The registry and the device of --gpu; false (after finish()) when there is none to use.
    [[nodiscard]] bool startGpu();

    // Declared in the reverse order of their release (the members' destruction order also covers
    // a failed OnInit(), after which wx does not call OnExit()).
    std::optional<AppearanceResult> appearance_result_;
    std::shared_ptr<LogPanelSink> log_sink_;  // also held by the Logger (never removed)
    std::shared_ptr<dvv::IExecutor> cpu_;
    std::shared_ptr<dvv::IExecutor> io_;
    std::shared_ptr<dvv::gpu::GpuRegistry> registry_;
    std::optional<dvv::gpu::DeviceId> device_;
    std::shared_ptr<dvv::ComponentRegistry> components_;
    std::shared_ptr<DisplayState> display_;
    std::shared_ptr<ShowHistory> show_history_;
    std::shared_ptr<SessionBridge> bridge_;
    std::unique_ptr<Session> session_;
};

}  // namespace rawreel

wxDECLARE_APP(rawreel::App);
