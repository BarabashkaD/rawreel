// The selftest hooks of wxMSW: the Info window copies through Ctrl+C (selftest_wx.h); the
// macOS-only steps add none.
// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on
#include <memory>
#include <vector>

#include "gui/selftest.h"
#include "gui/selftest_detail.h"
#include "gui/selftest_wx.h"

namespace rawreel {

void copyInInfoWindow(MainFrame& frame, const std::shared_ptr<InfoKeysState>& state) {
    wx_generic::copyInInfoWindow(frame, state);
}

void addInfoWindowCloseKeySteps(std::vector<SelfTestStep>& /*steps*/, SelfTest* /*test*/,
                                const std::shared_ptr<InfoKeysState>& /*state*/) {}

void addBackingScaleSteps(std::vector<SelfTestStep>& /*steps*/, SelfTest* /*test*/,
                          const std::shared_ptr<SmokeState>& /*state*/) {}

void addApplicationMenuSteps(std::vector<SelfTestStep>& /*steps*/, SelfTest* /*test*/,
                             bool /*opens_about*/) {}

void addSettingsCloseKeySteps(std::vector<SelfTestStep>& /*steps*/, SelfTest* /*test*/,
                              const std::shared_ptr<SettingsState>& /*state*/) {}

void openFilesFromFinder(const wxArrayString& /*names*/) {}

}  // namespace rawreel
