#include "core/recent.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/drop.h"
#include "core/open_text.h"
#include "dvv/base/error.h"
#include "dvv/base/json.h"
#include "dvv/base/uri.h"
#include "dvv/media/types.h"

namespace rawreel {
namespace {

constexpr std::string_view ELLIPSIS = "...";

// The length of the strict UTF-8 sequence at the start of `text`; 0 when it is not valid (a bad
// lead or continuation byte, an overlong form, a surrogate, a code point over U+10FFFF).
std::size_t utf8SequenceLength(std::string_view text) {
    const auto byte = [&text](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const auto continuation = [&](std::size_t i) { return (byte(i) & 0xC0U) == 0x80U; };
    const unsigned char lead = byte(0);
    if (lead < 0x80U) {
        return 1;
    }
    if (lead >= 0xC2U && lead <= 0xDFU) {
        return text.size() >= 2 && continuation(1) ? 2 : 0;
    }
    if (lead >= 0xE0U && lead <= 0xEFU) {
        if (text.size() < 3 || !continuation(1) || !continuation(2)) {
            return 0;
        }
        if (lead == 0xE0U && byte(1) < 0xA0U) {
            return 0;  // overlong
        }
        if (lead == 0xEDU && byte(1) >= 0xA0U) {
            return 0;  // surrogate
        }
        return 3;
    }
    if (lead >= 0xF0U && lead <= 0xF4U) {
        if (text.size() < 4 || !continuation(1) || !continuation(2) || !continuation(3)) {
            return 0;
        }
        if (lead == 0xF0U && byte(1) < 0x90U) {
            return 0;  // overlong
        }
        if (lead == 0xF4U && byte(1) >= 0x90U) {
            return 0;  // over U+10FFFF
        }
        return 4;
    }
    return 0;
}

// The starts of the code points of valid UTF-8 text, plus the end.
std::vector<std::size_t> codePointStarts(std::string_view text) {
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < text.size();) {
        starts.push_back(i);
        const std::size_t length = utf8SequenceLength(text.substr(i));
        i += length == 0 ? 1 : length;
    }
    starts.push_back(text.size());
    return starts;
}

// Cut in the middle to RECENT_LABEL_MAX code points: left keep - keep / 2, "...", right keep / 2.
std::string cutMiddle(std::string_view text) {
    const std::vector<std::size_t> starts = codePointStarts(text);
    const std::size_t count = starts.size() - 1;
    if (count <= RECENT_LABEL_MAX) {
        return std::string(text);
    }
    const std::size_t keep = RECENT_LABEL_MAX - ELLIPSIS.size();
    const std::size_t right = keep / 2;
    const std::size_t left = keep - right;
    std::string out(text.substr(0, starts[left]));
    out += ELLIPSIS;
    out += text.substr(starts[count - right]);
    return out;
}

std::string doubleAmpersands(std::string_view text) {
    std::string out;
    for (const char c : text) {
        out += c;
        if (c == '&') {
            out += '&';
        }
    }
    return out;
}

// The short text: a file entry's name on macOS and Linux, else the full text cut in the middle.
std::string shortText(std::string_view entry, TitleStyle style) {
    if (style != TitleStyle::Windows) {
        const dvv::Result<dvv::Uri> parsed = dvv::parseUri(entry);
        if (parsed && parsed->scheme == "file") {
            std::string name = streamName(entry);
            if (!name.empty()) {
                return name;
            }
        }
    }
    return cutMiddle(entry);
}

std::string prefixOf(std::size_t number, TitleStyle style) {
    switch (style) {
        case TitleStyle::MacOs: return {};
        case TitleStyle::Windows:
            return number == RECENT_MAX_SIZE ? "1&0 " : "&" + std::to_string(number) + " ";
        case TitleStyle::Linux: return "&" + std::to_string(number) + ". ";
    }
    return {};
}

}  // namespace

std::string recentEntryOfPath(const std::filesystem::path& path) {
    std::string text = dvv::media::utf8FromPath(path);
    if (text.find('?') != std::string::npos) {
        return fileUri(path);
    }
    return text;
}

std::string recentEntryOfText(std::string_view text, const std::filesystem::path& working_folder) {
    if (hasScheme(text) || working_folder.empty()) {
        return std::string(text);
    }
    const std::size_t query = text.find('?');
    const std::string_view part = text.substr(0, query);
    if (part.empty()) {
        return std::string(text);
    }
    // A plain join: '.' and '..' stay; an absolute part replaces the folder.
    std::string joined = dvv::media::utf8FromPath(working_folder / dvv::media::pathFromUtf8(part));
    if (query != std::string_view::npos) {
        joined += text.substr(query);
    }
    return joined;
}

std::optional<std::string_view> recentEntryProblem(std::string_view entry) {
    if (entry.size() > RECENT_ENTRY_MAX_BYTES) {
        return "longer than 4096 bytes";
    }
    for (std::size_t i = 0; i < entry.size();) {
        const std::size_t length = utf8SequenceLength(entry.substr(i));
        if (length == 0) {
            return "not valid UTF-8";
        }
        i += length;
    }
    if (std::ranges::any_of(entry, [](char c) {
            const auto byte = static_cast<unsigned char>(c);
            return byte < 0x20U || byte == 0x7FU;
        })) {
        return "a control character";
    }
    return std::nullopt;
}

RecentList::RecentList(std::size_t capacity, std::vector<std::string> entries)
    : capacity_(capacity), entries_(std::move(entries)) {
    if (entries_.size() > capacity_) {
        entries_.resize(capacity_);
    }
}

bool RecentList::contains(std::string_view entry) const {
    return std::ranges::find(entries_, entry) != entries_.end();
}

bool RecentList::add(std::string entry) {
    if (!entries_.empty() && entries_.front() == entry) {
        return false;
    }
    std::erase(entries_, entry);
    entries_.insert(entries_.begin(), std::move(entry));
    if (entries_.size() > capacity_) {
        entries_.resize(capacity_);
    }
    return true;
}

bool RecentList::remove(std::string_view entry) {
    return std::erase_if(entries_, [entry](const std::string& e) { return e == entry; }) > 0;
}

bool RecentList::setCapacity(std::size_t capacity) {
    capacity_ = capacity;
    if (entries_.size() > capacity_) {
        entries_.resize(capacity_);
        return true;
    }
    return false;
}

std::vector<RecentMenuItem> recentMenuItems(std::span<const std::string> entries,
                                            TitleStyle style) {
    std::vector<std::string> shorts;
    shorts.reserve(entries.size());
    for (const std::string& entry : entries) {
        shorts.push_back(shortText(entry, style));
    }
    std::vector<RecentMenuItem> items;
    items.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const bool same = std::ranges::count(shorts, shorts[i]) > 1;
        items.push_back(
            {.label = prefixOf(i + 1, style) + doubleAmpersands(same ? entries[i] : shorts[i]),
             .help = entries[i]});
    }
    return items;
}

