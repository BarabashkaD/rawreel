// The inspect pipeline's GPU tests: the layer block's layout as the shader reads it, the support
// frames the inspect layers' tests draw, and the plane panels the inspect pipeline draws.

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "core/frame_set.h"
#include "core/inspector.h"
#include "core/inspector_text.h"
#include "dvv/base/error.h"
#include "dvv/base/log.h"
#include "dvv/gpu/context_vulkan.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/gpu/glyph_strip_vulkan.h"
#include "dvv/gpu/gpu_instance.h"
#include "dvv/gpu/image.h"
#include "dvv/gpu/resources.h"
#include "dvv/media/frame_vulkan.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/reference.h"
#include "dvv/media/sample_grid.h"
#include "dvv/media/types.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "dvv/platform/pixel_size.h"
#include "gpu_support.h"
#include "inspect_reference.h"
#include "inspect_support.h"
#include "render/layer_block.h"
#include "render/pass_regions.h"
#include "render/picture_constants.h"
#include "render/picture_pass.h"
#include "render/view_layout.h"
#include "render/view_renderer.h"
#include "render/view_state.h"

namespace {

using rawreel::LayerBlock;
using rawreel::PanelKind;
using rawreel::RecordedInfo;
using rawreel::ViewParams;
using rawreel::ViewRenderer;
using rawreel::ViewRole;
using rawreel::testing::CELL_ZOOM;
using rawreel::testing::CellCheck;
using rawreel::testing::cellLineParams;
using rawreel::testing::checkCells;
using rawreel::testing::digitStrip;
using rawreel::testing::flaggedCell;
using rawreel::testing::formatNamed;
using rawreel::testing::GpuTestEnvironment;
using rawreel::testing::INSPECT_IMAGE;
using rawreel::testing::inspectRenderer;
using rawreel::testing::pixelOf;
using rawreel::testing::planesParams;
using rawreel::testing::renderInspect;
using rawreel::testing::VALUES_ZOOM;
using rawreel::testing::valuesParams;
using rawreel::testing::valueStrip;

// Formats whose stored words have padding bits (not all of them).
constexpr std::array<std::string_view, 8> PADDED_FORMATS{
    "p010le", "y210le", "yuv420p10le", "yuv420p10be", "gray10le", "gray12le", "v210", "x2rgb10le"};

}  // namespace

namespace {

using rawreel::ReadbackUse;
using rawreel::ViewReadback;

// inspect.on with the pin at `pixel` (the Picture panel).
ViewParams probeParams(dvv::media::ImagePixel pixel) {
    ViewParams params;
    params.inspect.on = true;
    params.inspect.probe = pixel;
    return params;
}

// The padding bits of every component's stored word at `pixel`.
std::vector<rawreel::testing::PlantedBits> paddingAt(dvv::media::FormatId format,
                                                     dvv::platform::PixelSize size,
                                                     dvv::media::ImagePixel pixel) {
    std::vector<rawreel::testing::PlantedBits> planted;
    const std::uint32_t count = dvv::media::formatDesc(format).componentCount;
    for (std::size_t k = 0; k < count; ++k) {
        const dvv::media::SamplePosition sample = rawreel::testing::sampleAt(format, k, pixel);
        const auto word = dvv::media::storedWord(format, size.width, size.height, k, sample);
        REQUIRE(word.has_value());
        if (word.has_value() && word->paddingMask != 0) {
            planted.push_back({.component = k, .sample = sample, .bits = word->paddingMask});
        }
    }
    return planted;
}

// `readback` against the frame: its tags, and per component unpack()'s code and the file's word.
void checkProbe(const rawreel::testing::InspectFrame& frame, dvv::platform::PixelSize size,
                const ViewReadback& readback, dvv::media::ImagePixel pixel) {
    const dvv::media::FormatId format = frame.reference.format;
    CHECK(readback.index == frame.frames.index);
    CHECK(readback.epoch == frame.frames.epoch);
    CHECK(readback.streamSet == frame.frames.streamSet);
    CHECK_FALSE(readback.paddingSamples.has_value());
    REQUIRE(readback.probe.has_value());
    if (!readback.probe) {
        return;
    }
    const rawreel::ProbeSample& probe = *readback.probe;
    CHECK(probe.pixel == pixel);
    const std::uint32_t count = dvv::media::formatDesc(format).componentCount;
    REQUIRE(probe.count == count);
    for (std::size_t k = 0; k < count; ++k) {
        CAPTURE(k);
        const auto word = dvv::media::storedWord(format, size.width, size.height, k,
                                                 rawreel::testing::sampleAt(format, k, pixel));
        REQUIRE(word.has_value());
        CHECK(probe.codes.at(k) == rawreel::testing::codeAt(frame.reference, k, pixel));
        CHECK(probe.words.at(k) == dvv::media::readStoredWord(frame.bytes, 0, *word));
    }
}

}  // namespace

TEST_CASE("readback: the probe returns unpack()'s codes and the stored words") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    std::vector<dvv::media::FormatId> formats(dvv::media::catalogue().begin(),
                                              dvv::media::catalogue().end());
    formats.push_back(rawreel::testing::testYuv411p());
    for (const dvv::media::FormatId format : formats) {
        const dvv::media::PixelFormatDesc& desc = dvv::media::formatDesc(format);
        const std::string_view name = desc.name;
        CAPTURE(name);
        // A width the format's pixel groups divide (v210 and the subsampled formats).
        const std::uint32_t multiple = std::max<std::uint32_t>(desc.widthMultiple, 1U);
        const dvv::platform::PixelSize size{
            .width = (INSPECT_IMAGE.width + multiple - 1) / multiple * multiple,
            .height = INSPECT_IMAGE.height};
        // The first pixel, the last one (both edges) and one inside.
        const std::array<dvv::media::ImagePixel, 3> pixels{
            dvv::media::ImagePixel{.x = 0, .y = 0},
            dvv::media::ImagePixel{.x = size.width - 1, .y = size.height - 1},
            dvv::media::ImagePixel{.x = 17, .y = 9}};
        // Every format with padding: garbage in the padding bits of every probed word.
        std::vector<rawreel::testing::PlantedBits> planted;
        for (const dvv::media::ImagePixel pixel : pixels) {
            const std::vector<rawreel::testing::PlantedBits> more = paddingAt(format, size, pixel);
            planted.insert(planted.end(), more.begin(), more.end());
        }
        const bool padded = std::ranges::find(PADDED_FORMATS, name) != PADDED_FORMATS.end();
        CHECK((!padded || !planted.empty()));
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, format, size, planted);
        const auto renderer = inspectRenderer(frame);
        std::vector<ViewReadback> delivered;
        renderer->setReadbackHandler(
            [&delivered](ViewReadback readback) { delivered.push_back(readback); });
        for (const dvv::media::ImagePixel pixel : pixels) {
            CAPTURE(pixel.x);
            CAPTURE(pixel.y);
            auto rendered =
                renderer->render({.width = 80, .height = 60}, frame.frames, probeParams(pixel));
            REQUIRE_MESSAGE(rendered, (rendered ? std::string() : rendered.error().describe()));
            REQUIRE(static_cast<bool>(rendered->completed));
            auto done = rawreel::testing::submitAndWait(*frame.context, *env.cpu,
                                                        std::move(rendered->record));
            REQUIRE_MESSAGE(done, (done ? std::string() : done.error().describe()));
            delivered.clear();
            rendered->completed(done);
            REQUIRE(delivered.size() == 1);
            checkProbe(frame, size, delivered.front(), pixel);
            const rawreel::ProbeSample probe =
                delivered.front().probe.value_or(rawreel::ProbeSample{});
            // The planted padding bits come back in the words (a format without has a mask of 0).
            for (std::size_t k = 0; k < probe.count; ++k) {
                CAPTURE(k);
                const auto word =
                    dvv::media::storedWord(format, size.width, size.height, k,
                                           rawreel::testing::sampleAt(format, k, pixel));
                REQUIRE(word.has_value());
                CHECK((probe.words.at(k) & word->paddingMask) == word->paddingMask);
            }
        }
        // A failed submission delivers nothing.
        auto rendered =
            renderer->render({.width = 80, .height = 60}, frame.frames, probeParams(pixels[2]));
        REQUIRE(rendered.has_value());
        auto done =
            rawreel::testing::submitAndWait(*frame.context, *env.cpu, std::move(rendered->record));
        REQUIRE(done.has_value());
        delivered.clear();
        rendered->completed(dvv::Result<std::uint64_t>{
            dvv::makeError(dvv::Errc::DeviceLost, "test: the submission failed")});
        CHECK(delivered.empty());
        // Without a pin, a record carries no readback.
        auto plain = renderer->render({.width = 80, .height = 60}, frame.frames, planesParams());
        REQUIRE(plain.has_value());
        CHECK_FALSE(static_cast<bool>(plain->completed));
        auto plain_done =
            rawreel::testing::submitAndWait(*frame.context, *env.cpu, std::move(plain->record));
        CHECK(plain_done.has_value());
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("readback: currency rule") {
    const ViewReadback readback{.index = 3, .epoch = 5, .streamSet = 7};
    CHECK(rawreel::readbackUse(readback, 5, 7) == ReadbackUse::Show);
    // Newer than the GUI's status: held until it catches up.
    CHECK(rawreel::readbackUse(readback, 4, 7) == ReadbackUse::Hold);
    CHECK(rawreel::readbackUse(readback, 4, 9) == ReadbackUse::Hold);
    CHECK(rawreel::readbackUse(readback, 5, 6) == ReadbackUse::Hold);
    // Older: dropped.
    CHECK(rawreel::readbackUse(readback, 6, 7) == ReadbackUse::Drop);
    CHECK(rawreel::readbackUse(readback, 6, 1) == ReadbackUse::Drop);
    CHECK(rawreel::readbackUse(readback, 5, 8) == ReadbackUse::Drop);
}

