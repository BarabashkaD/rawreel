// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/format_dialog.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <wx/artprov.h>
#include <wx/filedlg.h>
#include <wx/statbmp.h>

#include "core/format_presets.h"
#include "dvv/base/error.h"
#include "dvv/base/json.h"
#include "dvv/base/uri.h"
#include "dvv/media/clip_json.h"
#include "dvv/media/colour_preset.h"
#include "dvv/media/display.h"
#include "dvv/media/file_name.h"
#include "dvv/media/format_json.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/types.h"

namespace rawreel {

namespace {

using dvv::media::ColourModel;
using dvv::media::ColourParams;
using dvv::media::ColourPreset;
using dvv::media::ColourRange;

// The Range choice's entries in order (on every standard).
constexpr std::array RANGES{ColourRange::Limited, ColourRange::Full};
// The Curve choice's entries in order (BT.2100 only).
constexpr std::array CURVES{ColourPreset::Bt2100Pq, ColourPreset::Bt2100Hlg};

constexpr int MAX_SIZE = 65535;

wxString fromUtf8(std::string_view text) {
    return wxString::FromUTF8(text.data(), text.size());
}

bool isBt2100(std::optional<ColourPreset> standard) {
    return standard == ColourPreset::Bt2100Pq || standard == ColourPreset::Bt2100Hlg;
}

void showWindow(wxWindow& window, bool show) {
    if (window.IsShown() != show) {
        window.Show(show);
    }
}

std::string fpsText(dvv::media::Rational fps) {
    return fps.den == 1 ? std::format("{}", fps.num) : std::format("{}/{}", fps.num, fps.den);
}

wxString candidateText(const dvv::media::Candidate& candidate) {
    return wxString::FromUTF8(
        std::format("{} % {}: {}", candidate.confidence, candidate.component, candidate.why));
}

// The folder of the last JSON file loaded in this session (GUI thread only).
wxString& lastJsonFolder() {
    static wxString folder;
    return folder;
}

constexpr std::size_t TITLE_MAX = 40;  // D5R §4.4: longer names are shortened in the middle
constexpr std::string_view FROM_FILE = "  (from file)";
constexpr std::string_view TEMPLATE_LINE1 = "Name the file <name>_WxH[_fmt][_fps]";
constexpr std::string_view TEMPLATE_LINE2 = "Example: clip_1920x1080_yuv420p_25.yuv";

std::string baseName(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

// "a", "a and b", "a, b and c".
std::string joinItems(const std::vector<std::string_view>& items) {
    std::string text;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            text += i + 1 == items.size() ? " and " : ", ";
        }
        text += items[i];
    }
    return text;
}

// D5R §4.4: what the file name gave, "Format detected from the file name" when it gave format,
// size and frame rate, else "<What> detected from the file name; <rest> not detected".
FormatDetection nameDetection(const dvv::media::FileNameInfo& name) {
    std::vector<std::string_view> what{"size"};
    std::vector<std::string_view> rest;
    (name.format ? what : rest).emplace_back("format");
    (name.fps ? what : rest).emplace_back("frame rate");
    if (name.colour) {
        what.emplace_back("colour");  // colour has a default: never "not detected"
    }
    FormatDetection detection;
    if (rest.empty()) {
        detection.text = "Format detected from the file name";
        return detection;
    }
    std::string text = joinItems(what);
    text.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(text.front())));
    detection.text =
        std::format("{} detected from the file name; {} not detected", text, joinItems(rest));
    detection.partial = true;
    return detection;
}

// The raw provider's part of the factory's "no source provider can open '<uri>': raw: <why>;
// y4m: <why>" text; nullopt when it names no raw part.
std::optional<std::string> rawReason(std::string_view reason) {
    for (const std::string_view marker :
         {std::string_view("': raw: "), std::string_view("; raw: ")}) {
        if (const std::size_t at = reason.find(marker); at != std::string_view::npos) {
            const std::string_view rest = reason.substr(at + marker.size());
            return std::string(rest.substr(0, rest.find("; ")));
        }
    }
    return std::nullopt;
}

// The tooltip and file name of a URI: a file's full path and name, else the URI.
FormatDetection uriDetection(const std::string& uri, const dvv::Result<dvv::Uri>& parsed) {
    FormatDetection detection;
    if (parsed && parsed->scheme == "file" && !parsed->path.empty()) {
        detection.tooltip = parsed->path;
        detection.fileName = baseName(parsed->path);
    } else {
        detection.tooltip = uri;
    }
    return detection;
}

// A pixel-format JSON file interned (D5R §4.4: the dialog checks its width/height rule).
dvv::Result<dvv::media::FormatId> pixelFormatFromFile(const std::string& utf8_path) {
    DVV_TRY(const std::string text, dvv::media::readSmallFile(utf8_path));
    const dvv::Result<dvv::json::Value> root = dvv::json::parse(text);
    if (!root) {
        return dvv::makeError(dvv::Errc::Format,
                              std::format("{}: not JSON: {}", utf8_path, root.error().message));
    }
    dvv::Result<dvv::media::FormatId> format = dvv::media::formatFromJson(*root);
    if (!format) {
        return dvv::makeError(dvv::Errc::Format,
                              std::format("{}: {}", baseName(utf8_path), format.error().message));
    }
    return format;
}

}  // namespace

