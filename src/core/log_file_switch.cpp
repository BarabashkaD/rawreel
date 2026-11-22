#include "core/log_file_switch.h"

#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "dvv/base/error.h"
#include "dvv/base/log.h"
#include "dvv/base/paths.h"
#include "dvv/base/utf8.h"

namespace rawreel {

namespace fs = std::filesystem;

dvv::Result<void> prepareLogFolder(const fs::path& folder, FolderCreation creation) {
    if (creation == FolderCreation::WithParents) {
        return dvv::paths::ensureDirectory(folder);
    }
    std::error_code error;
    if (fs::is_directory(folder, error)) {
        return {};
    }
    const fs::path parent = folder.parent_path();
    if (!fs::is_directory(parent, error)) {
        return dvv::makeError(dvv::Errc::Io, std::format("the folder {} does not exist",
                                                         dvv::pathDisplayText(parent)));
    }
    if (!fs::create_directory(folder, error) && error) {
        return dvv::makeError(
            dvv::Errc::Io,
            std::format("cannot create {}: {}", dvv::pathDisplayText(folder), error.message()));
    }
    return {};
}

dvv::Result<std::shared_ptr<dvv::SplitFileSink>> openLogFile(const fs::path& folder,
                                                             FolderCreation creation,
                                                             dvv::SplitOpen open) {
    DVV_TRY_VOID(prepareLogFolder(folder, creation));
    return dvv::SplitFileSink::open(folder, std::string(LOG_FILE_PREFIX), LOG_FILE_BYTES, open);
}

void LogFileSwitch::write(const dvv::LogRecord& record, std::string_view /*line*/) {
    const std::scoped_lock lock(mutex_);
    if (file_) {
        file_->write(record, dvv::formatLogRecord(record, suffix_.load(std::memory_order_relaxed)));
    }
}

void LogFileSwitch::batchEnd() {
    const std::scoped_lock lock(mutex_);
    if (file_) {
        file_->batchEnd();
    }
}

void LogFileSwitch::flush() {
    const std::scoped_lock lock(mutex_);
    if (file_) {
        file_->flush();
    }
}

void LogFileSwitch::setSourceSuffix(dvv::SourceSuffix suffix) noexcept {
    suffix_.store(suffix, std::memory_order_relaxed);
}

std::shared_ptr<dvv::SplitFileSink> LogFileSwitch::replace(std::shared_ptr<dvv::SplitFileSink> file,
                                                           std::span<const LogEntry> backfill) {
    const std::scoped_lock lock(mutex_);
    if (file) {
        for (const LogEntry& entry : backfill) {
            const dvv::LogRecord record{.level = entry.level,
                                        .message = entry.message,
                                        .where = entry.where,
                                        .time = entry.time,
                                        .mark = entry.mark};
            file->write(record, entry.line);
        }
    }
    file_.swap(file);
    return file;
}

std::filesystem::path LogFileSwitch::currentFile() const {
    const std::scoped_lock lock(mutex_);
    return file_ ? file_->currentFile() : std::filesystem::path();
}

std::uintmax_t LogFileSwitch::totalBytes() const {
    const std::scoped_lock lock(mutex_);
    return file_ ? file_->totalBytes() : 0;
}

std::uint64_t LogFileSwitch::splits() const {
    const std::scoped_lock lock(mutex_);
    return file_ ? file_->splits() : 0;
}

}  // namespace rawreel
