#pragma once

// The Grid and Rulers window: a floating window whose Quick tab shows the rulers and, per grid
// level, its on/off, step and opacity, and whose Appearance tab shows the grid's style and, per
// level, its colour, thickness, Adaptive and Halo, and the rulers' cursor colour, font and last
// value (the last two outside the styles). Every change applies at once through
// MainFrame::setOverlay. A slider saves when its thumb or a key is released, and a quiet period
// after the scroll's end (a step of a held key, a click on the track, a wheel notch), so a held key
// or a turning wheel writes the file once; the colour applies live while its dialog is open and
// saves when the dialog closes with OK; every other control saves at its change. Its shown state
// belongs to MainFrame::setWindowShown, the one path every show and hide goes through (a hide
// saves). Nothing it has pending outlives it: a hide and the main window's close end it first
// (endPending), and its Destroy stops the quiet period.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/clrpicker.h>
#include <wx/gbsizer.h>
#include <wx/notebook.h>
#include <wx/radiobut.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/timer.h>

#include "core/overlay.h"
#include "gui/floating_window.h"
#include "gui/main_frame.h"  // OverlaySave

namespace rawreel {

class GridWindow final : public FloatingWindow {
public:
    // The width the Last value examples stand for without a clip.
    static constexpr std::uint32_t DEFAULT_EXAMPLE_WIDTH = 3840;

    // Shows the owner's grid and ruler settings; the notes stay empty until showNotes().
    explicit GridWindow(MainFrame& owner);
    GridWindow(const GridWindow&) = delete;
    GridWindow& operator=(const GridWindow&) = delete;
    GridWindow(GridWindow&&) = delete;
    GridWindow& operator=(GridWindow&&) = delete;
    ~GridWindow() override = default;

    // Stops the quiet period, so its save can never run after the window is gone; the value is
    // already applied (OverlaySave::Later) and the main window's close saves it. wx deletes the
    // window at idle.
    bool Destroy() override;
    // Ends what is pending before the window hides or the main window closes: the quiet period
    // stops (the caller saves the settings next), and a colour that an open dialog's live changes
    // applied and OK did not confirm is replaced by the colour from before the dialog (applied
    // through MainFrame::setOverlay, not saved).
    void endPending();

    // Sets every control from the values without sending events: each step list holds only the
    // validSteps of its level, Ruler numbers is enabled only while the rulers are shown, the font
    // and Last value rows only while the numbers are on and enabled, the Style list holds "Custom"
    // only while overlayStyleOf is Custom. The notes follow, for the view of
    // the last showNotes().
    void showOverlay(const GridConfig& grid, const RulerConfig& rulers);
    // The notes for the view shown now: gridLevelNote on the line of each level that is drawn when
    // the zoom allows (its own checkbox and Major on), and rulerOffNote.
    void showNotes(double zoom, double backing_per_dip);
    // The Last value examples for the open clip's width ("0 ... 3839", "0 ... 3840"); without a
    // clip, for a width of 3840.
    void showImageWidth(std::optional<std::uint32_t> width);

    // Test access.
    [[nodiscard]] wxCheckBox& rulersBoxForTest() const { return *rulers_box_; }
    [[nodiscard]] wxCheckBox& numbersBoxForTest() const { return *numbers_box_; }
    [[nodiscard]] wxCheckBox& levelBoxForTest(GridLevel level) const { return *row(level).box; }
    [[nodiscard]] wxChoice& stepChoiceForTest(GridLevel level) const { return *row(level).step; }
    [[nodiscard]] wxSlider& opacitySliderForTest(GridLevel level) const {
        return *row(level).opacity;
    }
    [[nodiscard]] wxChoice& styleChoiceForTest() const { return *style_; }
    [[nodiscard]] wxStaticText& styleLabelForTest() const { return *style_label_; }
    [[nodiscard]] wxStaticText& appearanceLabelForTest(GridLevel level) const {
        return *row(level).label;
    }
    [[nodiscard]] wxColourPickerCtrl& colourPickerForTest(GridLevel level) const {
        return *row(level).colour;
    }
    [[nodiscard]] wxChoice& thicknessChoiceForTest(GridLevel level) const {
        return *row(level).thickness;
    }
    [[nodiscard]] wxCheckBox& adaptiveBoxForTest(GridLevel level) const {
        return *row(level).adaptive;
    }
    [[nodiscard]] wxCheckBox& haloBoxForTest(GridLevel level) const { return *row(level).halo; }
    [[nodiscard]] wxColourPickerCtrl& cursorColourPickerForTest() const { return *cursor_colour_; }
    [[nodiscard]] wxStaticText& cursorLabelForTest() const { return *cursor_label_; }
    [[nodiscard]] wxChoice& fontFamilyChoiceForTest() const { return *font_family_; }
    [[nodiscard]] wxSpinCtrl& fontSizeSpinForTest() const { return *font_size_; }
    [[nodiscard]] wxCheckBox& systemSizeCheckForTest() const { return *system_size_; }
    [[nodiscard]] wxRadioButton& lastPixelRadioForTest() const { return *last_pixel_; }
    [[nodiscard]] wxRadioButton& edgeRadioForTest() const { return *edge_; }
    [[nodiscard]] wxString noteForTest(GridLevel level) const;
    [[nodiscard]] wxString rulerNoteForTest() const;
    // A scroll's end is waiting for its quiet period to save.
    [[nodiscard]] bool savePendingForTest() const { return save_timer_.IsRunning(); }
    // Sets how long the next scroll's end waits before it saves and returns the length it
    // replaces; a timer already running keeps its own.
    [[nodiscard]] std::chrono::milliseconds setSaveQuietForTest(std::chrono::milliseconds quiet) {
        return std::exchange(save_quiet_, quiet);
    }

private:
    struct LevelRow {
        // Quick
        wxCheckBox* box = nullptr;
        wxChoice* step = nullptr;  // the values of `steps`
        std::vector<std::uint32_t> steps;
        wxSlider* opacity = nullptr;
        wxStaticText* percent = nullptr;  // "85 %"
        wxStaticText* note = nullptr;
        // Appearance
        wxStaticText* label = nullptr;  // the level's name, in the label column
        wxColourPickerCtrl* colour = nullptr;
        // The colour before the open dialog's first live change; none while no dialog changed it.
        std::optional<Rgb8> dialogColour;
        wxChoice* thickness = nullptr;  // "1 pt".."4 pt": GRID_THICKNESS_RANGE
        wxCheckBox* adaptive = nullptr;
        wxCheckBox* halo = nullptr;
    };

