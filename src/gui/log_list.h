#pragma once

// The Log pane's list (Log pane and Settings design §4.1): one row per record of the pane's
// LogMirror, multiple selection, three columns (four with log.source, addendum §4). Two thin
// adapters read the same mirror: a virtual wxListCtrl (the native ListView on Windows) and a
// wxDataViewCtrl with a virtual list model (native on macOS and Linux). Both are built on every OS.
// A batch never deletes rows one by one: the adapter sets the new count, then restores the place
// rowsAfterBatch() mapped. Nothing here logs (spec §3.3: a line logged in the refresh path would
// post another refresh, forever).

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "core/log_mirror.h"

namespace rawreel {

enum class LogListKind : std::uint8_t { ListCtrl, DataView };

// ListCtrl on Windows, DataView elsewhere (spec §4.1).
[[nodiscard]] LogListKind nativeLogListKind();

inline constexpr std::array<std::string_view, 4> LOG_LIST_COLUMNS{"Time (local)", "Level",
                                                                  "Message", "Source"};
// The columns shown (addendum §4): Source only with log.source.
[[nodiscard]] constexpr std::size_t logListColumns(bool with_source) noexcept {
    return with_source ? LOG_LIST_COLUMNS.size() : LOG_LIST_COLUMNS.size() - 1;
}

// Whether a row is in view: `row` is its rectangle in the control's client coordinates (empty when
// the control reports it out of view) and its middle lies within the client height. Half a row is
// enough, so a pixel of rounding in either direction does not decide it.
[[nodiscard]] inline bool logRowInView(const wxRect& row, const wxSize& client) noexcept {
    if (row.width <= 0 || row.height <= 0) {
        return false;
    }
    const int middle = row.y + row.height / 2;
    return middle >= 0 && middle < client.y;
}

// The text of a cell: the local time "HH:MM:SS.mmm", the level name, the message's first line (with
// " …" when it has more) or the source; "" for a row past the mirror's end or an unknown column.
[[nodiscard]] wxString logCellText(const LogMirror& mirror, std::size_t row, std::size_t column);

class LogList : public wxPanel {
public:
    // A new list of `kind`, a child of `parent`, reading `mirror` (which outlives it), with the
    // Source column only when `with_source` (log.source); empty until rebuild() or applyBatch().
    [[nodiscard]] static LogList* create(wxWindow* parent, const LogMirror& mirror,
                                         LogListKind kind, bool with_source);

    // The rows from the mirror, no selection, the last row visible.
    virtual void rebuild() = 0;
    // After a batch: the count from the mirror, then `rows` restored (selection, anchor, focus, and
    // the last row or the top row).
    virtual void applyBatch(const ListRows& rows) = 0;
    // Where the list stands, by sequence number (taken before a batch); tail is lastRowVisible().
    [[nodiscard]] virtual ListPlace place() const = 0;
    // Whether the last row is in view (logRowInView with the control's own row rectangle), not row
    // arithmetic: wxOSX's GetCountPerPage counts only whole rows below the header while its
    // GetTopItem is the partly hidden row under it. False for an empty list.
    [[nodiscard]] virtual bool lastRowVisible() const = 0;
    // The sequences of the selected rows, ascending.
    [[nodiscard]] virtual std::vector<std::uint64_t> selectedSequences() const = 0;
    // Selftests: exactly `rows` selected.
    virtual void selectRows(std::span<const std::size_t> rows) = 0;
    // Selftests: `row` at the top (as near as the end of the list allows).
    virtual void scrollToTop(std::size_t row) = 0;
    virtual void selectAll() = 0;
    // The focusable child (the list control itself).
    [[nodiscard]] virtual wxWindow* control() const = 0;
    // Selftests: the rows the control has and a cell's text as the control's callback gives it.
    [[nodiscard]] virtual std::size_t rowCount() const = 0;
    [[nodiscard]] virtual wxString cellText(std::size_t row, std::size_t column) const = 0;
    // Selftests: the columns the control shows.
    [[nodiscard]] virtual std::size_t columnCount() const = 0;
    [[nodiscard]] virtual LogListKind kind() const noexcept = 0;

protected:
    LogList(wxWindow* parent, const LogMirror& mirror);
    [[nodiscard]] const LogMirror& mirror() const noexcept { return *mirror_; }
    // The sequence of `row`, if the mirror has it.
    [[nodiscard]] std::optional<std::uint64_t> sequenceAt(std::size_t row) const;

private:
    const LogMirror* mirror_;
};

// The adapters (log_list_listctrl.cpp, log_list_dataview.cpp).
[[nodiscard]] LogList* createListCtrlLogList(wxWindow* parent, const LogMirror& mirror,
                                             bool with_source);
[[nodiscard]] LogList* createDataViewLogList(wxWindow* parent, const LogMirror& mirror,
                                             bool with_source);

}  // namespace rawreel
