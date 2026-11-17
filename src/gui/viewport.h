#pragma once

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "core/view.h"
#include "dvv/base/error.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/presenter.h"
#include "dvv/platform/native_window.h"
#include "render/display_state.h"
#include "render/viewport_renderer.h"

namespace rawreel {

// The picture's move per wheel line in DIP (view zoom design §5). wxOSX reports scroll in points
// as rotation with a wheel delta of 10 and one line per notch, so 10 moves the picture one point
// per rotation unit; 16 elsewhere.
#if defined(__WXOSX__)
inline constexpr double WHEEL_LINE_DIP = 10;
#else
inline constexpr double WHEEL_LINE_DIP = 16;
#endif

// The viewport (M1b §5.6 plus design M3 v2 §5.3 GUI side): a native child window that shows the
// DisplayState through one ViewportRenderer and one Presenter of the current stack. Every member
// runs on the GUI thread; the GUI thread only calls the presenter's non-blocking members.
class Viewport final : public wxWindow {
public:
    // Sets display->setParams(CompositeParams{.dark = wxSystemSettings::GetAppearance().IsDark()}):
    // the Fit view. Only setView() changes the params later; the renderer resolves the view
    // against its extent and the frame size, so a resize sets nothing.
    Viewport(wxWindow* parent, std::shared_ptr<DisplayState> display);
    Viewport(const Viewport&) = delete;
    Viewport& operator=(const Viewport&) = delete;
    Viewport(Viewport&&) = delete;
    Viewport& operator=(Viewport&&) = delete;
    ~Viewport() override;  // detachStack() first (binding, M1b §5.6)

    // ISessionEvents::stackChanged on the GUI thread: drops presenter and renderer, keeps the
    // context of a live pointer and starts presenting when ready.
    void setStack(const std::weak_ptr<dvv::gpu::ExecutionContext>& context, std::uint64_t epoch);
    void markReady();       // CallAfter once the top-level frame is shown (M1b startPresenting)
    void stopPresenting();  // DisplayState::setOnChanged({}), presenter, renderer: in this order
    void detachStack();     // stopPresenting() and release the context (frame close, §5.5)
    // Called with the presenter's epoch when it fails with DeviceLost (MainFrame:
    // reportDeviceLost).
    void setDeviceLostHandler(std::function<void(std::uint64_t)> handler);
    // D25: called once with the failure when the presenter fails with an error other than
    // DeviceLost, or when renderer or presenter creation fails with such an error. Nothing is
    // recreated; MainFrame::onPresenterFailed logs it (Task 9: infobar with Reopen).
    void setFailureHandler(std::function<void(const dvv::Error&)> handler);
    void requestFrame();  // presenter->requestFrame() when there is one
    // The resize entry: with a presenter, syncNativeWindow() and then
    // presenter->resize(physicalClientSize()). onSize calls it, and on macOS so does the child
    // view's viewDidChangeBackingProperties (a display-scale change sends no wxEVT_SIZE). GUI
    // thread.
    void resizePresenter();
    // Test only (--selftest with RAWREEL_GUI_FAULT=presenter): the record function of every
    // presenter frame throws, so each presenter fails with Errc::Internal (the D25 path).
    void failPresentersForTest() noexcept { fail_presenters_ = true; }
    // Test only (the lost selftest): the next startPresenting() reports an Errc::Internal
    // creation error at once, on the GUI thread (the synchronous D25 path of setStack).
    void failNextStartForTest() noexcept { fail_next_start_ = true; }
    // Forwarded by the frame (wxEVT_ICONIZE and wxEVT_SHOW of the top-level window). Both set
    // one target, visible = shown && !iconized; the presenter hears only its changes.
    void topLevelIconized(bool iconized);
    void topLevelShown(bool shown);
    // Test hook (selftest): how often the presenter was made visible again (false -> true).
    [[nodiscard]] std::uint64_t presenterShows() const noexcept { return presenter_shows_; }

    // Null without a presenter (no stack, a lost device, a failed presenter, before markReady).
    [[nodiscard]] dvv::gpu::Presenter* presenter() const noexcept { return presenter_.get(); }
    // The last terminal failure: a presenter's failure() (DeviceLost included) or a renderer or
    // presenter creation error; kept after the presenter is dropped (D25, a lost device) and
    // cleared when the next presenter starts. The selftest runner reads it.
    [[nodiscard]] const std::optional<dvv::Error>& lastFailure() const noexcept {
        return last_failure_;
    }
    // The epoch of the current stack (ISessionEvents::stackChanged); 0 before the first.
    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_; }
    // The client area in physical pixels (wx 3.3 ToPhys); on macOS nativePixelSize(): the child
    // view's points times its layer's contentsScale.
    [[nodiscard]] dvv::platform::PixelSize physicalClientSize() const;
    [[nodiscard]] const std::shared_ptr<DisplayState>& display() const noexcept { return display_; }
    // The renderer's last recorded composite; nullopt without a renderer (no stack). GUI thread.
    [[nodiscard]] std::optional<RecordedInfo> lastRecorded() const;

