// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/viewport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <utility>

#include <wx/settings.h>

#include "dvv/base/log.h"
#include "gui/native_window.h"

namespace rawreel {

namespace {

// The compositor's background greys (Task 7: dark 0.12, light 0.85, sRGB-encoded) in 8 bits.
wxColour backgroundColour(bool dark) {
    return dark ? wxColour(31, 31, 31) : wxColour(217, 217, 217);
}

}  // namespace

Viewport::Viewport(wxWindow* parent, std::shared_ptr<DisplayState> display)
    : display_(std::move(display)), dark_(wxSystemSettings::GetAppearance().IsDark()) {
    // The Fit view; the renderer resolves the view against the presenter's extent, so only
    // setView() sets the params again.
    display_->setParams(CompositeParams{.dark = dark_});
    // wx never erases the background, so nothing paints over the presented image.
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    // wxWANTS_CHARS: the arrows and Space reach the frame's char hook instead of moving the focus.
    Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
           wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS);
#if defined(__WXGTK__)
    // Vulkan presents to the window directly; GTK's double buffer would paint over it. Only after
    // Create(): before it the widget does not exist and wx asserts.
    SetDoubleBuffered(false);
#endif
    // View zoom design §5: a pinch zooms. wxOSX attaches its gesture recognizer only here;
    // wxEVT_MAGNIFY is not bound.
    EnableTouchEvents(wxTOUCH_ZOOM_GESTURE);
    // wxOSX needs nothing here: the presenter draws to a child NSView above this window's view
    // (viewport_cocoa.mm), which wx's drawing does not reach.
    Bind(wxEVT_PAINT, &Viewport::onPaint, this);
    Bind(wxEVT_SIZE, &Viewport::onSize, this);
    // R5: a click gives the viewport the keyboard focus. View zoom design §5: a left- or
    // middle-button drag pans.
    Bind(wxEVT_LEFT_DOWN, &Viewport::onButtonDown, this);
    Bind(wxEVT_LEFT_DCLICK, &Viewport::onButtonDown, this);
    Bind(wxEVT_MIDDLE_DOWN, &Viewport::onButtonDown, this);
    Bind(wxEVT_MIDDLE_DCLICK, &Viewport::onButtonDown, this);
    Bind(wxEVT_MOTION, &Viewport::onMotion, this);
    Bind(wxEVT_LEFT_UP, &Viewport::onButtonUp, this);
    Bind(wxEVT_MIDDLE_UP, &Viewport::onButtonUp, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &Viewport::onCaptureLost, this);
    Bind(wxEVT_MOUSEWHEEL, &Viewport::onWheel, this);
    Bind(wxEVT_GESTURE_ZOOM, &Viewport::onZoomGesture, this);
    Bind(wxEVT_RIGHT_DOWN, &Viewport::onMouseDown, this);
    Bind(wxEVT_AUX1_DOWN, &Viewport::onMouseDown, this);
    Bind(wxEVT_AUX2_DOWN, &Viewport::onMouseDown, this);
}

Viewport::~Viewport() {
    // A close during a drag: wx must not destroy a window holding the capture. HasCapture(), the
    // public test: wx's capture stack (IsInCaptureStack) is not public API.
    if (HasCapture()) {
        ReleaseMouse();
    }
    detachStack();  // the presenter thread is joined while the native window still exists
}

void Viewport::setStack(const std::weak_ptr<dvv::gpu::ExecutionContext>& context,
                        std::uint64_t epoch) {
    stopPresenting();           // the old presenter and renderer hold the old context
    context_ = context.lock();  // GUI-held while current; released here on the next change
    epoch_ = epoch;
    if (!context_) {
        Refresh();  // no presenter: the paint handler fills the background
        return;
    }
    startPresenting();
}

void Viewport::markReady() {
    ready_ = true;
    startPresenting();
}

