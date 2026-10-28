// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/main_frame.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
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

#include "core/drop.h"
#include "core/info_text.h"
#include "core/layout.h"
#include "core/open_text.h"
#include "core/session.h"
#include "core/startup.h"
#include "core/status_text.h"
#include "dvv/base/json.h"
#include "dvv/base/log.h"
#include "dvv/base/uri.h"
#include "dvv/components/ffmpeg.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/gpu_info_json.h"
#include "dvv/media/types.h"
#include "gui/about.h"
#include "gui/app.h"
#include "gui/drop_target.h"
#include "gui/format_dialog.h"
#include "gui/info_panel.h"
#include "gui/log_panel.h"
#include "gui/player_panel.h"
#include "gui/selftest.h"
#include "gui/transport_bar.h"
#include "gui/viewport.h"

namespace rawreel {

namespace {

constexpr int STATUS_TICK_MS = 50;  // D20
// The docks' smallest panes (M4 GUI D8), DIP: Info's width, Log's height without its caption
// (about three lines of the log).
constexpr int INFO_MIN_WIDTH_DIP = 160;
constexpr int LOG_MIN_HEIGHT_DIP = 60;
// Added to the computed minimum client size: wxAUI's rows can come out 1 px short of it (two
// bottom rows at 150 %), which must not put the viewport below 320x240 DIP.
constexpr int LAYOUT_MARGIN_DIP = 2;
constexpr int LAST_COMMAND_ID = COMMAND_ID_BASE + static_cast<int>(COMMAND_COUNT) - 1;
constexpr const char* OPEN_WILDCARD =
    "Raw video (*.yuv;*.raw;*.rgb;*.bin;*.y4m)|*.yuv;*.raw;*.rgb;*.bin;*.y4m|All files (*.*)|*.*";
constexpr std::string_view OPEN_URI_EXAMPLE =
    "synthetic:gradient?fmt=yuv420p&size=1920x1080&frames=250";  // X15

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

// The file name of a file: URI (the infobar's short form); the URI itself otherwise.
std::string fileName(const std::string& uri) {
    const dvv::Result<dvv::Uri> parsed = dvv::parseUri(uri);
    if (!parsed || parsed->scheme != "file" || parsed->path.empty()) {
        return uri;
    }
    return dvv::media::utf8FromPath(dvv::media::pathFromUtf8(parsed->path).filename());
}

// A file dialog's start folder (M4 GUI D6); empty lets the system choose.
wxString dialogFolder(const std::filesystem::path& folder) {
    return wxString::FromUTF8(dvv::media::utf8FromPath(folder));
}

// View > Format's context (D5 design §1.8, D5R §4.4): what applies without a colour key
// (FileColourMemory::colourBase: the stream's standard, or the remembered one of the file when
// the stream was opened with colour keys).
FormatDialogContext streamContext(const StreamInfo& stream, const FileColourMemory& memory,
                                  bool auto_detect) {
    // D5 design §1.6: a Y4M header or a container gives format, size and fps; raw and synthetic
    // streams lock nothing.
    const bool locked = stream.info.provider == "y4m" || stream.info.provider == "ffmpeg";
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

// D5R §2.2: the 4th status field, the display mapping of the active (first) stream.
wxString mappingField(const std::vector<StreamInfo>& streams) {
    return wxString::FromUTF8(
        mappingText(streams.empty() ? nullptr : &streams.front().colour.params));
}

}  // namespace

MainFrame::MainFrame(std::shared_ptr<DisplayState> display)
    : wxFrame(nullptr, wxID_ANY, "rawreel"),
      aui_([this] {
          // A size event before the player panel exists: the viewport minimum.
          return player_ != nullptr
                     ? player_->minimumSize()
                     : FromDIP(wxSize(MIN_VIEWPORT_DIP.width, MIN_VIEWPORT_DIP.height));
      }),
      status_timer_(this) {
    aui_.manage(this);
    player_ = new PlayerPanel(
        this, std::move(display),
        [this](Command command) { execute(command, CommandSource::Button); },
        [this](std::int64_t index) {
            if (Session* session = wxGetApp().session(); session != nullptr && !closing_) {
                session->seek(index);
            }
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
    info_ = new InfoPanel(this);
    info_->setText(formatInfo({}));  // "No stream. ..." until the first streamsChanged
    log_ = new LogPanel(this, wxGetApp().logSink());
    // The centre pane never floats, so the viewport's native child window is never re-parented
    // (M1b F14); no border, so the presented extent equals the pane.
    aui_.AddPane(player_, wxAuiPaneInfo().Name("player").CenterPane().PaneBorder(false));
    aui_.AddPane(info_, wxAuiPaneInfo()
                            .Name("info")
                            .Caption("Info")
                            .Right()
                            .BestSize(FromDIP(wxSize(320, 400)))
                            .MinSize(FromDIP(wxSize(INFO_MIN_WIDTH_DIP, -1)))
                            .CloseButton(false));
    aui_.AddPane(log_, wxAuiPaneInfo()
                           .Name("log")
                           .Caption("Log")
                           .Bottom()
                           .BestSize(FromDIP(wxSize(800, 160)))
                           .MinSize(FromDIP(wxSize(-1, LOG_MIN_HEIGHT_DIP)))
                           .CloseButton(false));
    aui_.Update();
    buildMenus();
    buildStatusBar();
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
            std::string error_uri = format_error_uri_;  // dismissMessage() clears it
            dismissMessage();
            if (!error_uri.empty()) {
                format_error_uri_ = std::move(error_uri);
                showErrorFormat();  // an open error of a raw file (D4b): no stream to format
            } else {
                execute(Command::Format, CommandSource::Button);
            }
        },
        INFOBAR_FORMAT_ID);
    Bind(wxEVT_CHAR_HOOK, &MainFrame::onCharHook, this);
    Bind(wxEVT_ICONIZE, &MainFrame::onIconize, this);
    Bind(wxEVT_SHOW, &MainFrame::onShow, this);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::onClose, this);
}

MainFrame::~MainFrame() {
    status_timer_.Stop();
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
        menu->Append(COMMAND_ID_BASE + static_cast<int>(info.command),
                     wxString::FromUTF8(menuLabel(info)), wxString::FromUTF8(info.help));
    }
    SetMenuBar(bar);
    Bind(wxEVT_MENU, &MainFrame::onMenu, this, COMMAND_ID_BASE, LAST_COMMAND_ID);
    Bind(wxEVT_UPDATE_UI, &MainFrame::onUpdateUi, this, COMMAND_ID_BASE, LAST_COMMAND_ID);
}

