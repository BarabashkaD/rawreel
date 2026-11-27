#pragma once

// The latest FrameSet of the Session (its IDisplaySink) for every view that shows it. Each view
// subscribes with the roles it draws; a snapshot holds a share of only those roles. The union of
// the roles (always with Display) is what the Session needs to produce. Thread-safe and
// non-blocking.

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "core/frame_set.h"
#include "dvv/base/move_only_function.h"

namespace rawreel {

struct FeedSnapshot {
    FrameSet frames;            // a share of the shown FrameSet's roles, with leases of its own
    std::uint64_t version = 0;  // +1 for every show() and clear()
};
struct FeedStats {
    std::uint64_t overwritten = 0;  // FrameSets replaced before this subscriber's snapshot()
};

class FrameFeed;
struct FeedSubscriber;  // frame_feed.cpp

// One view's subscription. Move-only; its destructor unsubscribes, and once it returns no callback
// of this subscription is running and none will run. A callback must not drop its own
// subscription from inside it (a deadlock).
class FeedSubscription {
public:
    FeedSubscription() = default;
    FeedSubscription(const FeedSubscription&) = delete;
    FeedSubscription& operator=(const FeedSubscription&) = delete;
    FeedSubscription(FeedSubscription&& other) noexcept = default;
    FeedSubscription& operator=(FeedSubscription&& other) noexcept;
    ~FeedSubscription();

    // A share of only this subscription's roles of the shown FrameSet; marks it read for this
    // subscriber. Takes the store's mutex under the feed's (lock order: feed, then store).
    [[nodiscard]] FeedSnapshot snapshot() const;
    [[nodiscard]] FeedStats stats() const;
    // The roles this subscription asks for now; empty for an empty subscription.
    [[nodiscard]] ViewRoles roles() const;
    // Changes this subscription's roles in place, under the feed's mutex: safe while another
    // thread runs snapshot() on it. A role no subscription asks for any more leaves the held set
    // at once, and a change of the union reaches the roles callback, as subscribe and a drop do.
    // No effect on an empty subscription.
    void setRoles(ViewRoles roles);
    [[nodiscard]] explicit operator bool() const noexcept { return subscriber_ != nullptr; }

private:
    friend class FrameFeed;
    FeedSubscription(std::shared_ptr<FrameFeed> feed, std::shared_ptr<FeedSubscriber> subscriber);
    void reset() noexcept;

    std::shared_ptr<FrameFeed> feed_;
    std::shared_ptr<FeedSubscriber> subscriber_;
};

class FrameFeed final : public IDisplaySink, public std::enable_shared_from_this<FrameFeed> {
public:
    // Both bump the version, release the leases they replace outside the feed's mutex and then
    // call every subscription's callback, outside it too.
    void show(FrameSet frames) override;
    void clear() override;  // drops every lease it holds
    // `changed` runs after every show() and clear(), on their thread, under the subscription's
    // own mutex only; it must not block (a viewport calls Presenter::requestFrame) and must not
    // drop its own subscription from inside it (a deadlock). Needs a shared_ptr owner
    // (shared_from_this).
    [[nodiscard]] FeedSubscription subscribe(ViewRoles roles,
                                             dvv::MoveOnlyFunction<void()> changed);
    // Runs whenever the union of the roles changes, on the thread of subscribe(), of a
    // subscription's setRoles() or of its destructor, outside the feed's state mutex but under the
    // roles callback mutex (roles_mutex_), which serialises the calls: they arrive in order.
    // Installing a callback calls it once with the current union, on the caller's thread and the
    // same way, so the roles subscribed before it reach its owner too. Binding: once
    // setRolesChanged({}) returns, no callback is running and none will run. The callback must
    // not subscribe, change or drop a subscription of this feed, and must not call
    // setRolesChanged() (a deadlock on the roles callback mutex); it cannot throw (a destructor
    // calls it).
    void setRolesChanged(dvv::MoveOnlyFunction<void(ViewRoles) noexcept> callback);
    // The union of the subscriptions' roles, plus Display.
    [[nodiscard]] ViewRoles roles() const;

private:
    friend class FeedSubscription;
    [[nodiscard]] FeedSnapshot snapshot(FeedSubscriber& subscriber) const;
    [[nodiscard]] FeedStats stats(const FeedSubscriber& subscriber) const;
    [[nodiscard]] ViewRoles roles(const FeedSubscriber& subscriber) const;
    void setRoles(FeedSubscriber& subscriber, ViewRoles roles);
    void unsubscribe(const std::shared_ptr<FeedSubscriber>& subscriber);
    // The union of the subscriptions' roles and Display; under mutex_.
    [[nodiscard]] ViewRoles unionLocked() const;
    // Moves the leases of the roles outside roles_ from `frames` to the returned set; under mutex_.
    [[nodiscard]] FrameSet stripLocked(FrameSet& frames) const;
    static void notify(const std::vector<std::shared_ptr<FeedSubscriber>>& subscribers);

    // Lock order: roles_mutex_, then mutex_, then the store. snapshot() takes lease shares under
    // mutex_; show(), clear() and a dropped role release their leases outside it (a release may
    // free the frame and the store).
    mutable std::mutex mutex_;
    FrameSet frames_;
    std::uint64_t version_ = 0;
    std::vector<std::shared_ptr<FeedSubscriber>> subscribers_;
    ViewRoles roles_ = ViewRoles::of(ViewRole::Display);
    std::mutex roles_mutex_;  // serialises the union changes and the roles callback
    dvv::MoveOnlyFunction<void(ViewRoles) noexcept> roles_changed_;
};

}  // namespace rawreel
