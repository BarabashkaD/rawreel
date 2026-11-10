#pragma once

// The log file as a Logger sink that can change files (Settings window design): the Logger keeps
// one LogFileSwitch for the whole run and the GUI replaces the sink inside it, so no sink is ever
// removed from the Logger. wx-free. Lock order: Logger -> switch -> file sink; replace() takes only
// the switch's mutex and hands the old sink back so the caller drops it outside any lock.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>

#include "core/log_store.h"
#include "dvv/base/error.h"
#include "dvv/base/log.h"

namespace rawreel {

// A log file's size limit: a write that would pass it starts a new file (SplitFileSink).
inline constexpr std::uintmax_t LOG_FILE_BYTES = std::uintmax_t{4} << 20U;
// The log files' name prefix: rawreel-YYYYMMDD-HHMMSS[-N].log.
inline constexpr std::string_view LOG_FILE_PREFIX = "rawreel";

enum class FolderCreation : std::uint8_t {
    LastOnly,     // a chosen folder: only the last component, only when its parent exists
    WithParents,  // the default (user data) folder, as today
};

[[nodiscard]] dvv::Result<void> prepareLogFolder(const std::filesystem::path& folder,
                                                 FolderCreation creation);

// prepareLogFolder, then SplitFileSink::open(folder, LOG_FILE_PREFIX, LOG_FILE_BYTES,
// append_newest): append_newest appends to the newest log file below the limit, false always
// creates a new file.
[[nodiscard]] dvv::Result<std::shared_ptr<dvv::SplitFileSink>> openLogFile(
    const std::filesystem::path& folder, FolderCreation creation, bool append_newest);

// `name` is a log file's name: strictly rawreel-YYYYMMDD-HHMMSS.log or
// rawreel-YYYYMMDD-HHMMSS-N.log (N 1 to 9 digits, no leading zero), as SplitFileSink parses it.
// case_insensitive: letters compare without case (Windows and macOS file names).
[[nodiscard]] bool isLogFileName(std::string_view name, bool case_insensitive);

class LogFileSwitch final : public dvv::ILogSink {
public:
    // To the file, if any, as formatLogRecord(record, with_source); `line` is not used.
    void write(const dvv::LogRecord& record, std::string_view line) override;
    void batchEnd() override;
    void flush() override;
    // Whether live lines keep " [file.cpp:42]" (log.source); false by default. Set at start,
    // before any record.
    void setWithSource(bool with_source) noexcept;
    // Under the switch's mutex: writes each `backfill` entry's line unchanged into `file` (when
    // non-null), then swaps it in. Returns the old sink, which the caller drops outside any lock.
    // Null: no log file.
    [[nodiscard]] std::shared_ptr<dvv::SplitFileSink> replace(
        std::shared_ptr<dvv::SplitFileSink> file, std::span<const LogEntry> backfill = {});
    // The file in use (SplitFileSink::currentFile()); empty without a file. Thread-safe.
    [[nodiscard]] std::filesystem::path currentFile() const;
    // The file's SplitFileSink::totalBytes() (the log files in its folder) and splits() (the files
    // it started since it was opened); 0 without a file. Thread-safe. A replace() starts a new
    // count.
    [[nodiscard]] std::uintmax_t totalBytes() const;
    [[nodiscard]] std::uint64_t splits() const;

private:
    mutable std::mutex mutex_;
    std::shared_ptr<dvv::SplitFileSink> file_;
    std::atomic<bool> with_source_{false};
};

}  // namespace rawreel