wxString formatDialogTitle(const std::string& uri) {
    const dvv::Result<dvv::Uri> parsed = dvv::parseUri(uri);
    std::string name = uri;
    if (parsed && parsed->scheme == "file" && !parsed->path.empty()) {
        name = baseName(parsed->path);
    } else if (parsed && !parsed->path.empty()) {
        name = parsed->scheme + ":" + parsed->path;  // a non-file URI: the scheme plus the path
    }
    wxString shown = fromUtf8(name);
    if (shown.length() > TITLE_MAX) {
        const std::size_t keep = TITLE_MAX - 3;
        shown = shown.Left(keep - keep / 2) + "..." + shown.Right(keep / 2);
    }
    return "Format of the stream - " + shown;
}

FormatDetection openDetection(const std::string& uri,
                              std::span<const dvv::media::Candidate> candidates,
                              const std::string& reason) {
    const dvv::Result<dvv::Uri> parsed = dvv::parseUri(uri);
    FormatDetection detection = uriDetection(uri, parsed);
    if (parsed && parsed->param("fmt") && parsed->param("size")) {
        detection.text = "Format given in the URI";
        return detection;
    }
    if (!detection.fileName.empty()) {
        if (const auto name = dvv::media::parseFileName(parsed->path)) {
            FormatDetection from_name = nameDetection(*name);
            detection.text = std::move(from_name.text);
            detection.partial = from_name.partial;
            return detection;
        }
    }
    std::string why = reason;
    if (!candidates.empty() && candidates.front().component == "raw") {
        why = candidates.front().why;
    } else if (const std::optional<std::string> raw = rawReason(reason)) {
        why = *raw;
    }
    detection.text = why.empty() ? std::string("Format not detected")
                                 : std::format("Format not detected: {}", why);
    detection.partial = !detection.fileName.empty();  // the template is about file names
    return detection;
}

namespace {

// A clip description that gave the format or the size (a colour-only one did
// not detect the format).
bool clipGeometry(const std::string& clip_file) {
    if (clip_file.empty()) {
        return false;
    }
    const dvv::Result<dvv::media::ClipDescription> loaded = dvv::media::loadClipJson(clip_file);
    return loaded && (loaded->format || loaded->size);
}

}  // namespace

FormatDetection streamDetection(const dvv::media::SourceInfo& info) {
    const dvv::Result<dvv::Uri> parsed = dvv::parseUri(info.resolvedUri);
    FormatDetection detection = uriDetection(info.resolvedUri, parsed);
    if (info.provider == "y4m") {
        detection.text = "Format detected from the Y4M header";
    } else if (info.provider == "ffmpeg") {
        // SourceInfo carries no demuxer name: the file extension in upper case (MKV, AVI, ...).
        const std::size_t dot = detection.fileName.find_last_of('.');
        std::string container =
            dot == std::string::npos ? std::string() : detection.fileName.substr(dot + 1);
        std::ranges::transform(container, container.begin(), [](char c) {
            return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        });
        detection.text = container.empty()
                             ? std::string("Format detected from the container")
                             : std::format("Format detected from the container ({})", container);
    } else if (parsed && parsed->param("clip") && clipGeometry(info.clipFile)) {
        detection.text =
            std::format("Format detected from the clip description ({})", baseName(info.clipFile));
    } else if (clipGeometry(info.clipFile)) {
        detection.text = std::format("Format detected from {}", baseName(info.clipFile));
    } else {
        detection.text = "Format given in the URI";
        // The file name's format and size are the stream's: the name gave them.
        std::optional<dvv::media::FileNameInfo> name;
        if (!detection.fileName.empty()) {
            name = dvv::media::parseFileName(parsed->path);
        }
        if (name && name->format == info.desc.format && name->width == info.desc.width &&
            name->height == info.desc.height && (!name->fps || *name->fps == info.fps)) {
            FormatDetection from_name = nameDetection(*name);
            detection.text = std::move(from_name.text);
            detection.partial = from_name.partial;
        }
    }
    return detection;
}

