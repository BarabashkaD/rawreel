#include "core/log_store.h"

#include <format>
#include <utility>

#include "dvv/base/error.h"

namespace rawreel {

void LogStore::write(const dvv::LogRecord& record, std::string_view line) {
    LogEntry entry;
    entry.level = record.level;
    entry.time = record.time;
    entry.message = std::string(record.message);
    entry.source = std::format("{}:{}", dvv::sourceFileName(record.where), record.where.line());
    entry.line = std::string(line);

    const std::lock_guard lock(mutex_);
    entry.sequence = next_++;
    if (record.level == dvv::LogLevel::Warn) {
        ++counts_.warnings;
    } else if (record.level == dvv::LogLevel::Error) {
        ++counts_.errors;
    }
    entries_.push_back(std::move(entry));
    if (entries_.size() > LOG_STORE_RECORDS) {
        entries_.pop_front();
    }
    if (!pending_ && notify_) {
        pending_ = true;
        notify_();
    }
}

void LogStore::setNotify(std::function<void()> notify) {
    const std::lock_guard lock(mutex_);
    notify_ = std::move(notify);
}

LogBatch LogStore::take(std::uint64_t since) {
    const std::lock_guard lock(mutex_);
    pending_ = false;
    LogBatch batch;
    batch.counts = counts_;
    batch.next = next_;
    const std::uint64_t oldest = entries_.empty() ? next_ : entries_.front().sequence;
    batch.gap = since < oldest;
    for (const LogEntry& entry : entries_) {
        if (entry.sequence >= since) {
            batch.entries.push_back(entry);
        }
    }
    return batch;
}

std::vector<LogEntry> LogStore::entries() const {
    const std::lock_guard lock(mutex_);
    return {entries_.begin(), entries_.end()};
}

std::uint64_t LogStore::next() const {
    const std::lock_guard lock(mutex_);
    return next_;
}

}  // namespace rawreel
