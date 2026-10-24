// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/session_bridge.h"

#include <exception>
#include <mutex>
#include <utility>

#include "dvv/base/log.h"
#include "gui/main_frame.h"

namespace rawreel {

SessionBridge::SessionBridge(MainFrame& frame) noexcept : frame_(&frame) {}

void SessionBridge::detach() {
    const std::lock_guard lock(mutex_);
    frame_ = nullptr;
}

template <class F>
void SessionBridge::forward(const F& handler) {
    const std::lock_guard lock(mutex_);
    if (frame_ == nullptr) {
        return;  // detached: the frame is closing
    }
    try {
        MainFrame* frame = frame_;
        // wx copies the functor; F holds values and weak_ptrs only (design D10).
        frame->CallAfter([frame, handler] { handler(*frame); });
    } catch (const std::exception& error) {
        DVV_LOG_ERROR("session event lost: {}", error.what());
    }
}

void SessionBridge::streamsChanged(std::vector<StreamInfo> streams) {
    forward([streams = std::move(streams)](MainFrame& frame) { frame.onStreamsChanged(streams); });
}

void SessionBridge::formatNeeded(std::uint32_t slot, dvv::media::SourceRequest request,
                                 dvv::media::Ambiguous candidates, std::string reason) {
    forward([slot, request = std::move(request), candidates = std::move(candidates),
             reason = std::move(reason)](MainFrame& frame) {
        frame.onFormatNeeded(slot, request, candidates, reason);
    });
}

void SessionBridge::stackChanged(std::weak_ptr<dvv::gpu::ExecutionContext> context,
                                 std::uint64_t epoch) {
    forward([context = std::move(context), epoch](MainFrame& frame) {
        frame.onStackChanged(context, epoch);
    });
}

void SessionBridge::error(dvv::Error error) {
    forward([error = std::move(error)](MainFrame& frame) { frame.onError(error); });
}

}  // namespace rawreel
