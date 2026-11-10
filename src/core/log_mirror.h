#pragma once

// The Log pane's copy of the store (Log pane design): the batches LogStore::take delivered, capped
// at LOG_STORE_RECORDS, plus the mapping of the list's place (selection, anchor, focus, top row)
// from sequence numbers to rows after a batch. wx-free.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "core/log_store.h"

namespace rawreel {

struct MirrorChange {
    std::size_t dropped = 0;   // entries dropped from the front by this apply (0 on reset)
    std::size_t appended = 0;  // entries added at the end
    bool reset = false;        // the mirror was replaced by the batch (a gap)
};

class LogMirror {
public:
    MirrorChange apply(LogBatch batch);  // reset when batch.gap; keeps at most LOG_STORE_RECORDS
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const LogEntry& at(std::size_t row) const;
    [[nodiscard]] std::optional<std::size_t> rowOf(std::uint64_t sequence) const noexcept;
    [[nodiscard]] std::uint64_t next() const noexcept;  // what to pass to LogStore::take
    [[nodiscard]] const std::deque<LogEntry>& entries() const noexcept;
    // The entries with sequences in [first, last], oldest first (snapshots for copy and save).
    [[nodiscard]] std::vector<LogEntry> range(std::uint64_t first, std::uint64_t last) const;

private:
    std::deque<LogEntry> entries_;
    std::uint64_t next_ = 0;
};

// Where the list stood before a batch, by sequence number (spec §4.1).
struct ListPlace {
    std::vector<std::uint64_t> selected;
    std::optional<std::uint64_t> anchor, focus, top;
    bool tail = false;  // the last row was visible
};

struct ListRows {
    std::vector<std::size_t> selected;  // ascending
    std::optional<std::size_t> anchor, focus, top;
    bool tail = false;  // show the last row
};

// Maps `before` onto the mirror after the batch. A dropped top maps to row 0; other dropped
// sequences leave the result. With tail set, top stays empty.
[[nodiscard]] ListRows rowsAfterBatch(const ListPlace& before, const LogMirror& mirror);

}  // namespace rawreel