TEST_CASE("readback: slots rotate") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const dvv::media::FormatId format = formatNamed("yuv420p10le");
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, format, INSPECT_IMAGE);
        const auto renderer = inspectRenderer(frame);
        std::vector<ViewReadback> delivered;
        renderer->setReadbackHandler(
            [&delivered](ViewReadback readback) { delivered.push_back(readback); });
        const dvv::platform::PixelSize extent{.width = 80, .height = 60};
        // Five frame() records, each a new FrameSet, each with its own pin.
        dvv::media::ImagePixel last{};
        for (std::uint64_t version = 1; version <= 5; ++version) {
            CAPTURE(version);
            const dvv::media::ImagePixel pixel{.x = static_cast<std::uint32_t>(version * 7),
                                               .y = static_cast<std::uint32_t>(version * 4)};
            last = pixel;
            const rawreel::ViewSnapshot snapshot{
                .feed = {.frames = frame.frames.share(), .version = version},
                .params = probeParams(pixel)};
            auto presented = renderer->frame({.extent = extent,
                                              .frameNumber = version - 1,
                                              .previousFrameCompleted = version > 1},
                                             snapshot);
            REQUIRE_MESSAGE(presented, (presented ? std::string() : presented.error().describe()));
            REQUIRE(static_cast<bool>(presented->completed));
            CHECK(renderer->lastRecorded().readbackSlot == (version - 1) % 4);
            auto done = rawreel::testing::submitAndWait(*frame.context, *env.cpu,
                                                        std::move(presented->record));
            REQUIRE_MESSAGE(done, (done ? std::string() : done.error().describe()));
            delivered.clear();
            presented->completed(done);
            REQUIRE(delivered.size() == 1);
            checkProbe(frame, INSPECT_IMAGE, delivered.front(), pixel);
        }
        // The same snapshot again: the pin's values are known, no readback is recorded, after a
        // dropped frame too (the readback was delivered).
        const auto same = [&frame, last](std::uint64_t version) {
            return rawreel::ViewSnapshot{
                .feed = {.frames = frame.frames.share(), .version = version},
                .params = probeParams(last)};
        };
        std::uint64_t number = 5;
        // frame() of same(version); whether it carries a readback.
        const auto records = [&](std::uint64_t version, bool previous_completed) {
            auto again = renderer->frame({.extent = extent,
                                          .frameNumber = number++,
                                          .previousFrameCompleted = previous_completed},
                                         same(version));
            REQUIRE(again.has_value());
            return again;
        };
        CHECK_FALSE(static_cast<bool>(records(5, true)->completed));
        CHECK(renderer->lastRecorded().readbackSlot == 0);
        CHECK_FALSE(static_cast<bool>(records(5, false)->completed));
        // Submits and completes a readback-carrying frame; what the handler got.
        const auto complete = [&](dvv::Result<dvv::gpu::PresentFrame> presented, bool fail) {
            REQUIRE(presented.has_value());
            REQUIRE(static_cast<bool>(presented->completed));
            auto done = rawreel::testing::submitAndWait(*frame.context, *env.cpu,
                                                        std::move(presented->record));
            REQUIRE(done.has_value());
            delivered.clear();
            presented->completed(fail ? dvv::Result<std::uint64_t>{dvv::makeError(
                                            dvv::Errc::DeviceLost, "test: the submission failed")}
                                      : done);
            return delivered.size();
        };
        // A failed submission delivers nothing: the next same-key frame records it again.
        CHECK(complete(records(6, true), true) == 0);
        CHECK(complete(records(6, true), false) == 1);
        CHECK_FALSE(static_cast<bool>(records(6, true)->completed));
        // No handler: not delivered, recorded again until a handler gets it.
        renderer->setReadbackHandler({});
        CHECK(complete(records(7, true), false) == 0);
        renderer->setReadbackHandler(
            [&delivered](ViewReadback readback) { delivered.push_back(readback); });
        CHECK(complete(records(7, true), false) == 1);
        CHECK_FALSE(static_cast<bool>(records(7, true)->completed));
        // A handler that throws: not delivered, recorded again.
        bool thrown = false;
        renderer->setReadbackHandler([&delivered, &thrown](ViewReadback readback) {
            if (!thrown) {
                thrown = true;
                throw std::runtime_error("test: the handler failed");
            }
            delivered.push_back(readback);
        });
        CHECK(complete(records(8, true), false) == 0);
        CHECK(thrown);
        CHECK(complete(records(8, true), false) == 1);
        CHECK_FALSE(static_cast<bool>(records(8, true)->completed));
        // An unpin before the pin's completion runs: the late completion still reaches the
        // handler but delivers nothing, so the same pin on the same FrameSet records again.
        auto pinned = records(9, true);
        REQUIRE(static_cast<bool>(pinned->completed));
        auto pinned_done =
            rawreel::testing::submitAndWait(*frame.context, *env.cpu, std::move(pinned->record));
        REQUIRE(pinned_done.has_value());
        ViewParams unpinned_params = probeParams(last);
        unpinned_params.inspect.probe.reset();
        auto unpinned = renderer->frame(
            {.extent = extent, .frameNumber = number++, .previousFrameCompleted = true},
            rawreel::ViewSnapshot{.feed = {.frames = frame.frames.share(), .version = 9},
                                  .params = unpinned_params});
        REQUIRE(unpinned.has_value());
        CHECK_FALSE(static_cast<bool>(unpinned->completed));
        if (unpinned->record) {
            auto unpinned_done = rawreel::testing::submitAndWait(*frame.context, *env.cpu,
                                                                 std::move(unpinned->record));
            REQUIRE(unpinned_done.has_value());
        }
        delivered.clear();
        pinned->completed(pinned_done);
        CHECK(delivered.size() == 1);
        CHECK(complete(records(9, true), false) == 1);
        CHECK_FALSE(static_cast<bool>(records(9, true)->completed));
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("readback: slots keep their data apart") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, formatNamed("yuv420p10le"), INSPECT_IMAGE);
        const auto renderer = inspectRenderer(frame);
        std::vector<ViewReadback> delivered;
        renderer->setReadbackHandler(
            [&delivered](ViewReadback readback) { delivered.push_back(readback); });
        // Two records in flight (tests only; a presenter has one): each reads its own slot.
        constexpr std::array<dvv::media::ImagePixel, 2> PINS{
            dvv::media::ImagePixel{.x = 3, .y = 2}, dvv::media::ImagePixel{.x = 30, .y = 17}};
        std::vector<dvv::gpu::PresentFrame> frames;
        std::vector<std::uint32_t> slots;
        for (const dvv::media::ImagePixel pin : PINS) {
            auto rendered =
                renderer->render({.width = 80, .height = 60}, frame.frames, probeParams(pin));
            REQUIRE(rendered.has_value());
            slots.push_back(renderer->lastRecorded().readbackSlot);
            frames.push_back(std::move(*rendered));
        }
        CHECK(slots[0] != slots[1]);
        std::vector<dvv::Result<std::uint64_t>> done;
        for (dvv::gpu::PresentFrame& rendered : frames) {
            done.push_back(rawreel::testing::submitAndWait(*frame.context, *env.cpu,
                                                           std::move(rendered.record)));
            REQUIRE(done.back().has_value());
        }
        for (std::size_t i = 0; i < PINS.size(); ++i) {
            CAPTURE(i);
            delivered.clear();
            frames[i].completed(done[i]);
            REQUIRE(delivered.size() == 1);
            checkProbe(frame, INSPECT_IMAGE, delivered.front(), PINS.at(i));
        }
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

namespace {

using rawreel::ImageRect;
using rawreel::MarkerParams;

constexpr dvv::platform::PixelSize MARKER_IMAGE{.width = 640, .height = 360};
constexpr dvv::platform::PixelSize MARKER_EXTENT{.width = 3840, .height = 2160};
constexpr std::uint32_t MARKER_COLOUR = 0xFF00FF;  // OverlayParams::cursorColour, 0xRRGGBB

// The main view at `zoom` around the image centre, with `markers` in MARKER_COLOUR.
ViewParams markerParams(double zoom, MarkerParams markers) {
    ViewParams params{
        .view = {.mode = dvv::media::ViewMode::Zoom,
                 .zoom = zoom,
                 .centre = {.x = MARKER_IMAGE.width / 2.0, .y = MARKER_IMAGE.height / 2.0},
                 .image = MARKER_IMAGE},
        .markers = markers};
    params.overlay.cursorColour = MARKER_COLOUR;
    return params;
}

// frame() of `params` (FrameSet version 1, frame `number`), submitted; the output's bytes.
std::vector<std::byte> presentMarkers(GpuTestEnvironment& env,
                                      const rawreel::testing::InspectFrame& frame,
                                      ViewRenderer& renderer, const ViewParams& params,
                                      std::uint64_t number) {
    const rawreel::ViewSnapshot snapshot{.feed = {.frames = frame.frames.share(), .version = 1},
                                         .params = params};
    auto presented = renderer.frame(
        {.extent = MARKER_EXTENT, .frameNumber = number, .previousFrameCompleted = number > 0},
        snapshot);
    REQUIRE_MESSAGE(presented, (presented ? std::string() : presented.error().describe()));
    if (presented->record) {
        auto done =
            rawreel::testing::submitAndWait(*frame.context, *env.cpu, std::move(presented->record));
        REQUIRE_MESSAGE(done, (done ? std::string() : done.error().describe()));
    }
    auto bytes = rawreel::testing::readImage(*frame.context, *env.cpu, presented->image, 4);
    REQUIRE_MESSAGE(bytes, (bytes ? std::string() : bytes.error().describe()));
    return std::move(*bytes);
}

std::array<std::uint8_t, 3> rgbAt(const std::vector<std::byte>& bytes, std::int32_t x,
                                  std::int32_t y) {
    const std::size_t at =
        ((static_cast<std::size_t>(y) * MARKER_EXTENT.width) + static_cast<std::size_t>(x)) * 4;
    return {std::to_integer<std::uint8_t>(bytes.at(at)),
            std::to_integer<std::uint8_t>(bytes.at(at + 1)),
            std::to_integer<std::uint8_t>(bytes.at(at + 2))};
}

constexpr std::array<std::uint8_t, 3> MARKER_RGB{0xFF, 0x00, 0xFF};
constexpr std::array<std::uint8_t, 3> RIM_RGB{0, 0, 0};

std::uint64_t areaOf(const std::vector<dvv::media::PixelRect>& rects) {
    std::uint64_t area = 0;
    for (const dvv::media::PixelRect& rect : rects) {
        area += std::uint64_t{rect.width} * rect.height;
    }
    return area;
}

// markerRects of `before`, then of `after`, for `params`'s view over the whole MARKER_EXTENT.
std::vector<dvv::media::PixelRect> expectedParts(const ViewParams& params,
                                                 const MarkerParams& before,
                                                 const MarkerParams& after) {
    const dvv::media::ViewTransform view =
        dvv::media::resolveView(params.view, MARKER_EXTENT, MARKER_IMAGE);
    const rawreel::PassRegions regions =
        rawreel::passRegions(MARKER_EXTENT, params.overlay.strips, view);
    std::vector<dvv::media::PixelRect> parts =
        rawreel::markerRects(rawreel::pixelCells(before), view, regions);
    const std::vector<dvv::media::PixelRect> more =
        rawreel::markerRects(rawreel::pixelCells(after), view, regions);
    parts.insert(parts.end(), more.begin(), more.end());
    return parts;
}

}  // namespace