FormatDialog::FormatDialog(wxWindow* parent, const wxString& title, const FormatChoice& initial,
                           std::span<const dvv::media::Candidate> candidates,
                           const wxString& folder, const FormatDialogContext& context)
    // Not resizable (D5R §4.4); the grow-only logic of setLine() stays for content changes.
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE),
      candidates_(candidates.begin(), candidates.end()),
      folder_(folder),
      context_(context) {
    auto* top = new wxBoxSizer(wxVERTICAL);
    // The header (D5R §4.4): the detection line with the full path as its tooltip, then, when the
    // format is not or only partly detected, the two grey template lines with the (i) icon.
    const FormatDetection& detection = context_.detection;
    if (!detection.text.empty()) {
        detection_ = new wxStaticText(this, wxID_ANY, fromUtf8(detection.text));
        detection_->Wrap(FromDIP(460));
        detection_->SetToolTip(fromUtf8(detection.tooltip));
        top->Add(detection_, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP));
    }
    if (detection.partial) {
        const wxString tip = fromUtf8(std::format(
            "WxH  frame size, e.g. 1920x1080 (required)\n"
            "fmt  pixel format, e.g. yuv420p, nv12, yuv422p10le\n"
            "fps  frame rate, e.g. 25, 29.97 or 50fps\n"
            "_bt709 etc. at the end sets the colour: bt601-ntsc, bt601-pal, bt709, bt2020, "
            "bt2100-pq, bt2100-hlg\n"
            "Or put a clip description {}.json next to the file.",
            detection.fileName.empty() ? std::string("clip.yuv") : detection.fileName));
        const wxColour grey = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
        template1_ = new wxStaticText(this, wxID_ANY, fromUtf8(TEMPLATE_LINE1));
        template2_ = new wxStaticText(this, wxID_ANY, fromUtf8(TEMPLATE_LINE2));
        auto* icon = new wxStaticBitmap(
            this, wxID_ANY,
            wxArtProvider::GetBitmapBundle(wxART_INFORMATION, wxART_OTHER, wxSize(16, 16)));
        for (wxWindow* window :
             {static_cast<wxWindow*>(template1_), static_cast<wxWindow*>(template2_),
              static_cast<wxWindow*>(icon)}) {
            window->SetForegroundColour(grey);
            window->SetToolTip(tip);
        }
        auto* line1 = new wxBoxSizer(wxHORIZONTAL);
        line1->Add(template1_, wxSizerFlags().CenterVertical());
        line1->Add(icon, wxSizerFlags().CenterVertical().Border(wxLEFT, FromDIP(4)));
        top->Add(line1, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP));
        top->Add(template2_, wxSizerFlags().Border(wxLEFT | wxRIGHT));
    }

    auto* grid = new wxFlexGridSizer(2, FromDIP(wxSize(8, 6)));
    grid->AddGrowableCol(1);
    const auto row = [this, grid](const wxString& label, wxWindow* control) {
        grid->Add(new wxStaticText(this, wxID_ANY, label), wxSizerFlags().CenterVertical());
        grid->Add(control, wxSizerFlags().Expand());
    };
    const auto row_sizer = [this, grid](const wxString& label, wxSizer* sizer) {
        grid->Add(new wxStaticText(this, wxID_ANY, label), wxSizerFlags().CenterVertical());
        grid->Add(sizer, wxSizerFlags().Expand());
    };

    // Format: every catalogue name, then the "@<path>" entry once one is loaded.
    format_ = new wxChoice(this, wxID_ANY);
    for (const dvv::media::FormatId id : dvv::media::catalogue()) {
        format_->Append(wxString::FromUTF8(dvv::media::descriptor(id).name));
    }
    // A locked stream (D5 design §1.6) shows Format, Size and Frame rate as "(from file)" texts;
    // the hidden, disabled controls still hold the stream's values, which OK writes.
    format_text_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    auto* load = new wxButton(this, wxID_ANY, "Load JSON...");
    auto* format_row = new wxBoxSizer(wxHORIZONTAL);
    format_row->Add(format_, wxSizerFlags(1).CenterVertical());
    format_row->Add(format_text_, wxSizerFlags(1).CenterVertical());
    format_row->Add(load, wxSizerFlags().CenterVertical().Border(wxLEFT));
    row_sizer("Format:", format_row);

    // Size and frame rate on one line (D5R §4.4): [preset] [W] x [H] @ [fps]. The size choice
    // keeps its best width (its longest label); the fps combo keeps free typing (§1.4).
    size_ = new wxChoice(this, wxID_ANY);
    for (const SizePreset& preset : sizePresets()) {
        size_->Append(fromUtf8(sizeLabel(preset)));
    }
    size_->Append(fromUtf8(CUSTOM_LABEL));
    size_text_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
#if defined(__WXGTK__)
    // A GTK 3 spin button draws its - and + buttons beside the entry; 72 DIP is less than its
    // minimum (Gtk-CRITICAL "size >= 0" in GtkSpinButton), so GTK gets its best width.
    const wxSize spin_size = wxDefaultSize;
#else
    const wxSize spin_size = FromDIP(wxSize(72, -1));
