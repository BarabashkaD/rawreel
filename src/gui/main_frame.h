#pragma once

// rawreel-gui's main window (design M1b §5.6, M3 v2 §5.3, §5.5, D17, D18, D20, D25): a wxAUI frame
// with the fixed centre player panel (infobar + viewport), the Log pane, the Info window (window
// title and Info design §10), menus and accelerators from commandTable() plus the Edit menu
// (edit_dispatch.h), the status bar, and the GUI-thread handlers of the Session's events.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <wx/aui/framemanager.h>
#include <wx/weakref.h>

#include "core/commands.h"
#include "core/format_choice.h"
#include "core/gui_arguments.h"
#include "core/log_store.h"
#include "core/recent.h"
#include "core/session.h"
#include "core/session_types.h"
#include "core/version_text.h"
#include "dvv/base/error.h"
#include "gui/dock_manager.h"
#include "gui/info_window.h"
#include "gui/skim_key.h"
#include "render/display_state.h"

namespace dvv::gpu {
class ExecutionContext;
}  // namespace dvv::gpu

namespace rawreel {

class FormatDialog;
class LogPanel;
class PlayerPanel;
class SelfTest;
class Session;
class SettingsWindow;
class TransportBar;
class Viewport;

struct InfoButton {
    int id;          // wx id of the infobar button (wxEVT_BUTTON on the infobar)
    wxString label;  // e.g. "Restart GPU", "Format..."
};
inline constexpr int INFOBAR_REOPEN_ID = wxID_HIGHEST + 60;  // below COMMAND_ID_BASE (+100)
inline constexpr int INFOBAR_FORMAT_ID = wxID_HIGHEST + 61;  // "Format..." (§7 trailing bytes)
// File > Open Recent's items (Open Recent spec §3): entry i has RECENT_ID_BASE + i, up to +89.
inline constexpr int RECENT_ID_BASE = wxID_HIGHEST + 80;
inline constexpr int COMMAND_ID_BASE = wxID_HIGHEST + 100;  // menu id = base + int(command)

// The Open Recent entry of a typed text (the command line, Open URI...; spec §4):
// recentEntryOfText(text, the working folder); an empty folder when it cannot be found.
[[nodiscard]] std::string typedRecentEntry(std::string_view text);

// The menu item id of `command`: COMMAND_ID_BASE + int(command), except Settings, which is
// wxID_PREFERENCES everywhere (wxOSX shows it in the application menu, Log pane and Settings design
// §5.1), and on macOS About (wxID_ABOUT) and Quit (wxID_EXIT), which wxOSX shows there too (design
// C5).
[[nodiscard]] int menuId(Command command) noexcept;
// The command of a menu item id; nullopt for an id of no command.
[[nodiscard]] std::optional<Command> commandOfMenuId(int id) noexcept;

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
    // The status bar's fields (spec §7): the stream's name, the status tick's three (position,
    // rate, bottleneck), the display mapping and the Log notice.
    static constexpr std::size_t FILE_FIELD = 0, FIRST_TICK_FIELD = 1, MAPPING_FIELD = 4,
                                 LOG_NOTICE_FIELD = 5, STATUS_FIELD_COUNT = 6;
    // The fields' texts; the file field's is the shown one (cut in the middle to its width).
    [[nodiscard]] std::array<wxString, STATUS_FIELD_COUNT> statusTexts() const;
    // The field whose rectangle contains `point` (status bar client coordinates); none outside.
    [[nodiscard]] std::optional<int> statusFieldAt(const wxPoint& point) const;
    // The tooltip of a field (spec §7): the file field gives the stream's full path, another
    // field its full text when wx cut it; "" for none.
    [[nodiscard]] wxString statusTooltipFor(std::optional<int> field) const;
    // The stream's full path (spec §4: a file's absolute path, another URI as it is); "" with no
    // stream.
    [[nodiscard]] const wxString& streamPath() const noexcept { return stream_path_; }
    // View -> Log (Log pane and Settings design §2): shows or hides the Log pane. Showing it takes
    // the store's new records, rebuilds the list from the mirror and clears the notice. Nothing
    // when the pane is already so, or while closing.
    void setLogShown(bool shown);
    [[nodiscard]] bool logShown() const;
    // View -> Info (window title and Info design §10): every show and hide of the Info window goes
    // through setInfoShown (the command, the window's close box). The window is created by the
    // first show. Nothing while closing or when it is already so. infoShown() stays true while
    // the minimised (or, on X11, hidden) main window hides it.
    void setInfoShown(bool shown);
    [[nodiscard]] bool infoShown() const;
    // Null before the first show and once the frame has closed.
    [[nodiscard]] InfoWindow* infoWindow() const noexcept { return info_window_.get(); }
    // §10: the Info window when the focus (focusedWindow()) is inside it, or when it is shown and
    // the active window; null otherwise. Its Cmd+W (macOS) and Edit -> Copy act on it.
    [[nodiscard]] wxWindow* infoInFront() const;
    // The status bar's last field: the warnings and errors logged since the pane was last shown
    // ("2 errors, 3 warnings in the log"); empty while it is shown.
    [[nodiscard]] wxString logNotice() const { return status_texts_[LOG_NOTICE_FIELD]; }
    // Follows the Session by notification (no timer): subscribes, and shows the snapshot already
    // published. stopStatusUpdates() drops the subscription; it runs before the Session goes.
    void startStatusUpdates(Session& session);
    void stopStatusUpdates();
    // --debug-transport: the notification trace here and the scrub trace in the transport bar.
    void setDebugTransport(bool on) noexcept;
    // Handled, non-duplicate status notifications; the revision handled last.
    [[nodiscard]] std::uint64_t statusNotificationsForTest() const noexcept {
        return status_notifications_;
    }
    [[nodiscard]] std::uint64_t lastRevisionForTest() const noexcept { return last_revision_; }
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
    [[nodiscard]] LogPanel& logPanel() const noexcept { return *log_; }
    [[nodiscard]] wxAuiManager& aui() noexcept { return aui_; }
    [[nodiscard]] TransportBar& transportBar() noexcept;
    // The window that has the keyboard focus (wxWindow::FindFocus()), or the one a selftest set
    // with setFocusForTest(). Every focus decision of the frame (the char hook, the Edit menu) uses
    // it.
    [[nodiscard]] wxWindow* focusedWindow() const;
    // Selftests: focusedWindow() returns `window` (null: the real focus again).
    void setFocusForTest(wxWindow* window);
    // The App's Session; valid until onClose (closing_ guards the rest).
    [[nodiscard]] Session& session() const noexcept;
    // A drop or File -> Open (D21): the first file replaces the stream at frame 0; the others are
    // logged as ignored (one stream at a time).
    void openFiles(std::vector<std::filesystem::path> files);
    // File -> Open URI..., a format choice and the command line's URI: session().closeStreams()
    // first (M4 GUI D3), then parseUri (an error: log + infobar), then session().open(slot, {uri},
    // start); the URI is kept for onError's hint. `entry` is the open's Open Recent entry (spec
    // §4, §5), pending until its stream shows; none records nothing. A text that does not parse
    // leaves the pending entry as it is.
    void openUri(std::uint32_t slot, const std::string& text, std::optional<std::int64_t> start,
                 std::optional<std::string> entry = std::nullopt);
    // Rebuilds File > Open Recent's items from wxGetApp().recent() and enables its item exactly
    // when the list is not empty (spec §3); after every change of the list.
    void recentChanged();
    // The file check of `entry` after its failed open (spec §6), back on the GUI thread: Missing
    // or NotAccessible removes a still listed entry, saves the list (App::saveHistory), rebuilds
    // the submenu and logs "\"<entry>\" removed from Open Recent: missing" (or "not accessible");
    // Readable and NotChecked keep it. Nothing while the window closes.
    void recentChecked(const std::string& entry, RecentFileState state);
    // The File menu's Open Recent item (its submenu holds the entries); null before buildMenus().
    [[nodiscard]] wxMenuItem* recentMenuItem() const noexcept { return recent_item_; }
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
    // The window has started closing (onClose); the Settings pages do nothing from then on.
    [[nodiscard]] bool closing() const noexcept { return closing_; }
    // File -> Settings... (Log pane and Settings design §5): created by the first Settings command;
    // null before.
    [[nodiscard]] SettingsWindow* settingsWindow() const noexcept { return settings_.get(); }

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
        std::optional<std::string> entry;  // the Open Recent entry of the open that asked
    };

    void buildMenus();      // from commandTable() (D17)
    void buildStatusBar();  // 6 fields (D20, D5R §2.2, Log pane and Settings design §2, spec §7)
    // The width of the two fixed fields (display mapping and Log notice) at the current font.
    [[nodiscard]] int statusFixedWidth() const;
    // Spec §7: the tooltip of the field under the pointer; unset when that field changes, so
    // each field gets the OS's own hover delay.
    void onStatusBarMotion(wxMouseEvent& event);
    // Spec §5, §7: the title, the represented filename (wxOSX) and the file field from streams_.
    void showStreamName();
    // The file field: the name cut in the middle to the field's width (the extension stays).
    void cutFileField();
    // The notice field from log_counts_ and seen_ (no logging: the refresh path calls it).
    void updateLogNotice();
    // A left click inside the notice field shows the pane (wx has no click event per field).
    void onStatusBarLeftUp(wxMouseEvent& event);
    void showGpuInfo();
    void showOpenDialog();          // Command::Open
    void showOpenUriDialog();       // Command::OpenUri
    void logOpenCancelled() const;  // INFO "open cancelled", unless closing
    void showStreamFormat();        // Command::Format (View -> Format...)
    void showSettings();            // Command::Settings: creates the window once, then shows it
