#include "render/view_state.h"

#include <mutex>
#include <optional>
#include <utility>

#include "dvv/base/move_only_function.h"
#include "dvv/media/view.h"

namespace rawreel {

void ViewState::setView(dvv::media::ViewRequest view) {
    {
        std::lock_guard lock(mutex_);
        if (params_.view == view) {
            return;
        }
        params_.view = view;
    }
    notify();
}

void ViewState::setOverlay(const OverlayParams& overlay) {
    {
        std::lock_guard lock(mutex_);
        if (params_.overlay == overlay) {
            return;
        }
        params_.overlay = overlay;
    }
    notify();
}

void ViewState::setCursor(std::optional<dvv::media::PointD> cursor) {
    {
        std::lock_guard lock(mutex_);
        if (params_.cursor == cursor) {
            return;
        }
        params_.cursor = cursor;
    }
    notify();
}

void ViewState::setPaddingMarks(bool on) {
    {
        std::lock_guard lock(mutex_);
        if (params_.paddingMarks == on) {
            return;
        }
        params_.paddingMarks = on;
    }
    notify();
}

void ViewState::setAlpha(bool on) {
    {
        std::lock_guard lock(mutex_);
        if (params_.alpha == on) {
            return;
        }
        params_.alpha = on;
    }
    notify();
}

void ViewState::setInspect(const InspectParams& inspect) {
    {
        std::lock_guard lock(mutex_);
        if (params_.inspect == inspect) {
            return;
        }
        params_.inspect = inspect;
    }
    notify();
}

void ViewState::setMarkers(const MarkerParams& markers) {
    {
        std::lock_guard lock(mutex_);
        if (params_.markers == markers) {
            return;
        }
        params_.markers = markers;
    }
    notify();
}

ViewParams ViewState::params() const {
    std::lock_guard lock(mutex_);
    return params_;
}

void ViewState::notify() {
    // Never under mutex_; serialises callbacks against setOnChanged().
    std::lock_guard lock(callback_mutex_);
    if (on_changed_) {
        on_changed_();
    }
}

void ViewState::setOnChanged(dvv::MoveOnlyFunction<void()> callback) {
    dvv::MoveOnlyFunction<void()> previous;
    {
        std::lock_guard lock(callback_mutex_);  // waits for a running callback
        previous = std::exchange(on_changed_, std::move(callback));
    }
}  // `previous` is destroyed here, outside the lock

}  // namespace rawreel
