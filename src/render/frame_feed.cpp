#include "render/frame_feed.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "core/frame_set.h"
#include "dvv/base/move_only_function.h"

namespace rawreel {

struct FeedSubscriber {
    FeedSubscriber(ViewRoles asked, dvv::MoveOnlyFunction<void()> callback)
        : roles(asked), changed(std::move(callback)) {}

    // Under the feed's mutex_.
    ViewRoles roles;   // FeedSubscription::setRoles() changes it in place
    bool read = true;  // the shown FrameSet was returned by this subscriber's snapshot()
    FeedStats stats;
    // Serialises the callback against the subscription's destructor.
    std::mutex callbackMutex;
    dvv::MoveOnlyFunction<void()> changed;
};

FeedSubscription::FeedSubscription(std::shared_ptr<FrameFeed> feed,
                                   std::shared_ptr<FeedSubscriber> subscriber)
    : feed_(std::move(feed)), subscriber_(std::move(subscriber)) {}

FeedSubscription& FeedSubscription::operator=(FeedSubscription&& other) noexcept {
    if (this != &other) {
        reset();
        feed_ = std::move(other.feed_);
        subscriber_ = std::move(other.subscriber_);
    }
    return *this;
}

FeedSubscription::~FeedSubscription() {
    reset();
}

void FeedSubscription::reset() noexcept {
    if (subscriber_ != nullptr) {
        feed_->unsubscribe(subscriber_);
    }
    subscriber_.reset();
    feed_.reset();
}

FeedSnapshot FeedSubscription::snapshot() const {
    if (subscriber_ == nullptr) {
        return {};
    }
    return feed_->snapshot(*subscriber_);
}

ViewRoles FeedSubscription::roles() const {
    if (subscriber_ == nullptr) {
        return {};
    }
    return feed_->roles(*subscriber_);
}

void FeedSubscription::setRoles(ViewRoles roles) {
    if (subscriber_ != nullptr) {
        feed_->setRoles(*subscriber_, roles);
    }
}

FeedStats FeedSubscription::stats() const {
    if (subscriber_ == nullptr) {
        return {};
    }
    return feed_->stats(*subscriber_);
}

void FrameFeed::show(FrameSet frames) {
    FrameSet replaced;
    FrameSet stripped;
    std::vector<std::shared_ptr<FeedSubscriber>> subscribers;
    {
        std::lock_guard lock(mutex_);
        for (const std::shared_ptr<FeedSubscriber>& subscriber : subscribers_) {
            if (!subscriber->read && frames_.count > 0) {
                ++subscriber->stats.overwritten;  // this subscriber did not read it (E7, R13)
            }
            subscriber->read = false;
        }
        stripped = stripLocked(frames);  // a role nobody shows holds no lease (P3)
        replaced = std::exchange(frames_, std::move(frames));
        ++version_;
        subscribers = subscribers_;
    }
    replaced = FrameSet{};  // the leases are released here, outside every lock (design §5.2)
    stripped = FrameSet{};
    notify(subscribers);
}

void FrameFeed::clear() {
    FrameSet dropped;
    std::vector<std::shared_ptr<FeedSubscriber>> subscribers;
    {
        std::lock_guard lock(mutex_);
        dropped = std::exchange(frames_, FrameSet{});
        for (const std::shared_ptr<FeedSubscriber>& subscriber : subscribers_) {
            subscriber->read = true;
        }
        ++version_;
        subscribers = subscribers_;
    }
    dropped = FrameSet{};
    notify(subscribers);
}

FeedSubscription FrameFeed::subscribe(ViewRoles roles, dvv::MoveOnlyFunction<void()> changed) {
    auto self = shared_from_this();  // first: it throws without an owner, before any state change
    auto subscriber = std::make_shared<FeedSubscriber>(roles, std::move(changed));
    {
        std::lock_guard roles_lock(roles_mutex_);
        bool grown = false;
        ViewRoles now;
        {
            std::lock_guard lock(mutex_);
            subscribers_.push_back(subscriber);
            now = unionLocked();
            grown = now != roles_;
            roles_ = now;
        }
        if (grown && roles_changed_) {
            roles_changed_(now);  // outside mutex_
        }
    }
    return FeedSubscription(std::move(self), std::move(subscriber));
}

void FrameFeed::setRoles(FeedSubscriber& subscriber, ViewRoles roles) {
    FrameSet dropped;
    {
        std::lock_guard roles_lock(roles_mutex_);
        bool changed = false;
        ViewRoles now;
        {
            std::lock_guard lock(mutex_);
            subscriber.roles = roles;
            now = unionLocked();
            changed = now != roles_;
            roles_ = now;
            if (changed) {
                dropped = stripLocked(frames_);  // a dropped role leaves the held set at once
            }
        }
        if (changed && roles_changed_) {
            roles_changed_(now);  // outside mutex_
        }
    }
    dropped = FrameSet{};  // released outside every lock
}

void FrameFeed::unsubscribe(const std::shared_ptr<FeedSubscriber>& subscriber) {
    FrameSet dropped;
    {
        std::lock_guard roles_lock(roles_mutex_);
        bool shrunk = false;
        ViewRoles now;
        {
            std::lock_guard lock(mutex_);
            std::erase(subscribers_, subscriber);
            now = unionLocked();
            shrunk = now != roles_;
            roles_ = now;
            if (shrunk) {
                dropped = stripLocked(frames_);  // the dropped role leaves the held set at once
            }
        }
        if (shrunk && roles_changed_) {
            roles_changed_(now);  // outside mutex_
        }
    }
    dropped = FrameSet{};  // released outside every lock
    dvv::MoveOnlyFunction<void()> previous;
    {
        std::lock_guard lock(subscriber->callbackMutex);  // waits for a running callback
        previous = std::exchange(subscriber->changed, {});
    }
}  // `previous` is destroyed here, outside the lock

void FrameFeed::setRolesChanged(dvv::MoveOnlyFunction<void(ViewRoles) noexcept> callback) {
    dvv::MoveOnlyFunction<void(ViewRoles) noexcept> previous;
    {
        std::lock_guard roles_lock(roles_mutex_);  // waits for a running callback
        previous = std::exchange(roles_changed_, std::move(callback));
        if (roles_changed_) {
            ViewRoles now;
            {
                std::lock_guard lock(mutex_);
                now = roles_;
            }
            roles_changed_(now);  // the current union, once; outside mutex_
        }
    }
}  // `previous` is destroyed here, outside the lock

ViewRoles FrameFeed::roles() const {
    std::lock_guard lock(mutex_);
    return roles_;
}

FeedSnapshot FrameFeed::snapshot(FeedSubscriber& subscriber) const {
    std::lock_guard lock(mutex_);
    subscriber.read = true;
    // The share takes the store's mutex under mutex_ (lock order: feed, then store); the caller
    // drops the snapshot outside both.
    return FeedSnapshot{.frames = frames_.share(subscriber.roles), .version = version_};
}

FeedStats FrameFeed::stats(const FeedSubscriber& subscriber) const {
    std::lock_guard lock(mutex_);
    return subscriber.stats;
}

ViewRoles FrameFeed::roles(const FeedSubscriber& subscriber) const {
    std::lock_guard lock(mutex_);
    return subscriber.roles;
}

ViewRoles FrameFeed::unionLocked() const {
    ViewRoles all = ViewRoles::of(ViewRole::Display);
    for (const std::shared_ptr<FeedSubscriber>& subscriber : subscribers_) {
        all = all | subscriber->roles;
    }
    return all;
}

FrameSet FrameFeed::stripLocked(FrameSet& frames) const {
    FrameSet stripped;
    for (std::size_t r = 0; r < VIEW_ROLE_COUNT; ++r) {
        if (!roles_.has(static_cast<ViewRole>(r))) {
            stripped.leases[r] = std::exchange(frames.leases[r], RoleLeases{});
        }
    }
    return stripped;
}

void FrameFeed::notify(const std::vector<std::shared_ptr<FeedSubscriber>>& subscribers) {
    // Never under mutex_; each subscriber's own mutex serialises its callback against its
    // destructor.
    for (const std::shared_ptr<FeedSubscriber>& subscriber : subscribers) {
        std::lock_guard lock(subscriber->callbackMutex);
        if (subscriber->changed) {
            subscriber->changed();
        }
    }
}

}  // namespace rawreel
