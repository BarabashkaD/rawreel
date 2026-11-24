// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/grid_window.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <wx/clrpicker.h>
#include <wx/gbsizer.h>
#include <wx/notebook.h>
#include <wx/statline.h>

#include "core/overlay.h"
#include "gui/floating_window.h"
#include "gui/main_frame.h"
#include "gui/native_controls.h"
#include "gui/native_policy.h"

namespace rawreel {

namespace {

constexpr int ROW_GAP_DIP = 4;     // between the rows of a level, and around the static lines
constexpr int COLUMN_GAP_DIP = 8;  // between a level's controls
constexpr int SLIDER_WIDTH_DIP = 120;
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
    book_->GetPage(0)->Layout();  // the percent texts keep their right edge
    updateNotes();
}

void GridWindow::showNotes(double zoom, double backing_per_dip, std::array<float, 2> label_widths) {
    zoom_ = zoom;
    backing_per_dip_ = backing_per_dip;
    label_widths_ = label_widths;
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
        known ? rulerOffNote(grid_, rulers_, zoom_, backing_per_dip_, label_widths_)
              : std::string()));
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
