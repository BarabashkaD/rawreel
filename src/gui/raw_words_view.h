#pragma once

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <vector>

#include "core/inspector_text.h"

namespace rawreel {

// The pinned pixel's raw stored words (design §4.7 level 3), custom-drawn in the system monospace
// font, one line per word: value bits #50AAFF, another sample's bits #8FB8DE, padding bits grey
// (wxSYS_COLOUR_GRAYTEXT) when 0 and orange (#FF9F0A) when not; the line's own sample bold.
class RawWordsView final : public wxWindow {
public:
    explicit RawWordsView(wxWindow* parent);
    RawWordsView(const RawWordsView&) = delete;
    RawWordsView& operator=(const RawWordsView&) = delete;
    RawWordsView(RawWordsView&&) = delete;
    RawWordsView& operator=(RawWordsView&&) = delete;
    ~RawWordsView() override = default;

    // The lines (rawWordLines) and the theme: `dark` picks the background and the plain text
    // colour, as the picture's background beside it. The minimum size follows the lines.
    void setLines(std::vector<RawWordLine> lines, bool dark);
    [[nodiscard]] const std::vector<RawWordLine>& lines() const noexcept { return lines_; }

private:
    void measure();  // the minimum size: the widest line by the line count, plus the margins
    void onPaint(wxPaintEvent& event);
    void onDpiChanged(wxDPIChangedEvent& event);

    std::vector<RawWordLine> lines_;
    bool dark_ = false;
    wxFont font_;
    wxFont bold_;
};

}  // namespace rawreel
