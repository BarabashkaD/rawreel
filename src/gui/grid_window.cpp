// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/grid_window.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/clrpicker.h>
#include <wx/fontenum.h>
#include <wx/gbsizer.h>
#include <wx/notebook.h>
#include <wx/radiobut.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/stattext.h>

#include "core/overlay.h"
#include "gui/floating_window.h"
#include "gui/main_frame.h"
#include "gui/native_controls.h"
#include "gui/native_policy.h"
#include "gui/viewport.h"  // rulerFontPoints

namespace rawreel {

namespace {

constexpr int ROW_GAP_DIP = 4;     // between the rows of a level, and around the static lines
constexpr int COLUMN_GAP_DIP = 8;  // between a level's controls
constexpr int SLIDER_WIDTH_DIP = 120;
constexpr int FAMILY_WIDTH_DIP = 170;  // the family list: long family names never widen the window
// The width the Last value examples are measured for, so the window keeps its size up to
// five-digit clips.
constexpr std::uint32_t WIDEST_EXAMPLE_WIDTH = 99999;
// How long after a slider's scroll's end, with no other end following it, the value is saved.
constexpr std::chrono::milliseconds SCROLL_SAVE_QUIET{400};
constexpr long PERCENT_ALIGNMENT = wxALIGN_RIGHT;  // the "85 %" texts, on every OS
constexpr std::array<const char*, GRID_LEVELS> LEVEL_LABELS{"Major", "Minor", "Second minor"};
// The widest texts a note line and the ruler note can hold: the ruler note's minimum width, which
// the level notes below the checkboxes' labels share.
constexpr std::array<const char*, 3> NOTE_SAMPLES{
    "hidden at this zoom (256 px at 3200 % = 99.9 pt, needs 100 pt)",
    "hidden at this zoom (the major lines are hidden)",
    "Rulers off at this zoom: major ticks closer than 100 pt"};
// The Style list: the OverlayStyle values in their order, "Custom" (the last) only while Custom.
constexpr std::array<OverlayStyle, 4> PRESET_STYLES{OverlayStyle::Plain, OverlayStyle::Adaptive,
                                                    OverlayStyle::Coloured, OverlayStyle::Halo};

wxString percentText(std::uint32_t opacity) {
    return wxString::FromUTF8(std::format("{} %", opacity));
}

std::array<std::uint32_t, GRID_LEVELS> stepsOf(const GridConfig& grid) {
    return {grid.levels[0].step, grid.levels[1].step, grid.levels[2].step};
}

std::uint32_t clampedOpacity(int value) {
    return static_cast<std::uint32_t>(std::clamp(value,
                                                 static_cast<int>(GRID_OPACITY_RANGE.minimum),
                                                 static_cast<int>(GRID_OPACITY_RANGE.maximum)));
}

wxColour wxColourOf(Rgb8 colour) {
    return {colour.r, colour.g, colour.b};
}

Rgb8 rgbOf(const wxColour& colour) {
    return {.r = colour.Red(), .g = colour.Green(), .b = colour.Blue()};
}

// Case-insensitive, then exact for names that differ only in case: a total order, so equal names
// sort next to each other even with case variants between them, and unique() removes them all.
// Case variants stay apart (each may be its own family).
bool familyBefore(const std::string& left, const std::string& right) {
    const int nocase = wxString::FromUTF8(left).CmpNoCase(wxString::FromUTF8(right));
    return nocase != 0 ? nocase < 0 : left < right;
}

// "" (System) first, then the OS's families without the hidden ones ("@" vertical variants,
// "." system-private names), sorted case-insensitively.
std::vector<std::string> osFamilies() {
    std::vector<std::string> families;
    for (const wxString& name : wxFontEnumerator::GetFacenames()) {
        if (!name.empty() && name[0] != '@' && name[0] != '.') {
            families.push_back(name.utf8_string());
        }
    }
    std::ranges::sort(families, familyBefore);
    const auto [first, last] = std::ranges::unique(families);
    families.erase(first, last);
    families.insert(families.begin(), std::string());
    return families;
}

wxArrayString familyItems(const std::vector<std::string>& families) {
    wxArrayString items;
    for (const std::string& family : families) {
        items.Add(family.empty() ? wxString("System") : wxString::FromUTF8(family));
    }
    return items;
}

// The size the spin control shows while System size is checked: the system size, whole points.
int systemSizeShown() {
    return std::clamp(static_cast<int>(std::lround(rulerFontPoints({}))),
                      static_cast<int>(RULER_FONT_SIZE_RANGE.minimum),
                      static_cast<int>(RULER_FONT_SIZE_RANGE.maximum));
}

std::uint32_t clampedFontSize(int value) {
    return static_cast<std::uint32_t>(std::clamp(value,
                                                 static_cast<int>(RULER_FONT_SIZE_RANGE.minimum),
                                                 static_cast<int>(RULER_FONT_SIZE_RANGE.maximum)));
}

// "\xE2\x80\xA6" is U+2026 (horizontal ellipsis) in UTF-8.
wxString lastPixelText(std::uint32_t width) {
    return wxString::FromUTF8(
        std::format("Last pixel (0 \xE2\x80\xA6 {})", width == 0 ? 0 : width - 1));
}

wxString edgeText(std::uint32_t width) {
    return wxString::FromUTF8(std::format("Edge (0 \xE2\x80\xA6 {})", width));
}

}  // namespace

GridWindow::GridWindow(MainFrame& owner)
    : FloatingWindow(owner, FloatingKind::GridAndRulers, "Grid and Rulers"),
      save_quiet_(SCROLL_SAVE_QUIET) {
    book_ = new wxNotebook(&content(), wxID_ANY);
    book_->AddPage(buildQuick(), "Quick", true);
    book_->AddPage(buildAppearance(), "Appearance");

    auto* const content_sizer = new wxBoxSizer(wxVERTICAL);
    content_sizer->Add(book_, wxSizerFlags(1).Expand().Border());  // the OS's dialog margins
    content().SetSizer(content_sizer);

    save_timer_.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { apply(OverlaySave::Now); });
    applyColours();
    showOverlay(owner.grid(), owner.rulers());
    measure();
    Fit();  // once: the size is fixed from now on
}