RecentFileState recentFileState(std::string_view entry) {
    const dvv::Result<dvv::Uri> parsed = dvv::parseUri(entry);
    if (!parsed || parsed->scheme != "file") {
        return RecentFileState::NotChecked;
    }
    const std::filesystem::path path = dvv::media::pathFromUtf8(parsed->path);
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(path, error);
    if (status.type() == std::filesystem::file_type::not_found) {
        return RecentFileState::Missing;
    }
    if (error || !std::filesystem::is_regular_file(status)) {
        return RecentFileState::NotAccessible;
    }
    if (!std::ifstream(path, std::ios::binary)) {
        return RecentFileState::NotAccessible;
    }
    return RecentFileState::Readable;
}

namespace {

std::string plural(std::size_t count, std::string_view one_form, std::string_view many_form) {
    return count == 1 ? std::format("1 {}", one_form) : std::format("{} {}", count, many_form);
}

// The size when `value` is a whole number from RECENT_MIN_SIZE to RECENT_MAX_SIZE.
std::optional<std::size_t> sizeOf(const dvv::json::Value& value) {
    if (!value.is_number_integer()) {
        return std::nullopt;
    }
    std::uintmax_t number = 0;
    if (value.is_number_unsigned()) {
        number = value.get<std::uint64_t>();
    } else if (const auto signed_number = value.get<std::int64_t>(); signed_number >= 0) {
        number = static_cast<std::uintmax_t>(signed_number);
    } else {
        return std::nullopt;
    }
    if (number < RECENT_MIN_SIZE || number > RECENT_MAX_SIZE) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(number);
}

}  // namespace