TEST_CASE("markers: a marker move dispatches only the marker rects") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, formatNamed("yuv420p10le"), MARKER_IMAGE);
        const auto renderer = inspectRenderer(frame);
        // No markers: today's pipeline.
        const std::vector<std::byte> plain =
            presentMarkers(env, frame, *renderer, markerParams(8, {}), 0);
        CHECK(!renderer->lastRecorded().inspectPipeline);
        const MarkerParams first{.pin = dvv::media::ImagePixel{.x = 320, .y = 180}};
        static_cast<void>(presentMarkers(env, frame, *renderer, markerParams(8, first), 1));
        const RecordedInfo before = renderer->lastRecorded();
        CHECK_FALSE(before.inspectPipeline);  // picture.slang and the marker pass
        CHECK(before.markerRecords == 0);
        const MarkerParams moved{.pin = dvv::media::ImagePixel{.x = 322, .y = 181}};
        const ViewParams params = markerParams(8, moved);
        const std::vector<std::byte> shown = presentMarkers(env, frame, *renderer, params, 2);
        const RecordedInfo info = renderer->lastRecorded();
        CHECK(info.markerRecords == 1);
        CHECK(info.pictureRecords == before.pictureRecords);
        CHECK(info.pictureParts == expectedParts(params, first, moved));
        CHECK(info.markerParts == info.pictureParts);
        CHECK(info.pictureParts.size() == 8);  // the box's four strips, old and new
        const std::uint64_t output = std::uint64_t{MARKER_EXTENT.width} * MARKER_EXTENT.height;
        CHECK(areaOf(info.pictureParts) * 1000 < output);
        CHECK(shown == renderInspect(env, frame, *renderer, MARKER_EXTENT, params));
        // The look: a 2 px box in the cursor colour, a 1 px dark rim outside it, the pixel itself
        // as without markers.
        const dvv::media::ViewTransform view =
            dvv::media::resolveView(params.view, MARKER_EXTENT, MARKER_IMAGE);
        const std::int32_t x = dvv::media::firstPixelOf(dvv::media::axisX(view), 322);
        const std::int32_t y = dvv::media::firstPixelOf(dvv::media::axisY(view), 181);
        CHECK(rgbAt(shown, x - 1, y + 3) == MARKER_RGB);
        CHECK(rgbAt(shown, x - 2, y + 3) == MARKER_RGB);
        CHECK(rgbAt(shown, x - 3, y + 3) == RIM_RGB);
        CHECK(rgbAt(shown, x + 3, y - 2) == MARKER_RGB);
        CHECK(rgbAt(shown, x + 3, y + 3) == rgbAt(plain, x + 3, y + 3));
        CHECK(rgbAt(shown, x - 4, y + 3) == rgbAt(plain, x - 4, y + 3));
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("markers: a move at the image edge with ruler strips equals a full render") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, formatNamed("yuv420p10le"), MARKER_IMAGE);
        const auto renderer = inspectRenderer(frame);
        std::uint64_t number = 0;
        const std::uint32_t right = MARKER_IMAGE.width - 1;
        const std::uint32_t bottom = MARKER_IMAGE.height - 1;
        // Each corner pin moves one pixel inward; the view is centred on the corner, so the
        // strips clip the pictureDrawn edge next to it.
        const std::array<std::array<dvv::media::ImagePixel, 2>, 2> moves{
            {{dvv::media::ImagePixel{.x = 0, .y = 0}, dvv::media::ImagePixel{.x = 1, .y = 1}},
             {dvv::media::ImagePixel{.x = right, .y = bottom},
              dvv::media::ImagePixel{.x = right - 1, .y = bottom - 1}}}};
        for (const double zoom : {8.0, 0.5}) {
            for (const auto& move : moves) {
                CAPTURE(zoom);
                CAPTURE(move[0].x);
                const auto at = [&](dvv::media::ImagePixel pin) {
                    ViewParams params = markerParams(zoom, {.pin = pin});
                    params.view.centre = {.x = move[0].x + 0.5, .y = move[0].y + 0.5};
                    params.overlay.strips = {.top = 30, .left = 30};
                    return params;
                };
                static_cast<void>(presentMarkers(env, frame, *renderer, at(move[0]), number++));
                const RecordedInfo before = renderer->lastRecorded();
                const ViewParams params = at(move[1]);
                const std::vector<std::byte> shown =
                    presentMarkers(env, frame, *renderer, params, number++);
                const RecordedInfo info = renderer->lastRecorded();
                CHECK(info.markerRecords == before.markerRecords + 1);
                CHECK(info.pictureRecords == before.pictureRecords);
                CHECK(shown == renderInspect(env, frame, *renderer, MARKER_EXTENT, params));
            }
        }
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("markers: a move with the inspect pipeline equals a full render") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, formatNamed("yuv420p10le"), MARKER_IMAGE);
        const auto renderer = inspectRenderer(frame);
        const auto at = [](dvv::media::ImagePixel pin) {
            ViewParams params = cellLineParams();
            const ViewParams marked = markerParams(CELL_ZOOM, {.pin = pin});
            params.view = marked.view;
            params.overlay.cursorColour = MARKER_COLOUR;
            params.markers = marked.markers;
            return params;
        };
        // Two frames with the first pin: the first one uploads the value strip.
        for (const std::uint64_t number : {0U, 1U}) {
            static_cast<void>(
                presentMarkers(env, frame, *renderer, at({.x = 320, .y = 180}), number));
        }
        const RecordedInfo before = renderer->lastRecorded();
        CHECK(before.inspectPipeline);
        const ViewParams params = at({.x = 321, .y = 181});
        const std::vector<std::byte> shown = presentMarkers(env, frame, *renderer, params, 2);
        const RecordedInfo info = renderer->lastRecorded();
        // With the values on a pin move takes a full picture pass (the parts path runs with the
        // padding marks, "markers: the padding check and markers together"); either way the
        // bytes equal a full render.
        CHECK(info.markerRecords + info.pictureRecords ==
              before.markerRecords + before.pictureRecords + 1);
        CHECK(shown == renderInspect(env, frame, *renderer, MARKER_EXTENT, params));
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("markers: an outline move dispatches its edge strips") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, formatNamed("yuv420p10le"), MARKER_IMAGE);
        const auto renderer = inspectRenderer(frame);
        const std::vector<std::byte> plain =
            presentMarkers(env, frame, *renderer, markerParams(4, {}), 0);
        const MarkerParams first{.outline =
                                     ImageRect{.x = 300, .y = 170, .width = 20, .height = 10}};
        static_cast<void>(presentMarkers(env, frame, *renderer, markerParams(4, first), 1));
        const RecordedInfo before = renderer->lastRecorded();
        const MarkerParams moved{.outline =
                                     ImageRect{.x = 301, .y = 172, .width = 20, .height = 10}};
        const ViewParams params = markerParams(4, moved);
        const std::vector<std::byte> shown = presentMarkers(env, frame, *renderer, params, 2);
        const RecordedInfo info = renderer->lastRecorded();
        CHECK(info.markerRecords == before.markerRecords + 1);
        CHECK(info.pictureRecords == before.pictureRecords);
        REQUIRE(info.pictureParts == expectedParts(params, first, moved));
        REQUIRE(info.pictureParts.size() == 8);  // four edge strips, old and new
        // A 2 px line: the top strip of the new outline.
        CHECK(info.pictureParts[4].height == 2);
        CHECK(info.pictureParts[4].width == (20 * 4) + 4);
        CHECK(shown == renderInspect(env, frame, *renderer, MARKER_EXTENT, params));
        // Dashed 4 / 4 from the line's corner.
        const dvv::media::PixelRect top = info.pictureParts[4];
        const auto x0 = static_cast<std::int32_t>(top.x);
        const auto y0 = static_cast<std::int32_t>(top.y);
        for (std::int32_t t = 0; t < 8; ++t) {
            CAPTURE(t);
            for (std::int32_t row = 0; row < 2; ++row) {
                if (t < 4) {
                    CHECK(rgbAt(shown, x0 + t, y0 + row) == MARKER_RGB);
                } else {
                    CHECK(rgbAt(shown, x0 + t, y0 + row) == rgbAt(plain, x0 + t, y0 + row));
                }
            }
        }
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("markers: a dot below 6 px") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, formatNamed("yuv420p10le"), MARKER_IMAGE);
        const auto renderer = inspectRenderer(frame);
        const std::vector<std::byte> plain =
            renderInspect(env, frame, *renderer, MARKER_EXTENT, markerParams(2, {}));
        const ViewParams params =
            markerParams(2, {.pin = dvv::media::ImagePixel{.x = 100, .y = 50}});
        const std::vector<std::byte> shown =
            renderInspect(env, frame, *renderer, MARKER_EXTENT, params);
        CHECK_FALSE(renderer->lastRecorded().inspectPipeline);
        const dvv::media::ViewTransform view =
            dvv::media::resolveView(params.view, MARKER_EXTENT, MARKER_IMAGE);
        // The pin's span is 2 px: a 6 x 6 dot around its centre, a 1 px rim around the dot.
        const std::int32_t cx = dvv::media::firstPixelOf(dvv::media::axisX(view), 100) + 1;
        const std::int32_t cy = dvv::media::firstPixelOf(dvv::media::axisY(view), 50) + 1;
        CHECK(rgbAt(shown, cx - 3, cy - 3) == MARKER_RGB);
        CHECK(rgbAt(shown, cx + 2, cy + 2) == MARKER_RGB);
        CHECK(rgbAt(shown, cx, cy) == MARKER_RGB);
        CHECK(rgbAt(shown, cx - 4, cy) == RIM_RGB);
        CHECK(rgbAt(shown, cx + 3, cy) == RIM_RGB);
        CHECK(rgbAt(shown, cx, cy - 4) == RIM_RGB);
        CHECK(rgbAt(shown, cx - 5, cy) == rgbAt(plain, cx - 5, cy));
        CHECK(rgbAt(shown, cx + 4, cy) == rgbAt(plain, cx + 4, cy));
        const std::vector<dvv::media::PixelRect> rects =
            rawreel::markerRects(rawreel::pixelCells(params.markers), view,
                                 rawreel::passRegions(MARKER_EXTENT, {}, view));
        REQUIRE(rects.size() == 1);
        CHECK(rects[0] == dvv::media::PixelRect{static_cast<std::uint32_t>(cx - 4),
                                                static_cast<std::uint32_t>(cy - 4), 8, 8});
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("markers: the main view keeps picture.slang and the marker pass draws them") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, formatNamed("yuv420p10le"), MARKER_IMAGE);
        const auto renderer = inspectRenderer(frame);
        const MarkerParams markers{
            .pin = dvv::media::ImagePixel{.x = 320, .y = 180},
            .outline = ImageRect{.x = 300, .y = 170, .width = 40, .height = 20}};
        std::uint64_t number = 0;
        for (const double zoom : {0.5, 8.0}) {
            CAPTURE(zoom);
            const ViewParams params = markerParams(zoom, markers);
            const std::vector<std::byte> shown =
                presentMarkers(env, frame, *renderer, params, number++);
            const RecordedInfo info = renderer->lastRecorded();
            CHECK_FALSE(info.inspectPipeline);
            CHECK(info.pictureParts == std::vector{info.pictureDrawn});
            const dvv::media::ViewTransform view =
                dvv::media::resolveView(params.view, MARKER_EXTENT, MARKER_IMAGE);
            const std::vector<dvv::media::PixelRect> rects = rawreel::markerRects(
                rawreel::pixelCells(markers), view, rawreel::passRegions(MARKER_EXTENT, {}, view));
            CHECK_FALSE(rects.empty());
            CHECK(info.markerParts == rects);
            CHECK(shown == renderInspect(env, frame, *renderer, MARKER_EXTENT, params));
            CHECK(renderer->lastRecorded().markerParts == rects);
        }
        // The inspect pass draws the same markers in its shader, over its cell lines: every pixel
        // the marker pass changes, the inspect pass shows the same, and every pixel the inspect
        // pass's markers change, the marker pass shows the same (no extra marker pixels).
        const ViewParams params = markerParams(0.5, markers);
        const std::vector<std::byte> unmarked =
            renderInspect(env, frame, *renderer, MARKER_EXTENT, markerParams(0.5, {}));
        ViewParams inspected = params;
        inspected.inspect.on = true;
        inspected.inspect.numbers = true;
        inspected.inspect.glyphs = valueStrip(
            3, 5, [](std::uint32_t, std::uint32_t, std::uint32_t) { return std::uint8_t{255}; });
        const std::vector<std::byte> marked =
            renderInspect(env, frame, *renderer, MARKER_EXTENT, params);
        const std::vector<std::byte> inspect =
            renderInspect(env, frame, *renderer, MARKER_EXTENT, inspected);
        CHECK(renderer->lastRecorded().inspectPipeline);
        CHECK(renderer->lastRecorded().markerParts.empty());
        ViewParams inspected_unmarked = inspected;
        inspected_unmarked.markers = {};
        const std::vector<std::byte> inspect_unmarked =
            renderInspect(env, frame, *renderer, MARKER_EXTENT, inspected_unmarked);
        REQUIRE(marked.size() == inspect.size());
        REQUIRE(marked.size() == unmarked.size());
        REQUIRE(marked.size() == inspect_unmarked.size());
        std::size_t marker_pixels = 0;
        std::size_t inspect_marker_pixels = 0;
        std::size_t wrong = 0;
        std::size_t extra = 0;
        for (std::size_t at = 0; at < marked.size(); at += 4) {
            const auto pixel = [at](const std::vector<std::byte>& bytes) {
                return std::array{bytes[at], bytes[at + 1], bytes[at + 2]};
            };
            if (pixel(marked) != pixel(unmarked)) {
                ++marker_pixels;
                wrong += pixel(inspect) == pixel(marked) ? 0 : 1;
            }
            if (pixel(inspect) != pixel(inspect_unmarked)) {
                ++inspect_marker_pixels;
                extra += pixel(inspect) == pixel(marked) ? 0 : 1;
            }
        }
        CHECK(marker_pixels > 0);
        CHECK(inspect_marker_pixels > 0);
        CHECK(wrong == 0);
        CHECK(extra == 0);
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("markers: a cursor move still dispatches only ruler pixels") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, formatNamed("yuv420p10le"), MARKER_IMAGE);
        const auto renderer = inspectRenderer(frame);
        ViewParams params = markerParams(8, {.pin = dvv::media::ImagePixel{.x = 320, .y = 180}});
        params.overlay.strips = {.top = 30, .left = 30};
        params.overlay.tickBand = 20;
        params.overlay.markerDepth = 6;
        params.overlay.litDepth = 28;
        params.cursor = dvv::media::PointD{1000.5, 900.5};
        static_cast<void>(presentMarkers(env, frame, *renderer, params, 0));
        const RecordedInfo before = renderer->lastRecorded();
        CHECK_FALSE(before.inspectPipeline);
        params.cursor = dvv::media::PointD{1100.5, 950.5};
        static_cast<void>(presentMarkers(env, frame, *renderer, params, 1));
        const RecordedInfo info = renderer->lastRecorded();
        CHECK(info.rulerRecords == before.rulerRecords + 1);
        CHECK(info.pictureRecords == before.pictureRecords);
        CHECK(info.markerRecords == before.markerRecords);
        const dvv::media::ViewTransform view = dvv::media::resolveView(
            params.view, dvv::media::pictureRegion(MARKER_EXTENT, params.overlay.strips).size(),
            MARKER_IMAGE);
        const rawreel::PassRegions regions =
            rawreel::passRegions(MARKER_EXTENT, params.overlay.strips, view);
        CHECK(info.rulerDispatched == regions.bandRulers);
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