wxPanel* GridWindow::buildQuick() {
    auto* const quick = new wxPanel(book_);
    auto* const quick_sizer = new wxBoxSizer(wxVERTICAL);
    const int row_gap = FromDIP(ROW_GAP_DIP);
    const int column_gap = FromDIP(COLUMN_GAP_DIP);

    auto* const ruler_row = new wxBoxSizer(wxHORIZONTAL);
    rulers_box_ = new wxCheckBox(quick, wxID_ANY, "Show rulers");
    numbers_box_ = new wxCheckBox(quick, wxID_ANY, "Ruler numbers");
    ruler_row->Add(rulers_box_, wxSizerFlags().CenterVertical());
    ruler_row->AddSpacer(FromDIP(3 * COLUMN_GAP_DIP));
    ruler_row->Add(numbers_box_, wxSizerFlags().CenterVertical());
    quick_sizer->Add(ruler_row);
    rulers_box_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
        rulers_.show = event.IsChecked();
        apply(OverlaySave::Now);
    });
    numbers_box_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
        rulers_.numbers = event.IsChecked();
        apply(OverlaySave::Now);
    });
    quick_sizer->Add(new wxStaticLine(quick),
                     wxSizerFlags().Expand().Border(wxTOP | wxBOTTOM, 2 * row_gap));

    // The rows: checkbox with its label, step, slider (it takes the extra width), percent; the
    // note under the row starts under the label and has no width of its own, so it never widens
    // a column.
    auto* const levels = new wxGridBagSizer(row_gap, column_gap);
    for (std::size_t i = 0; i < GRID_LEVELS; ++i) {
        LevelRow& level = rows_[i];
        const wxString label = LEVEL_LABELS[i];
        level.box = new wxCheckBox(quick, wxID_ANY, label);
        level.step = new wxChoice(quick, wxID_ANY);
        setAccessibleName(*level.step, label + " step");
        level.opacity =
            new wxSlider(quick, wxID_ANY, 0, static_cast<int>(GRID_OPACITY_RANGE.minimum),
                         static_cast<int>(GRID_OPACITY_RANGE.maximum), wxDefaultPosition,
                         FromDIP(wxSize(SLIDER_WIDTH_DIP, -1)));
        setAccessibleName(*level.opacity, label + " opacity");
        level.percent = new wxStaticText(quick, wxID_ANY, wxString(), wxDefaultPosition,
                                         wxDefaultSize, PERCENT_ALIGNMENT);
        level.note = new wxStaticText(quick, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize,
                                      wxST_NO_AUTORESIZE);
        const int line = 2 * static_cast<int>(i);
        const auto flags = wxALIGN_CENTER_VERTICAL;
        levels->Add(level.box, wxGBPosition(line, 0), wxDefaultSpan, flags);
        levels->Add(level.step, wxGBPosition(line, 1), wxDefaultSpan, flags);
        levels->Add(level.opacity, wxGBPosition(line, 2), wxDefaultSpan, flags | wxEXPAND);
        levels->Add(level.percent, wxGBPosition(line, 3), wxDefaultSpan, flags);
        levels->Add(level.note, wxGBPosition(line + 1, 0), wxGBSpan(1, 4), wxEXPAND | wxLEFT);

        level.box->Bind(wxEVT_CHECKBOX, [this, i](wxCommandEvent& event) {
            grid_.levels[i].show = event.IsChecked();
            apply(OverlaySave::Now);
        });
        // Choosing a step never changes another level; the lists are rebuilt by showOverlay().
        level.step->Bind(wxEVT_CHOICE, [this, i](wxCommandEvent& event) {
            const std::vector<std::uint32_t>& steps = rows_[i].steps;
            const int chosen = event.GetSelection();
            if (chosen < 0 || static_cast<std::size_t>(chosen) >= steps.size()) {
                return;
            }
            grid_.levels[i].step = steps[static_cast<std::size_t>(chosen)];
            apply(OverlaySave::Now);
        });
        // A move applies live and is not saved. The thumb's release (macOS's only release event,
        // a drag's end elsewhere) saves the value it carries at once. The scroll's end (Windows
        // and GTK: a drag's end, every step of a held key, a click on the track, every wheel
        // notch) applies the value it carries, whatever its order with the move (GTK and the
        // Windows wheel send it before wxEVT_SLIDER), and saves once no other end followed for
        // the quiet period (SCROLL_SAVE_QUIET), so a held key or a turning wheel writes the file
        // once.
        level.opacity->Bind(wxEVT_SLIDER, [this, i](wxCommandEvent& event) {
            grid_.levels[i].opacity = clampedOpacity(event.GetInt());
            apply(OverlaySave::Later);
        });
        level.opacity->Bind(wxEVT_SCROLL_THUMBRELEASE, [this, i](wxScrollEvent& event) {
            event.Skip();
            grid_.levels[i].opacity = clampedOpacity(event.GetPosition());
            save_timer_.Stop();
            apply(OverlaySave::Now);
        });
        level.opacity->Bind(wxEVT_SCROLL_CHANGED, [this, i](wxScrollEvent& event) {
            event.Skip();
            grid_.levels[i].opacity = clampedOpacity(event.GetPosition());
            apply(OverlaySave::Later);
            save_timer_.StartOnce(static_cast<int>(save_quiet_.count()));
        });
        // A key's release saves at once: macOS sends no release after a key change (Full
        // Keyboard Access arrows); elsewhere it ends the quiet period of the key's last step.
        level.opacity->Bind(wxEVT_KEY_UP, [this](wxKeyEvent& event) {
            event.Skip();
            save_timer_.Stop();
            apply(OverlaySave::Now);
        });
    }
    levels->AddGrowableCol(2);
    quick_sizer->Add(levels, wxSizerFlags().Expand());

    hint_ = new wxStaticText(quick, wxID_ANY, wxString::FromUTF8(GRID_STEP_RULE));
    quick_sizer->Add(hint_, wxSizerFlags().Border(wxTOP, 2 * row_gap));
    ruler_note_ = new wxStaticText(quick, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize,
                                   wxST_NO_AUTORESIZE);
    quick_sizer->Add(ruler_note_, wxSizerFlags().Expand().Border(wxTOP, row_gap));
    auto* const quick_border = new wxBoxSizer(wxVERTICAL);
    quick_border->Add(quick_sizer, wxSizerFlags(1).Expand().Border());  // the OS's dialog margins
    quick->SetSizer(quick_border);
    return quick;
}

