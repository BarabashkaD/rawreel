// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include "gui/zoom_window.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <wx/tglbtn.h>

#include "core/commands.h"
#include "core/config.h"
#include "core/frame_set.h"
#include "core/inspector.h"
#include "core/inspector_text.h"
#include "core/layout.h"
#include "core/native_os.h"
#include "core/overlay.h"
#include "core/zoom_keys.h"
#include "dvv/gpu/execution_context.h"
#include "gui/edit_dispatch.h"
#include "gui/floating_window.h"
#include "gui/main_frame.h"
#include "gui/native_controls.h"
#include "gui/native_policy.h"
#include "gui/raw_words_view.h"
#include "gui/viewport.h"
#include "render/frame_feed.h"

namespace rawreel {

namespace {

constexpr int GAP_DIP = 6;  // between the toolbar's groups, and around the rows
constexpr bool CMD_FOR_CTRL = NATIVE_OS == Os::MacOs;  // the menus show Ctrl as Cmd there

// The roles the shown window's viewport subscribes to: the picture, the planes of the Planes
// layout and the stored words of the readback.
constexpr ViewRoles SHOWN_ROLES = ViewRoles::of(ViewRole::Display) |
                                  ViewRoles::of(ViewRole::Planes) | ViewRoles::of(ViewRole::Raw);

std::string commandTip(Command command, std::string_view help) {
    return tooltipText(commandTable()[static_cast<std::size_t>(command)], help, CMD_FOR_CTRL);
}

bool darkTheme() {
    return wxSystemSettings::GetAppearance().IsDark();
}

}  // namespace

void ZoomWindow::TogglePair::set(bool second_on) const {
    first->SetValue(!second_on);
    second->SetValue(second_on);
}

ZoomWindow::TogglePair ZoomWindow::addPair(wxSizer& sizer, const wxString& first,
                                           const wxString& second, const wxString& tip,
                                           std::function<void(bool)> on) {
    wxPanel& panel = content();
    TogglePair pair{.first = new wxToggleButton(&panel, wxID_ANY, first),
                    .second = new wxToggleButton(&panel, wxID_ANY, second)};
    pair.first->SetToolTip(tip);
    pair.second->SetToolTip(tip);
    pair.set(false);
    // A radio pair: a click turns its button on and the other off; a click on the button that is
    // on leaves it on. Then `on` tells the owner, which sets the pair again (setToggles).
    const auto click = [pair, on = std::move(on)](bool second_on) {
        pair.set(second_on);
        on(second_on);
    };
    pair.first->Bind(wxEVT_TOGGLEBUTTON, [click](wxCommandEvent& /*event*/) { click(false); });
    pair.second->Bind(wxEVT_TOGGLEBUTTON, [click](wxCommandEvent& /*event*/) { click(true); });
    auto* const group = new wxBoxSizer(wxHORIZONTAL);
    group->Add(pair.first);
    group->Add(pair.second);
    sizer.Add(group, wxSizerFlags().CentreVertical());
    return pair;
}

ZoomWindow::ZoomWindow(MainFrame& owner, std::shared_ptr<FrameFeed> feed, LayoutSize picture_dip,
                       std::optional<ScreenRect> place)
    : FloatingWindow(owner, FloatingKind::Zoom, "Zoom", FloatingSizing::Resizable) {
    wxPanel& panel = content();
    const int gap = FromDIP(GAP_DIP);

    auto* const toolbar = new wxBoxSizer(wxHORIZONTAL);
    live_ = addPair(*toolbar, "Pinned", "Live",
                    wxString::FromUTF8("The pinned pixel, or Live: the pixel under the cursor (" +
                                       keyText(KeyChord{.key = 'P', .shift = true}) + ")"),
                    [this](bool live) {
                        // As Shift+P and the menu: Live Probe only where it is available (a
                        // stream); a refusal sets the pair back.
                        MainFrame& frame = this->owner();
                        const Inspector& inspector = frame.inspector();
                        if (live != inspector.live() && frame.isAvailable(Command::LiveProbe)) {
                            frame.execute(Command::LiveProbe, CommandSource::Button);
                        }
                        setToggles(inspector.live(), inspector.mode(), inspector.layout());
                    });
    toolbar->AddSpacer(gap);
    mode_ = addPair(
        *toolbar, "Follow", "Cell", "Follow the pin, or show the grid cell that holds it (F)",
        [this](bool cell) {
            this->owner().setInspectorMode(cell ? InspectorMode::Cell : InspectorMode::Follow);
        });
    toolbar->AddSpacer(gap);
    layout_ = addPair(*toolbar, "Structure", "Planes",
                      "The pixel structure, or one panel per component (S)", [this](bool planes) {
                          this->owner().setInspectorLayout(planes ? InspectorLayout::Planes
                                                                  : InspectorLayout::Structure);
                      });
    toolbar->AddSpacer(gap);
    fit_cell_ = new wxButton(&panel, wxID_ANY, "Fit cell");
    fit_cell_->SetToolTip(wxString::FromUTF8(
        commandTip(Command::FitToWindow, "Zoom so that the grid cell of the pin fits")));
    numbers_fit_ = new wxButton(&panel, wxID_ANY, "Numbers fit");
    numbers_fit_->SetToolTip(wxString::FromUTF8(
        commandTip(Command::ActualSize, "Zoom so that every number fits its cell")));
    toolbar->Add(fit_cell_, wxSizerFlags().CentreVertical());
    toolbar->Add(numbers_fit_, wxSizerFlags().CentreVertical());
    // The buttons act on this window's view, wherever the focus is: as Zoom to Fit and Actual Size
    // do with the window in front (§4.2).
    fit_cell_->Bind(wxEVT_BUTTON, [this](wxCommandEvent& /*event*/) {
        this->owner().applyViewCommand(Command::FitToWindow, *viewport_);
    });
    numbers_fit_->Bind(wxEVT_BUTTON, [this](wxCommandEvent& /*event*/) {
        this->owner().applyViewCommand(Command::ActualSize, *viewport_);
    });

    const wxFont mono = systemMonospaceFont();
    auto* const header = new wxBoxSizer(wxVERTICAL);
    for (wxStaticText*& line : header_) {
        // One line each, whatever its length: a long line is cut, never widens the window.
        line = new wxStaticText(&panel, wxID_ANY, " ", wxDefaultPosition, wxDefaultSize,
                                wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
        line->SetFont(mono);
        header->Add(line, wxSizerFlags().Expand());
    }

    viewport_ = new Viewport(&panel, std::move(feed));
    viewport_->setFeedRoles(ViewRoles{});  // hidden: no subscription (attach() makes it)
    viewport_->setTool(ViewportTool::Pick);
    viewport_->setZoomRule(ZoomRule::WholePixels);
    const int width = std::max(picture_dip.width, ZOOM_WINDOW_SIZE_MIN);
    const int height = std::max(picture_dip.height, ZOOM_WINDOW_SIZE_MIN);
    viewport_->SetMinSize(FromDIP(wxSize(width, height)));

    raw_ = new RawWordsView(&panel);
    raw_->Hide();  // until there are raw words

    auto* const top = new wxBoxSizer(wxVERTICAL);  // the toolbar over the header
    top->Add(toolbar);
    top->AddSpacer(gap);
    top->Add(header, wxSizerFlags().Expand());
    auto* const column = new wxBoxSizer(wxVERTICAL);
    column->Add(top, wxSizerFlags().Expand().Border(wxALL, gap));
    column->Add(viewport_, wxSizerFlags(1).Expand());
    column->Add(raw_, wxSizerFlags().Expand());
    panel.SetSizer(column);

    // The size: the window around that picture area (the raw words hidden), kept in DIP from here
    // (FloatingWindow); then the user may shrink the picture to the config's minimum.
    Fit();
    viewport_->SetMinSize(FromDIP(wxSize(ZOOM_WINDOW_SIZE_MIN, ZOOM_WINDOW_SIZE_MIN)));
    const wxSize dip = ToDIP(GetSize());
    restorePlace(place, LayoutSize{.width = dip.x, .height = dip.y});

    Bind(wxEVT_SHOW, [this](wxShowEvent& event) {
        event.Skip();
        viewport_->topLevelShown(event.IsShown());
        if (event.IsShown() && !std::exchange(ready_, true)) {
            // The native window exists and is shown once the queued call runs (App::OnInit).
            viewport_->CallAfter([viewport = viewport_] { viewport->markReady(); });
        }
    });
}

void ZoomWindow::showValues(const std::optional<PixelValues>& values,
                            std::array<std::string, 4> header, std::vector<RawWordLine> raw,
                            bool pending) {
    // The header lines have a fixed height and cut their text at their own width, so a new label
    // needs no layout. A layout runs only when the raw words appear, go or change: it resizes the
    // viewport, whose view change comes back here (MainFrame::updateInspector), and the header
    // text, which follows the view, must not start another layout inside a running one.
    for (std::size_t i = 0; i < header_.size(); ++i) {
        const wxString label = header[i].empty() ? wxString(" ") : wxString::FromUTF8(header[i]);
        if (header_[i]->GetLabel() != label) {
            header_[i]->SetLabel(label);
        }
    }
    // A pin or probe move leaves the values pending until the readback of the new pixel: the raw
    // words go blank in place. Hiding them would resize the viewport twice per move (bigger, then
    // back with the values), and on GTK each size change reaches the renderer as its own redraw.
    if (!values) {
        raw.clear();
        if (pending && raw_->IsShown()) {
            raw.resize(raw_->lines().size());
        }
    }
    const bool changed = raw_->IsShown() == raw.empty() || raw_->lines() != raw;
    raw_->Show(!raw.empty());
    raw_->setLines(std::move(raw), darkTheme());
    if (changed) {
        content().Layout();
    }
}

void ZoomWindow::setToggles(bool live, InspectorMode mode, InspectorLayout layout) {
    live_.set(live);
    mode_.set(mode == InspectorMode::Cell);
    layout_.set(layout == InspectorLayout::Planes);
}

void ZoomWindow::setOverlayConfig(const GridConfig& grid, const RulerConfig& rulers) {
    grid_ = grid;
    rulers_ = rulers;
    sendOverlayConfig();
}

void ZoomWindow::sendOverlayConfig() {
    RulerConfig rulers = rulers_;
    rulers.show = attached_;  // always on while shown (design §4.2); no glyph strip while hidden
    viewport_->setOverlayConfig(grid_, rulers);
}

void ZoomWindow::attach(const std::weak_ptr<dvv::gpu::ExecutionContext>& context,
                        std::uint64_t epoch) {
    attached_ = true;
    sendOverlayConfig();
    viewport_->setFeedRoles(SHOWN_ROLES);
    viewport_->setValueGlyphs(true);
    viewport_->setStack(context, epoch);
}

void ZoomWindow::detach() {
    viewport_->detachStack();  // presenter, renderer and context
    viewport_->setValueGlyphs(false);
    viewport_->setFeedRoles(ViewRoles{});
    attached_ = false;
    sendOverlayConfig();  // the rulers off: the ruler glyph strip is released
}

std::array<wxString, 4> ZoomWindow::headerForTest() const {
    std::array<wxString, 4> lines;
    for (std::size_t i = 0; i < header_.size(); ++i) {
        lines.at(i) = header_.at(i)->GetLabel();
    }
    return lines;
}

std::vector<RawWordLine> ZoomWindow::rawLinesForTest() const {
    return raw_->IsShown() ? raw_->lines() : std::vector<RawWordLine>{};
}

// Copy (§4.6): Ctrl+C copies the pixel values before any other rule, a focused toolbar button too;
// on macOS Edit -> Copy does it (MainFrame::onEditMenu). Esc turns the pipette off before the
// window's own Esc rule hides it (§4.1): a second Esc hides it. Then the Zoom keys (§4.2), ahead of
// the main table: the arrows move the pin here and never step frames.
bool ZoomWindow::handleKey(const KeyChord& key) {
    if (key == chord(Key::Escape) && owner().pipetteOn()) {
        owner().execute(Command::Pipette, CommandSource::Key);
        return true;
    }
    if (native_policy::infoCopiesOnCtrlC() && key == KeyChord{.key = 'C', .ctrl = true}) {
        if (owner().isAvailable(Command::CopyValues)) {
            owner().execute(Command::CopyValues, CommandSource::Key);
        }
        return true;
    }
    if (const std::optional<ZoomKeyAction> action = zoomKeyAction(key)) {
        // A focused toolbar control keeps the keys it uses itself (controlKeepsKey: Space on a
        // toggle or a button; a button keeps no arrows). The picture area, a custom window the
        // rule would keep the arrows and Space for, gives them to the Zoom keys.
        const wxWindow* const focus = owner().focusedWindow();
        if (focus != viewport_ && focus != raw_ && controlKeepsKey(focus, key)) {
            return false;
        }
        owner().inspectorKey(*action);
        return true;
    }
    return false;
}

void ZoomWindow::applyColours() {
    raw_->setLines(raw_->lines(), darkTheme());
}

void ZoomWindow::measure() {
    const wxFont mono = systemMonospaceFont();
    for (wxStaticText* const line : header_) {
        line->SetFont(mono);
    }
    viewport_->SetMinSize(FromDIP(wxSize(ZOOM_WINDOW_SIZE_MIN, ZOOM_WINDOW_SIZE_MIN)));
    content().Layout();
}

LayoutSize ZoomWindow::pictureSizeDip() const {
    // The area under the header: the viewport, and the raw words while they show, so that a
    // restored window gets the same area whether its first values have come yet or not.
    wxSize area = viewport_->GetSize();
    if (raw_->IsShown()) {
        area.y += raw_->GetSize().y;
    }
    const wxSize dip = ToDIP(area);
    return LayoutSize{.width = dip.x, .height = dip.y};
}

}  // namespace rawreel
