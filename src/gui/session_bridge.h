#pragma once

// clang-format off
#include <wx/wx.h>  // first (M1b R11)
// clang-format on

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/session_types.h"

namespace rawreel {

class MainFrame;

// ISessionEvents for the GUI (design D10): every event is queued onto the frame with CallAfter and
// handled on the GUI thread. Called on the Session executor; never blocks. Event lambdas carry
// values and weak_ptrs only, never a strong reference to a managed object.
class SessionBridge final : public ISessionEvents {
public:
    explicit SessionBridge(MainFrame& frame) noexcept;
    // GUI thread, before the frame dies: afterwards no event is queued (already queued ones die
    // with the frame's pending events).
    void detach();

    void streamsChanged(std::vector<StreamInfo> streams) override;
    void formatNeeded(std::uint32_t slot, dvv::media::SourceRequest request,
                      dvv::media::Ambiguous candidates, std::string reason) override;
    void stackChanged(std::weak_ptr<dvv::gpu::ExecutionContext> context,
                      std::uint64_t epoch) override;
    void error(dvv::Error error) override;

private:
    template <class F>
    void forward(
        const F& handler);  // F: copyable callable taking MainFrame& (wx copies the functor)

    std::mutex mutex_;
    MainFrame* frame_;
};

}  // namespace rawreel