wxPanel* GridWindow::buildAppearance() {
    auto* const page = new wxPanel(book_);
    auto* const sizer = new wxBoxSizer(wxVERTICAL);
    const int row_gap = FromDIP(ROW_GAP_DIP);
    const int column_gap = FromDIP(COLUMN_GAP_DIP);

    // One label column: Style, then the levels, then Cursor, each label aligned as the OS aligns a
    // label column; the Style list spans the value columns, so it never widens the colour column.
    auto* const levels = new wxGridBagSizer(row_gap, column_gap);
    const int label_flags =
        static_cast<int>(native_policy::labelAlignment()) | wxALIGN_CENTER_VERTICAL;
    style_label_ = new wxStaticText(page, wxID_ANY, "Style");
    levels->Add(style_label_, wxGBPosition(0, 0), wxDefaultSpan, label_flags);
    style_ = new wxChoice(page, wxID_ANY);
    for (const OverlayStyle style : PRESET_STYLES) {
        style_->Append(wxString::FromUTF8(overlayStyleName(style)));
    }
    setAccessibleName(*style_, "Grid style");
    levels->Add(style_, wxGBPosition(0, 1), wxGBSpan(1, 4), wxALIGN_CENTER_VERTICAL);
    // A preset sets the colours, opacities, Adaptive, Halo and the cursor colour; choosing "Custom"
    // changes nothing.
    style_->Bind(wxEVT_CHOICE, [this](wxCommandEvent& event) {
        const int chosen = event.GetSelection();
        if (chosen < 0 || static_cast<std::size_t>(chosen) >= PRESET_STYLES.size()) {
            return;
        }
        applyOverlayStyle(grid_, rulers_, PRESET_STYLES[static_cast<std::size_t>(chosen)]);
        apply(OverlaySave::Now);
    });
    levels->Add(new wxStaticLine(page), wxGBPosition(1, 0), wxGBSpan(1, 5),
                wxEXPAND | wxTOP | wxBOTTOM, row_gap);

    // The rows: label, colour, thickness, Adaptive, Halo; the headings name the unlabelled columns.
    const auto heading = [this, page](const char* text) {
        auto* const label = new wxStaticText(page, wxID_ANY, text);
        headings_.push_back(label);
        return label;
    };
    levels->Add(heading("Colour"), wxGBPosition(2, 1), wxDefaultSpan, wxALIGN_BOTTOM);
    levels->Add(heading("Thickness"), wxGBPosition(2, 2), wxDefaultSpan, wxALIGN_BOTTOM);
    wxArrayString thicknesses;
    for (std::uint32_t pt = GRID_THICKNESS_RANGE.minimum; pt <= GRID_THICKNESS_RANGE.maximum;
         ++pt) {
        thicknesses.Add(wxString::Format("%u pt", pt));
    }
    for (std::size_t i = 0; i < GRID_LEVELS; ++i) {
        LevelRow& level = rows_[i];
        const wxString label = LEVEL_LABELS[i];
        level.label = new wxStaticText(page, wxID_ANY, label);
        level.colour = new wxColourPickerCtrl(page, wxID_ANY, *wxWHITE);
        setAccessibleName(*level.colour->GetPickerCtrl(), label + " colour");
        level.thickness =
            new wxChoice(page, wxID_ANY, wxDefaultPosition, wxDefaultSize, thicknesses);
        setAccessibleName(*level.thickness, label + " thickness");
        level.adaptive = new wxCheckBox(page, wxID_ANY, "Adaptive");
        setAccessibleName(*level.adaptive, label + " adaptive");
        level.halo = new wxCheckBox(page, wxID_ANY, "Halo");
        setAccessibleName(*level.halo, label + " halo");
        const int line = 3 + static_cast<int>(i);
        const auto flags = wxALIGN_CENTER_VERTICAL;
        levels->Add(level.label, wxGBPosition(line, 0), wxDefaultSpan, label_flags);
        levels->Add(level.colour, wxGBPosition(line, 1), wxDefaultSpan, flags);
        levels->Add(level.thickness, wxGBPosition(line, 2), wxDefaultSpan, flags);
        levels->Add(level.adaptive, wxGBPosition(line, 3), wxDefaultSpan, flags);
        levels->Add(level.halo, wxGBPosition(line, 4), wxDefaultSpan, flags);

        bindColour(*level.colour, grid_.levels[i].colour, level.dialogColour);
        level.thickness->Bind(wxEVT_CHOICE, [this, i](wxCommandEvent& event) {
            const int chosen = event.GetSelection();
            if (chosen < 0) {
                return;
            }
            grid_.levels[i].thickness =
                std::min(GRID_THICKNESS_RANGE.minimum + static_cast<std::uint32_t>(chosen),
                         GRID_THICKNESS_RANGE.maximum);
            apply(OverlaySave::Now);
        });
        level.adaptive->Bind(wxEVT_CHECKBOX, [this, i](wxCommandEvent& event) {
            grid_.levels[i].adaptive = event.IsChecked();
            apply(OverlaySave::Now);
        });
        level.halo->Bind(wxEVT_CHECKBOX, [this, i](wxCommandEvent& event) {
            grid_.levels[i].halo = event.IsChecked();
            apply(OverlaySave::Now);
        });
    }
    // The cursor colour, below the levels: the rulers' lit tick and cursor marker.
    cursor_label_ = new wxStaticText(page, wxID_ANY, "Cursor");
    cursor_colour_ = new wxColourPickerCtrl(page, wxID_ANY, wxColourOf(rulers_.cursorColour));
    setAccessibleName(*cursor_colour_->GetPickerCtrl(), "Cursor colour");
    const int cursor_line = 3 + static_cast<int>(GRID_LEVELS);
    levels->Add(cursor_label_, wxGBPosition(cursor_line, 0), wxDefaultSpan, label_flags);
    levels->Add(cursor_colour_, wxGBPosition(cursor_line, 1), wxDefaultSpan,
                wxALIGN_CENTER_VERTICAL);
    bindColour(*cursor_colour_, rulers_.cursorColour, cursor_dialog_colour_);
    buildRulerRows(*page, *levels, cursor_line + 1, label_flags);
    sizer->Add(levels);

    auto* const border = new wxBoxSizer(wxVERTICAL);
    border->Add(sizer, wxSizerFlags(1).Expand().Border());  // the OS's dialog margins
    page->SetSizer(border);
    return page;
}

