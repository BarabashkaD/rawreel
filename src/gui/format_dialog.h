#pragma once

// The format dialog (design M3 v2 D21, X7, X8, §7): pixel format (catalogue or a JSON file), size,
// fps, the colour standard with its curve and range (D5R §4.4), and the source factory's
// candidates. Shown for the Session's formatNeeded and for View -> Format...; every ShowModal goes
// through showModal() (selftest.h).

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/listbox.h>
#include <wx/spinctrl.h>

#include "core/format_choice.h"
#include "dvv/media/colour_preset.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/source.h"
#include "dvv/media/source_factory.h"
#include "dvv/media/types.h"

namespace rawreel {

// The dialog's header (D5R §4.4): where the format came from.
struct FormatDetection {
    std::string text;      // the detection line, e.g. "Format detected from the Y4M header"
    std::string tooltip;   // the full path (a file) or the URI
    std::string fileName;  // a file's name ("clip.yuv"); empty for a non-file URI
    bool partial = false;  // not or only partly detected: the template lines are shown
};

// "Format of the stream - <short name>": a file's name, else the URI scheme plus its path; a name
// longer than 40 characters is shortened in the middle with "...".
[[nodiscard]] wxString formatDialogTitle(const std::string& uri);
// An open that needs a format (formatNeeded, the error's Format... button): the URI's fmt and size,
// else what parseFileName gives, else "Format not detected: <reason>" (the raw provider's reason
// when it is the best candidate or named in the factory's `reason`, else `reason`).
[[nodiscard]] FormatDetection openDetection(const std::string& uri,
                                            std::span<const dvv::media::Candidate> candidates,
                                            const std::string& reason);
// View > Format of an open stream: the Y4M header, the container, the clip description, the
// sidecar, the file name or the URI.
[[nodiscard]] FormatDetection streamDetection(const dvv::media::SourceInfo& info);

// What the dialog knows about the stream (D5 design §1.8, D5R §4.4).
struct FormatDialogContext {
    bool locked = false;  // Format, size and fps come from the file (D5 design §1.6, T5b)
    // View > Format: what applies without a colour key (FileColourMemory::colourBase). nullopt for
    // an open: colourBaseFromUri(uri, <the format>, <the height>), which follows the controls.
    std::optional<ColourBase> base{};
    std::string uri{};       // an open's request URI (the colour of its clip, sidecar or name)
    std::string provider{};  // SourceInfo::provider ("raw", "y4m", "ffmpeg", ...); "" for an open
    FormatDetection detection{};  // the header lines (empty text: none)
    // The Auto detect box starts checked (the caller remembers it for the session).
    bool autoDetect = false;
};

class FormatDialog final : public wxDialog {
public:
    // `folder`: the clip's folder, where Load JSON... starts unless a JSON file was loaded in this
    // session (then that file's folder, M4 GUI D6); empty lets the system choose.
    // The header lines come from context.detection (D5R §4.4).
    FormatDialog(wxWindow* parent, const wxString& title, const FormatChoice& initial,
                 std::span<const dvv::media::Candidate> candidates, const wxString& folder,
                 const FormatDialogContext& context = {});

    void setChoice(const FormatChoice& choice);  // fills every control (hooks use it too)
    [[nodiscard]] FormatChoice choice() const;   // reads the controls
    // Picks an entry of the Colour list as the user does (hooks).
    void selectColour(int entry);
    // Load JSON... without the file dialog (hooks): a clip description fills the groups it has (on
    // a locked stream only the colour, with the note line), a pixel-format JSON the "@<path>"
    // format; an error goes to the red line and changes nothing (D5 design §1.7).
    void loadJson(const std::string& utf8_path);
    // The red line: the validation message of the current controls (OK disabled), else the last
    // Load JSON error (OK stays enabled); empty when neither (§7 "OK stays disabled").
    [[nodiscard]] wxString errorText() const { return error_->GetLabel(); }
    // The grey note line under the fields (a locked stream's Load JSON); empty when hidden.
    [[nodiscard]] wxString noteText() const;
    // True when Format, Size and Frame rate are read-only "(from file)" rows (D5 design §1.6).
    [[nodiscard]] bool fieldsLocked() const;
    // The read-only rows of a locked stream, "<format>  (from file)|<WxH> @ <fps>  (from file)";
    // empty when not locked.
    [[nodiscard]] wxString lockedText() const;
    [[nodiscard]] wxString sizeText() const { return size_->GetStringSelection(); }
    // The header (D5R §4.4): the detection line, its tooltip and the template lines with their
    // tooltip, "<detection>|<tooltip>|<line 1>|<line 2>|<template tooltip>" (hidden lines empty).
    [[nodiscard]] wxString headerText() const;
    // The Format list's selected entry as shown (e.g. "Custom (from x.json)") and its tooltip.
    [[nodiscard]] wxString formatLabel() const;

