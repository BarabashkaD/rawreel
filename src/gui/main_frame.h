#pragma once

// rawreel-gui's main window (design M1b §5.6, M3 v2 §5.3, §5.5, D17, D18, D20, D25): a wxAUI frame
// with the fixed centre player panel (infobar + viewport), the Info and Log panes, menus and
// accelerators from commandTable(), the status bar, and the GUI-thread handlers of the Session's
// events.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <wx/aui/framemanager.h>
#include <wx/timer.h>

#include "core/commands.h"
#include "core/format_choice.h"
#include "core/gui_arguments.h"
#include "core/session_types.h"
#include "dvv/base/error.h"
#include "render/display_state.h"

namespace dvv::gpu {
class ExecutionContext;
}  // namespace dvv::gpu

namespace rawreel {

class FormatDialog;
class InfoPanel;
class LogPanel;
class PlayerPanel;
class SelfTest;
class Session;
class TransportBar;
class Viewport;

struct InfoButton {
    int id;          // wx id of the infobar button (wxEVT_BUTTON on the infobar)
    wxString label;  // e.g. "Reopen", "Format..."
};
inline constexpr int INFOBAR_REOPEN_ID = wxID_HIGHEST + 60;  // below COMMAND_ID_BASE (+100)
inline constexpr int INFOBAR_FORMAT_ID = wxID_HIGHEST + 61;  // "Format..." (§7 trailing bytes)
inline constexpr int COMMAND_ID_BASE = wxID_HIGHEST + 100;   // menu id = base + int(command)

class MainFrame final : public wxFrame {
public:
    // The frame keeps no strong reference to a context: only the viewport (while its epoch is
    // current) and the Session do.
    explicit MainFrame(std::shared_ptr<DisplayState> display);
    MainFrame(const MainFrame&) = delete;
    MainFrame& operator=(const MainFrame&) = delete;
    MainFrame(MainFrame&&) = delete;
    MainFrame& operator=(MainFrame&&) = delete;
    ~MainFrame() override;  // aui_.UnInit(); selftest_ is destroyed after it

    void execute(Command command);  // the menu/hotkey path (design §8.4)
    [[nodiscard]] bool isAvailable(Command command) const;
    // Infobar (design §7): the latest message, an icon (wxICON_*), optional buttons.
    void showMessage(const wxString& text, int icon, std::span<const InfoButton> buttons = {});
    void dismissMessage();
    [[nodiscard]] const wxString& lastMessage() const noexcept { return last_message_; }
    [[nodiscard]] std::array<wxString, 3> statusTexts() const;
    // Infobar with `text`, wxICON_ERROR and one button "Reopen" (INFOBAR_REOPEN_ID) (§5.3, D25).
    void offerReopen(const wxString& text);
    // The infobar is shown and its current message carries the Reopen button.
    [[nodiscard]] bool reopenShown() const;
    // The context of the last stackChanged (expired: no stack). GUI thread.
    [[nodiscard]] std::weak_ptr<dvv::gpu::ExecutionContext> currentContext() const {
        return context_;
    }

    // The child viewport; valid until the frame closes (onClose destroys it).
    [[nodiscard]] Viewport& viewport() const noexcept { return *viewport_; }
    [[nodiscard]] PlayerPanel& playerPanel() const noexcept { return *player_; }
    [[nodiscard]] InfoPanel& infoPanel() const noexcept { return *info_; }
    [[nodiscard]] LogPanel& logPanel() const noexcept { return *log_; }
    [[nodiscard]] wxAuiManager& aui() noexcept { return aui_; }
    [[nodiscard]] TransportBar& transportBar() noexcept;
    // The App's Session; valid until onClose (closing_ guards the rest).
    [[nodiscard]] Session& session() const noexcept;
    // A drop or File -> Open (D21): the first file replaces the stream at frame 0; the others are
    // logged as ignored (one stream in v1.0).
    void openFiles(std::vector<std::filesystem::path> files);
    // The streams of the last streamsChanged.
    [[nodiscard]] const std::vector<StreamInfo>& streams() const noexcept { return streams_; }
    // Format dialogs shown so far (formatNeeded and View -> Format...).
    [[nodiscard]] std::uint64_t formatDialogsShown() const noexcept { return format_dialogs_; }
    // Creates the --selftest runner (selftest.h) and starts `scenario`.
    void startSelfTest(Scenario scenario);