void GridWindow::bindColour(wxColourPickerCtrl& picker, Rgb8& colour,
                            std::optional<Rgb8>& dialog_colour) {
    // The dialog's live changes apply and are not saved; OK saves the colour it chose, Cancel
    // shows the colour from before the dialog again. Only the Windows dialog sends live changes;
    // the macOS and GTK pickers apply and save on OK only (the macOS cancel still comes and finds
    // nothing to restore; GTK's native button sends no cancel).
    picker.Bind(wxEVT_COLOURPICKER_CURRENT_CHANGED,
                [this, &colour, &dialog_colour](wxColourPickerEvent& event) {
                    if (!dialog_colour) {
                        dialog_colour = colour;
                    }
                    colour = rgbOf(event.GetColour());
                    apply(OverlaySave::Later);
                });
    picker.Bind(wxEVT_COLOURPICKER_DIALOG_CANCELLED,
                [this, &colour, &dialog_colour](wxColourPickerEvent&) {
                    if (dialog_colour) {
                        colour = *dialog_colour;
                        dialog_colour.reset();
                        apply(OverlaySave::Later);
                    }
                });
    picker.Bind(wxEVT_COLOURPICKER_CHANGED,
                [this, &colour, &dialog_colour](wxColourPickerEvent& event) {
                    dialog_colour.reset();
                    colour = rgbOf(event.GetColour());
                    apply(OverlaySave::Now);
                });
}

