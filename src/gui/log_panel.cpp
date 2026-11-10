// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/log_panel.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

#include <wx/dataview.h>

#include "core/log_text.h"
#include "dvv/base/log.h"
#include "gui/selftest.h"

namespace rawreel {

// --- LogList: what the two adapters share ---

LogListKind nativeLogListKind() {
#if defined(__WXMSW__)
    return LogListKind::ListCtrl;
#else
    return LogListKind::DataView;
#endif
}

wxString logCellText(const LogMirror& mirror, std::size_t row, std::size_t column) {
    if (row >= mirror.size()) {
        return {};
    }
    const LogEntry& entry = mirror.at(row);
    switch (column) {
        case 0: return wxString::FromUTF8(logRowTime(localTime(entry.time)));
        case 1: {
            const std::string_view name = dvv::logLevelName(entry.level);
            return wxString::FromUTF8(name.data(), name.size());
        }
        case 2: return wxString::FromUTF8(logRowMessage(entry.message));
        case 3: return wxString::FromUTF8(entry.source);
        default: return {};
    }
}

LogList* LogList::create(wxWindow* parent, const LogMirror& mirror, LogListKind kind) {
    return kind == LogListKind::ListCtrl ? createListCtrlLogList(parent, mirror)
                                         : createDataViewLogList(parent, mirror);
}

LogList::LogList(wxWindow* parent, const LogMirror& mirror) : wxPanel(parent), mirror_(&mirror) {}

std::optional<std::uint64_t> LogList::sequenceAt(std::size_t row) const {
    if (row >= mirror_->size()) {
        return std::nullopt;
    }
    return mirror_->at(row).sequence;
}

// --- LogPanel ---

namespace {

// The context menu's own ids (spec §4.3): not the stock Edit ids, which wxOSX would send down the
// responder chain to whatever control has the focus.
constexpr int CONTEXT_COPY_ID = wxID_HIGHEST + 70;
constexpr int CONTEXT_SELECT_ALL_ID = wxID_HIGHEST + 71;
constexpr int CONTEXT_SAVE_SELECTED_ID = wxID_HIGHEST + 72;
constexpr int CONTEXT_SAVE_LOG_ID = wxID_HIGHEST + 73;

}  // namespace

LogPanel::LogPanel(wxWindow* parent, LogListKind kind)
    : wxPanel(parent), list_(LogList::create(this, mirror_, kind)) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(list_, wxSizerFlags(1).Expand());
    SetSizer(sizer);
    // Spec §4.3: a right-click (or Ctrl-click on macOS) in the list. Both events climb from the
    // control; each adapter uses the one its control sends (the data view sends its own on every
    // OS), so the menu opens once.
    Bind(wxEVT_CONTEXT_MENU, [this](wxContextMenuEvent& /*event*/) {
        if (list_->kind() == LogListKind::ListCtrl) {
            showContextMenu();
        }
    });
    Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, [this](wxDataViewEvent& /*event*/) {
        if (list_->kind() == LogListKind::DataView) {
            showContextMenu();
        }
    });
}

LogPanel::~LogPanel() {
    DestroyChildren();  // the list reads mirror_, a member that goes before ~wxWindow's children
}

void LogPanel::refresh(LogBatch batch, bool shown) {
    std::optional<ListPlace> before;
    if (shown) {
        before = list_->place();
    }
    const MirrorChange change = mirror_.apply(std::move(batch));
    if (shown && (change.reset || change.dropped > 0 || change.appended > 0)) {
        list_->applyBatch(rowsAfterBatch(*before, mirror_));
    }
}

void LogPanel::recreateList(LogListKind kind) {
    GetSizer()->Clear(true);  // deletes the old list
    list_ = LogList::create(this, mirror_, kind);
    GetSizer()->Add(list_, wxSizerFlags(1).Expand());
    Layout();
    list_->rebuild();
}

std::vector<LogEntry> LogPanel::selectedRecords() const {
    const std::vector<std::uint64_t> selected = list_->selectedSequences();  // ascending
    std::vector<LogEntry> records;
    records.reserve(selected.size());
    // One mirror range per run of consecutive sequences.
    for (std::size_t first = 0; first < selected.size();) {
        std::size_t last = first;
        while (last + 1 < selected.size() && selected[last + 1] == selected[last] + 1) {
            ++last;
        }
        std::vector<LogEntry> run = mirror_.range(selected[first], selected[last]);
        records.insert(records.end(), std::make_move_iterator(run.begin()),
                       std::make_move_iterator(run.end()));
        first = last + 1;
    }
    return records;
}

std::vector<LogEntry> LogPanel::allRecords() const {
    return {mirror_.entries().begin(), mirror_.entries().end()};
}

bool LogPanel::hasSelection() const {
    return !list_->selectedSequences().empty();
}

void LogPanel::showContextMenu() {
    wxMenu menu;
    menu.Append(CONTEXT_COPY_ID, "Copy");
    menu.Append(CONTEXT_SELECT_ALL_ID, "Select All");
    menu.AppendSeparator();
    menu.Append(CONTEXT_SAVE_SELECTED_ID, "Save Selected Lines...");
    menu.Append(CONTEXT_SAVE_LOG_ID, "Save Log...");
    const bool selection = hasSelection();
    menu.Enable(CONTEXT_COPY_ID, selection);
    menu.Enable(CONTEXT_SELECT_ALL_ID, list_->rowCount() > 0);
    menu.Enable(CONTEXT_SAVE_SELECTED_ID, selection);
    menu.Enable(CONTEXT_SAVE_LOG_ID, mirror_.size() > 0);
    // The chosen item acts on this list, wherever the keyboard focus is.
    switch (GetPopupMenuSelectionFromUser(menu)) {
        case CONTEXT_COPY_ID: setClipboardText(logRecordsText(selectedRecords())); break;
        case CONTEXT_SELECT_ALL_ID: list_->selectAll(); break;
        case CONTEXT_SAVE_SELECTED_ID:
            if (save_) {
                save_(true);
            }
            break;
        case CONTEXT_SAVE_LOG_ID:
            if (save_) {
                save_(false);
            }
            break;
        default: break;  // dismissed
    }
}

}  // namespace rawreel
