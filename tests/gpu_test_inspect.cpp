// The inspect pipeline's GPU tests: the layer block's layout as the shader reads it, the support
// frames the inspect layers' tests draw, and the plane panels the inspect pipeline draws.

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
#include "dvv/gpu/command_recorder.h"
#include "dvv/gpu/compute_pipeline.h"
#include "dvv/gpu/context_vulkan.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/gpu/glyph_strip_vulkan.h"
#include "dvv/gpu/gpu_instance.h"
#include "dvv/gpu/image.h"
#include "dvv/gpu/resources.h"
#include "dvv/media/frame_store.h"
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
#include "rawreel_test_shaders/layer_block_probe.h"
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
using rawreel::ViewParams;
using rawreel::testing::CELL_ZOOM;
using rawreel::testing::cellBox;
using rawreel::testing::CellCheck;
using rawreel::testing::cellHit;
using rawreel::testing::cellLineParams;
using rawreel::testing::checkCells;
using rawreel::testing::digitStrip;
using rawreel::testing::encodedOf;
using rawreel::testing::flaggedCell;
using rawreel::testing::formatNamed;
using rawreel::testing::GpuTestEnvironment;
using rawreel::testing::INSPECT_IMAGE;
using rawreel::testing::inspectRenderer;
using rawreel::testing::overCellLine;
using rawreel::testing::pixelOf;
using rawreel::testing::planesParams;
using rawreel::testing::renderInspect;
using rawreel::testing::VALUES_ZOOM;
using rawreel::testing::valuesParams;
using rawreel::testing::valueStrip;

constexpr std::size_t BLOCK_WORDS = sizeof(LayerBlock) / sizeof(std::uint32_t);

std::shared_ptr<dvv::gpu::ExecutionContext> bestContext(const GpuTestEnvironment& env) {
    std::shared_ptr<dvv::gpu::ExecutionContext> result;
    if (const auto device = env.gpuInstance->bestDevice(); device) {
        auto context = env.gpuInstance->createContext(*device, {});
        REQUIRE_MESSAGE(context, (context ? std::string() : context.error().describe()));
        result = std::move(*context);
    } else {
        FAIL("no usable Vulkan device");
    }
    return result;
}

}  // namespace

TEST_CASE("layer block: the shader reads every field at the C++ offsets") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const std::shared_ptr<dvv::gpu::ExecutionContext> context = bestContext(env);
        // A distinct value in every word: a field read at another offset gives another value.
        std::array<std::uint32_t, BLOCK_WORDS> expected{};
        for (std::size_t i = 0; i < expected.size(); ++i) {
            expected[i] = 0x9E3779B9U * static_cast<std::uint32_t>(i + 1);
        }
        const auto block = std::bit_cast<LayerBlock>(expected);

        auto buffer = dvv::gpu::createBuffer(*context, {.size = sizeof(LayerBlock),
                                                        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                        .memory = dvv::gpu::MemoryUsage::HostUpload,
                                                        .name = "layer block"});
        REQUIRE_MESSAGE(buffer, (buffer ? std::string() : buffer.error().describe()));
        {
            dvv::gpu::MappedWrite write = (*buffer)->mapWrite();
            REQUIRE(write.bytes().size() >= sizeof(LayerBlock));
            std::memcpy(write.bytes().data(), &block, sizeof(LayerBlock));
            const dvv::Result<void> flushed = write.flush();
            REQUIRE_MESSAGE(flushed, (flushed ? std::string() : flushed.error().describe()));
        }
        auto image = dvv::gpu::createImage(
            *context, {.width = static_cast<std::uint32_t>(BLOCK_WORDS),
                       .height = 1,
                       .format = VK_FORMAT_R32_UINT,
                       .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                       .name = "layer block words"});
        REQUIRE_MESSAGE(image, (image ? std::string() : image.error().describe()));
        const std::uint64_t address = (*buffer)->deviceAddress();
        auto pipeline =
            dvv::gpu::createComputePipeline(*context, rawreel_test_shaders::layerBlockProbe(),
                                            sizeof(address), {.storageImages = 1});
        REQUIRE_MESSAGE(pipeline, (pipeline ? std::string() : pipeline.error().describe()));

        auto done = rawreel::testing::submitAndWait(
            *context, *env.cpu,
            [held_buffer = *buffer, held_image = *image, held_pipeline = *pipeline,
             address](dvv::gpu::CommandRecorder& recorder) {
                const std::array<dvv::gpu::GpuImage*, 1> images{held_image.get()};
                recorder.dispatch(*held_pipeline, std::as_bytes(std::span(&address, 1)), images,
                                  dvv::gpu::GroupCount{});
            });
        REQUIRE_MESSAGE(done, (done ? std::string() : done.error().describe()));
        auto bytes = rawreel::testing::readImage(*context, *env.cpu, *image, 4);
        REQUIRE_MESSAGE(bytes, (bytes ? std::string() : bytes.error().describe()));
        REQUIRE(bytes->size() == sizeof(LayerBlock));
        std::array<std::uint32_t, BLOCK_WORDS> read{};
        std::memcpy(read.data(), bytes->data(), sizeof(LayerBlock));
        for (std::size_t i = 0; i < read.size(); ++i) {
            CAPTURE(i * sizeof(std::uint32_t));  // the byte offset
            CHECK(read[i] == expected[i]);
        }
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("layer block: a Planes panel carries its plane, the stored words and the colours") {
    const dvv::media::FormatId format = formatNamed("yuv420p10le");
    const dvv::media::FrameDesc desc{.format = format, .width = 37, .height = 23};
    ViewParams params;
    params.inspect.on = true;
    params.inspect.panels = PanelKind::Planes;
    params.inspect.letterCells = {3, 5, 7, 0};
    params.overlay.cursorColour = 0x123456U;

    const LayerBlock block = rawreel::layerBlock(params, desc, 2, PanelKind::Planes);
    CHECK(block.flags == (rawreel::LAYER_PLANE_SOURCE | rawreel::LAYER_CELL_FLAGS));
    CHECK(block.sourceComponent == 2);
    CHECK(block.componentCount == 3);
    for (std::size_t k = 0; k < 3; ++k) {
        CAPTURE(k);
        const auto words = dvv::media::storedWordLayout(format, 37, 23, k);
        REQUIRE(words.has_value());
        CHECK(std::memcmp(&block.words.at(k), &*words, sizeof(*words)) == 0);
    }
    CHECK(block.words[3].wordBytes == 0);
    CHECK(block.component[0] == ((10U << 8U) | (3U << 16U)));
    CHECK(block.component[1] == (1U | (1U << 4U) | (10U << 8U) | (5U << 16U)));
    CHECK(block.component[2] == (1U | (1U << 4U) | (10U << 8U) | (7U << 16U)));
    CHECK(block.component[3] == 0);
    CHECK(block.colours0 == std::array{0x99808080U, 0xF2FFAA50U, 0xFFFFD09FU, 0xB8000000U});
    CHECK(block.warning == 0xFF0A9FFFU);
    CHECK(block.marker == 0xFF563412U);

    const LayerBlock picture = rawreel::layerBlock(params, desc, 0, PanelKind::Picture);
    CHECK(picture.flags == rawreel::LAYER_CELL_FLAGS);
    CHECK(picture.sourceComponent == 0);

    // The padding marks' rim width and the markers' rim colour are separate fields.
    ViewParams marked;
    marked.paddingMarks = true;
    marked.overlay.markRim = 1;
    marked.markers.pin = dvv::media::ImagePixel{.x = 4, .y = 5};
    const LayerBlock both = rawreel::layerBlock(marked, desc, 0, PanelKind::Picture);
    CHECK(both.flags == (rawreel::LAYER_MARKERS | rawreel::LAYER_PADDING_MARKS));
    CHECK(both.markRim == 1);
    CHECK(both.markerRim == rawreel::LAYER_MARKER_RIM_COLOUR);
}

