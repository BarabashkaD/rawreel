// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

// The pixel inspector selftest (pixel inspector design §4.1, §4.2, §4.4-§4.6), slices A and B.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
#include "core/session.h"
#include "dvv/base/log.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/sample_grid.h"
#include "dvv/media/view.h"
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
    bool inOffAt256 = false, outOffAt1 = false, pinchKeepsFit = false;
    bool smallPinchInside = false, fractionalFit = false, bothAtFit = false;  // at Fit
    std::uint32_t fromFit = 0, wantFromFit = 0, fromNear3 = 0;  // Zoom In from Fit, 2.9999999
    dvv::media::ViewRequest panned;                             // the hand-panned Zoom view
    std::int64_t frameBefore = -1;  // the values' frame before the new frame
    LayoutSize sizeDip;             // the Zoom window's kept DIP size before a scale change
    wxSize windowDip;               // and its size in DIP
    double scale = 0;               // the scale the hook gives
    bool hooked = false;            // setBackingScaleForTest() took it
    bool changeSeen = false, blankAtChange = false, pinAtChange = false;  // the new clip
    std::optional<dvv::media::ImagePixel> probe;  // C in Live: the probe, away from the pin
    bool onProbe = false;                         // and the Zoom view centred on it
    std::uint32_t userZoom = 0;  // zoom_scale: the zoom the user chose, past the fit
    bool keyKept = false;        // a toolbar toggle with the focus keeps Space
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

std::string pixelText(const std::optional<dvv::media::ImagePixel>& pixel) {
    return pixel ? std::format("{} {}", pixel->x, pixel->y) : std::string("none");
}

// The Zoom view's shown zoom, whole.
std::uint32_t zoomShown(const ZoomWindow& zoom) {
    return wholeZoom(zoom.viewport().shownView().zoom());
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
    const std::uint32_t whole = wholeZoom(zoom->viewport().shownView().zoom());
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
            return pin == PIN && shown && !zoom_active && (!st->activeBefore || active) && numbers;
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
            const bool settled = now.markers == markers;
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
    // 11. With the Zoom window in front the View commands act on its view: Actual Size gives
    // numbersFitZoom, Zoom In the next whole stop; the main view's zoom stays. Under WholePixels
    // Zoom In is off at 256 and Zoom Out at 1, a pinch of factor 1 or one inside the same whole
    // zoom at Fit leaves Fit, both steps are there at a fractional Fit, Zoom In
    // from Fit goes to the stop past its zoom, and a request of 2.9999999 shows 3, so Zoom In
    // gives 4.
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
            frame.setFocusForTest(&view);
            const auto at = [&](double zoom_at) {
                view.setView({.mode = dvv::media::ViewMode::Zoom,
                              .zoom = zoom_at,
                              .centre = view.view().centre,
                              .image = view.imageSize()});
            };
            at(1);
            (void)sendMenu(frame, menuId(Command::ActualSize));
            st->actualZoom = zoomShown(*zoom);
            st->wantIn = nextWholeZoom(st->actualZoom, true).value_or(0);
            (void)sendMenu(frame, menuId(Command::ZoomIn));
            st->inZoom = zoomShown(*zoom);
            at(256);
            st->inOffAt256 =
                !frame.isAvailable(Command::ZoomIn) && frame.isAvailable(Command::ZoomOut);
            at(1);
            st->outOffAt1 =
                frame.isAvailable(Command::ZoomIn) && !frame.isAvailable(Command::ZoomOut);
            view.setView({});  // Fit
            const double fit = view.shownView().zoom();
            pinch(view, 1.0);
            st->pinchKeepsFit = view.view().mode == dvv::media::ViewMode::Fit;
            // A pinch that stays inside the same whole zoom keeps Fit too.
            constexpr double SMALL_PINCH = 1.02;
            st->smallPinchInside = std::floor(fit * SMALL_PINCH) == std::floor(fit);
            if (st->smallPinchInside) {
                pinch(view, SMALL_PINCH);
                st->pinchKeepsFit =
                    st->pinchKeepsFit && view.view().mode == dvv::media::ViewMode::Fit;
                view.setView({});
            } else {
                DVV_LOG_INFO(
                    "selftest: view_keys skips the 1.02 pinch: Fit {} crosses a whole zoom", fit);
            }
            // At a fractional Fit both steps are there.
            st->fractionalFit = fit != std::floor(fit) && fit > 1 && fit < 256;
            if (!st->fractionalFit) {
                DVV_LOG_INFO("selftest: view_keys skips In and Out at a fractional Fit: Fit {}",
                             fit);
            }
            st->bothAtFit =
                frame.isAvailable(Command::ZoomIn) && frame.isAvailable(Command::ZoomOut);
            st->wantFromFit =
                nextWholeZoom(static_cast<std::uint32_t>(std::floor(fit)), true).value_or(0);
            (void)sendMenu(frame, menuId(Command::ZoomIn));
            st->fromFit = zoomShown(*zoom);
            at(2.9999999);
            (void)sendMenu(frame, menuId(Command::ZoomIn));
            st->fromNear3 = zoomShown(*zoom);
            (void)sendMenu(frame, menuId(Command::ActualSize));  // back at the numbers' fit
            frame.setFocusForTest(nullptr);
        },
        [&frame, st](std::string& seen) {
            const std::optional<WantNumbers> want = wantNumbers(frame, frame.zoomWindow());
            const double main_zoom = frame.viewport().shownView().zoom();
            std::string numbers_seen;
            const bool open = numbersOpen(frame, frame.zoomWindow(), numbers_seen);
            seen = std::format(
                "Actual Size {} (want {}), Zoom In {} (want {}), main zoom {} (at the start {}), "
                "In off at 256 {}, Out off at 1 {}, a pinch keeps Fit {} (1.02 inside the whole "
                "zoom {}), In and Out at a fractional Fit {} (fractional {}), Zoom In from Fit {} "
                "(want {}), from 2.9999999 {} (want 4); {}",
                st->actualZoom, want ? want->fit : 0, st->inZoom, st->wantIn, main_zoom,
                st->mainZoom, st->inOffAt256, st->outOffAt1, st->pinchKeepsFit,
                st->smallPinchInside, st->bothAtFit, st->fractionalFit, st->fromFit,
                st->wantFromFit, st->fromNear3, numbers_seen);
            return want && st->actualZoom == want->fit && st->wantIn != 0 &&
                   st->inZoom == st->wantIn && main_zoom == st->mainZoom && st->inOffAt256 &&
                   st->outOffAt1 && st->pinchKeepsFit && (!st->fractionalFit || st->bothAtFit) &&
                   st->wantFromFit != 0 && st->fromFit == st->wantFromFit && st->fromNear3 == 4 &&
                   open;
        }));
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
                    st->userZoom = nextWholeZoom(zoomShown(*zoom), true).value_or(0);
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
