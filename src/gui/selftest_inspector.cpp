// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

// The pixel inspector selftest (pixel inspector design §4.1, §4.2, §4.4-§4.6), slices A and B.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <wx/tglbtn.h>

#include "core/commands.h"
#include "core/config.h"
#include "core/frame_set.h"
#include "core/inspector.h"
#include "core/inspector_text.h"
#include "core/layout.h"
#include "core/overlay.h"
#include "core/session.h"
#include "dvv/base/log.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/sample_grid.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "dvv/platform/pixel_size.h"
#include "gui/app.h"
#include "gui/floating_window.h"
#include "gui/main_frame.h"
#include "gui/native_policy.h"
#include "gui/selftest.h"
#include "gui/selftest_detail.h"
#include "gui/transport_bar.h"
#include "gui/viewport.h"
#include "gui/viewport_window.h"
#include "gui/zoom_window.h"
#include "render/frame_feed.h"
#include "render/view_renderer.h"
#include "render/view_state.h"

namespace rawreel {

namespace {

// The pinned pixel of the inspector steps, and the pixel the pin then moves to.
constexpr dvv::media::ImagePixel PIN{.x = 20, .y = 10};
constexpr dvv::media::ImagePixel MOVED_PIN{.x = 21, .y = 11};
// The pixel the markers step moves the pin to, beside MOVED_PIN in the same view.
constexpr dvv::media::ImagePixel MARKED_PIN{.x = 22, .y = 12};
// Whether the platform has the display-scale hook (setBackingScaleForTest): wxOSX only.
#ifdef __WXOSX__
constexpr bool SCALE_HOOK = true;
#else
constexpr bool SCALE_HOOK = false;
#endif

// The synthetic gradient's code value of `component` at image pixel `pixel` of frame `index`,
// computed on the host as the synthetic source's CPU generator computes it (dvv-media's
// gradientValue, internal to dvv-media): the ramp (x + y)·max / max(w + h − 2, 1) over the
// component's sample grid, plus the frame index, plus component·(max + 1)/4, modulo max + 1. The
// values step's expectation, independent of the GPU path under test.
std::uint32_t testCode(dvv::media::FormatId format, dvv::platform::PixelSize image,
                       std::size_t component, dvv::media::ImagePixel pixel, std::int64_t index) {
    const dvv::media::ComponentDesc& desc = dvv::media::formatDesc(format).components[component];
    const auto [grid_width, grid_height] =
        dvv::media::componentSize(format, component, image.width, image.height);
    const dvv::media::SamplePosition at = dvv::media::sampleOf(desc, pixel);
    const std::uint64_t maxv = (std::uint64_t{1} << desc.bits) - 1U;
    const std::uint64_t span =
        std::max<std::uint64_t>(std::uint64_t{grid_width} + grid_height, 3U) - 2U;
    const std::uint64_t ramp = (std::uint64_t{at.x} + at.y) * maxv / span;
    const std::uint64_t value =
        ramp + static_cast<std::uint64_t>(index) + (component * ((maxv + 1U) / 4U));
    return static_cast<std::uint32_t>(value % (maxv + 1U));
}

// The Zoom window's header lines; empty without the window.
std::array<wxString, 4> zoomHeader(const MainFrame& frame) {
    const ZoomWindow* const zoom = frame.zoomWindow();
    return zoom != nullptr ? zoom->headerForTest() : std::array<wxString, 4>{};
}

// Whether the frame's values are those of `pixel` and match the host's codes, the PIXEL status
// field shows them, and the Zoom header's first two lines are "pinned x X y Y · frame N" and the
// host's codes with the bit depth (pixelStatusText of the host-coded values after its ": ").
bool valuesMatch(const MainFrame& frame, dvv::media::ImagePixel pixel, std::string& seen) {
    const std::optional<PixelValues>& values = frame.pixelValues();
    const wxString status = frame.statusTexts()[MainFrame::PIXEL_FIELD];
    const std::array<wxString, 4> header = zoomHeader(frame);
    std::string codes;
    std::string want_line0;
    std::string want_line1;
    bool match = values && values->pixel == pixel && values->frame >= 0 &&
                 !frame.streams().empty() && status == wxString::FromUTF8(pixelStatusText(values));
    if (values && !frame.streams().empty()) {
        const dvv::media::FormatId format = frame.streams().front().info.desc.format;
        const dvv::platform::PixelSize image = frame.viewport().imageSize();
        PixelValues host = *values;
        for (std::size_t k = 0; k < values->count; ++k) {
            const std::uint32_t want = testCode(format, image, k, values->pixel, values->frame);
            codes += std::format(" {} (want {})", values->codes.at(k), want);
            match = match && values->codes.at(k) == want;
            host.codes.at(k) = want;
        }
        match = match && values->count == dvv::media::formatDesc(format).componentCount;
        const std::string status_text = pixelStatusText(host);
        const std::size_t colon = status_text.find(": ");
        want_line0 = std::format("pinned x {} y {}", pixel.x, pixel.y);
        want_line1 =
            std::format("{} ({}-bit)",
                        colon == std::string::npos ? std::string() : status_text.substr(colon + 2),
                        dvv::media::formatDesc(format).components.front().bits);
        match = match && header[0].utf8_string().starts_with(want_line0) &&
                header[1].utf8_string() == want_line1;
    }
    seen = std::format(
        "values {}, codes{}, status '{}', header '{}' | '{}' (want '{}...' | '{}')",
        values ? std::format("at {} {} frame {}", values->pixel.x, values->pixel.y, values->frame)
               : "none",
        codes, status.utf8_string(), header[0].utf8_string(), header[1].utf8_string(), want_line0,
        want_line1);
    return match;
}

// No values: the values, the PIXEL field and the header's first two lines are empty.
bool valuesBlank(const MainFrame& frame) {
    std::array<wxString, 4> header = zoomHeader(frame);
    return !frame.pixelValues() && frame.statusTexts()[MainFrame::PIXEL_FIELD].empty() &&
           header[0].Trim().empty() && header[1].Trim().empty();
}

// A left click (press and release) at the first window point of the main viewport over image
// pixel `pixel`. The view first zooms to four window points per image pixel around it: at a lower
// zoom one window point covers more than one backing pixel on a high-density display, and some
// image pixels lie under no window point at all. False without such a point.
bool clickPixel(Viewport& viewport, dvv::media::ImagePixel pixel, bool centre = true) {
    if (centre) {
        viewport.setView({.mode = dvv::media::ViewMode::Zoom,
                          .zoom = 4.0 * viewport.GetContentScaleFactor(),
                          .centre = {.x = pixel.x + 0.5, .y = pixel.y + 0.5},
                          .image = viewport.imageSize()});
    }
    const wxSize client = viewport.GetClientSize();
    for (int y = 0; y < client.y; ++y) {
        for (int x = 0; x < client.x; ++x) {
            if (viewport.pixelAt(wxPoint(x, y)) == pixel) {
                sendViewportMouse(viewport, wxEVT_LEFT_DOWN, wxPoint(x, y), true, false);
                sendViewportMouse(viewport, wxEVT_LEFT_UP, wxPoint(x, y), false, false);
                return true;
            }
        }
    }
    return false;
}

// Copy in the Zoom window with `focus` focused, as each platform copies there: Ctrl+C in the
// window (Windows, Linux), Edit -> Copy with the window in front (macOS).
void copyInZoomWindow(MainFrame& frame, wxWindow& focus) {
    if (native_policy::infoCopiesOnCtrlC()) {
        (void)sendKeyWithFocus(frame, &focus, 'C', true);
        return;
    }
    frame.setFocusForTest(&focus);
    (void)sendMenu(frame, wxID_COPY);
    frame.setFocusForTest(nullptr);
}

// An answer of Session::leasesForTest, written on the Session executor.
struct LeaseAnswer {
    std::mutex mutex;
    bool asked = false;
    std::optional<Session::LeaseCounts> counts;
};

struct InspectorState {
    std::shared_ptr<LeaseAnswer> leases;  // closed_costs_nothing
    bool clicked = false;                 // clickPixel() found the pixel
    bool activeBefore = false;            // the main frame was active before the click
    bool pending = false;                 // right after the move: no values, PIXEL blank
    bool lateShown = false;               // a late readback of the old pixel filled the values
    std::size_t rawBefore = 0;            // the Zoom window's raw word lines before the move
    bool rawKept = false;                 // right after the move: as many raw lines, all blank
    bool hiddenBlank = false;             // right after the hide: no values, PIXEL blank
    bool belowOff = false;  // one step below the fit zoom: no numbers, the metrics kept
    bool fitRule = false;   // at Fit: the numbers on exactly when the shown zoom reaches fit
    RecordedInfo recorded;  // the main view's record before the markers step's click
    std::string expected;   // copyText() of the values
    std::optional<std::string> copied;
    // The Zoom window steps.
    bool taken = false;          // every key of the step was taken by a handler
    std::int64_t position = -1;  // the player's position at the step's start
    std::optional<dvv::media::ImagePixel> afterRight;  // the pin after Right
    bool liveOn = false;  // Shift+P in the Zoom window: Live on, its toggle too
    int phase = 0;        // the space step: 0 until it plays, then 1
    double mainZoom = 0;  // the main view's shown zoom at the step's start
    std::uint32_t actualZoom = 0, inZoom = 0, wantIn = 0;  // view keys: Actual Size, Zoom In
    // View keys at the zoom floor (§4.5): the floor, a request of 1, Zoom Out from 256 (its stops
    // and the last), a pinch and Ctrl+wheel out from 256, a Fit request, Fit cell and its want,
    // Zoom In from just under the stop past the floor and its want.
    std::uint32_t floor = 0, atOne = 0, outLast = 0, pinchOut = 0, fitCellShown = 0;
    std::uint32_t wantCell = 0, fromNear = 0, wantFromNear = 0;
    std::optional<std::uint32_t> wheelOut;  // nullopt where Ctrl+wheel does not zoom (macOS)
    std::vector<std::uint32_t> outStops, wantOutStops;
    bool inOffAt256 = false, outOffAtFloor = false, pinchOneKeeps = false, fitIsZoom = false;
    // The floor step: Planes (its floor, every panel within 3 x 3 cells), Structure again, a
    // finer major step, another image size.
    std::uint32_t planesFloor = 0, structureFloor = 0, stepFloor = 0;
    bool planesCells = false, planesAtFloor = false, structureAtFloor = false, stepAtFloor = false;
    bool otherImageZoom = false;
    // The step back to the old major step, a 3 x 3 major cell clip: its open (a Fit request) and
    // its floor; the floor before a resize and the window's client size.
    bool stepBack = false, smallOpen = false, smallAtFloor = false;
    std::uint32_t floorBefore = 0, smallFloor = 0, resizeFloor = 0;  // and Structure's floor
    wxSize windowSize;
    dvv::media::ViewRequest resizeView;  // the Zoom view before the resize
    GridConfig gridBefore;
    dvv::media::ViewRequest panned;  // the hand-panned Zoom view
    std::int64_t frameBefore = -1;   // the values' frame before the new frame
    LayoutSize sizeDip;              // the Zoom window's kept DIP size before a scale change
    wxSize windowDip;                // and its size in DIP
    double scale = 0;                // the scale the hook gives
    bool hooked = false;             // setBackingScaleForTest() took it
    bool changeSeen = false, blankAtChange = false, pinAtChange = false;  // the new clip
    std::optional<dvv::media::ImagePixel> probe;    // C in Live: the probe, away from the pin
    bool onProbe = false;                           // and the Zoom view centred on it
    std::optional<dvv::media::ImagePixel> markPin;  // the mark steps: the pin they started with
    std::uint32_t userZoom = 0;  // zoom_scale: the zoom the user chose, past the fit
    bool keyKept = false;        // a toolbar toggle with the focus keeps Space
    // The Planes geometry step: Structure, then Planes at the same Zoom request.
    std::uint32_t geometryZoom = 0;
    dvv::media::PixelRect structurePicture, panelPicture;  // panel 0's picture region
    std::optional<ImageRect> structureOutline, planesOutline;
    std::uint32_t planesPanels = 0, structurePanels = 0;           // the Zoom view's panels in each
    std::vector<std::optional<dvv::media::ImagePixel>> panelPins;  // a click per panel
    std::uint32_t fitCell = 0, wantFitCell = 0, wholeFitCell = 0;  // Fit cell in Planes
    // Actual Size in Structure: the Zoom view's zoom and numbersFitZoom raised to the floor, and
    // whether the main view stayed as it was.
    std::uint32_t numbersFit = 0, wantNumbersFit = 0;
    bool mainKept = false;
    std::optional<dvv::media::ImagePixel> spotPixel;  // the pixel under the click spot, panel 0
    // The small clip at its whole fit: the Zoom view before, the image and the rulers, each
    // check's text and whether every check held.
    dvv::media::ViewRequest smallView;
    dvv::platform::PixelSize smallImage;
    RulerConfig smallRulers;
    std::string smallSeen;
    bool smallWhole = false;
};

// A key in the Zoom window's viewport, with the focus there; false without the window or when no
// handler took it.
bool zoomKey(MainFrame& frame, int keycode, bool shift = false) {
    ZoomWindow* const zoom = frame.zoomWindow();
    if (zoom == nullptr) {
        return false;
    }
    frame.setFocusForTest(&zoom->viewport());
    const bool taken = sendKey(zoom->viewport(), keycode, shift);
    frame.setFocusForTest(nullptr);
    return taken;
}

// A pinch at the viewport's centre: its start at factor 1, its end at `factor`.
void pinch(Viewport& viewport, double factor) {
    const wxSize client = viewport.GetClientSize();
    for (const bool start : {true, false}) {
        wxZoomGestureEvent event(viewport.GetId());
        event.SetEventObject(&viewport);
        event.SetPosition(wxPoint(client.x / 2, client.y / 2));
        event.SetZoomFactor(start ? 1.0 : factor);
        event.SetGestureStart(start);
        event.SetGestureEnd(!start);
        (void)injectInput(*viewport.GetEventHandler(), event);
    }
}

// The numbers as "a, b, c" (libstdc++ 14 has no range formatting).
std::string joined(const std::vector<std::uint32_t>& values) {
    std::string out;
    for (const std::uint32_t value : values) {
        out += (out.empty() ? "" : ", ") + std::to_string(value);
    }
    return out;
}

std::string pixelText(const std::optional<dvv::media::ImagePixel>& pixel) {
    return pixel ? std::format("{} {}", pixel->x, pixel->y) : std::string("none");
}

// The Zoom view's shown zoom, whole.
std::uint32_t zoomShown(const ZoomWindow& zoom) {
    return wholeZoom(zoom.viewport().shownView().zoom(), 1);
}

// The Zoom view's clip shown whole in every panel at the shown zoom and, `largest`, not at one
// zoom more.
bool clipWhole(const ZoomWindow& zoom, bool& largest) {
    const Viewport& view = zoom.viewport();
    const dvv::platform::PixelSize clip = view.imageSize();
    const std::uint64_t z = zoomShown(zoom);
    bool whole = view.panels() > 0;
    largest = z == WHOLE_ZOOM_MAX;
    for (std::size_t i = 0; i < view.panels(); ++i) {
        const dvv::media::PixelRect p = view.panelPicture(i);
        whole = whole && clip.width * z <= p.width && clip.height * z <= p.height;
        largest = largest || clip.width * (z + 1) > p.width || clip.height * (z + 1) > p.height;
    }
    return whole;
}

// The Zoom view frames `want` (§4.4, P9): in Cell on its grid cell at the shown zoom (cellView;
// the pixel centred when no level's cell fits), in Follow on the pixel; and the image pixel under
// the middle of the pixel's cell is `want` in every panel.
bool framesShown(const MainFrame& frame, const ZoomWindow& zoom, dvv::media::ImagePixel want,
                 std::string& seen) {
    const Viewport& view = zoom.viewport();
    dvv::media::PointD centre = followCentre(want);
    if (frame.inspector().mode() == InspectorMode::Cell) {
        if (const std::optional<CellView> cell = cellView(want, frame.grid(), zoomShown(zoom),
                                                          view.pictureSize(), view.imageSize())) {
            centre = cell->centre;
        }
    }
    const dvv::media::ViewRequest& now = view.view();
    const bool framed = now.mode == dvv::media::ViewMode::Zoom && now.centre == centre;
    const double scale = view.GetContentScaleFactor();
    const auto window_of = [scale](double backing) {
        return static_cast<int>(std::lround((backing / scale) - 0.5));
    };
    const dvv::media::ViewTransform shown = view.shownView();
    const dvv::media::AxisView ax = dvv::media::axisX(shown);
    const dvv::media::AxisView ay = dvv::media::axisY(shown);
    const double mid_x =
        (dvv::media::firstPixelOf(ax, want.x) + dvv::media::firstPixelOf(ax, want.x + 1)) / 2.0;
    const double mid_y =
        (dvv::media::firstPixelOf(ay, want.y) + dvv::media::firstPixelOf(ay, want.y + 1)) / 2.0;
    std::uint32_t at_mark = 0;
    for (std::uint32_t k = 0; k < view.panels(); ++k) {
        const dvv::media::PixelRect picture = view.panelPicture(k);
        at_mark += view.pixelAt(
                       wxPoint(window_of(picture.x + mid_x), window_of(picture.y + mid_y))) == want
                       ? 1
                       : 0;
    }
    seen = std::format(
        "shown pixel {}, zoom {}, centre {} {} (want {} {}), the pixel at its mark in {} of {} "
        "panels",
        pixelText(want), zoomShown(zoom), now.centre.x, now.centre.y, centre.x, centre.y, at_mark,
        view.panels());
    return framed && at_mark == view.panels();
}

// Copy with `focus` focused puts copyText() of the values on the clipboard.
SelfTestStep copyStep(MainFrame& frame, const std::shared_ptr<InspectorState>& st, std::string name,
                      bool numbers_fit) {
    return step(
        std::move(name),
        [&frame, st, numbers_fit] {
            const std::optional<PixelValues>& values = frame.pixelValues();
            st->expected = values ? copyText(*values) : std::string();
            st->copied.reset();
            dialogHooks().clipboard = [st](std::string text) { st->copied = std::move(text); };
            if (ZoomWindow* const zoom = frame.zoomWindow()) {
                copyInZoomWindow(frame, numbers_fit ? zoom->numbersFitForTest()
                                                    : static_cast<wxWindow&>(zoom->viewport()));
            }
        },
        [st](std::string& seen) {
            seen = std::format("copied '{}' (want '{}')", st->copied.value_or("nothing"),
                               st->expected);
            return !st->expected.empty() && st->copied == st->expected;
        });
}

// The metrics the Zoom view's numbers must use (its value strip's advance, its height plus 1 pt, a
// 2 pt pad) and the numbersFitZoom for them; nullopt without the window, a stream or a value strip.
struct WantNumbers {
    ValueMetrics metrics;
    std::uint32_t fit = 0;
};
std::optional<WantNumbers> wantNumbers(const MainFrame& frame, const ZoomWindow* zoom) {
    if (zoom == nullptr || frame.streams().empty() || !zoom->viewport().valueGlyphs()) {
        return std::nullopt;
    }
    const Viewport& view = zoom->viewport();
    const dvv::gpu::GlyphStrip& strip = *view.valueGlyphs();
    const double backing_per_point = native_policy::dipPerFontPoint() * view.backingPerDip();
    const auto backing = [backing_per_point](double points) {
        return static_cast<std::uint32_t>(std::lround(backing_per_point * points));
    };
    const ValueMetrics metrics{.advance = strip.advance,
                               .lineHeight = strip.height + backing(1),
                               .padX = backing(2),
                               .padY = backing(2)};
    const ClipGrid clip{.image = view.imageSize(),
                        .format = frame.streams().front().info.desc.format};
    return WantNumbers{.metrics = metrics,
                       .fit = numbersFitZoom(metrics, frame.inspector().layout(), clip)};
}

// The Zoom view at numbersFitZoom with the numbers on and the metrics of wantNumbers.
bool numbersOpen(const MainFrame& frame, const ZoomWindow* zoom, std::string& seen) {
    const std::optional<WantNumbers> want = wantNumbers(frame, zoom);
    if (!want) {
        seen = "no Zoom window, stream or value strip";
        return false;
    }
    const InspectParams& inspect = zoom->viewport().inspect();
    const std::uint32_t whole = wholeZoom(zoom->viewport().shownView().zoom(), 1);
    seen = std::format("numbers {}, metrics {} {} {} {} (want {} {} {} {}), zoom {} (fit {})",
                       inspect.numbers, inspect.metrics.advance, inspect.metrics.lineHeight,
                       inspect.metrics.padX, inspect.metrics.padY, want->metrics.advance,
                       want->metrics.lineHeight, want->metrics.padX, want->metrics.padY, whole,
                       want->fit);
    return inspect.numbers && inspect.metrics == want->metrics && whole == want->fit;
}

}  // namespace

std::vector<SelfTestStep> inspectorScenario(SelfTest& test) {
    MainFrame& frame = test.frame();
    auto st = std::make_shared<InspectorState>();
    std::vector<SelfTestStep> steps;
    // The main frame raised while the stream loads, so that the pin step's "stays active" check is
    // not trivial. No step waits for the activation: a process the OS does not let take it (one
    // started from a background shell, X11 without a window manager) stays inactive, and the pin
    // step then skips that check with a note.
    steps.push_back(step(
        "stream", [&frame] { frame.Raise(); },
        [&frame](std::string& seen) {
            const bool ready = frame.isAvailable(Command::Pipette);
            seen = std::format("pipette available {}", ready);
            return ready;
        }));
    // 1. P turns the pipette on: the viewport's tool and the bar's toggle follow.
    steps.push_back(step(
        "pipette_on", [&frame] { (void)sendKey(frame, 'P'); },
        [&frame](std::string& seen) {
            const auto* const toggle = dynamic_cast<const wxBitmapToggleButton*>(
                &frame.transportBar().buttonForTest(Command::Pipette));
            const bool value = toggle != nullptr && toggle->GetValue();
            const bool tool = frame.viewport().tool() == ViewportTool::Pipette;
            seen =
                std::format("tool Pipette {}, toggle {}, value {}", tool, toggle != nullptr, value);
            return tool && value;
        }));
    // 2. A click on image pixel (20, 10) pins it through the main viewport's pick path: the Zoom
    // window is shown without being activated, the main frame keeps its activation. The first pin
    // opens the Zoom view at numbersFitZoom with the numbers on.
    steps.push_back(step(
        "pin",
        [&frame, st] {
            st->activeBefore = frame.IsActive();
            st->clicked = clickPixel(frame.viewport(), PIN);
        },
        [&frame, st](std::string& seen) {
            const std::optional<dvv::media::ImagePixel> pin = frame.inspector().pin();
            ZoomWindow* const zoom = frame.zoomWindow();
            const bool shown = zoom != nullptr && zoom->IsShown();
            const bool zoom_active = zoom != nullptr && zoom->IsActive();
            const bool active = frame.IsActive();
            std::string numbers_seen;
            const bool numbers = numbersOpen(frame, zoom, numbers_seen);
            seen = std::format(
                "clicked {}, pin {}, Zoom shown {}, Zoom active {}, frame active {}{}; {}",
                st->clicked, pin ? std::format("{} {}", pin->x, pin->y) : "none", shown,
                zoom_active, active,
                st->activeBefore ? std::string()
                                 : " (activation not granted: the stays-active check is skipped)",
                numbers_seen);
            // Never Fit after the window opens.
            const bool zoomed =
                zoom != nullptr && zoom->viewport().view().mode == dvv::media::ViewMode::Zoom;
            return pin == PIN && shown && !zoom_active && (!st->activeBefore || active) &&
                   numbers && zoomed;
        }));
    // 2b. One whole step below the fit zoom the numbers are off and the metrics stay. At Fit, whose
    // zoom is not whole, the numbers follow the unrounded zoom. Back at the fit zoom they are on.
    steps.push_back(step(
        "numbers off below fit",
        [&frame, st] {
            ZoomWindow* const zoom = frame.zoomWindow();
            const std::optional<WantNumbers> want = wantNumbers(frame, zoom);
            if (!want || want->fit < 2) {
                return;
            }
            Viewport& view = zoom->viewport();
            const auto at = [&](double zoom_at) {
                view.setView({.mode = dvv::media::ViewMode::Zoom,
                              .zoom = zoom_at,
                              .centre = followCentre(PIN),
                              .image = view.imageSize()});
            };
            at(static_cast<double>(want->fit - 1));
            st->belowOff = !view.inspect().numbers && view.inspect().metrics == want->metrics;
            view.setView({});  // Fit
            st->fitRule = view.inspect().numbers ==
                          (view.shownView().zoom() >= static_cast<double>(want->fit));
            at(static_cast<double>(want->fit));
        },
        [&frame, st](std::string& seen) {
            const bool open = numbersOpen(frame, frame.zoomWindow(), seen);
            seen = std::format("off below fit {}, Fit follows the unrounded zoom {}; {}",
                               st->belowOff, st->fitRule, seen);
            return st->belowOff && st->fitRule && open;
        }));
    // 3. The readback of the pinned pixel fills the values: the host's codes, in the PIXEL field
    // and the Zoom header.
    steps.push_back(
        step("values", {}, [&frame](std::string& seen) { return valuesMatch(frame, PIN, seen); }));
    // 4. A pin move shows no stale values: right after the click the values, PIXEL and the header
    // are blank, a late readback of the old pixel shows nothing, then the readback of the new
    // pixel fills them. The raw words stay in place as blank lines meanwhile, so the Zoom picture
    // keeps its size.
    steps.push_back(step(
        "pin move: no stale values",
        [&frame, st] {
            const ZoomWindow* const zoom = frame.zoomWindow();
            st->rawBefore = zoom != nullptr ? zoom->rawLinesForTest().size() : 0;
            st->clicked = clickPixel(frame.viewport(), MOVED_PIN);
            st->pending = valuesBlank(frame);
            if (zoom != nullptr) {
                const std::vector<RawWordLine> raw = zoom->rawLinesForTest();
                st->rawKept = raw.size() == st->rawBefore &&
                              std::ranges::all_of(raw, [](const RawWordLine& line) {
                                  return line.text.empty() && line.runs.empty();
                              });
            }
            const dvv::media::FormatId format = frame.streams().front().info.desc.format;
            ProbeSample late{.pixel = PIN,
                             .count = dvv::media::formatDesc(format).componentCount,
                             .codes = {},
                             .words = {}};
            frame.readbackForTest({.index = 0, .probe = late});
            st->lateShown = frame.pixelValues().has_value();
        },
        [&frame, st](std::string& seen) {
            const bool match = valuesMatch(frame, MOVED_PIN, seen);
            seen = std::format(
                "clicked {}, blank after the click {}, raw lines {} kept blank {}, late readback "
                "shown {}; {}",
                st->clicked, st->pending, st->rawBefore, st->rawKept, st->lateShown, seen);
            return st->clicked && st->pending && st->rawBefore > 0 && st->rawKept &&
                   !st->lateShown && frame.inspector().pin() == MOVED_PIN && match;
        }));
    // 5. View -> Zoom Window toggles: it hides the window (the pin stays, the values and PIXEL go
    // blank: no readbacks while hidden), then shows it again and the values come back.
    steps.push_back(step(
        "Zoom Window hides",
        [&frame, st] {
            (void)sendMenu(frame, menuId(Command::ZoomWindow));
            st->hiddenBlank =
                !frame.pixelValues() && frame.statusTexts()[MainFrame::PIXEL_FIELD].empty();
        },
        [&frame, st](std::string& seen) {
            const bool shown = frame.windowShown(FloatingKind::Zoom);
            seen = std::format("windowShown {}, blank {}, pin kept {}", shown, st->hiddenBlank,
                               frame.inspector().pin() == MOVED_PIN);
            return !shown && st->hiddenBlank && frame.inspector().pin() == MOVED_PIN;
        }));
    steps.push_back(step(
        "Zoom Window shows", [&frame] { (void)sendMenu(frame, menuId(Command::ZoomWindow)); },
        [&frame](std::string& seen) {
            const bool shown = frame.windowShown(FloatingKind::Zoom);
            const bool match = valuesMatch(frame, MOVED_PIN, seen);
            // The view is still at the fit zoom: the numbers come back with the value strip.
            std::string numbers_seen;
            const bool numbers = numbersOpen(frame, frame.zoomWindow(), numbers_seen);
            seen = std::format("windowShown {}; {}; {}", shown, seen, numbers_seen);
            return shown && match && numbers;
        }));
    // 5b. A hand pan survives a hide and a show: the strips that go and come back are no layout
    // change, so the Zoom view is not re-framed on the pin.
    steps.push_back(step(
        "hand pan, then Zoom Window hides",
        [&frame, st] {
            ZoomWindow* const zoom = frame.zoomWindow();
            if (zoom == nullptr) {
                return;
            }
            Viewport& view = zoom->viewport();
            const dvv::media::PointD centre = view.view().centre;
            view.setView({.mode = dvv::media::ViewMode::Zoom,
                          .zoom = static_cast<double>(zoomShown(*zoom)),
                          .centre = {.x = centre.x + 3, .y = centre.y + 2},
                          .image = view.imageSize()});
            st->panned = view.view();
            (void)sendMenu(frame, menuId(Command::ZoomWindow));
        },
        [&frame](std::string& seen) {
            const bool shown = frame.windowShown(FloatingKind::Zoom);
            seen = std::format("windowShown {}", shown);
            return !shown;
        }));
    steps.push_back(step(
        "Zoom Window shows again with the pan",
        [&frame] { (void)sendMenu(frame, menuId(Command::ZoomWindow)); },
        [&frame, st](std::string& seen) {
            const ZoomWindow* const zoom = frame.zoomWindow();
            const bool shown = frame.windowShown(FloatingKind::Zoom);
            const bool kept = zoom != nullptr && zoom->viewport().view() == st->panned;
            seen = std::format("windowShown {}, pan kept {}", shown, kept);
            return shown && kept;
        }));
    // 6. A pin move in the same main view redraws only the markers there. The step waits until the
    // main view has drawn the final markers (the pin and the outline of the Zoom view, which
    // follows the pin), then requires that the whole move took no full picture pass and one or two
    // marker-only records: the click's setMarkers, and a second one when the Zoom view's change
    // reaches updateMarkers after that record.
    steps.push_back(step(
        "markers",
        [&frame, st] {
            st->recorded = frame.viewport().lastRecorded().value_or(RecordedInfo{});
            st->clicked = clickPixel(frame.viewport(), MARKED_PIN, false);
        },
        [&frame, st](std::string& seen) {
            const RecordedInfo now = frame.viewport().lastRecorded().value_or(RecordedInfo{});
            const MarkerParams& markers = frame.viewport().markers();
            const bool settled = now.markers == pixelCells(markers);
            const std::uint64_t marker_records = now.markerRecords - st->recorded.markerRecords;
            // The copy step after this one reads the values: wait for those of the new pin.
            std::string values_seen;
            const bool values = valuesMatch(frame, MARKED_PIN, values_seen);
            seen = std::format(
                "clicked {}, pin marker {}, outline {}, drawn {}, marker records +{}, picture "
                "records +{}",
                st->clicked, markers.pin == MARKED_PIN, markers.outline.has_value(), settled,
                marker_records, now.pictureRecords - st->recorded.pictureRecords);
            seen += std::format("; values of the pin {}: {}", values, values_seen);
            return st->clicked && frame.inspector().pin() == MARKED_PIN &&
                   markers.pin == MARKED_PIN && markers.outline.has_value() && settled &&
                   marker_records >= 1 && marker_records <= 2 &&
                   now.pictureRecords == st->recorded.pictureRecords && values;
        }));
    // 7. Copy in the Zoom window puts the values' text on the clipboard, also with the "Numbers
    // fit" button focused.
    steps.push_back(copyStep(frame, st, "copy", false));
    steps.push_back(copyStep(frame, st, "copy with Numbers fit focused", true));
    // 8. In the Zoom window the arrows move the pin, not the player: from the pin at (20, 10),
    // Right gives (21, 10), Shift+Down one chroma block down, (21, 12).
    steps.push_back(step(
        "arrows",
        [&frame, st] {
            st->clicked = clickPixel(frame.viewport(), PIN, false);
            st->position = frame.session().status().player.position;
            st->taken = zoomKey(frame, WXK_RIGHT);
            st->afterRight = frame.inspector().pin();
            st->taken = zoomKey(frame, WXK_DOWN, true) && st->taken;
        },
        [&frame, st](std::string& seen) {
            const std::optional<dvv::media::ImagePixel> pin = frame.inspector().pin();
            const std::int64_t position = frame.session().status().player.position;
            seen = std::format(
                "clicked {}, keys taken {}, after Right {}, after Shift+Down {}, position {} (at "
                "the start {})",
                st->clicked, st->taken, pixelText(st->afterRight), pixelText(pin), position,
                st->position);
            return st->clicked && st->taken &&
                   st->afterRight == dvv::media::ImagePixel{.x = 21, .y = 10} &&
                   pin == dvv::media::ImagePixel{.x = 21, .y = 12} && position == st->position;
        }));
    // 9. F, S and Shift+P in the Zoom window turn Cell, Planes and Live on, with their toggles;
    // Shift+P in the main window turns Live off again.
    steps.push_back(step(
        "toggles",
        [&frame, st] {
            st->taken = zoomKey(frame, 'F') && zoomKey(frame, 'S') && zoomKey(frame, 'P', true);
            const ZoomWindow* const zoom = frame.zoomWindow();
            st->liveOn = frame.inspector().live() && zoom != nullptr &&
                         zoom->liveForTest().GetValue() && frame.windowShown(FloatingKind::Zoom);
            st->taken = sendKey(frame, 'P', true) && st->taken;
        },
        [&frame, st](std::string& seen) {
            const ZoomWindow* const zoom = frame.zoomWindow();
            if (zoom == nullptr) {
                seen = "no Zoom window";
                return false;
            }
            const bool cell =
                frame.inspector().mode() == InspectorMode::Cell && zoom->cellForTest().GetValue();
            const bool planes = frame.inspector().layout() == InspectorLayout::Planes &&
                                zoom->planesForTest().GetValue() &&
                                zoom->viewport().inspect().panels == PanelKind::Planes;
            const bool live_off = !frame.inspector().live() && !zoom->liveForTest().GetValue();
            seen = std::format("keys taken {}, Cell {}, Planes {}, Live on {}, then off {}",
                               st->taken, cell, planes, st->liveOn, live_off);
            return st->taken && cell && planes && st->liveOn && live_off;
        }));
    // F and S again: Follow and Structure, as the later steps expect.
    steps.push_back(step(
        "toggles back", [&frame, st] { st->taken = zoomKey(frame, 'F') && zoomKey(frame, 'S'); },
        [&frame, st](std::string& seen) {
            const ZoomWindow* const zoom = frame.zoomWindow();
            const bool back =
                zoom != nullptr && frame.inspector().mode() == InspectorMode::Follow &&
                frame.inspector().layout() == InspectorLayout::Structure &&
                !zoom->cellForTest().GetValue() && !zoom->planesForTest().GetValue() &&
                zoom->viewport().inspect().panels == PanelKind::Picture;
            seen = std::format("keys taken {}, Follow and Structure {}", st->taken, back);
            return st->taken && back;
        }));
    // 9b. Planes geometry: every panel shows the same image region at the same scale (§4.3), the
    // main view's outline is the region one panel's picture shows (§4.1), a click in any panel
    // pins the pixel under the same spot of the first panel, and Fit cell fits one panel.
    steps.push_back(step(
        "planes geometry",
        [&frame, st] {
            ZoomWindow* const zoom = frame.zoomWindow();
            if (zoom == nullptr || !frame.inspector().pin()) {
                return;
            }
            Viewport& view = zoom->viewport();
            const dvv::media::ImagePixel pin = *frame.inspector().pin();
            st->structurePanels = view.panels();
            st->structurePicture = view.panelPicture(0);
            // A zoom where the outline is smaller than the 64 x 48 clip in Structure, and an
            // image pixel spans several window points.
            const auto ceil_div = [](std::uint32_t a, std::uint32_t b) { return (a + b - 1) / b; };
            st->geometryZoom = std::max({16U, ceil_div(st->structurePicture.width, 32),
                                         ceil_div(st->structurePicture.height, 24)});
            const dvv::media::ViewRequest request{.mode = dvv::media::ViewMode::Zoom,
                                                  .zoom = static_cast<double>(st->geometryZoom),
                                                  .centre = {.x = pin.x + 0.5, .y = pin.y + 0.5},
                                                  .image = view.imageSize()};
            view.setView(request);
            st->structureOutline = frame.viewport().markers().outline;
            st->taken = zoomKey(frame, 'S');
            st->planesPanels = view.panels();
            st->panelPicture = view.panelPicture(0);
            view.setView(request);
            st->planesOutline = frame.viewport().markers().outline;
            // A window point in the first panel whose image pixel is the same 2 window points
            // around it, so the same spot of another panel (within a window point) shows it too.
            const double scale = view.GetContentScaleFactor();
            const auto window_of = [scale](double backing) {
                return static_cast<int>(std::lround(backing / scale - 0.5));
            };
            const auto backing_of = [scale](int window) { return (window + 0.5) * scale; };
            const dvv::media::PixelRect first = st->panelPicture;
            std::optional<wxPoint> spot;
            for (int y = window_of(first.y) + 3; !spot && y + 3 < window_of(first.y + first.height);
                 ++y) {
                for (int x = window_of(first.x) + 3;
                     !spot && x + 3 < window_of(first.x + first.width); ++x) {
                    const std::optional<dvv::media::ImagePixel> here = view.pixelAt({x, y});
                    bool steady = here.has_value();
                    for (int dy = -2; steady && dy <= 2; ++dy) {
                        for (int dx = -2; steady && dx <= 2; ++dx) {
                            steady = view.pixelAt({x + dx, y + dy}) == here;
                        }
                    }
                    if (steady) {
                        spot = wxPoint(x, y);
                    }
                }
            }
            st->spotPixel = spot ? view.pixelAt(*spot) : std::nullopt;
            st->panelPins.clear();
            for (std::uint32_t k = 0; spot && k < st->planesPanels; ++k) {
                const dvv::media::PixelRect panel = view.panelPicture(k);
                const wxPoint at(window_of(panel.x + backing_of(spot->x) - first.x),
                                 window_of(panel.y + backing_of(spot->y) - first.y));
                view.setView(request);  // the click's pin moves the view: the same view for each
                sendViewportMouse(view, wxEVT_LEFT_DOWN, at, true, false);
                sendViewportMouse(view, wxEVT_LEFT_UP, at, false, false);
                st->panelPins.push_back(frame.inspector().pin());
            }
            // Fit cell in Planes: one panel's picture, not the whole client.
            frame.setFocusForTest(&view);
            (void)sendMenu(frame, menuId(Command::FitToWindow));
            st->fitCell = zoomShown(*zoom);
            st->wantFitCell = fitCellZoom(frame.grid(), view.panelPicture(0).size());
            st->wholeFitCell = fitCellZoom(frame.grid(), st->structurePicture.size());
            // Structure again, the pin back where the later steps expect it.
            st->taken = zoomKey(frame, 'S') && st->taken;
            // Numbers fit in the Zoom view. zoomKey() gave the focus back, and without it the menu
            // goes to the key window's view: the main one when another app is in front, at 1:1,
            // where a high-density display shows some pixels under no window point.
            const dvv::media::ViewRequest main_view = frame.viewport().view();
            frame.setFocusForTest(&view);
            (void)sendMenu(frame, menuId(Command::ActualSize));
            frame.setFocusForTest(nullptr);
            st->numbersFit = zoomShown(*zoom);
            const std::optional<WantNumbers> numbers = wantNumbers(frame, zoom);
            st->wantNumbersFit = numbers ? std::max(numbers->fit, view.minWholeZoom()) : 0;
            st->mainKept = frame.viewport().view() == main_view;
            st->clicked = clickPixel(frame.viewport(), pin, false);
        },
        [&frame, st](std::string& seen) {
            const ZoomWindow* const zoom = frame.zoomWindow();
            if (zoom == nullptr || st->geometryZoom == 0) {
                seen = "no Zoom window or no pin";
                return false;
            }
            const double z = st->geometryZoom;
            // One region's outline: the pixels a picture of `size` shows at the zoom, partial
            // pixels at both ends included.
            const auto fits = [z](const std::optional<ImageRect>& outline,
                                  dvv::media::PixelRect picture) {
                const auto along = [z](std::uint32_t pixels, std::uint32_t extent) {
                    // A span of `want` image pixels shows ceil(want) of them, one more when it
                    // starts inside one; under 8 that slack would be most of the answer.
                    const double want = extent / z;
                    return want >= 8 && pixels >= std::ceil(want) && pixels <= std::ceil(want) + 1;
                };
                return outline && along(outline->width, picture.width) &&
                       along(outline->height, picture.height);
            };
            const bool structure = fits(st->structureOutline, st->structurePicture);
            const bool planes = fits(st->planesOutline, st->panelPicture);
            // Independent of the Planes geometry: the Planes outline is the Structure outline
            // scaled by the panel's share of the client, and smaller than it.
            const auto share = [](std::uint32_t outline, std::uint32_t panel, std::uint32_t whole) {
                return whole != 0 ? outline * static_cast<double>(panel) / whole : 0.0;
            };
            const bool smaller = st->structureOutline && st->planesOutline &&
                                 st->planesOutline->width * st->planesOutline->height <
                                     st->structureOutline->width * st->structureOutline->height;
            const bool scaled =
                smaller &&
                std::abs(static_cast<double>(st->planesOutline->width) -
                         share(st->structureOutline->width, st->panelPicture.width,
                               st->structurePicture.width)) <= 1.5 &&
                std::abs(static_cast<double>(st->planesOutline->height) -
                         share(st->structureOutline->height, st->panelPicture.height,
                               st->structurePicture.height)) <= 1.5;
            const bool fit_differs = st->wantFitCell != st->wholeFitCell;
            // The pins are the pixel under the spot, not only equal to one another.
            bool same = st->panelPins.size() == st->planesPanels && st->planesPanels > 1 &&
                        st->spotPixel.has_value();
            for (const std::optional<dvv::media::ImagePixel>& pin : st->panelPins) {
                same = same && pin.has_value() && pin == st->spotPixel;
            }
            std::string pins;
            for (const std::optional<dvv::media::ImagePixel>& pin : st->panelPins) {
                pins += std::format(" ({})", pixelText(pin));
            }
            const auto rect = [](const std::optional<ImageRect>& r) {
                return r ? std::format("{}x{}", r->width, r->height) : std::string("none");
            };
            const std::uint32_t panels_after = zoom->viewport().panels();
            seen = std::format(
                "zoom {}, panels {} -> {} -> {}, Structure picture {}x{} outline {} ({}), panel "
                "picture {}x{} outline {} ({}), pins per panel{} (same {}), scaled {}, Fit cell {} "
                "(want {}, "
                "the whole client's {}, differ {}), keys taken {}, Actual Size zoom {} (want {}, "
                "the main view kept {}), pin back {}",
                st->geometryZoom, st->structurePanels, st->planesPanels, panels_after,
                st->structurePicture.width, st->structurePicture.height, rect(st->structureOutline),
                structure, st->panelPicture.width, st->panelPicture.height, rect(st->planesOutline),
                planes, pins, same, scaled, st->fitCell, st->wantFitCell, st->wholeFitCell,
                fit_differs, st->taken, st->numbersFit, st->wantNumbersFit, st->mainKept,
                st->clicked);
            return st->structurePanels == 1 && st->planesPanels == 3 && panels_after == 1 &&
                   structure && planes && scaled && same && fit_differs &&
                   st->fitCell == st->wantFitCell && st->taken && st->wantNumbersFit != 0 &&
                   st->numbersFit == st->wantNumbersFit && st->mainKept && st->clicked;
        }));
    // 9c. The GUI's panel count is the renderer's: once the frame settles, the panels the renderer
    // drew are the Zoom viewport's panels(), in Planes and in Structure.
    const auto panels_match = [&frame](std::uint32_t want) {
        return [&frame, want](std::string& seen) {
            const ZoomWindow* const zoom = frame.zoomWindow();
            if (zoom == nullptr) {
                seen = "no Zoom window";
                return false;
            }
            const std::uint32_t gui = zoom->viewport().panels();
            const std::uint32_t drawn =
                zoom->viewport().lastRecorded().value_or(RecordedInfo{}).panels;
            seen = std::format("GUI panels {}, renderer panels {}, want {}", gui, drawn, want);
            return gui == want && drawn == want;
        };
    };
    steps.push_back(step(
        "planes panels match the renderer", [&frame, st] { st->taken = zoomKey(frame, 'S'); },
        panels_match(3)));
    steps.push_back(step(
        "structure panels match the renderer", [&frame, st] { st->taken = zoomKey(frame, 'S'); },
        panels_match(1)));
    // 10. Space in the Zoom window plays, Space again pauses.
    steps.push_back(step(
        "space",
        [&frame, st] {
            st->phase = 0;
            st->taken = zoomKey(frame, WXK_SPACE);
        },
        [&frame, st](std::string& seen) {
            const bool playing = frame.session().status().player.playing;
            seen =
                std::format("phase {}, keys taken {}, playing {}", st->phase, st->taken, playing);
            if (st->phase == 0) {
                if (st->taken && playing) {
                    st->phase = 1;
                    st->taken = zoomKey(frame, WXK_SPACE);  // checked in a later turn
                }
                return false;
            }
            return st->taken && !playing;
        }));
    // A toolbar toggle with the focus keeps Space (controlKeepsKey; a button keeps no arrows),
    // so Space presses it: no playback, the pin stays. Where the OS gives buttons no keyboard focus
    // (macOS without full keyboard access) the same rule holds for a focus set by the test.
    steps.push_back(step(
        "Space at a toggle",
        [&frame, st] {
            st->keyKept = false;
            ZoomWindow* const zoom = frame.zoomWindow();
            const std::optional<dvv::media::ImagePixel> pin = frame.inspector().pin();
            if (zoom == nullptr || !pin) {
                return;
            }
            wxToggleButton& toggle = zoom->cellForTest();
            frame.setFocusForTest(&toggle);
            const bool space = sendKey(toggle, WXK_SPACE);
            frame.setFocusForTest(nullptr);
            st->keyKept = !space && frame.inspector().pin() == pin;
        },
        [&frame, st](std::string& seen) {
            const bool playing = frame.session().status().player.playing;
            seen = std::format("Space left to the toggle {}, playing {}", st->keyKept, playing);
            return st->keyKept && !playing;
        }));
    // 11. With the Zoom window in front the View commands act on its view; the main view's zoom
    // stays. The zoom floor (§4.5, minZoom: 3 x 3 major cells per panel) holds on every path:
    // Actual Size gives numbersFitZoom raised to it, Zoom In the next whole stop; a request of 1
    // shows the floor, where Zoom Out is off and Zoom In on; Zoom Out from 256 takes the whole
    // stops down to the floor and lands on it; a pinch and Ctrl+wheel out stop there; a pinch of
    // factor 1 leaves the view; a Fit request is a Zoom request at the floor (never Fit); Fit cell
    // is fitCellZoom raised to it; a request just under the stop past the floor shows that stop,
    // so Zoom In gives the next one. Zoom In is off at 256.
    steps.push_back(step(
        "view_keys",
        [&frame, st] {
            ZoomWindow* const zoom = frame.zoomWindow();
            const std::optional<WantNumbers> want = wantNumbers(frame, zoom);
            if (zoom == nullptr || !want) {
                return;
            }
            Viewport& view = zoom->viewport();
            st->mainZoom = frame.viewport().shownView().zoom();
            st->floor = view.minWholeZoom();
            frame.setFocusForTest(&view);
            const auto at = [&](double zoom_at) {
                view.setView({.mode = dvv::media::ViewMode::Zoom,
                              .zoom = zoom_at,
                              .centre = view.view().centre,
                              .image = view.imageSize()});
            };
            at(1);
            st->atOne = zoomShown(*zoom);
            st->outOffAtFloor =
                frame.isAvailable(Command::ZoomIn) && !frame.isAvailable(Command::ZoomOut);
            const dvv::media::ViewRequest at_floor = view.view();
            pinch(view, 1.0);
            st->pinchOneKeeps = view.view() == at_floor;
            (void)sendMenu(frame, menuId(Command::ActualSize));
            st->actualZoom = zoomShown(*zoom);
            st->wantIn = nextWholeZoom(st->actualZoom, true, st->floor).value_or(0);
            (void)sendMenu(frame, menuId(Command::ZoomIn));
            st->inZoom = zoomShown(*zoom);
            at(256);
            st->inOffAt256 =
                !frame.isAvailable(Command::ZoomIn) && frame.isAvailable(Command::ZoomOut);
            for (std::optional<std::uint32_t> z = nextWholeZoom(256, false, st->floor); z;
                 z = nextWholeZoom(*z, false, st->floor)) {
                st->wantOutStops.push_back(*z);
            }
            for (std::size_t i = 0;
                 i < WHOLE_ZOOM_STEPS.size() && frame.isAvailable(Command::ZoomOut); ++i) {
                (void)sendMenu(frame, menuId(Command::ZoomOut));
                st->outStops.push_back(zoomShown(*zoom));
            }
            st->outLast = zoomShown(*zoom);
            at(256);
            pinch(view, 1.0 / 1024);
            st->pinchOut = zoomShown(*zoom);
            if (native_policy::ctrlWheelZooms()) {
                at(256);
                constexpr int NOTCHES = 40, WHEEL_NOTCH = 120;
                wxMouseEvent event(wxEVT_MOUSEWHEEL);
                event.SetEventObject(&view);
                event.SetId(view.GetId());
                event.SetPosition(wxPoint(view.GetClientSize().x / 2, view.GetClientSize().y / 2));
                event.m_wheelRotation = -NOTCHES * WHEEL_NOTCH;
                event.m_wheelDelta = WHEEL_NOTCH;
                event.m_linesPerAction = 3;
                event.m_columnsPerAction = 3;
                event.m_wheelAxis = wxMOUSE_WHEEL_VERTICAL;
                event.m_controlDown = true;
                (void)injectInput(*view.GetEventHandler(), event);
                st->wheelOut = zoomShown(*zoom);
            }
            view.setView({});  // Fit
            std::vector<dvv::platform::PixelSize> pictures;
            for (std::size_t i = 0; i < view.panels(); ++i) {
                const dvv::media::PixelRect p = view.panelPicture(i);
                pictures.push_back({.width = p.width, .height = p.height});
            }
            const std::uint32_t want_fit =
                std::max(wholeFitZoom(pictures, view.imageSize()), st->floor);
            st->fitIsZoom =
                view.view().mode == dvv::media::ViewMode::Zoom && zoomShown(*zoom) == want_fit;
            (void)sendMenu(frame, menuId(Command::FitToWindow));
            st->fitCellShown = zoomShown(*zoom);
            st->wantCell =
                std::max(fitCellZoom(frame.grid(), view.panelPicture(0).size()), st->floor);
            const std::uint32_t past = nextWholeZoom(st->floor, true, st->floor).value_or(256);
            at(past - 1e-7);
            (void)sendMenu(frame, menuId(Command::ZoomIn));
            st->fromNear = zoomShown(*zoom);
            st->wantFromNear = nextWholeZoom(past, true, st->floor).value_or(0);
            (void)sendMenu(frame, menuId(Command::ActualSize));  // back at the numbers' fit
            frame.setFocusForTest(nullptr);
        },
        [&frame, st](std::string& seen) {
            const std::optional<WantNumbers> want = wantNumbers(frame, frame.zoomWindow());
            const double main_zoom = frame.viewport().shownView().zoom();
            const std::uint32_t want_actual = want ? std::max(want->fit, st->floor) : 0;
            const bool wheel_ok = !st->wheelOut || *st->wheelOut == st->floor;
            seen = std::format(
                "floor {}, at 1 {}, Out off at the floor {}, a pinch of 1 keeps the view {}, "
                "Actual Size {} (want {}), Zoom In {} (want {}), main zoom {} (at the start {}), "
                "In off at 256 {}, Zoom Out stops {} (want {}), pinch out {}, Ctrl+wheel out {}, "
                "Fit request a Zoom at the whole fit raised to the floor {}, Fit cell {} (want "
                "{}), Zoom In from just "
                "under {} (want {})",
                st->floor, st->atOne, st->outOffAtFloor, st->pinchOneKeeps, st->actualZoom,
                want_actual, st->inZoom, st->wantIn, main_zoom, st->mainZoom, st->inOffAt256,
                joined(st->outStops), joined(st->wantOutStops), st->pinchOut,
                st->wheelOut ? std::format("{}", *st->wheelOut) : std::string("n/a"), st->fitIsZoom,
                st->fitCellShown, st->wantCell, st->fromNear, st->wantFromNear);
            return want && st->floor >= 1 && st->atOne == st->floor && st->outOffAtFloor &&
                   st->pinchOneKeeps && st->actualZoom == want_actual && st->wantIn != 0 &&
                   st->inZoom == st->wantIn && main_zoom == st->mainZoom && st->inOffAt256 &&
                   !st->wantOutStops.empty() && st->outStops == st->wantOutStops &&
                   st->outLast == st->floor && st->pinchOut == st->floor && wheel_ok &&
                   st->fitIsZoom && st->fitCellShown == st->wantCell && st->wantFromNear != 0 &&
                   st->fromNear == st->wantFromNear;
        }));
    // 11b. The floor follows P, the major step and the image, and the view is ruled again at once:
    // a view at Structure's floor follows Planes' lower floor (every panel then shows at most
    // 3 x 3 major cells, one zoom lower more), and back in Structure (P about 3 times wider) it
    // rises to Structure's floor; a finer major step raises the floor and the view, the step back
    // lowers both again; a clip of 3 x 3 major cells opens (a Fit request) at the largest whole
    // zoom that shows it whole and zooms out until it is whole, not further; another image size
    // keeps a Zoom request (never Fit).
    steps.push_back(step(
        "zoom_floor",
        [&frame, st] {
            ZoomWindow* const zoom = frame.zoomWindow();
            if (zoom == nullptr) {
                return;
            }
            Viewport& view = zoom->viewport();
            const dvv::media::ViewRequest before = view.view();
            const auto at_one = [&] {
                view.setView({.mode = dvv::media::ViewMode::Zoom,
                              .zoom = 1,
                              .centre = view.view().centre,
                              .image = view.imageSize()});
            };
            const std::uint64_t step3 = std::uint64_t{MIN_ZOOM_CELLS} * frame.grid().levels[0].step;
            // Every panel within `side` image pixels per axis at the shown zoom; `beyond`: one
            // whole zoom lower (none at 1) shows more on an axis of some panel.
            const auto fits = [&](std::uint64_t side, bool& beyond) {
                const std::uint64_t z = zoomShown(*zoom);
                bool within = true;
                beyond = z == 1;
                for (std::size_t i = 0; i < view.panels(); ++i) {
                    const dvv::media::PixelRect p = view.panelPicture(i);
                    within = within && p.width <= side * z && p.height <= side * z;
                    beyond = beyond || std::max(p.width, p.height) > side * (z - 1);
                }
                return within;
            };
            // The clip shown whole in every panel at the shown zoom, and not at one zoom more.
            const auto whole = [&](dvv::platform::PixelSize clip, bool& largest) {
                const std::uint64_t z = zoomShown(*zoom);
                bool shown_whole = true;
                largest = z == WHOLE_ZOOM_MAX;
                for (std::size_t i = 0; i < view.panels(); ++i) {
                    const dvv::media::PixelRect p = view.panelPicture(i);
                    shown_whole =
                        shown_whole && clip.width * z <= p.width && clip.height * z <= p.height;
                    largest = largest || clip.width * (z + 1) > p.width ||
                              clip.height * (z + 1) > p.height;
                }
                return shown_whole;
            };
            // At the floor: the cell floor (3 x 3 major cells, one zoom lower more) or, for a clip
            // of at most 3 x 3 major cells, the clip whole (one zoom more not).
            const auto floor_rule = [&] {
                const dvv::platform::PixelSize clip = view.imageSize();
                bool beyond = false;
                bool largest = false;
                const bool cells = fits(step3, beyond) && beyond;
                const bool small = clip.width <= step3 && clip.height <= step3;
                return cells || (small && whole(clip, largest) && largest);
            };
            frame.setInspectorLayout(InspectorLayout::Structure);
            at_one();
            st->floorBefore = view.minWholeZoom();
            frame.setInspectorLayout(InspectorLayout::Planes);
            st->planesFloor = view.minWholeZoom();
            st->planesAtFloor = view.panels() > 1 && zoomShown(*zoom) == st->planesFloor;
            st->planesCells = floor_rule();
            frame.setInspectorLayout(InspectorLayout::Structure);
            st->structureFloor = view.minWholeZoom();
            st->structureAtFloor = zoomShown(*zoom) == st->structureFloor && floor_rule();
            st->gridBefore = frame.grid();
            GridConfig finer = st->gridBefore;
            finer.levels[0].step = std::max<std::uint32_t>(finer.levels[0].step / 2, 2);
            frame.setOverlay(finer, frame.rulers(), OverlaySave::Later);
            st->stepFloor = view.minWholeZoom();
            st->stepAtFloor = zoomShown(*zoom) == st->stepFloor;
            frame.setOverlay(st->gridBefore, frame.rulers(), OverlaySave::Later);
            st->stepBack = zoomShown(*zoom) == view.minWholeZoom();
            const dvv::platform::PixelSize image = view.imageSize();
            const auto side = static_cast<std::uint32_t>(step3);
            const dvv::platform::PixelSize small{.width = side, .height = side};
            view.setImageSize(small);
            view.setView({});  // a Fit request
            bool largest = false;
            st->smallOpen =
                view.view().mode == dvv::media::ViewMode::Zoom && whole(small, largest) && largest;
            at_one();
            st->smallFloor = view.minWholeZoom();
            st->smallAtFloor = zoomShown(*zoom) == st->smallFloor && whole(small, largest) &&
                               !view.zoomStep(view.view(), false, {});
            view.setImageSize({.width = image.width * 2, .height = image.height * 2});
            st->otherImageZoom = view.view().mode == dvv::media::ViewMode::Zoom &&
                                 view.view().image == view.imageSize();
            view.setImageSize(image);
            st->otherImageZoom =
                st->otherImageZoom && view.view().mode == dvv::media::ViewMode::Zoom;
            view.setView(before);
        },
        [&frame, st](std::string& seen) {
            const std::uint32_t floor =
                frame.zoomWindow() != nullptr ? frame.zoomWindow()->viewport().minWholeZoom() : 0;
            seen = std::format(
                "Planes floor {} (followed from {} {}, the floor rule in each panel {}), Structure "
                "floor "
                "{} (at it, the floor rule {}, now {}), finer step floor {} (at it {}, back {}), a "
                "3 x 3 cell clip whole at the open {}, floor {} (at it, whole, Out off {}), "
                "another image size a Zoom request {}",
                st->planesFloor, st->floorBefore, st->planesAtFloor, st->planesCells,
                st->structureFloor, st->structureAtFloor, floor, st->stepFloor, st->stepAtFloor,
                st->stepBack, st->smallOpen, st->smallFloor, st->smallAtFloor, st->otherImageZoom);
            return st->planesAtFloor && st->planesCells && st->planesFloor < st->floorBefore &&
                   st->structureAtFloor && st->structureFloor == floor && st->stepAtFloor &&
                   st->stepFloor > floor && st->stepBack && st->smallOpen && st->smallAtFloor &&
                   st->otherImageZoom;
        }));
    // 11b. A clip of one major cell opened without a pin (a Fit request) sits at the largest whole
    // zoom that shows it whole and stays there when the panel picture changes: Planes and the
    // rulers flipped, then back (the whole fit follows the panel both ways).
    const auto small_check = [&frame](std::string& seen, const char* when) {
        const ZoomWindow* const zoom = frame.zoomWindow();
        bool largest = false;
        const bool whole = zoom != nullptr && clipWhole(*zoom, largest);
        seen += std::format("{}: zoom {}, whole {}, largest {}; ", when,
                            zoom != nullptr ? zoomShown(*zoom) : 0, whole, largest);
        return whole && largest;
    };
    steps.push_back(step(
        "small clip whole in Planes with the rulers flipped",
        [&frame, st, small_check] {
            ZoomWindow* const zoom = frame.zoomWindow();
            st->smallSeen.clear();
            st->smallWhole = false;
            if (zoom == nullptr) {
                return;
            }
            Viewport& view = zoom->viewport();
            st->smallView = view.view();
            st->smallImage = view.imageSize();
            st->smallRulers = frame.rulers();
            frame.setInspectorLayout(InspectorLayout::Structure);
            const std::uint32_t side = frame.grid().levels[0].step;
            view.setImageSize({.width = side, .height = side});
            view.setView({});  // a Fit request, as a new Zoom window's first
            st->smallWhole = small_check(st->smallSeen, "open");
            frame.setInspectorLayout(InspectorLayout::Planes);
            RulerConfig flipped = st->smallRulers;
            flipped.show = !flipped.show;
            frame.setOverlay(frame.grid(), flipped, OverlaySave::Later);
        },
        [&frame, st, small_check](std::string& seen) {
            seen = st->smallSeen;
            const bool now = small_check(seen, "Planes, rulers flipped");
            const bool planes = frame.inspector().layout() == InspectorLayout::Planes &&
                                frame.zoomWindow() != nullptr &&
                                frame.zoomWindow()->viewport().panels() > 1 &&
                                frame.rulers().show != st->smallRulers.show;
            return st->smallWhole && now && planes;
        }));
    steps.push_back(step(
        "small clip whole back in Structure",
        [&frame, st] {
            frame.setOverlay(frame.grid(), st->smallRulers, OverlaySave::Later);
            frame.setInspectorLayout(InspectorLayout::Structure);
        },
        [&frame, st, small_check](std::string& seen) {
            seen.clear();
            const bool now = small_check(seen, "Structure, rulers back");
            ZoomWindow* const zoom = frame.zoomWindow();
            const bool done = now && zoom != nullptr && frame.rulers() == st->smallRulers;
            if (done) {
                zoom->viewport().setImageSize(st->smallImage);
                zoom->viewport().setView(st->smallView);  // the view before
            }
            return done;
        }));
    // 11c. A resize re-rules the view both ways: a smaller window lowers the floor and the view at
    // it follows; the old size raises both again.
    for (const bool smaller : {true, false}) {
        steps.push_back(step(
            smaller ? "zoom_floor smaller window" : "zoom_floor window back",
            [&frame, st, smaller] {
                ZoomWindow* const zoom = frame.zoomWindow();
                if (zoom == nullptr) {
                    return;
                }
                Viewport& view = zoom->viewport();
                if (smaller) {
                    st->windowSize = zoom->GetClientSize();
                    st->resizeView = view.view();
                    st->resizeFloor = view.minWholeZoom();
                    view.setView({.mode = dvv::media::ViewMode::Zoom,
                                  .zoom = 1,
                                  .centre = view.view().centre,
                                  .image = view.imageSize()});
                    zoom->SetClientSize(st->windowSize / 2);
                } else {
                    zoom->SetClientSize(st->windowSize);
                }
            },
            [&frame, st, smaller](std::string& seen) {
                const ZoomWindow* const zoom = frame.zoomWindow();
                if (zoom == nullptr) {
                    seen = "no Zoom window";
                    return false;
                }
                const Viewport& view = zoom->viewport();
                const std::uint32_t floor = view.minWholeZoom();
                const std::uint32_t shown = zoomShown(*zoom);
                seen = std::format("floor {} (at the start {}), zoom {}, mode Zoom {}", floor,
                                   st->resizeFloor, shown,
                                   view.view().mode == dvv::media::ViewMode::Zoom);
                const bool moved = smaller ? floor < st->resizeFloor : floor == st->resizeFloor;
                const bool done =
                    moved && shown == floor && view.view().mode == dvv::media::ViewMode::Zoom;
                if (done && !smaller) {
                    frame.zoomWindow()->viewport().setView(st->resizeView);  // the view before
                }
                return done;
            }));
    }
    // 12. A hand pan stays when a new frame arrives; C centres on the pin again.
    steps.push_back(step(
        "hand_pan",
        [&frame, st] {
            ZoomWindow* const zoom = frame.zoomWindow();
            const std::optional<dvv::media::ImagePixel> pin = frame.inspector().pin();
            if (zoom == nullptr || !pin) {
                return;
            }
            Viewport& view = zoom->viewport();
            const dvv::media::PointD centre = followCentre(*pin);
            view.setView({.mode = dvv::media::ViewMode::Zoom,
                          .zoom = static_cast<double>(zoomShown(*zoom)),
                          .centre = {.x = centre.x + 3, .y = centre.y + 2},
                          .image = view.imageSize()});
            st->panned = view.view();
            st->frameBefore = frame.pixelValues() ? frame.pixelValues()->frame : -1;
            (void)sendMenu(frame, menuId(Command::StepForward));
        },
        [&frame, st](std::string& seen) {
            const ZoomWindow* const zoom = frame.zoomWindow();
            const std::optional<PixelValues>& values = frame.pixelValues();
            const std::int64_t now = values ? values->frame : -1;
            const bool kept = zoom != nullptr && zoom->viewport().view() == st->panned;
            seen =
                std::format("values frame {} (before {}), pan kept {}", now, st->frameBefore, kept);
            return now >= 0 && now != st->frameBefore && kept;
        }));
    steps.push_back(step(
        "C recentres", [&frame, st] { st->taken = zoomKey(frame, 'C'); },
        [&frame, st](std::string& seen) {
            const ZoomWindow* const zoom = frame.zoomWindow();
            const std::optional<dvv::media::ImagePixel> pin = frame.inspector().pin();
            const bool centred =
                zoom != nullptr && pin && zoom->viewport().view().centre == followCentre(*pin);
            seen = std::format("C taken {}, centred on the pin {}", st->taken, centred);
            return st->taken && centred;
        }));
    // 12b. Live on with the pointer off the picture (no probe): the Zoom view stays a Zoom view at
    // or above the floor, never Fit.
    steps.push_back(step(
        "Live with no probe",
        [&frame] {
            if (!frame.isAvailable(Command::LiveProbe) || frame.inspector().live()) {
                return;
            }
            sendViewportMouse(frame.viewport(), wxEVT_LEAVE_WINDOW, wxPoint(0, 0), false, false);
            frame.execute(Command::LiveProbe, CommandSource::Script);
        },
        [&frame](std::string& seen) {
            const ZoomWindow* const zoom = frame.zoomWindow();
            const bool live = frame.inspector().live();
            const bool probe = frame.inspector().probe().has_value();
            const bool zoomed = zoom != nullptr &&
                                zoom->viewport().view().mode == dvv::media::ViewMode::Zoom &&
                                zoomShown(*zoom) >= zoom->viewport().minWholeZoom();
            seen = std::format("Live {}, probe {}, a Zoom view at or above the floor {}", live,
                               probe, zoomed);
            const bool done = live && !probe && zoomed;
            if (done) {
                frame.execute(Command::LiveProbe, CommandSource::Script);  // Live off
            }
            return done;
        }));
    // With Live on, C centres on the pin, not on the probe (§4.1: C acts on the pin); then Live
    // off again.
    steps.push_back(step(
        "C in Live",
        [&frame, st] {
            st->probe.reset();
            if (!frame.isAvailable(Command::LiveProbe) || frame.inspector().live()) {
                return;
            }
            frame.execute(Command::LiveProbe, CommandSource::Script);
            // The pointer on the main view's centre, or on a quarter point where the centre shows
            // the pin: a probe away from the pin.
            Viewport& main = frame.viewport();
            const wxSize client = main.GetClientSize();
            for (const int part : {2, 4}) {
                sendViewportMouse(main, wxEVT_MOTION, wxPoint(client.x / part, client.y / part),
                                  false, false);
                if (frame.inspector().probe() != frame.inspector().pin()) {
                    break;
                }
            }
            st->probe = frame.inspector().probe();
            const ZoomWindow* const zoom = frame.zoomWindow();
            st->onProbe = zoom != nullptr && st->probe &&
                          zoom->viewport().view().centre == followCentre(*st->probe);
            st->taken = zoomKey(frame, 'C');
        },
        [&frame, st](std::string& seen) {
            const ZoomWindow* const zoom = frame.zoomWindow();
            const std::optional<dvv::media::ImagePixel> pin = frame.inspector().pin();
            const bool centred =
                zoom != nullptr && pin && zoom->viewport().view().centre == followCentre(*pin);
            const bool live = frame.inspector().live();
            seen = std::format(
                "probe {} (pin {}), on the probe {}, C taken {}, on the pin {}, "
                "Live {}",
                pixelText(st->probe), pixelText(pin), st->onProbe, st->taken, centred, live);
            const bool done =
                st->probe && st->probe != pin && st->onProbe && st->taken && centred && live;
            if (done) {
                frame.execute(Command::LiveProbe, CommandSource::Script);  // Live off
            }
            return done;
        }));
    // 12c. The Zoom view marks the pixel it shows (§4.1): the probe in Live, else the pin, in
    // Structure and in Planes, Follow and Cell. Once the frame settles, every panel the renderer
    // drew carries the mark, the image pixel under the mark's middle in every panel is the shown
    // pixel, and in Follow so is the one under each panel picture's centre. The last step leaves
    // Follow, Structure and Live off, as the later steps expect.
    struct MarkCase {
        const char* name;
        InspectorLayout layout;
        InspectorMode mode;
        bool live;
    };
    for (const MarkCase& mark : {
             MarkCase{"mark in Live", InspectorLayout::Structure, InspectorMode::Follow, true},
             MarkCase{"mark in Live, Cell", InspectorLayout::Structure, InspectorMode::Cell, true},
             MarkCase{"mark in Live, Planes and Cell", InspectorLayout::Planes, InspectorMode::Cell,
                      true},
             MarkCase{"mark in Live, Planes", InspectorLayout::Planes, InspectorMode::Follow, true},
             MarkCase{"mark of the pin, Planes", InspectorLayout::Planes, InspectorMode::Follow,
                      false},
             MarkCase{"mark of the pin, Planes and Cell", InspectorLayout::Planes,
                      InspectorMode::Cell, false},
             MarkCase{"mark of the pin, Cell", InspectorLayout::Structure, InspectorMode::Cell,
                      false},
             MarkCase{"mark of the pin", InspectorLayout::Structure, InspectorMode::Follow, false},
         }) {
        steps.push_back(step(
            mark.name,
            [&frame, st, mark] {
                st->taken = true;
                st->clicked = true;
                if (!st->markPin) {
                    st->markPin = frame.inspector().pin();
                }
                if (frame.inspector().live() != mark.live &&
                    frame.isAvailable(Command::LiveProbe)) {
                    frame.execute(Command::LiveProbe, CommandSource::Script);
                }
                if (frame.inspector().layout() != mark.layout) {
                    st->taken = zoomKey(frame, 'S') && st->taken;
                }
                if (frame.inspector().mode() != mark.mode) {
                    st->taken = zoomKey(frame, 'F') && st->taken;
                }
                // Then a move of the shown pixel: the Zoom view follows only a move (P9).
                Viewport& main = frame.viewport();
                if (!mark.live && !st->markPin) {
                    st->clicked = false;  // no pin to move
                    return;
                }
                if (!mark.live) {
                    // The pin one pixel right and down of the pin the steps started with, or
                    // back to it, where the later steps expect it.
                    const dvv::media::ImagePixel pin = *st->markPin;
                    const dvv::media::ImagePixel moved{.x = pin.x + 1, .y = pin.y + 1};
                    st->clicked =
                        clickPixel(main, frame.inspector().pin() == moved ? pin : moved, false);
                    return;
                }
                // A probe away from the pin and from the last probe: (32, 24) or (33, 25) of the
                // 64 x 48 clip, the pointer on the first window point that shows it.
                const dvv::media::ImagePixel first{.x = 32, .y = 24};
                const dvv::media::ImagePixel target = frame.inspector().probe() == first
                                                          ? dvv::media::ImagePixel{.x = 33, .y = 25}
                                                          : first;
                const wxSize client = main.GetClientSize();
                bool moved = false;
                for (int y = 0; !moved && y < client.y; ++y) {
                    for (int x = 0; !moved && x < client.x; ++x) {
                        if (main.pixelAt(wxPoint(x, y)) == target) {
                            sendViewportMouse(main, wxEVT_MOTION, wxPoint(x, y), false, false);
                            moved = true;
                        }
                    }
                }
                st->clicked = moved && frame.inspector().probe() == target;
            },
            [&frame, st, mark](std::string& seen) {
                const ZoomWindow* const zoom = frame.zoomWindow();
                const Inspector& inspector = frame.inspector();
                const std::optional<dvv::media::ImagePixel> want =
                    mark.live ? inspector.probe() : inspector.pin();
                if (zoom == nullptr || !want) {
                    seen = std::format("Zoom window {}, shown pixel {}", zoom != nullptr,
                                       pixelText(want));
                    return false;
                }
                const Viewport& view = zoom->viewport();
                const bool state = inspector.live() == mark.live &&
                                   inspector.layout() == mark.layout &&
                                   inspector.mode() == mark.mode && st->taken && st->clicked &&
                                   (!mark.live || inspector.probe() != inspector.pin());
                const bool marked = view.markers().pin == want && inspector.shown() == want;
                const RecordedInfo recorded = view.lastRecorded().value_or(RecordedInfo{});
                const std::uint32_t panels = view.panels();
                // What each panel drew, against its cell worked out here: the pixel, in a chroma
                // panel of the yuv420p10le clip the 2 x 2 sample block (clipped to the image); the
                // cell's backing pixels from the panel picture, the shown offset and the whole
                // zoom; the drawn rects the ring 3 px (the 2 px box and the 1 px rim) outside it.
                const dvv::media::ViewTransform shown_view = view.shownView();
                const double zoom_shown = shown_view.zoom();
                const auto whole_zoom = static_cast<std::int64_t>(zoom_shown);
                const dvv::media::PixelOffset offset = shown_view.offset();
                const dvv::platform::PixelSize image = view.imageSize();
                std::uint32_t drawn_right = 0;
                for (std::uint32_t k = 0; k < recorded.panelMarkers.size() && k < panels; ++k) {
                    const std::uint32_t sub =
                        mark.layout == InspectorLayout::Planes && k != 0 ? 1 : 0;
                    const std::uint32_t cx = (want->x >> sub) << sub;
                    const std::uint32_t cy = (want->y >> sub) << sub;
                    const ImageRect cell{.x = cx,
                                         .y = cy,
                                         .width = std::min(cx + (1U << sub), image.width) - cx,
                                         .height = std::min(cy + (1U << sub), image.height) - cy};
                    const dvv::media::PixelRect picture = view.panelPicture(k);
                    const std::int64_t left = picture.x + offset.x + (cell.x * whole_zoom);
                    const std::int64_t top = picture.y + offset.y + (cell.y * whole_zoom);
                    const std::int64_t right = left + (cell.width * whole_zoom);
                    const std::int64_t bottom = top + (cell.height * whole_zoom);
                    const auto rect = [](std::int64_t x0, std::int64_t y0, std::int64_t x1,
                                         std::int64_t y1) {
                        return dvv::media::PixelRect{.x = static_cast<std::uint32_t>(x0),
                                                     .y = static_cast<std::uint32_t>(y0),
                                                     .width = static_cast<std::uint32_t>(x1 - x0),
                                                     .height = static_cast<std::uint32_t>(y1 - y0)};
                    };
                    const std::vector<dvv::media::PixelRect> ring{
                        rect(left - 3, top - 3, right + 3, top),
                        rect(left - 3, bottom, right + 3, bottom + 3),
                        rect(left - 3, top, left, bottom), rect(right, top, right + 3, bottom)};
                    const PanelMarkers& drew = recorded.panelMarkers[k];
                    drawn_right += drew.cells.pin == cell && !drew.cells.outline &&
                                           drew.rects == ring && left - 3 >= picture.x &&
                                           top - 3 >= picture.y &&
                                           right + 3 <= picture.x + picture.width &&
                                           bottom + 3 <= picture.y + picture.height
                                       ? 1
                                       : 0;
                }
                const bool drawn =
                    recorded.panels == panels && recorded.panelMarkers.size() == panels &&
                    drawn_right == panels && zoom_shown == static_cast<double>(whole_zoom);
                // Cell: the grid lines of the largest step whose cell fits the panel frame the
                // shown pixel's grid cell, centred to a pixel (P9, §4.4).
                std::uint32_t step = 0;
                const dvv::media::PixelRect first_picture = view.panelPicture(0);
                for (const GridLevelConfig& level : frame.grid().levels) {
                    if (level.step * whole_zoom <=
                        std::min(first_picture.width, first_picture.height)) {
                        step = std::max(step, level.step);
                    }
                }
                const auto frames_cell = [whole_zoom, step](std::uint32_t pixel, std::uint32_t size,
                                                            std::int64_t at, std::uint32_t room) {
                    const std::uint32_t from = pixel / step * step;
                    const std::uint32_t to = std::min(from + step, size);
                    const std::int64_t low = at + (from * whole_zoom);
                    const std::int64_t high = at + (to * whole_zoom);
                    return low >= 0 && high <= room && std::abs(low + high - room) <= 1;
                };
                const bool gridded =
                    mark.mode != InspectorMode::Cell ||
                    (step != 0 &&
                     frames_cell(want->x, image.width, offset.x, first_picture.width) &&
                     frames_cell(want->y, image.height, offset.y, first_picture.height));
                // The window point of a backing coordinate of the viewport.
                const double scale = view.GetContentScaleFactor();
                const auto window_of = [scale](double backing) {
                    return static_cast<int>(std::lround((backing / scale) - 0.5));
                };
                const dvv::media::ViewTransform shown = view.shownView();
                const dvv::media::AxisView ax = dvv::media::axisX(shown);
                const dvv::media::AxisView ay = dvv::media::axisY(shown);
                const std::int32_t x0 = dvv::media::firstPixelOf(ax, want->x);
                const std::int32_t x1 = dvv::media::firstPixelOf(ax, want->x + 1);
                const std::int32_t y0 = dvv::media::firstPixelOf(ay, want->y);
                const std::int32_t y1 = dvv::media::firstPixelOf(ay, want->y + 1);
                // A cell of at least 4 backing pixels: its middle is a window point inside it.
                const bool large = x1 - x0 >= 4 && y1 - y0 >= 4;
                std::uint32_t at_mark = 0;
                std::uint32_t at_centre = 0;
                for (std::uint32_t k = 0; k < panels; ++k) {
                    const dvv::media::PixelRect picture = view.panelPicture(k);
                    const wxPoint mark_at(window_of(picture.x + ((x0 + x1) / 2.0)),
                                          window_of(picture.y + ((y0 + y1) / 2.0)));
                    at_mark += view.pixelAt(mark_at) == want ? 1 : 0;
                    const wxPoint centre(window_of(picture.x + (picture.width / 2.0)),
                                         window_of(picture.y + (picture.height / 2.0)));
                    at_centre += view.pixelAt(centre) == want ? 1 : 0;
                }
                const bool centred = mark.mode != InspectorMode::Follow || at_centre == panels;
                seen = std::format(
                    "shown pixel {} (pin {}), state {}, marked {}, drawn in {} of {} panels, "
                    "right in {} {}, grid cell framed {} (step {}), cell {} x {}, the shown pixel "
                    "at the mark in {} and at the centre in {} panels",
                    pixelText(want), pixelText(inspector.pin()), state, marked,
                    recorded.panelMarkers.size(), panels, drawn_right, drawn, gridded, step,
                    x1 - x0, y1 - y0, at_mark, at_centre);
                const dvv::media::PixelRect first = view.panelPicture(0);
                seen += std::format("; mark at {} {} in panel 0 at {} {} size {} x {}, zoom {}", x0,
                                    y0, first.x, first.y, first.width, first.height, shown.zoom());
                return state && marked && drawn && gridded && large && at_mark == panels && centred;
            }));
    }
    // 12d. A new panel picture re-frames the shown pixel with no move of the probe or the pin
    // (P9): after a hand pan, S to Planes and back, a smaller Zoom window and the old size, each
    // leave the pixel's grid cell framed (Cell) or the pixel centred (Follow) with its mark inside
    // every panel; pinned and in Live. The last step leaves Follow, Structure and Live off.
    enum class Reframe : std::uint8_t { Planes, Structure, Smaller, Back };
    struct ReframeCase {
        bool live;
        InspectorMode mode;
    };
    for (const ReframeCase rc :
         {ReframeCase{false, InspectorMode::Cell}, ReframeCase{false, InspectorMode::Follow},
          ReframeCase{true, InspectorMode::Cell}, ReframeCase{true, InspectorMode::Follow}}) {
        for (const Reframe action :
             {Reframe::Planes, Reframe::Structure, Reframe::Smaller, Reframe::Back}) {
            static constexpr std::array<const char*, 4> ACTIONS{"S to Planes", "S to Structure",
                                                                "a smaller window", "the old size"};
            const bool last =
                rc.live && rc.mode == InspectorMode::Follow && action == Reframe::Back;
            steps.push_back(step(
                std::format("re-frame {} {} after {}", rc.live ? "in Live" : "of the pin",
                            rc.mode == InspectorMode::Cell ? "Cell" : "Follow",
                            ACTIONS.at(static_cast<std::size_t>(action))),
                [&frame, st, rc, action] {
                    ZoomWindow* const zoom = frame.zoomWindow();
                    st->taken = zoom != nullptr;
                    if (zoom == nullptr) {
                        return;
                    }
                    if (frame.inspector().live() != rc.live &&
                        frame.isAvailable(Command::LiveProbe)) {
                        frame.execute(Command::LiveProbe, CommandSource::Script);
                    }
                    frame.setInspectorMode(rc.mode);
                    if (rc.live && (!frame.inspector().probe() ||
                                    frame.inspector().probe() == frame.inspector().pin())) {
                        // A probe away from the pin: the pointer on (32, 24) of the 64 x 48 clip.
                        Viewport& main = frame.viewport();
                        const wxSize client = main.GetClientSize();
                        const dvv::media::ImagePixel target{.x = 32, .y = 24};
                        bool moved = false;
                        for (int y = 0; !moved && y < client.y; ++y) {
                            for (int x = 0; !moved && x < client.x; ++x) {
                                if (main.pixelAt(wxPoint(x, y)) == target) {
                                    sendViewportMouse(main, wxEVT_MOTION, wxPoint(x, y), false,
                                                      false);
                                    moved = true;
                                }
                            }
                        }
                    }
                    // A hand pan off the frame, which the change then undoes.
                    Viewport& view = zoom->viewport();
                    const dvv::media::PointD centre = view.view().centre;
                    view.setView({.mode = dvv::media::ViewMode::Zoom,
                                  .zoom = static_cast<double>(zoomShown(*zoom)),
                                  .centre = {.x = centre.x + 3, .y = centre.y + 2},
                                  .image = view.imageSize()});
                    st->panned = view.view();
                    switch (action) {
                        case Reframe::Planes:
                        case Reframe::Structure: st->taken = zoomKey(frame, 'S'); break;
                        case Reframe::Smaller:
                            st->windowSize = zoom->GetClientSize();
                            zoom->SetClientSize(st->windowSize / 2);
                            break;
                        case Reframe::Back: zoom->SetClientSize(st->windowSize); break;
                    }
                },
                [&frame, st, rc, action, last](std::string& seen) {
                    const ZoomWindow* const zoom = frame.zoomWindow();
                    const Inspector& inspector = frame.inspector();
                    const std::optional<dvv::media::ImagePixel> want = inspector.shown();
                    if (zoom == nullptr || !want) {
                        seen = std::format("Zoom window {}, shown pixel {}", zoom != nullptr,
                                           pixelText(want));
                        return false;
                    }
                    const InspectorLayout layout = action == Reframe::Planes
                                                       ? InspectorLayout::Planes
                                                       : InspectorLayout::Structure;
                    const wxSize client = zoom->GetClientSize();
                    const bool sized = action == Reframe::Smaller ? client != st->windowSize
                                       : action == Reframe::Back  ? client == st->windowSize
                                                                  : true;
                    const bool state = st->taken && inspector.live() == rc.live &&
                                       inspector.mode() == rc.mode &&
                                       inspector.layout() == layout &&
                                       (!rc.live || inspector.probe() != inspector.pin());
                    std::string framed_seen;
                    const bool framed = framesShown(frame, *zoom, *want, framed_seen);
                    seen =
                        std::format("state {} (pin {}, probe {}), window size {}, panned {}; {}",
                                    state, pixelText(inspector.pin()), pixelText(inspector.probe()),
                                    sized, zoom->viewport().view() == st->panned, framed_seen);
                    const bool done = state && sized && framed;
                    if (done && last) {
                        frame.execute(Command::LiveProbe, CommandSource::Script);  // Live off
                    }
                    return done;
                }));
        }
    }
    // 13. Another display scale. The macOS display-scale hook gives the viewport another backing
    // scale: the numbers' metrics follow it, and a view at the numbers-fit zoom follows the new fit
    // while a zoom the user chose stays. On every platform the window then gets a
    // wxEVT_DPI_CHANGED, the path a real DPI change takes into FloatingWindow::onDpiChanged: it
    // measures again (the viewport's minimum, reset before) and keeps the DIP size. Windows and
    // GTK have no hook and their real DPI stays, so there only the event's path is checked. Each
    // change is followed by the window's own scale again (restore).
    struct ScalePhase {
        const char* name;
        bool restore;
        bool away;  // the user's zoom, one stop past the fit
    };
    for (const ScalePhase phase :
         {ScalePhase{"zoom_scale", false, false}, ScalePhase{"zoom_scale restore", true, false},
          ScalePhase{"zoom_scale user zoom", false, true},
          ScalePhase{"zoom_scale user zoom restore", true, true}}) {
        steps.push_back(step(
            phase.name,
            [&frame, st, phase] {
                ZoomWindow* const zoom = frame.zoomWindow();
                if (zoom == nullptr) {
                    return;
                }
                Viewport& view = zoom->viewport();
                if (phase.away && !phase.restore) {
                    st->userZoom =
                        nextWholeZoom(zoomShown(*zoom), true, view.minWholeZoom()).value_or(0);
                    view.setView({.mode = dvv::media::ViewMode::Zoom,
                                  .zoom = static_cast<double>(st->userZoom),
                                  .centre = view.view().centre,
                                  .image = view.imageSize()});
                }
                st->sizeDip = zoom->keptSizeDip();
                st->windowDip = zoom->ToDIP(zoom->GetSize());
                const double window_scale = view.GetContentScaleFactor();
                if (!phase.restore) {
                    st->scale = window_scale >= 2 ? 1.0 : 2.0;
                }
                st->hooked = setBackingScaleForTest(view, phase.restore ? 0.0 : st->scale);
                // The event a real DPI change sends, after the viewport's minimum is reset so
                // that only measure() can set it again.
                view.SetMinSize(wxSize(1, 1));
                const wxSize dpi = zoom->GetDPI();
                const double ratio = phase.restore ? 1.0 / st->scale : st->scale;
                wxDPIChangedEvent event(
                    dpi, wxSize(static_cast<int>(dpi.x * ratio), static_cast<int>(dpi.y * ratio)));
                event.SetEventObject(zoom);
                (void)zoom->GetEventHandler()->ProcessEvent(event);
            },
            [&frame, st, phase](std::string& seen) {
                const ZoomWindow* const zoom = frame.zoomWindow();
                if (zoom == nullptr) {
                    seen = "no Zoom window";
                    return false;
                }
                const LayoutSize kept = zoom->keptSizeDip();
                const wxSize window_dip = zoom->ToDIP(zoom->GetSize());
                const wxSize min = zoom->viewport().GetMinSize();
                const wxSize want_min =
                    zoom->FromDIP(wxSize(ZOOM_WINDOW_SIZE_MIN, ZOOM_WINDOW_SIZE_MIN));
                const bool sized =
                    kept == st->sizeDip && window_dip == st->windowDip && min == want_min;
                const std::string sizes = std::format(
                    "kept DIP size {}x{} (at the start {}x{}), window {}x{} DIP (at the start "
                    "{}x{}), viewport minimum {}x{} (want {}x{})",
                    kept.width, kept.height, st->sizeDip.width, st->sizeDip.height, window_dip.x,
                    window_dip.y, st->windowDip.x, st->windowDip.y, min.x, min.y, want_min.x,
                    want_min.y);
                if (!st->hooked) {
                    seen = sizes +
                           "; no display-scale hook on this platform and the real DPI "
                           "stays: the metrics and the zoom are not checked";
                    return !SCALE_HOOK && sized;
                }
                const std::optional<WantNumbers> want = wantNumbers(frame, zoom);
                const ValueMetrics& metrics = zoom->viewport().inspect().metrics;
                const std::uint32_t shown = zoomShown(*zoom);
                const std::uint32_t want_zoom = phase.away ? st->userZoom : want ? want->fit : 0;
                seen = std::format(
                    "{}, metrics {} {} {} {} (want {} {} {} {}), zoom {} (want {})", sizes,
                    metrics.advance, metrics.lineHeight, metrics.padX, metrics.padY,
                    want ? want->metrics.advance : 0, want ? want->metrics.lineHeight : 0,
                    want ? want->metrics.padX : 0, want ? want->metrics.padY : 0, shown, want_zoom);
                return want && sized && metrics == want->metrics && want_zoom != 0 &&
                       shown == want_zoom;
            }));
    }
    // 14. A new clip blanks the values at once (no old clip's values beside the new picture), then
    // a readback of the new streams fills them while the pin stays inside the clip.
    steps.push_back(step(
        "new clip: values blank",
        [&test, &frame, st] {
            st->changeSeen = false;
            // The hook holds the state weakly: one left behind by a failed step keeps nothing.
            const std::weak_ptr<InspectorState> weak = st;
            frame.setStreamsChangedForTest([&frame, weak](const std::vector<StreamInfo>& streams) {
                const std::shared_ptr<InspectorState> state = weak.lock();
                if (state && !streams.empty() && !state->changeSeen) {
                    state->changeSeen = true;
                    state->blankAtChange = valuesBlank(frame);
                    state->pinAtChange = frame.inspector().pin().has_value();
                }
            });
            openSmokeStream(&test);
        },
        [&frame, st](std::string& seen) {
            if (st->changeSeen) {
                frame.setStreamsChangedForTest({});  // its one change is seen, pass or fail
            }
            const std::optional<dvv::media::ImagePixel> pin = frame.inspector().pin();
            std::string values_seen = "no pin: nothing to fill";
            const bool filled = !pin || valuesMatch(frame, *pin, values_seen);
            seen = std::format("change seen {}, blank at the change {}, pin then {}; {}",
                               st->changeSeen, st->blankAtChange, st->pinAtChange, values_seen);
            return st->changeSeen && st->blankAtChange && filled;
        }));
    // 15. X turns the padding check on: the main view adds Raw to its own subscription (the shown
    // Zoom window's Raw would fill the feed's union anyway) and runs the check. The synthetic clip
    // carries no padding garbage, so the PADDING field stays empty.
    steps.push_back(step(
        "padding",
        [&frame, st] {
            st->recorded = frame.viewport().lastRecorded().value_or(RecordedInfo{});
            (void)sendKey(frame, 'X');
        },
        [&frame, st](std::string& seen) {
            const auto* const toggle = dynamic_cast<const wxBitmapToggleButton*>(
                &frame.transportBar().buttonForTest(Command::PaddingCheck));
            const bool on = toggle != nullptr && toggle->GetValue();
            const bool raw = frame.viewport().subscription().roles().has(ViewRole::Raw);
            const RecordedInfo now = frame.viewport().lastRecorded().value_or(RecordedInfo{});
            const std::uint64_t passes = now.paddingPasses - st->recorded.paddingPasses;
            const wxString field = frame.statusTexts()[MainFrame::PADDING_FIELD];
            seen = std::format("toggle on {}, main Raw role {}, padding passes +{}, field '{}'", on,
                               raw, passes, field.utf8_string());
            return on && raw && passes > 0 && field.empty();
        }));
    // 16. The check off and the Zoom window hidden cost nothing: the feed's roles are {Display}
    // alone, the store holds no Planes or Raw lease and the Zoom viewport has no presenter. The
    // leases are asked for again until an answer shows none (the Session drops them in order).
    steps.push_back(step(
        "closed_costs_nothing",
        [&frame, st] {
            (void)sendKey(frame, 'X');
            if (frame.windowShown(FloatingKind::Zoom)) {
                (void)sendMenu(frame, menuId(Command::ZoomWindow));
            }
            st->leases = std::make_shared<LeaseAnswer>();
        },
        [&frame, st](std::string& seen) {
            const bool display_only =
                wxGetApp().feed()->roles() == ViewRoles::of(ViewRole::Display);
            const ZoomWindow* const zoom = frame.zoomWindow();
            const bool presenter = zoom != nullptr && zoom->viewport().presenter() != nullptr;
            std::optional<Session::LeaseCounts> counts;
            {
                const std::lock_guard lock(st->leases->mutex);
                counts = st->leases->counts;
                if (counts || !st->leases->asked) {
                    st->leases->counts.reset();
                    st->leases->asked = true;
                    Session* const session = wxGetApp().session();
                    if (session != nullptr) {
                        session->leasesForTest(
                            [answer = st->leases](Session::LeaseCounts now) noexcept {
                                const std::lock_guard answer_lock(answer->mutex);
                                answer->counts = now;
                            });
                    }
                }
            }
            const auto count = [&counts](ViewRole role) {
                return counts ? std::to_string(counts->roles.at(static_cast<std::size_t>(role)))
                              : "-";
            };
            seen = std::format(
                "roles Display only {}, Zoom presenter {}, Planes leases {}, Raw leases {}, other "
                "leases {}",
                display_only, presenter, count(ViewRole::Planes), count(ViewRole::Raw),
                counts ? std::to_string(counts->other) : "-");
            return display_only && !presenter && counts &&
                   counts->roles.at(static_cast<std::size_t>(ViewRole::Planes)) == 0 &&
                   counts->roles.at(static_cast<std::size_t>(ViewRole::Raw)) == 0 &&
                   counts->other == 0;
        }));
    // 17. The start order of a padding check saved on: the main view subscribes Raw (X) while no
    // roles callback is installed, as in the frame's constructor, so the Session still produces
    // {Display} alone and no check runs.
    steps.push_back(step(
        "padding before the roles binding",
        [&frame] {
            wxGetApp().feed()->setRolesChanged({});
            (void)sendKey(frame, 'X');
        },
        [&frame](std::string& seen) {
            const bool raw = frame.viewport().subscription().roles().has(ViewRole::Raw);
            const bool union_raw = wxGetApp().feed()->roles().has(ViewRole::Raw);
            seen = std::format("main Raw role {}, feed union Raw {}", raw, union_raw);
            return raw && union_raw;
        }));
    // 18. Installing the binding (App::OnInit's order) reports the union at once, so the restored
    // check runs: padding passes grow without any later role change.
    steps.push_back(step(
        "padding restored on",
        [&frame, st] {
            st->recorded = frame.viewport().lastRecorded().value_or(RecordedInfo{});
            wxGetApp().bindViewRoles();
        },
        [&frame, st](std::string& seen) {
            const RecordedInfo now = frame.viewport().lastRecorded().value_or(RecordedInfo{});
            const std::uint64_t passes = now.paddingPasses - st->recorded.paddingPasses;
            const wxString field = frame.statusTexts()[MainFrame::PADDING_FIELD];
            seen = std::format("padding passes +{}, field '{}'", passes, field.utf8_string());
            return passes > 0 && field.empty();
        }));
    return steps;
}

}  // namespace rawreel