TEST_CASE("inspect support: a frame carries its roles, its codes and the planted bits") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    const dvv::platform::PixelSize size{.width = 37, .height = 23};
    {
        const dvv::media::FormatId format = formatNamed("yuv420p10le");
        const std::array planted{rawreel::testing::PlantedBits{
            .component = 1, .sample = {.x = 3, .y = 2}, .bits = 0xFC00U}};
        rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, format, size, planted);
        for (const rawreel::ViewRole role :
             {rawreel::ViewRole::Display, rawreel::ViewRole::Planes, rawreel::ViewRole::Raw}) {
            CAPTURE(static_cast<int>(role));
            CHECK(static_cast<bool>(frame.frames.lease(role)));
        }
        const auto word = dvv::media::storedWord(format, 37, 23, 1, {.x = 3, .y = 2});
        REQUIRE(word.has_value());
        CHECK((dvv::media::readStoredWord(frame.bytes, 0, *word) & 0xFC00U) == 0xFC00U);
        // The padding bits change no code.
        CHECK(rawreel::testing::codeAt(frame.reference, 1, {.x = 6, .y = 4}) ==
              dvv::media::codeOf(dvv::media::readStoredWord(frame.bytes, 0, *word), word->shift,
                                 word->bits));
        CHECK(rawreel::testing::cellOf(format, 1, size, {.x = 36, .y = 22}) ==
              dvv::media::PixelBlock{.x = 36, .y = 22, .width = 1, .height = 1});
        CHECK(rawreel::testing::planeGrey(1023, 10) == 1.0F);
    }
    {
        rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, rawreel::testing::testYuv411p(), size);
        CHECK(static_cast<bool>(frame.frames.lease(rawreel::ViewRole::Planes)));
        CHECK(rawreel::testing::sampleAt(frame.reference.format, 1, {.x = 7, .y = 5}) ==
              dvv::media::SamplePosition{.x = 1, .y = 5});
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

namespace {

using rawreel::RecordedInfo;
using rawreel::ViewRenderer;

// How far, in bytes, a plane panel's shown byte may be from the unrounded grey
// code / (2^bits - 1) * 255: less than one step of the 8-bit output. Vulkan's float-to-UNORM
// conversion may return either of the two bytes nearest to grey * 255 (round to nearest is only
// recommended), so the bound is one step, not half of one. NVIDIA (RTX 2070 SUPER) lands one byte
// low for deep codes up to 0.06 of a step above a .5 boundary, 0.56 of a step from the grey; the
// other GPUs round to nearest. The comparison is in double, so a miss of exactly one byte stays
// above the bound. What it catches: at 8 bits any wrong code; at 10 bits errors of about 4 codes
// or more; at 14 bits about 64 codes or more; a wrong panel, a wrong bit depth or a doubly applied
// encode always.
constexpr double GREY_STEPS = 1.0;

// The bound, in bytes, for a cell line of alpha `alpha` (0..1) over the picture. The shader
// blends the line over the float picture value and stores once. The reference blends over `off`,
// the picture's own UNORM store, which is less than one step from that float value; the blend
// scales this error by (1 - alpha); the blended value is stored once more, less than one step. So
// |shown - reference * 255| < 1 + (1 - alpha): 1.4 for the cell line (alpha 0.6), 1.05 for the
// chroma outline (alpha 0.95). The check pins position, colour and alpha to about a step; a wrong
// alpha moves the result by far more.
double lineSteps(double alpha) {
    return 1.0 + (1.0 - alpha);
}

// Whether `shown` is `code` of a `bits`-bit component as grey, within GREY_STEPS bytes.
bool isGrey(std::uint8_t shown, std::uint32_t code, std::uint32_t bits) {
    const double maximum = static_cast<double>((std::uint64_t{1} << bits) - 1);
    const double grey = static_cast<double>(code) / maximum * 255.0;
    return std::abs(static_cast<double>(shown) - grey) < GREY_STEPS;
}

// The pixels of the picture regions of `panels` (render() of `params`, without a grid) that are
// not the code of the sample nearest to imageCoordinateAt as grey (isGrey), or not opaque. Counts
// every channel; CHECKs that each panel shows some image pixels.
std::size_t wrongGreys(const rawreel::testing::InspectFrame& frame, dvv::platform::PixelSize image,
                       const std::vector<std::byte>& bytes, dvv::platform::PixelSize extent,
                       const std::vector<dvv::media::PixelRect>& panels, const ViewParams& params) {
    const dvv::media::PixelFormatDesc& format = dvv::media::formatDesc(frame.reference.format);
    std::size_t wrong = 0;
    for (std::size_t k = 0; k < panels.size(); ++k) {
        const dvv::media::PixelRect region =
            dvv::media::pictureRegion(panels[k].size(), params.overlay.strips);
        const dvv::media::ViewTransform view =
            dvv::media::resolveView(params.view, region.size(), image);
        const dvv::media::AxisView ax = dvv::media::axisX(view);
        const dvv::media::AxisView ay = dvv::media::axisY(view);
        const std::uint32_t bits = format.components.at(k).bits;
        std::size_t shown_pixels = 0;
        for (std::int32_t py = 0; std::cmp_less(py, region.height); ++py) {
            const float v = dvv::media::imageCoordinateAt(ay, py);
            for (std::int32_t px = 0; std::cmp_less(px, region.width); ++px) {
                const float u = dvv::media::imageCoordinateAt(ax, px);
                if (u < 0.0F || v < 0.0F || u >= static_cast<float>(image.width) ||
                    v >= static_cast<float>(image.height)) {
                    continue;  // the background
                }
                ++shown_pixels;
                const std::uint32_t code = rawreel::testing::codeAt(
                    frame.reference, k,
                    {.x = static_cast<std::uint32_t>(u), .y = static_cast<std::uint32_t>(v)});
                const auto shown =
                    pixelOf(bytes, extent, static_cast<std::uint32_t>(panels[k].x + region.x + px),
                            static_cast<std::uint32_t>(panels[k].y + region.y + py));
                for (std::size_t c = 0; c < 3; ++c) {
                    wrong += isGrey(shown.at(c), code, bits) ? 0 : 1;
                }
                wrong += shown[3] == 255 ? 0 : 1;
            }
        }
        CHECK(shown_pixels > 0);
    }
    return wrong;
}

}  // namespace

