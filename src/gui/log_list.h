#pragma once

// The Log pane's list (Log pane and Settings design §4.1): one row per record of the pane's
// LogMirror, multiple selection, four columns. Two thin adapters read the same mirror: a virtual
// wxListCtrl (the native ListView on Windows) and a wxDataViewCtrl with a virtual list model
// (native on macOS and Linux). Both are built on every OS. A batch never deletes rows one by one:
// the adapter sets the new count, then restores the place rowsAfterBatch() mapped. Nothing here
// logs (spec §3.3: a line logged in the refresh path would post another refresh, forever).

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

// The text of a cell: the local time "HH:MM:SS.mmm", the level name, the message's first line (with
// " …" when it has more) or the source; "" for a row past the mirror's end or an unknown column.
[[nodiscard]] wxString logCellText(const LogMirror& mirror, std::size_t row, std::size_t column);

class LogList : public wxPanel {
public:
    // A new list of `kind`, a child of `parent`, reading `mirror` (which outlives it); empty until
    // rebuild() or applyBatch().
    [[nodiscard]] static LogList* create(wxWindow* parent, const LogMirror& mirror,
                                         LogListKind kind);

    // The rows from the mirror, no selection, the last row visible.
    virtual void rebuild() = 0;
    // After a batch: the count from the mirror, then `rows` restored (selection, anchor, focus, and
    // the last row or the top row).
    virtual void applyBatch(const ListRows& rows) = 0;
    // Where the list stands, by sequence number (taken before a batch).
    [[nodiscard]] virtual ListPlace place() const = 0;
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
[[nodiscard]] LogList* createListCtrlLogList(wxWindow* parent, const LogMirror& mirror);
[[nodiscard]] LogList* createDataViewLogList(wxWindow* parent, const LogMirror& mirror);

}  // namespace rawreel
