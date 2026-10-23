#pragma once

// rawreel-gui's main window (design M1b §5.6): one VulkanViewport that fills the client area.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <memory>

#include "dvv/gpu/execution_context.h"

namespace rawreel {

class SelfTest;
class TestPatternRenderer;
class VulkanViewport;

class MainFrame final : public wxFrame {
public:
    // The frame keeps no reference to the context: only the viewport (through its presenter) does,
    // so App::OnExit() releases the last one once the viewport is gone.
    MainFrame(std::shared_ptr<dvv::gpu::ExecutionContext> context, TestPatternRenderer& renderer);
    MainFrame(const MainFrame&) = delete;
    MainFrame& operator=(const MainFrame&) = delete;
    ~MainFrame() override;

    // Null once the frame is closing.
    [[nodiscard]] VulkanViewport* viewport() const noexcept { return viewport_; }
    // Starts the --selftest script (selftest.h).
    void startSelfTest();

private:
    void onIconize(wxIconizeEvent& event);
    void onShow(wxShowEvent& event);
    void onClose(wxCloseEvent& event);
    // Stops the selftest, then destroys the viewport now, so its presenter is reset on the window
    // thread while the native windows still exist.
    void stopPresenting();

    VulkanViewport* viewport_;  // a child window: wx owns it
    std::unique_ptr<SelfTest> selftest_;
};

}  // namespace rawreel
