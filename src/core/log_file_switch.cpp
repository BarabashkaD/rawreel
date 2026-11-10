#include "core/log_file_switch.h"

#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <source_location>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

#include "dvv/base/error.h"
#include "dvv/base/log.h"
#include "dvv/base/paths.h"
#include "dvv/media/types.h"

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
                                                         dvv::media::utf8FromPath(parent)));
    }
    if (!fs::create_directory(folder, error) && error) {
        return dvv::makeError(
            dvv::Errc::Io,
            std::format("cannot create {}: {}", dvv::media::utf8FromPath(folder), error.message()));
    }
    return {};
}

dvv::Result<std::shared_ptr<dvv::RotatingFileSink>> openLogFile(const fs::path& folder,
                                                                FolderCreation creation) {
    DVV_TRY_VOID(prepareLogFolder(folder, creation));
    return dvv::RotatingFileSink::open(folder / LOG_FILE_NAME, LOG_FILE_BYTES, LOG_FILE_BACKUPS);
}

void LogFileSwitch::write(const dvv::LogRecord& record, std::string_view line) {
    const std::scoped_lock lock(mutex_);
    if (file_) {
        file_->write(record, line);
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

std::shared_ptr<dvv::ILogSink> LogFileSwitch::replace(std::shared_ptr<dvv::ILogSink> file,
                                                      std::span<const LogEntry> backfill) {
    const std::scoped_lock lock(mutex_);
    if (file) {
        for (const LogEntry& entry : backfill) {
            const dvv::LogRecord record{.level = entry.level,
                                        .message = entry.message,
                                        .where = std::source_location::current(),
                                        .time = entry.time};
            file->write(record, entry.line);
        }
    }
    file_.swap(file);
    return file;
}

}  // namespace rawreel
