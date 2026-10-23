#pragma once

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <memory>

#include "dvv/base/error.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/presenter.h"
#include "dvv/platform/native_window.h"

namespace rawreel {

class TestPatternRenderer;

// The viewport (design M1b §5.6): a native child window that owns one Presenter, which shows the
// test pattern. The GUI thread only calls the presenter's non-blocking members.
class VulkanViewport final : public wxWindow {
public:
    VulkanViewport(wxWindow* parent, std::shared_ptr<dvv::gpu::ExecutionContext> context,
                   TestPatternRenderer& renderer);
    VulkanViewport(const VulkanViewport&) = delete;
    VulkanViewport& operator=(const VulkanViewport&) = delete;
    // Resets the presenter first (binding, §5.6): its thread is joined while the native window
    // still exists, and no CallAfter from its error handler can target this object afterwards.
    ~VulkanViewport() override;

    // Creates the presenter for the realised native window; call it through CallAfter once the
    // top-level frame is shown.
    [[nodiscard]] dvv::Result<void> startPresenting();
    // Null before startPresenting() and while the presenter is being destroyed.
    [[nodiscard]] dvv::gpu::Presenter* presenter() const noexcept { return presenter_.get(); }
    // Forwarded by the frame (wxEVT_ICONIZE and wxEVT_SHOW of the top-level window).
    void topLevelIconized(bool iconized);
    void topLevelShown(bool shown);
    // The client area in physical pixels (wx 3.3 ToPhys).
    [[nodiscard]] dvv::platform::PixelSize physicalClientSize() const;

private:
    void onPaint(wxPaintEvent& event);
    void onSize(wxSizeEvent& event);
    void showError(const dvv::Error& error);  // GUI thread

    std::shared_ptr<dvv::gpu::ExecutionContext> context_;
    TestPatternRenderer* renderer_;  // owned by the App, outlives every viewport
    std::unique_ptr<dvv::gpu::Presenter> presenter_;
    wxString status_;  // a presenter failure, drawn instead of the image
};

}  // namespace rawreel
