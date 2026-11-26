// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/main_frame.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// clang-format off
#include <wx/infobar.h>  // wxInfoBarBase, before the generic class
#include <wx/generic/infobar.h>
// clang-format on

#include <wx/filedlg.h>
#include <wx/textdlg.h>
#include <wx/tooltip.h>

#include "core/config.h"
#include "core/config_writer.h"
#include "core/drop.h"
#include "core/info_text.h"
#include "core/layout.h"
#include "core/log_file_switch.h"
#include "core/log_store.h"
#include "core/log_text.h"
#include "core/open_text.h"
#include "core/overlay.h"
#include "core/recent.h"
#include "core/session.h"
#include "core/startup.h"
#include "core/status_text.h"
#include "core/stream_name.h"
#include "core/version_text.h"
#include "core/view.h"
#include "dvv/base/json.h"
#include "dvv/base/log.h"
#include "dvv/base/uri.h"
#include "dvv/base/utf8.h"
#include "dvv/components/ffmpeg.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/gpu_info_json.h"
#include "dvv/media/display.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/types.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "dvv/platform/file_writer.h"
#include "dvv/registry/module.h"
#include "gui/about.h"
#include "gui/app.h"
#include "gui/app_icon.h"
#include "gui/drop_target.h"
#include "gui/edit_dispatch.h"
#include "gui/floating_window.h"
#include "gui/format_dialog.h"
#include "gui/grid_window.h"
#include "gui/info_window.h"
#include "gui/log_panel.h"
#include "gui/native_policy.h"
#include "gui/player_panel.h"
#include "gui/selftest.h"
#include "gui/settings.h"
#include "gui/transport_bar.h"
#include "gui/viewport.h"

using dvv::media::PointD;
using dvv::media::ViewMode;
using dvv::media::ViewRequest;
using dvv::media::ViewTransform;
using dvv::media::zoomAround;

namespace rawreel {

namespace {

// The open clip's width for the Last value examples; none without a clip.
std::optional<std::uint32_t> clipWidth(const Viewport& viewport) {
    const std::uint32_t width = viewport.imageSize().width;
    return width != 0 ? std::optional<std::uint32_t>(width) : std::nullopt;
}

const CommandInfo& commandInfo(Command command) {
    return commandTable()[static_cast<std::size_t>(command)];
}

// Edit -> Copy copies the Info window's rows while it is the floating window in front.
bool infoInFront(const FloatingWindow* in_front) {
    return in_front != nullptr && in_front->kind() == FloatingKind::Info;
}

// What the transport bar shows for `status`: nothing without a stream (a default BarStatus).
BarStatus barStatus(const SessionStatus& status) {
    if (status.streams == 0) {
        return {};
    }
    return {
        .player = status.player, .hasStream = true, .streamGeneration = status.streamGeneration};
}

// The Log pane's smallest height (M4 GUI D8), DIP, without its caption (about three lines of the
// log).
constexpr int LOG_MIN_HEIGHT_DIP = 60;
// Its smallest width, DIP, docked at a side: without one wxAUI counts the side dock as almost
// nothing in the minimum client size, but lays out the log wider and so cuts into the centre.
constexpr int LOG_MIN_WIDTH_DIP = 120;
// Added to the computed minimum client size: wxAUI's rows can come out 1 px short of it (two
// bottom rows at 150 %), which must not put the viewport below 320x240 DIP.
constexpr int LAYOUT_MARGIN_DIP = 2;
constexpr int LAST_COMMAND_ID = COMMAND_ID_BASE + static_cast<int>(COMMAND_COUNT) - 1;
constexpr const char* OPEN_WILDCARD =
    "Raw video (*.yuv;*.raw;*.rgb;*.bin;*.y4m)|*.yuv;*.raw;*.rgb;*.bin;*.y4m|All files (*.*)|*.*";
constexpr std::string_view OPEN_URI_EXAMPLE =
    "synthetic:gradient?fmt=yuv420p&size=1920x1080&frames=250";  // X15
// Spec §2: the notice field's width comes from this text.
constexpr const char* LONG_LOG_NOTICE = "99 errors, 99 warnings in the log";
// Spec §7: the variable status fields' shares of the width the fixed fields leave (file,
// position, rate, bottleneck), and the file field's width at the main window's minimum.
constexpr std::array<int, 4> VARIABLE_FIELD_SHARES{3, 3, 2, 2};
constexpr int FILE_FIELD_MINIMUM_DIP = 120;
// The four variable fields together at the minimum, so that the file field gets its 120 DIP.
constexpr int VARIABLE_FIELDS_MINIMUM_DIP = FILE_FIELD_MINIMUM_DIP *
                                            (VARIABLE_FIELD_SHARES[0] + VARIABLE_FIELD_SHARES[1] +
                                             VARIABLE_FIELD_SHARES[2] + VARIABLE_FIELD_SHARES[3]) /
                                            VARIABLE_FIELD_SHARES[0];

// The wx key codes Task 4's normaliser names (core/commands.cpp, D17).
static_assert(WXK_ESCAPE == 27 && WXK_SPACE == 32);
static_assert(WXK_END == 312 && WXK_HOME == 313);
static_assert(WXK_LEFT == 314 && WXK_UP == 315 && WXK_RIGHT == 316 && WXK_DOWN == 317);
static_assert(WXK_F11 == 350 && WXK_ADD == 335 && WXK_SUBTRACT == 337);
static_assert(WXK_NUMPAD0 == 324 && WXK_NUMPAD9 == 333);
static_assert(WXK_NUMPAD_ADD == 388 && WXK_NUMPAD_SUBTRACT == 390);

std::string_view commandId(Command command) noexcept {
    return commandTable()[static_cast<std::size_t>(command)].id;
}

std::string_view sourceName(CommandSource source) noexcept {
    switch (source) {
        case CommandSource::Script: return "script";
        case CommandSource::Key: return "key";
        case CommandSource::Menu: return "menu";
        case CommandSource::Button: return "button";
    }
    return "?";
}

// A file dialog's start folder (M4 GUI D6); empty lets the system choose.
wxString dialogFolder(const std::filesystem::path& folder) {
    return wxString::FromUTF8(dvv::pathDisplayText(folder));
}

// View > Format's context (D5 design §1.8, D5R §4.4): what applies without a colour key
// (FileColourMemory::colourBase: the stream's standard, or the remembered one of the file when
// the stream was opened with colour keys).
FormatDialogContext streamContext(const StreamInfo& stream, const FileColourMemory& memory,
                                  bool auto_detect) {
    // The source's header, container or graph fixes format, size and fps (SourceInfo::fixed).
    const bool locked = stream.info.fixed.covers(dvv::media::FixedFields::all());
    return FormatDialogContext{.locked = locked,
                               .base = memory.colourBase(stream),
                               .uri = {},
                               .provider = stream.info.provider,
                               .detection = streamDetection(stream.info),
                               .autoDetect = auto_detect};
}

// An open's context: the colour of the request URI's clip description, sidecar or file name.
// The header's detection line (D5R §4.4) comes with it.
FormatDialogContext openContext(const std::string& uri, FormatDetection detection,
                                bool auto_detect) {
    return FormatDialogContext{.locked = false,
                               .base = {},
                               .uri = uri,
                               .provider = {},
                               .detection = std::move(detection),
                               .autoDetect = auto_detect};
}

// Log pane and Settings design §4.4: macOS one type, plain text, with no format popup (wxOSX shows
// one only for two filters or more); Windows and Linux text files and all files.
#if defined(__WXOSX__)
constexpr const char* LOG_SAVE_WILDCARD = "Text files (*.txt)|*.txt";
#else
constexpr const char* LOG_SAVE_WILDCARD = "Text files (*.txt;*.log)|*.txt;*.log|All files|*";
#endif

// Spec §4.4 (addendum §5.5): `file` is a log file rawreel writes: the current one (by any path),
// or a name in the log folder in use that strictly matches the log file pattern (a later split
// could create it; names compare without case on Windows and macOS).
bool isLiveLogFile(const std::filesystem::path& file, const std::filesystem::path& current,
                   const std::filesystem::path& log_folder) {
    std::error_code error;  // a missing file or folder is never equivalent
    if (!current.empty() && std::filesystem::equivalent(file, current, error) && !error) {
        return true;
    }
    if (log_folder.empty()) {
        return false;
    }
    error.clear();
    if (!std::filesystem::equivalent(file.parent_path(), log_folder, error) || error) {
        return false;
    }
#if defined(__WXMSW__) || defined(__WXOSX__)
    constexpr dvv::LetterCase LOG_NAME_LETTERS = dvv::LetterCase::IgnoreAscii;
#else
    constexpr dvv::LetterCase LOG_NAME_LETTERS = dvv::LetterCase::Exact;
#endif
    return dvv::parseSplitLogName(dvv::pathDisplayText(file.filename()), LOG_FILE_PREFIX,
                                  LOG_NAME_LETTERS)
        .has_value();
}

// The widths of the status bar's three fixed fields at its font. The cursor field is as wide
// as WIDEST_CURSOR_TEXT plus a margin, so a move never re-lays the bar out. The display mapping is
// as wide as its longest text plus a margin. The Log notice is as wide as a long notice plus the
// same margin.
std::array<int, 3> fixedFieldWidths(wxStatusBar& bar) {
    wxClientDC dc(&bar);
    int mapping = 0;
    for (const dvv::media::TransferFn curve :
         {dvv::media::TransferFn::Pq, dvv::media::TransferFn::Hlg,
          dvv::media::TransferFn::Bt1886}) {
        dvv::media::ColourParams sample{};
        sample.transfer = curve;
        mapping = std::max(mapping, dc.GetTextExtent(wxString::FromUTF8(mappingText(&sample))).x);
    }
    const int margin = bar.FromDIP(24);
    return {dc.GetTextExtent(wxString::FromUTF8(WIDEST_CURSOR_TEXT)).x + margin, mapping + margin,
            dc.GetTextExtent(LONG_LOG_NOTICE).x + margin};
}

// D5R §2.2: the mapping status field, the display mapping of the active (first) stream.
wxString mappingField(const std::vector<StreamInfo>& streams) {
    return wxString::FromUTF8(
        mappingText(streams.empty() ? nullptr : &streams.front().colour.params));
}

// View zoom design §3.1, §4: `image` at 100 %, its centre at the viewport's centre.
ViewRequest actualSizeView(dvv::platform::PixelSize image) {
    return {.mode = ViewMode::Zoom,
            .zoom = 1,
            .centre = {.x = image.width / 2.0, .y = image.height / 2.0},
            .image = image};
}

// The colour component (0, 1, 2) a plane command shows or hides; nullopt for other commands.
std::optional<std::size_t> planeIndex(Command command) {
    const auto first = static_cast<std::size_t>(Command::Plane1);
    const auto value = static_cast<std::size_t>(command);
    if (value < first || value > static_cast<std::size_t>(Command::Plane3)) {
        return std::nullopt;
    }
    return value - first;
}

}  // namespace

int menuId(Command command) noexcept {
    if (command == Command::Settings) {
        return wxID_PREFERENCES;
    }
    if (const std::optional<int> stock = native_policy::stockId(command); stock) {
        return *stock;
    }
    return COMMAND_ID_BASE + static_cast<int>(command);
}

std::optional<Command> commandOfMenuId(int id) noexcept {
    if (id == wxID_PREFERENCES) {
        return Command::Settings;
    }
    if (const std::optional<Command> stock = native_policy::commandOfStockId(id); stock) {
        return stock;
    }
    if (id < COMMAND_ID_BASE || id > LAST_COMMAND_ID) {
        return std::nullopt;
    }
    const auto command = static_cast<Command>(id - COMMAND_ID_BASE);
    return menuId(command) == id ? std::optional(command) : std::nullopt;
}

std::string typedRecentEntry(std::string_view text) {
    std::error_code error;
    std::filesystem::path folder = std::filesystem::current_path(error);
    if (error) {
        folder.clear();
    }
    return recentEntryOfText(text, folder);
}

MainFrame::MainFrame(std::shared_ptr<DisplayState> display)
    : wxFrame(nullptr, wxID_ANY, "rawreel"), aui_([this] {
          // A size event before the player panel exists: the viewport minimum.
          return player_ != nullptr
                     ? player_->minimumSize()
                     : FromDIP(wxSize(MIN_VIEWPORT_DIP.width, MIN_VIEWPORT_DIP.height));
      }) {
    // App icon plan D4, D5: the ICO group (Windows), the embedded set (Linux); none on macOS, where
    // the bundle icon is used. wx asserts on an empty bundle, so it is set only when loaded.
    if (const wxIconBundle icons = windowIcons(); !icons.IsEmpty()) {
        SetIcons(icons);
    }
    aui_.manage(this);
    player_ = new PlayerPanel(
        this, std::move(display),
        [this](Command command) { execute(command, CommandSource::Button); },
        [this](std::int64_t index) {
            if (Session* session = wxGetApp().session(); session != nullptr && !closing_) {
                session->seek(index);
            }
        },
        // The bar's state read (spec §3): the status onStatusChanged would take now.
        [] {
            const Session* session = wxGetApp().session();
            return session != nullptr ? barStatus(session->status()) : BarStatus{};
        });
    viewport_ = &player_->viewport();
    // One target on the viewport and one on the player panel (GTK may deliver a drop on the
    // native child to the parent, R5).
    const auto open = [this](std::vector<std::filesystem::path> files) {
        openFiles(std::move(files));
    };
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): wx takes ownership
    viewport_->SetDropTarget(new DropTarget(open));
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): wx takes ownership
    player_->SetDropTarget(new DropTarget(open));
    log_ = new LogPanel(this, wxGetApp().sourceSuffix());
    log_->setSaveHandler([this](bool selected) {  // the list's context menu (spec §4.3)
        execute(selected ? Command::SaveSelectedLines : Command::SaveLog, CommandSource::Menu);
    });
    // The centre pane never floats, so the viewport's native child window is never re-parented
    // (M1b F14); no border, so the presented extent equals the pane.
    aui_.AddPane(player_, wxAuiPaneInfo().Name("player").CenterPane().PaneBorder(false));
    // Spec §2: hidden unless the config says otherwise; a selftest always starts with it hidden.
    log_shown_at_start_ =
        wxGetApp().config().gui.showLogPane && App::arguments().selftest == Scenario::None;
    aui_.AddPane(log_, wxAuiPaneInfo()
                           .Name("log")
                           .Caption("Log")
                           .Bottom()
                           .BestSize(FromDIP(wxSize(800, 160)))
                           .MinSize(FromDIP(wxSize(LOG_MIN_WIDTH_DIP, LOG_MIN_HEIGHT_DIP)))
                           .CloseButton(true)
                           .Show(log_shown_at_start_));
    // Spec §2: the pane keeps its height across hide and show within a run. wxAUI sizes the dock
    // it re-creates on show from the pane's BestSize, so BestSize follows the height the pane has
    // while shown: it is the last height before any hide (View -> Log, the caption's close button).
    log_->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        wxAuiPaneInfo& pane = aui_.GetPane(log_);
        if (pane.IsOk() && pane.IsShown() && event.GetSize().y > 0) {
            pane.BestSize(pane.best_size.x, event.GetSize().y);
        }
    });
    // The caption's close button hides the pane without setLogShown(): the focus leaves it after.
    Bind(wxEVT_AUI_PANE_CLOSE, [this](wxAuiManagerEvent& event) {
        event.Skip();
        if (event.GetPane() != nullptr && event.GetPane()->window == log_) {
            CallAfter([this] { moveFocusOutOfLog(); });  // after wxAUI hid it
        }
    });
    aui_.Update();
    buildMenus();
    buildStatusBar();
    // The zoom button follows every view change and every resize.
    viewport_->setViewChangedHandler([this] {
        showZoom();
        showGridNotes();
    });
    // The cursor field follows the image pixel under the pointer; empty off the image and without
    // a clip. Set only when its text changes.
    viewport_->setCursorHandler([this](std::optional<dvv::media::ImagePixel> pixel) {
        const wxString text = wxString::FromUTF8(cursorText(pixel));
        if (text != status_texts_[CURSOR_FIELD]) {
            status_texts_[CURSOR_FIELD] = text;
            SetStatusText(text, static_cast<int>(CURSOR_FIELD));
        }
    });
    // The grid and ruler settings loaded at start.
    setOverlay(wxGetApp().overlay().grid, wxGetApp().overlay().rulers, OverlaySave::Later);
    // A context menu on the picture: Grid and Rulers.
    viewport_->setContextMenuHandler([this](wxPoint point) { popUpViewportMenu(point); });
    updatePlaneControls();  // the bar's plane toggles: their letters with no clip, disabled
    // Zoom buttons design §2.2: the zoom button's menu for the shown view. Its Fit, Actual size
    // and Resize items reach onMenu; its percentages onZoomMenu.
    transportBar().setZoomMenu([this] {
        const ViewTransform view = viewport_->shownView();
        return zoomMenu(view.mode(), view.zoom());
    });
    Bind(wxEVT_MENU, &MainFrame::onZoomMenu, this, ZOOM_MENU_ID_BASE,
         ZOOM_MENU_ID_BASE + static_cast<int>(ZOOM_MENU_STEPS.size()) - 1);
    SetClientSize(FromDIP(wxSize(1280, 720)));
    applyLayoutLimits();
    Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) {
        event.Skip();
        CallAfter([this] { applyLayoutLimits(); });  // after wx and wxAUI scaled their sizes
    });
    // Every Update() that changes a dock (a pane moved, docked as a new row, floated or shown)
    // resizes the centre pane.
    player_->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        checkLayoutLimits();
    });
    // The handler does not Skip(), so wxInfoBarGeneric's own button handler (which would hide the
    // bar) does not run; execute() dismisses it.
    player_->infoBar().Bind(
        wxEVT_BUTTON,
        [this](wxCommandEvent& /*event*/) { execute(Command::Reopen, CommandSource::Button); },
        INFOBAR_REOPEN_ID);
    player_->infoBar().Bind(
        wxEVT_BUTTON,
        [this](wxCommandEvent& /*event*/) {
            std::string error_uri = format_error_uri_;  // dismissMessage() clears both
            std::optional<std::string> error_entry = format_error_entry_;
            dismissMessage();
            if (!error_uri.empty()) {
                format_error_uri_ = std::move(error_uri);
                format_error_entry_ = std::move(error_entry);
                showErrorFormat();  // an open error of a raw file (D4b): no stream to format
            } else {
                execute(Command::Format, CommandSource::Button);
            }
        },
        INFOBAR_FORMAT_ID);
    Bind(wxEVT_CHAR_HOOK, &MainFrame::onCharHook, this);
    Bind(wxEVT_ACTIVATE, &MainFrame::onActivate, this);
    Bind(wxEVT_ICONIZE, &MainFrame::onIconize, this);
    Bind(wxEVT_SHOW, &MainFrame::onShow, this);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::onClose, this);
    // The Log pane's feed (spec §3.2), last, once the status bar exists: the store's notify runs on
    // the log thread and posts onLogChanged to the GUI thread (CallAfter, no timer); the first call
    // takes what was logged so far.
    if (const std::shared_ptr<LogStore>& store = wxGetApp().logStore(); store != nullptr) {
        store->setNotify([this] { CallAfter([this] { onLogChanged(); }); });
    }
    onLogChanged();
}