namespace {

constexpr dvv::platform::PixelSize PADDING_IMAGE{.width = 38, .height = 24};  // v210: even
constexpr std::uint32_t MARK_SIZE = 6;
constexpr std::uint32_t MARK_RIM = 1;
constexpr std::array<std::uint8_t, 3> WARNING{0xFF, 0x9F, 0x0A};  // #FF9F0A

// The padding mask of sample `sample` of `component` (0 outside the grid).
std::uint32_t paddingMaskOf(dvv::media::FormatId format, std::size_t component,
                            dvv::media::SamplePosition sample) {
    const auto word = dvv::media::storedWord(format, PADDING_IMAGE.width, PADDING_IMAGE.height,
                                             component, sample);
    return word ? word->paddingMask : 0;
}

// Five planted words, each its padding mask: the first sample, the last column, one inside, a
// chroma sample (the second component's; luma for a grey) and the last sample of the last
// component.
std::vector<rawreel::testing::PlantedBits> plantedPadding(dvv::media::FormatId format) {
    const dvv::media::PixelFormatDesc& desc = dvv::media::formatDesc(format);
    const auto grid = [&desc](std::size_t k) {
        const dvv::media::ComponentDesc& component = desc.components.at(k);
        return dvv::media::SamplePosition{
            .x = (PADDING_IMAGE.width + (1U << component.log2SubX) - 1U) >> component.log2SubX,
            .y = (PADDING_IMAGE.height + (1U << component.log2SubY) - 1U) >> component.log2SubY};
    };
    const std::size_t chroma = desc.componentCount > 1 ? 1 : 0;
    const std::size_t last = desc.componentCount - 1;
    const std::array<std::pair<std::size_t, dvv::media::SamplePosition>, 5> at{{
        {0, {.x = 0, .y = 0}},
        {0, {.x = grid(0).x - 1, .y = 5}},
        {0, {.x = 17, .y = 11}},
        {chroma, {.x = 3, .y = chroma == 0 ? 20U : 2U}},
        {last, {.x = grid(last).x - 1, .y = grid(last).y - 1}},
    }};
    std::vector<rawreel::testing::PlantedBits> planted;
    planted.reserve(at.size());
    for (const auto& [k, sample] : at) {
        planted.push_back(
            {.component = k, .sample = sample, .bits = paddingMaskOf(format, k, sample)});
    }
    return planted;
}

// The Picture panel with the padding check on (paddingMarks) or off.
ViewParams marksParams(bool on) {
    ViewParams params;
    params.paddingMarks = on;
    params.overlay.markSize = MARK_SIZE;
    params.overlay.markRim = MARK_RIM;
    return params;
}

struct Rendered {
    std::vector<std::byte> bytes;
    std::vector<ViewReadback> delivered;  // what the completion delivered
    RecordedInfo info;
};

// render() of `params` (of `frames`, else frame's), submitted, completed and read back.
Rendered renderCompleted(GpuTestEnvironment& env, const rawreel::testing::InspectFrame& frame,
                         ViewRenderer& renderer, dvv::platform::PixelSize extent,
                         const ViewParams& params, const rawreel::FrameSet* frames = nullptr) {
    Rendered out;
    renderer.setReadbackHandler(
        [&out](ViewReadback readback) { out.delivered.push_back(readback); });
    auto rendered = renderer.render(extent, frames != nullptr ? *frames : frame.frames, params);
    REQUIRE_MESSAGE(rendered, (rendered ? std::string() : rendered.error().describe()));
    auto done =
        rawreel::testing::submitAndWait(*frame.context, *env.cpu, std::move(rendered->record));
    REQUIRE_MESSAGE(done, (done ? std::string() : done.error().describe()));
    if (rendered->completed) {
        rendered->completed(done);
    }
    out.info = renderer.lastRecorded();
    auto bytes = rawreel::testing::readImage(*frame.context, *env.cpu, rendered->image, 4);
    REQUIRE_MESSAGE(bytes, (bytes ? std::string() : bytes.error().describe()));
    out.bytes = std::move(*bytes);
    renderer.setReadbackHandler({});
    return out;
}

// The pixels of a one-panel `extent` (no strips) that differ from the marks of the CPU tile map:
// a disc or ring pixel not the warning colour, a rim pixel not black, any other pixel not as
// `plain` shows it. `marked` counts the mark pixels.
std::size_t wrongMarks(const rawreel::testing::InspectFrame& frame, dvv::platform::PixelSize extent,
                       const std::vector<std::byte>& marks, const std::vector<std::byte>& plain,
                       std::size_t& marked) {
    const dvv::media::ViewTransform view =
        dvv::media::resolveView(dvv::media::ViewRequest{}, extent, PADDING_IMAGE);
    const rawreel::testing::PaddingTiles tiles = rawreel::testing::paddingTiles(
        frame.reference.format, PADDING_IMAGE, frame.bytes, view, extent, MARK_SIZE);
    std::size_t wrong = 0;
    marked = 0;
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            const std::uint32_t mark = rawreel::testing::paddingMarkAt(
                tiles, MARK_SIZE, MARK_RIM, static_cast<int>(x), static_cast<int>(y));
            const auto shown = pixelOf(marks, extent, x, y);
            std::array<std::uint8_t, 4> expected = pixelOf(plain, extent, x, y);
            if (mark == 1 || mark == 3) {
                expected = {WARNING[0], WARNING[1], WARNING[2], 255};
            } else if (mark == 2) {
                expected = {0, 0, 0, 255};
            }
            marked += mark != 0 ? 1 : 0;
            wrong += shown == expected ? 0 : 1;
        }
    }
    return wrong;
}

