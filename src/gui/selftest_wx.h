#pragma once
// The wx-only selftest bodies that the MSW and GTK backends share (selftest_msw.cpp,
// selftest_gtk.cpp). Include it only from those files.
// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on
#include <memory>

#include "gui/selftest_detail.h"

namespace rawreel::wx_generic {

// The Info window's Copy through Ctrl+C in the window (MSW, GTK).
inline void copyInInfoWindow(MainFrame& frame, const std::shared_ptr<InfoKeysState>& state) {
    state->copyEnabled = true;  // no Edit menu path here: the main window's menu activates it
    state->consumed = sendInfoKey(frame, 'C', true);
}

}  // namespace rawreel::wx_generic
