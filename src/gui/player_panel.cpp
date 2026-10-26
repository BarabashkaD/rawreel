// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/player_panel.h"

#include <algorithm>
#include <utility>

// clang-format off
#include <wx/infobar.h>  // wxInfoBarBase, before the generic class
#include <wx/generic/infobar.h>
// clang-format on

#include "core/layout.h"
#include "gui/transport_bar.h"
#include "gui/viewport.h"

namespace rawreel {

namespace {

constexpr int INFO_BAR_BORDERS_DIP = 12;  // around the infobar's buttons (wxInfoBarGeneric)

}  // namespace

PlayerPanel::PlayerPanel(wxWindow* parent, std::shared_ptr<DisplayState> display,
                         std::move_only_function<void(Command)> execute,
                         std::move_only_function<void(std::int64_t)> seek)
    : wxPanel(parent), info_bar_(new wxInfoBarGeneric(this)) {
    // No slide animation: each animation step would resize the viewport's swapchain.
    info_bar_->SetShowHideEffects(wxSHOW_EFFECT_NONE, wxSHOW_EFFECT_NONE);
    viewport_ = new Viewport(this, std::move(display));
    transport_ = new TransportBar(this, std::move(execute), std::move(seek));
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(info_bar_, wxSizerFlags().Expand());
    sizer->Add(viewport_, wxSizerFlags(1).Expand());
    sizer->Add(transport_, wxSizerFlags().Expand());
    SetSizer(sizer);
}

wxSize PlayerPanel::minimumSize() {
    const wxSize viewport = FromDIP(wxSize(MIN_VIEWPORT_DIP.width, MIN_VIEWPORT_DIP.height));
    const wxSize transport = transport_->GetBestSize();
    // The infobar's height is reserved while it is hidden too, so a message never takes the
    // viewport below its minimum: at least a button's height and the bar's borders (an estimate),
    // or the bar's real height while it is shown.
    int info_bar = std::max(info_bar_->GetBestSize().y,
                            wxButton::GetDefaultSize(this).y + FromDIP(INFO_BAR_BORDERS_DIP));
    if (info_bar_->IsShown()) {
        info_bar = std::max(info_bar, info_bar_->GetSize().y);
    }
    const LayoutSize size = centreMinimum({.width = viewport.x, .height = viewport.y},
                                          {.width = transport.x, .height = transport.y}, info_bar);
    return {size.width, size.height};
}

void PlayerPanel::destroyViewport() {
    if (viewport_ != nullptr) {
        viewport_
            ->Destroy();  // a child's Destroy() deletes it now (and detaches it from the sizer)
        viewport_ = nullptr;
    }
}

}  // namespace rawreel