#if defined(__WXOSX__)
    // Spec §5.1: the Settings window when it holds the focus (focusedWindow()) or is the active
    // window, whose ⌘W closes it instead of the stream; null otherwise. Only that window (and the
    // Info window, infoInFront()): a floating AUI pane (Log) is a top-level window too, and closing
    // it would hide the pane for the session.
    [[nodiscard]] wxWindow* settingsInFront() const;
#endif
    // The infobar's Format... after an open error of a raw file (D4b): the dialog for that URI.
    void showErrorFormat();
    // Shows the queued formatNeeded dialogs one after the other; returns at once while a format
    // dialog is open (that dialog's caller goes on with the queue).
    void showFormatDialogs();
    // showModal(dialog) with the open-dialog bookkeeping; wxID_CANCEL once the frame is closing.
    int runFormatDialog(FormatDialog& dialog);
    // applyChoice(uri, choice), then openUri with `entry`.
    void openWithChoice(std::uint32_t slot, const std::string& uri, const FormatChoice& choice,
                        std::optional<std::int64_t> start,
                        std::optional<std::string> entry = std::nullopt);
    // Spec §5: a shown open's entry goes on top of the list; a changed list is saved
    // (App::saveHistory) and the submenu rebuilt. An invalid entry only logs
    // "not added to Open Recent: <reason>".
    void recordRecent(const std::string& entry);
    // The Open Recent item `index`: its entry is opened, and is its own entry.
    void openRecent(std::size_t index);
    void showWarning(const std::string& text);  // log + infobar (wxICON_WARNING)
    [[nodiscard]] bool hasStream() const;
    void onCharHook(wxKeyEvent& event);
    void onActivate(wxActivateEvent& event);
    void onSkimKeyUp();  // the skim key's release anywhere: the bar returns from a skim
    void onMenu(wxCommandEvent& event);
    void onUpdateUi(wxUpdateUIEvent& event);
    // The Edit items (spec §4.2): runEdit and canEdit on focusedWindow().
    void onEditMenu(wxCommandEvent& event);
    void onEditUpdateUi(wxUpdateUIEvent& event);
    // File -> Save Log... (every record of the mirror) and Save Selected Lines... (spec §4.3,
    // §4.4): the records are taken first, then the save dialog; the log files rawreel writes are
    // refused and a failed write is reported, each with a message.
    void saveLog(bool selected);
    void onStatusChanged();
    // Posted by the LogStore's notify (spec §3.2): the store's new records into the Log pane,
    // whose list follows only while the pane is shown; also the log size check after a split
    // (App::checkLogSplits). Never logs.
    void onLogChanged();
    void onIconize(wxIconizeEvent& event);
    void onShow(wxShowEvent& event);
    // §10 Life: the Info window hides while the main window is minimised (X11: hidden) and shows
    // again, without activating, when it is restored.
    void infoFollowsFrame(bool frame_hidden);
    // Spec §4.1: the native focus inside the Log pane (any of its windows) goes to the player
    // panel, so a hidden pane, or a wrapper panel with nothing visibly focused, does not hold it.
    // When the pane hides (View -> Log, the caption's close button) and once after the first show.
    void moveFocusOutOfLog();