MainFrame::~MainFrame() {
    // First (spec §3.3): no notify reaches this frame from now on; posted refreshes die with it.
    if (const std::shared_ptr<LogStore>& store = wxGetApp().logStore(); store != nullptr) {
        store->setNotify({});
    }
    stopStatusUpdates();
    if (selftest_) {
        selftest_->stop();
    }
    // Normally done in onClose already; also covers a frame deleted without a close event (a
    // failed start): the presenter is joined while the native windows still exist.
    if (viewport_ != nullptr) {
        viewport_->detachStack();
    }
    aui_.UnInit();
    selftest_.reset();
}

void MainFrame::onLogChanged() {
    // No logging here (spec §3.3): a line logged here would post another refresh, forever.
    App& app = wxGetApp();
    // Addendum §5.3: a split since the last refresh checks the log files' size; the warning, if
    // any, is posted, never logged here.
    app.checkLogSplits();
    const std::shared_ptr<LogStore>& store = app.logStore();
    if (store == nullptr || log_ == nullptr) {
        return;
    }
    LogBatch batch = store->take(log_->mirror().next());
    log_counts_ = batch.counts;  // setLogShown() sets seen_ from them without another take
    const bool shown = logShown();
    log_->refresh(std::move(batch), shown);
    if (shown) {
        seen_ = log_counts_;  // nothing is unseen while the pane is shown
    }
    updateLogNotice();
}

void MainFrame::buildMenus() {
    auto* bar = new wxMenuBar();
    std::vector<std::pair<std::string_view, wxMenu*>> menus;  // in first-use order
    for (const CommandInfo& info : commandTable()) {
        wxMenu* menu = nullptr;
        for (const auto& [name, existing] : menus) {
            if (name == info.menu) {
                menu = existing;
            }
        }
        if (menu == nullptr) {
            menu = new wxMenu();
            menus.emplace_back(info.menu, menu);
            bar->Append(menu, wxString::FromUTF8(info.menu));
        }
        // An Accelerator command's "\t<key>" makes wx register the accelerator; CharHook commands
        // show their key without "\t" (Task 10 dispatches them).
        // The plane items start with planeLetters() (no clip yet); onUpdateUi follows the clip's
        // in the title, updatePlaneControls in the help.
        const std::optional<std::size_t> plane = planeIndex(info.command);
        const wxString label = wxString::FromUTF8(
            plane ? menuLabel(info, planeTitle(planeLetters().at(*plane))) : menuLabel(info));
        const wxString help = wxString::FromUTF8(plane ? planeHelp(planeLetters().at(*plane))
                                                       : std::string(info.help));
        if (native_policy::settingsAtEndOfEdit() && info.command == Command::Settings) {
            continue;  // Edit -> Preferences..., below
        }
        if (info.command == Command::Plane1) {
            menu->AppendSeparator();  // after Grid and Rulers...: the planes, the fill and Alpha
        }
        if (info.check == MenuCheck::Shown && native_policy::shownItemHasChangingTitle()) {
            // macOS shows and hides a part of a window with a title that changes, not a check
            // mark; onUpdateUi keeps the title. A mode (Alpha) stays a check item there too.
            const bool shown = menuState(info.command);
            menu->Append(menuId(info.command), wxString::FromUTF8(shownItemTitle(info, shown)),
                         help);
        } else if (info.check != MenuCheck::None) {
            menu->AppendCheckItem(menuId(info.command), label, help);  // onUpdateUi checks it
        } else {
            menu->Append(menuId(info.command), label, help);
        }
        if (info.command == Command::ResizeWindowToActualSize) {
            menu->AppendSeparator();
        }
        if (info.command == Command::OpenUri) {
            // Open Recent spec §3: right after Open URI... on every OS, no key, no mnemonic; its
            // items come from recentChanged().
            recent_item_ = menu->AppendSubMenu(new wxMenu(), "Open Recent");
        }
    }
    buildEditMenu(*bar);  // between File and Playback (spec §4.2)
    if (native_policy::settingsAtEndOfEdit()) {
        // Spec §5.1: GTK puts Preferences... at the end of Edit, after a separator.
        const CommandInfo& settings = commandTable()[static_cast<std::size_t>(Command::Settings)];
        wxMenu* edit = bar->GetMenu(1);
        edit->AppendSeparator();
        edit->Append(menuId(Command::Settings), wxString::FromUTF8(menuLabel(settings)),
                     wxString::FromUTF8(settings.help));
    }
    SetMenuBar(bar);
    Bind(wxEVT_MENU, &MainFrame::onMenu, this, COMMAND_ID_BASE, LAST_COMMAND_ID);
    Bind(wxEVT_UPDATE_UI, &MainFrame::onUpdateUi, this, COMMAND_ID_BASE, LAST_COMMAND_ID);
    // Open Recent's items (spec §3): not commands, and no update-UI handler (recentChanged()
    // enables the submenu item).
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent& event) {
            openRecent(static_cast<std::size_t>(event.GetId() - RECENT_ID_BASE));
        },
        RECENT_ID_BASE, RECENT_ID_BASE + 9);
    // Spec §4.2: the Edit items act on the focused control. On macOS these arrive only when the
    // native responder chain did not handle the item (and always for Undo and Redo).
    for (const int id :
         {wxID_UNDO, wxID_REDO, wxID_CUT, wxID_COPY, wxID_PASTE, wxID_CLEAR, wxID_SELECTALL}) {
        Bind(wxEVT_MENU, &MainFrame::onEditMenu, this, id);
        Bind(wxEVT_UPDATE_UI, &MainFrame::onEditUpdateUi, this, id);
    }
    // Settings... carries wxID_PREFERENCES on every OS (spec §5.1).
    Bind(wxEVT_MENU, &MainFrame::onMenu, this, wxID_PREFERENCES);
    Bind(wxEVT_UPDATE_UI, &MainFrame::onUpdateUi, this, wxID_PREFERENCES);
    bindApplicationMenu();
    recentChanged();
}

