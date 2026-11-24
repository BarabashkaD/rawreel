#pragma once

// The Info window (window title and Info design §10, §11): a floating window with the stream's
// picture and colour rows, or "No stream". Its shown state belongs to MainFrame::setWindowShown,
// the one path every show and hide goes through.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <string>
#include <utility>
#include <vector>

#include <wx/simplebook.h>

#include "core/commands.h"
#include "gui/floating_window.h"

namespace rawreel {

class MainFrame;
struct StreamInfo;

class InfoWindow final : public FloatingWindow {
public:
    // Shows the owner's first stream, or "No stream".
    explicit InfoWindow(MainFrame& owner);
    InfoWindow(const InfoWindow&) = delete;
    InfoWindow& operator=(const InfoWindow&) = delete;
    InfoWindow(InfoWindow&&) = delete;
    InfoWindow& operator=(InfoWindow&&) = delete;
    ~InfoWindow() override = default;

    // The rows of `stream`, or the "No stream" page for nullptr. The size never changes.
    void showFor(const StreamInfo* stream);

    // "Label", "value" of each row; empty while "No stream" shows.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> rowsForTest() const;

private:
    // Ctrl+C copies the rows where Edit -> Copy is not the way (Windows, Linux).
    bool handleKey(const KeyChord& key) override;
    // The label and value columns' minimum widths from the current fonts.
    void measure() override;
    void applyColours() override;

    wxSimplebook* book_ = nullptr;
    std::vector<wxStaticText*> labels_;
    std::vector<wxStaticText*> values_;
    wxStaticText* no_stream_ = nullptr;
};

}  // namespace rawreel
