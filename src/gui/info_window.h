#pragma once

// The Info window (window title and Info design §10, §11): a floating tool window owned by the main
// window with the stream's picture and colour rows, or "No stream". Its shown state belongs to
// MainFrame::setInfoShown, the one path every show and hide goes through.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <wx/simplebook.h>

namespace rawreel {

class MainFrame;
struct StreamInfo;

class InfoWindow final : public wxFrame {
public:
    explicit InfoWindow(MainFrame& owner);
    InfoWindow(const InfoWindow&) = delete;
    InfoWindow& operator=(const InfoWindow&) = delete;
    InfoWindow(InfoWindow&&) = delete;
    InfoWindow& operator=(InfoWindow&&) = delete;
    ~InfoWindow() override = default;

    // The rows of `stream`, or the "No stream" page for nullptr. The size never changes.
    void showFor(const StreamInfo* stream);
    // Shows or hides the window through the owner (MainFrame::setInfoShown).
    void toggle();
    // §10 Place: at the remembered place when a display is there, else beside the owner
    // (infoWindowPlace); shown without activating, so the main window keeps the focus.
    void showBesideOwner();
    // Hides the window and, if it was shown, remembers its place for this run.
    void hideKeepingPlace();
    // The owner was restored: shown again as showBesideOwner() does (where it was when a display
    // is there, else beside the owner; also the first show while the owner was minimised).
    void restoreAfterIconize();

    // "Label", "value" of each row; empty while "No stream" shows.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> rowsForTest() const;
    // The rectangle the last showBesideOwner() asked for (position and size), screen coordinates.
    [[nodiscard]] wxRect requestedRectForTest() const noexcept { return requested_; }
    // The content panel, which takes the focus (§10): the selftest's key events start there.
    [[nodiscard]] wxWindow* contentForTest() const noexcept { return content_; }
    // Back to the never-placed state of a new window (no remembered place, no request), moved to
    // the work area's corner. Hidden windows only.
    void forgetPlaceForTest();

private:
    // The label and value columns' minimum widths from the current fonts.
    void measure();
    void refit();  // measure() and Fit(), keeping the top-left corner
    void applyLabelColours();
    void onCharHook(wxKeyEvent& event);
    void onClose(wxCloseEvent& event);
    void onDpiChanged(wxDPIChangedEvent& event);
    void onSysColourChanged(wxSysColourChangedEvent& event);
#if defined(__WXGTK__)
    void onShow(wxShowEvent& event);
#endif

    MainFrame& owner_;
    wxPanel* content_ = nullptr;
    wxSimplebook* book_ = nullptr;
    std::vector<wxStaticText*> headings_;
    std::vector<wxStaticText*> labels_;
    std::vector<wxStaticText*> values_;
    wxStaticText* no_stream_ = nullptr;
    std::optional<wxPoint> place_;  // remembered at hide, for this run
    wxRect requested_;
#if defined(__WXGTK__)
    bool wanted_shown_ = false;  // the owner's last choice (showBesideOwner, hideKeepingPlace)
#endif
};

}  // namespace rawreel
