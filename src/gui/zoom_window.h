#pragma once

// The Zoom window (pixel inspector design §4.2): a resizable floating window with a toolbar, a
// four-line header, a Viewport of its own and the pinned pixel's raw words under it.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <wx/tglbtn.h>

#include "core/commands.h"
#include "core/inspector.h"
#include "core/inspector_text.h"
#include "core/layout.h"
#include "core/overlay.h"
#include "dvv/gpu/execution_context.h"
#include "gui/floating_window.h"

namespace rawreel {

class FrameFeed;
class MainFrame;
class RawWordsView;
class Viewport;

class ZoomWindow final : public FloatingWindow {
public:
    // Resizable; the picture area (the viewport with its ruler strips) is `picture_dip` (the
    // config's zoomSizeDip, at least ZOOM_WINDOW_SIZE_MIN a side) and the next show uses `place`
    // (its x and y) when set. Created hidden and holding nothing: no feed roles, no value glyphs,
    // no stack.
    ZoomWindow(MainFrame& owner, std::shared_ptr<FrameFeed> feed, LayoutSize picture_dip,
               std::optional<ScreenRect> place);
    ZoomWindow(const ZoomWindow&) = delete;
    ZoomWindow& operator=(const ZoomWindow&) = delete;
    ZoomWindow(ZoomWindow&&) = delete;
    ZoomWindow& operator=(ZoomWindow&&) = delete;
    ~ZoomWindow() override = default;

    // Valid while the window exists.
    [[nodiscard]] Viewport& viewport() const noexcept { return *viewport_; }
    // The picture area in DIP, as the config saves it (zoomSizeDip).
    [[nodiscard]] LayoutSize pictureSizeDip() const;
    // The toolbar's buttons, for the selftest: the Live, Cell and Planes halves of the toggles.
    [[nodiscard]] wxToggleButton& liveForTest() const noexcept { return *live_.second; }
    [[nodiscard]] wxToggleButton& cellForTest() const noexcept { return *mode_.second; }
    [[nodiscard]] wxToggleButton& planesForTest() const noexcept { return *layout_.second; }
    // The "Numbers fit" button, for the selftest (a focused toolbar button and Ctrl+C).
    [[nodiscard]] wxWindow& numbersFitForTest() const noexcept { return *numbers_fit_; }
    // The header's four lines as shown (" " for an empty line), for the selftest.
    [[nodiscard]] std::array<wxString, 4> headerForTest() const;
    // The raw words as shown (none while hidden), for the selftest.
    [[nodiscard]] std::vector<RawWordLine> rawLinesForTest() const;
    // The header's four lines (inspectorHeader) and the raw words (rawWordLines) of `values`;
    // nullopt clears the raw words. `pending`: the values of a new pixel are on the way, so shown
    // raw words keep their place as blank lines and the picture keeps its size until they come.
    void showValues(const std::optional<PixelValues>& values, std::array<std::string, 4> header,
                    std::vector<RawWordLine> raw, bool pending);
    // The toolbar's three toggles: Pinned | Live, Follow | Cell, Structure | Planes.
    void setToggles(bool live, InspectorMode mode, InspectorLayout layout);
    // The main window's grid and rulers, kept; the rulers are shown while the window is attached
    // and off while it is detached, so a hidden window holds and rasters no ruler glyphs.
    void setOverlayConfig(const GridConfig& grid, const RulerConfig& rulers);

    // Shown (MainFrame::setWindowShown, after the show): feed roles {Display, Planes, Raw}, the
    // value glyphs, the stack. Hidden (before the hide), and before the window is destroyed: no
    // feed roles, no value glyphs, no stack, so it holds no lease, glyph strip or GPU object.
    void attach(const std::weak_ptr<dvv::gpu::ExecutionContext>& context, std::uint64_t epoch);
    void detach();

private:
    // Copy and Esc, then the Zoom keys (zoomKeyAction, MainFrame::inspectorKey).
    bool handleKey(const KeyChord& key) override;
    void applyColours() override;
    // A DPI or font change: the header's font and the viewport's minimum again; the size stays.
    void measure() override;
    void sendOverlayConfig();  // grid_ and rulers_ to the viewport, the rulers shown when attached

    // A segmented toggle: two toggle buttons acting as a radio pair (wx has no native segmented
    // control); `second` is on when the second button is.
    struct TogglePair {
        wxToggleButton* first = nullptr;
        wxToggleButton* second = nullptr;
        void set(bool second_on) const;
    };
    // `on(second)` runs after a click turned a button on.
    [[nodiscard]] TogglePair addPair(wxSizer& sizer, const wxString& first, const wxString& second,
                                     const wxString& tip, std::function<void(bool)> on);

    Viewport* viewport_ = nullptr;
    RawWordsView* raw_ = nullptr;
    std::array<wxStaticText*, 4> header_{};
    TogglePair live_, mode_, layout_;
    wxButton* fit_cell_ = nullptr;
    wxButton* numbers_fit_ = nullptr;
    GridConfig grid_{};
    RulerConfig rulers_{};
    bool attached_ = false;  // attach() ran, detach() did not since
    bool ready_ = false;     // the viewport was marked ready (after the first show)
};

}  // namespace rawreel