RecentConfig parseRecent(std::string_view text) {
    RecentConfig result;
    const dvv::Result<dvv::json::Value> root = dvv::json::parse(text);
    if (!root || !root->is_object()) {
        return result;
    }
    const auto gui = root->find("gui");
    if (gui == root->end() || !gui->is_object()) {
        return result;
    }
    if (const auto size = gui->find("recentSize"); size != gui->end()) {
        if (const std::optional<std::size_t> valid = sizeOf(*size)) {
            result.size = *valid;
        } else {
            result.warnings.push_back(std::format(
                "gui.recentSize: expected a whole number from {} to {}, not {}; using {}",
                RECENT_MIN_SIZE, RECENT_MAX_SIZE, dvv::json::dump(*size), RECENT_DEFAULT_SIZE));
        }
    }
    const auto recent = gui->find("recent");
    if (recent == gui->end()) {
        return result;
    }
    if (!recent->is_array()) {
        result.warnings.push_back(
            std::format("gui.recent: expected an array of strings, not {}; the list is empty",
                        dvv::json::dump(*recent)));
        return result;
    }
    std::size_t not_strings = 0;
    std::size_t empty = 0;
    std::size_t invalid = 0;
    std::size_t duplicates = 0;
    std::vector<std::string> kept;
    std::unordered_set<std::string> seen;
    for (const dvv::json::Value& item : *recent) {
        if (!item.is_string()) {
            ++not_strings;
            continue;
        }
        std::string entry = item.get<std::string>();
        if (entry.empty()) {
            ++empty;
        } else if (recentEntryProblem(entry)) {
            ++invalid;
        } else if (!seen.insert(entry).second) {
            ++duplicates;
        } else {
            kept.push_back(std::move(entry));
        }
    }
    std::vector<std::string> parts;
    if (not_strings > 0) {
        parts.push_back(
            plural(not_strings, "entry that is not a string", "entries that are not strings"));
    }
    if (empty > 0) {
        parts.push_back(std::format("{} empty", empty));
    }
    if (invalid > 0) {
        parts.push_back(std::format("{} invalid", invalid));
    }
    if (duplicates > 0) {
        parts.push_back(plural(duplicates, "duplicate", "duplicates"));
    }
    if (!parts.empty()) {
        std::string joined;
        for (const std::string& part : parts) {
            joined += joined.empty() ? part : ", " + part;
        }
        result.warnings.push_back("gui.recent: ignored " + joined);
    }
    if (kept.size() > result.size) {
        result.warnings.push_back(
            std::format("gui.recent: {} entries, more than gui.recentSize {}; the first {} kept",
                        kept.size(), result.size, result.size));
        kept.resize(result.size);
    }
    result.entries = std::move(kept);
    return result;
}

void RecentPending::opened(std::optional<std::string> entry, std::string requested) {
    entry_ = std::move(entry);
    requested_ = entry_ ? std::move(requested) : std::string();
}

std::optional<std::string> RecentPending::streamsChanged(std::span<const StreamInfo> streams) {
    if (!entry_ || streams.size() != 1 || streams[0].requestedUri != requested_) {
        return std::nullopt;
    }
    return std::exchange(entry_, std::nullopt);
}

std::optional<std::string> RecentPending::formatNeeded(std::string_view requested) {
    if (!entry_ || requested != requested_) {
        return std::nullopt;
    }
    return std::exchange(entry_, std::nullopt);
}

void RecentPending::closed() noexcept {
    entry_.reset();
}

std::optional<std::string> RecentPending::entry() const {
    return entry_;
}

}  // namespace rawreel