void MainFrame::buildStatusBar() {
    CreateStatusBar(static_cast<int>(status_texts_.size()));
    // D5R §2.2: field 3 (the display mapping) is as wide as its longest text plus a margin.
    wxClientDC dc(GetStatusBar());
    int mapping = 0;
    for (const dvv::media::TransferFn curve :
         {dvv::media::TransferFn::Pq, dvv::media::TransferFn::Hlg,
          dvv::media::TransferFn::Bt1886}) {
        dvv::media::ColourParams sample{};
        sample.transfer = curve;
        mapping = std::max(mapping, dc.GetTextExtent(wxString::FromUTF8(mappingText(&sample))).x);
    }
    mapping += FromDIP(24);
    const std::array<int, 4> widths{-3, -2, -2, mapping};
    SetStatusWidths(static_cast<int>(widths.size()), widths.data());
    Bind(wxEVT_TIMER, &MainFrame::onStatusTimer, this, status_timer_.GetId());
    status_timer_.Start(STATUS_TICK_MS);
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
        case Command::Diagnostics:
        case Command::About: return true;
        case Command::CloseStreams:
        case Command::PlayPause:
        case Command::Stop:
        case Command::StepBack:
        case Command::StepForward:
        case Command::StepBack10:
        case Command::StepForward10:
        case Command::First:
        case Command::Loop: return hasStream();
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
        case Command::Format: return hasStream() && !streams_.empty();
    }
    return false;
}

void MainFrame::execute(Command command, CommandSource source) {
    if (closing_) {
        return;
    }
    if (!isAvailable(command)) {
        DVV_LOG_DEBUG("command {} is not available", commandId(command));
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
    Session* session = wxGetApp().session();
    if (session == nullptr) {
        return;
    }
    switch (command) {
        case Command::CloseStreams: session->closeStreams(); return;
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
        case Command::Diagnostics: session->logDiagnostics(); return;
        case Command::Quit:
        case Command::GpuInfo:
        case Command::Open:
        case Command::OpenUri:
        case Command::Format:
        case Command::About: return;
    }
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
                .reason = {}};
    }
    return {.available = false, .device = {}, .api = {}, .reason = "no GPU device is in use"};
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
    openFiles({dvv::media::pathFromUtf8(dialog.GetPath().utf8_string())});
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
    openUri(0, last_typed_uri_, std::nullopt);
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
        DVV_LOG_INFO("{}", *refusal);
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
    }
    showFormatDialogs();  // formatNeeded events that arrived meanwhile
}

void MainFrame::showErrorFormat() {
    // The failed open closed the stream, so View -> Format... has nothing to show (D4b).
    const std::string uri = std::exchange(format_error_uri_, {});
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
        openWithChoice(0, withRawImpl(uri), choice, std::nullopt);
    } else if (!closing_) {
        DVV_LOG_INFO("open of {} cancelled", uri);
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
            if (!closing_) {
                DVV_LOG_INFO("open of {} cancelled", uri);
            }
            continue;
        }
        const FormatChoice choice = dialog.choice();
        last_choice_ = choice;
        openWithChoice(pending.slot, uri, choice, std::nullopt);
    }
}

