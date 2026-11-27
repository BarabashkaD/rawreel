// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/viewport.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <wx/settings.h>

#include "core/frame_set.h"
#include "core/inspector.h"
#include "core/inspector_text.h"
#include "core/overlay.h"
#include "core/ruler_labels.h"
#include "core/view.h"
#include "dvv/base/error.h"
#include "dvv/base/log.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "gui/icons.h"
#include "gui/native_policy.h"
#include "gui/overlay_params.h"
#include "gui/viewport_window.h"
#include "render/frame_feed.h"
#include "render/view_layout.h"
#include "render/view_state.h"

using dvv::media::canPan;
using dvv::media::panBy;
using dvv::media::PointD;
using dvv::media::resolveView;
using dvv::media::ViewRequest;
using dvv::media::ViewTransform;
using dvv::media::zoomAround;

namespace rawreel {

namespace {

// The view's background greys (Task 7: dark 0.12, light 0.85, sRGB-encoded) in 8 bits.
wxColour backgroundColour(bool dark) {
    return dark ? wxColour(31, 31, 31) : wxColour(217, 217, 217);
}

}  // namespace

double rulerFontPoints(const RulerFont& font) {
    return font.size != 0 ? static_cast<double>(font.size) : wxSMALL_FONT->GetFractionalPointSize();
}

Viewport::Viewport(wxWindow* parent, std::shared_ptr<FrameFeed> feed)
    : feed_(std::move(feed)), dark_(wxSystemSettings::GetAppearance().IsDark()) {
    // Safe with `this`: the destructor drops the subscription (a member after wake_) and the view
    // state's callback before wake_mutex_ dies, and no callback runs after either returns.
    subscription_ = feed_->subscribe(ViewRoles::of(ViewRole::Display), [this] { wakePresenter(); });
    view_state_->setOnChanged([this] { wakePresenter(); });
    // The Fit view; the renderer resolves the view against the presenter's extent, so only
    // setView() sets the view again.
    view_state_->setView(view_);
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
    // The default settings converted at the new window's scale and theme (setOverlayConfig()
    // replaces them); needs the window for its scale and wx's small font.
    sendOverlay();
    // View zoom design §5: a pinch zooms. wxOSX attaches its gesture recognizer only here;
    // wxEVT_MAGNIFY is not bound.
    EnableTouchEvents(wxTOUCH_ZOOM_GESTURE);
    // wxOSX needs nothing here: the presenter draws to a child NSView above this window's view
    // (viewport_cocoa.mm), which wx's drawing does not reach.
    Bind(wxEVT_PAINT, &Viewport::onPaint, this);
    Bind(wxEVT_SIZE, &Viewport::onSize, this);
    Bind(wxEVT_DPI_CHANGED, &Viewport::onDpiChanged, this);
    Bind(wxEVT_SYS_COLOUR_CHANGED, &Viewport::onSysColourChanged, this);
    // R5: a click gives the viewport the keyboard focus. View zoom design §5: a left- or
    // middle-button drag pans.
    Bind(wxEVT_LEFT_DOWN, &Viewport::onButtonDown, this);
    Bind(wxEVT_LEFT_DCLICK, &Viewport::onButtonDown, this);
    Bind(wxEVT_MIDDLE_DOWN, &Viewport::onButtonDown, this);
    Bind(wxEVT_MIDDLE_DCLICK, &Viewport::onButtonDown, this);
    Bind(wxEVT_MOTION, &Viewport::onMotion, this);
    Bind(wxEVT_LEAVE_WINDOW, &Viewport::onLeave, this);
    Bind(wxEVT_LEFT_UP, &Viewport::onButtonUp, this);
    Bind(wxEVT_MIDDLE_UP, &Viewport::onButtonUp, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &Viewport::onCaptureLost, this);
    Bind(wxEVT_MOUSEWHEEL, &Viewport::onWheel, this);
    Bind(wxEVT_GESTURE_ZOOM, &Viewport::onZoomGesture, this);
    Bind(wxEVT_RIGHT_DOWN, &Viewport::onMouseDown, this);
    Bind(wxEVT_AUX1_DOWN, &Viewport::onMouseDown, this);
    Bind(wxEVT_AUX2_DOWN, &Viewport::onMouseDown, this);
    Bind(wxEVT_CONTEXT_MENU, &Viewport::onContextMenu, this);
}

Viewport::~Viewport() {
    // A close during a drag: wx must not destroy a window holding the capture. HasCapture(), the
    // public test: wx's capture stack (IsInCaptureStack) is not public API.
    if (HasCapture()) {
        ReleaseMouse();
    }
    detachStack();  // the presenter thread is joined while the native window still exists
    view_state_->setOnChanged({});
    subscription_ = {};
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
    // On failure paths below, `window` (and what it lends) is released after the failed creation.
    dvv::Result<ViewportWindow> window = viewportWindowOf(*this);
    if (!window) {
        reportFailure(window.error());
        return;
    }
    if (std::exchange(fail_next_start_, false)) {
        reportFailure(
            dvv::makeError(dvv::Errc::Internal, "injected presenter creation failure").error());
        return;
    }
    auto renderer = ViewRenderer::create(context_, epoch_);
    if (!renderer) {
        reportFailure(renderer.error());
        return;
    }
    renderer_ = std::move(*renderer);
    const std::uint64_t serial = ++serial_;
    installReadbackBridge(serial);
    const bool visible = IsShownOnScreen() && !iconized_;
    auto presenter = createViewPresenter(
        *renderer_,
        [state = view_state_, subscription = &subscription_] {
            // Presenter thread: the feed's frames, then the params (design §5.3).
            FeedSnapshot feed = subscription->snapshot();
            return ViewSnapshot{.feed = std::move(feed), .params = state->params()};
        },
        {.window = window->window,
         .extent = physicalClientSize(),
         .visible = visible,
         .onError =
             [this, serial](const dvv::Error& error) noexcept {
                 // Presenter thread. CallAfter allocates, so it may throw; the handler may not.
                 try {
                     CallAfter([this, serial, error] { onPresenterError(serial, error); });
                 } catch (...) {
                     std::fputs("rawreel-gui: a presenter error could not reach the GUI\n", stderr);
                 }
             },
         .name = "viewport"},
        fail_presenters_);
    if (!presenter) {
        renderer_.reset();
        reportFailure(presenter.error());
        return;
    }
    presenter_ = std::move(*presenter);
    presenter_visible_ = visible;
    lent_ = std::move(window->lent);
    last_failure_.reset();
    // Safe with a raw pointer: stopPresenting() clears wake_ under wake_mutex_ before the
    // presenter dies, so no wake reaches it after that.
    {
        std::lock_guard lock(wake_mutex_);
        wake_ = presenter_.get();
    }
    presenter_->requestFrame();  // once: a paused frame is presented without a change (§5.3)
}

void Viewport::wakePresenter() {
    std::lock_guard lock(wake_mutex_);
    if (wake_ != nullptr) {
        wake_->requestFrame();
    }
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
    {
        // First (M1b order): no callback can reach the presenter after this; waits for a running
        // wake.
        std::lock_guard lock(wake_mutex_);
        wake_ = nullptr;
    }
    // reset() clears the member before ~Presenter runs, so a handler of this window that runs
    // while the presenter waits (Windows dispatches cross-thread sent messages) sees no presenter.
    presenter_.reset();  // joins the presenter thread (about one frame)
    renderer_.reset();   // after the presenter: its provider used the renderer
    lent_.reset();       // after the presenter's surface and swapchain; the window still exists
}

void Viewport::detachStack() {
    stopPresenting();
    context_.reset();
}

void Viewport::setDeviceLostHandler(dvv::MoveOnlyFunction<void(std::uint64_t)> handler) {
    device_lost_ = std::move(handler);
}

void Viewport::setFailureHandler(dvv::MoveOnlyFunction<void(const dvv::Error&)> handler) {
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

// On macOS the layer's own scale (it follows the backing scale and the backing_scale test hook).
dvv::platform::PixelSize Viewport::physicalClientSize() const {
    return nativePixelSize(*this);
}

void Viewport::onPaint(wxPaintEvent& /*event*/) {
    wxPaintDC dc(this);  // validates the update region, whatever is drawn
    if (!presenter_) {
        // No stack, a lost device, a failed presenter or not ready yet: the view's grey.
        dc.SetBackground(wxBrush(backgroundColour(dark_)));
        dc.Clear();
        return;
    }
    presenter_->requestFrame();
}

void Viewport::setContextMenuHandler(dvv::MoveOnlyFunction<void(wxPoint)> handler) {
    context_menu_ = std::move(handler);
}

void Viewport::onContextMenu(wxContextMenuEvent& event) {
    if (!context_menu_) {
        event.Skip();
        return;
    }
    const wxPoint position = event.GetPosition();
    const wxPoint client = position == wxDefaultPosition ? wxPoint(GetClientSize().GetWidth() / 2,
                                                                   GetClientSize().GetHeight() / 2)
                                                         : ScreenToClient(position);
    context_menu_(client);
}

void Viewport::onMouseDown(wxMouseEvent& event) {
    SetFocus();
    event.Skip();
}

void Viewport::onButtonDown(wxMouseEvent& event) {
    SetFocus();
    event.Skip();
    // A second button during a drag starts no second capture; a press on a ruler no pan, a press
    // on the background beside the rulers pans as one in the picture does.
    if (drag_ || onRuler(event.GetPosition())) {
        return;
    }
    // A left press with a tool on is a click until the pointer moves past the drag threshold.
    const bool click = event.Button(wxMOUSE_BTN_LEFT) && tool_ != ViewportTool::None && pick_;
    const bool pannable = canPan(view_, pictureSize(), image_);
    if (!pannable && !click) {
        return;
    }
    CaptureMouse();
    if (!click) {
        // The standard wx cursors: the closed hand (macOS), the move cursor (Windows), fleur
        // (wxGTK maps wxCURSOR_SIZENWSE to it).
        SetCursor(wxCursor(native_policy::panCursor()));
        pipette_cursor_set_ = false;
    }
    // Bound to left and middle presses and double-clicks only.
    const wxMouseButton button =
        event.Button(wxMOUSE_BTN_LEFT) ? wxMOUSE_BTN_LEFT : wxMOUSE_BTN_MIDDLE;
    drag_ = Drag{.origin = event.GetPosition(),
                 .last = event.GetPosition(),
                 .button = button,
                 .moved = !click};
}

void Viewport::onMotion(wxMouseEvent& event) {
    event.Skip();
    const wxPoint position = event.GetPosition();
    position_ = position;
    if (!drag_) {
        updateCursor();
        refreshCursor();
        return;
    }
    Drag& drag = *drag_;
    if (!event.ButtonIsDown(drag.button)) {
        if (HasCapture()) {
            ReleaseMouse();
        }
        endDrag();
        updateCursor();
        return;
    }
    if (!drag.moved) {
        // wx reports -1 for a metric the platform lacks; 4 is the common default.
        const int limit_x = std::max(wxSystemSettings::GetMetric(wxSYS_DRAG_X, this), 4);
        const int limit_y = std::max(wxSystemSettings::GetMetric(wxSYS_DRAG_Y, this), 4);
        if (std::abs(position.x - drag.origin.x) <= limit_x &&
            std::abs(position.y - drag.origin.y) <= limit_y) {
            updateCursor();
            return;
        }
        drag.moved = true;  // a drag: the pan includes the move so far
        if (canPan(view_, pictureSize(), image_)) {
            SetCursor(wxCursor(native_policy::panCursor()));
            pipette_cursor_set_ = false;
        }
    }
    const wxPoint delta = position - drag.last;
    drag.last = position;
    const double scale = contentScale();  // window units to backing pixels
    setView(panBy(view_, pictureSize(), image_, delta.x * scale, delta.y * scale));
    updateCursor();  // setView() maps it when the view changed; a pan at its limit did not
}

void Viewport::onButtonUp(wxMouseEvent& event) {
    event.Skip();
    if (!drag_ || !event.ButtonUp(drag_->button)) {
        return;
    }
    const bool click = !drag_->moved;
    if (HasCapture()) {
        ReleaseMouse();
    }
    endDrag();
    if (click && pick_) {
        if (const std::optional<dvv::media::ImagePixel> pixel = pixelAt(event.GetPosition())) {
            pick_(*pixel);
        }
    }
}

void Viewport::onCaptureLost(wxMouseCaptureLostEvent& /*event*/) {
    if (drag_) {
        endDrag();  // wx has already taken the capture away
    }
}

void Viewport::endDrag() {
    SetCursor(wxNullCursor);
    pipette_cursor_set_ = false;
    drag_.reset();
    refreshCursor();
}

void Viewport::onWheel(wxMouseEvent& event) {
    if (image_.empty()) {
        event.Skip();
        return;
    }
    const dvv::platform::PixelSize extent = pictureSize();
    const bool vertical = event.GetWheelAxis() == wxMOUSE_WHEEL_VERTICAL;
    // macOS has no scroll zoom (design §5): there the wheel pans with Cmd too.
    if (native_policy::ctrlWheelZooms() && event.ControlDown() && vertical) {
        const int notches =
            takeWheelNotches(wheel_zoom_, event.GetWheelRotation(), event.GetWheelDelta());
        const PointD anchor = pictureAnchor(event.GetPosition());
        ViewRequest request = view_;
        for (int i = 0; i < std::abs(notches); ++i) {
            const std::optional<ViewRequest> next = zoomStep(request, notches > 0, anchor);
            if (!next) {
                break;  // at the limit
            }
            request = *next;
        }
        setView(request);
        return;
    }
    if (native_policy::ctrlWheelZooms() && !event.ControlDown()) {
        wheel_zoom_ = 0;  // a half notch counts only within one run of Ctrl+wheel
    }
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
        const double wanted = pinch_start_zoom_ * factor;
        double zoom = wanted;
        if (zoom_rule_ == ZoomRule::WholePixels) {
            zoom = wholeZoom(wanted);
        } else {
            const ZoomLimits limits = zoomLimits(fitZoom());
            zoom = std::clamp(wanted, limits.minimum, limits.maximum);
        }
        // The zoom shown already (a start at factor 1): no setView, so Fit stays Fit. Whole
        // pixels: only a pinch that reaches another whole zoom changes the view.
        const bool changed = zoom_rule_ == ZoomRule::WholePixels
                                 ? zoom != wholeZoom(shown)
                                 : std::abs(zoom - shown) > 1e-6 * shown;
        if (changed) {
            setView(
                zoomAround(view_, pictureSize(), image_, zoom, pictureAnchor(event.GetPosition())));
        }
    }
    if (event.IsGestureEnd()) {
        pinch_start_zoom_ = 0;
    }
}

void Viewport::resizePresenter() {
    // The child's layer scale first (macOS: contentScale() reads it), then the overlay at that
    // scale (a new scale changes the strips), then the extent: the renderer resolves the view
    // against P of the new extent with the new strips.
    if (presenter_) {
        syncNativeWindow(*this);
    }
    sendOverlay();
    if (presenter_) {
        presenter_->resize(physicalClientSize());
    }
    updateCursor();
    if (view_changed_) {
        notifyViewChanged();  // the shown zoom follows the extent (a Fit view, a clamped centre)
    }
}

void Viewport::setView(const ViewRequest& wanted) {
    ViewRequest request = wanted;
    if (zoom_rule_ == ZoomRule::WholePixels && request.mode == dvv::media::ViewMode::Zoom) {
        request.zoom = wholeZoom(request.zoom);
    }
    if (request == view_) {
        return;
    }
    view_ = request;
    view_state_->setView(view_);
    updateCursor();
    if (view_changed_) {
        notifyViewChanged();
    }
}

void Viewport::setAlpha(bool on) {
    view_state_->setAlpha(on);
}

void Viewport::setPaddingMarks(bool on) {
    view_state_->setPaddingMarks(on);
}

void Viewport::setImageSize(dvv::platform::PixelSize image) {
#if !defined(__WXOSX__)
    if (image != image_) {
        wheel_zoom_ = 0;  // a new clip: no half notch carries over
    }
#endif
    image_ = image;
    updateCursor();
    if (view_changed_) {
        notifyViewChanged();
    }
}

ViewTransform Viewport::shownView() const {
    return resolveView(view_, pictureSize(), image_);
}

double Viewport::fitZoom() const {
    return ViewTransform::fit(pictureSize(), image_).zoom();
}

double Viewport::contentScale() const {
    return nativeContentScale(*this);
}

double Viewport::backingPerDip() const {
#if defined(__WXMSW__)
    return GetDPIScaleFactor();
#else
    return contentScale();
#endif
}

void Viewport::setViewChangedHandler(dvv::MoveOnlyFunction<void()> handler) {
    view_changed_ = std::move(handler);
}

void Viewport::notifyViewChanged() {
    ++view_changes_;
    view_changed_();
}

void Viewport::setCursorHandler(
    dvv::MoveOnlyFunction<void(std::optional<dvv::media::ImagePixel>)> handler) {
    cursor_changed_ = std::move(handler);
}

void Viewport::onLeave(wxMouseEvent& event) {
    event.Skip();
    position_.reset();
    updateCursor();
    refreshCursor();
}

void Viewport::updateCursor() {
    const dvv::media::PixelRect picture = pictureRect(pointerPanel());
    const ViewTransform view = resolveView(view_, picture.size(), image_);
    std::optional<PointD> point;  // the pointer's backing point in the whole viewport, on the image
    std::optional<dvv::media::ImagePixel> pixel;
    if (position_ && !image_.empty()) {
        const PointD at = backingPoint(*position_);
        pixel = dvv::media::cursorPixel(view, picture, at);
        if (pixel) {
            point = at;
        }
    }
    const auto push = [this](std::optional<PointD> to, std::optional<dvv::media::ImagePixel> at) {
        pushed_point_ = to;
        pushed_pixel_ = at;
        view_state_->setCursor(to);
        ++cursor_pushes_;
    };
    if (rulers_.show) {
        // The renderer maps the point with the view and P it draws: push again also when the
        // point the display has would now show another pixel.
        if (pixel != pushed_pixel_ ||
            (pushed_point_ && dvv::media::cursorPixel(view, picture, *pushed_point_) != pixel)) {
            push(point, pixel);
        }
    } else if (pushed_point_) {
        push(std::nullopt, std::nullopt);
    }
    if (pixel != cursor_pixel_) {
        cursor_pixel_ = pixel;
        if (cursor_changed_) {
            cursor_changed_(pixel);
        }
    }
}

void Viewport::onSize(wxSizeEvent& event) {
    resizePresenter();
    event.Skip();
}

void Viewport::onDpiChanged(wxDPIChangedEvent& event) {
    resizePresenter();  // the strips and P at the new scale; Fit refits
    if (tool_ == ViewportTool::Pipette) {
        pipette_cursor_ = pipetteCursor(*this);  // the new scale
        pipette_cursor_set_ = false;
        refreshCursor();
    }
    event.Skip();
}

void Viewport::onSysColourChanged(wxSysColourChangedEvent& event) {
    dark_ = wxSystemSettings::GetAppearance().IsDark();
    sendOverlay();
    Refresh();  // the paint handler's background without a presenter
    event.Skip();
}

void Viewport::setOverlayConfig(const GridConfig& grid, const RulerConfig& rulers) {
    grid_ = grid;
    rulers_ = rulers;
    sendOverlay();
    updateCursor();
    if (view_changed_) {
        notifyViewChanged();  // a Fit view refits P: the shown zoom follows
    }
}

void Viewport::sendOverlay() {
    const double points = rulerFontPoints(rulers_.font);
    const double per_dip = backingPerDip();
    const OverlayScale scale{.display = {.backingPerDip = per_dip, .contentScale = contentScale()},
                             .fontDip = points * native_policy::dipPerFontPoint()};
    overlay_ = convertOverlay(grid_, rulers_, scale, dark_);
    if (rulers_.show && rulers_.numbers) {
        overlay_.params.glyphs = glyphStrip(ruler_glyphs_, RULER_DIGITS,
                                            {.family = rulers_.font.family, .sizePoints = points},
                                            native_policy::dipPerFontPoint() * per_dip);
    } else {
        ruler_glyphs_ = {};  // no numbers drawn: no strip held
    }
    view_state_->setOverlay(overlay_.params);  // notifies only when the params changed
    refreshValueGlyphs();
}

std::shared_ptr<const dvv::gpu::GlyphStrip> Viewport::glyphStrip(GlyphCache& cache,
                                                                 std::string_view glyphs,
                                                                 const dvv::gpu::FontSpec& font,
                                                                 double scale) {
    if (font == cache.font && scale == cache.scale) {
        return cache.strip;
    }
    cache.font = font;
    cache.scale = scale;
    cache.strip.reset();
    dvv::Result<dvv::gpu::GlyphStrip> strip = dvv::gpu::rasterGlyphStrip(font, scale, glyphs);
    if (strip) {
        cache.strip = std::make_shared<const dvv::gpu::GlyphStrip>(std::move(*strip));
    } else if (std::ranges::find(glyph_warned_, font) == glyph_warned_.end()) {
        glyph_warned_.push_back(font);
        DVV_LOG_WARN("glyph strip off: the font '{}' at {} pt failed: {}",
                     font.family.empty() ? "System" : font.family, font.sizePoints,
                     strip.error().message);
    }
    return cache.strip;
}

void Viewport::refreshValueGlyphs() {
    if (!value_glyphs_on_) {
        value_glyphs_ = {};
    } else {
        const double points = rulerFontPoints(rulers_.font);
        value_glyphs_.strip = glyphStrip(value_glyphs_, VALUE_GLYPHS,
                                         {.family = rulers_.font.family, .sizePoints = points},
                                         native_policy::dipPerFontPoint() * backingPerDip());
    }
    sendInspect();  // notifies only when the params changed
}

void Viewport::sendInspect() {
    InspectParams params = inspect_;
    params.glyphs = value_glyphs_.strip;
    view_state_->setInspect(params);
}

void Viewport::setInspect(const InspectParams& inspect) {
    inspect_ = inspect;
    sendInspect();
}

void Viewport::setMarkers(const MarkerParams& markers) {
    markers_ = markers;
    view_state_->setMarkers(markers);
}

void Viewport::setReadbackHandler(dvv::MoveOnlyFunction<void(ViewReadback)> handler) {
    readback_handler_ = std::move(handler);
    if (renderer_) {
        installReadbackBridge(serial_);
    }
}

void Viewport::installReadbackBridge(std::uint64_t serial) {
    if (!readback_handler_) {
        renderer_->setReadbackHandler({});
        return;
    }
    renderer_->setReadbackHandler([this, serial](ViewReadback readback) {
        // Presenter thread: it must not block. CallAfter allocates, so it may throw.
        try {
            CallAfter([this, serial, readback] {
                if (serial == serial_ && readback_handler_) {
                    readback_handler_(readback);
                }
            });
        } catch (...) {
            std::fputs("rawreel-gui: a readback could not reach the GUI\n", stderr);
        }
    });
}

void Viewport::setValueGlyphs(bool on) {
    value_glyphs_on_ = on;
    refreshValueGlyphs();
}

void Viewport::setFeedRoles(ViewRoles roles) {
    if (subscription_ && roles != ViewRoles{}) {
        // In place, under the feed's mutex: the presenter thread may be reading this subscription.
        subscription_.setRoles(roles);
        return;
    }
    // A subscription is made or dropped only while no presenter reads it.
    assert(presenter_ == nullptr);
    if (roles == ViewRoles{}) {
        subscription_ = {};
        return;
    }
    subscription_ = feed_->subscribe(roles, [this] { wakePresenter(); });
}

void Viewport::setTool(ViewportTool tool) {
    tool_ = tool;
    if (tool == ViewportTool::Pipette) {
        pipette_cursor_ = pipetteCursor(*this);
    }
    refreshCursor();
}

void Viewport::setPickHandler(dvv::MoveOnlyFunction<void(dvv::media::ImagePixel)> handler) {
    pick_ = std::move(handler);
}

void Viewport::setZoomRule(ZoomRule rule) {
    zoom_rule_ = rule;
    setView(view_);  // rounds a Zoom request under WholePixels
}

std::optional<ViewRequest> Viewport::zoomStep(const ViewRequest& from, bool in,
                                              PointD anchor) const {
    const dvv::platform::PixelSize extent = pictureSize();
    if (zoom_rule_ == ZoomRule::Free) {
        return stepZoom(from, extent, image_, in, anchor);
    }
    // The stop strictly past the zoom shown, which need not be a whole number.
    const double shown = std::clamp(resolveView(from, extent, image_).zoom(), 0.0, 1e6);
    // A zoom within 1e-6 of a whole number counts as that number.
    const double nearest = std::round(shown);
    const double snapped = std::abs(shown - nearest) <= 1e-6 * nearest ? nearest : shown;
    const auto start = static_cast<std::uint32_t>(in ? std::floor(snapped) : std::ceil(snapped));
    const std::optional<std::uint32_t> next = nextWholeZoom(start, in);
    if (!next) {
        return std::nullopt;
    }
    return zoomAround(from, extent, image_, *next, anchor);
}

void Viewport::setPanels(std::uint32_t count) {
    count = std::max<std::uint32_t>(count, 1);
    if (count == panels_) {
        return;
    }
    panels_ = count;
    panel_rects_.clear();  // the cached rects are of the old count
    updateCursor();
    if (view_changed_) {
        notifyViewChanged();  // P is the panel's
    }
}

dvv::media::PixelRect Viewport::panelRect(std::optional<PointD> at) const {
    const dvv::platform::PixelSize extent = physicalClientSize();
    const dvv::media::PixelRect whole{
        .x = 0, .y = 0, .width = extent.width, .height = extent.height};
    if (panels_ <= 1) {
        return whole;
    }
    if (panel_rects_.empty() || panel_rects_extent_.width != extent.width ||
        panel_rects_extent_.height != extent.height) {
        panel_rects_ = panelRects(extent, panelGrid(extent, panels_));
        panel_rects_extent_ = extent;
    }
    const std::vector<dvv::media::PixelRect>& rects = panel_rects_;
    if (rects.empty()) {
        return whole;
    }
    const std::optional<std::size_t> index = at ? panelAt(rects, *at) : std::nullopt;
    return rects[index.value_or(0)];
}

dvv::media::PixelRect Viewport::pointerPanel() const {
    return panelRect(position_ ? std::optional<PointD>{backingPoint(*position_)} : std::nullopt);
}

dvv::media::PixelRect Viewport::pictureRect(dvv::media::PixelRect panel) const {
    dvv::media::PixelRect picture = dvv::media::pictureRegion(panel.size(), overlay_.params.strips);
    picture.x += panel.x;
    picture.y += panel.y;
    return picture;
}

std::optional<dvv::media::ImagePixel> Viewport::pixelAt(wxPoint position) const {
    if (image_.empty()) {
        return std::nullopt;
    }
    const PointD at = backingPoint(position);
    const dvv::media::PixelRect picture = pictureRect(panelRect(at));
    return dvv::media::cursorPixel(resolveView(view_, picture.size(), image_), picture, at);
}

dvv::platform::PixelSize Viewport::pictureSize() const {
    return pictureRect(pointerPanel()).size();
}

PointD Viewport::pictureOffset() const {
    const dvv::media::PixelRect picture = pictureRect(pointerPanel());
    return {.x = static_cast<double>(picture.x), .y = static_cast<double>(picture.y)};
}

PointD Viewport::backingPoint(wxPoint position) const {
    const double scale = contentScale();  // window units to backing pixels
    return {.x = (position.x + 0.5) * scale, .y = (position.y + 0.5) * scale};
}

PointD Viewport::picturePoint(wxPoint position) const {
    const PointD point = backingPoint(position);
    const dvv::media::PixelRect picture = pictureRect(panelRect(point));
    return {.x = point.x - picture.x, .y = point.y - picture.y};
}

bool Viewport::inPicture(wxPoint position) const {
    const PointD point = backingPoint(position);
    return pictureRect(panelRect(point)).contains(point);
}

bool Viewport::onRuler(wxPoint position) const {
    const PointD point = backingPoint(position);
    const dvv::media::PixelRect panel = panelRect(point);
    const ViewTransform view = resolveView(
        view_, dvv::media::pictureRegion(panel.size(), overlay_.params.strips).size(), image_);
    const dvv::media::RulerRects rulers =
        dvv::media::rulerRects(panel.size(), overlay_.params.strips, view);
    const PointD local{.x = point.x - panel.x, .y = point.y - panel.y};
    return rulers.top.contains(local) || rulers.left.contains(local) ||
           rulers.corner.contains(local);
}

PointD Viewport::pictureAnchor(wxPoint position) const {
    const PointD point = picturePoint(position);
    const dvv::platform::PixelSize size = pictureSize();
    return {.x = std::clamp(point.x, 0.0, static_cast<double>(size.width)),
            .y = std::clamp(point.y, 0.0, static_cast<double>(size.height))};
}

void Viewport::refreshCursor() {
    if (drag_ && drag_->moved) {
        return;  // the pan cursor
    }
    const bool want =
        tool_ == ViewportTool::Pipette && position_ && inPicture(*position_) && !image_.empty();
    if (want && !pipette_cursor_set_) {
        SetCursor(pipette_cursor_);
        pipette_cursor_set_ = true;
    } else if (!want && pipette_cursor_set_) {
        SetCursor(wxNullCursor);
        pipette_cursor_set_ = false;
    }
}

}  // namespace rawreel