void GridWindow::buildRulerRows(wxWindow& page, wxGridBagSizer& rows, int line, int label_flags) {
    const int column_gap = FromDIP(COLUMN_GAP_DIP);
    rows.Add(new wxStaticLine(&page), wxGBPosition(line, 0), wxGBSpan(1, 5),
             wxEXPAND | wxTOP | wxBOTTOM, FromDIP(ROW_GAP_DIP));

    // Ruler font: family, size, System size. Checked, the size is 0 (the OS's small system font
    // size) and the spin control, disabled, shows the system size; unchecking stores the spin
    // control's value.
    font_label_ = new wxStaticText(&page, wxID_ANY, "Ruler font");
    rows.Add(font_label_, wxGBPosition(line + 1, 0), wxDefaultSpan, label_flags);
    families_ = osFamilies();
    font_family_ = new wxChoice(&page, wxID_ANY, wxDefaultPosition,
                                FromDIP(wxSize(FAMILY_WIDTH_DIP, -1)), familyItems(families_));
    setAccessibleName(*font_family_, "Ruler font");
    font_size_ = new wxSpinCtrl(&page, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize,
                                wxSP_ARROW_KEYS, static_cast<int>(RULER_FONT_SIZE_RANGE.minimum),
                                static_cast<int>(RULER_FONT_SIZE_RANGE.maximum), systemSizeShown());
    setAccessibleName(*font_size_, "Ruler font size");
    font_unit_ = new wxStaticText(&page, wxID_ANY, "pt");
    system_size_ = new wxCheckBox(&page, wxID_ANY, "System size");
    auto* const font_row = new wxBoxSizer(wxHORIZONTAL);
    font_row->Add(font_family_, wxSizerFlags().CenterVertical());
    font_row->AddSpacer(column_gap);
    font_row->Add(font_size_, wxSizerFlags().CenterVertical());
    font_row->AddSpacer(column_gap / 2);
    font_row->Add(font_unit_, wxSizerFlags().CenterVertical());
    font_row->AddSpacer(column_gap);
    font_row->Add(system_size_, wxSizerFlags().CenterVertical());
    rows.Add(font_row, wxGBPosition(line + 1, 1), wxGBSpan(1, 4), wxALIGN_CENTER_VERTICAL);
    font_family_->Bind(wxEVT_CHOICE, [this](wxCommandEvent& event) {
        const int chosen = event.GetSelection();
        if (chosen < 0 || static_cast<std::size_t>(chosen) >= families_.size()) {
            return;
        }
        rulers_.font.family = families_[static_cast<std::size_t>(chosen)];
        apply(OverlaySave::Now);
    });
    font_size_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent& event) {
        if (rulers_.font.size == 0) {
            return;  // System size: the spin control only shows the system size
        }
        rulers_.font.size = clampedFontSize(event.GetPosition());
        apply(OverlaySave::Now);
    });
    system_size_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
        rulers_.font.size = event.IsChecked() ? 0 : clampedFontSize(font_size_->GetValue());
        apply(OverlaySave::Now);
    });

    // Last value: the number at a ruler's far end, each with its example for the open clip.
    last_value_label_ = new wxStaticText(&page, wxID_ANY, "Last value");
    rows.Add(last_value_label_, wxGBPosition(line + 2, 0), wxDefaultSpan, label_flags);
    last_pixel_ = new wxRadioButton(&page, wxID_ANY, lastPixelText(image_width_), wxDefaultPosition,
                                    wxDefaultSize, wxRB_GROUP);
    edge_ = new wxRadioButton(&page, wxID_ANY, edgeText(image_width_));
    auto* const last_row = new wxBoxSizer(wxHORIZONTAL);
    last_row->Add(last_pixel_, wxSizerFlags().CenterVertical());
    last_row->AddSpacer(column_gap);
    last_row->Add(edge_, wxSizerFlags().CenterVertical());
    rows.Add(last_row, wxGBPosition(line + 2, 1), wxGBSpan(1, 4), wxALIGN_CENTER_VERTICAL);
    last_pixel_->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) {
        rulers_.lastValue = LastValue::Pixel;
        apply(OverlaySave::Now);
    });
    edge_->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) {
        rulers_.lastValue = LastValue::Edge;
        apply(OverlaySave::Now);
    });
}

