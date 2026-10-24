// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/viewport.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

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
    // D23: set once; the renderer fits from the presenter's extent, so the GUI never refits.
    display_->setParams(CompositeParams{.dark = dark_});
    // wx never erases the background, so nothing paints over the presented image.
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    // wxWANTS_CHARS: the arrows and Space reach the frame's char hook instead of moving the focus.
    Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
           wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS);
    Bind(wxEVT_PAINT, &Viewport::onPaint, this);
    Bind(wxEVT_SIZE, &Viewport::onSize, this);
    // R5: a click gives the viewport the keyboard focus (no other mouse handling in v1.0).
    Bind(wxEVT_LEFT_DOWN, &Viewport::onMouseDown, this);
    Bind(wxEVT_MIDDLE_DOWN, &Viewport::onMouseDown, this);
    Bind(wxEVT_RIGHT_DOWN, &Viewport::onMouseDown, this);
    Bind(wxEVT_AUX1_DOWN, &Viewport::onMouseDown, this);
    Bind(wxEVT_AUX2_DOWN, &Viewport::onMouseDown, this);
}

Viewport::~Viewport() {
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
    const dvv::platform::NativeWindow window = nativeWindowOf(*this);
    if (std::holds_alternative<std::monostate>(window)) {
        reportFailure(
            dvv::makeError(dvv::Errc::Internal, "the viewport has no native Win32 or X11 window")
                .error());
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
    auto presenter = context_->createPresenter(
        {.window = window,
         .extent = physicalClientSize(),
         .visible = IsShownOnScreen() && !iconized_,
         .frames =
             [display = display_, renderer = renderer_.get(),
              fail = fail_presenters_](const dvv::gpu::PresentFrameInfo& info) {
                 // Presenter thread: one consistent read of the state, then record (design §5.3).
                 const DisplaySnapshot snapshot = display->snapshot();
                 dvv::Result<dvv::gpu::PresentFrame> frame = renderer->frame(info, snapshot);
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
    if (presenter_) {
        presenter_->setVisible(!iconized);
    }
}

void Viewport::topLevelShown(bool shown) {
    if (presenter_) {
        presenter_->setVisible(shown);
    }
}

std::optional<RecordedInfo> Viewport::lastRecorded() const {
    // The renderer exists only while a stack is current; lastRecorded() is safe on this thread.
    return renderer_ ? std::optional(renderer_->lastRecorded()) : std::nullopt;
}

dvv::platform::PixelSize Viewport::physicalClientSize() const {
    const wxSize size = ToPhys(GetClientSize());
    return {.width = static_cast<std::uint32_t>(std::max(size.x, 0)),
            .height = static_cast<std::uint32_t>(std::max(size.y, 0))};
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

void Viewport::onSize(wxSizeEvent& event) {
    // Only the presenter is resized: the renderer fits from the presenter's extent (D23).
    if (presenter_) {
        presenter_->resize(physicalClientSize());
    }
    event.Skip();
}

}  // namespace rawreel