// The Planes panels of `extent` at zoom 8: the cells whose left frame pixel (the cell's first
// column, its middle row) is the warning colour while its sample is not flagged, or the reverse.
// `flagged` counts the flagged cells.
std::size_t wrongCellFlags(const rawreel::testing::InspectFrame& frame,
                           dvv::platform::PixelSize extent, const std::vector<std::byte>& bytes,
                           std::size_t& flagged) {
    const dvv::media::FormatId format = frame.reference.format;
    const dvv::media::PixelFormatDesc& desc = dvv::media::formatDesc(format);
    const std::vector<dvv::media::PixelRect> panels =
        rawreel::panelRects(extent, rawreel::panelGrid(extent, desc.componentCount));
    REQUIRE(panels.size() == desc.componentCount);
    std::size_t wrong = 0;
    flagged = 0;
    for (std::size_t k = 0; k < panels.size(); ++k) {
        const dvv::media::ComponentDesc& component = desc.components.at(k);
        const dvv::media::ViewTransform view =
            dvv::media::resolveView(dvv::media::ViewRequest{}, panels[k].size(), PADDING_IMAGE);
        REQUIRE(view.zoom() == 8.0);
        const dvv::media::AxisView ax = dvv::media::axisX(view);
        const dvv::media::AxisView ay = dvv::media::axisY(view);
        for (std::uint32_t y = 0; y < PADDING_IMAGE.height; ++y) {
            for (std::uint32_t x = 0; x < PADDING_IMAGE.width; ++x) {
                const dvv::media::SamplePosition sample = dvv::media::sampleOf(component, {x, y});
                const dvv::media::PixelBlock block =
                    dvv::media::blockOf(component, sample, PADDING_IMAGE);
                if (block.x != x || block.y != y) {
                    continue;  // one check per cell
                }
                const bool is_flagged =
                    rawreel::testing::paddingFlagged(format, PADDING_IMAGE, frame.bytes, k, sample);
                flagged += is_flagged ? 1 : 0;
                const std::int32_t top = dvv::media::firstPixelOf(ay, block.y);
                const std::int32_t bottom = dvv::media::firstPixelOf(ay, block.y + block.height);
                const auto shown = pixelOf(
                    bytes, extent,
                    static_cast<std::uint32_t>(panels[k].x + dvv::media::firstPixelOf(ax, block.x)),
                    static_cast<std::uint32_t>(panels[k].y + ((top + bottom) / 2)));
                const bool orange =
                    shown[0] == WARNING[0] && shown[1] == WARNING[1] && shown[2] == WARNING[2];
                wrong += orange == is_flagged ? 0 : 1;
            }
        }
    }
    return wrong;
}

// The Planes panels at zoom 8 in one row.
dvv::platform::PixelSize zoom8Extent(dvv::media::FormatId format) {
    const std::uint32_t count = dvv::media::formatDesc(format).componentCount;
    return {.width = count * 8 * PADDING_IMAGE.width, .height = 8 * PADDING_IMAGE.height};
}

constexpr dvv::platform::PixelSize MARKS_EXTENT{.width = 4 * PADDING_IMAGE.width,
                                                .height = 4 * PADDING_IMAGE.height};

}  // namespace

