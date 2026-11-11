// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/info_window.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <wx/display.h>
#include <wx/simplebook.h>

#include "core/commands.h"
#include "core/info_text.h"
#include "core/layout.h"
#include "core/session_types.h"
#include "gui/main_frame.h"
#include "gui/native_controls.h"
#if !defined(__WXOSX__)
#include "gui/selftest.h"  // setClipboardText
#endif

namespace rawreel {

namespace {

constexpr int COLUMN_GAP_DIP = 12;  // §11: between the label and the value column
constexpr int ROW_GAP_DIP = 4;      // between rows, and below a heading
constexpr int PLACE_GAP_DIP = 8;    // §10: between the main window and the Info window

// §11: labels right-aligned on macOS (as Finder's Get Info), left-aligned elsewhere.
#if defined(__WXOSX__)
constexpr long LABEL_ALIGN = wxALIGN_RIGHT;
#else
constexpr long LABEL_ALIGN = wxALIGN_LEFT;
#endif

ScreenRect screenRect(const wxRect& rect) {
    return {.x = rect.x, .y = rect.y, .width = rect.width, .height = rect.height};
}

}  // namespace

InfoWindow::InfoWindow(MainFrame& owner)
    : wxFrame(&owner, wxID_ANY, "Info", wxDefaultPosition, wxDefaultSize,
              wxCAPTION | wxCLOSE_BOX | wxFRAME_TOOL_WINDOW | wxFRAME_FLOAT_ON_PARENT),
      owner_(owner) {
    EnableFullScreenView(false);  // macOS: shows over a full-screen main window
    // §10: a panel that takes the focus (no ring drawn), so the window gets key events once the
    // user activates it.
    // Its events stop here, as a dialog's do: keys reach the main window only through onCharHook.
    SetExtraStyle(GetExtraStyle() | wxWS_EX_BLOCK_EVENTS);
    content_ = new wxPanel(this);
    content_->SetCanFocus(true);
    book_ = new wxSimplebook(content_);

    auto* const rows_page = new wxPanel(book_);
    auto* const rows_sizer = new wxBoxSizer(wxVERTICAL);
    const std::vector<InfoRow> rows = infoRows(StreamInfo{});  // the labels, in order
    const int gap = FromDIP(ROW_GAP_DIP);
    for (std::size_t group = 0; group < 2; ++group) {
        if (group > 0) {
            rows_sizer->AddSpacer(GetCharHeight());  // §11: one line between the groups
        }
        auto* const heading =
            new wxStaticText(rows_page, wxID_ANY, group == 0 ? "Picture" : "Colour");
        heading->SetFont(heading->GetFont().Bold());
        headings_.push_back(heading);
        rows_sizer->Add(heading);
        rows_sizer->AddSpacer(gap);
        auto* const grid = new wxFlexGridSizer(2, gap, FromDIP(COLUMN_GAP_DIP));
        const std::size_t first = group == 0 ? 0 : INFO_PICTURE_ROWS;
        const std::size_t end = group == 0 ? INFO_PICTURE_ROWS : rows.size();
        for (std::size_t i = first; i < end; ++i) {
            auto* const label = new wxStaticText(
                rows_page, wxID_ANY, wxString::FromUTF8(rows[i].label), wxDefaultPosition,
                wxDefaultSize, LABEL_ALIGN | wxST_NO_AUTORESIZE);
            auto* const value = new wxStaticText(rows_page, wxID_ANY, wxString(), wxDefaultPosition,
                                                 wxDefaultSize, wxST_NO_AUTORESIZE);
            labels_.push_back(label);
            values_.push_back(value);
            grid->Add(label, wxSizerFlags().Expand());
            grid->Add(value, wxSizerFlags().Expand());
        }
        rows_sizer->Add(grid);
    }
    rows_page->SetSizer(rows_sizer);
    book_->AddPage(rows_page, "rows");

    auto* const empty_page = new wxPanel(book_);
    auto* const empty_sizer = new wxBoxSizer(wxVERTICAL);
    no_stream_ = new wxStaticText(empty_page, wxID_ANY, "No stream");
    empty_sizer->AddStretchSpacer();
    empty_sizer->Add(no_stream_, wxSizerFlags().Center());
    empty_sizer->AddStretchSpacer();
    empty_page->SetSizer(empty_sizer);
    book_->AddPage(empty_page, "no stream");

    auto* const content_sizer = new wxBoxSizer(wxVERTICAL);
    content_sizer->Add(book_, wxSizerFlags(1).Expand().Border());  // the OS's dialog margins
    content_->SetSizer(content_sizer);
    auto* const frame_sizer = new wxBoxSizer(wxVERTICAL);
    frame_sizer->Add(content_, wxSizerFlags(1).Expand());
    SetSizer(frame_sizer);

    applyLabelColours();
    measure();
    showFor(nullptr);
    Fit();  // once: the size is fixed from now on (§11)

    Bind(wxEVT_CHAR_HOOK, &InfoWindow::onCharHook, this);
    Bind(wxEVT_CLOSE_WINDOW, &InfoWindow::onClose, this);
    Bind(wxEVT_DPI_CHANGED, &InfoWindow::onDpiChanged, this);
    Bind(wxEVT_SYS_COLOUR_CHANGED, &InfoWindow::onSysColourChanged, this);
#if defined(__WXGTK__)
    Bind(wxEVT_SHOW, &InfoWindow::onShow, this);
#endif
}

void InfoWindow::measure() {
    // §11: the value column is as wide as the widest sample (the catalogue's formats among them),
    // so no value is cut and the window never changes size; the label columns of both groups are
    // as wide as the widest label.
    int value_width = 0;
    for (const std::string& sample : infoValueSamples()) {
        value_width = std::max(
            value_width, values_.front()->GetTextExtent(wxString::FromUTF8(sample)).GetWidth());
    }
    int label_width = 0;
    for (const wxStaticText* const label : labels_) {
        label_width = std::max(label_width, label->GetTextExtent(label->GetLabel()).GetWidth());
    }
    for (wxStaticText* const value : values_) {
        value->SetMinSize(wxSize(value_width, -1));
    }
    for (wxStaticText* const label : labels_) {
        label->SetMinSize(wxSize(label_width, -1));
    }
}

void InfoWindow::refit() {
    const wxPoint top_left = GetPosition();
    measure();
    Fit();
    SetPosition(top_left);
}

void InfoWindow::applyLabelColours() {
    for (wxStaticText* const label : labels_) {
        setSecondaryText(*label);
    }
    setSecondaryText(*no_stream_);
}

void InfoWindow::showFor(const StreamInfo* stream) {
    if (stream == nullptr) {
        book_->ChangeSelection(1);
        return;
    }
    const std::vector<InfoRow> rows = infoRows(*stream);
    for (std::size_t i = 0; i < rows.size() && i < values_.size(); ++i) {
        const wxString label = wxString::FromUTF8(rows[i].label);
        const wxString value = wxString::FromUTF8(rows[i].value);
        values_[i]->SetLabelText(value);                       // no mnemonics: a value may hold '&'
        setAccessibleName(*values_[i], label + ", " + value);  // §11: read as one item
    }
    book_->ChangeSelection(0);
    book_->GetPage(0)->Layout();
}

void InfoWindow::toggle() {
    owner_.setInfoShown(!owner_.infoShown());
}

void InfoWindow::showBesideOwner() {
    if (place_ && wxDisplay::GetFromPoint(*place_) != wxNOT_FOUND) {
        requested_ = wxRect(*place_, GetSize());
    } else {
        // §10: beside the main window on its display, else inside its right edge.
        const int display = wxDisplay::GetFromWindow(&owner_);
        const wxRect work =
            wxDisplay(display == wxNOT_FOUND ? 0U : static_cast<unsigned>(display)).GetClientArea();
        const wxRect client(owner_.ClientToScreen(wxPoint(0, 0)), owner_.GetClientSize());
        const wxSize size = GetSize();
        const ScreenRect place =
            infoWindowPlace(screenRect(owner_.GetScreenRect()), screenRect(client),
                            LayoutSize{.width = size.x, .height = size.y}, screenRect(work),
                            FromDIP(PLACE_GAP_DIP));
        requested_ = wxRect(place.x, place.y, place.width, place.height);
    }
    SetPosition(requested_.GetTopLeft());
#if defined(__WXGTK__)
    wanted_shown_ = true;  // before the show: its wxEVT_SHOW comes from inside it
#endif
    ShowWithoutActivating();  // §10: the main window keeps the focus
}

void InfoWindow::hideKeepingPlace() {
    // Only a shown window has a place to keep: one hidden by a minimise keeps the place it had,
    // one created while the main window was minimised has none yet.
    if (IsShown()) {
        place_ = GetPosition();
    }
#if defined(__WXGTK__)
    wanted_shown_ = false;
#endif
    Hide();
}

void InfoWindow::restoreAfterIconize() {
    showBesideOwner();  // §10: the remembered place when a display is there, else beside the owner
}

void InfoWindow::forgetPlaceForTest() {
    place_.reset();
    requested_ = wxRect();
    const int display = wxDisplay::GetFromWindow(&owner_);
    Move(wxDisplay(display == wxNOT_FOUND ? 0U : static_cast<unsigned>(display))
             .GetClientArea()
             .GetTopLeft());
}

std::vector<std::pair<std::string, std::string>> InfoWindow::rowsForTest() const {
    std::vector<std::pair<std::string, std::string>> rows;
    if (book_->GetSelection() != 0) {
        return rows;
    }
    for (std::size_t i = 0; i < labels_.size() && i < values_.size(); ++i) {
        rows.emplace_back(labels_[i]->GetLabel().utf8_string(),
                          values_[i]->GetLabelText().utf8_string());
    }
    return rows;
}

// §10 Keys: Esc closes, Ctrl/Cmd+I toggles (closes), Ctrl+C copies on Windows and Linux (on macOS
// Edit -> Copy does, MainFrame::onEditMenu); every other key goes to the main window's key
// dispatch, so the player keys act as in the main window. Not skipped when handled: the window
// blocks its events (wxWS_EX_BLOCK_EVENTS), so a skipped key ends here.
void InfoWindow::onCharHook(wxKeyEvent& event) {
    if (owner_.closing()) {
        event.Skip();
        return;
    }
    const KeyChord key =
        normaliseKey(event.GetKeyCode(), event.ControlDown(), event.ShiftDown(), event.AltDown());
    if (key == chord(Key::Escape)) {
        owner_.setInfoShown(false);
        return;
    }
    if (key == commandTable()[static_cast<std::size_t>(Command::InfoWindow)].key) {
        owner_.execute(Command::InfoWindow, CommandSource::Key);
        return;
    }
#if !defined(__WXOSX__)
    if (key == KeyChord{.key = 'C', .ctrl = true}) {
        const std::vector<StreamInfo>& streams = owner_.streams();
        setClipboardText(infoCopyText(streams.empty() ? nullptr : &streams.front()));
        return;
    }
#endif
    if (const std::optional<Command> command = commandForKey(key, false)) {
        owner_.execute(*command, CommandSource::Key);
        return;
    }
    event.Skip();
}

void InfoWindow::onClose(wxCloseEvent& event) {
    if (event.CanVeto()) {
        event.Veto();
        owner_.setInfoShown(false);  // the close box hides it, remembering the place
        return;
    }
    Destroy();  // the owner is closing
}

#if defined(__WXMSW__)
// Windows: skipped after the new min sizes are set, so wx sizes the window from its sizer around
// the rect the OS suggests; keeping the old top-left could move the window back to the old display
// and start a stream of WM_DPICHANGED messages.
void InfoWindow::onDpiChanged(wxDPIChangedEvent& event) {
    measure();
    event.Skip();
}
#else
// Not skipped: the window takes its own fitted size, not the size the OS suggests.
void InfoWindow::onDpiChanged(wxDPIChangedEvent& /*event*/) {
    refit();
}
#endif

#if defined(__WXGTK__)
// X11 reports a map asynchronously. When the map notification of a show arrives after a Hide()
// (Ctrl+I twice in quick succession), wxGTK marks the hidden window shown again and sends
// wxEVT_SHOW (wxTopLevelWindowGTK::GTKHandleMapped calls wxWindowBase::Show(true), "it is possible
// for m_isShown to be false here"), while the X window stays unmapped. Hidden again here, so
// IsShown(), MainFrame::infoShown() and the "i" toggle keep the owner's last choice.
void InfoWindow::onShow(wxShowEvent& event) {
    event.Skip();
    if (event.IsShown() && !wanted_shown_) {
        Hide();  // wx's state only: the X window is not mapped
    }
}
#endif

// §11: a font change fits the window again; the label colours follow the appearance.
void InfoWindow::onSysColourChanged(wxSysColourChangedEvent& event) {
    applyLabelColours();
    refit();
    event.Skip();
}

}  // namespace rawreel