#endif
    width_ = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, spin_size,
                            wxSP_ARROW_KEYS, 1, MAX_SIZE, 1920);
    height_ = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, spin_size,
                             wxSP_ARROW_KEYS, 1, MAX_SIZE, 1080);
    fps_ = new wxComboBox(this, wxID_ANY, "25", wxDefaultPosition, FromDIP(wxSize(90, -1)));
    for (const std::string_view entry : frameRatePresets()) {
        fps_->Append(fromUtf8(entry));
    }
    fps_->SetHint("N, N/D or a decimal");
    auto* size_row = new wxBoxSizer(wxHORIZONTAL);
    size_row->Add(size_, wxSizerFlags().CenterVertical());
    size_row->Add(width_, wxSizerFlags().CenterVertical().Border(wxLEFT));
    auto* times = new wxStaticText(this, wxID_ANY, "x");
    size_row->Add(times, wxSizerFlags().CenterVertical().Border(wxLEFT | wxRIGHT));
    size_row->Add(height_, wxSizerFlags().CenterVertical());
    auto* at = new wxStaticText(this, wxID_ANY, "@");
    size_row->Add(at, wxSizerFlags().CenterVertical().Border(wxLEFT | wxRIGHT));
    size_row->Add(fps_, wxSizerFlags(1).CenterVertical());
    size_row->Add(size_text_, wxSizerFlags(1).CenterVertical());
    row_sizer("Size:", size_row);
    if (context_.locked) {
        // One text, "3840x2160 @ 50  (from file)"; the hidden, disabled controls keep the values.
        for (wxWindow* control : {static_cast<wxWindow*>(format_), static_cast<wxWindow*>(size_),
                                  static_cast<wxWindow*>(width_), static_cast<wxWindow*>(height_),
                                  static_cast<wxWindow*>(fps_)}) {
            control->Disable();
        }
        format_row->Hide(format_);
        for (wxWindow* control : {static_cast<wxWindow*>(size_), static_cast<wxWindow*>(width_),
                                  static_cast<wxWindow*>(times), static_cast<wxWindow*>(height_),
                                  static_cast<wxWindow*>(at), static_cast<wxWindow*>(fps_)}) {
            size_row->Hide(control);
        }
    } else {
        format_row->Hide(format_text_);
        size_row->Hide(size_text_);
    }

    // Colour (D5R §4.4): "From file (no standard)" when the file's colour matches no standard,
    // then the five standards, the one that applies without a key with its origin suffix. The
    // mouse wheel keeps the OS default: nothing is bound.
    colour_ = new wxChoice(this, wxID_ANY);
    auto_detect_ = context_.autoDetect;  // Auto detect: remembered for the session by the caller
    const ColourBase base = colourBase();
    no_standard_ = !base.standard;
    for (const std::string& label : colourListLabels(base)) {
        colour_->Append(fromUtf8(label));
    }
    colour_->SetSelection(0);
    colour_->SetMinSize(FromDIP(wxSize(300, -1)));  // room for " (from <clip>.json)"
    row("Colour:", colour_);
    if (context_.locked) {
        // Format, Size and fps are disabled, so Load JSON would be the first focusable child and
        // take Enter on MSW: the Colour list comes first instead, and Enter
        // presses the default OK button.
        load->MoveAfterInTabOrder(colour_);
    }

    // The options: Curve, a PQ/HLG choice for BT.2100, else read-only; Range on every
    // standard. Under them the read-only values line and the size-mismatch hint.
    curve_ = new wxChoice(this, wxID_ANY);
    curve_->Append("PQ");
    curve_->Append("HLG");
    curve_->SetSelection(0);
    curve_text_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    range_ = new wxChoice(this, wxID_ANY);
    for (const ColourRange range : RANGES) {
        range_->Append(fromUtf8(rangeLabel(range)));
    }
    range_->SetSelection(0);
    auto* options_row = new wxBoxSizer(wxHORIZONTAL);
    options_row->Add(new wxStaticText(this, wxID_ANY, "Curve"), wxSizerFlags().CenterVertical());
    options_row->Add(curve_, wxSizerFlags().CenterVertical().Border(wxLEFT));
    options_row->Add(curve_text_, wxSizerFlags().CenterVertical().Border(wxLEFT));
    options_row->Add(new wxStaticText(this, wxID_ANY, "Range"),
                     wxSizerFlags().CenterVertical().Border(wxLEFT, FromDIP(16)));
    options_row->Add(range_, wxSizerFlags().CenterVertical().Border(wxLEFT));
    // Auto detect: off by default; an untagged clip then gets the standard its height suggests.
    auto_ = new wxCheckBox(this, wxID_ANY, "Auto detect");
    auto_->SetValue(auto_detect_);
    auto_->SetToolTip(
        "Guess the standard of an untagged clip from its height: up to 480 lines "
        "BT.601 (NTSC), up to 576 lines BT.601 (PAL), else BT.709");
    options_row->Add(auto_, wxSizerFlags().CenterVertical().Border(wxLEFT, FromDIP(16)));
    grid->AddSpacer(0);
    grid->Add(options_row, wxSizerFlags().Expand());
    values_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    values_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    grid->AddSpacer(0);
    grid->Add(values_, wxSizerFlags().Expand());
    hint_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    hint_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    grid->AddSpacer(0);
    grid->Add(hint_, wxSizerFlags().Expand());
    hint_->Hide();
    top->Add(grid, wxSizerFlags().Expand().Border());
    note_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    note_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    top->Add(note_, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT));
    note_->Hide();

    if (!candidates_.empty() && !context_.locked) {  // a locked stream has no candidate list
        top->Add(new wxStaticText(this, wxID_ANY, "Candidates:"),
                 wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP));
        list_ = new wxListBox(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(460, 90)));
        for (const dvv::media::Candidate& candidate : candidates_) {
            list_->Append(candidateText(candidate));
        }
        top->Add(list_, wxSizerFlags().Expand().Border());
        list_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& /*event*/) {
            userChanged();  // a candidate pick drops a stale Load JSON error
            onCandidate();
        });
    }

    error_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    error_->SetForegroundColour(*wxRED);
    top->Add(error_, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT));
    top->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), wxSizerFlags().Expand().Border());
    ok_ = dynamic_cast<wxButton*>(FindWindow(wxID_OK));

    // Every user change first drops a stale Load JSON error. ChangeValue,
    // SetValue and SetSelection send no event, so the dialog's own updates keep it.
    // The format, the size and the colour options drive the colour line (the base's values).
    const auto colour_changed = [this](wxCommandEvent& /*event*/) {
        userChanged();
        refreshColour();
    };
    const auto size_changed = [this](wxEvent& /*event*/) {
        userChanged();
        onSize();
    };
    format_->Bind(wxEVT_CHOICE, colour_changed);
    // SetValue and SetSelection send no event, so the size choice and the spins do not loop.
    size_->Bind(wxEVT_CHOICE, [this](wxCommandEvent& /*event*/) {
        userChanged();
        onSizePreset();
    });
    width_->Bind(wxEVT_SPINCTRL, size_changed);
    height_->Bind(wxEVT_SPINCTRL, size_changed);
    width_->Bind(wxEVT_TEXT, size_changed);
    height_->Bind(wxEVT_TEXT, size_changed);
    fps_->Bind(wxEVT_TEXT, [this](wxCommandEvent& /*event*/) {
        userChanged();
        onFps(fps_->GetValue());
    });
    // The selected entry's text (GetValue() may still hold the old text in this event).
    fps_->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& event) {
        userChanged();
        onFps(event.GetString());
    });
    colour_->Bind(wxEVT_CHOICE, [this](wxCommandEvent& /*event*/) {
        userChanged();
        onColour();
    });
    curve_->Bind(wxEVT_CHOICE, colour_changed);
    range_->Bind(wxEVT_CHOICE, [this, colour_changed](wxCommandEvent& event) {
        range_picked_ = true;  // a picked range stays when the format changes
        colour_changed(event);
    });
    auto_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& /*event*/) {
        userChanged();
        onAutoDetect();
    });
    load->Bind(wxEVT_BUTTON, [this](wxCommandEvent& /*event*/) { onLoadJson(); });

    SetSizerAndFit(top);
    setChoice(initial);
    // Room for the longest validation message without a relayout.
    SetMinSize(GetSize());
