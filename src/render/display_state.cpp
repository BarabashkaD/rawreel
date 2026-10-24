#include "render/display_state.h"

#include <utility>

namespace rawreel {

void DisplayState::show(FrameSet frames) {
    FrameSet replaced;
    {
        std::lock_guard lock(mutex_);
        if (!read_ && frames_.count > 0) {
            ++stats_.overwritten;  // nobody read it (design E7, R13)
        }
        replaced = std::exchange(frames_, std::move(frames));
        read_ = false;
        ++version_;
    }
    replaced = FrameSet{};  // its leases are released here, outside every lock (design §5.2)
    notify();
}

void DisplayState::clear() {
    FrameSet dropped;
    {
        std::lock_guard lock(mutex_);
        dropped = std::exchange(frames_, FrameSet{});
        read_ = true;
        ++version_;
    }
    dropped = FrameSet{};
    notify();
}

void DisplayState::setParams(CompositeParams params) {
    {
        std::lock_guard lock(mutex_);
        params_ = params;
        ++version_;
    }
    notify();
}

DisplaySnapshot DisplayState::snapshot() const {
    std::lock_guard lock(mutex_);
    read_ = true;
    return DisplaySnapshot{frames_, params_, version_};  // copies shared_ptrs under the lock
}

DisplayStats DisplayState::stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}

void DisplayState::notify() {
    // Never under mutex_; serialises callbacks against setOnChanged().
    std::lock_guard lock(callback_mutex_);
    if (on_changed_) {
        on_changed_();
    }
}

void DisplayState::setOnChanged(std::move_only_function<void()> callback) {
    std::move_only_function<void()> previous;
    {
        std::lock_guard lock(callback_mutex_);  // waits for a running callback
        previous = std::exchange(on_changed_, std::move(callback));
    }
}  // `previous` is destroyed here, outside the lock

}  // namespace rawreel
