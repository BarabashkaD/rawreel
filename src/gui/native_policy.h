#pragma once

// The small per-toolkit choices of the GUI, one function each (private to rawreel-gui). Each
// backend defines them: native_policy_msw.cpp, native_policy_gtk.cpp, native_policy_cocoa.mm; a
// value that two toolkits share comes from native_policy_wx.h. The shared code calls them
// unguarded. GUI thread only.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <optional>

#include "core/commands.h"
#include "dvv/base/move_only_function.h"

namespace rawreel::nativePolicy {

// Ctrl+wheel zooms the view (MSW, GTK). macOS has no scroll zoom (design §5).
[[nodiscard]] bool ctrlWheelZooms() noexcept;

// Spec §5.2: the label of the Log page's button that shows the log folder.
[[nodiscard]] wxString revealFolderLabel();

// Spec §5.2: the label of the Log page's system log check box.
[[nodiscard]] wxString systemLogLabel();

// A Settings page after its controls exist (spec §5.1). GTK: the window is only hidden between
// shows, so `bind_shown` gets the page's top-level window to bind its wxEVT_SHOW; nothing on the
// other toolkits.
void applySettingsPageStyle(wxWindow& page, dvv::MoveOnlyFunction<void(wxWindow& top)> bind_shown);

// Spec §2: View -> Log and View -> Info have a title that changes (Show/Hide) instead of a check
// mark (macOS).
[[nodiscard]] bool logItemHasChangingTitle() noexcept;

// The wx stock id that the toolkit gives `command` instead of rawreel's own menu id (macOS: About
// and Quit, which wxOSX moves to the application menu); nullopt otherwise.
[[nodiscard]] std::optional<int> stockId(Command command) noexcept;

// The command of the stock id `id` (stockId's inverse); nullopt for any other id.
[[nodiscard]] std::optional<Command> commandOfStockId(int id) noexcept;

// Spec §5.1: Preferences... at the end of Edit, after a separator, not in its command menu (GTK).
[[nodiscard]] bool settingsAtEndOfEdit() noexcept;

// Spec §5.1: ⌘W (Close stream) closes the Settings or Info window when it is in front (macOS).
[[nodiscard]] bool closeCommandClosesSettingsFirst() noexcept;

// The frame applies its minimum client size again once the window is mapped (GTK: the
// decoration sizes are known only then).
[[nodiscard]] bool reappliesMinimumAfterMap() noexcept;

// A hidden main window counts as iconified (GTK: X11 has no WM-independent iconify).
[[nodiscard]] bool hideMeansIconified() noexcept;

// The clipboard is flushed at exit, so copied text outlives rawreel (GTK).
[[nodiscard]] bool flushesClipboardOnExit() noexcept;

// The format dialog's spin buttons take their best width, not a fixed one (GTK).
[[nodiscard]] bool spinButtonsTakeBestWidth() noexcept;

// The format dialog undoes a resize it did not make (GTK).
[[nodiscard]] bool undoesForeignResize() noexcept;

// Return with no focused child goes to the default button (macOS).
[[nodiscard]] bool returnTakesDefaultButtonWithoutFocus() noexcept;

// Ctrl+C in the Info window copies its text (MSW, GTK).
[[nodiscard]] bool infoCopiesOnCtrlC() noexcept;

// The Info window takes the rect the OS suggests on a DPI change (MSW).
[[nodiscard]] bool dpiChangeTakesSuggestedRect() noexcept;

// The Info window hides again after a map notification that arrives after its Hide() (GTK).
[[nodiscard]] bool hidesStaleMapShow() noexcept;

// The viewport's cursor while it pans.
[[nodiscard]] wxStockCursor panCursor() noexcept;

}  // namespace rawreel::nativePolicy