#if defined(__WXOSX__)
    // wxGTK and wxMSW keep a top-level window at its minimum size on every resize, wxOSX only on
    // the user's (windowWillResize:toSize:): a SetSize() or SetClientSize() below the minimum
    // shrinks the window, and the viewport below 320x240 DIP (D8). The frame applies its minimum
    // to those as well.
    void DoSetSize(int x, int y, int width, int height, int flags) override;
#endif
    // The minimum client size from the real controls (D8), the client grown to it and the
    // docks fitted to it; again after a DPI change and after a dock change.
    void applyLayoutLimits();
    // After a layout of the player panel: applyLayoutLimits() once the minimum differs from the
    // applied one (a pane moved, docked, floated or shown).
    void checkLayoutLimits();
    // At close (spec §2): gui.showLogPane through App::saveSettings when the pane's state differs
    // from its state at start (log_shown_at_start_).
    void saveLogPaneState();
    // Design §5.5, binding order: status timer, selftest timer, viewport stack, display leases,
    // Session and viewport window.
    void onClose(wxCloseEvent& event);

    DockManager aui_;  // the docks give way so the viewport keeps its minimum (D8)
    // Child windows: wx owns them.
    PlayerPanel* player_ = nullptr;
    LogPanel* log_ = nullptr;
    Viewport* viewport_ = nullptr;  // null once the frame has closed
    wxSize applied_minimum_;        // the last minimum client size applyLayoutLimits() set
    bool limits_check_pending_ = false;
    std::array<wxString, STATUS_FIELD_COUNT> status_texts_;
    wxString stream_name_;                    // spec §4: the shown name; "" with no stream
    wxString stream_path_;                    // spec §4: the full path; "" with no stream
    std::optional<int> tip_field_;            // the field whose tooltip the status bar shows
    wxString tip_text_;                       // that tooltip; "" for none
    LogCounts log_counts_;                    // the counts of the last take (onLogChanged)
    LogCounts seen_;                          // the counts when the pane was last shown (spec §2)
    bool log_shown_at_start_ = false;         // the pane's state when the frame was built (spec §2)
    StatusSubscription status_subscription_;  // empty before startStatusUpdates / after stop
    std::uint64_t last_revision_ = 0;
    std::uint64_t status_notifications_ = 0;
    bool debug_transport_ = false;
    // Rate trace (--debug-transport): entries of onStatusChanged() per steady-clock second.
    std::chrono::steady_clock::time_point rate_second_{};
    std::uint64_t rate_count_ = 0;
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
    // The folder of the last log save in this run: the save dialog's start folder (spec §4.4).
    std::filesystem::path last_log_save_dir_;
    // The URI whose open error shows the Format... button (D4b); empty otherwise.
    std::string format_error_uri_;
    // That open's Open Recent entry (spec §5), cleared with format_error_uri_.
    std::optional<std::string> format_error_entry_;
    RecentPending recent_pending_;             // the entry of the open in flight (spec §5)
    wxMenuItem* recent_item_ = nullptr;        // File > Open Recent (wx owns it)
    std::vector<StreamInfo> streams_;          // of the last streamsChanged
    std::deque<PendingFormat> format_queue_;   // formatNeeded events not shown yet
    FormatDialog* format_dialog_ = nullptr;    // the open format dialog (a local of its caller)
    std::optional<FormatChoice> last_choice_;  // the last accepted formatNeeded choice
    FileColourMemory file_colours_;            // the files' own colour
    bool auto_detect_ = false;  // the format dialog's Auto detect box, for the session
    std::uint64_t format_dialogs_ = 0;
    std::uint64_t about_shown_ = 0;             // About dialogs shown (noteAbout)
    std::string last_about_text_;               // the text of the last About dialog
    std::unique_ptr<SettingsWindow> settings_;  // Settings... (dismissed in onClose)
    wxWeakRef<InfoWindow> info_window_;    // View -> Info; a child top-level window (wx owns it)
    bool info_hidden_by_iconize_ = false;  // the minimised main window hid the shown Info window
    std::unique_ptr<SelfTest> selftest_;
    bool closing_ = false;
    wxWeakRef<wxWindow> focus_for_test_;  // setFocusForTest(); cleared when that window goes
    bool first_shown_ = false;            // the frame has been shown (onShow)
    // Removed before wx destroys the children (members go first).
    SkimKeyRelease skim_key_release_{[this] { onSkimKeyUp(); }};
};

}  // namespace rawreel
