#pragma once

// The Log pane (design M3 D18; Log pane and Settings design §3.2, §4.1, §4.3): the records of this
// run, newest last, at most 5000, in a virtual list. The pane keeps the GUI-thread mirror of the
// LogStore; MainFrame feeds it one batch per posted refresh (no timer), and the list reads only the
// mirror, so painting takes no lock. The list's context menu copies and saves its records.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <cstddef>
#include <deque>
#include <utility>
#include <vector>

#include "core/log_mirror.h"
#include "core/log_store.h"
#include "dvv/base/log.h"
#include "dvv/base/move_only_function.h"
#include "gui/log_list.h"

namespace rawreel {

class LogPanel final : public wxPanel {
public:
    // The list shows the Source column only with SourceSuffix::With (log.source, addendum §4).
    LogPanel(wxWindow* parent, dvv::SourceSuffix suffix, LogListKind kind = nativeLogListKind());
    LogPanel(const LogPanel&) = delete;
    LogPanel& operator=(const LogPanel&) = delete;
    LogPanel(LogPanel&&) = delete;
    LogPanel& operator=(LogPanel&&) = delete;
    // Destroys the list before the mirror it reads.
    ~LogPanel() override;

    // One batch from LogStore::take(mirror().next()). While `shown`, the list's place is taken
    // before the mirror changes and restored after it; hidden, only the mirror changes (showing
    // the pane rebuilds the list). Never logs (spec §3.3).
    void refresh(LogBatch batch, bool shown);
    [[nodiscard]] const LogMirror& mirror() const noexcept { return mirror_; }
    [[nodiscard]] LogList& list() const noexcept { return *list_; }
    // Selftests: replaces the list with one of `kind`, rebuilt from the mirror.
    void recreateList(LogListKind kind);

    // Spec §4.3: the records of the selected rows, oldest first, copied out of the mirror now (a
    // snapshot that later batches do not change).
    [[nodiscard]] std::vector<LogEntry> selectedRecords() const;
    // Every record the mirror holds, copied out now.
    [[nodiscard]] std::vector<LogEntry> allRecords() const;
    [[nodiscard]] bool hasSelection() const;
    // The context menu's Save Selected Lines... (true) and Save Log... (false).
    void setSaveHandler(dvv::MoveOnlyFunction<void(bool selected)> save) {
        save_ = std::move(save);
    }
    // Spec §4.3, log level design §2.2: Copy, Select All, a separator, Level ▸ (the five levels as
    // radio items, the active one checked), a separator, Save Selected Lines..., Save Log....
    // The right-click; a selftest calls it too (the menu then goes to the popupMenu hook, unshown).
    void showContextMenu();
    // The menu's Level ▸ items (log level design §2.2): the active level for this run only
    // (App::setActiveLevel(level, LevelSource::Menu)); nothing is saved.
    void setRunLevel(dvv::LogLevel level);

    // Selftests only (a diagnostic text): one shown refresh, the list's place taken before the
    // batch and what the batch changed.
    struct RefreshRecord {
        ListPlace before;
        MirrorChange change;
    };
    static constexpr std::size_t REFRESH_RECORDS_KEPT = 8;
    // Selftests only: every refresh() so far, and the last REFRESH_RECORDS_KEPT shown ones, oldest
    // first.
    [[nodiscard]] std::size_t refreshCount() const noexcept { return refreshes_; }
    [[nodiscard]] const std::deque<RefreshRecord>& refreshRecords() const noexcept {
        return refresh_records_;
    }

private:
    LogMirror mirror_;
    std::size_t refreshes_ = 0;
    std::deque<RefreshRecord> refresh_records_;
    LogList* list_;  // a child window: wx owns it
    dvv::SourceSuffix suffix_;
    dvv::MoveOnlyFunction<void(bool selected)> save_;
};

}  // namespace rawreel
