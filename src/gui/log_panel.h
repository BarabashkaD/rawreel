#pragma once

// The Log pane (design M3 D18; Log pane and Settings design §3.2, §4.1, §4.3): the records of this
// run, newest last, at most 5000, in a virtual list. The pane keeps the GUI-thread mirror of the
// LogStore; MainFrame feeds it one batch per posted refresh (no timer), and the list reads only the
// mirror, so painting takes no lock. The list's context menu copies and saves its records.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <functional>
#include <utility>
#include <vector>

#include "core/log_mirror.h"
#include "core/log_store.h"
#include "gui/log_list.h"

namespace rawreel {

class LogPanel final : public wxPanel {
public:
    explicit LogPanel(wxWindow* parent, LogListKind kind = nativeLogListKind());
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
    void setSaveHandler(std::function<void(bool selected)> save) { save_ = std::move(save); }

private:
    // Spec §4.3: Copy, Select All, a separator, Save Selected Lines..., Save Log....
    void showContextMenu();

    LogMirror mirror_;
    LogList* list_;  // a child window: wx owns it
    std::function<void(bool selected)> save_;
};

}  // namespace rawreel