void GridWindow::listFamily(const std::string& family) {
    if (std::ranges::find(families_, family) != families_.end()) {
        return;
    }
    // After System, in the sorted place among the OS's families.
    const auto place =
        std::upper_bound(families_.begin() + 1, families_.end(), family, familyBefore);
    families_.insert(place, family);
    font_family_->Set(familyItems(families_));
}

void GridWindow::showImageWidth(std::optional<std::uint32_t> width) {
    image_width_ = width.value_or(DEFAULT_EXAMPLE_WIDTH);
    last_pixel_->SetLabel(lastPixelText(image_width_));
    edge_->SetLabel(edgeText(image_width_));
    book_->GetPage(1)->Layout();
}

const GridWindow::LevelRow& GridWindow::row(GridLevel level) const {
    return rows_[static_cast<std::size_t>(level)];
}

void GridWindow::measure() {
    int note_width = 0;
    for (const char* const sample : NOTE_SAMPLES) {
        note_width = std::max(note_width, ruler_note_->GetTextExtent(sample).GetWidth());
    }
    const int percent_width = rows_[0].percent->GetTextExtent(percentText(100)).GetWidth();
    int indent = 0;
    for (LevelRow& level : rows_) {
        // The checkbox's glyph and its gap: its width without its label's.
        const int glyph =
            std::max(0, level.box->GetBestSize().GetWidth() -
                            level.box->GetTextExtent(level.box->GetLabel()).GetWidth());
        if (wxSizerItem* const item = level.note->GetContainingSizer()->GetItem(level.note);
            item != nullptr) {
            item->SetBorder(glyph);
        }
        indent = std::max(indent, glyph);
        level.note->SetMinSize(wxSize(0, -1));
        level.percent->SetMinSize(wxSize(percent_width, -1));
    }
    ruler_note_->SetMinSize(wxSize(note_width + indent, -1));
    // The Last value radios: wide enough for the examples of a five-digit clip.
    const auto example_width = [](wxRadioButton& radio, const wxString& widest) {
        const int glyph = std::max(
            0, radio.GetBestSize().GetWidth() - radio.GetTextExtent(radio.GetLabel()).GetWidth());
        radio.SetMinSize(wxSize(glyph + radio.GetTextExtent(widest).GetWidth(), -1));
    };
    example_width(*last_pixel_, lastPixelText(WIDEST_EXAMPLE_WIDTH));
    example_width(*edge_, edgeText(WIDEST_EXAMPLE_WIDTH));
}