void MainFrame::openFiles(std::vector<std::filesystem::path> files) {
    if (closing_) {
        return;
    }
    const DropPlan plan = routeDrop(files);
    if (plan.uri) {
        openUri(0, *plan.uri, std::nullopt);  // frame 0 (design v2 §4.3)
    }
    if (plan.ignored > 0) {
        DVV_LOG_INFO("{} files ignored: one stream at a time", plan.ignored);
    }
}

void MainFrame::openUri(std::uint32_t slot, const std::string& text,
                        std::optional<std::int64_t> start) {
    // M4 GUI D3: every open (Open, Open URI, drop, command line, View -> Format) of a URI that
    // parses first closes the current stream. The Session keeps replace-on-success. A text that
    // does not parse only warns and keeps the stream (and a loss's Reopen target).
    dvv::Result<dvv::Uri> uri = dvv::parseUri(text);
    if (!uri) {
        showWarning(uri.error().describe());
        return;
    }
    if (Session* session = closing_ ? nullptr : wxGetApp().session(); session != nullptr) {
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
                               const FormatChoice& choice, std::optional<std::int64_t> start) {
    const dvv::Result<std::string> applied = applyChoice(uri, choice);
    if (!applied) {
        showWarning(applied.error().describe());
        return;
    }
    openUri(slot, *applied, start);
}

void MainFrame::showWarning(const std::string& text) {
    DVV_LOG_WARN("{}", text);
    showMessage(wxString::FromUTF8(text), wxICON_WARNING);
}

void MainFrame::onCharHook(wxKeyEvent& event) {
    if (closing_) {
        event.Skip();
        return;
    }
    const KeyChord chord =
        normaliseKey(event.GetKeyCode(), event.ControlDown(), event.ShiftDown(), event.AltDown());
    // Only an editable text entry keeps bare keys (the read-only Info and Log panes do not, D17).
    const auto* entry = dynamic_cast<const wxTextEntry*>(wxWindow::FindFocus());
    const bool text_focus = entry != nullptr && entry->IsEditable();
    if (const std::optional<Command> command = commandForKey(chord, text_focus)) {
        execute(*command, CommandSource::Key);
        return;  // consumed: the slider and the buttons never see bare keys
    }
    event.Skip();
}

TransportBar& MainFrame::transportBar() noexcept {
    return player_->transportBar();
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
    // The bar lives inside the player panel, so no size event runs the check.
    checkLayoutLimits();
}

void MainFrame::dismissMessage() {
    player_->infoBar().Dismiss();
    reopen_offered_ = false;
    format_error_uri_.clear();
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

std::array<wxString, 4> MainFrame::statusTexts() const {
    return status_texts_;
}

void MainFrame::onStackChanged(const std::weak_ptr<dvv::gpu::ExecutionContext>& context,
                               std::uint64_t epoch) {
    if (closing_) {
        return;
    }
    context_ = context;
    stack_lost_ = context.expired();
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
    // No viewport call: the renderer fits from the display image (D23).
    streams_ = streams;
    for (const StreamInfo& stream : streams_) {
        file_colours_.remember(stream);  // the file's colour for View > Format
        DVV_LOG_INFO("stream {}: {}", stream.slot, stream.resolvedUri);
        // What the provider noted (tag conflicts, a non-standard colour, the
        // clip description in use), on open and reopen.
        for (const std::string& note : stream.info.notes) {
            DVV_LOG_INFO("stream {}: {}", stream.slot, note);
        }
    }
    info_->setText(formatInfo(streams_));
    // D5R §2.2: the mapping field changes with the stream (open, reopen, Format, close), not on
    // the status tick.
    status_texts_[3] = mappingField(streams_);
    SetStatusText(status_texts_[3], 3);
    const std::array<InfoButton, 1> format_button{
        {{.id = INFOBAR_FORMAT_ID, .label = "Format..."}}};
    for (const StreamInfo& stream : streams_) {
        for (const std::string& warning : stream.info.warnings) {
            if (warning.find("trailing bytes ignored") != std::string::npos) {
                // §7: the size is probably wrong; the button opens View -> Format...
                showMessage(wxString::FromUTF8(fileName(stream.resolvedUri) + ": " + warning),
                            wxICON_INFORMATION, format_button);
            } else {
                showMessage(wxString::FromUTF8(stream.resolvedUri + ": " + warning),
                            wxICON_INFORMATION);
            }
        }
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
    if (const Startup& startup = wxGetApp().startup(); !startup.ffmpeg.core) {
        DVV_LOG_INFO("{}", ffmpegStatusLine(startup.ffmpeg));
    }
    // D21: one dialog at a time; the next one after the current one closes.
    format_queue_.push_back(
        {.slot = slot, .request = request, .candidates = candidates, .reason = reason});
    showFormatDialogs();
}

void MainFrame::onError(const dvv::Error& error) {
    if (closing_) {
        return;
    }
    // Design v2 §4.6: the FFmpeg status is appended when FFmpeg is unavailable. The event carries
    // no URI, so the last open request's URI stands in (a lavfi: open without avfilter). The log
    // keeps the full status line; the infobar shows no directory or path.
    const dvv::components::FfmpegStatus& ffmpeg = wxGetApp().startup().ffmpeg;
    // M4 GUI D7 C: a scheme-less text that failed as a missing file was probably meant as a URI.
    // The hint and the Format... button are for open errors only; an open closes the
    // stream first (D3), so an error while a stream is shown is a playback error.
    const bool open_error = streams_.empty();
    const std::string_view typed = open_error ? std::string_view(last_open_uri_) : "";
    DVV_LOG_WARN("{}", withSchemeHint(openErrorText(error, last_open_uri_, ffmpeg), typed, error));
    const std::string text = withSchemeHint(
        openErrorText(error, last_open_uri_, ffmpeg, StatusPaths::Hidden), typed, error);
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
    } else {
        showMessage(wxString::FromUTF8(text), wxICON_WARNING);
    }
}

void MainFrame::onPresenterFailed(const dvv::Error& error) {
    if (closing_) {
        return;
    }
    // D25: the viewport logged it; the user's Reopen rebuilds the stack.
    offerReopen(wxString::FromUTF8(error.describe()));
}

void MainFrame::onMenu(wxCommandEvent& event) {
    execute(static_cast<Command>(event.GetId() - COMMAND_ID_BASE), CommandSource::Menu);
}

void MainFrame::onUpdateUi(wxUpdateUIEvent& event) {
    event.Enable(!closing_ && isAvailable(static_cast<Command>(event.GetId() - COMMAND_ID_BASE)));
}

void MainFrame::onStatusTimer(wxTimerEvent& /*event*/) {
    const Session* session = wxGetApp().session();
    if (closing_ || session == nullptr) {
        return;
    }
    const SessionStatus status = session->status();
    // Without a stream the transport bar shows nothing (a default PlayerStatus).
    player_->transportBar().update(status.streams > 0 ? status.player : PlayerStatus{});
    const StatusFields fields = statusFields(status);
    const std::array<wxString, 3> texts{wxString::FromUTF8(fields.position),
                                        wxString::FromUTF8(fields.fps),
                                        wxString::FromUTF8(fields.bottleneck)};
    for (std::size_t i = 0; i < texts.size(); ++i) {
        if (texts[i] != status_texts_[i]) {
            status_texts_[i] = texts[i];
            SetStatusText(texts[i], static_cast<int>(i));
        }
    }
}

void MainFrame::onIconize(wxIconizeEvent& event) {
    if (viewport_ != nullptr) {
        viewport_->topLevelIconized(event.IsIconized());
    }
    event.Skip();
}

void MainFrame::applyLayoutLimits() {
    // The minimum client size comes from the real controls at the current scale and font: the
    // viewport's 320x240 DIP, the transport bar, the infobar, the dock minimums and the sashes,
    // plus a small margin for wxAUI's rounding.
    const wxSize computed = aui_.minimumClientSize();
    applied_minimum_ = computed;  // checkLayoutLimits() compares the computed value
    const wxSize minimum = computed + FromDIP(wxSize(LAYOUT_MARGIN_DIP, LAYOUT_MARGIN_DIP));
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
#if defined(__WXGTK__)
        // wxGTK keeps a minimum as a window size, converted with the decoration size it knew at
        // SetMinClientSize(); the menu bar, status bar and window decorations are known only after
        // the map, so the minimum client size can end up below the one applied: apply it again.
        const wxSize wanted =
            applied_minimum_ + FromDIP(wxSize(LAYOUT_MARGIN_DIP, LAYOUT_MARGIN_DIP));
        const wxSize effective = GetMinClientSize();
        stale = stale || effective.x < wanted.x || effective.y < wanted.y;
#endif
        if (stale) {
            applyLayoutLimits();
        }
    });
}

void MainFrame::onShow(wxShowEvent& event) {
    if (viewport_ != nullptr) {
        viewport_->topLevelShown(event.IsShown());
    }
    event.Skip();
}

void MainFrame::onClose(wxCloseEvent& event) {
    if (closing_) {
        event.Skip();
        return;
    }
    closing_ = true;
    status_timer_.Stop();
    if (selftest_) {
        selftest_->stop();  // the runner lives until ~MainFrame
    }
    format_queue_.clear();
    if (format_dialog_ != nullptr && format_dialog_->IsModal()) {
        format_dialog_->EndModal(wxID_CANCEL);  // its caller sees closing_ and returns
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