void Viewport::startPresenting() {
    if (!ready_ || !context_ || presenter_) {
        return;
    }
    // On failure paths below, `window` (and its connection) is released after the failed creation.
    dvv::Result<ViewportWindow> window = nativeWindowOf(*this);
    if (!window) {
        reportFailure(window.error());
        return;
    }
    if (std::exchange(fail_next_start_, false)) {
        reportFailure(
            dvv::makeError(dvv::Errc::Internal, "injected presenter creation failure").error());
        return;
    }
    auto renderer = ViewportRenderer::create(context_, epoch_);
    if (!renderer) {
        reportFailure(renderer.error());
        return;
    }
    renderer_ = std::move(*renderer);
    const std::uint64_t serial = ++serial_;
    const bool visible = IsShownOnScreen() && !iconized_;
    auto presenter = context_->createPresenter(
        {.window = window->window,
         .extent = physicalClientSize(),
         .visible = visible,
         .frames =
             [display = display_, renderer = renderer_.get(),
              fail = fail_presenters_](const dvv::gpu::PresentFrameParams& params) {
                 // Presenter thread: one consistent read of the state, then record (design §5.3).
                 const DisplaySnapshot snapshot = display->snapshot();
                 dvv::Result<dvv::gpu::PresentFrame> frame = renderer->frame(params, snapshot);
                 if (fail && frame) {
                     // Test only: the renderer's record function is dropped unrun (as after a
                     // failed acquire); the failed recording fails the presenter.
                     frame->record = [](dvv::gpu::CommandRecorder& /*recorder*/) {
                         throw std::runtime_error("injected presenter failure");
                     };
                 }
                 return frame;
             },
         .onError =
             [this, serial](const dvv::Error& error) noexcept {
                 // Presenter thread. CallAfter allocates, so it may throw; the handler may not.
                 try {
                     CallAfter([this, serial, error] { onPresenterError(serial, error); });
                 } catch (...) {
                     std::fputs("rawreel-gui: a presenter error could not reach the GUI\n", stderr);
                 }
             },
         .name = "viewport"});
    if (!presenter) {
        renderer_.reset();
        reportFailure(presenter.error());
        return;
    }
    presenter_ = std::move(*presenter);
    presenter_visible_ = visible;
    connection_ = std::move(window->connection);
    last_failure_.reset();
    // Safe with a raw pointer: stopPresenting() clears the callback before the presenter dies,
    // and after setOnChanged({}) returns no callback runs (DisplayState binding rule).
    dvv::gpu::Presenter* raw = presenter_.get();
    display_->setOnChanged([raw] { raw->requestFrame(); });
    presenter_->requestFrame();  // once: a paused frame is presented without a change (§5.3)
}

// Creation errors: DeviceLost goes to the Session, anything else to the D25 failure hook.
void Viewport::reportFailure(const dvv::Error& error) {
    DVV_LOG_ERROR("viewport: {}", error.describe());
    last_failure_ = error;
    if (error.code == dvv::Errc::DeviceLost) {
        if (device_lost_) {
            device_lost_(epoch_);
        }
    } else if (failure_) {
        failure_(error);
    }
    Refresh();
}

void Viewport::stopPresenting() {
    display_->setOnChanged(
        {});  // first (M1b order): no callback can reach the presenter after this
    // reset() clears the member before ~Presenter runs, so a handler of this window that runs
    // while the presenter waits (Windows dispatches cross-thread sent messages) sees no presenter.
    presenter_.reset();   // joins the presenter thread (about one frame)
    renderer_.reset();    // after the presenter: its provider used the renderer
    connection_.reset();  // after the presenter's surface and swapchain; the window still exists
}

void Viewport::detachStack() {
    stopPresenting();
    context_.reset();
}

void Viewport::setDeviceLostHandler(std::function<void(std::uint64_t)> handler) {
    device_lost_ = std::move(handler);
}

void Viewport::setFailureHandler(std::function<void(const dvv::Error&)> handler) {
    failure_ = std::move(handler);
}

void Viewport::requestFrame() {
    if (presenter_) {
        presenter_->requestFrame();
    }
}

void Viewport::onPresenterError(std::uint64_t serial, const dvv::Error& error) {
    if (serial != serial_) {
        return;  // an older presenter's report
    }
    DVV_LOG_ERROR("viewport: {}", error.describe());
    const std::optional<dvv::Error> failure = presenter_ ? presenter_->failure() : std::nullopt;
    if (!failure) {
        return;  // a skipped frame; the presenter goes on
    }
    // Kept for the selftest runner: both branches below drop the presenter (D25 at once, a lost
    // device at the Session's stackChanged), usually before the runner's next tick.
    last_failure_ = failure;
    if (failure->code == dvv::Errc::DeviceLost || error.code == dvv::Errc::DeviceLost) {
        if (device_lost_) {
            device_lost_(epoch_);  // the Session releases the stack and sends stackChanged({})
        }
        return;
    }
    // D25: M1b Failed is terminal and is not recreated; the user's Reopen rebuilds the stack.
    stopPresenting();
    Refresh();
    if (failure_) {
        failure_(*failure);
    }
}

void Viewport::topLevelIconized(bool iconized) {
    iconized_ = iconized;
    const wxWindow* top = wxGetTopLevelParent(this);
    // Before the one target, a restore made the presenter visible even while the frame was hidden.
    updateVisible(top != nullptr && top->IsShown(), !iconized, "restore");
}