TEST_CASE("inspect: a plane panel shows the code as grey") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, formatNamed("yuv420p10le"), INSPECT_IMAGE);
        const auto renderer = inspectRenderer(frame);
        // Three panels in one row, each 4x the image: Fit shows the image at zoom 4.
        const dvv::platform::PixelSize extent{.width = 3 * 4 * 37, .height = 4 * 23};
        const std::vector<std::byte> bytes =
            renderInspect(env, frame, *renderer, extent, planesParams());
        const RecordedInfo info = renderer->lastRecorded();
        CHECK(info.inspectPipeline);
        CHECK(info.panels == 3);
        CHECK(info.zoom == 4.0);
        const std::vector<dvv::media::PixelRect> panels =
            rawreel::panelRects(extent, rawreel::panelGrid(extent, 3));
        REQUIRE(panels.size() == 3);
        for (std::size_t k = 0; k < panels.size(); ++k) {
            CAPTURE(k);
            std::size_t wrong = 0;
            for (std::uint32_t y = 0; y < INSPECT_IMAGE.height; ++y) {
                for (std::uint32_t x = 0; x < INSPECT_IMAGE.width; ++x) {
                    const std::uint32_t code =
                        rawreel::testing::codeAt(frame.reference, k, {.x = x, .y = y});
                    // The grey is the code as an sRGB-encoded value.
                    const auto shown = pixelOf(
                        bytes, extent, static_cast<std::uint32_t>(panels[k].x) + (4 * x) + 1,
                        static_cast<std::uint32_t>(panels[k].y) + (4 * y) + 2);
                    for (std::size_t c = 0; c < 3; ++c) {
                        wrong += isGrey(shown.at(c), code, 10) ? 0 : 1;
                    }
                    wrong += shown[3] == 255 ? 0 : 1;
                }
            }
            CHECK(wrong == 0);
        }
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("inspect: a plane panel shows a 14-bit plane's codes as grey") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        // 32768 seeded 14-bit codes: nearly every code, each grey within GREY_EPSILON of
        // code / 16383.
        const dvv::platform::PixelSize image{.width = 256, .height = 128};
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, rawreel::testing::testGray14(), image);
        const auto renderer = inspectRenderer(frame);
        const dvv::platform::PixelSize extent{.width = 2 * 256, .height = 2 * 128};
        const ViewParams params = planesParams();
        const std::vector<std::byte> bytes = renderInspect(env, frame, *renderer, extent, params);
        CHECK(renderer->lastRecorded().inspectPipeline);
        CHECK(renderer->lastRecorded().panels == 1);
        CHECK(renderer->lastRecorded().zoom == 2.0);
        const std::vector<dvv::media::PixelRect> panels =
            rawreel::panelRects(extent, rawreel::panelGrid(extent, 1));
        CHECK(wrongGreys(frame, image, bytes, extent, panels, params) == 0);
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("inspect: plane panels zoomed out show the nearest sample's code") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const dvv::platform::PixelSize image{.width = 36, .height = 24};
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, formatNamed("yuv420p10le"), image);
        const auto renderer = inspectRenderer(frame);
        // Three panels in one row, each half the image: Fit shows it at zoom 0.5.
        const dvv::platform::PixelSize extent{.width = 3 * 18, .height = 12};
        const ViewParams params = planesParams();
        const std::vector<std::byte> bytes = renderInspect(env, frame, *renderer, extent, params);
        CHECK(renderer->lastRecorded().panels == 3);
        CHECK(renderer->lastRecorded().zoom == 0.5);
        const std::vector<dvv::media::PixelRect> panels =
            rawreel::panelRects(extent, rawreel::panelGrid(extent, 3));
        CHECK(wrongGreys(frame, image, bytes, extent, panels, params) == 0);
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("inspect: planes panels tile the extent and carry their own rulers") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        // Each panel count (2: a row, 3: a row, 4: a 2x2 grid and a 1x4 row), extents no count
        // divides. 397 x 251 fits the picture's height in the panels (the corners begin past the
        // panels' left edge), 251 x 397 its width (the top bands' rows begin past their top edge).
        struct Layout {
            dvv::media::FormatId format;
            std::uint32_t count = 0;
            dvv::platform::PixelSize extent;
            rawreel::PanelGrid grid;
        };
        const std::array<Layout, 5> layouts{{{.format = rawreel::testing::testYa10(),
                                              .count = 2,
                                              .extent = {.width = 397, .height = 251},
                                              .grid = {.columns = 2, .rows = 1}},
                                             {.format = formatNamed("yuv420p10le"),
                                              .count = 3,
                                              .extent = {.width = 397, .height = 251},
                                              .grid = {.columns = 3, .rows = 1}},
                                             {.format = formatNamed("rgba"),
                                              .count = 4,
                                              .extent = {.width = 397, .height = 251},
                                              .grid = {.columns = 2, .rows = 2}},
                                             {.format = formatNamed("rgba"),
                                              .count = 4,
                                              .extent = {.width = 251, .height = 397},
                                              .grid = {.columns = 2, .rows = 2}},
                                             {.format = formatNamed("rgba"),
                                              .count = 4,
                                              .extent = {.width = 397, .height = 61},
                                              .grid = {.columns = 4, .rows = 1}}}};
        for (const Layout& layout : layouts) {
            CAPTURE(layout.count);
            CAPTURE(layout.extent.width);
            const dvv::platform::PixelSize extent = layout.extent;
            const rawreel::testing::InspectFrame frame =
                rawreel::testing::openInspectFrame(env, layout.format, INSPECT_IMAGE);
            const auto renderer = inspectRenderer(frame);
            ViewParams params = planesParams();
            params.overlay.strips = {.top = 18, .left = 22};
            params.overlay.numbers = true;
            params.overlay.glyphs = digitStrip();  // the numbers, placed by the strips' thickness
            const std::vector<std::byte> bytes =
                renderInspect(env, frame, *renderer, extent, params);
            const RecordedInfo info = renderer->lastRecorded();
            CHECK(info.panels == layout.count);
            CHECK(info.inspectPipeline);
            CHECK(rawreel::panelGrid(extent, layout.count) == layout.grid);
            const std::vector<dvv::media::PixelRect> panels =
                rawreel::panelRects(extent, rawreel::panelGrid(extent, layout.count));
            REQUIRE(panels.size() == layout.count);
            // Each panel shows its own plane.
            CHECK(wrongGreys(frame, INSPECT_IMAGE, bytes, extent, panels, params) == 0);
            std::size_t covered = 0;
            for (std::size_t k = 0; k < panels.size(); ++k) {
                CAPTURE(k);
                // One panel of the same size, without layers: its rulers are each panel's.
                const dvv::platform::PixelSize one = panels[k].size();
                ViewParams plain = params;
                plain.inspect = {};
                const std::vector<std::byte> single =
                    renderInspect(env, frame, *renderer, one, plain);
                CHECK(renderer->lastRecorded().panels == 1);
                CHECK_FALSE(renderer->lastRecorded().inspectPipeline);
                CHECK(renderer->lastRecorded().rulerNumbers);
                covered += std::size_t{panels[k].width} * panels[k].height;
                std::size_t wrong = 0;
                for (std::uint32_t y = 0; y < one.height; ++y) {
                    for (std::uint32_t x = 0; x < one.width; ++x) {
                        if (y >= 18 && x >= 22) {
                            continue;  // the picture region
                        }
                        const auto shown =
                            pixelOf(bytes, extent, static_cast<std::uint32_t>(panels[k].x) + x,
                                    static_cast<std::uint32_t>(panels[k].y) + y);
                        wrong += shown == pixelOf(single, one, x, y) ? 0 : 1;
                    }
                }
                CHECK(wrong == 0);
            }
            CHECK(covered == std::size_t{extent.width} * extent.height);
        }
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("inspect: a new frame reuses the layer block") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, formatNamed("yuv420p10le"), INSPECT_IMAGE);
        const auto renderer = inspectRenderer(frame);
        const dvv::platform::PixelSize extent{.width = 300, .height = 100};
        std::uint64_t uploads = 0;
        for (std::uint64_t version = 1; version <= 3; ++version) {
            CAPTURE(version);
            const rawreel::ViewSnapshot snapshot{
                .feed = {.frames = frame.frames.share(), .version = version},
                .params = planesParams()};
            auto presented = renderer->frame({.extent = extent,
                                              .frameNumber = version - 1,
                                              .previousFrameCompleted = version > 1},
                                             snapshot);
            REQUIRE_MESSAGE(presented, (presented ? std::string() : presented.error().describe()));
            REQUIRE(static_cast<bool>(presented->record));  // a new frame records again
            auto done = rawreel::testing::submitAndWait(*frame.context, *env.cpu,
                                                        std::move(presented->record));
            REQUIRE_MESSAGE(done, (done ? std::string() : done.error().describe()));
            const RecordedInfo info = renderer->lastRecorded();
            CHECK(info.inspectPipeline);
            CHECK(info.panels == 3);
            CHECK(info.version == version);
            if (version == 1) {
                uploads = info.layerUploads;
                CHECK(uploads == 3);  // one block per panel
            } else {
                CHECK(info.layerUploads == uploads);
            }
        }
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

