#pragma once

// The Log pane (design M3 D18): the log lines of this run, newest last, at most 5000.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <wx/timer.h>

#include "dvv/base/log.h"

namespace rawreel {

inline constexpr std::size_t LOG_PANEL_LINES = 5000;

// Keeps the formatted log lines in memory for the Log pane (design D18). It never refers to the
// panel, so it stays valid in the Logger after the panel is gone; it keeps at most 5000 lines.
class LogPanelSink final : public dvv::ILogSink {
public:
    // Any thread, with the Logger's lock held: no logging here.
    void write(const dvv::LogRecord& record, std::string_view line) override;
    [[nodiscard]] std::vector<std::string> takeLines();  // and empties the buffer

private:
    std::mutex mutex_;
    std::deque<std::string> lines_;
};

// A read-only wxTextCtrl fed from the sink by a 100 ms timer, capped at 5000 lines.
class LogPanel final : public wxPanel {
public:
    LogPanel(wxWindow* parent, std::shared_ptr<LogPanelSink> sink);
    LogPanel(const LogPanel&) = delete;
    LogPanel& operator=(const LogPanel&) = delete;
    LogPanel(LogPanel&&) = delete;
    LogPanel& operator=(LogPanel&&) = delete;
    ~LogPanel() override;

    [[nodiscard]] wxString text() const;  // selftests

private:
    void onTimer(wxTimerEvent& event);

    std::shared_ptr<LogPanelSink> sink_;  // null: an empty pane
    wxTextCtrl* text_;                    // a child window: wx owns it
    wxTimer timer_;
    std::size_t lines_ = 0;  // lines in text_
};

}  // namespace rawreel
