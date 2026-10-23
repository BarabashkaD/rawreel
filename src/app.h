#pragma once

// rawreel-gui's wx application (spec §10.2; design M1b §5.6): GPU startup, the main frame and the
// exit code.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <memory>
#include <optional>
#include <string>

#include "arguments.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/gpu_registry.h"
#include "test_pattern_renderer.h"

namespace rawreel {

class App final : public wxApp {
public:
    // main() passes the parsed arguments before wxEntry().
    static void setArguments(const Arguments& arguments) noexcept;
    [[nodiscard]] static const Arguments& arguments() noexcept;
    // The exit code the App decided (0 after a passed selftest, 4, 6, 77); nullopt otherwise.
    // main() reads it after wxEntry(), which returns -1 when OnInit() fails.
    [[nodiscard]] static std::optional<int> exitCode() noexcept;
    // Records the exit code (the first one wins) and logs `message`.
    static void record(int code, const std::string& message);

    // Records the exit code (the first one wins), reports `message` (stderr through the log; a
    // modal wxMessageBox as well when not --selftest and code != 0) and closes the top window. The
    // close is queued (CallAfter), so a caller inside a window's or timer's handler returns first.
    void finish(int code, const std::string& message);

    // Does not call wxApp::OnInit(), which would parse the command line again.
    bool OnInit() override;
    // Releases the renderer and the context (which drains), checks the validation messages of a
    // --selftest run, then releases the registry.
    int OnExit() override;

    [[nodiscard]] dvv::gpu::GpuRegistry* registry() const noexcept { return registry_.get(); }

private:
    // Registry, presentable device, context and renderer; false (after finish()) when one fails.
    [[nodiscard]] bool startGpu();

    std::shared_ptr<dvv::gpu::GpuRegistry> registry_;
    std::shared_ptr<dvv::gpu::ExecutionContext> context_;
    std::unique_ptr<TestPatternRenderer> renderer_;
};

}  // namespace rawreel

wxDECLARE_APP(rawreel::App);