void MainFrame::recentChanged() {
    if (recent_item_ == nullptr) {
        return;
    }
    wxMenu& submenu = *recent_item_->GetSubMenu();
    while (submenu.GetMenuItemCount() > 0) {
        submenu.Destroy(submenu.FindItemByPosition(0));
    }
    const std::vector<std::string>& entries = wxGetApp().recent().entries();
    const std::vector<RecentMenuItem> items = recentMenuItems(entries, NATIVE_TITLE_STYLE);
    for (std::size_t i = 0; i < items.size(); ++i) {
        submenu.Append(RECENT_ID_BASE + static_cast<int>(i), wxString::FromUTF8(items[i].label),
                       wxString::FromUTF8(items[i].help));
    }
    recent_item_->Enable(!entries.empty());
}

void MainFrame::openRecent(std::size_t index) {
    const std::vector<std::string>& entries = wxGetApp().recent().entries();
    if (index >= entries.size()) {
        return;
    }
    const std::string entry = entries[index];  // a copy: the open's success changes the list
    openUri(0, entry, std::nullopt, entry);
}

void MainFrame::recordRecent(const std::string& entry) {
    if (const std::optional<std::string_view> problem = recentEntryProblem(entry)) {
        DVV_LOG_INFO("not added to Open Recent: {}", *problem);
        return;
    }
    App& app = wxGetApp();
    if (!app.recent().add(entry)) {
        return;  // already on top: nothing is saved (spec §7)
    }
    const std::array<ConfigValue, 1> values{
        {{.key = config_key::GUI_RECENT, .value = app.recent().entries()}}};
    app.saveHistory(values);  // a failure is logged there; the list stays in effect
    recentChanged();
}

void MainFrame::buildStatusBar() {
    // Spec §7: without wxSTB_SHOW_TIPS, which shows only a cut field's own text and asserts on a
    // tooltip set by hand; the bar keeps its size grip and its end cut.
    CreateStatusBar(static_cast<int>(status_texts_.size()), wxSTB_DEFAULT_STYLE & ~wxSTB_SHOW_TIPS);
    const std::array<int, 3> fixed = fixedFieldWidths(*GetStatusBar());
    const std::array<int, STATUS_FIELD_COUNT> widths{-VARIABLE_FIELD_SHARES[0],
                                                     -VARIABLE_FIELD_SHARES[1],
                                                     -VARIABLE_FIELD_SHARES[2],
                                                     -VARIABLE_FIELD_SHARES[3],
                                                     fixed[0],
                                                     fixed[1],
                                                     fixed[2]};
    SetStatusWidths(static_cast<int>(widths.size()), widths.data());
#if defined(__WXMSW__)
    // Spec §7: wxMSW caps a tooltip at min(400 px, half the display); a path stays on one line
    // where it fits.
    wxToolTip::SetMaxWidth(FromDIP(800));
#endif
    wxStatusBar* const bar = GetStatusBar();
    bar->Bind(wxEVT_LEFT_UP, &MainFrame::onStatusBarLeftUp, this);
    bar->Bind(wxEVT_MOTION, &MainFrame::onStatusBarMotion, this);
    bar->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& event) {
        event.Skip();
        if (wxStatusBar* const status_bar = GetStatusBar(); status_bar != nullptr) {
            status_bar->UnsetToolTip();
        }
        tip_field_.reset();
        tip_text_.clear();
    });
    bar->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        // This handler runs before the bar's own: re-cut once wx has laid the fields out.
        CallAfter([this] {
            if (!closing_) {
                cutFileField();
            }
        });
    });
}

int MainFrame::statusFixedWidth() const {
    wxStatusBar* const bar = GetStatusBar();
    if (bar == nullptr) {
        return 0;
    }
    const std::array<int, 3> fixed = fixedFieldWidths(*bar);
    return fixed[0] + fixed[1] + fixed[2];
}

int MainFrame::statusMinimumWidth() const {
    return statusFixedWidth() + FromDIP(VARIABLE_FIELDS_MINIMUM_DIP);
}

void MainFrame::showZoom() {
    if (closing_ || viewport_ == nullptr) {
        return;
    }
    std::string zoom;
    if (!viewport_->imageSize().empty()) {
        const ViewTransform view = viewport_->shownView();
        zoom = zoomText(view.mode(), view.zoom());
    }
    // Zoom buttons design §4: the bar's zoom controls, enabled as the View menu's items.
    transportBar().showZoom({.text = std::move(zoom),
                             .available = isAvailable(Command::FitToWindow),
                             .canZoomOut = isAvailable(Command::ZoomOut),
                             .canZoomIn = isAvailable(Command::ZoomIn)});
}

void MainFrame::showGridNotes() {
    auto* const window = dynamic_cast<GridWindow*>(floatingWindow(FloatingKind::GridAndRulers));
    if (closing_ || viewport_ == nullptr || window == nullptr) {
        return;
    }
    window->showNotes(viewport_->shownView().zoom(), viewport_->backingPerDip());
}

void MainFrame::setOverlay(const GridConfig& grid, const RulerConfig& rulers, OverlaySave save) {
    if (closing_ || viewport_ == nullptr) {
        return;  // the close saved the settings; the viewport goes during it
    }
    grid_ = grid;
    rulers_ = rulers;
    viewport_->setOverlayConfig(grid_, rulers_);
    transportBar().setGridShown(grid_.levels[0].show);
    if (auto* const window = dynamic_cast<GridWindow*>(floatingWindow(FloatingKind::GridAndRulers));
        window != nullptr) {
        window->showOverlay(grid_, rulers_);
    }
    if (save == OverlaySave::Now) {
        (void)wxGetApp().saveOverlay(grid_, rulers_, /*closing=*/false);
    }
}

void MainFrame::applyViewCommand(Command command) {
    const dvv::platform::PixelSize extent = viewport_->pictureSize();
    const dvv::platform::PixelSize image = viewport_->imageSize();
    if (command == Command::FitToWindow) {
        viewport_->setView({});
        return;
    }
    if (command == Command::ActualSize) {
        zoomAtCentre(1);
        return;
    }
    // Backing pixels in P: P's centre stays on its clip point.
    const PointD anchor{.x = extent.width / 2.0, .y = extent.height / 2.0};
    const std::optional<ViewRequest> next =
        stepZoom(viewport_->view(), extent, image, command == Command::ZoomIn, anchor);
    if (next) {  // none at the limit (isAvailable said so)
        viewport_->setView(*next);
    }
}

void MainFrame::zoomAtCentre(double zoom) {
    const dvv::platform::PixelSize extent = viewport_->pictureSize();
    // Backing pixels in P: P's centre stays on its clip point.
    const PointD anchor{.x = extent.width / 2.0, .y = extent.height / 2.0};
    viewport_->setView(zoomAround(viewport_->view(), extent, viewport_->imageSize(), zoom, anchor));
}

void MainFrame::onZoomMenu(wxCommandEvent& event) {
    if (closing_ || !isAvailable(Command::FitToWindow)) {
        return;
    }
    const int index = event.GetId() - ZOOM_MENU_ID_BASE;
    if (index >= 0 && std::cmp_less(index, ZOOM_MENU_STEPS.size())) {
        zoomAtCentre(ZOOM_MENU_STEPS[static_cast<std::size_t>(index)]);
    }
}

std::unique_ptr<wxMenu> zoomMenu(ViewMode mode, double zoom) {
    const ZoomMenuEntry checked = zoomMenuChecked(mode, zoom);
    auto menu = std::make_unique<wxMenu>();
    const auto command_item = [&menu, &checked](Command command, ZoomMenuCheck check) {
        const CommandInfo& info = commandTable()[static_cast<std::size_t>(command)];
        const wxString label = wxString::FromUTF8(menuLabel(info));
        if (check == ZoomMenuCheck::None) {
            menu->Append(menuId(command), label);
        } else {
            menu->AppendCheckItem(menuId(command), label)->Check(checked.check == check);
        }
    };
    command_item(Command::FitToWindow, ZoomMenuCheck::Fit);
    command_item(Command::ActualSize, ZoomMenuCheck::ActualSize);
    menu->AppendSeparator();
    for (std::size_t i = 0; i < ZOOM_MENU_STEPS.size(); ++i) {
        menu->AppendCheckItem(ZOOM_MENU_ID_BASE + static_cast<int>(i),
                              wxString::FromUTF8(zoomText(ViewMode::Zoom, ZOOM_MENU_STEPS[i])))
            ->Check(checked.check == ZoomMenuCheck::Step && checked.step == i);
    }
    menu->AppendSeparator();
    command_item(Command::ResizeWindowToActualSize, ZoomMenuCheck::None);
    return menu;
}

wxRect MainFrame::workArea() const {
    return work_area_for_test_ ? *work_area_for_test_ : workAreaOf(*this);
}

WindowNow MainFrame::windowNow() const {
    const wxSize client = GetClientSize();
    const wxSize viewport = viewport_->GetClientSize();
    const wxSize minimum = GetMinClientSize();
    return {.outer = screenRect(GetScreenRect()),
            .client = {.width = client.x, .height = client.y},
            .viewport = {.width = viewport.x, .height = viewport.y},
            .minimumClient = {.width = minimum.x, .height = minimum.y}};
}

bool MainFrame::windowIsFixed() const {
#if defined(__WXOSX__)
    return IsFullScreen();
#else
    return IsMaximized() || IsFullScreen();
#endif
}

namespace {

// View zoom design §3.1, §3.2 (rev 6): after the open sizing the view is Fit, so the picture
// follows the window, unless Fit would show the clip above 100 % in a picture region P of `extent`
// backing pixels (fit zoom > 1.002; the allowance covers viewportForClip's 1-pixel round-up on a
// 2x display): then Zoom 100 %.
bool opensAtActualSize(dvv::platform::PixelSize image, dvv::platform::PixelSize extent) {
    return ViewTransform::fit(extent, image).zoom() > 1.002;
}

}  // namespace

void MainFrame::sizeWindowForOpen() {
    const dvv::platform::PixelSize image = viewport_->imageSize();
    // A minimised window reports no usable size (wxMSW: position -32000, client 0x0); the view
    // stays Fit.
    if (image.empty() || IsIconized()) {
        return;
    }
    if (windowIsFixed()) {
        // §3.2: the window stays; 100 % when Fit would show the clip above 100 %, else Fit.
        if (opensAtActualSize(image, viewport_->pictureSize())) {
            viewport_->setView(actualSizeView(image));
        }
        return;
    }
    const double scale = viewport_->contentScale();
    const LayoutSize wanted = viewportForActualSize(image);
    const WindowNow now = windowNow();
    const ScreenRect work = screenRect(workArea());
    if (const std::optional<ScreenRect> rect =
            windowForViewport(now, wanted, work, WindowSizing::Exact)) {
        SetSize(wxRectOf(*rect));
        // P of the viewport the window gets, in backing pixels: wanted, or larger at the minimum
        // window, minus the strips. Computed, as the size may arrive later (wxGTK).
        const auto backing = [scale](int window_units) {
            return static_cast<std::uint32_t>(std::lround(std::max(0, window_units) * scale));
        };
        const dvv::platform::PixelSize extent{
            .width = backing(rect->width - (now.outer.width - now.viewport.width)),
            .height = backing(rect->height - (now.outer.height - now.viewport.height))};
        const dvv::platform::PixelSize picture =
            dvv::media::pictureRegion(extent, viewport_->overlayParams().strips).size();
        if (opensAtActualSize(image, picture)) {
            viewport_->setView(actualSizeView(image));
        }  // else the view stays Fit (openUri set it)
    } else if (const std::optional<ScreenRect> shape =
                   windowForViewport(now, wanted, work, WindowSizing::KeepShape)) {
        SetSize(wxRectOf(*shape));  // the view stays Fit (openUri set it)
    }
}