    // The controls a selftest hook picks in as the user does.
    enum class Control : std::uint8_t { Format, Size, Fps, Colour, Curve, Range, Candidate };
    // Selects entry `index` of `control` and sends the event a user's pick sends, so the bound
    // handler runs. False (nothing changes) when the control is disabled, hidden or has no such
    // entry.
    bool userPick(Control control, int index);
    bool userPick(Control control, const wxString& item);  // the entry with this exact text
    // Sets the width (or the height) spin to `value` and sends wxEVT_SPINCTRL as a user's spin
    // does. False when the spin is disabled or hidden.
    bool userSpin(bool height, int value);
    // Sets the Auto detect box and sends wxEVT_CHECKBOX as a user's click does.
    void userAutoDetect(bool on);
    // The Auto detect box; an untagged clip's standard is then guessed from the height.
    [[nodiscard]] bool autoDetect() const noexcept { return auto_detect_; }
    [[nodiscard]] wxString colourText() const { return colour_->GetStringSelection(); }
    // The options under the Colour list, "<curve>|<range>|<range enabled 1/0>": the curve is the
    // PQ/HLG choice for BT.2100, else the read-only curve text; e.g. "BT.1886|Limited|1".
    [[nodiscard]] wxString colourOptionsText() const;
    // The read-only values line, e.g. "Matrix BT.709, primaries BT.709 (not converted)".
    [[nodiscard]] wxString colourValuesText() const { return values_->GetLabel(); }
    // The size-mismatch hint under the values line; empty when hidden.
    [[nodiscard]] wxString colourHintText() const;
    // True when the first child that can take the focus is the Colour list (what wx focuses when
    // the dialog opens): on a locked stream Enter then presses OK, not Load JSON.
    [[nodiscard]] bool colourTakesInitialFocus() const;

private:
    void validate();  // after every change
    // A user's change of a control: the last Load JSON error no longer applies.
    void userChanged() { load_error_.clear(); }
    void onColour();  // a Colour list pick: BT.2100 takes the base's curve, then refreshColour()
    void onLoadJson();
    void onCandidate();
    void onSizePreset();
    void onSize();  // a spin control changed: the size choice follows, then refreshColour()
    void onFps(const wxString& text);
    void selectFormat(const std::string& format);
    // The selected format as choice() writes it: a catalogue name or "@<path>" (the Custom entry).
    [[nodiscard]] std::string selectedFormat() const;
    void setSize(std::uint32_t width, std::uint32_t height);  // the spins and the size choice
    void syncSizePreset();  // the size choice follows the spins: the matching preset, else Custom
    void setFps(dvv::media::Rational fps);
    // The Colour list entry, the curve and the range of a colour (no key: the base's).
    void setColour(const ColourChoice& colour);
    void showLockedRows();  // the "(from file)" texts of the current format, size and fps
    // After a format, size, Colour, curve or range change: the list labels, the options, the
    // values line and the hint; then validate().
    void refreshColour();
    // The Auto detect box changed: a pick that wrote no key moves to the new base's entry.
    void onAutoDetect();
    // What applies without a colour key for the current format and height (D5R §4.4); with Auto
    // detect on, an untagged clip's guessed standard (autoBase, origin Auto).
    [[nodiscard]] ColourBase colourBase() const;
    // The same without Auto detect: what the stream renders with when no colour key is written.
    [[nodiscard]] ColourBase plainColourBase() const;
    // The standard the list and the curve choice select; nullopt for "From file (no standard)".
    [[nodiscard]] std::optional<dvv::media::ColourPreset> selectedStandard() const;
    [[nodiscard]] dvv::media::ColourModel model() const;  // of the selected format (cached)
    // Shows or hides a grey line under the fields; the dialog grows if it must.
    void setLine(wxStaticText& line, const wxString& text);

    std::vector<dvv::media::Candidate> candidates_;
    wxString folder_;  // the clip's folder (Load JSON...)
    FormatDialogContext context_;
    // formatModel() of model_format_ (an "@<json>" format reads its file once per name).
    mutable std::string model_format_;
    mutable dvv::media::ColourModel model_ = dvv::media::ColourModel::Yuv;
    bool no_standard_ = false;  // the Colour list starts with "From file (no standard)"
    bool auto_detect_ = false;  // the Auto detect box
    // The base standard the list labels last showed (a guess follows the height).
    std::optional<dvv::media::ColourPreset> listed_base_;
    // The user (or a range= key) picked the range; else the Range choice follows
    // the base's range, which changes with the format.
    bool range_picked_ = false;
    wxCheckBox* auto_ = nullptr;
    // Child windows: wx owns them.
    wxChoice* format_ = nullptr;
    int json_item_ = wxNOT_FOUND;  // the "Custom (from <file>)" entry of format_, when there is one
    std::string json_path_;        // its UTF-8 path ("@" + json_path_ is written)
    // Its interned format (formatFromJson), whose geometry validate() checks; the error otherwise.
    dvv::Result<dvv::media::FormatId> json_format_{dvv::media::FormatId{}};
    wxChoice* size_ = nullptr;  // the size presets, then Custom
    wxSpinCtrl* width_ = nullptr;
    wxSpinCtrl* height_ = nullptr;
    wxComboBox* fps_ = nullptr;
    // The read-only rows of a locked stream (D5 design §1.6); hidden otherwise.
    wxStaticText* format_text_ = nullptr;
    wxStaticText* size_text_ = nullptr;  // "<W>x<H> @ <fps>  (from file)"
    // The header (D5R §4.4); hidden lines stay null or hidden.
    wxStaticText* detection_ = nullptr;
    wxStaticText* template1_ = nullptr;
    wxStaticText* template2_ = nullptr;
    wxStaticText* note_ = nullptr;  // the locked stream's Load JSON note
    wxString load_error_;           // the last Load JSON error (the red line when valid)
    wxChoice* colour_ = nullptr;
    wxChoice* curve_ = nullptr;           // PQ, HLG: shown for BT.2100 only
    wxStaticText* curve_text_ = nullptr;  // the read-only curve of the other entries
    wxChoice* range_ = nullptr;           // Limited, Full ("(not standard)" off BT.2100)
    wxStaticText* values_ = nullptr;      // the read-only values line
    wxStaticText* hint_ = nullptr;        // the size-mismatch hint; hidden when empty
    wxListBox* list_ = nullptr;
    wxStaticText* error_ = nullptr;
    wxButton* ok_ = nullptr;
};

}  // namespace rawreel
