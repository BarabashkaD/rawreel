#include "core/log_mirror.h"

#include <algorithm>
#include <utility>

namespace rawreel {

MirrorChange LogMirror::apply(LogBatch batch) {
    MirrorChange change;
    if (batch.gap) {
        entries_.clear();
        change.reset = true;
    }
    change.appended = batch.entries.size();
    for (auto& entry : batch.entries) {
        entries_.push_back(std::move(entry));
    }
    while (entries_.size() > LOG_STORE_RECORDS) {
        entries_.pop_front();
        if (!change.reset) {
            ++change.dropped;
        }
    }
    next_ = batch.next;
    return change;
}

std::size_t LogMirror::size() const noexcept {
    return entries_.size();
}

const LogEntry& LogMirror::at(std::size_t row) const {
    return entries_.at(row);
}

std::optional<std::size_t> LogMirror::rowOf(std::uint64_t sequence) const noexcept {
    if (entries_.empty() || sequence < entries_.front().sequence ||
        sequence > entries_.back().sequence) {
        return std::nullopt;
    }
    // Sequences are contiguous, so the row is the offset from the first one.
    return static_cast<std::size_t>(sequence - entries_.front().sequence);
}

std::uint64_t LogMirror::next() const noexcept {
    return next_;
}

const std::deque<LogEntry>& LogMirror::entries() const noexcept {
    return entries_;
}

std::vector<LogEntry> LogMirror::range(std::uint64_t first, std::uint64_t last) const {
    std::vector<LogEntry> result;
    if (entries_.empty() || first > last) {
        return result;
    }
    const std::uint64_t from = std::max(first, entries_.front().sequence);
    const std::uint64_t to = std::min(last, entries_.back().sequence);
    for (std::uint64_t sequence = from; sequence <= to; ++sequence) {
        result.push_back(entries_[*rowOf(sequence)]);
        if (sequence == to) {
            break;
        }
    }
    return result;
}

ListRows rowsAfterBatch(const ListPlace& before, const LogMirror& mirror) {
    ListRows rows;
    for (const std::uint64_t sequence : before.selected) {
        if (const auto row = mirror.rowOf(sequence)) {
            rows.selected.push_back(*row);
        }
    }
    std::ranges::sort(rows.selected);
    if (before.anchor) {
        rows.anchor = mirror.rowOf(*before.anchor);
    }
    if (before.focus) {
        rows.focus = mirror.rowOf(*before.focus);
    }
    rows.tail = before.tail;
    if (!before.tail && before.top && mirror.size() > 0) {
        rows.top = mirror.rowOf(*before.top);
        if (!rows.top && *before.top < mirror.at(0).sequence) {
            rows.top = 0;
        }
    }
    return rows;
}

}  // namespace rawreel