LayoutSize MainFrame::viewportForActualSize(dvv::platform::PixelSize image) const {
    // P at least the clip at any scale; at a fractional one up to scale + 1 pixels more.
    const LayoutSize clip = viewportForClip(layoutSizeOf(image), viewport_->contentScale());
    const int strip = viewport_->stripWindowUnits();
    return {.width = clip.width + strip, .height = clip.height + strip};
}

void MainFrame::resizeWindowToActualSize() {
    if (windowIsFixed()) {
        applyViewCommand(Command::ActualSize);
        return;
    }
    const dvv::platform::PixelSize image = viewport_->imageSize();
    const LayoutSize wanted = viewportForActualSize(image);
    const std::optional<ScreenRect> rect =
        windowForViewport(windowNow(), wanted, screenRect(workArea()), WindowSizing::Clamped);
    if (!rect) {
        return;  // no image or no work area
    }
    SetSize(wxRectOf(*rect));
    viewport_->setView(actualSizeView(image));  // at 100 %, panned to the clip's centre
}

Session& MainFrame::session() const noexcept {
    return *wxGetApp().session();
}

void MainFrame::startSelfTest(Scenario scenario) {
    if (closing_ || selftest_) {
        return;
    }
    selftest_ = std::make_unique<SelfTest>(*this, wxGetApp());
    selftest_->start(scenario);
}

bool MainFrame::hasStream() const {
    const Session* session = wxGetApp().session();
    return session != nullptr && session->status().streams > 0;
}

bool MainFrame::isAvailable(Command command) const {
    switch (command) {
        case Command::Quit:
        case Command::Reopen:  // D25: also with a healthy stack (rebuilds it) or without a stream
        case Command::GpuInfo:
        case Command::LogPane:
        case Command::InfoWindow:
        case Command::Grid:
        case Command::Rulers:
        case Command::GridWindow:
        case Command::Diagnostics:
        case Command::Settings:
        case Command::About: return true;
        case Command::CloseStreams:
            if (native_policy::closeCommandClosesSettingsFirst()) {
                return hasStream() || settingsInFront() != nullptr || floatingInFront() != nullptr;
            }
            [[fallthrough]];
        case Command::PlayPause:
        case Command::Stop:
        case Command::StepBack:
        case Command::StepForward:
        case Command::StepBack10:
        case Command::StepForward10:
        case Command::First:
        case Command::Loop: return hasStream();
        case Command::FitToWindow:
        case Command::ActualSize:
        case Command::ZoomIn:
        case Command::ZoomOut:
        case Command::ResizeWindowToActualSize: {
            if (!hasStream() || viewport_ == nullptr || viewport_->imageSize().empty()) {
                return false;
            }
            // Zoom in and out: a next step from the shown zoom (none at the limits).
            const double zoom = viewport_->shownView().zoom();
            const double fit = viewport_->fitZoom();
            if (command == Command::ZoomIn) {
                return nextZoomIn(zoom, fit).has_value();
            }
            if (command == Command::ZoomOut) {
                return nextZoomOut(zoom, fit).has_value();
            }
            return true;
        }
        case Command::Last: {
            const Session* session = wxGetApp().session();
            if (session == nullptr) {
                return false;
            }
            const SessionStatus status = session->status();
            return status.streams > 0 && status.player.length.has_value();
        }
        case Command::Open:
        case Command::OpenUri: return true;
        case Command::SaveLog: return log_ != nullptr && log_->mirror().size() > 0;
        // Spec §4.3: disabled when no row is selected (a hidden pane shows none).
        case Command::SaveSelectedLines: return logShown() && log_->hasSelection();
        case Command::Format: return hasStream() && !streams_.empty();
        case Command::Plane1: return hasStream();
        case Command::Plane2:
        case Command::Plane3:
            return hasStream() && !streams_.empty() &&
                   dvv::media::formatDesc(streams_.front().info.desc.format).componentCount >= 3;
        case Command::HiddenChromaZero:
            return hasStream() && !streams_.empty() &&
                   dvv::media::hasChroma(streams_.front().info.desc.format,
                                         streams_.front().colour.params);
        case Command::Alpha: return hasStream() && clipHasAlpha();
    }
    return false;
}

void MainFrame::execute(Command command, CommandSource source) {
    if (closing_) {
        return;
    }
    // Spec §5.1: ⌘W closes the Settings window when it is in front (it has no menu of its own);
    // otherwise Close stream acts on the main window.
    if (native_policy::closeCommandClosesSettingsFirst() && command == Command::CloseStreams) {
        if (wxWindow* settings = settingsInFront(); settings != nullptr) {
            settings->Close();
            return;
        }
        // A floating window too, through setWindowShown.
        if (const FloatingWindow* const floating = floatingInFront(); floating != nullptr) {
            setWindowShown(floating->kind(), false);
            return;
        }
    }
    if (!isAvailable(command)) {
        DVV_LOG_DEBUG("command {} is not available", commandId(command));
        if (command == Command::Alpha) {
            updateAlphaControls();  // a refused click on the bar toggle has flipped its value
        } else if (planeIndex(command)) {
            updatePlaneControls();  // as for Alpha
        }
        return;
    }
    if (command == Command::Quit) {
        Close();
        return;
    }
    if (command == Command::GpuInfo) {
        showGpuInfo();
        return;
    }
    if (command == Command::Open) {
        showOpenDialog();
        return;
    }
    if (command == Command::OpenUri) {
        showOpenUriDialog();
        return;
    }
    if (command == Command::Format) {
        showStreamFormat();
        return;
    }
    if (command == Command::About) {
        showAbout(*this);
        return;
    }
    if (command == Command::LogPane) {
        setLogShown(!logShown());
        return;
    }
    if (command == Command::InfoWindow) {
        setWindowShown(FloatingKind::Info, !windowShown(FloatingKind::Info));
        return;
    }
    if (command == Command::GridWindow) {
        setWindowShown(FloatingKind::GridAndRulers, true);
        return;
    }
    if (command == Command::Grid || command == Command::Rulers) {
        GridConfig grid = grid_;
        RulerConfig rulers = rulers_;
        if (command == Command::Grid) {
            grid.levels[0].show = !grid.levels[0].show;
        } else {
            rulers.show = !rulers.show;
        }
        setOverlay(grid, rulers, OverlaySave::Now);
        return;
    }
    if (command == Command::Settings) {
        showSettings();
        return;
    }
    if (command == Command::SaveLog || command == Command::SaveSelectedLines) {
        saveLog(command == Command::SaveSelectedLines);
        return;
    }
    if (command == Command::FitToWindow || command == Command::ActualSize ||
        command == Command::ZoomIn || command == Command::ZoomOut) {
        applyViewCommand(command);
        return;
    }
    if (command == Command::ResizeWindowToActualSize) {
        resizeWindowToActualSize();
        return;
    }
    if (command == Command::Plane1 || command == Command::Plane2 || command == Command::Plane3 ||
        command == Command::HiddenChromaZero) {
        if (const std::optional<std::size_t> plane = planeIndex(command)) {
            planes_.at(*plane) = !planes_.at(*plane);
        } else {
            chroma_zero_ = !chroma_zero_;
        }
        const std::array<char, 3> letters = planeLetters();
        const auto state = [&](std::size_t k) { return planes_.at(k) ? "on" : "off"; };
        // One line per change with its source, as for loop and alpha; the fill only where the clip
        // has U and V.
        const std::string fill =
            isAvailable(Command::HiddenChromaZero)
                ? std::format(", hidden U and V at {}", chroma_zero_ ? "0" : "mid")
                : std::string();
        DVV_LOG_INFO("planes {} {}, {} {}, {} {}{} ({})", letters[0], state(0), letters[1],
                     state(1), letters[2], state(2), fill, sourceName(source));
        if (Session* const session = wxGetApp().session(); session != nullptr) {
            session->setHiddenComponents({.components = {!planes_[0], !planes_[1], !planes_[2]},
                                          .chroma = chroma_zero_ ? dvv::media::ChromaFill::Zero
                                                                 : dvv::media::ChromaFill::Mid});
        }
        updatePlaneControls();
        return;
    }
    if (command == Command::Alpha) {
        alpha_ = !alpha_;
        DVV_LOG_INFO("alpha {} ({})", alpha_ ? "on" : "off", sourceName(source));
        viewport_->setAlpha(alpha_);
        updateAlphaControls();
        return;
    }
    Session* session = wxGetApp().session();
    if (session == nullptr) {
        return;
    }
    switch (command) {
        case Command::CloseStreams:
            recent_pending_.closed();  // Open Recent spec §5
            sizing_uri_.clear();       // view zoom design §3.3
            viewport_->setView({});    // the next stream starts at Fit
            session->closeStreams();
            return;
        case Command::PlayPause: session->togglePlay(); return;
        case Command::Stop: session->stop(); return;
        case Command::StepBack: session->step(-1); return;
        case Command::StepForward: session->step(1); return;
        case Command::StepBack10: session->step(-10); return;
        case Command::StepForward10: session->step(10); return;
        case Command::First: session->seek(0); return;
        case Command::Last:
            if (const std::optional<std::int64_t> length = session->status().player.length;
                length) {
                session->seek(*length - 1);
            }
            return;
        case Command::Loop:
            // The flag mirrors the Session's loop_, which survives stream changes.
            loop_ = !loop_;
            // One line per change with its source: a selftest log shows any toggle it did not make.
            DVV_LOG_INFO("loop {} ({})", loop_ ? "on" : "off", sourceName(source));
            session->setLoop(loop_);
            return;
        case Command::Reopen:
            dismissMessage();
            session->reopen();  // repeated clicks are coalesced by the Session
            return;
        case Command::Diagnostics:
            // Spec §2: logDiagnostics() is a queued Session command; the pane is shown now and the
            // lines stream into it as they are written.
            session->logDiagnostics();
            setLogShown(true);
            return;
        case Command::FitToWindow:  // the five: above
        case Command::ActualSize:
        case Command::ZoomIn:
        case Command::ZoomOut:
        case Command::ResizeWindowToActualSize:
        case Command::Quit:
        case Command::GpuInfo:
        case Command::Open:
        case Command::OpenUri:
        case Command::SaveLog:
        case Command::SaveSelectedLines:
        case Command::Format:
        case Command::LogPane:
        case Command::InfoWindow:
        case Command::Grid:
        case Command::Rulers:
        case Command::GridWindow:
        case Command::Plane1:
        case Command::Plane2:
        case Command::Plane3:
        case Command::HiddenChromaZero:
        case Command::Alpha:
        case Command::Settings:
        case Command::About: return;
    }
}

void MainFrame::showSettings() {
    if (!settings_) {
        settings_ = std::make_unique<SettingsWindow>(*this);
    }
    showSettingsWindow(*settings_, *this);  // the selftest's hook, if any
}

FloatingWindow* MainFrame::floatingInFront() const {
    // As settingsInFront(): wxOSX FindFocus() can be null while the window is the key window.
    wxWindow* const focus = focusedWindow();
    for (const wxWeakRef<FloatingWindow>& window : floating_) {
        FloatingWindow* const floating = window.get();
        if (floating == nullptr) {
            continue;
        }
        const bool focused = focus != nullptr && wxGetTopLevelParent(focus) == floating;
        if (focused || (floating->IsShown() && floating->IsActive())) {
            return floating;
        }
    }
    return nullptr;
}

