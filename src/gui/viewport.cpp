// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/viewport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>

#include <wx/settings.h>

#include "core/overlay.h"
#include "core/ruler_labels.h"
#include "core/view.h"
#include "dvv/base/error.h"
#include "dvv/base/log.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "gui/native_policy.h"
#include "gui/overlay_params.h"
#include "gui/viewport_window.h"

using dvv::media::canPan;
using dvv::media::panBy;
using dvv::media::PointD;
using dvv::media::resolveView;
using dvv::media::ViewRequest;
using dvv::media::ViewTransform;
using dvv::media::zoomAround;

namespace rawreel {

namespace {

// The compositor's background greys (Task 7: dark 0.12, light 0.85, sRGB-encoded) in 8 bits.
wxColour backgroundColour(bool dark) {
    return dark ? wxColour(31, 31, 31) : wxColour(217, 217, 217);
}

}  // namespace

double rulerFontPoints(const RulerFont& font) {
    return font.size != 0 ? static_cast<double>(font.size) : wxSMALL_FONT->GetFractionalPointSize();
}

Viewport::Viewport(wxWindow* parent, std::shared_ptr<DisplayState> display)
    : display_(std::move(display)), dark_(wxSystemSettings::GetAppearance().IsDark()) {
    // The Fit view; the renderer resolves the view against the presenter's extent, so only
    // setView() sets the view again.
    display_->setView(view_);
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
    auto renderer = ViewportRenderer::create(context_, epoch_);
    if (!renderer) {
        reportFailure(renderer.error());
        return;
    }
    renderer_ = std::move(*renderer);
    const std::uint64_t serial = ++serial_;
    const bool visible = IsShownOnScreen() && !iconized_;
    auto presenter = createViewportPresenter(
        *renderer_, display_,
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
        // No stack, a lost device, a failed presenter or not ready yet: the compositor's grey.
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
    if (drag_ || onRuler(event.GetPosition()) || !canPan(view_, pictureSize(), image_)) {
        return;
    }
    CaptureMouse();
    // The standard wx cursors: the closed hand (macOS), the move cursor (Windows), fleur (wxGTK
    // maps wxCURSOR_SIZENWSE to it).
    SetCursor(wxCursor(native_policy::panCursor()));
    // Bound to left and middle presses and double-clicks only.
    const wxMouseButton button =
        event.Button(wxMOUSE_BTN_LEFT) ? wxMOUSE_BTN_LEFT : wxMOUSE_BTN_MIDDLE;
    drag_ = Drag{.last = event.GetPosition(), .button = button};
}

void Viewport::onMotion(wxMouseEvent& event) {
    event.Skip();
    const wxPoint position = event.GetPosition();
    position_ = position;
    if (!drag_) {
        updateCursor();
        return;
    }
    if (!event.ButtonIsDown(drag_->button)) {
        if (HasCapture()) {
            ReleaseMouse();
        }
        endDrag();
        updateCursor();
        return;
    }
    const wxPoint delta = position - drag_->last;
    drag_->last = position;
    const double scale = contentScale();  // window units to backing pixels
    setView(panBy(view_, pictureSize(), image_, delta.x * scale, delta.y * scale));
    updateCursor();  // setView() maps it when the view changed; a pan at its limit did not
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
    const dvv::platform::PixelSize extent = pictureSize();
    const bool vertical = event.GetWheelAxis() == wxMOUSE_WHEEL_VERTICAL;
    // macOS has no scroll zoom (design §5): there the wheel pans with Cmd too.
    if (native_policy::ctrlWheelZooms() && event.ControlDown() && vertical) {
        const int notches =
            takeWheelNotches(wheel_zoom_, event.GetWheelRotation(), event.GetWheelDelta());
        const PointD anchor = pictureAnchor(event.GetPosition());
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
        const ZoomLimits limits = zoomLimits(fitZoom());
        const double zoom = std::clamp(pinch_start_zoom_ * factor, limits.minimum, limits.maximum);
        // The zoom shown already (a start at factor 1): no setView, so Fit stays Fit.
        if (std::abs(zoom - shown) > 1e-6 * shown) {
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

void Viewport::setView(const ViewRequest& request) {
    if (request == view_) {
        return;
    }
    view_ = request;
    display_->setView(view_);
    updateCursor();
    if (view_changed_) {
        notifyViewChanged();
    }
}

void Viewport::setAlpha(bool on) {
    display_->setAlpha(on);
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
}

void Viewport::updateCursor() {
    const dvv::media::PixelRect picture =
        dvv::media::pictureRegion(physicalClientSize(), overlay_.params.strips);
    const ViewTransform view = shownView();
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
        display_->setCursor(to);
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
        overlay_.params.glyphs = glyphStrip({.family = rulers_.font.family, .sizePoints = points},
                                            native_policy::dipPerFontPoint() * per_dip);
    }
    display_->setOverlay(overlay_.params);  // notifies only when the params changed
}

std::shared_ptr<const dvv::gpu::GlyphStrip> Viewport::glyphStrip(const dvv::gpu::FontSpec& font,
                                                                 double scale) {
    if (font == glyph_font_ && scale == glyph_scale_) {
        return glyphs_;
    }
    glyph_font_ = font;
    glyph_scale_ = scale;
    glyphs_.reset();
    dvv::Result<dvv::gpu::GlyphStrip> strip = dvv::gpu::rasterGlyphStrip(font, scale, RULER_DIGITS);
    if (strip) {
        glyphs_ = std::make_shared<const dvv::gpu::GlyphStrip>(std::move(*strip));
    } else if (std::ranges::find(glyph_warned_, font) == glyph_warned_.end()) {
        glyph_warned_.push_back(font);
        DVV_LOG_WARN("ruler numbers off: the font '{}' at {} pt failed: {}",
                     font.family.empty() ? "System" : font.family, font.sizePoints,
                     strip.error().message);
    }
    return glyphs_;
}

dvv::platform::PixelSize Viewport::pictureSize() const {
    return dvv::media::pictureRegion(physicalClientSize(), overlay_.params.strips).size();
}

PointD Viewport::pictureOffset() const {
    const dvv::media::PixelRect picture =
        dvv::media::pictureRegion(physicalClientSize(), overlay_.params.strips);
    return {.x = static_cast<double>(picture.x), .y = static_cast<double>(picture.y)};
}

PointD Viewport::backingPoint(wxPoint position) const {
    const double scale = contentScale();  // window units to backing pixels
    return {.x = (position.x + 0.5) * scale, .y = (position.y + 0.5) * scale};
}

PointD Viewport::picturePoint(wxPoint position) const {
    const PointD point = backingPoint(position);
    const PointD offset = pictureOffset();
    return {.x = point.x - offset.x, .y = point.y - offset.y};
}

bool Viewport::inPicture(wxPoint position) const {
    return dvv::media::pictureRegion(physicalClientSize(), overlay_.params.strips)
        .contains(backingPoint(position));
}

bool Viewport::onRuler(wxPoint position) const {
    const dvv::media::RulerRects rulers =
        dvv::media::rulerRects(physicalClientSize(), overlay_.params.strips, shownView());
    const PointD point = backingPoint(position);
    return rulers.top.contains(point) || rulers.left.contains(point) ||
           rulers.corner.contains(point);
}

PointD Viewport::pictureAnchor(wxPoint position) const {
    const PointD point = picturePoint(position);
    const dvv::platform::PixelSize size = pictureSize();
    return {.x = std::clamp(point.x, 0.0, static_cast<double>(size.width)),
            .y = std::clamp(point.y, 0.0, static_cast<double>(size.height))};
}

}  // namespace rawreel