void Viewport::topLevelShown(bool shown) {
    // Before the one target, a show made the presenter visible even while the frame was iconized.
    updateVisible(shown, shown, "show");
}

void Viewport::updateVisible(bool shown, bool old_visible, const char* event) {
    if (!presenter_) {
        return;  // a new presenter starts from IsShownOnScreen() && !iconized_
    }
    const bool visible = shown && !iconized_;
    if (old_visible && !visible && !presenter_visible_) {
        // wxGTK sends asynchronous iconize toggles and a show event on map (selftest step 4).
        DVV_LOG_INFO("viewport: {} ignored: the frame is {}", event,
                     iconized_ ? "iconized" : "hidden");
    }
    if (visible == presenter_visible_) {
        return;
    }
    presenter_visible_ = visible;
    if (visible) {
        ++presenter_shows_;
    }
    if (visible) {
        presenter_->show();
    } else {
        presenter_->hide();
    }
}

std::optional<RecordedInfo> Viewport::lastRecorded() const {
    // The renderer exists only while a stack is current; lastRecorded() is safe on this thread.
    return renderer_ ? std::optional(renderer_->lastRecorded()) : std::nullopt;
}

dvv::platform::PixelSize Viewport::physicalClientSize() const {
#if defined(__WXOSX__)
    // The layer's own scale (it follows the backing scale and the backing_scale test hook).
    return nativePixelSize(*this);
#else
    const wxSize size = ToPhys(GetClientSize());
    return {.width = static_cast<std::uint32_t>(std::max(size.x, 0)),
            .height = static_cast<std::uint32_t>(std::max(size.y, 0))};
#endif
}

void Viewport::onPaint(wxPaintEvent& /*event*/) {
    wxPaintDC dc(this);  // validates the update region, whatever is drawn
    if (!presenter_) {
        // No stack, a lost device, a failed presenter or not ready yet: the compositor's grey.
        dc.SetBackground(wxBrush(backgroundColour(dark_)));
        dc.Clear();
        return;
    }
    presenter_->requestFrame();
}

void Viewport::onMouseDown(wxMouseEvent& event) {
    SetFocus();
    event.Skip();
}

void Viewport::onButtonDown(wxMouseEvent& event) {
    SetFocus();
    event.Skip();
    // A second button during a drag starts no second capture.
    if (drag_ || !canPan(view_, physicalClientSize(), image_)) {
        return;
    }
    CaptureMouse();
    // The standard wx cursors: the closed hand (macOS), the move cursor (Windows), fleur (wxGTK
    // maps wxCURSOR_SIZENWSE to it).
#if defined(__WXOSX__)
    SetCursor(wxCursor(wxCURSOR_CLOSED_HAND));
#elif defined(__WXMSW__)
    SetCursor(wxCursor(wxCURSOR_SIZING));
#elif defined(__WXGTK__)
    SetCursor(wxCursor(wxCURSOR_SIZENWSE));
#endif
    // Bound to left and middle presses and double-clicks only.
    const wxMouseButton button =
        event.Button(wxMOUSE_BTN_LEFT) ? wxMOUSE_BTN_LEFT : wxMOUSE_BTN_MIDDLE;
    drag_ = Drag{.last = event.GetPosition(), .button = button};
}

void Viewport::onMotion(wxMouseEvent& event) {
    event.Skip();
    if (!drag_) {
        return;
    }
    if (!event.ButtonIsDown(drag_->button)) {
        if (HasCapture()) {
            ReleaseMouse();
        }
        endDrag();
        return;
    }
    const wxPoint position = event.GetPosition();
    const wxPoint delta = position - drag_->last;
    drag_->last = position;
    const double scale = contentScale();  // window units to backing pixels
    setView(panBy(view_, physicalClientSize(), image_, delta.x * scale, delta.y * scale));
}

void Viewport::onButtonUp(wxMouseEvent& event) {
    event.Skip();
    if (!drag_ || !event.ButtonUp(drag_->button)) {
        return;
    }
    if (HasCapture()) {
        ReleaseMouse();
    }
    endDrag();
}

void Viewport::onCaptureLost(wxMouseCaptureLostEvent& /*event*/) {
    if (drag_) {
        endDrag();  // wx has already taken the capture away
    }
}

void Viewport::endDrag() {
    SetCursor(wxNullCursor);
    drag_.reset();
}

