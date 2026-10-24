// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/player_panel.h"

#include <utility>

// clang-format off
#include <wx/infobar.h>  // wxInfoBarBase, before the generic class
#include <wx/generic/infobar.h>
// clang-format on

#include "gui/transport_bar.h"
#include "gui/viewport.h"

namespace rawreel {

PlayerPanel::PlayerPanel(wxWindow* parent, std::shared_ptr<DisplayState> display,
                         std::move_only_function<void(Command)> execute,
                         std::move_only_function<void(std::int64_t)> seek)
    : wxPanel(parent), info_bar_(new wxInfoBarGeneric(this)) {
    // No slide animation: each animation step would resize the viewport's swapchain (LOG 27).
    info_bar_->SetShowHideEffects(wxSHOW_EFFECT_NONE, wxSHOW_EFFECT_NONE);
    viewport_ = new Viewport(this, std::move(display));
    transport_ = new TransportBar(this, std::move(execute), std::move(seek));
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(info_bar_, wxSizerFlags().Expand());
    sizer->Add(viewport_, wxSizerFlags(1).Expand());
    sizer->Add(transport_, wxSizerFlags().Expand());
    SetSizer(sizer);
}

void PlayerPanel::destroyViewport() {
    if (viewport_ != nullptr) {
        viewport_
            ->Destroy();  // a child's Destroy() deletes it now (and detaches it from the sizer)
        viewport_ = nullptr;
    }
}

}  // namespace rawreel
