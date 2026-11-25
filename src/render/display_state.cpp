#include "render/display_state.h"

#include <mutex>
#include <optional>
#include <utility>

#include "dvv/media/view.h"

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

void DisplayState::setView(dvv::media::ViewRequest view) {
    {
        std::lock_guard lock(mutex_);
        if (params_.view == view) {
            return;
        }
        params_.view = view;
    }
    notify();
}

void DisplayState::setOverlay(const OverlayParams& overlay) {
    {
        std::lock_guard lock(mutex_);
        if (params_.overlay == overlay) {
            return;
        }
        params_.overlay = overlay;
    }
    notify();
}

void DisplayState::setCursor(std::optional<dvv::media::PointD> cursor) {
    {
        std::lock_guard lock(mutex_);
        if (params_.cursor == cursor) {
            return;
        }
        params_.cursor = cursor;
    }
    notify();
}

void DisplayState::setAlpha(bool on) {
    {
        std::lock_guard lock(mutex_);
        if (params_.alpha == on) {
            return;
        }
        params_.alpha = on;
    }
    notify();
}

DisplaySnapshot DisplayState::snapshot() const {
    std::lock_guard lock(mutex_);
    read_ = true;
    // The share takes the store's mutex under mutex_ (lock order: display state, then store); the
    // caller drops the snapshot outside both.
    return DisplaySnapshot{frames_.share(), params_, version_};
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

void DisplayState::setOnChanged(dvv::MoveOnlyFunction<void()> callback) {
    dvv::MoveOnlyFunction<void()> previous;
    {
        std::lock_guard lock(callback_mutex_);  // waits for a running callback
        previous = std::exchange(on_changed_, std::move(callback));
    }
}  // `previous` is destroyed here, outside the lock

}  // namespace rawreel