void MainFrame::showGpuInfo() {
    std::string text = "no GPU device is in use";
    if (const std::shared_ptr<dvv::gpu::ExecutionContext> context = context_.lock()) {
        // As dvv-gpu-info --json prints it. The strong reference ends with this block (GUI thread,
        // §5.3 release rule).
        text = dvv::json::dump(dvv::gpu::toJson(context->deviceInfo()), 2);
    }
    if (App::arguments().selftest != Scenario::None) {
        DVV_LOG_INFO("GPU info:\n{}", text);  // no modal dialog in scripts
        return;
    }
    wxDialog dialog(this, wxID_ANY, "GPU info", wxDefaultPosition, wxDefaultSize,
                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* field = new wxTextCtrl(&dialog, wxID_ANY, wxString::FromUTF8(text), wxDefaultPosition,
                                 dialog.FromDIP(wxSize(520, 400)),
                                 wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    sizer->Add(field, wxSizerFlags(1).Expand().Border());
    sizer->Add(dialog.CreateStdDialogButtonSizer(wxOK), wxSizerFlags().Expand().Border());
    dialog.SetSizerAndFit(sizer);
    dialog.ShowModal();
}

VulkanStatus MainFrame::vulkanStatus() const {
    if (const std::shared_ptr<dvv::gpu::ExecutionContext> context = context_.lock()) {
        // The strong reference ends with this block (GUI thread, §5.3 release rule).
        const dvv::gpu::GpuInfo& device = context->deviceInfo();
        return {.available = true,
                .device = device.name,
                .api = dvv::gpu::formatVulkanVersion(device.apiVersion),
                .driver = vulkanDriverText(device.driverName, device.driverInfo),
                .reason = {}};
    }
    return {.available = false,
            .device = {},
            .api = {},
            .driver = {},
            .reason = "no GPU device is in use"};
}

void MainFrame::noteAbout(std::string text) {
    last_about_text_ = std::move(text);
    ++about_shown_;
}

void MainFrame::showOpenDialog() {
    wxFileDialog dialog(this, "Open a raw video file", dialogFolder(last_dir_), wxEmptyString,
                        OPEN_WILDCARD, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK || closing_) {
        logOpenCancelled();
        return;
    }
    openFiles({dvv::pathFromText(dialog.GetPath().utf8_string())});
}

void MainFrame::showOpenUriDialog() {
    // M4 GUI D7: the last typed URI (the example only on first use) in a wide multi-line field,
    // so a long URI wraps and stays visible; Enter's line breaks are dropped.
    const std::string_view value =
        last_typed_uri_.empty() ? OPEN_URI_EXAMPLE : std::string_view(last_typed_uri_);
    wxTextEntryDialog dialog(this, "Source URI:", "Open URI", wxString::FromUTF8(value),
                             wxTextEntryDialogStyle | wxTE_MULTILINE);
    dialog.SetClientSize(FromDIP(wxSize(740, 250)));  // a field of about 720x160 DIP
    dialog.CentreOnParent();
    if (showModal(dialog) != wxID_OK || closing_) {
        logOpenCancelled();
        return;
    }
    last_typed_uri_ = cleanUriText(dialog.GetValue().utf8_string());
    openUri(0, last_typed_uri_, std::nullopt, typedRecentEntry(last_typed_uri_));
}

void MainFrame::logOpenCancelled() const {
    if (!closing_) {  // every cancel leaves one Log line (the format dialog names the URI)
        DVV_LOG_INFO("open cancelled");
    }
}

void MainFrame::showStreamFormat() {
    if (format_dialog_ != nullptr || streams_.empty()) {
        return;
    }
    const StreamInfo stream = streams_.front();
    // D28: a lavfi: stream sets its colour in the graph; nothing is reopened.
    if (const std::optional<std::string> refusal = formatRefusal(stream.resolvedUri)) {
        DVV_LOG_TEXT(dvv::LogLevel::Info, *refusal);
        if (!reopenShown()) {  // the Reopen offer of a loss stays (the log has the refusal)
            showMessage(wxString::FromUTF8(*refusal), wxICON_INFORMATION);
        }
        return;
    }
    FormatDialog dialog(this, formatDialogTitle(stream.resolvedUri), choiceFromInfo(stream.info),
                        {}, dialogFolder(uriFolder(stream.resolvedUri)),
                        streamContext(stream, file_colours_, auto_detect_));
    if (runFormatDialog(dialog) == wxID_OK) {
        // D21, design v2 §4.3: a new origin at the kept position.
        const std::int64_t position = session().status().player.position;
        openWithChoice(stream.slot, stream.resolvedUri, dialog.choice(),
                       position >= 0 ? std::optional(position) : std::nullopt);
    } else {
        // View zoom design §3.3: a cancelled (or closing) dialog sizes nothing.
        sizing_uri_.clear();
    }
    showFormatDialogs();  // formatNeeded events that arrived meanwhile
}

void MainFrame::showErrorFormat() {
    // The failed open closed the stream, so View -> Format... has nothing to show (D4b).
    const std::string uri = std::exchange(format_error_uri_, {});
    std::optional<std::string> entry = std::exchange(format_error_entry_, std::nullopt);
    if (format_dialog_ != nullptr || uri.empty() || closing_) {
        return;
    }
    // The failed file's own name and query first, then the last choice.
    FormatDialog dialog(
        this, formatDialogTitle(uri),
        choiceFromUri(uri, last_choice_.value_or(FormatChoice{.format = "yuv420p"})), {},
        dialogFolder(uriFolder(uri)), openContext(uri, openDetection(uri, {}, {}), auto_detect_));
    if (runFormatDialog(dialog) == wxID_OK) {
        const FormatChoice choice = dialog.choice();
        last_choice_ = choice;
        // The raw dialog's choice goes to the raw provider (no Y4M or container
        // provider wins the reopen again).
        openWithChoice(0, withRawImpl(uri), choice, std::nullopt, std::move(entry));
    } else {
        // View zoom design §3.3: a cancelled (or closing) dialog sizes nothing.
        sizing_uri_.clear();
        if (!closing_) {
            DVV_LOG_INFO("open of {} cancelled", uri);
        }
    }
    showFormatDialogs();
}

int MainFrame::runFormatDialog(FormatDialog& dialog) {
    format_dialog_ = &dialog;
    ++format_dialogs_;
    const int result = showModal(dialog);
    format_dialog_ = nullptr;
    auto_detect_ = dialog.autoDetect();  // Auto detect: remembered for the session (OK or Cancel)
    return closing_ ? wxID_CANCEL : result;
}

void MainFrame::showFormatDialogs() {
    while (format_dialog_ == nullptr && !closing_ && !format_queue_.empty()) {
        const PendingFormat pending = std::move(format_queue_.front());
        format_queue_.pop_front();
        // Prefill (D21, D5 design §4.4): the URI's own fmt, size and fps (and the file name's),
        // else the best candidate, else the last accepted choice, else yuv420p 1080p25; the
        // colour from the URI's colour keys (none: entry 0).
        FormatChoice fallback = last_choice_.value_or(FormatChoice{.format = "yuv420p"});
        if (!pending.candidates.candidates.empty()) {
            fallback = choiceFromCandidate(pending.candidates.candidates.front());
        }
        const std::string uri = dvv::formatUri(pending.request.uri);
        FormatDialog dialog(
            this, formatDialogTitle(uri), choiceFromUri(uri, fallback),
            pending.candidates.candidates, dialogFolder(uriFolder(uri)),
            openContext(uri, openDetection(uri, pending.candidates.candidates, pending.reason),
                        auto_detect_));
        if (runFormatDialog(dialog) != wxID_OK) {
            // View zoom design §3.3: a cancelled (or closing) dialog sizes nothing.
            sizing_uri_.clear();
            if (!closing_) {
                DVV_LOG_INFO("open of {} cancelled", uri);
            }
            continue;
        }
        const FormatChoice choice = dialog.choice();
        last_choice_ = choice;
        openWithChoice(pending.slot, uri, choice, std::nullopt, pending.entry);
    }
}

void MainFrame::openFiles(std::vector<std::filesystem::path> files) {
    if (closing_) {
        return;
    }
    const DropPlan plan = routeDrop(files);
    if (plan.uri) {
        // Frame 0 (design v2 §4.3); the first file's Open Recent entry (spec §4).
        openUri(0, *plan.uri, std::nullopt, recentEntryOfPath(files.front()));
    }
    if (plan.ignored > 0) {
        DVV_LOG_INFO("{} files ignored: one stream at a time", plan.ignored);
    }
}

void MainFrame::openUri(std::uint32_t slot, const std::string& text,
                        std::optional<std::int64_t> start, std::optional<std::string> entry) {
    // M4 GUI D3: every open (Open, Open URI, drop, command line, View -> Format) of a URI that
    // parses first closes the current stream. The Session keeps replace-on-success. A text that
    // does not parse only warns and keeps the stream (and a loss's Reopen target).
    dvv::Result<dvv::Uri> uri = dvv::parseUri(text);
    if (!uri) {
        showWarning(uri.error().describe());
        return;
    }
    if (Session* session = closing_ ? nullptr : wxGetApp().session(); session != nullptr) {
        // Open Recent spec §5: this open's entry is the pending one until its stream shows.
        recent_pending_.opened(std::move(entry), dvv::formatUri(*uri));
        // View zoom design §3.3: this open's stream sizes the window; it starts at Fit, so a new
        // clip of the old size never shows the old zoom.
        sizing_uri_ = dvv::formatUri(*uri);
        viewport_->setView({});
        session->closeStreams();
        last_open_uri_ = text;
        // M4 GUI D6: File -> Open starts in the folder of the last opened file.
        if (std::filesystem::path folder = uriFolder(text); !folder.empty()) {
            std::error_code ignored;
            if (std::filesystem::is_directory(folder, ignored)) {
                last_dir_ = std::move(folder);
            }
        }
        session->open(slot, dvv::media::SourceRequest{.uri = std::move(*uri), .hint = {}}, start);
    }
}

void MainFrame::openWithChoice(std::uint32_t slot, const std::string& uri,
                               const FormatChoice& choice, std::optional<std::int64_t> start,
                               std::optional<std::string> entry) {
    const dvv::Result<std::string> applied = applyChoice(uri, choice);
    if (!applied) {
        showWarning(applied.error().describe());
        return;
    }
    openUri(slot, *applied, start, std::move(entry));
}

void MainFrame::showWarning(const std::string& text) {
    DVV_LOG_TEXT(dvv::LogLevel::Warn, text);
    showMessage(wxString::FromUTF8(text), wxICON_WARNING);
}

void MainFrame::onCharHook(wxKeyEvent& event) {
    if (closing_) {
        event.Skip();
        return;
    }
    const KeyChord chord =
        normaliseKey(event.GetKeyCode(), event.ControlDown(), event.ShiftDown(), event.AltDown());
    // An editable text entry and the Log list keep bare keys (D17; Log pane and Settings design
    // §4.1).
    const bool keeps_keys =
        focusKeepsKeys(focusedWindow(), log_ != nullptr ? log_->list().control() : nullptr);
    if (const std::optional<Command> command = commandForKey(chord, keeps_keys)) {
        execute(*command, CommandSource::Key);
        return;  // consumed: the slider and the buttons never see bare keys
    }
    event.Skip();
}

// The bar's activation: a skim starts only while active, and deactivation returns from it.
void MainFrame::onActivate(wxActivateEvent& event) {
    if (!closing_) {
        transportBar().frameActivated(event.GetActive());
    }
    event.Skip();
}

void MainFrame::onSkimKeyUp() {
    if (!closing_ && player_ != nullptr) {
        transportBar().skimKeyReleased();
    }
}

TransportBar& MainFrame::transportBar() const noexcept {
    return player_->transportBar();
}

wxWindow* MainFrame::focusedWindow() const {
    if (wxWindow* window = focus_for_test_.get(); window != nullptr) {
        return window;
    }
    return wxWindow::FindFocus();
}

void MainFrame::setFocusForTest(wxWindow* window) {
    focus_for_test_ = window;
}

void MainFrame::setDebugTransport(bool on) noexcept {
    debug_transport_ = on;
    player_->transportBar().setTrace(on);
}

void MainFrame::showMessage(const wxString& text, int icon, std::span<const InfoButton> buttons) {
    wxInfoBarGeneric& bar = player_->infoBar();
    for (const int id : message_buttons_) {
        bar.RemoveButton(id);
    }
    message_buttons_.clear();
    for (const InfoButton& button : buttons) {
        bar.AddButton(button.id, button.label);
        message_buttons_.push_back(button.id);
    }
    bar.ShowMessage(text, icon);
    last_message_ = text;
    reopen_offered_ = false;
    format_error_uri_.clear();
    format_error_entry_.reset();
    // The bar lives inside the player panel, so no size event runs the check.
    checkLayoutLimits();
}

void MainFrame::dismissMessage() {
    player_->infoBar().Dismiss();
    reopen_offered_ = false;
    format_error_uri_.clear();
    format_error_entry_.reset();
    checkLayoutLimits();
}

void MainFrame::offerReopen(const wxString& text) {
    // M4 GUI D10: the button names what it does; the command id stays "reopen".
    const std::array<InfoButton, 1> buttons{{{.id = INFOBAR_REOPEN_ID, .label = "Restart GPU"}}};
    showMessage(text, wxICON_ERROR, buttons);
    reopen_offered_ = true;
}

bool MainFrame::reopenShown() const {
    return reopen_offered_ && player_->infoBar().IsShown();
}

std::array<wxString, MainFrame::STATUS_FIELD_COUNT> MainFrame::statusTexts() const {
    return status_texts_;
}

void MainFrame::setLogShown(bool shown) {
    if (closing_ || log_ == nullptr) {
        return;
    }
    wxAuiPaneInfo& pane = aui_.GetPane(log_);
    if (!pane.IsOk() || pane.IsShown() == shown) {
        return;  // e.g. Help -> Diagnostics with the pane shown: the list keeps its place
    }
    pane.Show(shown);
    aui_.Update();  // the player panel's size event checks the layout limits
    if (!shown) {
        moveFocusOutOfLog();
    }
    if (shown) {
        // Spec §3.2: the list did not follow while hidden; it is rebuilt from the mirror.
        onLogChanged();
        log_->list().rebuild();
        seen_ = log_counts_;
        updateLogNotice();
    }
}

bool MainFrame::logShown() const {
    for (const wxAuiPaneInfo& pane : aui_.GetAllPanes()) {
        if (pane.window == log_) {
            return log_ != nullptr && pane.IsShown();
        }
    }
    return false;
}

void MainFrame::updateLogNotice() {
    // No logging (spec §3.3): onLogChanged calls this.
    const wxString notice = wxString::FromUTF8(
        logNoticeText(log_counts_.errors - seen_.errors, log_counts_.warnings - seen_.warnings));
    if (notice != status_texts_[LOG_NOTICE_FIELD]) {
        status_texts_[LOG_NOTICE_FIELD] = notice;
        SetStatusText(notice, static_cast<int>(LOG_NOTICE_FIELD));
    }
}

void MainFrame::onStatusBarLeftUp(wxMouseEvent& event) {
    event.Skip();
    if (statusFieldAt(event.GetPosition()) == static_cast<int>(LOG_NOTICE_FIELD)) {
        setLogShown(true);
    }
}

std::optional<int> MainFrame::statusFieldAt(const wxPoint& point) const {
    // As the Log notice click: wx's GetFieldFromPoint exists only in the generic bar.
    const wxStatusBar* const bar = GetStatusBar();
    if (bar == nullptr) {
        return std::nullopt;
    }
    for (int n = 0; n < bar->GetFieldsCount(); ++n) {
        wxRect rect;
        if (bar->GetFieldRect(n, rect) && rect.Contains(point)) {
            return n;
        }
    }
    return std::nullopt;
}

wxString MainFrame::statusTooltipFor(std::optional<int> field) const {
    const wxStatusBar* const bar = GetStatusBar();
    if (!field || bar == nullptr || *field < 0 ||
        static_cast<std::size_t>(*field) >= status_texts_.size()) {
        return {};
    }
    if (static_cast<std::size_t>(*field) == FILE_FIELD) {
        return stream_path_;
    }
    return bar->GetField(*field).IsEllipsized() ? status_texts_[static_cast<std::size_t>(*field)]
                                                : wxString();
}

void MainFrame::onStatusBarMotion(wxMouseEvent& event) {
    event.Skip();
    wxStatusBar* const bar = GetStatusBar();
    if (bar == nullptr) {
        return;
    }
    const std::optional<int> field = statusFieldAt(event.GetPosition());
    const wxString tip = statusTooltipFor(field);
    if (field != tip_field_) {
        // Unset first: the next field's tooltip waits for the OS's hover delay again.
        bar->UnsetToolTip();
        tip_text_.clear();
    }
    if (tip != tip_text_) {
        if (tip.empty()) {
            bar->UnsetToolTip();
        } else {
            bar->SetToolTip(tip);
        }
    }
    tip_field_ = field;
    tip_text_ = tip;
}

void MainFrame::showStreamName() {
    const std::string uri = streams_.empty() ? std::string() : streams_.front().resolvedUri;
    std::string path;
    bool file = false;
    if (!uri.empty()) {
        std::error_code error;
        const std::filesystem::path folder = std::filesystem::current_path(error);
        path = streamFullPath(uri, error ? std::string() : dvv::pathDisplayText(folder),
                              NATIVE_PATH_STYLE);
        const dvv::Result<dvv::Uri> parsed = dvv::parseUri(uri);
        file = parsed && parsed->scheme == "file";
    }
    const std::string name = uri.empty() ? std::string() : streamName(uri);
    stream_name_ = wxString::FromUTF8(name);
    stream_path_ = wxString::FromUTF8(path);
    SetTitle(wxString::FromUTF8(windowTitle(name, NATIVE_TITLE_STYLE)));
    // A no-op except on wxOSX (spec §5): the proxy icon and the path menu of the title.
    SetRepresentedFilename(file ? stream_path_ : wxString());
    cutFileField();
}

void MainFrame::cutFileField() {
    wxStatusBar* const bar = GetStatusBar();
    wxRect rect;
    if (bar == nullptr || !bar->GetFieldRect(static_cast<int>(FILE_FIELD), rect)) {
        return;
    }
    wxClientDC dc(bar);
    const wxString shown =
        stream_name_.empty()
            ? wxString()
            : wxControl::Ellipsize(stream_name_, dc, wxELLIPSIZE_MIDDLE, rect.width - FromDIP(8));
    if (shown != status_texts_[FILE_FIELD]) {
        status_texts_[FILE_FIELD] = shown;
        SetStatusText(shown, static_cast<int>(FILE_FIELD));
    }
}

void MainFrame::onStackChanged(const std::weak_ptr<dvv::gpu::ExecutionContext>& context,
                               std::uint64_t epoch) {
    if (closing_) {
        return;
    }
    context_ = context;
    stack_lost_ = context.expired();
    if (const std::shared_ptr<dvv::gpu::ExecutionContext> live = context.lock()) {
        // The device actually in use, once per stack (the log of a report names it).
        const dvv::gpu::GpuInfo& device = live->deviceInfo();
        DVV_LOG_INFO("Vulkan device: {}, driver {}", device.name,
                     vulkanDriverText(device.driverName, device.driverInfo));
    }
    if (!stack_lost_ && reopen_offered_) {
        dismissMessage();  // e.g. a File -> Open after a loss built a new stack
    }
    // After the dismissal: setStack may offer Reopen at once (a failed presenter start, D25).
    viewport_->setStack(context, epoch);
}

void MainFrame::onStreamsChanged(const std::vector<StreamInfo>& streams) {
    if (closing_) {
        return;
    }
    // M4 GUI D2: a new stream set (an open, Close stream) ends the old message, e.g. an open
    // error; a Reopen offer stays (a loss). Before the warnings below, so the new stream's show.
    if (!reopenShown() && player_->infoBar().IsShown()) {
        dismissMessage();
    }
    streams_ = streams;
    updateAlphaControls();  // another clip keeps the state; the button follows its format
    updatePlaneControls();  // the planes too, by position, with the new clip's letters
    // The view's frame size; the renderer resolves the view against the frame it shows.
    if (streams_.empty()) {
        viewport_->setImageSize({});
    } else {
        const dvv::media::FrameDesc& desc = streams_.front().info.desc;
        viewport_->setImageSize({.width = desc.width, .height = desc.height});
    }
    if (auto* const window = dynamic_cast<GridWindow*>(floatingWindow(FloatingKind::GridAndRulers));
        window != nullptr) {
        window->showImageWidth(clipWidth(*viewport_));
    }
    for (const StreamInfo& stream : streams_) {
        file_colours_.remember(stream);  // the file's colour for View > Format
        DVV_LOG_INFO("stream {}: {}", stream.slot, stream.resolvedUri);
        // What the provider noted (tag conflicts, a non-standard colour, the
        // clip description in use), on open and reopen.
        for (const std::string& note : stream.info.notes) {
            DVV_LOG_INFO("stream {}: {}", stream.slot, note);
        }
    }
    // Open Recent spec §5: the stream of the pending open records its entry.
    if (const std::optional<std::string> entry = recent_pending_.streamsChanged(streams_)) {
        recordRecent(*entry);
    }
    // D5R §2.2: the mapping field changes with the stream (open, reopen, Format, close), not on
    // the status tick.
    status_texts_[MAPPING_FIELD] = mappingField(streams_);
    SetStatusText(status_texts_[MAPPING_FIELD], static_cast<int>(MAPPING_FIELD));
    showStreamName();  // spec §5, §7: the title and the file field change with it
    if (auto* const info = dynamic_cast<InfoWindow*>(floatingWindow(FloatingKind::Info));
        info != nullptr) {
        info->showFor(streams_.empty() ? nullptr : &streams_.front());
    }
    // §13: the "i" tooltip's second line follows the stream.
    transportBar().setInfoHint(streams_.empty() ? "" : infoHint(streams_.front()));
    const std::array<InfoButton, 1> format_button{
        {{.id = INFOBAR_FORMAT_ID, .label = "Format..."}}};
    for (const StreamInfo& stream : streams_) {
        for (const std::string& warning : stream.info.warnings) {
            if (warning.find("trailing bytes ignored") != std::string::npos) {
                // §7: the size is probably wrong; the button opens View -> Format...
                showMessage(wxString::FromUTF8(streamName(stream.resolvedUri) + ": " + warning),
                            wxICON_INFORMATION, format_button);
            } else {
                showMessage(wxString::FromUTF8(stream.resolvedUri + ": " + warning),
                            wxICON_INFORMATION);
            }
        }
    }
    // View zoom design §3.3: the stream of the pending open sizes the window, last, so the window
    // is measured with the infobar as it stays. An empty list (before every open's and every
    // Reopen's streams) leaves the pending open.
    if (!streams_.empty() && !sizing_uri_.empty() && streams_.front().requestedUri == sizing_uri_) {
        sizing_uri_.clear();
        sizeWindowForOpen();
    }
}

void MainFrame::onFormatNeeded(std::uint32_t slot, const dvv::media::SourceRequest& request,
                               const dvv::media::Ambiguous& candidates, const std::string& reason) {
    if (closing_) {
        return;
    }
    // D5R §4.4: the Log line says what the dialog's detection line says.
    const std::string uri = dvv::formatUri(request.uri);
    DVV_LOG_INFO("stream {}: {} needs a format ({} candidates): {}", slot, uri,
                 candidates.candidates.size(),
                 openDetection(uri, candidates.candidates, reason).text);
    // Design v2 §4.6: a container file without FFmpeg is any unknown file (the M3 D21 dialog),
    // plus the FFmpeg status in the log.
    if (const dvv::ModuleStatus* const ffmpeg =
            wxGetApp().startup().components->module(dvv::components::FFMPEG_MODULE);
        ffmpeg == nullptr ||
        std::ranges::contains(ffmpeg->withheld, dvv::components::FFMPEG_CAPABILITY)) {
        DVV_LOG_TEXT(dvv::LogLevel::Info, ffmpegStatusLine(ffmpeg));
    }
    // D21: one dialog at a time; the next one after the current one closes.
    // Open Recent spec §5: the dialog's OK opens with the entry of the open that asked.
    format_queue_.push_back({.slot = slot,
                             .request = request,
                             .candidates = candidates,
                             .reason = reason,
                             .entry = recent_pending_.formatNeeded(uri)});
    showFormatDialogs();
}

void MainFrame::onError(const dvv::Error& error) {
    if (closing_) {
        return;
    }
    // Design v2 §4.6: the FFmpeg status is appended when FFmpeg withholds what an unavailable
    // Source provider for the request's scheme misses. The log keeps the full status line; the
    // infobar shows no directory or path.
    const Startup& startup = wxGetApp().startup();
    // M4 GUI D7 C: a scheme-less text that failed as a missing file was probably meant as a URI.
    // The hint and the Format... button are for open errors only; an open closes the
    // stream first (D3), so an error while a stream is shown is a playback error.
    const bool open_error = streams_.empty();
    if (open_error) {
        sizing_uri_.clear();  // view zoom design §3.3: the failed open sizes nothing
    }
    const std::string_view typed = open_error ? std::string_view(last_open_uri_) : "";
    // The scheme of the failed open (openUri stores only texts that parse); empty otherwise.
    const dvv::Result<dvv::Uri> typed_uri = dvv::parseUri(typed);
    const std::string_view scheme = typed_uri ? std::string_view(typed_uri->scheme) : "";
    DVV_LOG_TEXT(dvv::LogLevel::Warn,
                 withSchemeHint(openErrorText(error, scheme, startup), typed, error));
    const std::string text =
        withSchemeHint(openErrorText(error, scheme, startup, StatusPaths::Hidden), typed, error);
    // A loss, or a failed makeStack while no stack is installed (§7 "infobar keeps Reopen").
    // Session events arrive in the Session's order and stackChanged({}, 0) comes before
    // error(DeviceLost), so stack_lost_ is current.
    if (error.code == dvv::Errc::DeviceLost || stack_lost_) {
        offerReopen(wxString::FromUTF8(text));
    } else if (open_error && offersFormat(last_open_uri_, error)) {
        // D4b: a raw file whose format, size or length does not fit; the button opens the
        // format dialog for it (the failed open left no stream for View -> Format...).
        const std::array<InfoButton, 1> format_button{
            {{.id = INFOBAR_FORMAT_ID, .label = "Format..."}}};
        showMessage(wxString::FromUTF8(text), wxICON_WARNING, format_button);
        format_error_uri_ = last_open_uri_;
        format_error_entry_ = recent_pending_.entry();  // Open Recent spec §5: for the reopen
    } else {
        showMessage(wxString::FromUTF8(text), wxICON_WARNING);
    }
    // Open Recent spec §6: a listed entry whose open failed with Errc::Io gets one look at its
    // file, on the io executor (a dead network share can block it); recentChecked() acts on it.
    if (open_error && error.code == dvv::Errc::Io) {
        App& app = wxGetApp();
        std::optional<std::string> entry = recent_pending_.entry();
        if (entry && app.recent().contains(*entry) && app.io()) {
            app.io()->post([entry = std::move(*entry)]() mutable {
                const RecentFileState state = recentFileState(entry);
                wxGetApp().CallAfter([entry = std::move(entry), state] {
                    if (MainFrame* frame = wxGetApp().mainFrame(); frame != nullptr) {
                        frame->recentChecked(entry, state);
                    }
                });
            });
        }
    }
}

void MainFrame::recentChecked(const std::string& entry, RecentFileState state) {
    if (closing_) {
        return;
    }
    std::string_view reason;
    switch (state) {
        case RecentFileState::Missing: reason = "missing"; break;
        case RecentFileState::NotAccessible: reason = "not accessible"; break;
        case RecentFileState::Readable:
        case RecentFileState::NotChecked: return;  // the entry stays
    }
    App& app = wxGetApp();
    if (!app.recent().remove(entry)) {
        return;  // no longer listed
    }
    const std::array<ConfigValue, 1> values{
        {{.key = config_key::GUI_RECENT, .value = app.recent().entries()}}};
    app.saveHistory(values);  // a failure is logged there; the list stays in effect
    recentChanged();
    DVV_LOG_INFO("\"{}\" removed from Open Recent: {}", entry, reason);
}

void MainFrame::onPresenterFailed(const dvv::Error& error) {
    if (closing_) {
        return;
    }
    // D25: the viewport logged it; the user's Reopen rebuilds the stack.
    offerReopen(wxString::FromUTF8(error.describe()));
}

void MainFrame::onMenu(wxCommandEvent& event) {
    if (const std::optional<Command> command = commandOfMenuId(event.GetId())) {
        execute(*command, CommandSource::Menu);
    }
}

void MainFrame::onUpdateUi(wxUpdateUIEvent& event) {
    const std::optional<Command> command = commandOfMenuId(event.GetId());
    event.Enable(!closing_ && command && isAvailable(*command));
    if (!command) {
        return;
    }
    switch (commandInfo(*command).check) {
        case MenuCheck::None: break;
        case MenuCheck::Shown: {
            // A show-or-hide item follows its part however it changed. Only the menu bar's items
            // change their title; a popup menu's (the picture's context menu) stay check items on
            // every OS.
            const auto* const menu = dynamic_cast<const wxMenu*>(event.GetEventObject());
            const bool in_menu_bar = menu == nullptr || menu->GetMenuBar() != nullptr;
            if (native_policy::shownItemHasChangingTitle() && in_menu_bar) {
                event.SetText(
                    wxString::FromUTF8(shownItemTitle(commandInfo(*command), menuState(*command))));
            } else {
                event.Check(menuState(*command));
            }
            break;
        }
        // A mode item is a check item everywhere; disabled, it keeps showing the kept state. A
        // plane item's title follows the clip's letters (Y U V, R G B or G B R). It is set on every
        // update, as the Shown titles are: the event does not carry the item's current title, and
        // wxMenuBase::UpdateUI sets any text given without comparing. The title has no "\t", so
        // wxOSX registers no key equivalent again.
        case MenuCheck::Mode:
            if (const std::optional<std::size_t> plane = planeIndex(*command)) {
                event.SetText(wxString::FromUTF8(
                    menuLabel(commandInfo(*command), planeTitle(planeLetters().at(*plane)))));
            }
            event.Check(menuState(*command));
            break;
    }
}

// Every command is listed (no default), so a new one gets its case here: its state when the
// table gives it a check item, or the plain list below.
bool MainFrame::menuState(Command command) const {
    switch (command) {
        case Command::LogPane: return logShown();
        case Command::InfoWindow: return windowShown(FloatingKind::Info);
        case Command::Grid: return grid_.levels[0].show;
        case Command::Rulers: return rulers_.show;
        case Command::Plane1: return planes_[0];
        case Command::Plane2: return planes_[1];
        case Command::Plane3: return planes_[2];
        case Command::HiddenChromaZero: return chroma_zero_;
        case Command::Alpha: return alpha_;
        case Command::Open:
        case Command::OpenUri:
        case Command::SaveLog:
        case Command::SaveSelectedLines:
        case Command::CloseStreams:
        case Command::Settings:
        case Command::Quit:
        case Command::PlayPause:
        case Command::Stop:
        case Command::StepBack:
        case Command::StepForward:
        case Command::StepBack10:
        case Command::StepForward10:
        case Command::First:
        case Command::Last:
        case Command::Loop:
        case Command::FitToWindow:
        case Command::ActualSize:
        case Command::ZoomIn:
        case Command::ZoomOut:
        case Command::ResizeWindowToActualSize:
        case Command::Format:
        case Command::Reopen:
        case Command::GridWindow:
        case Command::GpuInfo:
        case Command::Diagnostics:
        case Command::About: return false;  // MenuCheck::None
    }
    return false;
}

bool MainFrame::clipHasAlpha() const {
    if (streams_.empty()) {
        return false;
    }
    const dvv::media::PixelFormatDesc& desc =
        dvv::media::formatDesc(streams_.front().info.desc.format);
    const auto components = std::span(desc.components).first(desc.componentCount);
    return std::ranges::any_of(components, [](const dvv::media::ComponentDesc& component) {
        return component.kind == dvv::media::ComponentKind::A;
    });
}

std::array<char, 3> MainFrame::planeLetters() const {
    if (streams_.empty()) {
        return {'Y', 'U', 'V'};
    }
    const StreamInfo& stream = streams_.front();
    const dvv::media::FormatId format = stream.info.desc.format;
    return rawreel::planeLetters(dvv::media::formatDesc(format).model,
                                 dvv::media::hasChroma(format, stream.colour.params));
}

void MainFrame::updateAlphaControls() {
    transportBar().setAlpha(alpha_, isAvailable(Command::Alpha));
}

void MainFrame::updatePlaneControls() {
    const std::array<char, 3> letters = planeLetters();
    transportBar().setPlanes(
        planes_,
        {isAvailable(Command::Plane1), isAvailable(Command::Plane2), isAvailable(Command::Plane3)},
        letters);
    // The menu bar's plane items' help follows the letters (the title follows in onUpdateUi;
    // wxUpdateUIEvent carries no help).
    if (wxMenuBar* const bar = GetMenuBar()) {
        for (std::size_t k = 0; k < letters.size(); ++k) {
            const int id =
                menuId(static_cast<Command>(static_cast<std::size_t>(Command::Plane1) + k));
            const wxString help = wxString::FromUTF8(planeHelp(letters.at(k)));
            if (bar->FindItem(id) != nullptr && bar->GetHelpString(id) != help) {
                bar->SetHelpString(id, help);
            }
        }
    }
}

void MainFrame::popUpViewportMenu(wxPoint point) {
    if (closing_) {
        return;
    }
    wxMenu menu;
    // Check items on every OS: a popup's show-or-hide items keep a check mark on macOS too. The
    // plane items are titled as in the menu bar; the fill item is not here.
    const std::array<char, 3> letters = planeLetters();
    for (const Command command : {Command::Grid, Command::Rulers, Command::Plane1, Command::Plane2,
                                  Command::Plane3, Command::Alpha}) {
        const CommandInfo& info = commandInfo(command);
        const std::optional<std::size_t> plane = planeIndex(command);
        const std::string label =
            plane ? menuLabel(info, planeTitle(letters.at(*plane))) : menuLabel(info);
        const std::string help = plane ? planeHelp(letters.at(*plane)) : std::string(info.help);
        wxMenuItem* const item = menu.AppendCheckItem(menuId(command), wxString::FromUTF8(label),
                                                      wxString::FromUTF8(help));
        item->Check(menuState(command));
        item->Enable(isAvailable(command));
    }
    const CommandInfo& window = commandInfo(Command::GridWindow);
    menu.Append(menuId(Command::GridWindow), wxString::FromUTF8(menuLabel(window)),
                wxString::FromUTF8(window.help));
    const int chosen = showPopupMenu(*viewport_, menu, point);
    if (const std::optional<Command> command = commandOfMenuId(chosen)) {
        execute(*command, CommandSource::Menu);
    }
}

void MainFrame::onEditMenu(wxCommandEvent& event) {
    if (closing_) {
        return;
    }
    // Window title and Info design §10: Copy with the Info window in front copies its rows.
    if (event.GetId() == wxID_COPY && infoInFront(floatingInFront())) {
        setClipboardText(infoCopyText(streams_.empty() ? nullptr : &streams_.front()));
        return;
    }
    // The action is called on the focused control; the event is never sent again (spec §4.2).
    if (log_ != nullptr) {
        (void)runEdit(event.GetId(), focusedWindow(), *log_);
    }
}

void MainFrame::onEditUpdateUi(wxUpdateUIEvent& event) {
    if (!closing_ && event.GetId() == wxID_COPY && infoInFront(floatingInFront())) {
        event.Enable(true);  // §10 Copy: always, "No stream" too
        return;
    }
    event.Enable(!closing_ && log_ != nullptr && canEdit(event.GetId(), focusedWindow(), *log_));
}

void MainFrame::saveLog(bool selected) {
    // Spec §4.3: the records saved are the ones of the moment the command ran, even if new records
    // drop them from the mirror while the dialog is open.
    const std::vector<LogEntry> records = selected ? log_->selectedRecords() : log_->allRecords();
    const std::string text = logRecordsText(records);
    const wxString name =
        wxString::FromUTF8(logSaveFileName(localTime(std::chrono::system_clock::now())));
    wxFileDialog dialog(this, selected ? "Save Selected Lines" : "Save Log",
                        dialogFolder(last_log_save_dir_), name, LOG_SAVE_WILDCARD,
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (showModal(dialog) != wxID_OK || closing_) {
        return;
    }
    const std::filesystem::path file = dvv::pathFromText(chosenPath(dialog));
    const std::string path = dvv::pathDisplayText(file);
    const auto refuse = [this](const std::string& detail) {
        wxMessageDialog message(this, "The log was not saved.", "rawreel", wxOK | wxICON_ERROR);
        message.SetExtendedMessage(wxString::FromUTF8(detail));
        (void)showModal(message);
    };
    // Spec §4.4: the log files rawreel writes are never overwritten.
    const std::filesystem::path current = wxGetApp().currentLogFile();
    if (isLiveLogFile(file, current, wxGetApp().logFolderInUse())) {
        DVV_LOG_INFO("log not saved: '{}' is a log file rawreel writes", path);
        refuse(
            std::format("'{}' is a log file rawreel writes (the current one is '{}'). Choose "
                        "another file.",
                        path, dvv::pathDisplayText(current)));
        return;
    }
    // Windows: a brief open by another program (an indexer, a scanner) is waited out, 5 x 10 ms.
    constexpr dvv::platform::ReplaceFileOptions SAVE_LOG_WRITE{.retries = 5};
    if (const dvv::Result<void> saved =
            dvv::platform::replaceFile(file, std::as_bytes(std::span(text)), SAVE_LOG_WRITE);
        !saved) {
        DVV_LOG_WARN("log not saved: {}", saved.error().message);
        refuse(saved.error().message);
        return;
    }
    last_log_save_dir_ = file.parent_path();  // the next save dialog starts there
    DVV_LOG_INFO("{} log records saved to '{}'", records.size(), path);
}

void MainFrame::startStatusUpdates(Session& session) {
    status_subscription_ =
        session.subscribeStatus([this] { CallAfter([this] { onStatusChanged(); }); });
    // The snapshot published before the subscription existed.
    CallAfter([this] { onStatusChanged(); });
}

void MainFrame::stopStatusUpdates() {
    status_subscription_ = StatusSubscription{};
}

void MainFrame::onStatusChanged() {
    if (closing_ || !status_subscription_) {
        return;
    }
    if (debug_transport_) {
        const auto now = std::chrono::steady_clock::now();
        if (rate_second_ == std::chrono::steady_clock::time_point{}) {
            rate_second_ = now;
        } else if (now - rate_second_ >= std::chrono::seconds(1)) {
            DVV_LOG_INFO("transport: rate {} notifications/s", rate_count_);
            rate_count_ = 0;
            rate_second_ = now;
        }
        ++rate_count_;
    }
    const SessionStatus status = status_subscription_.take();
    if (status.revision == last_revision_) {
        return;
    }
    last_revision_ = status.revision;
    ++status_notifications_;
    if (debug_transport_) {
        DVV_LOG_INFO("transport: notification revision {} position {} target {} playing {}",
                     status.revision, status.player.position, status.player.target,
                     status.player.playing ? 1 : 0);
    }
    player_->transportBar().update(barStatus(status));
    // isAvailable depends on the session's stream state: the zoom controls, the Alpha button and
    // the plane toggles follow every status notification too (seven isAvailable calls per
    // notification).
    showZoom();
    updateAlphaControls();
    updatePlaneControls();
    const StatusFields fields = statusFields(status);
    const std::array<wxString, 3> texts{wxString::FromUTF8(fields.position),
                                        wxString::FromUTF8(fields.fps),
                                        wxString::FromUTF8(fields.bottleneck)};
    for (std::size_t i = 0; i < texts.size(); ++i) {
        if (texts[i] != status_texts_[FIRST_TICK_FIELD + i]) {
            status_texts_[FIRST_TICK_FIELD + i] = texts[i];
            SetStatusText(texts[i], static_cast<int>(FIRST_TICK_FIELD + i));
        }
    }
}

void MainFrame::onIconize(wxIconizeEvent& event) {
    if (viewport_ != nullptr) {
        viewport_->topLevelIconized(event.IsIconized());
    }
    floatingWindowsFollow(event.IsIconized());
    event.Skip();
}

void MainFrame::applyLayoutLimits() {
    // The minimum client size comes from the real controls at the current scale and font: the
    // viewport's 320x240 DIP, the transport bar, the infobar, the dock minimums and the sashes,
    // plus a small margin for wxAUI's rounding.
    const wxSize computed = aui_.minimumClientSize();
    applied_minimum_ = computed;  // checkLayoutLimits() compares the computed value
    wxSize minimum = computed + FromDIP(wxSize(LAYOUT_MARGIN_DIP, LAYOUT_MARGIN_DIP));
    // Spec §7: the status bar's fixed fields plus room for the variable ones, so that the file
    // field keeps 120 DIP and never shrinks to nothing.
    minimum.x = std::max(minimum.x, statusMinimumWidth());
    SetMinClientSize(minimum);
    // A dock change can raise the minimum above the client size (a pane docked as a new row), and
    // Windows applies a minimum only on the next resize: grow to it now.
    const wxSize client = GetClientSize();
    if (!IsIconized() && !IsMaximized() && !IsFullScreen() &&
        (client.x < minimum.x || client.y < minimum.y)) {
        SetClientSize(std::max(client.x, minimum.x), std::max(client.y, minimum.y));
    }
    aui_.fitDocks();
}

void MainFrame::checkLayoutLimits() {
    if (limits_check_pending_ || closing_) {
        return;
    }
    limits_check_pending_ = true;
    CallAfter([this] {  // after the Update() that sent the size event has finished
        limits_check_pending_ = false;
        if (closing_) {
            return;
        }
        bool stale = aui_.minimumClientSize() != applied_minimum_;
        // wxGTK keeps a minimum as a window size, converted with the decoration size it knew at
        // SetMinClientSize(); the menu bar, status bar and window decorations are known only after
        // the map, so the minimum client size can end up below the one applied: apply it again.
        // MSW and wxOSX know the decorations when the window is created (wxOSX's menu bar is not
        // in the window), so their minimum client size stays as applied.
        if (native_policy::reappliesMinimumAfterMap()) {
            wxSize wanted =
                applied_minimum_ + FromDIP(wxSize(LAYOUT_MARGIN_DIP, LAYOUT_MARGIN_DIP));
            wanted.x = std::max(wanted.x, statusMinimumWidth());
            const wxSize effective = GetMinClientSize();
            stale = stale || effective.x < wanted.x || effective.y < wanted.y;
        }
        if (stale) {
            applyLayoutLimits();
        }
    });
}

void MainFrame::onShow(wxShowEvent& event) {
    if (viewport_ != nullptr) {
        viewport_->topLevelShown(event.IsShown());
    }
#if defined(__WXGTK__)
    floatingWindowsFollow(!event.IsShown());  // X11 has no WM-independent iconify: Hide and Show
#endif
    if (event.IsShown() && !std::exchange(first_shown_, true)) {
        // wx gives the first focusable child the focus when the window appears, which can be a
        // panel of the Log pane (shown or hidden): after that, the player has it.
        CallAfter([this] { moveFocusOutOfLog(); });
    }
    event.Skip();
}

// Every show and hide of a floating window (the command, its close box, Esc, the keys) comes here;
// the "i" toggle follows the Info window.
void MainFrame::setWindowShown(FloatingKind kind, bool shown) {
    if (closing_) {
        return;
    }
    if (shown != windowShown(kind)) {
        wxWeakRef<FloatingWindow>& window = floating_[static_cast<std::size_t>(kind)];
        if (!shown) {
            if (auto* const grid_window = dynamic_cast<GridWindow*>(window.get())) {
                grid_window->endPending();  // before the save below
            }
            window->hideKeepingPlace();
            if (kind == FloatingKind::GridAndRulers) {
                setOverlay(grid_, rulers_, OverlaySave::Now);  // writes nothing when unchanged
            }
        } else {
            if (!window) {
                // Creating a floating window leaves wx's focus where it was: wxGTK's native book
                // gives the page it selects the focus, the first one too, and wx then names a
                // control of the hidden new window as the focus (FindFocus) until it is set again.
                // With no focus before, the viewport takes it, as a click on it does.
                wxWindow* const focus = wxWindow::FindFocus();
                switch (kind) {
                    case FloatingKind::Info: window = new InfoWindow(*this); break;
                    case FloatingKind::GridAndRulers: {
                        auto* const grid_window = new GridWindow(*this);
                        grid_window->showImageWidth(clipWidth(*viewport_));
                        window = grid_window;
                        break;
                    }
                }
                wxWindow* const now = wxWindow::FindFocus();
                wxWindow* const back = focus != nullptr ? focus : viewport_;
                if (now != nullptr && wxGetTopLevelParent(now) == window.get() && back != nullptr) {
                    back->SetFocus();
                }
            }
            window->showBesideOwner();
            if (kind == FloatingKind::GridAndRulers) {
                showGridNotes();
            }
        }
    }
    if (kind == FloatingKind::Info) {
        transportBar().setInfoShown(windowShown(kind));
    }
}

bool MainFrame::windowShown(FloatingKind kind) const {
    const FloatingWindow* const window = floatingWindow(kind);
    return window != nullptr && window->wanted();
}

void MainFrame::floatingWindowsFollow(bool frame_hidden) {
    if (closing_) {
        return;
    }
    for (const wxWeakRef<FloatingWindow>& window : floating_) {
        if (window) {
            window->followOwner(frame_hidden);
        }
    }
}

void MainFrame::moveFocusOutOfLog() {
    if (closing_ || log_ == nullptr || player_ == nullptr) {
        return;
    }
    if (const wxWindow* focus = wxWindow::FindFocus();
        focus != nullptr && windowWithin(focus, log_)) {
        player_->SetFocus();  // its first focusable child (the transport bar)
    }
}

void MainFrame::saveLogPaneState() {
    // Spec §2: written once, at close, only when the pane's state differs from its state at start
    // (an untouched pane writes nothing: no file is created, and a selftest that starts hidden
    // never overwrites a --config file's true) and only by the settings owner (saveSettings).
    // Closing: a failure is logged, never shown in a dialog.
    App& app = wxGetApp();
    const bool shown = logShown();
    if (shown == log_shown_at_start_) {
        return;
    }
    const std::array<ConfigValue, 1> values{
        {{.key = config_key::GUI_SHOW_LOG_PANE, .value = shown}}};
    if (app.saveSettings(values, /*closing=*/true)) {
        app.setSavedLogPane(shown);
    }
}

void MainFrame::saveOverlayState() {
    (void)wxGetApp().saveOverlay(grid_, rulers_, /*closing=*/true);  // a failure is only logged
}

void MainFrame::onClose(wxCloseEvent& event) {
    if (closing_) {
        event.Skip();
        return;
    }
    // Before closing_, which stops setOverlay: what the Grid and Rulers window has pending is
    // applied for the save below, and its quiet period can no longer run.
    if (auto* const grid_window =
            dynamic_cast<GridWindow*>(floatingWindow(FloatingKind::GridAndRulers))) {
        grid_window->endPending();
    }
    closing_ = true;
    saveLogPaneState();
    saveOverlayState();
    stopStatusUpdates();
    if (selftest_) {
        selftest_->stop();  // the runner lives until ~MainFrame
    }
    format_queue_.clear();
    if (format_dialog_ != nullptr && format_dialog_->IsModal()) {
        format_dialog_->EndModal(wxID_CANCEL);  // its caller sees closing_ and returns
    }
    if (settings_) {
        settings_->dismiss();  // spec §5.1: before the Session shuts down
    }
    for (wxWeakRef<FloatingWindow>& window : floating_) {
        if (window) {
            window->Destroy();  // §10: it closes with the main window, before the Session goes
            window = nullptr;
        }
    }
    if (viewport_ != nullptr) {
        viewport_->detachStack();  // setOnChanged({}), presenter, renderer, context
    }
    if (const std::shared_ptr<DisplayState>& display = wxGetApp().display()) {
        display->clear();  // drops the last FrameSet's leases on the GUI thread
    }
    wxGetApp().shutdownSession();  // bridge detached, syncWait(session->shutdown()), Session gone
    if (viewport_ != nullptr) {
        viewport_ = nullptr;
        player_->destroyViewport();  // a child's Destroy() deletes it now
    }
    event.Skip();  // wx destroys the frame
}

}  // namespace rawreel
