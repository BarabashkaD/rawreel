// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/log_panel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <wx/dataview.h>

#include "core/log_level.h"
#include "core/log_text.h"
#include "dvv/base/log.h"
#include "gui/app.h"
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
        case 3: return wxString::FromUTF8(logSourceText(entry));
        default: return {};
    }
}

LogList* LogList::create(wxWindow* parent, const LogMirror& mirror, LogListKind kind,
                         bool with_source) {
    return kind == LogListKind::ListCtrl ? createListCtrlLogList(parent, mirror, with_source)
                                         : createDataViewLogList(parent, mirror, with_source);
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
// Level ▸ (log level design §2.2): one id per level, in MENU_LEVELS order.
constexpr int CONTEXT_LEVEL_FIRST_ID = wxID_HIGHEST + 74;
constexpr std::array<dvv::LogLevel, 5> MENU_LEVELS{dvv::LogLevel::Error, dvv::LogLevel::Warn,
                                                   dvv::LogLevel::Info, dvv::LogLevel::Debug,
                                                   dvv::LogLevel::Trace};

}  // namespace

LogPanel::LogPanel(wxWindow* parent, bool with_source, LogListKind kind)
    : wxPanel(parent),
      list_(LogList::create(this, mirror_, kind, with_source)),
      with_source_(with_source) {
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
    list_ = LogList::create(this, mirror_, kind, with_source_);
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
    // Log level design §2.2: the five levels as radio items, the active one checked.
    auto* levels = new wxMenu;  // NOLINT(cppcoreguidelines-owning-memory): the menu owns it
    const dvv::LogLevel active = wxGetApp().activeLevel();
    for (std::size_t i = 0; i < MENU_LEVELS.size(); ++i) {
        const std::string_view name = levelShownName(MENU_LEVELS[i]);
        const int id = CONTEXT_LEVEL_FIRST_ID + static_cast<int>(i);
        levels->AppendRadioItem(id, wxString::FromUTF8(name.data(), name.size()));
        levels->Check(id, MENU_LEVELS[i] == active);
    }
    menu.AppendSubMenu(levels, "Level");
    menu.AppendSeparator();
    menu.Append(CONTEXT_SAVE_SELECTED_ID, "Save Selected Lines...");
    menu.Append(CONTEXT_SAVE_LOG_ID, "Save Log...");
    const bool selection = hasSelection();
    menu.Enable(CONTEXT_COPY_ID, selection);
    menu.Enable(CONTEXT_SELECT_ALL_ID, list_->rowCount() > 0);
    menu.Enable(CONTEXT_SAVE_SELECTED_ID, selection);
    menu.Enable(CONTEXT_SAVE_LOG_ID, mirror_.size() > 0);
    // The chosen item acts on this list, wherever the keyboard focus is.
    const int chosen = showLogMenu(*this, menu);
    if (chosen >= CONTEXT_LEVEL_FIRST_ID &&
        chosen < CONTEXT_LEVEL_FIRST_ID + static_cast<int>(MENU_LEVELS.size())) {
        setRunLevel(MENU_LEVELS[static_cast<std::size_t>(chosen - CONTEXT_LEVEL_FIRST_ID)]);
        return;
    }
    switch (chosen) {
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

void LogPanel::setRunLevel(dvv::LogLevel level) {
    wxGetApp().setActiveLevel(level, LevelSource::Menu);  // for this run only: nothing is saved
}

}  // namespace rawreel
