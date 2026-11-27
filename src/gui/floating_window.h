#pragma once

// A floating tool window owned by the main window: placed beside the main window
// or where the user left it, shown without taking the focus, hidden by its close box and Esc,
// hidden while the main window is minimised. Every show and hide goes through
// MainFrame::setWindowShown, the one path for all floating windows.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/commands.h"
#include "core/layout.h"

namespace rawreel {

class MainFrame;

enum class FloatingKind : std::uint8_t { Info, GridAndRulers, Zoom };
// Fitted: the window sizes itself from its controls (refit). Resizable: the user sizes it; the size
// is kept in DIP, so a font or scale change leaves it, and it is saved with the place.
enum class FloatingSizing : std::uint8_t { Fitted, Resizable };

inline constexpr std::size_t FLOATING_KIND_COUNT =
    static_cast<std::size_t>(FloatingKind::Zoom) + 1;  // the last kind

// A wx rectangle as the layout's screen rectangle, and back.
[[nodiscard]] ScreenRect screenRect(const wxRect& rect);
[[nodiscard]] wxRect wxRectOf(ScreenRect rect);
// The work area (GetClientArea) of the display `window` is on, else of display 0.
[[nodiscard]] wxRect workAreaOf(const wxWindow& window);

class FloatingWindow : public wxFrame {
public:
    FloatingWindow(MainFrame& owner, FloatingKind kind, const wxString& title,
                   FloatingSizing sizing = FloatingSizing::Fitted);
    FloatingWindow(const FloatingWindow&) = delete;
    FloatingWindow& operator=(const FloatingWindow&) = delete;
    FloatingWindow(FloatingWindow&&) = delete;
    FloatingWindow& operator=(FloatingWindow&&) = delete;
    ~FloatingWindow() override = default;

    [[nodiscard]] FloatingKind kind() const noexcept { return kind_; }
    // At the kept place when a display is there, else beside the owner (floatingWindowPlace, below
    // or beside any other floating window that is shown); shown without activating, so the main
    // window keeps the focus. While the owner is minimised (X11: hidden) it waits for the restore.
    void showBesideOwner();
    // Hides the window and, if it was shown, keeps its place for this run.
    void hideKeepingPlace();
    // The kept place (the position, and the size in window units) when there is one: the window was
    // hidden once or restorePlace() set it.
    [[nodiscard]] std::optional<ScreenRect> keptPlace() const;
    // The kept size in DIP (a Resizable window; the last size it had, else 0 x 0).
    [[nodiscard]] LayoutSize keptSizeDip() const noexcept { return size_dip_; }
    // Sets the place the next show uses (x and y of `place`) and the size in DIP; a Resizable
    // window takes the size at once.
    void restorePlace(std::optional<ScreenRect> place, LayoutSize size_dip);
    // The owner was minimised (X11: hidden) or restored: a shown window hides with it, keeping its
    // place, and shows again, without activating, when the owner comes back.
    void followOwner(bool owner_hidden);
    // Shown, or hidden only while the owner is minimised.
    [[nodiscard]] bool wanted() const noexcept { return IsShown() || hidden_by_owner_; }

    // The rectangle the last showBesideOwner() asked for (position and size), screen coordinates.
    [[nodiscard]] wxRect requestedRectForTest() const noexcept { return requested_; }
    // The content panel: the selftest finds where wx puts the focus from there.
    [[nodiscard]] wxWindow* contentForTest() const noexcept { return content_; }
    // Back to the never-placed state of a new window (no kept place, no request), moved to the work
    // area's corner. Hidden windows only.
    void forgetPlaceForTest();

protected:
    // The panel that holds the window's controls. It takes the focus itself (no ring drawn) while
    // nothing inside it can, so the window gets key events once the user activates it; else wx
    // gives the focus to its first child that can take it (a page panel, a control). A plain key
    // reaches the main window unless the focused native control uses it (controlKeepsKey).
    [[nodiscard]] wxPanel& content() const noexcept { return *content_; }
    [[nodiscard]] MainFrame& owner() const noexcept { return owner_; }
    // measure() and Fit(), keeping the top-left corner.
    void refit();

    // A key of the window's own before the shared ones; true when it took the key.
    virtual bool handleKey(const KeyChord& key);
    // The controls' minimum sizes from the current fonts and scale.
    virtual void measure() {}
    // The colours that follow the system appearance.
    virtual void applyColours() {}

private:
    [[nodiscard]] bool ownerHidden() const;
    void showNow();  // placed (kept place, else computed) and shown without activating
    // The screen rectangles of the other floating windows that are shown.
    [[nodiscard]] std::vector<ScreenRect> othersShown() const;
    void keepPlace();  // the position and, when Resizable, the DIP size, for this run
    [[nodiscard]] wxSize windowSizeOf(LayoutSize dip) const;  // DIP to this window's units
    void onSize(wxSizeEvent& event);
    void onCharHook(wxKeyEvent& event);
    void onClose(wxCloseEvent& event);
    void onDpiChanged(wxDPIChangedEvent& event);
    void onSysColourChanged(wxSysColourChangedEvent& event);
    void onShow(wxShowEvent& event);

    MainFrame& owner_;
    FloatingKind kind_;
    FloatingSizing sizing_;
    wxPanel* content_ = nullptr;
    std::optional<wxPoint> place_;  // kept at hide, for this run
    LayoutSize size_dip_;           // the Resizable size, kept in DIP
    wxRect requested_;
    bool visible_ = false;          // the last show or hide asked for it to be on screen
    bool hidden_by_owner_ = false;  // the minimised owner hid the shown window
};

}  // namespace rawreel