void GridWindow::applyColours() {
    for (LevelRow& level : rows_) {
        setSecondaryText(*level.note);
    }
    for (wxStaticText* const heading : headings_) {
        setSecondaryText(*heading);
    }
    setSecondaryText(*hint_);
    setSecondaryText(*ruler_note_);
}

void GridWindow::showOverlay(const GridConfig& grid, const RulerConfig& rulers) {
    grid_ = grid;
    rulers_ = rulers;
    rulers_box_->SetValue(rulers_.show);
    numbers_box_->SetValue(rulers_.numbers);
    numbers_box_->Enable(rulers_.show);
    const OverlayStyle style = overlayStyleOf(grid_, rulers_);
    const bool listed = style_->GetCount() > PRESET_STYLES.size();  // "Custom" is in the list
    if (style == OverlayStyle::Custom && !listed) {
        style_->Append(wxString::FromUTF8(overlayStyleName(OverlayStyle::Custom)));
    } else if (style != OverlayStyle::Custom && listed) {
        style_->Delete(static_cast<unsigned int>(PRESET_STYLES.size()));
    }
    style_->SetSelection(static_cast<int>(style));
    const std::array<std::uint32_t, GRID_LEVELS> steps = stepsOf(grid_);
    for (std::size_t i = 0; i < GRID_LEVELS; ++i) {
        LevelRow& level = rows_[i];
        const GridLevelConfig& config = grid_.levels[i];
        level.box->SetValue(config.show);
        std::vector<std::uint32_t> valid = validSteps(static_cast<GridLevel>(i), steps);
        if (valid != level.steps) {
            level.steps = std::move(valid);
            wxArrayString items;
            for (const std::uint32_t step : level.steps) {
                items.Add(wxString::Format("%u", step));
            }
            level.step->Set(items);
        }
        const auto at = std::ranges::find(level.steps, config.step);
        level.step->SetSelection(
            at == level.steps.end() ? wxNOT_FOUND : static_cast<int>(at - level.steps.begin()));
        level.opacity->SetValue(static_cast<int>(config.opacity));
        level.percent->SetLabel(percentText(config.opacity));
        level.colour->SetColour(wxColourOf(config.colour));
        level.thickness->SetSelection(
            static_cast<int>(std::clamp(config.thickness, GRID_THICKNESS_RANGE.minimum,
                                        GRID_THICKNESS_RANGE.maximum) -
                             GRID_THICKNESS_RANGE.minimum));
        level.adaptive->SetValue(config.adaptive);
        level.halo->SetValue(config.halo);
    }
    cursor_colour_->SetColour(wxColourOf(rulers_.cursorColour));
    listFamily(rulers_.font.family);
    font_family_->SetSelection(
        static_cast<int>(std::ranges::find(families_, rulers_.font.family) - families_.begin()));
    const bool system_size = rulers_.font.size == 0;
    system_size_->SetValue(system_size);
    font_size_->SetValue(
        system_size ? systemSizeShown()
                    : static_cast<int>(clampedFontSize(static_cast<int>(rulers_.font.size))));
    last_pixel_->SetValue(rulers_.lastValue == LastValue::Pixel);
    edge_->SetValue(rulers_.lastValue == LastValue::Edge);
    // The numbers' rows: off with the numbers, and with the rulers (Ruler numbers is disabled).
    const bool numbered = rulers_.show && rulers_.numbers;
    for (wxWindow* const control :
         std::initializer_list<wxWindow*>{font_label_, font_family_, font_unit_, system_size_,
                                          last_value_label_, last_pixel_, edge_}) {
        control->Enable(numbered);
    }
    font_size_->Enable(numbered && !system_size);
    book_->GetPage(0)->Layout();  // the percent texts keep their right edge
    updateNotes();
}

