// The Log list as a virtual report wxListCtrl (Log pane and Settings design §4.1): the native
// ListView on Windows, wx's generic list elsewhere (built and tested on every OS). A batch sets the
// item count, then the selection, focus and selection mark from ListRows. Nothing here logs.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <wx/listctrl.h>
#if defined(__WXMSW__)
#include <wx/msw/wrapcctl.h>  // ListView_GetSelectionMark, ListView_SetSelectionMark
#endif

#include "gui/log_list.h"

namespace rawreel {

namespace {

// The column widths in DIP, in LOG_LIST_COLUMNS order.
constexpr std::array<int, 4> COLUMN_WIDTHS_DIP{100, 60, 520, 160};

constexpr long SELECTED = wxLIST_STATE_SELECTED;
constexpr long FOCUSED = wxLIST_STATE_FOCUSED;

// The control: its cells come from the mirror, bounds-checked.
class VirtualListCtrl final : public wxListCtrl {
public:
    VirtualListCtrl(wxWindow* parent, const LogMirror& mirror)
        : wxListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                     wxLC_REPORT | wxLC_VIRTUAL),
          mirror_(&mirror) {}

private:
    [[nodiscard]] wxString OnGetItemText(long item, long column) const override {
        if (item < 0 || column < 0) {
            return {};
        }
        return logCellText(*mirror_, static_cast<std::size_t>(item),
                           static_cast<std::size_t>(column));
    }

    const LogMirror* mirror_;
};

class ListCtrlLogList final : public LogList {
public:
    ListCtrlLogList(wxWindow* parent, const LogMirror& mirror)
        : LogList(parent, mirror), list_(new VirtualListCtrl(this, mirror)) {
        for (std::size_t column = 0; column < LOG_LIST_COLUMNS.size(); ++column) {
            const std::string_view title = LOG_LIST_COLUMNS[column];
            list_->AppendColumn(wxString::FromUTF8(title.data(), title.size()), wxLIST_FORMAT_LEFT,
                                FromDIP(COLUMN_WIDTHS_DIP[column]));
        }
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(list_, wxSizerFlags(1).Expand());
        SetSizer(sizer);
    }

    void rebuild() override {
        ListRows rows;
        rows.tail = true;
        applyBatch(rows);
    }

    void applyBatch(const ListRows& rows) override {
        const auto count = static_cast<long>(mirror().size());
        list_->SetItemCount(count);  // no row is deleted one by one
        if (count > 0) {
            list_->SetItemState(-1, 0, SELECTED | FOCUSED);
            if (rows.selected.size() == mirror().size()) {
                list_->SetItemState(-1, SELECTED, SELECTED);
            } else {
                for (const std::size_t row : rows.selected) {
                    list_->SetItemState(static_cast<long>(row), SELECTED, SELECTED);
                }
            }
            if (rows.focus) {
                list_->SetItemState(static_cast<long>(*rows.focus), FOCUSED, FOCUSED);
            }
#if defined(__WXMSW__)
            static_cast<void>(
                ListView_SetSelectionMark(static_cast<HWND>(list_->GetHWND()),
                                          rows.anchor ? static_cast<int>(*rows.anchor) : -1));
#endif
            if (rows.tail) {
                list_->EnsureVisible(count - 1);
            } else if (rows.top) {
                showAtTop(static_cast<long>(*rows.top));
            }
        }
        // The Windows control is told not to repaint on a new count (LVSICF_NOINVALIDATEALL),
        // while every row may hold another record now.
        list_->Refresh();
    }

    [[nodiscard]] ListPlace place() const override {
        ListPlace place;
        const long count = list_->GetItemCount();
        if (count == 0) {
            place.tail = true;
            return place;
        }
        place.selected = selectedSequences();
        if (const long focus = list_->GetNextItem(-1, wxLIST_NEXT_ALL, FOCUSED); focus >= 0) {
            place.focus = sequenceAt(static_cast<std::size_t>(focus));
        }
#if defined(__WXMSW__)
        if (const int mark = ListView_GetSelectionMark(static_cast<HWND>(list_->GetHWND()));
            mark >= 0) {
            place.anchor = sequenceAt(static_cast<std::size_t>(mark));
        }
#endif
        const long top = std::max(list_->GetTopItem(), 0L);
        place.top = sequenceAt(static_cast<std::size_t>(top));
        place.tail = top + std::max(list_->GetCountPerPage(), 1) >= count;
        return place;
    }

    [[nodiscard]] std::vector<std::uint64_t> selectedSequences() const override {
        std::vector<std::uint64_t> sequences;
        for (long item = list_->GetNextItem(-1, wxLIST_NEXT_ALL, SELECTED); item >= 0;
             item = list_->GetNextItem(item, wxLIST_NEXT_ALL, SELECTED)) {
            if (const std::optional<std::uint64_t> sequence =
                    sequenceAt(static_cast<std::size_t>(item))) {
                sequences.push_back(*sequence);
            }
        }
        return sequences;
    }

    void selectRows(std::span<const std::size_t> rows) override {
        const auto count = static_cast<std::size_t>(list_->GetItemCount());
        if (count == 0) {
            return;
        }
        list_->SetItemState(-1, 0, SELECTED);
        for (const std::size_t row : rows) {
            if (row < count) {
                list_->SetItemState(static_cast<long>(row), SELECTED, SELECTED);
            }
        }
    }

    void scrollToTop(std::size_t row) override {
        if (row < static_cast<std::size_t>(list_->GetItemCount())) {
            showAtTop(static_cast<long>(row));
        }
    }

    void selectAll() override {
        if (list_->GetItemCount() > 0) {
            list_->SetItemState(-1, SELECTED, SELECTED);
        }
    }

    [[nodiscard]] wxWindow* control() const override { return list_; }

    [[nodiscard]] std::size_t rowCount() const override {
        return static_cast<std::size_t>(list_->GetItemCount());
    }

    [[nodiscard]] wxString cellText(std::size_t row, std::size_t column) const override {
        if (row >= rowCount() || column >= LOG_LIST_COLUMNS.size()) {
            return {};
        }
        return list_->GetItemText(static_cast<long>(row), static_cast<int>(column));
    }

    [[nodiscard]] LogListKind kind() const noexcept override { return LogListKind::ListCtrl; }

private:
    // `row` at the top: EnsureVisible lays the rows out first (the generic control does that
    // lazily), then ScrollList moves by whole rows (both controls round to them).
    void showAtTop(long row) {
        list_->EnsureVisible(row);
        const long top = list_->GetTopItem();
        if (top == row) {
            return;
        }
        wxRect rect;
        if (list_->GetItemRect(row, rect) && rect.height > 0) {
            list_->ScrollList(0, static_cast<int>((row - top) * rect.height));
        }
    }

    VirtualListCtrl* list_;  // a child window: wx owns it
};

}  // namespace

LogList* createListCtrlLogList(wxWindow* parent, const LogMirror& mirror) {
    return new ListCtrlLogList(parent, mirror);
}

}  // namespace rawreel
