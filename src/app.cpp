// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "app.h"

#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <utility>

#include "dvv/base/log.h"
#include "dvv/gpu/gpu_info.h"
#include "dvv/platform/native_window.h"
#include "main_frame.h"
#include "vulkan_viewport.h"

namespace rawreel {

namespace {

// Written by main() before wxEntry() and by the GUI thread afterwards; read by main() after
// wxEntry() returns. No other thread touches them.
Arguments the_arguments;
std::optional<int> decided_exit_code;

}  // namespace

void App::setArguments(const Arguments& arguments) noexcept {
    the_arguments = arguments;
}

const Arguments& App::arguments() noexcept {
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
    if (!arguments().selftest && code != exit_code::SUCCESS) {
        wxMessageBox(wxString::FromUTF8(message), "rawreel", wxOK | wxICON_ERROR);
    }
    CallAfter([] {
        if (wxWindow* top = wxTheApp->GetTopWindow(); top != nullptr) {
            top->Close();
        }
    });
}

bool App::OnInit() {
    if (arguments().selftest) {
        // wx's default GUI log target shows modal dialogs; the selftest runs unattended.
        delete wxLog::SetActiveTarget(new wxLogStderr());
    }
    if (!startGpu()) {
        return false;
    }
    auto* frame = new MainFrame(context_, *renderer_);  // wx owns top-level windows
    frame->Show();
    VulkanViewport* viewport = frame->viewport();
    // The native window exists and is shown once the queued call runs.
    viewport->CallAfter([this, viewport] {
        if (const dvv::Result<void> started = viewport->startPresenting(); !started) {
            finish(exit_code::GPU_ERROR,
                   "the viewport could not present: " + started.error().describe());
        }
    });
    if (arguments().selftest) {
        frame->startSelfTest();
    }
    return true;
}

bool App::startGpu() {
    const bool selftest = arguments().selftest;
    auto created = dvv::gpu::GpuRegistry::create(
        {.applicationName = "rawreel", .windowSystem = dvv::platform::defaultWindowSystem()});
    if (!created) {
        const bool no_device = created.error().code == dvv::Errc::Unsupported;
        finish(no_device && selftest ? skipExitCode() : exit_code::GPU_ERROR,
               "Vulkan could not start: " + created.error().describe());
        return false;
    }
    registry_ = std::move(*created);

    const std::optional<dvv::gpu::DeviceId> best = registry_->bestDevice();
    if (!best || !registry_->devices()[best->index()].optional.swapchain) {
        finish(selftest ? skipExitCode() : exit_code::GPU_ERROR,
               "no Vulkan 1.4 device that can present to a window");
        return false;
    }
    const dvv::gpu::GpuInfo& info = registry_->devices()[best->index()];

    // Plan choice: without a cache directory the pipeline cache stays in memory.
    std::filesystem::path cache_file;
    if (auto file = dvv::gpu::defaultPipelineCacheFile("rawreel", info); file) {
        cache_file = std::move(*file);
    } else {
        DVV_LOG_WARN("the pipeline cache stays in memory: {}", file.error().describe());
    }

    auto context =
        registry_->createContext(*best, {.pipelineCacheFile = cache_file, .presentation = true});
    if (!context) {
        finish(exit_code::GPU_ERROR, "Vulkan could not start: " + context.error().describe());
        return false;
    }
    context_ = std::move(*context);

    auto renderer = TestPatternRenderer::create(context_);
    if (!renderer) {
        finish(exit_code::GPU_ERROR, "Vulkan could not start: " + renderer.error().describe());
        return false;
    }
    renderer_ = std::move(*renderer);
    return true;
}

int App::OnExit() {
    // Every viewport, and so every presenter, is gone: MainFrame destroys its viewport on close.
    renderer_.reset();
    context_.reset();  // drains; teardown validation messages are counted now
    if (arguments().selftest && !decided_exit_code && registry_) {
        if (const std::uint64_t messages = registry_->validationMessageCount(); messages != 0) {
            record(exit_code::SELFTEST_FAILED,
                   std::format("rawreel-gui --selftest: step 5 (close) failed: {} validation "
                               "messages",
                               messages));
        } else {
            record(exit_code::SUCCESS, "rawreel-gui --selftest: passed");
        }
    }
    registry_.reset();
    return wxApp::OnExit();
}

}  // namespace rawreel
