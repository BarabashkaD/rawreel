#include "core/player.h"

#include <algorithm>

namespace rawreel {

namespace {

// a mod b in [0, b) for b > 0.
std::int64_t floorMod(std::int64_t a, std::int64_t b) noexcept {
    const std::int64_t r = a % b;
    return r < 0 ? r + b : r;
}

}  // namespace

PlayerLogic::PlayerLogic(PlayerConfig config) : config_(config) {}

bool PlayerLogic::active() const noexcept {
    return started_ && (!config_.length || *config_.length > 0);
}

Clock::duration PlayerLogic::period() const noexcept {
    // fps.den / fps.num seconds, truncated to the clock tick. A non-positive fps (not a valid
    // stream) plays at the default 25 fps.
    std::int64_t num = config_.fps.num;
    std::int64_t den = config_.fps.den;
    if (num <= 0 || den <= 0) {
        num = 25;
        den = 1;
    }
    using Period = Clock::period;
    // ticks = den / num seconds * (Period::den / Period::num) ticks per second.
    const std::int64_t per_second = Period::den / Period::num;
    const std::int64_t whole = den / num;
    const std::int64_t rest = den % num;
    return Clock::duration(whole * per_second + rest * per_second / num);
}

std::int64_t PlayerLogic::clampIndex(std::int64_t index) const noexcept {
    if (config_.length) {
        return std::clamp<std::int64_t>(index, 0, *config_.length - 1);
    }
    return std::max<std::int64_t>(index, 0);
}

std::int64_t PlayerLogic::base() const noexcept {
    return wanted_.value_or(position_);
}

std::int64_t PlayerLogic::target() const noexcept {
    return base();
}

std::optional<std::int64_t> PlayerLogic::next() const noexcept {
    if (wanted_) {
        return wanted_;
    }
    if (config_.length && position_ + 1 == *config_.length) {
        return loop_ ? std::optional<std::int64_t>(0) : std::nullopt;
    }
    return position_ + 1;
}

void PlayerLogic::reset(PlayerConfig config, std::int64_t position) {
    config_ = config;
    started_ = true;
    ready_.clear();
    outstanding_.clear();
    failed_.clear();
    show_.reset();
    shows_.clear();
    stalled_ = false;
    deadline_.reset();
    playing_ = false;
    position_ = -1;
    direction_ = 1;
    wanted_.reset();
    if (active()) {
        wanted_ = clampIndex(position);
    }
}

void PlayerLogic::play(Clock::time_point now) {
    if (!active() || (position_ < 0 && !wanted_)) {
        return;
    }
    if (config_.length && !loop_ && !wanted_ && position_ == *config_.length - 1) {
        wanted_ = 0;  // at the end without loop: restart from the beginning
    }
    playing_ = true;
    direction_ = 1;
    stalled_ = false;
    deadline_ = now + period();
    prune();
}

void PlayerLogic::pause() {
    playing_ = false;
    deadline_.reset();
    if (stalled_) {
        stalled_ = false;
        wanted_.reset();  // it came from playback
    }
    prune();
}

void PlayerLogic::stop() {
    pause();
    seek(0);
}

void PlayerLogic::step(std::int64_t delta) {
    pause();
    if (!active() || delta == 0) {
        return;
    }
    direction_ = delta > 0 ? 1 : -1;
    moveTo(clampIndex(base() + delta));
}

void PlayerLogic::seek(std::int64_t index) {
    pause();
    if (!active()) {
        return;
    }
    const std::int64_t target = clampIndex(index);
    if (target != base()) {
        direction_ = target > base() ? 1 : -1;
    }
    moveTo(target);
}

void PlayerLogic::moveTo(std::int64_t target) {
    failed_.erase(target);
    if (target == position_ && !wanted_) {
        prune();  // the direction may have changed
        return;
    }
    wanted_ = target;
    prune();
    if (ready_.contains(target)) {
        show(target, false, Clock::time_point{});  // not a playback show: no time
    }
}

void PlayerLogic::setLoop(bool loop) {
    loop_ = loop;
    prune();
}

void PlayerLogic::setWindow(std::uint32_t window) {
    config_.window = std::max<std::uint32_t>(window, 1);
    prune();
}

bool PlayerLogic::frameReady(std::int64_t index, Clock::time_point now) {
    outstanding_.erase(index);
    if (!active() || !keeps(index)) {
        return false;
    }
    ready_.insert(index);
    if (wanted_ == index) {
        show(index, playing_, now);
        if (stalled_) {
            stalled_ = false;
            deadline_ = now + period();  // re-anchor, no catch-up
        }
    }
    return true;
}

void PlayerLogic::frameFailed(std::int64_t index) {
    outstanding_.erase(index);
    if (!active()) {
        return;
    }
    failed_.insert(index);
    if (wanted_ == index) {
        // A seek or step target, or the frame a stalled playback waits for: the position stays
        // the last shown frame.
        pause();
        wanted_.reset();
        prune();
    }
}

void PlayerLogic::tick(Clock::time_point now) {
    if (!active() || !playing_ || stalled_ || !deadline_ || *deadline_ > now) {
        return;
    }
    const std::optional<std::int64_t> index = next();
    if (!index || failed_.contains(*index)) {
        pause();  // the end without loop, or a failed frame: the position stays
        return;
    }
    if (ready_.contains(*index)) {
        show(*index, true, now);
        *deadline_ += period();
        if (*deadline_ <= now) {
            deadline_ = now + period();  // late tick: re-anchor
        }
        return;
    }
    wanted_ = index;
    stalled_ = true;
    ++stalls_;
    prune();
}

void PlayerLogic::show(std::int64_t index, bool playback, Clock::time_point now) {
    position_ = index;
    wanted_.reset();
    show_ = index;
    ++shown_;
    if (playback) {
        while (!shows_.empty() && shows_.front() <= now - std::chrono::seconds(1)) {
            shows_.pop_front();
        }
        shows_.push_back(now);
    }
    prune();
}

void PlayerLogic::prune() {
    std::erase_if(ready_, [this](std::int64_t entry) { return !keeps(entry); });
}

std::optional<std::int64_t> PlayerLogic::takeShow() {
    return std::exchange(show_, std::nullopt);
}

std::vector<std::int64_t> PlayerLogic::takeRequests() {
    std::vector<std::int64_t> requests;
    const std::int64_t b = base();
    if (!active() || b < 0) {
        return requests;
    }
    const std::optional<std::int64_t> length = config_.length;
    const bool wrap = length && loop_;
    const auto consider = [&](std::int64_t index) {
        if (ready_.contains(index) || outstanding_.contains(index) || failed_.contains(index) ||
            index == position_) {
            return;
        }
        outstanding_.insert(index);
        requests.push_back(index);
    };
    if (wanted_) {
        if (outstanding_.size() >= config_.inflight) {
            return requests;
        }
        consider(b);
    }
    for (std::int64_t j = 1; j <= static_cast<std::int64_t>(config_.window); ++j) {
        if (outstanding_.size() >= config_.inflight) {
            break;
        }
        std::int64_t index = b + j * direction_;
        if (wrap) {
            index = floorMod(index, *length);
        } else if (index < 0 || (length && index >= *length)) {
            break;
        }
        consider(index);
    }
    return requests;
}

std::pair<std::int64_t, std::int64_t> PlayerLogic::keepRange() const {
    const std::int64_t b = base();
    const auto window = static_cast<std::int64_t>(config_.window);
    if (direction_ > 0) {
        return {b - 2, b + window};
    }
    return {b - window, b + 2};
}

bool PlayerLogic::keeps(std::int64_t index) const {
    if (index == position_) {
        return true;
    }
    const auto [lo, hi] = keepRange();
    if (lo <= index && index <= hi) {
        return true;
    }
    if (config_.length && loop_ && *config_.length > 0) {
        const std::int64_t length = *config_.length;
        if (hi - lo + 1 >= length) {
            return true;
        }
        // The smallest index + k * length that is >= lo.
        return lo + floorMod(index - lo, length) <= hi;
    }
    return false;
}

std::optional<Clock::time_point> PlayerLogic::nextDeadline() const {
    if (playing_ && !stalled_) {
        return deadline_;
    }
    return std::nullopt;
}

PlayerStatus PlayerLogic::status(Clock::time_point now) const {
    PlayerStatus status;
    status.position = position_;
    status.length = config_.length;
    status.playing = playing_;
    status.loop = loop_ && config_.length.has_value();
    status.stalled = stalled_;
    if (config_.fps.den != 0) {
        status.targetFps =
            static_cast<double>(config_.fps.num) / static_cast<double>(config_.fps.den);
    }
    const auto first = now - std::chrono::seconds(1);
    status.actualFps = static_cast<double>(
        std::ranges::count_if(shows_, [&](Clock::time_point t) { return t > first && t <= now; }));
    status.shown = shown_;
    status.stalls = stalls_;
    return status;
}

}  // namespace rawreel