    // Session events on the GUI thread (SessionBridge); each returns at once while closing.
    void onStackChanged(const std::weak_ptr<dvv::gpu::ExecutionContext>& context,
                        std::uint64_t epoch);
    void onStreamsChanged(const std::vector<StreamInfo>& streams);
    void onFormatNeeded(std::uint32_t slot, const dvv::media::SourceRequest& request,
                        const dvv::media::Ambiguous& candidates, const std::string& reason);
    // DeviceLost, or any error while no stack is installed: infobar with Reopen; else a warning.
    void onError(const dvv::Error& error);
    // D25: the viewport's presenter failed with an error other than DeviceLost; nothing is
    // recreated: infobar with Reopen.
    void onPresenterFailed(const dvv::Error& error);

private:
    // A formatNeeded event waiting for its dialog (one dialog at a time, FIFO).
    struct PendingFormat {
        std::uint32_t slot = 0;
        dvv::media::SourceRequest request;
        dvv::media::Ambiguous candidates;
        std::string reason;
    };

    void buildMenus();      // from commandTable() (D17)
    void buildStatusBar();  // 3 fields (D20) + the 50 ms timer
    void showGpuInfo();
    void showOpenDialog();     // Command::Open
    void showOpenUriDialog();  // Command::OpenUri
    void showStreamFormat();   // Command::Format (View -> Format...)
    // Shows the queued formatNeeded dialogs one after the other; returns at once while a format
    // dialog is open (that dialog's caller goes on with the queue).
    void showFormatDialogs();
    // showModal(dialog) with the open-dialog bookkeeping; wxID_CANCEL once the frame is closing.
    int runFormatDialog(FormatDialog& dialog);
    // parseUri (an error: log + infobar), then session().open(slot, {uri}, start).
    void openUri(std::uint32_t slot, const std::string& text, std::optional<std::int64_t> start);
    // applyChoice(uri, choice), then openUri.
    void openWithChoice(std::uint32_t slot, const std::string& uri, const FormatChoice& choice,
                        std::optional<std::int64_t> start);
    void showWarning(const std::string& text);  // log + infobar (wxICON_WARNING)
    [[nodiscard]] bool hasStream() const;
    void onCharHook(wxKeyEvent& event);
    void onMenu(wxCommandEvent& event);
    void onUpdateUi(wxUpdateUIEvent& event);
    void onStatusTimer(wxTimerEvent& event);
    void onIconize(wxIconizeEvent& event);
    void onShow(wxShowEvent& event);
    // Design §5.5, binding order: status timer, selftest timer, viewport stack, display leases,
    // Session and viewport window.
    void onClose(wxCloseEvent& event);

    wxAuiManager aui_;
    // Child windows: wx owns them.
    PlayerPanel* player_;
    InfoPanel* info_;
    LogPanel* log_;
    Viewport* viewport_;  // null once the frame has closed
    wxTimer status_timer_;
    std::array<wxString, 3> status_texts_;
    wxString last_message_;
    std::vector<int> message_buttons_;  // the custom buttons of the current infobar message
    std::weak_ptr<dvv::gpu::ExecutionContext> context_;  // of the last stackChanged
    bool stack_lost_ = false;                            // the last stackChanged had no context
    bool reopen_offered_ = false;              // the current message carries the Reopen button
    bool loop_ = false;                        // mirrors the Session's loop flag (Command::Loop)
    std::vector<StreamInfo> streams_;          // of the last streamsChanged
    std::deque<PendingFormat> format_queue_;   // formatNeeded events not shown yet
    FormatDialog* format_dialog_ = nullptr;    // the open format dialog (a local of its caller)
    std::optional<FormatChoice> last_choice_;  // the last accepted formatNeeded choice
    std::uint64_t format_dialogs_ = 0;
    std::unique_ptr<SelfTest> selftest_;
    bool closing_ = false;
};

}  // namespace rawreel
