// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/main_frame.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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
#include "core/session.h"
#include "core/status_text.h"
#include "dvv/base/json.h"
#include "dvv/base/log.h"
#include "dvv/base/uri.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/gpu_info_json.h"
#include "dvv/media/types.h"
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

// The file name of a file: URI (the infobar's short form); the URI itself otherwise.
std::string fileName(const std::string& uri) {
    const dvv::Result<dvv::Uri> parsed = dvv::parseUri(uri);
    if (!parsed || parsed->scheme != "file" || parsed->path.empty()) {
        return uri;
    }
    return dvv::media::utf8FromPath(dvv::media::pathFromUtf8(parsed->path).filename());
}

}  // namespace

MainFrame::MainFrame(std::shared_ptr<DisplayState> display)
    : wxFrame(nullptr, wxID_ANY, "rawreel"), status_timer_(this) {
    aui_.SetManagedWindow(this);
    player_ = new PlayerPanel(
        this, std::move(display), [this](Command command) { execute(command); },
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
    // (M1b F14); no border, so the presented extent equals the pane (arbiter LOG 21).
    aui_.AddPane(player_, wxAuiPaneInfo().Name("player").CenterPane().PaneBorder(false));
    aui_.AddPane(info_, wxAuiPaneInfo()
                            .Name("info")
                            .Caption("Info")
                            .Right()
                            .BestSize(FromDIP(wxSize(320, 400)))
                            .CloseButton(false));
    aui_.AddPane(log_, wxAuiPaneInfo()
                           .Name("log")
                           .Caption("Log")
                           .Bottom()
                           .BestSize(FromDIP(wxSize(800, 160)))
                           .CloseButton(false));
    aui_.Update();
    buildMenus();
    buildStatusBar();
    SetClientSize(FromDIP(wxSize(1280, 720)));
    // The handler does not Skip(), so wxInfoBarGeneric's own button handler (which would hide the
    // bar) does not run; execute() dismisses it.
    player_->infoBar().Bind(
        wxEVT_BUTTON, [this](wxCommandEvent& /*event*/) { execute(Command::Reopen); },
        INFOBAR_REOPEN_ID);
    player_->infoBar().Bind(
        wxEVT_BUTTON,
        [this](wxCommandEvent& /*event*/) {
            dismissMessage();
            execute(Command::Format);
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
    const std::array<int, 3> widths{-3, -2, -2};
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
        case Command::Diagnostics: return true;
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

void MainFrame::execute(Command command) {
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
        case Command::Format: return;
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

void MainFrame::showOpenDialog() {
    wxFileDialog dialog(this, "Open a raw video file", wxEmptyString, wxEmptyString, OPEN_WILDCARD,
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK || closing_) {
        return;
    }
    openFiles({dvv::media::pathFromUtf8(dialog.GetPath().utf8_string())});
}

void MainFrame::showOpenUriDialog() {
    wxTextEntryDialog dialog(this, "Source URI:", "Open URI", wxString::FromUTF8(OPEN_URI_EXAMPLE));
    if (dialog.ShowModal() != wxID_OK || closing_) {
        return;
    }
    openUri(0, dialog.GetValue().utf8_string(), std::nullopt);
}

void MainFrame::showStreamFormat() {
    if (format_dialog_ != nullptr || streams_.empty()) {
        return;
    }
    const StreamInfo stream = streams_.front();
    FormatDialog dialog(this, "Format", choiceFromInfo(stream.info), {}, {});
    if (runFormatDialog(dialog) == wxID_OK) {
        // D21, design v2 §4.3: a new origin at the kept position.
        const std::int64_t position = session().status().player.position;
        openWithChoice(stream.slot, stream.resolvedUri, dialog.choice(),
                       position >= 0 ? std::optional(position) : std::nullopt);
    }
    showFormatDialogs();  // formatNeeded events that arrived meanwhile
}

int MainFrame::runFormatDialog(FormatDialog& dialog) {
    format_dialog_ = &dialog;
    ++format_dialogs_;
    const int result = showModal(dialog);
    format_dialog_ = nullptr;
    return closing_ ? wxID_CANCEL : result;
}

void MainFrame::showFormatDialogs() {
    while (format_dialog_ == nullptr && !closing_ && !format_queue_.empty()) {
        const PendingFormat pending = std::move(format_queue_.front());
        format_queue_.pop_front();
        // Prefill (D21): the best candidate, else the last accepted choice, else yuv420p 1080p25.
        FormatChoice initial = last_choice_.value_or(FormatChoice{.format = "yuv420p"});
        if (!pending.candidates.candidates.empty()) {
            initial = choiceFromCandidate(pending.candidates.candidates.front());
        }
        const std::string uri = dvv::formatUri(pending.request.uri);
        FormatDialog dialog(this, "Format of the stream", initial, pending.candidates.candidates,
                            pending.reason);
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
        DVV_LOG_INFO("{} files ignored: one stream in v1.0", plan.ignored);
    }
}

void MainFrame::openUri(std::uint32_t slot, const std::string& text,
                        std::optional<std::int64_t> start) {
    dvv::Result<dvv::Uri> uri = dvv::parseUri(text);
    if (!uri) {
        showWarning(uri.error().describe());
        return;
    }
    if (Session* session = wxGetApp().session(); session != nullptr && !closing_) {
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
        execute(*command);
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
}

void MainFrame::dismissMessage() {
    player_->infoBar().Dismiss();
    reopen_offered_ = false;
}

void MainFrame::offerReopen(const wxString& text) {
    const std::array<InfoButton, 1> buttons{{{.id = INFOBAR_REOPEN_ID, .label = "Reopen"}}};
    showMessage(text, wxICON_ERROR, buttons);
    reopen_offered_ = true;
}

bool MainFrame::reopenShown() const {
    return reopen_offered_ && player_->infoBar().IsShown();
}

std::array<wxString, 3> MainFrame::statusTexts() const {
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
    // No viewport call: the renderer fits from the display image (D23).
    streams_ = streams;
    for (const StreamInfo& stream : streams_) {
        DVV_LOG_INFO("stream {}: {}", stream.slot, stream.resolvedUri);
    }
    info_->setText(formatInfo(streams_));
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
    DVV_LOG_INFO("stream {}: {} needs a format ({} candidates): {}", slot,
                 dvv::formatUri(request.uri), candidates.candidates.size(), reason);
    // D21: one dialog at a time; the next one after the current one closes.
    format_queue_.push_back(
        {.slot = slot, .request = request, .candidates = candidates, .reason = reason});
    showFormatDialogs();
}

void MainFrame::onError(const dvv::Error& error) {
    if (closing_) {
        return;
    }
    const std::string text = error.describe();
    DVV_LOG_WARN("{}", text);
    // A loss, or a failed makeStack while no stack is installed (§7 "infobar keeps Reopen").
    // Session events arrive in the Session's order and stackChanged({}, 0) comes before
    // error(DeviceLost), so stack_lost_ is current.
    if (error.code == dvv::Errc::DeviceLost || stack_lost_) {
        offerReopen(wxString::FromUTF8(text));
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
    execute(static_cast<Command>(event.GetId() - COMMAND_ID_BASE));
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