#if defined(__WXGTK__)
    fixed_client_ = GetClientSize();
    Bind(wxEVT_SIZE, &FormatDialog::keepFixedSize, this);
#endif
    CentreOnParent();
}

#if defined(__WXGTK__)
void FormatDialog::keepFixedSize(wxSizeEvent& event) {
    event.Skip();
    if (restore_pending_ || GetClientSize() == fixed_client_) {
        return;
    }
    restore_pending_ = true;
    CallAfter([this] {
        restore_pending_ = false;
        if (GetClientSize() != fixed_client_) {
            SetClientSize(fixed_client_);
            Layout();
        }
    });
}
#endif

void FormatDialog::selectFormat(const std::string& format) {
    const wxString name = wxString::FromUTF8(format);
    if (format.starts_with('@')) {
        // D5R §4.4: one entry "Custom (from <file>.json)", appended or replaced, its full path
        // the tooltip (refreshColour()); the interned format's geometry is checked by validate().
        json_path_ = format.substr(1);
        json_format_ = pixelFormatFromFile(json_path_);
        const wxString label = fromUtf8(std::format("Custom (from {})", baseName(json_path_)));
        if (json_item_ == wxNOT_FOUND) {
            json_item_ = format_->Append(label);
        } else {
            format_->SetString(static_cast<unsigned int>(json_item_), label);
        }
        format_->SetSelection(json_item_);
        return;
    }
    int index = format_->FindString(name, true);
    if (index == wxNOT_FOUND) {
        index = format_->Append(name);  // not in the catalogue: validation names the problem
    }
    format_->SetSelection(index);
}

std::string FormatDialog::selectedFormat() const {
    if (json_item_ != wxNOT_FOUND && format_->GetSelection() == json_item_) {
        return "@" + json_path_;
    }
    return format_->GetStringSelection().utf8_string();
}

wxString FormatDialog::formatLabel() const {
    return format_->GetStringSelection() + "|" + format_->GetToolTipText();
}

wxString FormatDialog::headerText() const {
    const auto shown = [](const wxWindow* line, std::string_view text) {
        return line != nullptr && line->IsShown() ? fromUtf8(text) : wxString();
    };
    const FormatDetection& detection = context_.detection;
    return shown(detection_, detection.text) + "|" + shown(detection_, detection.tooltip) + "|" +
           shown(template1_, TEMPLATE_LINE1) + "|" + shown(template2_, TEMPLATE_LINE2) + "|" +
           (template1_ != nullptr ? template1_->GetToolTipText() : wxString());
}

void FormatDialog::setChoice(const FormatChoice& choice) {
    selectFormat(choice.format);
    setSize(choice.width, choice.height);
    setFps(choice.fps);
    setColour(choice.colour);
    showLockedRows();
    refreshColour();  // shows a preset's or entry 0's values, enables, validates
}

void FormatDialog::setSize(std::uint32_t width, std::uint32_t height) {
    width_->SetValue(static_cast<int>(std::min<std::uint32_t>(width, MAX_SIZE)));
    height_->SetValue(static_cast<int>(std::min<std::uint32_t>(height, MAX_SIZE)));
    syncSizePreset();
}

void FormatDialog::syncSizePreset() {
    const std::optional<std::size_t> preset =
        findSizePreset(static_cast<std::uint32_t>(std::max(width_->GetValue(), 0)),
                       static_cast<std::uint32_t>(std::max(height_->GetValue(), 0)));
    size_->SetSelection(preset ? static_cast<int>(*preset)
                               : static_cast<int>(size_->GetCount()) - 1);  // Custom
}

void FormatDialog::setFps(dvv::media::Rational fps) {
    fps_->ChangeValue(wxString::FromUTF8(fpsText(fps)));
}

void FormatDialog::onSizePreset() {
    const auto presets = sizePresets();
    const int index = size_->GetSelection();
    if (index >= 0 && static_cast<std::size_t>(index) < presets.size()) {
        const SizePreset& preset = presets[static_cast<std::size_t>(index)];
        setSize(preset.width, preset.height);
    }  // Custom keeps the spins
    refreshColour();
}

void FormatDialog::onSize() {
    syncSizePreset();  // the spins keep the typed text
    refreshColour();
}

void FormatDialog::onFps(const wxString& /*text*/) {
    validate();  // a bad fps text shows on the red line (D5R §4.4: no "= N fps" label)
}

void FormatDialog::showLockedRows() {
    if (!context_.locked) {
        return;
    }
    const FormatChoice shown = choice();
    format_text_->SetLabel(fromUtf8(std::format("{}{}", shown.format, FROM_FILE)));
    size_text_->SetLabel(fromUtf8(
        std::format("{}x{} @ {}{}", shown.width, shown.height, fpsText(shown.fps), FROM_FILE)));
}

wxString FormatDialog::noteText() const {
    return note_->IsShown() ? note_->GetLabel() : wxString();
}

bool FormatDialog::fieldsLocked() const {
    return !format_->IsEnabled() && !size_->IsEnabled() && !width_->IsEnabled() &&
           !height_->IsEnabled() && !fps_->IsEnabled() && format_text_->IsShown() &&
           size_text_->IsShown() && !fps_->IsShown();
}