namespace {

using rawreel::LAYER_TEST_CODES;
using rawreel::PicturePass;
using rawreel::ViewRole;

struct InspectOutput {
    std::vector<std::uint32_t> codes;  // what LAYER_TEST_CODES wrote, row-major
    std::vector<std::byte> bytes;      // the RGBA8 output of `extent`, row-major
};

// The inspect pipeline driven directly with `block` over one Picture region of `extent` (no
// strips, Fit) for `frame` (`image` big), the value strip `strip` (null: the dummy) uploaded in
// the same submission.
InspectOutput dispatchInspect(GpuTestEnvironment& env, const rawreel::testing::InspectFrame& frame,
                              const PicturePass& pass, dvv::platform::PixelSize image,
                              dvv::platform::PixelSize extent, const LayerBlock& block,
                              const std::shared_ptr<const dvv::gpu::GlyphStrip>& strip) {
    dvv::gpu::ExecutionContext& context = *frame.context;
    ViewParams params;
    params.inspect.on = true;
    const dvv::media::ViewTransform view = dvv::media::resolveView(params.view, extent, image);
    const rawreel::PassRegions regions = rawreel::panelRegions(
        {.x = 0, .y = 0, .width = extent.width, .height = extent.height}, {}, view);
    const rawreel::PictureConstants constants =
        rawreel::pictureConstants(params, view, regions, false);
    const dvv::media::ReadLease& planes = frame.frames.lease(ViewRole::Planes);
    REQUIRE(static_cast<bool>(planes));
    dvv::gpu::RecordFunction upload;
    std::shared_ptr<dvv::gpu::GpuImage> strip_image;
    if (strip != nullptr) {
        auto uploaded = dvv::gpu::uploadGlyphStrip(context, *strip);
        REQUIRE_MESSAGE(uploaded, (uploaded ? std::string() : uploaded.error().describe()));
        strip_image = uploaded->image;
        upload = std::move(uploaded->record);
    }
    auto buffer = dvv::gpu::createBuffer(context, {.size = sizeof(LayerBlock),
                                                   .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                   .memory = dvv::gpu::MemoryUsage::HostUpload,
                                                   .name = "layer block"});
    REQUIRE_MESSAGE(buffer, (buffer ? std::string() : buffer.error().describe()));
    {
        dvv::gpu::MappedWrite write = (*buffer)->mapWrite();
        std::memcpy(write.bytes().data(), &block, sizeof(LayerBlock));
        const dvv::Result<void> flushed = write.flush();
        REQUIRE_MESSAGE(flushed, (flushed ? std::string() : flushed.error().describe()));
    }
    auto codes = dvv::gpu::createImage(
        context, {.width = image.width,
                  .height = image.height,
                  .format = VK_FORMAT_R32_UINT,
                  .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                  .name = "test codes"});
    REQUIRE_MESSAGE(codes, (codes ? std::string() : codes.error().describe()));
    auto output = dvv::gpu::createImage(
        context, {.width = extent.width,
                  .height = extent.height,
                  .format = VK_FORMAT_R8G8B8A8_UNORM,
                  .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                  .name = "view output"});
    REQUIRE_MESSAGE(output, (output ? std::string() : output.error().describe()));
    auto lines = PicturePass::createLineTable(context, extent);
    REQUIRE_MESSAGE(lines, (lines ? std::string() : lines.error().describe()));

    rawreel::InspectBinding binding;
    binding.layers = *buffer;
    binding.testCodes = codes->get();
    binding.valueStrip = strip_image;
    const std::uint32_t count = dvv::media::formatDesc(planes.desc().format).componentCount;
    for (std::size_t i = 0; i < std::min<std::size_t>(count, binding.planes.size()); ++i) {
        binding.planes.at(i) = dvv::media::vulkan::image(planes, i);
    }
    dvv::gpu::GpuImage* const display =
        dvv::media::vulkan::image(frame.frames.lease(ViewRole::Display), 0);
    REQUIRE(display != nullptr);
    PicturePass::Dispatch dispatch = pass.dispatch(true, display, constants, regions.picture,
                                                   regions.pictureDrawn, false, binding);
    REQUIRE(dispatch.groupsX != 0);
    auto done = rawreel::testing::submitAndWait(
        context, *env.cpu,
        [dispatch = std::move(dispatch), upload = std::move(upload), held_codes = *codes,
         held_output = *output, held_lines = *lines](dvv::gpu::CommandRecorder& recorder) mutable {
            static_cast<void>(held_codes);  // held only for its lifetime
            if (upload) {
                upload(recorder);
            }
            dispatch.record(recorder, *held_output, *held_lines);
        });
    REQUIRE_MESSAGE(done, (done ? std::string() : done.error().describe()));
    auto bytes = rawreel::testing::readImage(context, *env.cpu, *codes, 4);
    REQUIRE_MESSAGE(bytes, (bytes ? std::string() : bytes.error().describe()));
    REQUIRE(bytes->size() == std::size_t{image.width} * image.height * 4);
    InspectOutput read{.codes = std::vector<std::uint32_t>(std::size_t{image.width} * image.height),
                       .bytes = {}};
    std::memcpy(read.codes.data(), bytes->data(), bytes->size());
    auto shown = rawreel::testing::readImage(context, *env.cpu, *output, 4);
    REQUIRE_MESSAGE(shown, (shown ? std::string() : shown.error().describe()));
    read.bytes = std::move(*shown);
    return read;
}

// The codes LAYER_TEST_CODES writes for component `k` of `frame` (`image` big) over one Picture
// panel of `extent`.
std::vector<std::uint32_t> testCodes(GpuTestEnvironment& env,
                                     const rawreel::testing::InspectFrame& frame,
                                     const PicturePass& pass, dvv::platform::PixelSize image,
                                     dvv::platform::PixelSize extent, std::uint32_t k) {
    ViewParams params;
    params.inspect.on = true;
    LayerBlock block = rawreel::layerBlock(params, frame.frames.lease(ViewRole::Planes).desc(), k,
                                           PanelKind::Picture);
    block.flags |= LAYER_TEST_CODES;
    block.sourceComponent = k;
    return dispatchInspect(env, frame, pass, image, extent, block, nullptr).codes;
}

}  // namespace

TEST_CASE("inspect: values equal unpack() for every catalogue format") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        std::vector<dvv::media::FormatId> formats(dvv::media::catalogue().begin(),
                                                  dvv::media::catalogue().end());
        formats.push_back(rawreel::testing::testYuv411p());
        for (const dvv::media::FormatId format : formats) {
            const dvv::media::PixelFormatDesc& desc = dvv::media::formatDesc(format);
            CAPTURE(desc.name);
            const std::uint32_t multiple = std::max<std::uint32_t>(desc.widthMultiple, 1U);
            const dvv::platform::PixelSize image{.width = (37 + multiple - 1) / multiple * multiple,
                                                 .height = 23};
            const rawreel::testing::InspectFrame frame =
                rawreel::testing::openInspectFrame(env, format, image);
            auto pass = PicturePass::create(*frame.context);
            REQUIRE_MESSAGE(pass, (pass ? std::string() : pass.error().describe()));
            // No strips, the extent twice the image: Fit shows it at zoom 2.
            const dvv::platform::PixelSize extent{.width = 2 * image.width,
                                                  .height = 2 * image.height};
            for (std::uint32_t k = 0; k < desc.componentCount; ++k) {
                CAPTURE(k);
                const std::vector<std::uint32_t> codes =
                    testCodes(env, frame, *pass, image, extent, k);
                std::size_t wrong = 0;
                for (std::uint32_t y = 0; y < image.height; ++y) {
                    for (std::uint32_t x = 0; x < image.width; ++x) {
                        const std::uint32_t expected =
                            rawreel::testing::codeAt(frame.reference, k, {.x = x, .y = y});
                        wrong += codes.at((std::size_t{y} * image.width) + x) == expected ? 0 : 1;
                    }
                }
                CHECK(wrong == 0);
            }
        }
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

