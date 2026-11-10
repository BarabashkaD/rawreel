#include "core/log_file_switch.h"

#include <algorithm>
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
#include "dvv/media/types.h"

namespace rawreel {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view LOG_FILE_EXTENSION = ".log";
constexpr std::size_t DATE_DIGITS = 8;
constexpr std::size_t TIME_DIGITS = 6;
constexpr std::size_t MAX_SUFFIX_DIGITS = 9;

bool allDigits(std::string_view text) noexcept {
    return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

char foldCase(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// `text` starts with `head`, letters compared without case when `fold`.
bool startsWith(std::string_view text, std::string_view head, bool fold) noexcept {
    if (text.size() < head.size()) {
        return false;
    }
    return std::ranges::equal(text.substr(0, head.size()), head, [fold](char a, char b) {
        return fold ? foldCase(a) == foldCase(b) : a == b;
    });
}

}  // namespace

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

dvv::Result<std::shared_ptr<dvv::SplitFileSink>> openLogFile(const fs::path& folder,
                                                             FolderCreation creation,
                                                             bool append_newest) {
    DVV_TRY_VOID(prepareLogFolder(folder, creation));
    return dvv::SplitFileSink::open(folder, std::string(LOG_FILE_PREFIX), LOG_FILE_BYTES,
                                    append_newest);
}

bool isLogFileName(std::string_view name, bool case_insensitive) {
    // <prefix>-YYYYMMDD-HHMMSS[-N].log
    if (!startsWith(name, LOG_FILE_PREFIX, case_insensitive)) {
        return false;
    }
    std::string_view rest = name.substr(LOG_FILE_PREFIX.size());
    if (rest.size() < LOG_FILE_EXTENSION.size() ||
        !startsWith(rest.substr(rest.size() - LOG_FILE_EXTENSION.size()), LOG_FILE_EXTENSION,
                    case_insensitive)) {
        return false;
    }
    rest.remove_suffix(LOG_FILE_EXTENSION.size());
    constexpr std::size_t STAMP_CHARS = 1 + DATE_DIGITS + 1 + TIME_DIGITS;  // "-YYYYMMDD-HHMMSS"
    if (rest.size() < STAMP_CHARS || rest[0] != '-' || rest[1 + DATE_DIGITS] != '-' ||
        !allDigits(rest.substr(1, DATE_DIGITS)) ||
        !allDigits(rest.substr(2 + DATE_DIGITS, TIME_DIGITS))) {
        return false;
    }
    const std::string_view suffix = rest.substr(STAMP_CHARS);
    if (suffix.empty()) {
        return true;
    }
    const std::string_view number = suffix.substr(1);
    return suffix[0] == '-' && number.size() <= MAX_SUFFIX_DIGITS && allDigits(number) &&
           number[0] != '0';
}

void LogFileSwitch::write(const dvv::LogRecord& record, std::string_view /*line*/) {
    const std::scoped_lock lock(mutex_);
    if (file_) {
        file_->write(record,
                     dvv::formatLogRecord(record, with_source_.load(std::memory_order_relaxed)));
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

void LogFileSwitch::setWithSource(bool with_source) noexcept {
    with_source_.store(with_source, std::memory_order_relaxed);
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