    // The view request (view zoom design; values in backing and clip pixels). Stores the request;
    // when it changed, display->setParams({.dark, .view = request}), then the view handler.
    void setView(const ViewRequest& request);
    [[nodiscard]] const ViewRequest& view() const noexcept { return view_; }
    // The stream's frame size; {} without a stream (MainFrame::onStreamsChanged). A new size drops
    // the Ctrl+wheel rotation not yet a whole notch.
    void setImageSize(dvv::platform::PixelSize image) noexcept;
    [[nodiscard]] dvv::platform::PixelSize imageSize() const noexcept { return image_; }
    // The view as shown: resolveView(view(), physicalClientSize(), imageSize()).
    [[nodiscard]] ViewTransform shownView() const;
    // The fit's zoom: ViewTransform::fit(physicalClientSize(), imageSize()).zoom(); 0 without an
    // image.
    [[nodiscard]] double fitZoom() const;
    // Backing pixels per window unit: GetContentScaleFactor() (1 on Windows, where window units
    // are physical pixels).
    [[nodiscard]] double contentScale() const;
    // Backing pixels per DIP: GetDPIScaleFactor() on Windows, contentScale() elsewhere.
    [[nodiscard]] double backingPerDip() const;
    // Called after setView() changed the request and at the end of every resizePresenter().
    void setViewChangedHandler(std::function<void()> handler);
    // Test hook (selftest): a left- or middle-button drag is panning the picture.
    [[nodiscard]] bool draggingForTest() const noexcept { return drag_.has_value(); }

private:
    void startPresenting();
    // Creation errors: DeviceLost goes to the device-lost handler, anything else to the failure
    // handler.
    void reportFailure(const dvv::Error& error);
    // A presenter's onError, forwarded to the GUI thread; `serial` names the presenter.
    void onPresenterError(std::uint64_t serial, const dvv::Error& error);
    // Sends visible = shown && !iconized_ to the presenter if it changed; `event` names the event.
    void updateVisible(bool shown, bool old_visible, const char* event);
    void onPaint(wxPaintEvent& event);
    void onSize(wxSizeEvent& event);
    void onMouseDown(wxMouseEvent& event);  // SetFocus() on a right or aux button press (R5)
    // Left and middle press and double-click: SetFocus(); with canPan(), a drag starts (mouse
    // capture, the drag cursor).
    void onButtonDown(wxMouseEvent& event);
    // During a drag: panBy() the pointer's move; with the drag's button no longer down (wxOSX may
    // not send its release, e.g. during a menu or a modal loop), ReleaseMouse() and endDrag().
    void onMotion(wxMouseEvent& event);
    // The drag's button released: ReleaseMouse(), endDrag(). Another button's release does not.
    void onButtonUp(wxMouseEvent& event);
    void onCaptureLost(wxMouseCaptureLostEvent& event);  // ends a drag without ReleaseMouse()
    void endDrag();                                      // the default cursor, drag_ reset
    // View zoom design §5: with an image, Ctrl+wheel (Windows, Linux; vertical axis) steps the
    // zoom at the pointer (stepZoom), one step per whole notch; a wheel without Ctrl drops the
    // rotation not yet a whole notch. Any other wheel pans when canPan().
    void onWheel(wxMouseEvent& event);
    // View zoom design §5: with an image, a pinch zooms at the gesture position: the zoom shown at
    // the gesture's start times the event's factor, within zoomLimits(fit). No snap at the end.
    // Without a seen start the start zoom is the shown zoom over the factor; a factor that is not
    // finite or not > 0 is ignored; a zoom equal to the shown one (within 1e-6) sets nothing.
    void onZoomGesture(wxZoomGestureEvent& event);

    std::shared_ptr<DisplayState> display_;
    bool dark_ = false;  // the background of the paint handler without a presenter
    bool ready_ = false;
    bool iconized_ = false;  // the top-level frame is minimised: a new presenter starts hidden
    bool presenter_visible_ = false;     // the last value sent to the presenter (or its creation)
    std::uint64_t presenter_shows_ = 0;  // presenterShows()
    bool fail_presenters_ = false;       // failPresentersForTest()
    bool fail_next_start_ = false;       // failNextStartForTest()
    // The current stack's context, GUI-held while its epoch is current; released here at the next
    // stackChanged or at close (release rule of §5.3).
    std::shared_ptr<dvv::gpu::ExecutionContext> context_;
    std::uint64_t epoch_ = 0;
    std::uint64_t serial_ = 0;  // +1 for every presenter created
    // wxGTK: the presenter's own X connection (ViewportWindow); released after the presenter.
    std::shared_ptr<void> connection_;
    // Destroyed after the presenter: the presenter's frame provider uses the renderer.
    std::unique_ptr<ViewportRenderer> renderer_;
    std::unique_ptr<dvv::gpu::Presenter> presenter_;
    std::function<void(std::uint64_t)> device_lost_;
    std::function<void(const dvv::Error&)> failure_;
    std::function<void()> view_changed_;  // setViewChangedHandler()
    std::optional<dvv::Error> last_failure_;
    ViewRequest view_{};                // view()
    dvv::platform::PixelSize image_{};  // imageSize()
    struct Drag {
        wxPoint last;          // the last pointer position (window units)
        wxMouseButton button;  // the button that started it: wxMOUSE_BTN_LEFT or _MIDDLE
    };
    std::optional<Drag> drag_;  // during a drag
    // The zoom shown at the current pinch's start; 0 between gestures.
    double pinch_start_zoom_ = 0;
#if !defined(__WXOSX__)
    double wheel_zoom_ = 0;  // Ctrl+wheel rotation not yet a whole notch (takeWheelNotches)
#endif
};

}  // namespace rawreel