namespace {

// Every cell pixel of the strip a different coverage, 0 and 255 included.
std::shared_ptr<const dvv::gpu::GlyphStrip> mixedStrip() {
    return valueStrip(5, 7, [](std::uint32_t cell, std::uint32_t x, std::uint32_t y) {
        return static_cast<std::uint8_t>(((cell * 61U) + (x * 37U) + (y * 101U)) % 256U);
    });
}

constexpr rawreel::ValueMetrics MIXED_METRICS{.advance = 5, .lineHeight = 9, .padX = 4, .padY = 4};

}  // namespace

TEST_CASE("inspect: drawn digits equal the strip") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const dvv::platform::PixelSize image{.width = 8, .height = 8};
        const auto strip = mixedStrip();
        const ViewParams params = valuesParams(strip, MIXED_METRICS);
        // yuv420p: one line over the chroma label in each block's bottom row; yuv444p: three
        // stacked lines with their letters.
        for (const std::string_view name : {"yuv420p", "yuv444p"}) {
            CAPTURE(name);
            const rawreel::testing::InspectFrame frame =
                rawreel::testing::openInspectFrame(env, formatNamed(name), image);
            const auto renderer = inspectRenderer(frame);
            const dvv::platform::PixelSize extent{.width = 8 * VALUES_ZOOM,
                                                  .height = 8 * VALUES_ZOOM};
            const std::vector<std::byte> off = renderInspect(env, frame, *renderer, extent, {});
            const std::vector<std::byte> on = renderInspect(env, frame, *renderer, extent, params);
            CHECK(renderer->lastRecorded().zoom == double{VALUES_ZOOM});
            CHECK(renderer->lastRecorded().inspectPipeline);
            const LayerBlock block = rawreel::layerBlock(
                params, frame.frames.lease(ViewRole::Planes).desc(), 0, PanelKind::Picture);
            const CellCheck check = checkCells(
                frame, block, *strip, VALUES_ZOOM, off, on, extent, {0, image.width},
                {0, image.height},
                rawreel::cellShapes(params, frame.frames.lease(ViewRole::Planes).desc()));
            CHECK(check.cells > image.width * image.height / 2);
            CHECK(check.inked > 0);
            CHECK(check.wrong == 0);
        }
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("inspect: an edge block draws no cut text") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const dvv::media::FormatId format = formatNamed("yuv420p");
        const dvv::media::PixelFormatDesc& desc = dvv::media::formatDesc(format);
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, format, INSPECT_IMAGE);
        auto pass = PicturePass::create(*frame.context);
        REQUIRE_MESSAGE(pass, (pass ? std::string() : pass.error().describe()));
        const auto strip = mixedStrip();
        const ViewParams params = valuesParams(strip, MIXED_METRICS);
        const dvv::media::FrameDesc& frame_desc = frame.frames.lease(ViewRole::Planes).desc();
        const std::uint32_t last = INSPECT_IMAGE.width - 1;  // the 1-pixel chroma column
        const std::uint32_t bottom = INSPECT_IMAGE.height - 1;
        // `block` at `zoom`, without and with the values; checks the image pixels [x0, w) x
        // [y0, h) against the mirror. Returns the two outputs.
        const auto run = [&](LayerBlock block, std::uint32_t zoom, std::uint32_t x0,
                             std::uint32_t y0) {
            const dvv::platform::PixelSize extent{.width = zoom * INSPECT_IMAGE.width,
                                                  .height = zoom * INSPECT_IMAGE.height};
            LayerBlock plain = block;
            plain.flags &= ~rawreel::LAYER_VALUES;
            const std::vector<std::byte> off =
                dispatchInspect(env, frame, *pass, INSPECT_IMAGE, extent, plain, nullptr).bytes;
            const std::vector<std::byte> on =
                dispatchInspect(env, frame, *pass, INSPECT_IMAGE, extent, block, strip).bytes;
            const CellCheck check =
                checkCells(frame, block, *strip, zoom, off, on, extent, {x0, INSPECT_IMAGE.width},
                           {y0, INSPECT_IMAGE.height}, {});  // a bare dispatch draws no cell level
            CHECK(check.cells > 0);
            CHECK(check.inked > 0);
            CHECK(check.wrong == 0);
            return std::pair{off, on};
        };
        // The P pixels of image pixel (x, y) at `zoom` in rows [r0, r1) of the cell that differ.
        const auto changed =
            [](const std::pair<std::vector<std::byte>, std::vector<std::byte>>& out,
               std::uint32_t zoom, std::uint32_t x, std::uint32_t y, std::uint32_t r0,
               std::uint32_t r1) {
                const dvv::platform::PixelSize extent{.width = zoom * INSPECT_IMAGE.width,
                                                      .height = zoom * INSPECT_IMAGE.height};
                std::size_t count = 0;
                for (std::uint32_t py = (y * zoom) + r0; py < (y * zoom) + r1; ++py) {
                    for (std::uint32_t px = x * zoom; px < (x + 1) * zoom; ++px) {
                        count += pixelOf(out.first, extent, px, py) ==
                                         pixelOf(out.second, extent, px, py)
                                     ? 0
                                     : 1;
                    }
                }
                return count;
            };

        // Structure at its numbers-fit zoom: the 2-pixel blocks hold their label; the last
        // column's 1-pixel block is too narrow, its label rows stay the picture.
        const std::uint32_t fit =
            rawreel::numbersFitZoom(MIXED_METRICS, rawreel::InspectorLayout::Structure, desc);
        CAPTURE(fit);
        const LayerBlock structure = rawreel::layerBlock(params, frame_desc, 0, PanelKind::Picture);
        const auto label_rows = [&](std::uint32_t zoom) {
            return std::array<std::uint32_t, 2>{
                zoom - MIXED_METRICS.padY - MIXED_METRICS.lineHeight, zoom - MIXED_METRICS.padY};
        };
        const auto shown = run(structure, fit, last - 4, bottom - 3);
        const std::array<std::uint32_t, 2> rows = label_rows(fit);
        CHECK(changed(shown, fit, last - 2, 1, rows[0], rows[1]) > 0);  // a 2-pixel block's label
        for (std::uint32_t y = 0; y <= bottom; ++y) {
            if (y % 2 == 0 && y != bottom) {
                continue;  // a block's top row: no label
            }
            CAPTURE(y);
            CHECK(changed(shown, fit, last, y, rows[0], rows[1]) == 0);
        }

        // The U Planes panel at the Planes numbers-fit zoom: every code fits, the edge ones too.
        const LayerBlock planes = rawreel::layerBlock(params, frame_desc, 1, PanelKind::Planes);
        const std::uint32_t planes_fit =
            rawreel::numbersFitZoom(MIXED_METRICS, rawreel::InspectorLayout::Planes, desc);
        CAPTURE(planes_fit);
        const auto fitted = run(planes, planes_fit, last - 4, bottom - 4);
        CHECK(changed(fitted, planes_fit, last, bottom, 0, planes_fit) > 0);
        // At half that zoom a 2-pixel sample holds its code; the 1-pixel edge column and row
        // cannot, and show none.
        const std::uint32_t half = (planes_fit + 1) / 2;
        const auto halved = run(planes, half, last - 4, bottom - 4);
        CHECK(changed(halved, half, last - 2, bottom - 2, 0, half) +
                  changed(halved, half, last - 1, bottom - 1, 0, half) >
              0);
        for (std::uint32_t y = 0; y <= bottom; ++y) {
            CHECK(changed(halved, half, last, y, 0, half) == 0);
        }
        for (std::uint32_t x = 0; x <= last; ++x) {
            CHECK(changed(halved, half, x, bottom, 0, half) == 0);
        }
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("inspect: the pin's mark frames the panel's cell and never covers a value or label") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const dvv::media::FormatId format = formatNamed("yuv420p");
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, format, INSPECT_IMAGE);
        auto pass = PicturePass::create(*frame.context);
        REQUIRE_MESSAGE(pass, (pass ? std::string() : pass.error().describe()));
        const auto strip = mixedStrip();
        const dvv::media::FrameDesc& frame_desc = frame.frames.lease(ViewRole::Planes).desc();
        // The bottom-right pixel of the chroma block [32, 34) x [20, 22), as the selftest's probe
        // (33, 25) is of its block: in Structure the block's label runs across the pixel's left
        // edge, where the mark's left band lies.
        const dvv::media::ImagePixel pin{.x = 33, .y = 21};
        // The 1x metrics: a pad of 2 backing pixels, less than the mark's 3 px band.
        constexpr rawreel::ValueMetrics ONE_X{.advance = 5, .lineHeight = 9, .padX = 2, .padY = 2};
        struct MarkCase {
            const char* name;
            rawreel::ValueMetrics metrics;
            std::uint32_t panel;
            PanelKind kind;
            rawreel::InspectorLayout layout;
            std::array<std::uint32_t, 4> cell;  // the cell it frames: x0, y0, x1, y1
            bool crossesText;                   // its ring runs through text
        };
        const rawreel::InspectorLayout structure = rawreel::InspectorLayout::Structure;
        const rawreel::InspectorLayout planes = rawreel::InspectorLayout::Planes;
        for (const MarkCase& mark : {
                 MarkCase{"Structure",
                          MIXED_METRICS,
                          0,
                          PanelKind::Picture,
                          structure,
                          {33, 21, 34, 22},
                          true},
                 MarkCase{"Structure at 1x",
                          ONE_X,
                          0,
                          PanelKind::Picture,
                          structure,
                          {33, 21, 34, 22},
                          true},
                 MarkCase{"Y plane",
                          MIXED_METRICS,
                          0,
                          PanelKind::Planes,
                          planes,
                          {33, 21, 34, 22},
                          false},
                 MarkCase{"U plane",
                          MIXED_METRICS,
                          1,
                          PanelKind::Planes,
                          planes,
                          {32, 20, 34, 22},
                          false},
                 MarkCase{
                     "V plane at 1x", ONE_X, 2, PanelKind::Planes, planes, {32, 20, 34, 22}, false},
             }) {
            CAPTURE(mark.name);
            const std::uint32_t zoom =
                rawreel::numbersFitZoom(mark.metrics, mark.layout, dvv::media::formatDesc(format));
            CAPTURE(zoom);
            const dvv::platform::PixelSize extent{.width = zoom * INSPECT_IMAGE.width,
                                                  .height = zoom * INSPECT_IMAGE.height};
            ViewParams params = valuesParams(strip, mark.metrics);
            const LayerBlock values =
                rawreel::layerBlock(params, frame_desc, mark.panel, mark.kind);
            LayerBlock plain = values;
            plain.flags &= ~rawreel::LAYER_VALUES;
            params.markers.pin = pin;
            const LayerBlock marked =
                rawreel::layerBlock(params, frame_desc, mark.panel, mark.kind);
            CHECK(marked.pinRect == mark.cell);
            const std::vector<std::byte> off =
                dispatchInspect(env, frame, *pass, INSPECT_IMAGE, extent, plain, nullptr).bytes;
            const std::vector<std::byte> on =
                dispatchInspect(env, frame, *pass, INSPECT_IMAGE, extent, values, strip).bytes;
            const std::vector<std::byte> shown =
                dispatchInspect(env, frame, *pass, INSPECT_IMAGE, extent, marked, strip).bytes;
            // The ring the mark may draw: 3 px (the 2 px box and the 1 px rim) outside the cell.
            const auto at = [zoom](std::uint32_t pixel) { return std::int64_t{pixel} * zoom; };
            const std::int64_t x0 = at(mark.cell[0]);
            const std::int64_t y0 = at(mark.cell[1]);
            const std::int64_t x1 = at(mark.cell[2]);
            const std::int64_t y1 = at(mark.cell[3]);
            std::size_t text = 0;       // the pixels the values and labels draw
            std::size_t marks = 0;      // the pixels the mark changes
            std::size_t on_text = 0;    // ... of them on text
            std::size_t outside = 0;    // ... of them outside the ring
            std::size_t crossed = 0;    // the ring's text pixels
            std::size_t ring_free = 0;  // the ring's other pixels
            for (std::uint32_t py = 0; py < extent.height; ++py) {
                for (std::uint32_t px = 0; px < extent.width; ++px) {
                    const bool in_box = px >= x0 - 3 && px < x1 + 3 && py >= y0 - 3 && py < y1 + 3;
                    const bool in_cell = px >= x0 && px < x1 && py >= y0 && py < y1;
                    const bool in_ring = in_box && !in_cell;
                    const bool is_text =
                        pixelOf(off, extent, px, py) != pixelOf(on, extent, px, py);
                    const bool is_mark =
                        pixelOf(on, extent, px, py) != pixelOf(shown, extent, px, py);
                    text += is_text ? 1 : 0;
                    marks += is_mark ? 1 : 0;
                    on_text += is_mark && is_text ? 1 : 0;
                    outside += is_mark && !in_ring ? 1 : 0;
                    crossed += in_ring && is_text ? 1 : 0;
                    ring_free += in_ring && !is_text ? 1 : 0;
                }
            }
            CAPTURE(crossed);
            CHECK(text > 0);
            CHECK(on_text == 0);
            CHECK(outside == 0);
            // The mark draws the ring but where text is (a rim pixel may match the picture).
            CHECK(marks * 2 >= ring_free);
            if (mark.crossesText) {
                CHECK(crossed > 0);
            }
        }
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("inspect: the value strip uploads once per view") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, formatNamed("yuv420p"), INSPECT_IMAGE);
        const auto renderer = inspectRenderer(frame);
        const dvv::platform::PixelSize extent{.width = 300, .height = 200};
        ViewParams params = valuesParams(mixedStrip(), MIXED_METRICS);
        std::uint64_t version = 0;
        // frame() of a new feed version (the picture pass records again), submitted unless
        // `dropped`; `completed`: whether the previous frame ran.
        const auto next = [&](bool completed, bool dropped) {
            ++version;
            const rawreel::ViewSnapshot snapshot{
                .feed = {.frames = frame.frames.share(), .version = version}, .params = params};
            auto presented = renderer->frame(
                {.extent = extent, .frameNumber = version, .previousFrameCompleted = completed},
                snapshot);
            REQUIRE_MESSAGE(presented, (presented ? std::string() : presented.error().describe()));
            REQUIRE(static_cast<bool>(presented->record));
            if (!dropped) {
                auto done = rawreel::testing::submitAndWait(*frame.context, *env.cpu,
                                                            std::move(presented->record));
                REQUIRE_MESSAGE(done, (done ? std::string() : done.error().describe()));
            }
            return renderer->lastRecorded();
        };
        CHECK(next(false, false).valueStripUploads == 1);
        CHECK(next(true, false).valueStripUploads == 1);   // the strip stays
        CHECK(next(true, true).valueStripUploads == 1);    // recorded, never run
        CHECK(next(false, false).valueStripUploads == 2);  // the dropped frame: again
        params.inspect.numbers = false;
        CHECK(next(true, false).valueStripUploads == 2);
        params.inspect.numbers = true;
        CHECK(next(true, false).valueStripUploads == 3);  // the numbers back on
        // render() uploads on every call.
        static_cast<void>(renderInspect(env, frame, *renderer, extent, params));
        CHECK(renderer->lastRecorded().valueStripUploads == 4);
        CHECK(renderer->lastRecorded().valueStripFailures == 0);
        // A strip that does not upload: tried, and warned, once.
        params.inspect.glyphs = std::make_shared<const dvv::gpu::GlyphStrip>();
        for (int i = 0; i < 3; ++i) {
            CAPTURE(i);
            const RecordedInfo info = next(true, false);
            CHECK(info.valueStripUploads == 4);
            CHECK(info.valueStripFailures == 1);
        }
        static_cast<void>(renderInspect(env, frame, *renderer, extent, params));
        CHECK(renderer->lastRecorded().valueStripFailures == 1);
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("layer block: the value metrics and the values flag") {
    const dvv::media::FrameDesc desc{.format = formatNamed("yuv420p"), .width = 8, .height = 8};
    const rawreel::ValueMetrics metrics{.advance = 3, .lineHeight = 5, .padX = 7, .padY = 9};
    ViewParams params = valuesParams(mixedStrip(), metrics);
    LayerBlock block = rawreel::layerBlock(params, desc, 0, PanelKind::Picture);
    CHECK(block.metrics == std::array<std::uint32_t, 4>{3, 5, 7, 9});
    CHECK((block.flags & rawreel::LAYER_VALUES) != 0);
    for (int off = 0; off < 3; ++off) {
        CAPTURE(off);
        ViewParams without = params;
        if (off == 0) {
            without.inspect.on = false;
        } else if (off == 1) {
            without.inspect.numbers = false;
        } else {
            without.inspect.glyphs = nullptr;
        }
        block = rawreel::layerBlock(without, desc, 0, PanelKind::Picture);
        CHECK((block.flags & rawreel::LAYER_VALUES) == 0);
        CHECK(block.metrics == std::array<std::uint32_t, 4>{3, 5, 7, 9});
    }
}

