// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/floating_window.h"

#include <cstddef>
#include <optional>
#include <utility>

#include <wx/display.h>

#include "core/commands.h"
#include "core/layout.h"
#include "gui/edit_dispatch.h"
#include "gui/main_frame.h"
#include "gui/native_policy.h"

namespace rawreel {

namespace {

constexpr int PLACE_GAP_DIP = 8;  // between the main window and a floating window, and between two

}  // namespace

ScreenRect screenRect(const wxRect& rect) {
    return {.x = rect.x, .y = rect.y, .width = rect.width, .height = rect.height};
}

wxRect wxRectOf(ScreenRect rect) {
    return {rect.x, rect.y, rect.width, rect.height};
}

wxRect workAreaOf(const wxWindow& window) {
    const int display = wxDisplay::GetFromWindow(&window);
    return wxDisplay(display == wxNOT_FOUND ? 0U : static_cast<unsigned>(display)).GetClientArea();
}

FloatingWindow::FloatingWindow(MainFrame& owner, FloatingKind kind, const wxString& title)
    : wxFrame(&owner, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
              wxCAPTION | wxCLOSE_BOX | wxFRAME_TOOL_WINDOW | wxFRAME_FLOAT_ON_PARENT),
      owner_(owner),
      kind_(kind) {
    EnableFullScreenView(false);  // macOS: shows over a full-screen main window
    // Its events stop here, as a dialog's do: keys reach the main window only through onCharHook.
    SetExtraStyle(GetExtraStyle() | wxWS_EX_BLOCK_EVENTS);
    content_ = new wxPanel(this);
    content_->SetCanFocus(true);
    auto* const frame_sizer = new wxBoxSizer(wxVERTICAL);
    frame_sizer->Add(content_, wxSizerFlags(1).Expand());
    SetSizer(frame_sizer);

    Bind(wxEVT_CHAR_HOOK, &FloatingWindow::onCharHook, this);
    Bind(wxEVT_CLOSE_WINDOW, &FloatingWindow::onClose, this);
    Bind(wxEVT_DPI_CHANGED, &FloatingWindow::onDpiChanged, this);
    Bind(wxEVT_SYS_COLOUR_CHANGED, &FloatingWindow::onSysColourChanged, this);
    if (native_policy::hidesStaleMapShow()) {
        Bind(wxEVT_SHOW, &FloatingWindow::onShow, this);
    }
}

bool FloatingWindow::ownerHidden() const {
    // X11: Hide stands for iconify (MainFrame::onShow).
    return owner_.IsIconized() || (native_policy::hideMeansIconified() && !owner_.IsShown());
}

std::optional<ScreenRect> FloatingWindow::otherShown() const {
    for (std::size_t i = 0; i < FLOATING_KIND_COUNT; ++i) {
        const auto other_kind = static_cast<FloatingKind>(i);
        if (other_kind == kind_) {
            continue;
        }
        if (const FloatingWindow* const other = owner_.floatingWindow(other_kind);
            other != nullptr && other->IsShown()) {
            return screenRect(other->GetScreenRect());
        }
    }
    return std::nullopt;
}

void FloatingWindow::showBesideOwner() {
    if (ownerHidden()) {
        hidden_by_owner_ = true;  // shown with the owner when it is restored
        return;
    }
    hidden_by_owner_ = false;
    showNow();
}

void FloatingWindow::showNow() {
    if (place_ && wxDisplay::GetFromPoint(*place_) != wxNOT_FOUND) {
        requested_ = wxRect(*place_, GetSize());  // the kept place wins over the computed one
    } else {
        const wxRect work = workAreaOf(owner_);
        const wxRect client(owner_.ClientToScreen(wxPoint(0, 0)), owner_.GetClientSize());
        const wxSize size = GetSize();
        const ScreenRect place =
            floatingWindowPlace(screenRect(owner_.GetScreenRect()), screenRect(client),
                                LayoutSize{.width = size.x, .height = size.y}, screenRect(work),
                                otherShown(), FromDIP(PLACE_GAP_DIP));
        requested_ = wxRectOf(place);
    }
    SetPosition(requested_.GetTopLeft());
    visible_ = true;          // before the show: a wxEVT_SHOW comes from inside it
    ShowWithoutActivating();  // the main window keeps the focus
}

void FloatingWindow::hideKeepingPlace() {
    hidden_by_owner_ = false;
    // Only a shown window has a place to keep: one hidden by a minimise keeps the place it had,
    // one created while the main window was minimised has none yet.
    if (IsShown()) {
        place_ = GetPosition();
    }
    visible_ = false;
    Hide();
}

void FloatingWindow::followOwner(bool owner_hidden) {
    if (owner_hidden && IsShown()) {
        hideKeepingPlace();
        hidden_by_owner_ = true;
    } else if (!owner_hidden && std::exchange(hidden_by_owner_, false)) {
        showNow();  // the kept place when a display is there, else beside the owner
    }
}

void FloatingWindow::forgetPlaceForTest() {
    place_.reset();
    requested_ = wxRect();
    Move(workAreaOf(owner_).GetTopLeft());
}

void FloatingWindow::refit() {
    const wxPoint top_left = GetPosition();
    measure();
    Fit();
    SetPosition(top_left);
}

bool FloatingWindow::handleKey(const KeyChord& /*key*/) {
    return false;
}

// Keys: the window's own first; Esc hides the window, the View -> Info key toggles the Info
// window. A plain key (no Ctrl, Alt or Cmd) goes to the main window's char-hook commands unless the
// focused native control uses it itself (controlKeepsKey). A modified chord
// goes to the char-hook commands and, where the menu bar does not take a floating window's key
// equivalents, to the command that has it. Not skipped when handled: the window blocks its events
// (wxWS_EX_BLOCK_EVENTS), so a skipped key goes to the focused control only.
void FloatingWindow::onCharHook(wxKeyEvent& event) {
    if (owner_.closing()) {
        event.Skip();
        return;
    }
    const KeyChord key =
        normaliseKey(event.GetKeyCode(), event.ControlDown(), event.ShiftDown(), event.AltDown());
    if (handleKey(key)) {
        return;
    }
    if (key == chord(Key::Escape)) {
        owner_.setWindowShown(kind_, false);
        return;
    }
    if (key == commandTable()[static_cast<std::size_t>(Command::InfoWindow)].key) {
        owner_.execute(Command::InfoWindow, CommandSource::Key);
        return;
    }
    std::optional<Command> command;
    if (!key.ctrl && !key.alt) {
        // wxOSX can report no focus while the window is the key window: then no control keeps it.
        if (!controlKeepsKey(owner_.focusedWindow(), key)) {
            command = commandForKey(key, false);
        }
    } else {
        command = commandForKey(key, false);
        if (!command && native_policy::floatingForwardsAccelerators()) {
            command = commandOfChord(key);
        }
    }
    if (command) {
        owner_.execute(*command, CommandSource::Key);
        return;
    }
    event.Skip();
}

void FloatingWindow::onClose(wxCloseEvent& event) {
    if (event.CanVeto()) {
        event.Veto();
        owner_.setWindowShown(kind_, false);  // the close box hides it, keeping the place
        return;
    }
    Destroy();  // the owner is closing
}

void FloatingWindow::onDpiChanged(wxDPIChangedEvent& event) {
    if (native_policy::dpiChangeTakesSuggestedRect()) {
        // Windows: skipped after the new min sizes are set, so wx sizes the window from its sizer
        // around the rect the OS suggests; keeping the old top-left could move the window back to
        // the old display and start a stream of WM_DPICHANGED messages.
        measure();
        event.Skip();
        return;
    }
    // Not skipped: the window takes its own fitted size, not the size the OS suggests.
    refit();
}

// A font change fits the window again; the colours follow the appearance.
void FloatingWindow::onSysColourChanged(wxSysColourChangedEvent& event) {
    applyColours();
    refit();
    event.Skip();
}

// X11 reports a map asynchronously. When the map notification of a show arrives after a Hide()
// (Ctrl+I twice in quick succession), wxGTK marks the hidden window shown again and sends
// wxEVT_SHOW (wxTopLevelWindowGTK::GTKHandleMapped calls wxWindowBase::Show(true), "it is possible
// for m_isShown to be false here"), while the X window stays unmapped. Hidden again here, so
// IsShown(), MainFrame::windowShown() and the "i" toggle keep the owner's last choice.
void FloatingWindow::onShow(wxShowEvent& event) {
    event.Skip();
    if (event.IsShown() && !visible_) {
        Hide();  // wx's state only: the X window is not mapped
    }
}

}  // namespace rawreel
