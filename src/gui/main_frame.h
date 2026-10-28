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
#include "core/version_text.h"
#include "dvv/base/error.h"
#include "gui/dock_manager.h"
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
    wxString label;  // e.g. "Restart GPU", "Format..."
};
inline constexpr int INFOBAR_REOPEN_ID = wxID_HIGHEST + 60;  // below COMMAND_ID_BASE (+100)
inline constexpr int INFOBAR_FORMAT_ID = wxID_HIGHEST + 61;  // "Format..." (§7 trailing bytes)
inline constexpr int COMMAND_ID_BASE = wxID_HIGHEST + 100;   // menu id = base + int(command)

// Where an execute() call came from (the Loop log line names it).
enum class CommandSource : std::uint8_t {
    Script,  // the selftest script or other program code
    Key,     // onCharHook
    Menu,    // a menu item (wxEVT_MENU)
    Button,  // a transport or infobar button
};

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

    // The menu/hotkey path (design §8.4).
    void execute(Command command, CommandSource source = CommandSource::Script);
    [[nodiscard]] bool isAvailable(Command command) const;
    // The GUI's loop flag (the mirror of the Session's, set at once by Command::Loop).
    [[nodiscard]] bool loop() const noexcept { return loop_; }
    // Infobar (design §7): the latest message, an icon (wxICON_*), optional buttons.
    void showMessage(const wxString& text, int icon, std::span<const InfoButton> buttons = {});
    void dismissMessage();
    [[nodiscard]] const wxString& lastMessage() const noexcept { return last_message_; }
    [[nodiscard]] std::array<wxString, 4> statusTexts() const;
    // Infobar with `text`, wxICON_ERROR and one button "Restart GPU" (INFOBAR_REOPEN_ID) (§5.3,
    // D25).
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
    // logged as ignored (one stream at a time).
    void openFiles(std::vector<std::filesystem::path> files);
    // File -> Open URI..., a format choice and the command line's URI: session().closeStreams()
    // first (M4 GUI D3), then parseUri (an error: log + infobar), then session().open(slot, {uri},
    // start); the URI is kept for onError's hint.
    void openUri(std::uint32_t slot, const std::string& text, std::optional<std::int64_t> start);
    // The streams of the last streamsChanged.
    [[nodiscard]] const std::vector<StreamInfo>& streams() const noexcept { return streams_; }
    // Format dialogs shown so far (formatNeeded and View -> Format...).
    [[nodiscard]] std::uint64_t formatDialogsShown() const noexcept { return format_dialogs_; }
    // Help -> About (selftests): dialogs shown so far and the text of the last one.
    [[nodiscard]] std::uint64_t aboutShown() const noexcept { return about_shown_; }
    [[nodiscard]] const std::string& lastAboutText() const noexcept { return last_about_text_; }
    // The Vulkan status of the current stack (context_): its device and API version, or "no GPU
    // device is in use".
    [[nodiscard]] VulkanStatus vulkanStatus() const;
    // Stores the About text and counts the dialog (showAbout).
    void noteAbout(std::string text);
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
    void showOpenDialog();          // Command::Open
    void showOpenUriDialog();       // Command::OpenUri
    void logOpenCancelled() const;  // INFO "open cancelled", unless closing
    void showStreamFormat();        // Command::Format (View -> Format...)
    // The infobar's Format... after an open error of a raw file (D4b): the dialog for that URI.
    void showErrorFormat();
    // Shows the queued formatNeeded dialogs one after the other; returns at once while a format
    // dialog is open (that dialog's caller goes on with the queue).
    void showFormatDialogs();
    // showModal(dialog) with the open-dialog bookkeeping; wxID_CANCEL once the frame is closing.
    int runFormatDialog(FormatDialog& dialog);
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
    // The minimum client size from the real controls (D8), the client grown to it and the
    // docks fitted to it; again after a DPI change and after a dock change.
    void applyLayoutLimits();
    // After a layout of the player panel: applyLayoutLimits() once the minimum differs from the
    // applied one (a pane moved, docked, floated or shown).
    void checkLayoutLimits();
    // Design §5.5, binding order: status timer, selftest timer, viewport stack, display leases,
    // Session and viewport window.
    void onClose(wxCloseEvent& event);

    DockManager aui_;  // the docks give way so the viewport keeps its minimum (D8)
    // Child windows: wx owns them.
    PlayerPanel* player_ = nullptr;
    InfoPanel* info_ = nullptr;
    LogPanel* log_ = nullptr;
    Viewport* viewport_ = nullptr;  // null once the frame has closed
    wxSize applied_minimum_;        // the last minimum client size applyLayoutLimits() set
    bool limits_check_pending_ = false;
    wxTimer status_timer_;
    std::array<wxString, 4> status_texts_;
    wxString last_message_;
    std::vector<int> message_buttons_;  // the custom buttons of the current infobar message
    std::weak_ptr<dvv::gpu::ExecutionContext> context_;  // of the last stackChanged
    bool stack_lost_ = false;                            // the last stackChanged had no context
    bool reopen_offered_ = false;  // the current message carries the Reopen button
    bool loop_ = false;            // mirrors the Session's loop flag (Command::Loop)
    std::string last_open_uri_;    // of the last openUri (onError's FFmpeg status, design v2 §4.6)
    // The folder of the last opened file (drop, File -> Open, a file: URI): the file dialogs'
    // start folder (M4 GUI D6). Empty until a file is opened.
    std::filesystem::path last_dir_;
    std::string last_typed_uri_;  // the last OK value of Open URI... (D7); empty: the example
    // The URI whose open error shows the Format... button (D4b); empty otherwise.
    std::string format_error_uri_;
    std::vector<StreamInfo> streams_;          // of the last streamsChanged
    std::deque<PendingFormat> format_queue_;   // formatNeeded events not shown yet
    FormatDialog* format_dialog_ = nullptr;    // the open format dialog (a local of its caller)
    std::optional<FormatChoice> last_choice_;  // the last accepted formatNeeded choice
    FileColourMemory file_colours_;            // the files' own colour
    bool auto_detect_ = false;  // the format dialog's Auto detect box, for the session
    std::uint64_t format_dialogs_ = 0;
    std::uint64_t about_shown_ = 0;  // About dialogs shown (noteAbout)
    std::string last_about_text_;    // the text of the last About dialog
    std::unique_ptr<SelfTest> selftest_;
    bool closing_ = false;
};

}  // namespace rawreel
