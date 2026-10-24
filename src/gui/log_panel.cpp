// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/log_panel.h"

#include <utility>

namespace rawreel {

namespace {

constexpr int LOG_PANEL_TICK_MS = 100;

}  // namespace

void LogPanelSink::write(const dvv::LogRecord& /*record*/, std::string_view line) {
    const std::scoped_lock lock(mutex_);
    lines_.emplace_back(line);
    while (lines_.size() > LOG_PANEL_LINES) {
        lines_.pop_front();
    }
}

std::vector<std::string> LogPanelSink::takeLines() {
    std::deque<std::string> taken;
    {
        const std::scoped_lock lock(mutex_);
        taken.swap(lines_);
    }
    return {std::make_move_iterator(taken.begin()), std::make_move_iterator(taken.end())};
}

LogPanel::LogPanel(wxWindow* parent, std::shared_ptr<LogPanelSink> sink)
    : wxPanel(parent),
      sink_(std::move(sink)),
      // RICH2: no size limit of the plain edit control.
      text_(new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                           wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxTE_DONTWRAP)),
      timer_(this) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(text_, wxSizerFlags(1).Expand());
    SetSizer(sizer);
    Bind(wxEVT_TIMER, &LogPanel::onTimer, this, timer_.GetId());
    timer_.Start(LOG_PANEL_TICK_MS);
}

LogPanel::~LogPanel() {
    timer_.Stop();
}

wxString LogPanel::text() const {
    return text_->GetValue();
}

void LogPanel::onTimer(wxTimerEvent& /*event*/) {
    if (!sink_) {
        return;
    }
    const std::vector<std::string> lines = sink_->takeLines();
    if (lines.empty()) {
        return;
    }
    wxString appended;
    for (const std::string& line : lines) {
        appended += wxString::FromUTF8(line);
        appended += '\n';
    }
    text_->AppendText(appended);
    lines_ += lines.size();
    if (lines_ > LOG_PANEL_LINES) {
        // The oldest lines go (design D18).
        const auto excess = static_cast<long>(lines_ - LOG_PANEL_LINES);
        text_->Remove(0, text_->XYToPosition(0, excess));
        lines_ = LOG_PANEL_LINES;
    }
}

}  // namespace rawreel