TEST_CASE("inspect: matrix-Rgb letters read G B R") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const dvv::media::FormatId format = formatNamed("yuv444p");
        const dvv::media::PixelFormatDesc& desc = dvv::media::formatDesc(format);
        // A YUV layout with matrix Rgb has no chroma: its planes read G, B and R.
        const std::array<char, 4> letters = rawreel::componentLetters(desc, false);
        std::array<std::uint32_t, 4> letter_cells{};
        for (std::size_t k = 0; k < letters.size(); ++k) {
            letter_cells.at(k) = letters.at(k) == '\0' ? 0 : rawreel::letterCell(letters.at(k));
        }
        // Cell c inks its row c only: a glyph's inked row names its cell.
        const auto strip =
            valueStrip(3, 17, [](std::uint32_t cell, std::uint32_t /*x*/, std::uint32_t y) {
                return static_cast<std::uint8_t>(cell == y ? 255 : 0);
            });
        const rawreel::ValueMetrics metrics{.advance = 3, .lineHeight = 17, .padX = 2, .padY = 2};
        ViewParams params = valuesParams(strip, metrics);
        params.inspect.letterCells = letter_cells;

        const dvv::media::FrameDesc frame_desc{.format = format, .width = 8, .height = 8};
        const LayerBlock block = rawreel::layerBlock(params, frame_desc, 0, PanelKind::Picture);
        CHECK((block.flags & rawreel::LAYER_VALUES) != 0);
        const std::array<std::uint32_t, 3> expected{14, 15, 13};  // G B R in VALUE_GLYPHS
        for (std::size_t k = 0; k < expected.size(); ++k) {
            CAPTURE(k);
            CHECK((block.component.at(k) >> rawreel::LAYER_LETTER_SHIFT) == expected.at(k));
        }

        const dvv::platform::PixelSize image{.width = 8, .height = 8};
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, format, image);
        const auto renderer = inspectRenderer(frame);
        const dvv::platform::PixelSize extent{.width = 8 * VALUES_ZOOM, .height = 8 * VALUES_ZOOM};
        const std::vector<std::byte> off = renderInspect(env, frame, *renderer, extent, {});
        const std::vector<std::byte> on = renderInspect(env, frame, *renderer, extent, params);
        CHECK(renderer->lastRecorded().zoom == double{VALUES_ZOOM});
        for (const std::uint32_t x : {0U, 5U}) {
            for (const std::uint32_t y : {0U, 7U}) {
                CAPTURE(x);
                CAPTURE(y);
                const rawreel::testing::TextBox box = cellBox(x, y, metrics);
                for (std::size_t line = 0; line < expected.size(); ++line) {
                    CAPTURE(line);
                    const std::uint32_t code =
                        rawreel::testing::codeAt(frame.reference, line, {.x = x, .y = y});
                    // "<letter> <code>"
                    const std::size_t length = 2 + rawreel::testing::codeCells(code).size();
                    const rawreel::testing::TextOrigin origin = rawreel::testing::lineOrigin(
                        *strip, metrics, box, expected.size(), line, length);
                    std::vector<std::uint32_t> inked;
                    for (std::uint32_t row = 0; row < strip->height; ++row) {
                        const auto px = static_cast<std::uint32_t>(origin.left);
                        const auto py = static_cast<std::uint32_t>(origin.top) + row;
                        const auto shown = pixelOf(on, extent, px, py);
                        const auto under = pixelOf(off, extent, px, py);
                        int most = 0;
                        for (std::size_t c = 0; c < 3; ++c) {
                            most = std::max(most, std::abs(int{shown.at(c)} - int{under.at(c)}));
                        }
                        if (most > 64) {
                            inked.push_back(row);
                        }
                    }
                    CHECK(inked == std::vector<std::uint32_t>{expected.at(line)});
                }
            }
        }
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("inspect: cell lines and chroma outlines follow the line rule") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const dvv::platform::PixelSize image{.width = 8, .height = 8};
        const dvv::platform::PixelSize extent{.width = 8 * CELL_ZOOM, .height = 8 * CELL_ZOOM};
        const ViewParams params = cellLineParams();
        for (const std::string_view name : {"yuyv422", "yuv420p"}) {
            CAPTURE(name);
            const rawreel::testing::InspectFrame frame =
                rawreel::testing::openInspectFrame(env, formatNamed(name), image);
            const auto renderer = inspectRenderer(frame);
            const std::vector<std::byte> off = renderInspect(env, frame, *renderer, extent, {});
            const std::vector<std::byte> on = renderInspect(env, frame, *renderer, extent, params);
            CHECK(renderer->lastRecorded().zoom == double{CELL_ZOOM});
            CHECK(renderer->lastRecorded().inspectPipeline);
            const dvv::media::FrameDesc desc = frame.frames.lease(ViewRole::Planes).desc();
            const std::array<std::uint32_t, 3> shapes = rawreel::cellShapes(params, desc);
            const dvv::media::ViewTransform view =
                dvv::media::resolveView(params.view, extent, image);
            const dvv::media::AxisView ax = dvv::media::axisX(view);
            const dvv::media::AxisView ay = dvv::media::axisY(view);
            // The shapes follow the rule: an outline only on a subsampled axis.
            const auto drawn = [](std::uint32_t shape) {
                return (shape & rawreel::LEVEL_DRAWN) != 0;
            };
            const auto thickness = [](std::uint32_t shape) {
                return (shape >> rawreel::LEVEL_THICKNESS_SHIFT) & rawreel::LEVEL_THICKNESS_MAX;
            };
            const auto step = [](std::uint32_t shape) { return shape & rawreel::LEVEL_STEP_MASK; };
            CHECK(drawn(shapes[0]));  // both subsample x by 2
            CHECK(step(shapes[0]) == 1);
            CHECK(thickness(shapes[0]) == 2);
            CHECK(drawn(shapes[1]) == (name == "yuv420p"));
            if (name == "yuv420p") {
                CHECK(step(shapes[1]) == 1);
                CHECK(thickness(shapes[1]) == 2);
            }
            CHECK(drawn(shapes[2]));
            CHECK(step(shapes[2]) == 0);
            CHECK(thickness(shapes[2]) == 1);
            // No subsampling, no outline; 4:1:1 outlines every fourth column.
            const std::array<std::uint32_t, 3> full = rawreel::cellShapes(
                params, {.format = formatNamed("yuv444p"), .width = 8, .height = 8});
            CHECK_FALSE(drawn(full[0]));
            CHECK_FALSE(drawn(full[1]));
            CHECK(drawn(full[2]));
            CHECK(step(full[2]) == 0);
            const std::array<std::uint32_t, 3> quarter = rawreel::cellShapes(
                params, {.format = rawreel::testing::testYuv411p(), .width = 8, .height = 8});
            CHECK(drawn(quarter[0]));
            CHECK(step(quarter[0]) == 2);
            CHECK_FALSE(drawn(quarter[1]));
            CHECK(drawn(quarter[2]));
            const std::array<std::uint32_t, 4> colours{rawreel::LAYER_CELL_LINE_COLOUR,
                                                       rawreel::LAYER_CHROMA_COLOUR, 0, 0};
            std::size_t cell_pixels = 0;
            std::size_t chroma_pixels = 0;
            std::size_t wrong = 0;
            for (std::uint32_t y = 0; y < extent.height; ++y) {
                for (std::uint32_t x = 0; x < extent.width; ++x) {
                    const std::optional<dvv::media::LineHit> hit = cellHit(
                        shapes, ax, ay, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
                    const auto before = pixelOf(off, extent, x, y);
                    const auto shown = pixelOf(on, extent, x, y);
                    if (!hit) {
                        wrong += shown == before ? 0 : 1;
                        continue;
                    }
                    (hit->level == 0 ? chroma_pixels : cell_pixels) += 1;
                    const std::array<float, 3> want = overCellLine(encodedOf(before), hit, colours);
                    for (std::size_t c = 0; c < 3; ++c) {
                        const double exact = static_cast<double>(want.at(c)) * 255.0;
                        const double bound = lineSteps(
                            static_cast<double>(colours.at(hit->level == 0 ? 1 : 0) >> 24U) /
                            255.0);
                        wrong += std::abs(static_cast<double>(shown.at(c)) - exact) < bound ? 0 : 1;
                    }
                }
            }
            CHECK(cell_pixels > 0);
            CHECK(chroma_pixels > 0);
            CHECK(wrong == 0);
            // The grid draws over the cell lines: an opaque grid level, no halo, every 4 image
            // pixels. Its pixels equal the grid-only render, also where a cell line lies under.
            rawreel::OverlayParams grid;
            grid.levels[0] = {.stepLog2 = 2,
                              .minimumSpacing = 1,
                              .thickness = 1,
                              .colour = 0xFF00FF00U,
                              .shown = true};
            ViewParams grid_only;
            grid_only.view = params.view;
            grid_only.overlay = grid;
            ViewParams lined = params;
            lined.overlay = grid;
            const std::vector<std::byte> grid_shown =
                renderInspect(env, frame, *renderer, extent, grid_only);
            const std::vector<std::byte> both = renderInspect(env, frame, *renderer, extent, lined);
            std::size_t grid_pixels = 0;
            std::size_t over_lines = 0;
            std::size_t grid_wrong = 0;
            for (std::uint32_t y = 0; y < extent.height; ++y) {
                for (std::uint32_t x = 0; x < extent.width; ++x) {
                    const auto shown = pixelOf(grid_shown, extent, x, y);
                    if (shown == pixelOf(off, extent, x, y)) {
                        continue;
                    }
                    ++grid_pixels;
                    over_lines += cellHit(shapes, ax, ay, static_cast<std::int32_t>(x),
                                          static_cast<std::int32_t>(y))
                                      ? 1
                                      : 0;
                    grid_wrong += pixelOf(both, extent, x, y) == shown ? 0 : 1;
                }
            }
            CHECK(grid_pixels > 0);
            CHECK(over_lines > 0);
            CHECK(grid_wrong == 0);
        }
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("inspect: no cell lines below numbers fit") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const dvv::platform::PixelSize image{.width = 8, .height = 8};
        const dvv::platform::PixelSize extent{.width = 8 * CELL_ZOOM, .height = 8 * CELL_ZOOM};
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, formatNamed("yuv420p"), image);
        const auto renderer = inspectRenderer(frame);
        ViewParams params = cellLineParams();
        params.inspect.numbers = false;  // the numbers do not fit: the lines go with them
        const std::vector<std::byte> off = renderInspect(env, frame, *renderer, extent, {});
        const std::vector<std::byte> hidden = renderInspect(env, frame, *renderer, extent, params);
        CHECK(hidden == off);
        CHECK(renderer->lastRecorded().linePasses == 0);
        params.inspect.numbers = true;
        const std::vector<std::byte> shown = renderInspect(env, frame, *renderer, extent, params);
        CHECK(shown != off);
        CHECK(renderer->lastRecorded().linePasses == 1);
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}