wxString FormatDialog::lockedText() const {
    if (!context_.locked) {
        return {};
    }
    return format_text_->GetLabel() + "|" + size_text_->GetLabel();
}

void FormatDialog::setColour(const ColourChoice& colour) {
    // D5R §4.4 "Reading a URI": colour=T selects T, no key the base's entry; range= sets the
    // range, else the base's.
    const ColourBase base = colourBase();
    const std::optional<ColourPreset> standard = colour.standard ? colour.standard : base.standard;
    const int offset = no_standard_ ? 1 : 0;
    colour_->SetSelection(standard ? offset + static_cast<int>(standardEntry(*standard)) : 0);
    if (isBt2100(standard)) {
        curve_->SetSelection(standard == ColourPreset::Bt2100Hlg ? 1 : 0);
    }
    const ColourRange range =
        colour.range != ColourRange::Unspecified ? colour.range : base.values.range;
    range_->SetSelection(range == ColourRange::Full ? 1 : 0);
    range_picked_ = colour.range != ColourRange::Unspecified;
}

FormatChoice FormatDialog::choice() const {
    FormatChoice choice;
    choice.format = selectedFormat();
    choice.width = static_cast<std::uint32_t>(std::max(width_->GetValue(), 0));
    choice.height = static_cast<std::uint32_t>(std::max(height_->GetValue(), 0));
    // Unparseable: 0/1, which validateChoice refuses (validate() names the fps rule first).
    choice.fps = parseFpsText(fps_->GetValue().utf8_string()).value_or(dvv::media::Rational{0, 1});
    // What OK writes (D5R §4.4): no key when the pick equals what applies without one.
    const ColourRange range =
        range_->GetSelection() == 1 ? ColourRange::Full : ColourRange::Limited;
    // A guessed standard is written as colour=<token>, because without a key the stream
    // renders with the plain base (BT.709).
    const ColourBase base = colourBase();
    const bool guessed = base.origin == ColourOrigin::Auto;
    choice.colour = keyedColour(selectedStandard(), range, guessed ? plainColourBase() : base);
    choice.colour.autoDetect =
        guessed && choice.colour.standard && choice.colour.standard == base.standard;
    return choice;
}

void FormatDialog::onAutoDetect() {
    const ColourRange range =
        range_->GetSelection() == 1 ? ColourRange::Full : ColourRange::Limited;
    // The box is about the standard only: a selection without a colour key follows it, and a
    // range pick stays, as when a guess follows a new height (refreshColour).
    const bool was_base = selectedStandard() == colourBase().standard;
    auto_detect_ = auto_->GetValue();
    listed_base_ = colourBase().standard;  // the box moves the selection only as below
    if (was_base) {
        const bool picked = range_picked_;
        setColour(ColourChoice{.range = range});  // the entry that applies without a key
        range_picked_ = picked;
    }
    refreshColour();
}

bool FormatDialog::userSpin(bool height, int value) {
    wxSpinCtrl* const spin = height ? height_ : width_;
    if (!spin->IsEnabled() || !spin->IsShown()) {
        return false;
    }
    spin->SetValue(value);  // sends no event
    wxSpinEvent event(wxEVT_SPINCTRL, spin->GetId());
    event.SetEventObject(spin);
    event.SetPosition(spin->GetValue());
    spin->ProcessWindowEvent(event);
    return true;
}

void FormatDialog::userAutoDetect(bool on) {
    auto_->SetValue(on);  // sends no event
    wxCommandEvent event(wxEVT_CHECKBOX, auto_->GetId());
    event.SetEventObject(auto_);
    event.SetInt(on ? 1 : 0);
    auto_->ProcessWindowEvent(event);
}

void FormatDialog::selectColour(int entry) {
    colour_->SetSelection(entry);
    onColour();
}

ColourModel FormatDialog::model() const {
    const std::string format = selectedFormat();
    if (format != model_format_) {
        model_format_ = format;
        model_ = formatModel(format);
    }
    return model_;
}

ColourBase FormatDialog::colourBase() const {
    ColourBase base = plainColourBase();  // not const: the return below moves it
    if (!auto_detect_) {
        return base;  // no automatic change without the Auto detect box
    }
    return autoBase(base, static_cast<std::uint32_t>(std::max(height_->GetValue(), 1)));
}

ColourBase FormatDialog::plainColourBase() const {
    const std::string format = selectedFormat();
    const auto height = static_cast<std::uint32_t>(std::max(height_->GetValue(), 1));
    if (!context_.base) {
        return colourBaseFromUri(context_.uri, format, height);
    }
    ColourBase base = *context_.base;
    if (base.origin == ColourOrigin::Default) {
        const ColourRange file_range = base.values.range;
        base.values = defaultColour(format, height).params;  // follows the format and height
        if (base.rangeFromFile) {
            base.values.range = file_range;  // a range the file gives stays (D5R §1.3)
        }
    } else if ((base.origin == ColourOrigin::FileName || base.origin == ColourOrigin::Clip) &&
               base.standard) {
        // The name's or clip's standard on the new format and height.
        const ColourRange file_range = base.values.range;
        ColourBase plain{};
        plain.values = defaultColour(format, height).params;
        base.values = colourValues(ColourChoice{.standard = base.standard}, plain, model());
        if (base.rangeFromFile) {
            base.values.range = file_range;
        }
    }
    return base;
}

