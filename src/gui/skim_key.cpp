// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/skim_key.h"

#include <utility>

namespace rawreel {

bool skimKeyDown(const wxKeyboardState& state) noexcept {
#if defined(__WXOSX__)
    return state.AltDown();
#else
    return state.ControlDown();
#endif
}

bool isSkimKey(int key_code) noexcept {
#if defined(__WXOSX__)
    return key_code == WXK_ALT;
#else
    return key_code == WXK_CONTROL;
#endif
}

SkimKeyRelease::SkimKeyRelease(dvv::MoveOnlyFunction<void()> released)
    : released_(std::move(released)) {
    wxEvtHandler::AddFilter(this);
}

SkimKeyRelease::~SkimKeyRelease() {
    wxEvtHandler::RemoveFilter(this);
}

int SkimKeyRelease::FilterEvent(wxEvent& event) {
    if (event.GetEventType() == wxEVT_KEY_UP && released_ &&
        isSkimKey(static_cast<wxKeyEvent&>(event).GetKeyCode())) {
        released_();
    }
    return Event_Skip;
}

}  // namespace rawreel