TEST_CASE("inspect: a new frame keeps the cell line table") {
    GpuTestEnvironment env = rawreel::testing::makeGpuTestEnvironment();
    {
        const dvv::platform::PixelSize image{.width = 8, .height = 8};
        const dvv::platform::PixelSize extent{.width = 8 * CELL_ZOOM, .height = 8 * CELL_ZOOM};
        const rawreel::testing::InspectFrame frame =
            rawreel::testing::openInspectFrame(env, formatNamed("yuv420p"), image);
        const auto renderer = inspectRenderer(frame);
        const ViewParams params = cellLineParams();
        for (std::uint64_t version = 1; version <= 3; ++version) {
            CAPTURE(version);
            const rawreel::ViewSnapshot snapshot{
                .feed = {.frames = frame.frames.share(), .version = version}, .params = params};
            auto presented = renderer->frame({.extent = extent,
                                              .frameNumber = version - 1,
                                              .previousFrameCompleted = version > 1},
                                             snapshot);
            REQUIRE_MESSAGE(presented, (presented ? std::string() : presented.error().describe()));
            REQUIRE(static_cast<bool>(presented->record));
            auto done = rawreel::testing::submitAndWait(*frame.context, *env.cpu,
                                                        std::move(presented->record));
            REQUIRE_MESSAGE(done, (done ? std::string() : done.error().describe()));
            CHECK(renderer->lastRecorded().pictureRecords == version);
            CHECK(renderer->lastRecorded().linePasses == 1);  // the table built once, no grid
        }
    }
    CHECK(env.gpuInstance->validationMessageCount() == 0);
}