TEST_CASE("padding: planted garbage is counted and marked for every format with padding") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    for (const std::string_view name : PADDED_FORMATS) {
        CAPTURE(name);
        const dvv::media::FormatId format = formatNamed(name);
        const std::vector<rawreel::testing::PlantedBits> planted = plantedPadding(format);
        for (const rawreel::testing::PlantedBits& bits : planted) {
            CHECK(bits.bits != 0);  // every planted word has padding
        }
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, format, PADDING_IMAGE, planted);
        const std::uint64_t expected =
            rawreel::testing::paddingCount(format, PADDING_IMAGE, frame.bytes);
        CHECK(expected >= planted.size());
        const auto renderer = inspectRenderer(frame);
        // Level 1: the count and the marks.
        const Rendered plain =
            renderCompleted(env, frame, *renderer, MARKS_EXTENT, marksParams(false));
        const Rendered marks =
            renderCompleted(env, frame, *renderer, MARKS_EXTENT, marksParams(true));
        CHECK(marks.info.paddingPasses == plain.info.paddingPasses + 1);
        REQUIRE(marks.delivered.size() == 1);
        CHECK(marks.delivered.front().paddingSamples == std::optional<std::uint64_t>{expected});
        std::size_t marked = 0;
        CHECK(wrongMarks(frame, MARKS_EXTENT, marks.bytes, plain.bytes, marked) == 0);
        CHECK(marked > 0);
        // Level 2: the Planes panels' cell flags.
        const dvv::platform::PixelSize extent = zoom8Extent(format);
        const std::vector<std::byte> panels =
            renderInspect(env, frame, *renderer, extent, planesParams());
        std::size_t flagged = 0;
        CHECK(wrongCellFlags(frame, extent, panels, flagged) == 0);
        CHECK(flagged >= planted.size());
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("padding: the pin's mark stays visible over flagged neighbours") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        // gray10le: every pixel its own sample with padding bits in its stored word. The 8
        // neighbours of the pin are flagged, the pin is not.
        const dvv::media::FormatId format = formatNamed("gray10le");
        const dvv::media::ImagePixel pin{.x = 10, .y = 8};
        std::vector<rawreel::testing::PlantedBits> planted;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx != 0 || dy != 0) {
                    const dvv::media::SamplePosition sample{.x = pin.x + dx, .y = pin.y + dy};
                    planted.push_back({.component = 0,
                                       .sample = sample,
                                       .bits = paddingMaskOf(format, 0, sample)});
                }
            }
        }
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, format, PADDING_IMAGE, planted);
        const auto renderer = inspectRenderer(frame);
        constexpr rawreel::ValueMetrics METRICS{
            .advance = 5, .lineHeight = 9, .padX = 4, .padY = 4};
        constexpr std::uint32_t ZOOM = 28;
        const dvv::platform::PixelSize extent{.width = ZOOM * PADDING_IMAGE.width,
                                              .height = ZOOM * PADDING_IMAGE.height};
        const auto strip =
            valueStrip(5, 7, [](std::uint32_t cell, std::uint32_t x, std::uint32_t y) {
                return static_cast<std::uint8_t>(((cell * 61U) + (x * 37U) + (y * 101U)) % 256U);
            });
        // Without coverage the values draw nothing: what `on` adds to it is the text.
        ViewParams params = valuesParams(
            valueStrip(5, 7,
                       [](std::uint32_t, std::uint32_t, std::uint32_t) { return std::uint8_t{0}; }),
            METRICS);
        const std::vector<std::byte> off = renderInspect(env, frame, *renderer, extent, params);
        params = valuesParams(strip, METRICS);
        const std::vector<std::byte> on = renderInspect(env, frame, *renderer, extent, params);
        params.markers.pin = pin;
        const std::vector<std::byte> shown = renderInspect(env, frame, *renderer, extent, params);
        const std::int64_t x0 = std::int64_t{pin.x} * ZOOM;
        const std::int64_t y0 = std::int64_t{pin.y} * ZOOM;
        const std::int64_t x1 = x0 + ZOOM;
        const std::int64_t y1 = y0 + ZOOM;
        constexpr std::int64_t LEG = 4;  // max(4, advance / 2)
        // The corner flag of the cell holding (px, py): its top-right triangle.
        const auto on_corner = [](std::int64_t px, std::int64_t py) {
            return ((ZOOM - 1) - (px % ZOOM)) + (py % ZOOM) < LEG;
        };
        // The neighbours are flagged: the left one's frame, mid-height, is the warning colour.
        const auto left = pixelOf(on, extent, static_cast<std::uint32_t>(x0 - ZOOM),
                                  static_cast<std::uint32_t>(y0 + ZOOM / 2));
        REQUIRE(left[0] == WARNING[0]);
        REQUIRE(left[1] == WARNING[1]);
        std::size_t marks = 0;
        std::size_t on_text = 0;
        std::size_t on_corners = 0;
        std::size_t ring_free = 0;  // the ring's pixels that are neither text nor a corner flag
        std::size_t corners = 0;    // the ring's corner flag pixels
        for (std::uint32_t py = 0; py < extent.height; ++py) {
            for (std::uint32_t px = 0; px < extent.width; ++px) {
                const bool in_cell = px >= x0 && px < x1 && py >= y0 && py < y1;
                const bool in_ring =
                    !in_cell && px >= x0 - 3 && px < x1 + 3 && py >= y0 - 3 && py < y1 + 3;
                const bool is_text = pixelOf(off, extent, px, py) != pixelOf(on, extent, px, py);
                const bool is_mark = pixelOf(on, extent, px, py) != pixelOf(shown, extent, px, py);
                const bool is_corner = on_corner(px, py) && !in_cell;
                marks += is_mark ? 1 : 0;
                on_text += is_mark && is_text ? 1 : 0;
                on_corners += is_mark && is_corner ? 1 : 0;
                corners += in_ring && is_corner ? 1 : 0;
                ring_free += in_ring && !is_text && !is_corner ? 1 : 0;
            }
        }
        CHECK(corners > 0);
        CHECK(on_text == 0);
        CHECK(on_corners == 0);
        CHECK(marks * 2 >= ring_free);
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("padding: clean clips give no marks and count 0") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    for (const std::string_view name : PADDED_FORMATS) {
        CAPTURE(name);
        const dvv::media::FormatId format = formatNamed(name);
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, format, PADDING_IMAGE);
        REQUIRE(rawreel::testing::paddingCount(format, PADDING_IMAGE, frame.bytes) == 0);
        const auto renderer = inspectRenderer(frame);
        const Rendered plain =
            renderCompleted(env, frame, *renderer, MARKS_EXTENT, marksParams(false));
        const Rendered marks =
            renderCompleted(env, frame, *renderer, MARKS_EXTENT, marksParams(true));
        REQUIRE(marks.delivered.size() == 1);
        CHECK(marks.delivered.front().paddingSamples == std::optional<std::uint64_t>{0});
        CHECK(marks.bytes == plain.bytes);
        const dvv::platform::PixelSize extent = zoom8Extent(format);
        std::size_t flagged = 0;
        CHECK(wrongCellFlags(frame, extent,
                             renderInspect(env, frame, *renderer, extent, planesParams()),
                             flagged) == 0);
        CHECK(flagged == 0);
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("padding: formats without padding never flag") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    for (const std::string_view name : {"yuv420p", "yuv444p16le", "gray16le", "rgba64le"}) {
        CAPTURE(name);
        const dvv::media::FormatId format = formatNamed(name);
        // Every word all ones: every byte 0xFF.
        std::vector<rawreel::testing::PlantedBits> planted;
        const dvv::media::PixelFormatDesc& desc = dvv::media::formatDesc(format);
        for (std::size_t k = 0; k < desc.componentCount; ++k) {
            const dvv::media::ComponentDesc& component = desc.components.at(k);
            for (std::uint32_t y = 0; y < PADDING_IMAGE.height >> component.log2SubY; ++y) {
                for (std::uint32_t x = 0; x < PADDING_IMAGE.width >> component.log2SubX; ++x) {
                    CHECK(paddingMaskOf(format, k, {.x = x, .y = y}) == 0);
                    planted.push_back({.component = k, .sample = {.x = x, .y = y}, .bits = ~0U});
                }
            }
        }
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, format, PADDING_IMAGE, planted);
        CHECK(std::ranges::all_of(frame.bytes, [](std::byte b) { return b == std::byte{0xFF}; }));
        const auto renderer = inspectRenderer(frame);
        const Rendered plain =
            renderCompleted(env, frame, *renderer, MARKS_EXTENT, marksParams(false));
        const Rendered marks =
            renderCompleted(env, frame, *renderer, MARKS_EXTENT, marksParams(true));
        REQUIRE(marks.delivered.size() == 1);
        CHECK(marks.delivered.front().paddingSamples == std::optional<std::uint64_t>{0});
        CHECK(marks.bytes == plain.bytes);
        const dvv::platform::PixelSize extent = zoom8Extent(format);
        std::size_t flagged = 0;
        CHECK(wrongCellFlags(frame, extent,
                             renderInspect(env, frame, *renderer, extent, planesParams()),
                             flagged) == 0);
        CHECK(flagged == 0);
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("padding: the check is no pass while off") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const dvv::media::FormatId format = formatNamed("p010le");
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, format, PADDING_IMAGE, plantedPadding(format));
        const auto renderer = inspectRenderer(frame);
        const Rendered plain =
            renderCompleted(env, frame, *renderer, MARKS_EXTENT, marksParams(false));
        CHECK(plain.info.paddingPasses == 0);
        CHECK_FALSE(plain.info.inspectPipeline);
        CHECK(plain.delivered.empty());  // no readback
        const rawreel::ViewSnapshot snapshot{.feed = {.frames = frame.frames.share(), .version = 1},
                                             .params = marksParams(false)};
        auto presented = renderer->frame(
            {.extent = MARKS_EXTENT, .frameNumber = 0, .previousFrameCompleted = false}, snapshot);
        REQUIRE(presented.has_value());
        CHECK_FALSE(static_cast<bool>(presented->completed));
        auto done =
            rawreel::testing::submitAndWait(*frame.context, *env.cpu, std::move(presented->record));
        REQUIRE(done.has_value());
        CHECK(renderer->lastRecorded().paddingPasses == 0);
        CHECK_FALSE(renderer->lastRecorded().inspectPipeline);
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("padding: a new frame with the check on keeps the layer block") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const dvv::media::FormatId format = formatNamed("p010le");
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, format, PADDING_IMAGE, plantedPadding(format));
        const std::uint64_t expected =
            rawreel::testing::paddingCount(format, PADDING_IMAGE, frame.bytes);
        const auto renderer = inspectRenderer(frame);
        std::vector<ViewReadback> delivered;
        renderer->setReadbackHandler(
            [&delivered](ViewReadback readback) { delivered.push_back(readback); });
        std::uint64_t uploads = 0;
        for (std::uint64_t version = 1; version <= 3; ++version) {
            CAPTURE(version);
            const rawreel::ViewSnapshot snapshot{
                .feed = {.frames = frame.frames.share(), .version = version},
                .params = marksParams(true)};
            auto presented = renderer->frame({.extent = MARKS_EXTENT,
                                              .frameNumber = version - 1,
                                              .previousFrameCompleted = version > 1},
                                             snapshot);
            REQUIRE_MESSAGE(presented, (presented ? std::string() : presented.error().describe()));
            REQUIRE(static_cast<bool>(presented->record));
            REQUIRE(static_cast<bool>(presented->completed));
            auto done = rawreel::testing::submitAndWait(*frame.context, *env.cpu,
                                                        std::move(presented->record));
            REQUIRE_MESSAGE(done, (done ? std::string() : done.error().describe()));
            delivered.clear();
            presented->completed(done);
            REQUIRE(delivered.size() == 1);
            CHECK(delivered.front().paddingSamples == std::optional<std::uint64_t>{expected});
            CHECK_FALSE(delivered.front().probe.has_value());
            const RecordedInfo info = renderer->lastRecorded();
            CHECK(info.inspectPipeline);
            CHECK(info.paddingPasses == version);
            if (version == 1) {
                uploads = info.layerUploads;
                CHECK(uploads == 1);
            } else {
                CHECK(info.layerUploads == uploads);
            }
        }
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("padding: the reads need a Raw view that fits the shown frame") {
    const dvv::media::FormatId format = formatNamed("p010le");
    const dvv::media::FrameDesc desc{.format = format, .width = 38, .height = 24};
    // The last read window: the last chroma sample's word (the UV plane is the last).
    std::uint64_t needed = 0;
    for (std::size_t k = 0; k < 3; ++k) {
        const dvv::media::ComponentDesc& component =
            dvv::media::formatDesc(format).components.at(k);
        const auto word = dvv::media::storedWord(
            format, 38, 24, k,
            {.x = (38U >> component.log2SubX) - 1U, .y = (24U >> component.log2SubY) - 1U});
        REQUIRE(word.has_value());
        needed = std::max(needed, (word->byteOffset & ~std::uint64_t{3}) + 8);
    }
    CHECK(rawreel::rawReadsFit(desc, needed, desc, 38, 24));
    CHECK_FALSE(rawreel::rawReadsFit(desc, needed - 1, desc, 38, 24));  // past the buffer
    dvv::media::FrameDesc smaller = desc;
    smaller.width = 20;
    CHECK_FALSE(rawreel::rawReadsFit(smaller, needed, desc, 38, 24));  // the Raw view's size
    CHECK_FALSE(rawreel::rawReadsFit(desc, needed, smaller, 38, 24));  // the shown desc's size
    CHECK_FALSE(rawreel::rawReadsFit(desc, needed, desc, 38, 23));     // the display's size
    dvv::media::FrameDesc other = desc;
    other.format = formatNamed("yuv420p10le");
    CHECK_FALSE(rawreel::rawReadsFit(other, needed, desc, 38, 24));  // the Raw view's format
    CHECK_FALSE(rawreel::rawReadsFit(desc, needed, dvv::media::FrameDesc{}, 0, 0));
}

