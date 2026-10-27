#pragma once

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "dvv/base/error.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/presenter.h"
#include "dvv/platform/native_window.h"
#include "render/display_state.h"
#include "render/viewport_renderer.h"

namespace rawreel {

// The viewport (M1b §5.6 plus design M3 v2 §5.3 GUI side): a native child window that shows the
// DisplayState through one ViewportRenderer and one Presenter of the current stack. Every member
// runs on the GUI thread; the GUI thread only calls the presenter's non-blocking members.
class Viewport final : public wxWindow {
public:
    // Sets display->setParams(CompositeParams{.dark = wxSystemSettings::GetAppearance().IsDark()})
    // once (D23: the renderer fits; the GUI never refits).
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
    // Test only (--selftest with RAWREEL_GUI_FAULT=presenter): the record function of every
    // presenter frame throws, so each presenter fails with Errc::Internal (the D25 path).
    void failPresentersForTest() noexcept { fail_presenters_ = true; }
    // Test only (the lost selftest): the next startPresenting() reports an Errc::Internal
    // creation error at once, on the GUI thread (the synchronous D25 path of setStack).
    void failNextStartForTest() noexcept { fail_next_start_ = true; }
    // Forwarded by the frame (wxEVT_ICONIZE and wxEVT_SHOW of the top-level window).
    void topLevelIconized(bool iconized);
    void topLevelShown(bool shown);

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
    // The client area in physical pixels (wx 3.3 ToPhys).
    [[nodiscard]] dvv::platform::PixelSize physicalClientSize() const;
    [[nodiscard]] const std::shared_ptr<DisplayState>& display() const noexcept { return display_; }
    // The renderer's last recorded composite; nullopt without a renderer (no stack). GUI thread.
    [[nodiscard]] std::optional<RecordedInfo> lastRecorded() const;

private:
    void startPresenting();
    // Creation errors: DeviceLost goes to the device-lost handler, anything else to the failure
    // handler.
    void reportFailure(const dvv::Error& error);
    // A presenter's onError, forwarded to the GUI thread; `serial` names the presenter.
    void onPresenterError(std::uint64_t serial, const dvv::Error& error);
    void onPaint(wxPaintEvent& event);
    void onSize(wxSizeEvent& event);
    void onMouseDown(wxMouseEvent& event);  // SetFocus() on every button press (R5)

    std::shared_ptr<DisplayState> display_;
    bool dark_ = false;  // the background of the paint handler without a presenter
    bool ready_ = false;
    bool iconized_ = false;  // the top-level frame is minimised: a new presenter starts hidden
    bool fail_presenters_ = false;  // failPresentersForTest()
    bool fail_next_start_ = false;  // failNextStartForTest()
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
    std::optional<dvv::Error> last_failure_;
};

}  // namespace rawreel