std::optional<ColourPreset> FormatDialog::selectedStandard() const {
    const int entry = colour_->GetSelection() - (no_standard_ ? 1 : 0);
    const auto standards = standardEntries();
    if (entry < 0 || static_cast<std::size_t>(entry) >= standards.size()) {
        return std::nullopt;  // "From file (no standard)"
    }
    const ColourPreset standard = standards[static_cast<std::size_t>(entry)];
    if (isBt2100(standard)) {
        return CURVES.at(curve_->GetSelection() == 1 ? 1 : 0);
    }
    return standard;
}

void FormatDialog::setLine(wxStaticText& line, const wxString& text) {
    if (line.GetLabel() == text && line.IsShown() == !text.empty()) {
        return;
    }
    line.SetLabel(text);
    line.Show(!text.empty());
    line.Wrap(std::max(GetClientSize().x - FromDIP(20), FromDIP(200)));
    // One more line: the dialog grows if it must, never shrinks one the user enlarged, and its
    // minimum keeps the line visible.
    InvalidateBestSize();
    const wxSize best = GetBestSize();
    const wxSize now = GetSize();
    SetMinSize(wxSize(std::max(GetMinSize().x, best.x), std::max(GetMinSize().y, best.y)));
    SetSize(wxSize(std::max(now.x, best.x), std::max(now.y, best.y)));
#if defined(__WXGTK__)
    fixed_client_ = GetClientSize();
#endif
    Layout();
}

void FormatDialog::refreshColour() {
    // The Custom entry's tooltip is its full path (a wxChoice has no per-entry tooltip).
    const wxString format_tip = json_item_ != wxNOT_FOUND && format_->GetSelection() == json_item_
                                    ? fromUtf8(json_path_)
                                    : wxString();
    if (format_->GetToolTipText() != format_tip) {
        if (format_tip.empty()) {
            format_->UnsetToolTip();
        } else {
            format_->SetToolTip(format_tip);
        }
    }
    const ColourModel format_model = model();
    const ColourBase base = colourBase();
    // The labels follow the base (its origin suffix); the selection stays.
    const std::vector<std::string> labels = colourListLabels(base);
    const int entry = colour_->GetSelection();
    for (unsigned int i = 0; i < labels.size() && i < colour_->GetCount(); ++i) {
        if (const wxString label = fromUtf8(labels[i]); colour_->GetString(i) != label) {
            colour_->SetString(i, label);
        }
    }
    colour_->SetSelection(entry);
    // A guess that follows a new height takes the selection with it.
    if (base.origin == ColourOrigin::Auto && listed_base_ && base.standard &&
        listed_base_ != base.standard && selectedStandard() == listed_base_) {
        colour_->SetSelection((no_standard_ ? 1 : 0) +
                              static_cast<int>(standardEntry(*base.standard)));
    }
    listed_base_ = base.standard;
    // A range nobody picked follows the base (a new format's default range), so
    // OK writes no range= the user did not choose.
    if (!range_picked_) {
        range_->SetSelection(base.values.range == ColourRange::Full ? 1 : 0);
    }
    const std::optional<ColourPreset> standard = selectedStandard();
    // The curve is a choice for BT.2100 only; the range is a choice on every standard,
    // Full marked "(not standard)" where the standard's range is limited.
    const bool bt2100 = isBt2100(standard);
    showWindow(*curve_, bt2100);
    showWindow(*curve_text_, !bt2100);
    range_->Enable(standard.has_value());
    if (!standard) {
        range_->SetSelection(base.values.range == ColourRange::Full ? 1 : 0);
    }
    const bool not_standard =
        standard && dvv::media::rangeNotStandard(*standard, format_model, ColourRange::Full);
    const wxString full = fromUtf8(
        std::format("{}{}", rangeLabel(ColourRange::Full), not_standard ? " (not standard)" : ""));
    if (range_->GetString(1) != full) {
        const int range = range_->GetSelection();
        range_->SetString(1, full);
        range_->SetSelection(range);
    }
    const ColourChoice shown{
        .standard = standard,
        .range = range_->GetSelection() == 1 ? ColourRange::Full : ColourRange::Limited,
        .autoDetect = false};
    const ColourParams values = colourValues(shown, base, format_model);
    curve_text_->SetLabel(fromUtf8(transferLabel(values.transfer)));
    const wxString values_text =
        fromUtf8(rawreel::colourValuesText(values, format_model, !standard));
    const bool from_file = standard == base.standard && base.origin != ColourOrigin::Default &&
                           base.origin != ColourOrigin::Auto;
    const auto height = static_cast<std::uint32_t>(std::max(height_->GetValue(), 1));
    setLine(*values_, values_text);  // grows the dialog for a longer line
    setLine(*hint_, fromUtf8(mismatchHint(standard, from_file, height)));
    Layout();
    validate();
}

void FormatDialog::validate() {
    wxString message;
    if (!parseFpsText(fps_->GetValue().utf8_string())) {
        message = "fps must be N, N/D or a decimal";
    } else if (const FormatChoice current = choice();
               const dvv::Result<void> valid = validateChoice(current)) {
        // The Custom entry: its interned format's width/height rule (D5R §4.4, the 641 case).
        if (current.format.starts_with('@')) {
            if (!json_format_) {
                message = fromUtf8(json_format_.error().message);
            } else if (const auto geometry =
                           dvv::media::geometry(*json_format_, current.width, current.height);
                       !geometry) {
                message = fromUtf8(geometry.error().message);
            }
        }
    } else {
        message = wxString::FromUTF8(valid.error().message);
    }
    const bool valid = message.empty();
    if (valid) {
        message = load_error_;  // the last Load JSON error; the controls stay as they were
    }
    if (error_->GetLabel() != message) {
        error_->SetLabel(message);
        error_->Wrap(std::max(GetClientSize().x - FromDIP(20), FromDIP(200)));
        Layout();
    }
    if (ok_ != nullptr) {
        ok_->Enable(valid);
    }
}