void Viewport::onWheel(wxMouseEvent& event) {
    if (image_.empty()) {
        event.Skip();
        return;
    }
    const dvv::platform::PixelSize extent = physicalClientSize();
    const bool vertical = event.GetWheelAxis() == wxMOUSE_WHEEL_VERTICAL;
#if !defined(__WXOSX__)
    // macOS has no scroll zoom (design §5): there the wheel pans with Cmd too.
    if (event.ControlDown() && vertical) {
        const int notches =
            takeWheelNotches(wheel_zoom_, event.GetWheelRotation(), event.GetWheelDelta());
        const double scale = contentScale();  // window units to backing pixels
        const PointD anchor{.x = event.GetPosition().x * scale, .y = event.GetPosition().y * scale};
        ViewRequest request = view_;
        for (int i = 0; i < std::abs(notches); ++i) {
            const std::optional<ViewRequest> next =
                stepZoom(request, extent, image_, notches > 0, anchor);
            if (!next) {
                break;  // at the limit
            }
            request = *next;
        }
        setView(request);
        return;
    }
    if (!event.ControlDown()) {
        wheel_zoom_ = 0;  // a half notch counts only within one run of Ctrl+wheel
    }
#endif
    if (!canPan(view_, extent, image_)) {
        return;
    }
    const double per_dip = backingPerDip();
    const int lines = event.IsPageScroll()
                          ? -1
                          : (vertical ? event.GetLinesPerAction() : event.GetColumnsPerAction());
    const double page_dip = (vertical ? extent.height : extent.width) / per_dip;
    const double move = wheelPanDip(event.GetWheelRotation(), event.GetWheelDelta(), lines,
                                    WHEEL_LINE_DIP, page_dip) *
                        per_dip;
    if (vertical) {
        setView(panBy(view_, extent, image_, 0, move));
    } else {
        setView(panBy(view_, extent, image_, -move, 0));
    }
}

void Viewport::onZoomGesture(wxZoomGestureEvent& event) {
    if (image_.empty()) {
        if (event.IsGestureEnd()) {
            pinch_start_zoom_ = 0;  // a clip closed mid-pinch: no stale start zoom
        }
        event.Skip();
        return;
    }
    const double factor = event.GetZoomFactor();
    if (std::isfinite(factor) && factor > 0) {
        const double shown = shownView().zoom();
        // A gesture whose start was not seen starts at the zoom shown now.
        if (event.IsGestureStart() || !(pinch_start_zoom_ > 0)) {
            pinch_start_zoom_ = shown / factor;
        }
        const ZoomLimits limits = zoomLimits(fitZoom());
        const double zoom = std::clamp(pinch_start_zoom_ * factor, limits.minimum, limits.maximum);
        // The zoom shown already (a start at factor 1): no setView, so Fit stays Fit.
        if (std::abs(zoom - shown) > 1e-6 * shown) {
            const double scale = contentScale();  // window units to backing pixels
            const PointD anchor{.x = event.GetPosition().x * scale,
                                .y = event.GetPosition().y * scale};
            setView(zoomAround(view_, physicalClientSize(), image_, zoom, anchor));
        }
    }
    if (event.IsGestureEnd()) {
        pinch_start_zoom_ = 0;
    }
}

void Viewport::resizePresenter() {
    // Only the presenter is resized: the renderer resolves the view against its extent.
    if (presenter_) {
        syncNativeWindow(*this);
        presenter_->resize(physicalClientSize());
    }
    if (view_changed_) {
        view_changed_();  // the shown zoom follows the extent (a Fit view, a clamped centre)
    }
}

void Viewport::setView(const ViewRequest& request) {
    if (request == view_) {
        return;
    }
    view_ = request;
    display_->setParams(CompositeParams{.dark = dark_, .view = view_});
    if (view_changed_) {
        view_changed_();
    }
}

void Viewport::setImageSize(dvv::platform::PixelSize image) noexcept {
#if !defined(__WXOSX__)
    if (image != image_) {
        wheel_zoom_ = 0;  // a new clip: no half notch carries over
    }
#endif
    image_ = image;
}

ViewTransform Viewport::shownView() const {
    return resolveView(view_, physicalClientSize(), image_);
}

double Viewport::fitZoom() const {
    return ViewTransform::fit(physicalClientSize(), image_).zoom();
}

double Viewport::contentScale() const {
    return GetContentScaleFactor();
}

double Viewport::backingPerDip() const {
#if defined(__WXMSW__)
    return GetDPIScaleFactor();
#else
    return contentScale();
#endif
}

void Viewport::setViewChangedHandler(std::function<void()> handler) {
    view_changed_ = std::move(handler);
}

void Viewport::onSize(wxSizeEvent& event) {
    resizePresenter();
    event.Skip();
}

}  // namespace rawreel
