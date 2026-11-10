// The Log list as a wxDataViewCtrl with a virtual list model (Log pane and Settings design §4.1):
// native on macOS and Linux, built on every OS. A batch resets the model to the mirror's size, then
// sets the selection, the focus (SetCurrentItem) and the top row (EnsureVisible) from ListRows.
// Nothing here logs.

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

#include <wx/dataview.h>

#include "gui/log_list.h"

namespace rawreel {

namespace {

// The column widths in DIP, in LOG_LIST_COLUMNS order.
constexpr std::array<int, 4> COLUMN_WIDTHS_DIP{100, 60, 520, 160};

// The model: its cells come from the mirror, bounds-checked; read-only.
class LogListModel final : public wxDataViewVirtualListModel {
public:
    explicit LogListModel(const LogMirror& mirror) : mirror_(&mirror) {}

    void GetValueByRow(wxVariant& variant, unsigned row, unsigned column) const override {
        variant = logCellText(*mirror_, row, column);
    }
    bool SetValueByRow(const wxVariant& /*variant*/, unsigned /*row*/,
                       unsigned /*column*/) override {
        return false;
    }

private:
    const LogMirror* mirror_;
};

class DataViewLogList final : public LogList {
public:
    DataViewLogList(wxWindow* parent, const LogMirror& mirror)
        : LogList(parent, mirror),
          model_(new LogListModel(mirror)),
          view_(
              new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_MULTIPLE)) {
        view_->AssociateModel(model_.get());
        for (std::size_t column = 0; column < LOG_LIST_COLUMNS.size(); ++column) {
            const std::string_view title = LOG_LIST_COLUMNS[column];
            view_->AppendTextColumn(wxString::FromUTF8(title.data(), title.size()),
                                    static_cast<unsigned>(column), wxDATAVIEW_CELL_INERT,
                                    FromDIP(COLUMN_WIDTHS_DIP[column]));
        }
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(view_, wxSizerFlags(1).Expand());
        SetSizer(sizer);
    }

    void rebuild() override {
        ListRows rows;
        rows.tail = true;
        applyBatch(rows);
    }

    void applyBatch(const ListRows& rows) override {
        const auto count = static_cast<unsigned>(mirror().size());
        model_->Reset(count);  // no row is deleted one by one
        if (count == 0) {
            return;
        }
        if (rows.selected.size() == count) {
            view_->SelectAll();
        } else {
            view_->SetSelections(itemsOf(rows.selected));
        }
        // The focus only on a selected row: wxOSX's SetCurrentItem also selects it (NSTableView
        // has no current row apart from the selection).
        if (rows.focus && std::ranges::binary_search(rows.selected, *rows.focus)) {
            view_->SetCurrentItem(itemAt(*rows.focus));
        }
        if (rows.tail) {
            view_->EnsureVisible(itemAt(count - 1));
        } else if (rows.top) {
            showAtTop(*rows.top);
        }
    }

    [[nodiscard]] ListPlace place() const override {
        ListPlace place;
        const std::size_t count = model_->GetCount();
        if (count == 0) {
            place.tail = true;
            return place;
        }
        place.selected = selectedSequences();
        if (const wxDataViewItem focus = view_->GetCurrentItem(); focus.IsOk()) {
            place.focus = sequenceAt(model_->GetRow(focus));
        }
        const std::size_t top = topRow();
        place.top = sequenceAt(top);
        place.tail = top + static_cast<std::size_t>(std::max(view_->GetCountPerPage(), 1)) >= count;
        return place;
    }

    [[nodiscard]] std::vector<std::uint64_t> selectedSequences() const override {
        wxDataViewItemArray items;
        view_->GetSelections(items);
        std::vector<std::uint64_t> sequences;
        sequences.reserve(items.size());
        for (const wxDataViewItem& item : items) {
            if (const std::optional<std::uint64_t> sequence = sequenceAt(model_->GetRow(item))) {
                sequences.push_back(*sequence);
            }
        }
        std::ranges::sort(sequences);
        return sequences;
    }

    void selectRows(std::span<const std::size_t> rows) override {
        const std::size_t count = model_->GetCount();
        std::vector<std::size_t> kept;
        for (const std::size_t row : rows) {
            if (row < count) {
                kept.push_back(row);
            }
        }
        view_->SetSelections(itemsOf(kept));
    }

    void scrollToTop(std::size_t row) override {
        if (row < model_->GetCount()) {
            showAtTop(row);
        }
    }

    void selectAll() override { view_->SelectAll(); }

    [[nodiscard]] wxWindow* control() const override { return view_; }

    [[nodiscard]] std::size_t rowCount() const override { return model_->GetCount(); }

    [[nodiscard]] wxString cellText(std::size_t row, std::size_t column) const override {
        if (row >= rowCount() || column >= LOG_LIST_COLUMNS.size()) {
            return {};
        }
        wxVariant value;
        model_->GetValueByRow(value, static_cast<unsigned>(row), static_cast<unsigned>(column));
        return value.GetString();
    }

    [[nodiscard]] LogListKind kind() const noexcept override { return LogListKind::DataView; }

private:
    [[nodiscard]] wxDataViewItem itemAt(std::size_t row) const {
        return model_->GetItem(static_cast<unsigned>(row));
    }

    [[nodiscard]] wxDataViewItemArray itemsOf(std::span<const std::size_t> rows) const {
        wxDataViewItemArray items;
        items.reserve(rows.size());
        for (const std::size_t row : rows) {
            items.push_back(itemAt(row));
        }
        return items;
    }

    [[nodiscard]] std::size_t topRow() const {
        const wxDataViewItem top = view_->GetTopItem();
        return top.IsOk() ? model_->GetRow(top) : 0;
    }

    // `row` at the top: EnsureVisible scrolls as little as it can, so a row below the view first
    // goes to the end and then back up to the top edge.
    void showAtTop(std::size_t row) {
        const std::size_t top = topRow();
        if (top == row) {
            return;
        }
        if (top < row) {
            view_->EnsureVisible(itemAt(model_->GetCount() - 1));
        }
        view_->EnsureVisible(itemAt(row));
    }

    wxObjectDataPtr<LogListModel> model_;
    wxDataViewCtrl* view_;  // a child window: wx owns it
};

}  // namespace

LogList* createDataViewLogList(wxWindow* parent, const LogMirror& mirror) {
    return new DataViewLogList(parent, mirror);
}

}  // namespace rawreel
