// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "vulkan_viewport.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <variant>

#include "dvv/base/log.h"
#include "native_window.h"
#include "test_pattern_renderer.h"

namespace rawreel {

VulkanViewport::VulkanViewport(wxWindow* parent,
                               std::shared_ptr<dvv::gpu::ExecutionContext> context,
                               TestPatternRenderer& renderer)
    : context_(std::move(context)), renderer_(&renderer) {
    // wx never erases the background, so nothing paints over the presented image.
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE);
    Bind(wxEVT_PAINT, &VulkanViewport::onPaint, this);
    Bind(wxEVT_SIZE, &VulkanViewport::onSize, this);
}

VulkanViewport::~VulkanViewport() {
    // Moved out first: while ~Presenter waits (and on Windows dispatches cross-thread sent
    // messages), presenter_ is already null, so no handler of this window calls into the presenter
    // being destroyed.
    std::unique_ptr<dvv::gpu::Presenter> presenter = std::move(presenter_);
    presenter.reset();
}

dvv::Result<void> VulkanViewport::startPresenting() {
    const dvv::platform::NativeWindow window = nativeWindowOf(*this);
    if (std::holds_alternative<std::monostate>(window)) {
        return dvv::makeError(dvv::Errc::Unsupported,
                              "the viewport has no native Win32 or X11 window");
    }
    TestPatternRenderer* renderer = renderer_;
    auto presenter = context_->createPresenter(
        {.window = window,
         .extent = physicalClientSize(),
         .visible = IsShownOnScreen(),
         .frames =
             [renderer](const dvv::gpu::PresentFrameInfo& info) { return renderer->frame(info); },
         .onError =
             [this](const dvv::Error& error) noexcept {
                 // Presenter thread. CallAfter allocates, so it may throw; the handler may not.
                 try {
                     CallAfter([this, error] { showError(error); });
                 } catch (...) {
                     std::fputs("rawreel-gui: a presenter error could not reach the GUI\n", stderr);
                 }
             },
         .name = "viewport"});
    if (!presenter) {
        return std::unexpected(std::move(presenter).error());
    }
    presenter_ = std::move(*presenter);
    return {};
}

void VulkanViewport::topLevelIconized(bool iconized) {
    if (presenter_) {
        presenter_->setVisible(!iconized);
    }
}

void VulkanViewport::topLevelShown(bool shown) {
    if (presenter_) {
        presenter_->setVisible(shown);
    }
}

dvv::platform::PixelSize VulkanViewport::physicalClientSize() const {
    const wxSize size = ToPhys(GetClientSize());
    return {.width = static_cast<std::uint32_t>(std::max(size.x, 0)),
            .height = static_cast<std::uint32_t>(std::max(size.y, 0))};
}

void VulkanViewport::onPaint(wxPaintEvent& /*event*/) {
    wxPaintDC dc(this);  // validates the update region, whatever is drawn
    if (!status_.empty()) {
        dc.SetBackground(*wxBLACK_BRUSH);
        dc.Clear();
        dc.SetTextForeground(*wxWHITE);
        dc.DrawText(status_, FromDIP(wxPoint(8, 8)));
        return;
    }
    if (presenter_) {
        presenter_->requestFrame();
    }
}

void VulkanViewport::onSize(wxSizeEvent& event) {
    if (presenter_) {
        presenter_->resize(physicalClientSize());
    }
    event.Skip();
}

void VulkanViewport::showError(const dvv::Error& error) {
    DVV_LOG_ERROR("viewport: {}", error.describe());
    if (presenter_ && presenter_->failure()) {
        status_ = wxString::FromUTF8(error.describe());
        Refresh();
    }
}

}  // namespace rawreel