    [[nodiscard]] const LevelRow& row(GridLevel level) const;
    [[nodiscard]] wxPanel* buildQuick();
    [[nodiscard]] wxPanel* buildAppearance();
    // The Ruler font and Last value rows below the Cursor row, from `line` of the label column.
    void buildRulerRows(wxWindow& page, wxGridBagSizer& rows, int line, int label_flags);
    // The family list shows `family`: a family the OS does not list is added in its sorted place.
    void listFamily(const std::string& family);
    // Binds a colour picker to `colour`: the dialog's live changes apply unsaved and keep the
    // colour from before them in `dialog_colour` (Cancel and endPending restore it); OK saves.
    void bindColour(wxColourPickerCtrl& picker, Rgb8& colour, std::optional<Rgb8>& dialog_colour);
    // The settings shown, applied through the owner (MainFrame::setOverlay).
    void apply(OverlaySave save);
    // The notes for the settings shown and the view of the last showNotes().
    void updateNotes();
    // The ruler note's and percent texts' minimum widths from the current fonts, so the size never
    // changes; each level's note starts under its checkbox's label.
    void measure() override;
    void applyColours() override;

    GridConfig grid_{};     // shown
    RulerConfig rulers_{};  // shown
    double zoom_ = 0;       // of the last showNotes(); 0 before it: no notes
    double backing_per_dip_ = 1;
    std::uint32_t image_width_ = DEFAULT_EXAMPLE_WIDTH;  // of the last showImageWidth()
    wxNotebook* book_ = nullptr;
    wxCheckBox* rulers_box_ = nullptr;
    wxCheckBox* numbers_box_ = nullptr;
    std::array<LevelRow, GRID_LEVELS> rows_{};
    wxStaticText* hint_ = nullptr;
    wxStaticText* ruler_note_ = nullptr;
    wxStaticText* style_label_ = nullptr;
    // The OverlayStyle values in order; "Custom" last, only while Custom.
    wxChoice* style_ = nullptr;
    wxStaticText* cursor_label_ = nullptr;
    wxColourPickerCtrl* cursor_colour_ = nullptr;  // the rulers' cursor colour
    // The cursor colour before the open dialog's first live change; none while no dialog changed
    // it.
    std::optional<Rgb8> cursor_dialog_colour_;
    wxStaticText* font_label_ = nullptr;
    // "" (System) first, then the OS's families sorted case-insensitively: the items of
    // font_family_, in UTF-8.
    std::vector<std::string> families_;
    wxChoice* font_family_ = nullptr;
    wxSpinCtrl* font_size_ = nullptr;    // RULER_FONT_SIZE_RANGE; the system size while disabled
    wxStaticText* font_unit_ = nullptr;  // "pt"
    wxCheckBox* system_size_ = nullptr;
    wxStaticText* last_value_label_ = nullptr;
    wxRadioButton* last_pixel_ = nullptr;
    wxRadioButton* edge_ = nullptr;
    std::vector<wxStaticText*> headings_;  // the Appearance tab's column headings
    wxTimer save_timer_;  // the quiet period after a scroll's end; the timer gets its own events
    std::chrono::milliseconds save_quiet_;  // the quiet period's length
};

}  // namespace rawreel