namespace {

// The Zoom window's padding flags at VALUES_ZOOM on an 8x8 frame.
constexpr dvv::platform::PixelSize FLAG_IMAGE{.width = 8, .height = 8};
constexpr int FLAG_ZOOM = static_cast<int>(VALUES_ZOOM);
// advance 12: corner flag legs of 6, so the flag shows inside the 2-pixel frame.
constexpr rawreel::ValueMetrics FLAG_METRICS{.advance = 12, .lineHeight = 9, .padX = 4, .padY = 4};

// Every third strip pixel fully covered (a glyph pixel in the text colour), the others mixed.
std::shared_ptr<const dvv::gpu::GlyphStrip> flagStrip() {
    return valueStrip(12, 7, [](std::uint32_t cell, std::uint32_t x, std::uint32_t y) {
        return (x + y) % 3 == 0
                   ? std::uint8_t{255}
                   : static_cast<std::uint8_t>(((cell * 61U) + (x * 37U) + (y * 101U)) % 256U);
    });
}

// Padding bits in three words of an 8x8 frame: luma (5, 2), the second component's (1, 1) (luma
// for a grey) and the last sample of the last component.
std::vector<rawreel::testing::PlantedBits> plantedFlags(dvv::media::FormatId format) {
    const dvv::media::PixelFormatDesc& desc = dvv::media::formatDesc(format);
    const std::size_t chroma = desc.componentCount > 1 ? 1 : 0;
    const std::size_t last = desc.componentCount - 1;
    const dvv::media::ComponentDesc& component = desc.components.at(last);
    const std::array<std::pair<std::size_t, dvv::media::SamplePosition>, 3> at{{
        {0, {.x = 5, .y = 2}},
        {chroma, {.x = 1, .y = 1}},
        {last,
         {.x = ((FLAG_IMAGE.width - 1) >> component.log2SubX),
          .y = ((FLAG_IMAGE.height - 1) >> component.log2SubY)}},
    }};
    std::vector<rawreel::testing::PlantedBits> planted;
    for (const auto& [k, sample] : at) {
        const auto word =
            dvv::media::storedWord(format, FLAG_IMAGE.width, FLAG_IMAGE.height, k, sample);
        REQUIRE(word.has_value());
        REQUIRE(word->paddingMask != 0);
        planted.push_back({.component = k, .sample = sample, .bits = word->paddingMask});
    }
    return planted;
}

// The `rect` of the `extent` image `bytes`.
std::vector<std::byte> cropped(const std::vector<std::byte>& bytes, dvv::platform::PixelSize extent,
                               const dvv::media::PixelRect& rect) {
    std::vector<std::byte> out;
    out.reserve(std::size_t{rect.width} * rect.height * 4);
    for (std::uint32_t y = 0; y < rect.height; ++y) {
        const std::size_t at =
            ((((std::size_t{static_cast<std::uint32_t>(rect.y) + y}) * extent.width) +
              static_cast<std::uint32_t>(rect.x)) *
             4);
        out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(at),
                   bytes.begin() + static_cast<std::ptrdiff_t>(at + (std::size_t{rect.width} * 4)));
    }
    return out;
}

// The warning-colour pixels of `bytes` in [x0, x1) x [y0, y1).
std::size_t warned(const std::vector<std::byte>& bytes, dvv::platform::PixelSize extent, int x0,
                   int x1, int y0, int y1) {
    std::size_t count = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const auto shown = pixelOf(bytes, extent, static_cast<std::uint32_t>(x),
                                       static_cast<std::uint32_t>(y));
            count += shown[0] == WARNING[0] && shown[1] == WARNING[1] && shown[2] == WARNING[2];
        }
    }
    return count;
}

bool isWarning(const std::vector<std::byte>& bytes, dvv::platform::PixelSize extent, int x, int y) {
    return warned(bytes, extent, x, x + 1, y, y + 1) == 1;
}

