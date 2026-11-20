#pragma once

// The viewport's shared state (design M3 v2 §4.4, D23): the latest FrameSet (the Session's
// IDisplaySink) and the composite parameters (GUI). Thread-safe and non-blocking.

#include <cstdint>
#include <functional>
#include <mutex>

#include "core/session_types.h"
#include "core/view.h"
#include "dvv/base/move_only_function.h"

namespace rawreel {

struct CompositeParams {
    bool dark = false;   // background: dark (0.12) or light (0.85) sRGB-encoded grey
    ViewRequest view{};  // resolved by the renderer against its extent and the frame size
    bool operator==(const CompositeParams&) const = default;
};
struct DisplaySnapshot {  // one consistent read, taken under one lock
    FrameSet frames;      // a share of the shown FrameSet, with leases of its own
    CompositeParams params;
    std::uint64_t version = 0;  // +1 for every show(), clear() and setParams()
};
struct DisplayStats {
    std::uint64_t overwritten = 0;  // FrameSets replaced before any snapshot() read them
};

class DisplayState final : public IDisplaySink {
public:
    void show(FrameSet frames) override;
    void clear() override;  // drops every lease it holds
    void setParams(CompositeParams params);
    // show(), clear() and setParams() call the callback after every change while holding
    // callback_mutex_ (never mutex_). Binding: once setOnChanged({}) returns, no callback is
    // running and none will run. The callback must not block (the viewport calls
    // Presenter::requestFrame).
    void setOnChanged(dvv::MoveOnlyFunction<void()> callback);
    [[nodiscard]] DisplaySnapshot snapshot() const;
    [[nodiscard]] DisplayStats stats() const;

private:
    void notify();

    // Lock order: display state, then store. snapshot() takes lease shares under mutex_ (a share
    // takes the store's mutex); show() and clear() drop the leases they replace outside mutex_
    // (a ReadLease destructor takes the store's mutex too).
    mutable std::mutex mutex_;
    FrameSet frames_;
    CompositeParams params_{};
    std::uint64_t version_ = 0;
    mutable bool read_ = true;  // the current FrameSet was returned by snapshot()
    DisplayStats stats_;
    std::mutex callback_mutex_;
    dvv::MoveOnlyFunction<void()> on_changed_;
};

}  // namespace rawreel
