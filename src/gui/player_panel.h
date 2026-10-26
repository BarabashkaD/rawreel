#pragma once

// The fixed centre pane of the main frame (design M3 D18): the infobar above the viewport and the
// transport bar below it.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <cstdint>
#include <functional>
#include <memory>

// clang-format off
#include <wx/infobar.h>  // wxInfoBarBase, before the generic class
#include <wx/generic/infobar.h>
// clang-format on

#include "core/commands.h"
#include "render/display_state.h"

namespace rawreel {

class TransportBar;
class Viewport;

class PlayerPanel final : public wxPanel {
public:
    // `execute` and `seek` are the transport bar's (MainFrame::execute, Session::seek).
    PlayerPanel(wxWindow* parent, std::shared_ptr<DisplayState> display,
                std::move_only_function<void(Command)> execute,
                std::move_only_function<void(std::int64_t)> seek);

    [[nodiscard]] wxInfoBarGeneric& infoBar() noexcept { return *info_bar_; }
    [[nodiscard]] const wxInfoBarGeneric& infoBar() const noexcept { return *info_bar_; }
    // Valid until destroyViewport().
    [[nodiscard]] Viewport& viewport() noexcept { return *viewport_; }
    [[nodiscard]] TransportBar& transportBar() noexcept { return *transport_; }
    // The smallest size that keeps the viewport at MIN_VIEWPORT_DIP (M4 GUI D8), physical pixels:
    // the transport bar's best size and the infobar's height (reserved while it is hidden).
    [[nodiscard]] wxSize minimumSize();
    // MainFrame::onClose (design §5.5): destroys the viewport window now.
    void destroyViewport();

private:
    wxInfoBarGeneric* info_bar_;  // child windows: wx owns them
    Viewport* viewport_;
    TransportBar* transport_;
};

}  // namespace rawreel