// The warning-colour pixels in cells that are neither a flagged cell nor one of its 4 neighbours
// (the cells checkCells visits with `near_flags_only`): a flag or mark there is a false positive.
// `structure`: a cell also counts as flagged when any component's sample (a chroma sample too, its
// label underlined) is flagged.
std::size_t warnedFarFromFlags(const rawreel::testing::InspectFrame& frame, const LayerBlock& block,
                               const std::vector<std::byte>& image, dvv::platform::PixelSize extent,
                               std::uint32_t zoom, bool structure = false) {
    const dvv::platform::PixelSize size{frame.reference.width, frame.reference.height};
    const auto flagged = [&](std::uint32_t x, std::uint32_t y) {
        bool any = flaggedCell(frame, block, x, y).has_value();
        for (std::size_t k = 0;
             structure && !any && k < std::min<std::size_t>(block.componentCount, 4); ++k) {
            const std::uint32_t sx = block.component.at(k) & 0xFU;
            const std::uint32_t sy = (block.component.at(k) >> rawreel::LAYER_SUB_Y_SHIFT) & 0xFU;
            any = rawreel::testing::paddingFlagged(frame.reference.format, size, frame.bytes, k,
                                                   {.x = x >> sx, .y = y >> sy});
        }
        return any;
    };
    std::size_t far = 0;
    for (std::uint32_t y = 0; y < frame.reference.height; ++y) {
        for (std::uint32_t x = 0; x < frame.reference.width; ++x) {
            bool near = false;
            for (const std::array<int, 2> d :
                 {std::array{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
                const auto nx = static_cast<std::uint32_t>(static_cast<int>(x) + d[0]);
                const auto ny = static_cast<std::uint32_t>(static_cast<int>(y) + d[1]);
                near = near || (nx < frame.reference.width && ny < frame.reference.height &&
                                flagged(nx, ny));
            }
            if (!near) {
                far += warned(image, extent, static_cast<int>(x * zoom),
                              static_cast<int>((x + 1) * zoom), static_cast<int>(y * zoom),
                              static_cast<int>((y + 1) * zoom));
            }
        }
    }
    return far;
}

// Collects the Warn lines logged while it lives; the other sinks stay.
class WarnLog {
public:
    WarnLog() { dvv::Logger::instance().addSink(sink_, dvv::LogLevel::Warn); }
    WarnLog(const WarnLog&) = delete;
    WarnLog& operator=(const WarnLog&) = delete;
    WarnLog(WarnLog&&) = delete;
    WarnLog& operator=(WarnLog&&) = delete;
    ~WarnLog() { dvv::Logger::instance().setSinkLevel(*sink_, dvv::LogLevel::Off); }

    [[nodiscard]] std::size_t count(std::string_view text) const {
        dvv::Logger::instance().flush();
        return static_cast<std::size_t>(std::ranges::count_if(
            sink_->lines(), [text](const std::string& line) { return line.contains(text); }));
    }

private:
    std::shared_ptr<dvv::MemorySink> sink_ = std::make_shared<dvv::MemorySink>();
};

}  // namespace

TEST_CASE("padding: the Zoom window flags equal the CPU reference in Structure and Planes") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    const auto strip = flagStrip();
    std::vector<std::string_view> names(PADDED_FORMATS.begin(), PADDED_FORMATS.end());
    names.emplace_back("yuv444p10le");  // three stacked lines, each its own word
    for (const std::string_view name : names) {
        CAPTURE(name);
        const dvv::media::FormatId format = formatNamed(name);
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, format, FLAG_IMAGE, plantedFlags(format));
        // The same seeded codes without padding bits: the planes' greys without any flag.
        const rawreel::testing::InspectFrame clean =
            rawreel::testing::openInspectFrame(env, format, FLAG_IMAGE);
        const auto renderer = inspectRenderer(frame);
        const auto clean_renderer = inspectRenderer(clean);
        const dvv::media::FrameDesc desc = frame.frames.lease(ViewRole::Planes).desc();
        const ViewParams params = valuesParams(strip, FLAG_METRICS);
        // Structure: the frame and the corner flag of a flagged pixel, the warning number of its
        // flagged line, the underlined label component of a flagged chroma sample.
        const dvv::platform::PixelSize extent{.width = FLAG_IMAGE.width * VALUES_ZOOM,
                                              .height = FLAG_IMAGE.height * VALUES_ZOOM};
        const std::vector<std::byte> off = renderInspect(env, frame, *renderer, extent, {});
        const std::vector<std::byte> on = renderInspect(env, frame, *renderer, extent, params);
        const LayerBlock block = rawreel::layerBlock(params, desc, 0, PanelKind::Picture);
        REQUIRE((block.flags & rawreel::LAYER_CELL_FLAGS) != 0);
        const CellCheck structure =
            checkCells(frame, block, *strip, VALUES_ZOOM, off, on, extent, {0, FLAG_IMAGE.width},
                       {0, FLAG_IMAGE.height}, rawreel::cellShapes(params, desc), true);
        CHECK(structure.wrong == 0);
        CHECK(structure.flagged > 0);
        CHECK(warnedFarFromFlags(frame, block, on, extent, VALUES_ZOOM, true) == 0);
        // Planes: each panel's flagged sample blocks.
        ViewParams planes = params;
        planes.inspect.panels = PanelKind::Planes;
        ViewParams planes_off = planesParams();
        const std::uint32_t count = dvv::media::formatDesc(format).componentCount;
        const dvv::platform::PixelSize wide{.width = count * extent.width, .height = extent.height};
        const std::vector<std::byte> planes_on = renderInspect(env, frame, *renderer, wide, planes);
        const std::vector<std::byte> planes_plain =
            renderInspect(env, clean, *clean_renderer, wide, planes_off);
        const std::vector<dvv::media::PixelRect> panels =
            rawreel::panelRects(wide, rawreel::panelGrid(wide, count));
        REQUIRE(panels.size() == count);
        std::size_t panel_flags = 0;
        for (std::uint32_t k = 0; k < count; ++k) {
            CAPTURE(k);
            REQUIRE(panels[k].size() == extent);
            const LayerBlock plane = rawreel::layerBlock(planes, desc, k, PanelKind::Planes);
            const CellCheck check = checkCells(
                frame, plane, *strip, VALUES_ZOOM, cropped(planes_plain, wide, panels[k]),
                cropped(planes_on, wide, panels[k]), extent, {0, FLAG_IMAGE.width},
                {0, FLAG_IMAGE.height}, rawreel::cellShapes(planes, desc), true);
            CHECK(check.wrong == 0);
            CHECK(warnedFarFromFlags(frame, plane, cropped(planes_on, wide, panels[k]), extent,
                                     VALUES_ZOOM) == 0);
            panel_flags += check.flagged;
        }
        CHECK(panel_flags > 0);
        if (name == "p010le") {
            // Spot checks on the GPU picture alone. Luma (5, 2) is flagged, (4, 2) is not.
            const int z = FLAG_ZOOM;
            CHECK(isWarning(on, extent, (6 * z) - 3, (2 * z) + 2));  // inside the corner flag
            CHECK_FALSE(isWarning(on, extent, (5 * z) - 3, (2 * z) + 2));
            CHECK(warned(on, extent, (5 * z) + 8, (6 * z) - 8, (2 * z) + 8, (3 * z) - 8) > 0);
            CHECK(warned(on, extent, (4 * z) + 8, (5 * z) - 8, (2 * z) + 8, (3 * z) - 8) == 0);
            // U (1, 1) is flagged: the label of block (2, 2) is underlined, block (0, 2)'s not.
            CHECK(warned(on, extent, (2 * z) + 8, (4 * z) - 8, (3 * z) + 40, (4 * z) - 3) > 0);
            CHECK(warned(on, extent, 8, (2 * z) - 8, (3 * z) + 40, (4 * z) - 3) == 0);
            // The U panel: sample (1, 1)'s cell has its corner flag and a warning number.
            const auto u = static_cast<int>(panels[1].x);
            CHECK(isWarning(planes_on, wide, u + (4 * z) - 3, (2 * z) + 2));
            CHECK_FALSE(isWarning(planes_on, wide, u + (2 * z) - 3, (2 * z) + 2));
            CHECK(warned(planes_on, wide, u + (2 * z) + 8, u + (4 * z) - 8, (2 * z) + 8,
                         (4 * z) - 8) > 0);
        }
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("padding: a Raw view that does not fit turns the flags and the marks off, logged once") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const dvv::media::FormatId format = formatNamed("p010le");
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, format, PADDING_IMAGE, plantedPadding(format));
        // The Raw view of a smaller frame of the same format.
        const dvv::platform::PixelSize small{.width = 20, .height = 24};
        const rawreel::testing::InspectFrame other =
            rawreel::testing::openInspectFrame(env, format, small);
        rawreel::FrameSet mixed = frame.frames.share();
        mixed.leases.at(static_cast<std::size_t>(ViewRole::Raw)).at(0) =
            other.frames.lease(ViewRole::Raw).share();
        const WarnLog log;
        const auto renderer = inspectRenderer(frame);
        const Rendered plain =
            renderCompleted(env, frame, *renderer, MARKS_EXTENT, marksParams(false));
        for (int round = 0; round < 2; ++round) {
            CAPTURE(round);
            const Rendered marks =
                renderCompleted(env, frame, *renderer, MARKS_EXTENT, marksParams(true), &mixed);
            CHECK(marks.info.paddingPasses == 0);  // no padding check ran
            CHECK(std::ranges::none_of(marks.delivered, [](const ViewReadback& readback) {
                return readback.paddingSamples.has_value();
            }));
            CHECK(marks.bytes == plain.bytes);  // no marks
        }
        // The Zoom window's Planes panels: no cell flags (the planted frame's flags otherwise).
        const dvv::platform::PixelSize extent = zoom8Extent(format);
        auto rendered = renderer->render(extent, mixed, planesParams());
        REQUIRE_MESSAGE(rendered, (rendered ? std::string() : rendered.error().describe()));
        auto done =
            rawreel::testing::submitAndWait(*frame.context, *env.cpu, std::move(rendered->record));
        REQUIRE_MESSAGE(done, (done ? std::string() : done.error().describe()));
        auto bytes = rawreel::testing::readImage(*frame.context, *env.cpu, rendered->image, 4);
        REQUIRE_MESSAGE(bytes, (bytes ? std::string() : bytes.error().describe()));
        CHECK(warned(*bytes, extent, 0, static_cast<int>(extent.width), 0,
                     static_cast<int>(extent.height)) == 0);
        std::size_t flagged = 0;
        CHECK(wrongCellFlags(frame, extent,
                             renderInspect(env, frame, *renderer, extent, planesParams()),
                             flagged) == 0);
        CHECK(flagged > 0);  // the fitting Raw view flags the same frame
        CHECK(log.count("the Raw view does not match the shown frame") == 1);
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("markers: the padding check and markers together") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const dvv::media::FormatId format = formatNamed("p010le");
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, format, PADDING_IMAGE, plantedPadding(format));
        const std::uint64_t expected =
            rawreel::testing::paddingCount(format, PADDING_IMAGE, frame.bytes);
        const auto renderer = inspectRenderer(frame);
        std::vector<ViewReadback> delivered;
        renderer->setReadbackHandler(
            [&delivered](ViewReadback readback) { delivered.push_back(readback); });
        // frame() of `params` (FrameSet version 1, frame `number`), submitted and completed.
        const auto present = [&](const ViewParams& params, std::uint64_t number) {
            const rawreel::ViewSnapshot snapshot{
                .feed = {.frames = frame.frames.share(), .version = 1}, .params = params};
            auto presented = renderer->frame({.extent = MARKS_EXTENT,
                                              .frameNumber = number,
                                              .previousFrameCompleted = number > 0},
                                             snapshot);
            REQUIRE_MESSAGE(presented, (presented ? std::string() : presented.error().describe()));
            delivered.clear();
            if (presented->record) {
                auto done = rawreel::testing::submitAndWait(*frame.context, *env.cpu,
                                                            std::move(presented->record));
                REQUIRE_MESSAGE(done, (done ? std::string() : done.error().describe()));
                if (presented->completed) {
                    presented->completed(done);
                }
            }
            auto bytes = rawreel::testing::readImage(*frame.context, *env.cpu, presented->image, 4);
            REQUIRE_MESSAGE(bytes, (bytes ? std::string() : bytes.error().describe()));
            return std::move(*bytes);
        };
        const auto count = [](const std::vector<std::byte>& bytes,
                              std::array<std::uint8_t, 3> rgb) {
            std::size_t n = 0;
            for (std::size_t at = 0; at + 3 < bytes.size(); at += 4) {
                n += std::to_integer<std::uint8_t>(bytes[at]) == rgb[0] &&
                             std::to_integer<std::uint8_t>(bytes[at + 1]) == rgb[1] &&
                             std::to_integer<std::uint8_t>(bytes[at + 2]) == rgb[2]
                         ? 1
                         : 0;
            }
            return n;
        };
        ViewParams params = marksParams(true);
        params.overlay.cursorColour = MARKER_COLOUR;
        params.markers = {.pin = dvv::media::ImagePixel{.x = 17, .y = 11}};
        // The check on: the inspect pipeline draws the marks and the markers, no marker pass.
        const std::vector<std::byte> first = present(params, 0);
        const RecordedInfo before = renderer->lastRecorded();
        CHECK(before.inspectPipeline);
        CHECK(before.markerParts.empty());
        CHECK(before.paddingPasses == 1);
        REQUIRE(delivered.size() == 1);
        CHECK(delivered.front().paddingSamples == std::optional<std::uint64_t>{expected});
        CHECK(count(first, MARKER_RGB) > 0);
        CHECK(count(first, WARNING) > 0);
        // A pin move redraws the marker rects alone: no padding check, no count, the marks drawn
        // from the kept tile map.
        params.markers = {.pin = dvv::media::ImagePixel{.x = 5, .y = 7}};
        const std::vector<std::byte> moved = present(params, 1);
        const RecordedInfo info = renderer->lastRecorded();
        CHECK(info.inspectPipeline);
        CHECK(info.markerRecords == before.markerRecords + 1);
        CHECK(info.pictureRecords == before.pictureRecords);
        CHECK(info.paddingPasses == before.paddingPasses);
        CHECK(info.markerParts.empty());
        CHECK_FALSE(info.pictureParts.empty());
        CHECK(info.pictureParts != std::vector{info.pictureDrawn});
        for (const ViewReadback& readback : delivered) {
            CHECK_FALSE(readback.paddingSamples.has_value());
        }
        CHECK(moved != first);
        CHECK(count(moved, WARNING) > 0);
        const Rendered full = renderCompleted(env, frame, *renderer, MARKS_EXTENT, params);
        CHECK(moved == full.bytes);
        // The check off: picture.slang and the marker pass.
        params.paddingMarks = false;
        const std::vector<std::byte> unchecked = present(params, 2);
        const RecordedInfo off = renderer->lastRecorded();
        CHECK_FALSE(off.inspectPipeline);
        CHECK_FALSE(off.markerParts.empty());
        CHECK(off.paddingPasses == full.info.paddingPasses);
        CHECK(count(unchecked, MARKER_RGB) == count(moved, MARKER_RGB));
        CHECK(count(unchecked, WARNING) == 0);
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}