void GridWindow::showNotes(double zoom, double backing_per_dip) {
    zoom_ = zoom;
    backing_per_dip_ = backing_per_dip;
    updateNotes();
}

void GridWindow::updateNotes() {
    const bool known = zoom_ > 0 && backing_per_dip_ > 0;
    for (std::size_t i = 0; i < GRID_LEVELS; ++i) {
        const bool drawn = grid_.levels[0].show && grid_.levels[i].show;
        const std::string note = known && drawn ? gridLevelNote(grid_, static_cast<GridLevel>(i),
                                                                zoom_, backing_per_dip_)
                                                : std::string();
        rows_[i].note->SetLabelText(wxString::FromUTF8(note));
    }
    ruler_note_->SetLabelText(wxString::FromUTF8(
        known ? rulerOffNote(grid_, rulers_, zoom_, backing_per_dip_) : std::string()));
}

bool GridWindow::Destroy() {
    save_timer_.Stop();
    return FloatingWindow::Destroy();
}

void GridWindow::endPending() {
    save_timer_.Stop();
    bool restored = false;
    for (std::size_t i = 0; i < GRID_LEVELS; ++i) {
        if (const std::optional<Rgb8> before = std::exchange(rows_[i].dialogColour, std::nullopt)) {
            grid_.levels[i].colour = *before;
            restored = true;
        }
    }
    if (const std::optional<Rgb8> before = std::exchange(cursor_dialog_colour_, std::nullopt)) {
        rulers_.cursorColour = *before;
        restored = true;
    }
    if (restored) {
        apply(OverlaySave::Later);
    }
}

void GridWindow::apply(OverlaySave save) {
    owner().setOverlay(grid_, rulers_, save);  // shows the result again (showOverlay)
}

wxString GridWindow::noteForTest(GridLevel level) const {
    return row(level).note->GetLabelText();
}

wxString GridWindow::rulerNoteForTest() const {
    return ruler_note_->GetLabelText();
}

}  // namespace rawreel