void FormatDialog::onColour() {
    // BT.2100 starts from the base's curve (PQ when the base is not BT.2100 HLG).
    curve_->SetSelection(colourBase().standard == ColourPreset::Bt2100Hlg ? 1 : 0);
    refreshColour();
}

void FormatDialog::onLoadJson() {
    // M4 GUI D6: the folder of the last JSON file of this session, else the clip's folder.
    wxString& last_json_folder = lastJsonFolder();
    wxFileDialog dialog(this, "Load a clip description or pixel format",
                        last_json_folder.empty() ? folder_ : last_json_folder, wxEmptyString,
                        "JSON files (*.json)|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    last_json_folder = dialog.GetDirectory();
    loadJson(dialog.GetPath().utf8_string());
}

void FormatDialog::loadJson(const std::string& utf8_path) {
    const dvv::Result<FormatJson> loaded = loadFormatJson(utf8_path);
    if (!loaded) {
        load_error_ = wxString::FromUTF8(loaded.error().message);
        validate();
        return;
    }
    const FormatJson& json = *loaded;
    if (!context_.locked && !json.clip && json.format) {
        // A pixel-format file: a JSON error goes on the red line and adds no entry (D5R §4.4).
        if (const dvv::Result<dvv::media::FormatId> format = pixelFormatFromFile(utf8_path);
            !format) {
            load_error_ = fromUtf8(format.error().message);
            validate();
            return;
        }
    }
    load_error_.clear();
    bool skipped = false;
    if (context_.locked) {
        // §1.7: only the colour group applies; format, size and fps come from the file.
        skipped = json.format || json.size || json.fps;
    } else {
        if (json.format) {
            selectFormat(*json.format);
        }
        if (json.size) {
            setSize(json.size->first, json.size->second);
        }
        if (json.fps) {
            setFps(*json.fps);
        }
    }
    if (json.colour) {
        setColour(*json.colour);
    }
    wxString note;
    if (skipped) {
        if (!json.clip) {
            note =
                "Format, size and frame rate come from the file; the pixel format was not loaded.";
        } else if (json.colour) {
            note = "Format, size and frame rate come from the file; only the colour was loaded.";
        } else {  // a clip description without a colour group
            note = "Format, size and frame rate come from the file; nothing was loaded.";
        }
    }
    setLine(*note_, note);
    refreshColour();  // the list labels, the options, the values line, validate()
}

void FormatDialog::onCandidate() {
    const int index = list_ != nullptr ? list_->GetSelection() : wxNOT_FOUND;
    if (index < 0 || static_cast<std::size_t>(index) >= candidates_.size()) {
        return;
    }
    FormatChoice next = choiceFromCandidate(candidates_[static_cast<std::size_t>(index)]);
    next.colour = choice().colour;  // a candidate names no colour: keep the fields
    setChoice(next);
}

bool FormatDialog::userPick(Control control, int index) {
    wxControlWithItems* picked = nullptr;
    switch (control) {
        case Control::Format: picked = format_; break;
        case Control::Size: picked = size_; break;
        case Control::Colour: picked = colour_; break;
        case Control::Curve: picked = curve_; break;
        case Control::Range: picked = range_; break;
        case Control::Candidate:
            if (list_ == nullptr) {
                return false;
            }
            picked = list_;
            break;
        case Control::Fps: break;
    }
    wxWindow* const window = picked != nullptr ? static_cast<wxWindow*>(picked) : fps_;
    wxItemContainer* const items = picked != nullptr ? static_cast<wxItemContainer*>(picked) : fps_;
    if (!window->IsEnabled() || !window->IsShown() || index < 0 ||
        static_cast<unsigned int>(index) >= items->GetCount()) {
        return false;
    }
    items->SetSelection(index);  // sends no event
    const wxEventType type = control == Control::Candidate ? wxEVT_LISTBOX
                             : picked != nullptr           ? wxEVT_CHOICE
                                                           : wxEVT_COMBOBOX;
    wxCommandEvent event(type, window->GetId());
    event.SetEventObject(window);
    event.SetInt(index);
    event.SetString(items->GetString(static_cast<unsigned int>(index)));
    window->ProcessWindowEvent(event);
    return true;
}

bool FormatDialog::userPick(Control control, const wxString& item) {
    const wxItemContainerImmutable* items = nullptr;
    switch (control) {
        case Control::Format: items = format_; break;
        case Control::Size: items = size_; break;
        case Control::Fps: items = fps_; break;
        case Control::Colour: items = colour_; break;
        case Control::Curve: items = curve_; break;
        case Control::Range: items = range_; break;
        case Control::Candidate: items = list_; break;
    }
    return items != nullptr && userPick(control, items->FindString(item, true));
}

wxString FormatDialog::colourOptionsText() const {
    const wxString curve =
        curve_->IsShown() ? curve_->GetStringSelection() : curve_text_->GetLabel();
    return curve + "|" + range_->GetStringSelection() + "|" + (range_->IsEnabled() ? "1" : "0");
}

wxString FormatDialog::colourHintText() const {
    return hint_->IsShown() ? hint_->GetLabel() : wxString();
}

bool FormatDialog::colourTakesInitialFocus() const {
    // wxSetFocusToChild()'s rule: the first child, in tab order, that can take the focus.
    for (const wxWindow* child : GetChildren()) {
        if (child->CanAcceptFocus() && !child->IsTopLevel()) {
            return child == colour_;
        }
    }
    return false;
}

}  // namespace rawreel
