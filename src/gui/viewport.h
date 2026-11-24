#pragma once

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <cstdint>
#include <memory>
#include <optional>

#include "core/overlay.h"
#include "dvv/base/error.h"
#include "dvv/base/move_only_function.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/presenter.h"
#include "dvv/media/view.h"
#include "dvv/platform/pixel_size.h"
#include "gui/overlay_params.h"
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
    // Sets the display's view to Fit and its overlay to the default GridConfig and RulerConfig,
    // converted at the window's scale and theme (sendOverlay), until setOverlayConfig(). Only
    // setView() changes the view later; the renderer resolves the view against P (the extent minus
    // the ruler strips) and the frame size, so a resize sets no view.
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
    void setDeviceLostHandler(dvv::MoveOnlyFunction<void(std::uint64_t)> handler);
    // D25: called once with the failure when the presenter fails with an error other than
    // DeviceLost, or when renderer or presenter creation fails with such an error. Nothing is
    // recreated; MainFrame::onPresenterFailed logs it (Task 9: infobar with Reopen).
    void setFailureHandler(dvv::MoveOnlyFunction<void(const dvv::Error&)> handler);
    void requestFrame();  // presenter->requestFrame() when there is one
    // The resize entry: the overlay converted again at the current scale (the strips follow it),
    // then, with a presenter, syncNativeWindow() and presenter->resize(physicalClientSize()).
    // onSize and onDpiChanged call it, and on macOS so does the child view's
    // viewDidChangeBackingProperties (a display-scale change sends no wxEVT_SIZE). GUI thread.
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
    // when it changed, display->setView(request), then the view handler.
    void setView(const dvv::media::ViewRequest& request);
    [[nodiscard]] const dvv::media::ViewRequest& view() const noexcept { return view_; }
    // The stream's frame size; {} without a stream (MainFrame::onStreamsChanged). A new size drops
    // the Ctrl+wheel rotation not yet a whole notch. Then updateCursor() and the view handler.
    void setImageSize(dvv::platform::PixelSize image);
    [[nodiscard]] dvv::platform::PixelSize imageSize() const noexcept { return image_; }
    // The grid and ruler settings: stored, converted with convertOverlay() at the current scale,
    // theme and font and sent (DisplayState::setOverlay); P and a Fit view follow, then the view
    // handler.
    void setOverlayConfig(const GridConfig& grid, const RulerConfig& rulers);
    // The params last sent.
    [[nodiscard]] const OverlayParams& overlayParams() const noexcept { return overlay_.params; }
    // The picture region P: dvv::media::pictureRegion(physicalClientSize(), the strips). Every
    // view computation resolves against its size.
    [[nodiscard]] dvv::platform::PixelSize pictureSize() const;
    // P's origin in the viewport: (left strip, top strip), backing pixels.
    [[nodiscard]] dvv::media::PointD pictureOffset() const;
    // The ruler strips' thickness, whole window units; 0 while the rulers are hidden.
    [[nodiscard]] int stripWindowUnits() const noexcept { return overlay_.strip.windowUnits; }
    // The backing point of a window position, relative to P:
    // (position + 0.5) x contentScale() - pictureOffset().
    [[nodiscard]] dvv::media::PointD picturePoint(wxPoint position) const;
    // The window position's backing point lies in P (PixelRect::contains), not in a reserved strip.
    [[nodiscard]] bool inPicture(wxPoint position) const;
    // The window position's backing point lies in a drawn ruler: the top, left or corner rect of
    // dvv::media::rulerRects(physicalClientSize(), the strips, shownView()), the rects the rulers
    // hug the visible picture with. A press there starts no pan; the background beside them pans.
    [[nodiscard]] bool onRuler(wxPoint position) const;
    // The view as shown: resolveView(view(), pictureSize(), imageSize()).
    [[nodiscard]] dvv::media::ViewTransform shownView() const;
    // The fit's zoom: ViewTransform::fit(pictureSize(), imageSize()).zoom(); 0 without an image.
    [[nodiscard]] double fitZoom() const;
    // Backing pixels per window unit, the scale of physicalClientSize(): nativeContentScale() (1 on
    // Windows, where window units are physical pixels).
    [[nodiscard]] double contentScale() const;
    // Backing pixels per DIP: GetDPIScaleFactor() on Windows, contentScale() elsewhere.
    [[nodiscard]] double backingPerDip() const;
    // Called after setView() changed the request, after setOverlayConfig(), after setImageSize()
    // and at the end of every resizePresenter().
    void setViewChangedHandler(dvv::MoveOnlyFunction<void()> handler);
    // The image pixel under the pointer, dvv::media::cursorPixel(shownView(), P, the pointer's
    // backing point); nullopt off the image, over a strip, without an image or after the pointer
    // left. updateCursor() maps it on every motion (with or without a drag), on a leave and after
    // every change of the view, P or the image (setView, resizePresenter, setImageSize,
    // setOverlayConfig), so it follows the picture under a still pointer. The handler hears every
    // change of the pixel; moves never run the view handler.
    void setCursorHandler(
        dvv::MoveOnlyFunction<void(std::optional<dvv::media::ImagePixel>)> handler);
    [[nodiscard]] std::optional<dvv::media::ImagePixel> cursorPixel() const noexcept {
        return cursor_pixel_;
    }
    // A context menu request (a right-click, the menu key, Shift+F10): the handler gets the point
    // in client coordinates; one from the keyboard (wxDefaultPosition) becomes the centre of the
    // client area. Without a handler the event goes on to the parent.
    void setContextMenuHandler(dvv::MoveOnlyFunction<void(wxPoint)> handler);
    // Test hook (selftest): how often the cursor was sent to the display.
    [[nodiscard]] std::uint64_t cursorPushesForTest() const noexcept { return cursor_pushes_; }
    // Test hook (selftest): how often the view handler ran.
    [[nodiscard]] std::uint64_t viewChangesForTest() const noexcept { return view_changes_; }
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
    // A display of another scale (wxMSW, wxGTK): the overlay and P follow (resizePresenter).
    void onDpiChanged(wxDPIChangedEvent& event);
    // The light or dark theme changed: dark_ follows, the overlay is sent again, Refresh().
    void onSysColourChanged(wxSysColourChangedEvent& event);
    // overlay_ = convertOverlay() of grid_ and rulers_ at the current scale, theme and font, its
    // params sent to the display.
    void sendOverlay();
    // The backing point of a window position: (position + 0.5) x contentScale().
    [[nodiscard]] dvv::media::PointD backingPoint(wxPoint position) const;
    // picturePoint() moved into P (its edges included): the anchor of a wheel or pinch zoom.
    [[nodiscard]] dvv::media::PointD pictureAnchor(wxPoint position) const;
    void onMouseDown(wxMouseEvent& event);  // SetFocus() on a right or aux button press (R5)
    void onContextMenu(wxContextMenuEvent& event);  // the handler, or Skip() without one
    // Left and middle press and double-click: SetFocus(); with canPan(), a drag starts (mouse
    // capture, the drag cursor).
    void onButtonDown(wxMouseEvent& event);
    // During a drag: panBy() the pointer's move; with the drag's button no longer down (wxOSX may
    // not send its release, e.g. during a menu or a modal loop), ReleaseMouse() and endDrag().
    void onMotion(wxMouseEvent& event);
    // The drag's button released: ReleaseMouse(), endDrag(). Another button's release does not.
    void onButtonUp(wxMouseEvent& event);
    void onCaptureLost(wxMouseCaptureLostEvent& event);  // ends a drag without ReleaseMouse()
    // The pointer left the window: no cursor (updateCursor).
    void onLeave(wxMouseEvent& event);
    // cursor_pixel_ from position_ (cursorPixel()), the cursor handler when it changed. The display
    // gets the pointer's backing point (DisplayState::setCursor) only while the rulers are shown
    // and only when the pixel the display last got differs from this one (a new pixel, the image
    // left, or the last point now maps to another pixel); nullopt once when the rulers are hidden.
    void updateCursor();
    void endDrag();            // the default cursor, drag_ reset
    void notifyViewChanged();  // the view handler (set: the callers check), counted
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
    bool dark_ = false;     // the theme: the overlay's colours, the paint handler's background
    GridConfig grid_{};     // setOverlayConfig()
    RulerConfig rulers_{};  // setOverlayConfig()
    OverlayConversion overlay_{};  // overlayParams() and the strip it holds
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
    // What the window lends the presenter (ViewportWindow::lent); released after the presenter.
    std::shared_ptr<void> lent_;
    // Destroyed after the presenter: the presenter's frame provider uses the renderer.
    std::unique_ptr<ViewportRenderer> renderer_;
    std::unique_ptr<dvv::gpu::Presenter> presenter_;
    dvv::MoveOnlyFunction<void(std::uint64_t)> device_lost_;
    dvv::MoveOnlyFunction<void(const dvv::Error&)> failure_;
    dvv::MoveOnlyFunction<void()> view_changed_;  // setViewChangedHandler()
    dvv::MoveOnlyFunction<void(std::optional<dvv::media::ImagePixel>)> cursor_changed_;
    dvv::MoveOnlyFunction<void(wxPoint)> context_menu_;   // setContextMenuHandler()
    std::optional<wxPoint> position_;                     // the pointer (window units); none: left
    std::optional<dvv::media::ImagePixel> cursor_pixel_;  // cursorPixel()
    std::optional<dvv::media::PointD> pushed_point_;      // the point the display last got
    std::optional<dvv::media::ImagePixel> pushed_pixel_;  // its pixel; none: the display has none
    std::uint64_t cursor_pushes_ = 0;                     // cursorPushesForTest()
    std::uint64_t view_changes_ = 0;                      // viewChangesForTest()
    std::optional<dvv::Error> last_failure_;
    dvv::media::ViewRequest view_{};    // view()
    dvv::platform::PixelSize image_{};  // imageSize()
    struct Drag {
        wxPoint last;          // the last pointer position (window units)
        wxMouseButton button;  // the button that started it: wxMOUSE_BTN_LEFT or _MIDDLE
    };
    std::optional<Drag> drag_;  // during a drag
    // The zoom shown at the current pinch's start; 0 between gestures.
    double pinch_start_zoom_ = 0;
    double wheel_zoom_ = 0;  // Ctrl+wheel rotation not yet a whole notch (takeWheelNotches)
};

}  // namespace rawreel
