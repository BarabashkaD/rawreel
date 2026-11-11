#pragma once

// Open Recent: entries, the list and the menu labels (spec §3, §4, §6). Wx-free.
// An entry is the text the user opened: a file path as UTF-8 text, or a URI as typed. A path that
// contains '?' is written as its file: URI, so the query cannot be mistaken for options. Typed
// text with a scheme is kept as it is; a bare relative path is joined to the working folder (a
// plain join: '.' and '..' are not resolved), its query appended as typed. An entry is at most
// 4096 bytes of strict UTF-8 without control characters (U+0000..U+001F, U+007F).
// The list keeps the newest entry first, exact text only; a repeat moves to the top; a smaller
// capacity cuts the oldest entries for good, a larger one brings nothing back.
// The menu label is, per OS: macOS the short text, Windows the number and the full text, Linux
// the number and the short text. The short text of a file entry is its file name (never cut),
// of any other entry (and everywhere on Windows) its full text cut in the middle to 60 code
// points with "...". Entries with equal short texts show their full text, uncut. '&' is doubled;
// Windows numbers 1..9 as "&1 ", 10 as "1&0 "; Linux numbers as "&1. " .. "&10. ".
// The file check after a failed open: only a file: entry (a bare path is one) is checked; a
// missing path is Missing, a directory or an unreadable file NotAccessible.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/session_types.h"
#include "core/stream_name.h"

namespace rawreel {

inline constexpr std::size_t RECENT_DEFAULT_SIZE = 5;
inline constexpr std::size_t RECENT_MIN_SIZE = 1;
inline constexpr std::size_t RECENT_MAX_SIZE = 10;
inline constexpr std::size_t RECENT_ENTRY_MAX_BYTES = 4096;
inline constexpr std::size_t RECENT_LABEL_MAX = 60;

[[nodiscard]] std::string recentEntryOfPath(const std::filesystem::path& path);
[[nodiscard]] std::string recentEntryOfText(std::string_view text,
                                            const std::filesystem::path& working_folder);
// Why the entry cannot be kept; nullopt when it is valid. The reason texts are log texts.
[[nodiscard]] std::optional<std::string_view> recentEntryProblem(std::string_view entry);

class RecentList {
public:
    explicit RecentList(std::size_t capacity = RECENT_DEFAULT_SIZE,
                        std::vector<std::string> entries = {});
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] const std::vector<std::string>& entries() const noexcept { return entries_; }
    [[nodiscard]] bool contains(std::string_view entry) const;
    // On top; true when the list changed (false: the entry was already on top).
    bool add(std::string entry);
    bool remove(std::string_view entry);
    // True when entries were cut.
    bool setCapacity(std::size_t capacity);

private:
    std::size_t capacity_;
    std::vector<std::string> entries_;
};

struct RecentMenuItem {
    std::string label;
    std::string help;
    bool operator==(const RecentMenuItem&) const = default;
};
[[nodiscard]] std::vector<RecentMenuItem> recentMenuItems(std::span<const std::string> entries,
                                                          TitleStyle style);

// gui.recentSize and gui.recent of a config text (spec §8). Never an error: a bad value gives a
// warning (without the "config '<path>': " prefix, in rule order) and the default; the size is
// 1..10 and the entries are valid, without duplicates, at most `size`.
struct RecentConfig {
    std::size_t size = RECENT_DEFAULT_SIZE;
    std::vector<std::string> entries;
    std::vector<std::string> warnings;
};
[[nodiscard]] RecentConfig parseRecent(std::string_view text);

enum class RecentFileState : std::uint8_t { NotChecked, Missing, NotAccessible, Readable };
[[nodiscard]] RecentFileState recentFileState(std::string_view entry);

// The entry of the open in flight, recorded once its stream shows (spec §5); one at a time.
class RecentPending {
public:
    // An open started: its entry (none: an open that records nothing) and dvv::formatUri of its
    // parsed text. Replaces the earlier pending entry; with no entry, clears it.
    void opened(std::optional<std::string> entry, std::string requested);
    // The entry to record when `streams` is one stream whose requestedUri equals the pending one
    // (the pending entry is cleared then); otherwise none, and the pending entry stays.
    [[nodiscard]] std::optional<std::string> streamsChanged(std::span<const StreamInfo> streams);
    // The entry, moved out for the queued Format dialog, when `requested` (formatUri of the
    // formatNeeded request) equals the pending one; otherwise none, and it stays.
    [[nodiscard]] std::optional<std::string> formatNeeded(std::string_view requested);
    void closed() noexcept;  // Close stream
    // The pending entry's text: an error keeps it (the removal check, the Format... offer).
    [[nodiscard]] std::optional<std::string> entry() const;

private:
    std::optional<std::string> entry_;
    std::string requested_;
};

}  // namespace rawreel
