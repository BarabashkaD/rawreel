// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/raw_words_view.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/inspector_text.h"
#include "gui/native_controls.h"

namespace rawreel {

namespace {

constexpr int MARGIN_DIP = 6;  // around the lines

// One run of characters drawn alike: [begin, end) of the line's text, the run kind (none: plain
// text) and whether it is the line's own sample.
struct Segment {
    std::size_t begin = 0, end = 0;
    std::optional<BitKind> kind;
    bool own = false;
};

// The line's text cut where the drawing changes. The binary digits follow the first double space
// ("Y 0x9DAD  1001 1101 …"), the most significant bit first, a space after every four bits; the
// text before them is ASCII, so byte positions are character positions there. A UTF-8 continuation
// byte always stays in the segment of its character, so no cut splits a character.
std::vector<Segment> segmentsOf(const RawWordLine& line) {
    const std::string_view text = line.text;
    std::vector<std::optional<BitKind>> kinds(text.size());
    std::vector<bool> own(text.size(), false);
    if (const std::size_t gap = text.find("  "); gap != std::string_view::npos) {
        const std::size_t first = gap + 2;
        std::size_t width = 0;  // the word's bits
        for (std::size_t i = first;
             i < text.size() && (text[i] == '0' || text[i] == '1' ||
                                 (text[i] == ' ' && i + 1 < text.size() && text[i + 1] != ' '));
             ++i) {
            width += text[i] != ' ' ? 1 : 0;
        }
        for (const BitRun& run : line.runs) {
            for (std::size_t bit = run.begin; bit < run.end && bit < width; ++bit) {
                const std::size_t digit = width - 1 - bit;
                const std::size_t at = first + digit + (digit / 4);
                if (at < text.size()) {
                    kinds[at] = run.kind;
                    own[at] = run.own;
                }
            }
        }
    }
    std::vector<Segment> segments;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const bool continuation = (static_cast<unsigned char>(text[i]) & 0xC0U) == 0x80U;
        if (!segments.empty() &&
            (continuation || (segments.back().kind == kinds[i] && segments.back().own == own[i]))) {
            segments.back().end = i + 1;
        } else {
            segments.push_back({.begin = i, .end = i + 1, .kind = kinds[i], .own = own[i]});
        }
    }
    return segments;
}

wxColour plainColour(bool dark) {
    return dark ? wxColour(230, 230, 230) : wxColour(20, 20, 20);
}

// The picture's background greys (viewport.cpp), so the lines read as part of the picture area.
wxColour backgroundOf(bool dark) {
    return dark ? wxColour(31, 31, 31) : wxColour(217, 217, 217);
}

wxColour colourOf(const std::optional<BitKind>& kind, bool dark) {
    if (!kind) {
        return plainColour(dark);
    }
    switch (*kind) {
        case BitKind::Value: return {0x50, 0xAA, 0xFF};
        case BitKind::OtherSample: return {0x8F, 0xB8, 0xDE};
        // The system's grey on the dark background; on the light one a darker grey, about 4.3:1
        // against it where the system grey gives only about 3:1.
        case BitKind::Padding:
            return dark ? wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT)
                        : wxColour(100, 100, 100);
        case BitKind::PaddingSet: return {0xFF, 0x9F, 0x0A};
    }
    return plainColour(dark);
}

}  // namespace

RawWordsView::RawWordsView(wxWindow* parent)
    : wxWindow(parent, wxID_ANY), font_(systemMonospaceFont()), bold_(font_.Bold()) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);  // onPaint fills it
    SetFont(font_);
    Bind(wxEVT_PAINT, &RawWordsView::onPaint, this);
    Bind(wxEVT_DPI_CHANGED, &RawWordsView::onDpiChanged, this);
    measure();
}

void RawWordsView::setLines(std::vector<RawWordLine> lines, bool dark) {
    lines_ = std::move(lines);
    dark_ = dark;
    measure();
    Refresh();
}

void RawWordsView::measure() {
    const int margin = FromDIP(MARGIN_DIP);
    int width = 0;
    for (const RawWordLine& line : lines_) {
        width = std::max(width, GetTextExtent(wxString::FromUTF8(line.text)).x);
    }
    const int lines = static_cast<int>(lines_.size());
    SetMinSize(lines == 0 ? wxSize(0, 0)
                          : wxSize(width + (2 * margin), (lines * GetCharHeight()) + (2 * margin)));
}

void RawWordsView::onDpiChanged(wxDPIChangedEvent& event) {
    event.Skip();
    measure();
}

void RawWordsView::onPaint(wxPaintEvent& /*event*/) {
    wxPaintDC dc(this);
    dc.SetBackground(wxBrush(backgroundOf(dark_)));
    dc.Clear();
    const int margin = FromDIP(MARGIN_DIP);
    dc.SetFont(font_);
    const int line_height = dc.GetCharHeight();
    int y = margin;
    for (const RawWordLine& line : lines_) {
        int x = margin;
        for (const Segment& segment : segmentsOf(line)) {
            const wxString text =
                wxString::FromUTF8(line.text.substr(segment.begin, segment.end - segment.begin));
            dc.SetFont(segment.own ? bold_ : font_);
            dc.SetTextForeground(colourOf(segment.kind, dark_));
            dc.DrawText(text, x, y);
            x += dc.GetTextExtent(text).x;
        }
        y += line_height;
    }
}

}  // namespace rawreel
