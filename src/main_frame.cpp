// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "main_frame.h"

#include <utility>

#include "selftest.h"
#include "vulkan_viewport.h"

namespace rawreel {

MainFrame::MainFrame(std::shared_ptr<dvv::gpu::ExecutionContext> context,
                     TestPatternRenderer& renderer)
    : wxFrame(nullptr, wxID_ANY, "rawreel"),
      viewport_(new VulkanViewport(this, std::move(context), renderer)) {
    // wxFrame lays out its single child to fill the client area.
    SetClientSize(FromDIP(wxSize(960, 540)));
    Bind(wxEVT_ICONIZE, &MainFrame::onIconize, this);
    Bind(wxEVT_SHOW, &MainFrame::onShow, this);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::onClose, this);
}

MainFrame::~MainFrame() {
    // Normally done in onClose already; also covers a frame deleted without a close event.
    stopPresenting();
}

void MainFrame::startSelfTest() {
    if (viewport_ != nullptr && !selftest_) {
        selftest_ = std::make_unique<SelfTest>(*this, *viewport_);
    }
}

void MainFrame::onIconize(wxIconizeEvent& event) {
    if (viewport_ != nullptr) {
        viewport_->topLevelIconized(event.IsIconized());
    }
    event.Skip();
}

void MainFrame::onShow(wxShowEvent& event) {
    if (viewport_ != nullptr) {
        viewport_->topLevelShown(event.IsShown());
    }
    event.Skip();
}

void MainFrame::onClose(wxCloseEvent& event) {
    stopPresenting();
    event.Skip();  // the default handler destroys the frame
}

void MainFrame::stopPresenting() {
    selftest_.reset();
    if (viewport_ != nullptr) {
        // Clear the member first, so nothing forwards to the viewport while it is destroyed.
        VulkanViewport* viewport = std::exchange(viewport_, nullptr);
        viewport->Destroy();  // a child window is deleted now; its destructor resets the presenter
    }
}

}  // namespace rawreel
