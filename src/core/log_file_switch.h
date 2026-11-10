#pragma once

// The log file as a Logger sink that can change files (Settings window design): the Logger keeps
// one LogFileSwitch for the whole run and the GUI replaces the sink inside it, so no sink is ever
// removed from the Logger. wx-free. Lock order: Logger -> switch; replace() takes only the
// switch's mutex and hands the old sink back so the caller drops it outside any lock.

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

inline constexpr std::uintmax_t LOG_FILE_BYTES = std::uintmax_t{4} << 20U;
inline constexpr int LOG_FILE_BACKUPS = 3;
inline constexpr std::string_view LOG_FILE_NAME = "rawreel.log";

enum class FolderCreation : std::uint8_t {
    LastOnly,     // a chosen folder: only the last component, only when its parent exists
    WithParents,  // the default (user data) folder, as today
};

[[nodiscard]] dvv::Result<void> prepareLogFolder(const std::filesystem::path& folder,
                                                 FolderCreation creation);

// prepareLogFolder, then RotatingFileSink::open(folder / LOG_FILE_NAME, LOG_FILE_BYTES,
// LOG_FILE_BACKUPS).
[[nodiscard]] dvv::Result<std::shared_ptr<dvv::RotatingFileSink>> openLogFile(
    const std::filesystem::path& folder, FolderCreation creation);

class LogFileSwitch final : public dvv::ILogSink {
public:
    void write(const dvv::LogRecord& record,
               std::string_view line) override;  // to the file, if any
    void batchEnd() override;
    void flush() override;
    // Under the switch's mutex: writes `backfill`'s lines into `file` (when non-null), then swaps
    // it in. Returns the old sink, which the caller drops outside any lock. Null: no log file.
    [[nodiscard]] std::shared_ptr<dvv::ILogSink> replace(std::shared_ptr<dvv::ILogSink> file,
                                                         std::span<const LogEntry> backfill = {});

private:
    std::mutex mutex_;
    std::shared_ptr<dvv::ILogSink> file_;
};

}  // namespace rawreel
