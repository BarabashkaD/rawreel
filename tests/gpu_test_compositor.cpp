// The viewport compositor on a real device (design M3 v2 §4.4, §8.3): composite.slang matches the
// CPU reference within 1 on store-made display images, frame() records only on change, other
// epochs draw the background, an sRGB swapchain skips the encode, and lastRecorded() is readable
// while frame() records.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "composite_reference.h"
#include "core/layout.h"
#include "core/overlay.h"
#include "core/ruler_labels.h"
#include "core/session_types.h"
#include "dvv/base/environment.h"
#include "dvv/base/error.h"
#include "dvv/base/log.h"
#include "dvv/base/task.h"
#include "dvv/base/uri.h"
#include "dvv/gpu/command_recorder.h"
#include "dvv/gpu/context_vulkan.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/gpu/gpu_instance.h"
#include "dvv/gpu/presenter_vulkan.h"
#include "dvv/media/display.h"
#include "dvv/media/display_vulkan.h"
#include "dvv/media/frame.h"
#include "dvv/media/frame_store.h"
#include "dvv/media/frame_vulkan.h"
#include "dvv/media/source_factory.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "dvv/platform/pixel_size.h"
#include "gpu_support.h"
#include "render/composite_constants.h"
#include "render/display_state.h"
#include "render/pass_regions.h"
#include "render/ruler_constants.h"
#include "render/viewport_renderer.h"
#include "ruler_reference.h"

using dvv::media::resolveView;
using dvv::media::ViewMode;
using dvv::media::ViewTransform;
using dvv::platform::PixelSize;
using rawreel::CompositeConstants;
using rawreel::DisplayParams;
using rawreel::DisplaySnapshot;
using rawreel::FrameSet;
using rawreel::HostDisplayImage;
using rawreel::RecordedInfo;
using rawreel::ViewportRenderer;

namespace {

// The regions of both passes for `view`, resolved against P's size, as the renderer splits them.
rawreel::PassRegions regionsOf(PixelSize extent, const rawreel::OverlayParams& overlay,
                               const ViewTransform& view) {
    return rawreel::passRegions(extent, overlay.strips, view);
}

using Rgba8 = std::array<std::uint8_t, 4>;

constexpr std::string_view G = "synthetic:gradient?w=37&h=23&fmt=yuv420p&frames=1";
constexpr std::string_view N = "synthetic:noise?w=37&h=23&fmt=gbrp&frames=1";
constexpr PixelSize IMAGE{37, 23};
// An image smaller than P at zoom 1, so the rulers hug it.
constexpr std::string_view HUG_IMAGE_URI = "synthetic:gradient?w=100&h=50&fmt=yuv420p&frames=1";
constexpr PixelSize HUG_IMAGE{100, 50};
const DisplayParams DARK{.overlay = {.dark = true}};
const DisplayParams LIGHT{.overlay = {.dark = false}};
constexpr Rgba8 DARK_BG{31, 31, 31, 255};
constexpr Rgba8 LIGHT_BG{217, 217, 217, 255};

// A display view made by the GPU FrameStore through ToDisplay, and its host copy.
struct DisplayFrame {
    dvv::media::ReadLease lease;
    std::vector<std::uint16_t> host;
    PixelSize size{};
    [[nodiscard]] HostDisplayImage image() const {
        return HostDisplayImage{.width = size.width, .height = size.height, .rgba16f = host};
    }
};

FrameSet frameSet(const DisplayFrame& frame, std::int64_t index, std::uint64_t epoch) {
    return FrameSet{.index = index, .count = 1, .views = {frame.lease.share()}, .epoch = epoch};
}

dvv::gpu::PresentFrameParams frameParams(PixelSize extent, dvv::gpu::PresentEncoding encoding,
                                         std::uint64_t number, bool completed) {
    return {.extent = extent,
            .frameNumber = number,
            .previousFrameCompleted = completed,
            .encoding = encoding};
}

Rgba8 pixelAt(const std::vector<std::byte>& bytes, PixelSize extent, std::uint32_t x,
              std::uint32_t y) {
    const std::size_t base = ((std::size_t{y} * extent.width) + x) * 4;
    return {std::to_integer<std::uint8_t>(bytes[base]),
            std::to_integer<std::uint8_t>(bytes[base + 1]),
            std::to_integer<std::uint8_t>(bytes[base + 2]),
            std::to_integer<std::uint8_t>(bytes[base + 3])};
}

// Every channel within 1: the GPU's float maths (a divide or pow that is not correctly rounded, a
// float-to-UNORM conversion on the other side of a .5 tie) may move a channel by one step.
bool withinOne(const Rgba8& a, const Rgba8& b) {
    for (std::size_t c = 0; c < 4; ++c) {
        if (std::abs(static_cast<int>(a[c]) - static_cast<int>(b[c])) > 1) {
            return false;
        }
    }
    return true;
}

// Pixels with any channel more than 1 away from compositePixel(); the first one is reported.
std::size_t mismatches(const std::vector<std::byte>& bytes, const CompositeConstants& constants,
                       const std::optional<HostDisplayImage>& image) {
    const PixelSize extent{constants.viewportW, constants.viewportH};
    REQUIRE(bytes.size() == std::size_t{extent.width} * extent.height * 4);
    std::size_t count = 0;
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            const Rgba8 gpu = pixelAt(bytes, extent, x, y);
            const Rgba8 cpu = rawreel::compositePixel(constants, x, y, image);
            const bool wrong = !withinOne(gpu, cpu);
            if (wrong && count == 0) {
                MESSAGE("first mismatch at ("
                        << x << ", " << y << "): zoom " << constants.zoom << " offset ("
                        << constants.offsetX << ", " << constants.offsetY << ") flags "
                        << constants.flags << "; gpu " << int{gpu[0]} << ' ' << int{gpu[1]} << ' '
                        << int{gpu[2]} << ' ' << int{gpu[3]} << ", reference " << int{cpu[0]} << ' '
                        << int{cpu[1]} << ' ' << int{cpu[2]} << ' ' << int{cpu[3]});
            }
            count += wrong ? 1 : 0;
        }
    }
    return count;
}

// Pixels of `region` in `bytes` (an output of `extent`) whose every channel is within 1 of
// compositePixel() at the same pixel of P (`exact`), and those that match no pixel one step away
// on one axis either (`missed`; the rule at a zoom that is not a power of two).
struct RegionMatch {
    std::size_t exact = 0;
    std::size_t missed = 0;
};
RegionMatch regionMatch(const std::vector<std::byte>& bytes, PixelSize extent,
                        const CompositeConstants& constants,
                        const std::optional<HostDisplayImage>& image) {
    REQUIRE(bytes.size() == std::size_t{extent.width} * extent.height * 4);
    RegionMatch match;
    // The pass draws P from (drawX, drawY): thread (x, y) writes P pixel (x + drawX, y + drawY).
    const std::int64_t width = constants.viewportW - constants.drawX;
    const std::int64_t height = constants.viewportH - constants.drawY;
    for (std::int64_t y = 0; y < height; ++y) {
        for (std::int64_t x = 0; x < width; ++x) {
            const Rgba8 gpu = pixelAt(
                bytes, extent, constants.regionX + constants.drawX + static_cast<std::uint32_t>(x),
                constants.regionY + constants.drawY + static_cast<std::uint32_t>(y));
            const auto cpu = [&](std::int64_t cx, std::int64_t cy) {
                return rawreel::compositePixel(constants, static_cast<std::uint32_t>(cx),
                                               static_cast<std::uint32_t>(cy), image);
            };
            if (withinOne(gpu, cpu(x, y))) {
                ++match.exact;
                continue;
            }
            bool near = false;
            for (const auto [dx, dy] :
                 {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}}) {
                const std::int64_t nx = x + dx;
                const std::int64_t ny = y + dy;
                near = near || (nx >= 0 && ny >= 0 && nx < width && ny < height &&
                                withinOne(gpu, cpu(nx, ny)));
            }
            match.missed += near ? 0 : 1;
        }
    }
    return match;
}

// A grid in every look the shader draws: major 32 with a halo, minor 8 adaptive, second minor 2.
rawreel::OverlayParams testGrid(float minimum, std::uint32_t major_thickness) {
    rawreel::OverlayParams overlay{.dark = true};
    overlay.levels[0] = {.stepLog2 = 5,
                         .minimumSpacing = minimum,
                         .thickness = major_thickness,
                         .colour = 0xD91A9FFFU,
                         .shown = true,
                         .halo = true};
    overlay.levels[1] = {.stepLog2 = 3,
                         .minimumSpacing = minimum / 4,
                         .thickness = 1,
                         .colour = 0x80FFFFFFU,
                         .shown = true,
                         .adaptive = true};
    overlay.levels[2] = {
        .stepLog2 = 1, .minimumSpacing = 3, .thickness = 1, .colour = 0x40FFD22BU, .shown = true};
    return overlay;
}

// testGrid with rulers of 30 backing pixels (a tick band of 20) along the top and the left;
// the lit tick 8 deeper than the tick band.
rawreel::OverlayParams testRulers(float minimum) {
    rawreel::OverlayParams overlay = testGrid(minimum, 2);
    overlay.strips = {.top = 30, .left = 30};
    overlay.tickBand = 20;
    overlay.tickWidth = 2;
    overlay.markerDepth = 6;
    overlay.litDepth = 28;
    overlay.tickLength = {20, 12, 6};
    overlay.cursorColour = 0xff3b30;
    return overlay;
}

// The band pixels of `bytes` that are not withinOne of rulerPixel; with `neighbour`, a pixel that
// matches the reference of a neighbour on the band's axis (one pixel either way) counts as a match:
// the only position tolerance. The colour tolerance cannot hide a wrong element: the unmarked
// palette colours differ by at least 10 per channel (0xcf / 0xd9, 0x26 / 0x32, 0x32 / 0x3d), and
// the marker differs from each of them by far more, so a missing, extra or moved tick, separator
// or marker shows. Under the marker the background and the separator differ by about one tenth
// of that, but neither depends on the marker, and both are checked unblended along the band.
std::size_t rulerMismatches(const std::vector<std::byte>& bytes, PixelSize extent,
                            const std::array<rawreel::RulerConstants, 2>& bands, bool neighbour) {
    std::size_t wrong = 0;
    for (const rawreel::RulerConstants& band : bands) {
        for (std::uint32_t y = band.regionY; y < band.regionY + band.regionH; ++y) {
            for (std::uint32_t x = band.regionX; x < band.regionX + band.regionW; ++x) {
                const Rgba8 gpu = pixelAt(bytes, extent, x, y);
                bool match = withinOne(gpu, rawreel::rulerPixel(band, nullptr, x, y));
                const bool top = band.axis == 0;
                const std::uint32_t along = top ? x : y;
                const std::uint32_t end =
                    top ? band.regionX + band.regionW : band.regionY + band.regionH;
                if (!match && neighbour && along > (top ? band.regionX : band.regionY)) {
                    match = withinOne(
                        gpu, rawreel::rulerPixel(band, nullptr, top ? x - 1 : x, top ? y : y - 1));
                }
                if (!match && neighbour && along + 1 < end) {
                    match = withinOne(
                        gpu, rawreel::rulerPixel(band, nullptr, top ? x + 1 : x, top ? y : y + 1));
                }
                wrong += match ? 0 : 1;
            }
        }
    }
    return wrong;
}

std::size_t allowance(PixelSize extent) {
    return std::size_t{extent.width} * extent.height * 5 / 1000;
}

std::string describe(PixelSize extent) {
    return std::to_string(extent.width) + "x" + std::to_string(extent.height);
}

// A display-format frame bound as host planes, with no bytes: a display view whose binding is not
// the display image, which the renderer must refuse.
class HostDisplayFrame final : public dvv::media::IFrame {
public:
    explicit HostDisplayFrame(const dvv::media::ViewSpec& view)
        : desc_{.format = view.format, .colour = view.colour, .width = 1, .height = 1},
          view_(view) {}
    [[nodiscard]] const dvv::media::FrameDesc& desc() const noexcept override { return desc_; }
    [[nodiscard]] std::span<const dvv::media::ViewSpec> nativeViews() const noexcept override {
        return {&view_, 1};
    }
    [[nodiscard]] dvv::media::Binding bind(const dvv::media::ViewSpec& /*native*/) const override {
        return dvv::media::HostPlanesB{};
    }
    [[nodiscard]] std::size_t bytes() const noexcept override { return 0; }

private:
    dvv::media::FrameDesc desc_;
    dvv::media::ViewSpec view_;
};

// One GPU instance, pools and component registry; a context on the best device, a GPU FrameStore
// and a SourceFactory. Test-case locals (FrameSets, leases, renderer) are released first; then the
// store, the factory and the context; then no validation message may have been counted.
class CompositorTest {
public:
    CompositorTest() : env_(rawreel::testing::makeGpuTestEnvironment()) {
        // The dereference stays in the checked branch: clang-tidy does not know that FAIL throws.
        if (const auto device = env_.gpuInstance->bestDevice(); device) {
            auto context = env_.gpuInstance->createContext(*device, {});
            REQUIRE_MESSAGE(context, (context ? std::string() : context.error().describe()));
            context_ = std::move(*context);
        } else {
            FAIL("no usable Vulkan device");
        }
        auto store = dvv::media::FrameStore::create(
            dvv::media::FrameStoreOptions{.cpu = env_.cpu, .io = env_.io, .gpu = context_});
        REQUIRE_MESSAGE(store, (store ? std::string() : store.error().describe()));
        store_ = std::move(*store);
        auto factory = dvv::media::SourceFactory::create(*env_.components, env_.io);
        REQUIRE_MESSAGE(factory, (factory ? std::string() : factory.error().describe()));
        factory_ = std::move(*factory);
    }
    CompositorTest(const CompositorTest&) = delete;
    CompositorTest& operator=(const CompositorTest&) = delete;
    CompositorTest(CompositorTest&&) = delete;
    CompositorTest& operator=(CompositorTest&&) = delete;
    ~CompositorTest() {
        store_.reset();
        factory_.reset();
        context_.reset();
        CHECK(env_.gpuInstance->validationMessageCount() == 0);
    }

    [[nodiscard]] std::shared_ptr<dvv::gpu::ExecutionContext> context() const { return context_; }

    [[nodiscard]] std::vector<dvv::media::LeaseInfo> outstandingLeases() const {
        return store_->outstandingLeases();
    }

    // A display-format view in host memory (HostDisplayFrame), adopted as frame `index`.
    [[nodiscard]] dvv::media::ReadLease hostDisplayLease(std::int64_t index) {
        const dvv::media::ViewSpec view{.format = dvv::media::displayFormat(),
                                        .colour = dvv::media::displayColour(),
                                        .layout = dvv::media::LayoutPolicy::Tight,
                                        .domain = store_->hostDomain()};
        auto lease = store_->adopt(dvv::media::FrameKey{.origin = 1, .index = index},
                                   std::make_shared<HostDisplayFrame>(view));
        REQUIRE_MESSAGE(lease, (lease ? std::string() : lease.error().describe()));
        return lease ? std::move(*lease) : dvv::media::ReadLease{};
    }

    // A source for `uri`, attached to the store.
    [[nodiscard]] dvv::media::OriginId attach(std::string_view uri) {
        INFO("uri ", uri);
        auto parsed = dvv::parseUri(uri);
        REQUIRE(parsed);
        const dvv::media::SourceRequest request{.uri = std::move(*parsed), .hint = {}};
        auto opened = dvv::syncWait(factory_->open(request));
        REQUIRE_MESSAGE(opened, (opened ? std::string() : opened.error().describe()));
        auto* source = std::get_if<std::unique_ptr<dvv::media::ISource>>(&*opened);
        REQUIRE(source != nullptr);
        REQUIRE(*source != nullptr);
        auto origin = store_->attach(std::shared_ptr<dvv::media::ISource>(std::move(*source)));
        REQUIRE_MESSAGE(origin, (origin ? std::string() : origin.error().describe()));
        return *origin;
    }

    // Frame 0 of `uri` as a display view of `size`, with its host copy.
    [[nodiscard]] DisplayFrame displayFrame(std::string_view uri, PixelSize size = IMAGE) {
        const dvv::media::OriginId origin = attach(uri);
        const std::optional<dvv::media::Domain> domain = store_->deviceDomain();
        REQUIRE(domain.has_value());
        DisplayFrame frame;
        if (domain) {
            auto lease =
                dvv::syncWait(store_->acquire(dvv::media::FrameKey{.origin = origin, .index = 0},
                                              dvv::media::displayView(*domain), *env_.cpu));
            REQUIRE_MESSAGE(lease, (lease ? std::string() : lease.error().describe()));
            frame.lease = std::move(*lease);
        }
        dvv::gpu::GpuImage* const image = dvv::media::vulkan::image(frame.lease, 0);
        REQUIRE(image != nullptr);
        REQUIRE(dvv::media::vulkan::image(frame.lease, 1) == nullptr);  // one plane
        REQUIRE(dvv::gpu::vulkanFormat(*image) == dvv::media::DISPLAY_IMAGE_FORMAT);
        frame.size = {image->width(), image->height()};
        REQUIRE(frame.size == size);
        // No owner: frame.lease keeps the image alive while it is read.
        const std::shared_ptr<dvv::gpu::GpuImage> plane(std::shared_ptr<void>(), image);
        auto bytes = rawreel::testing::readImage(*context_, *env_.cpu, plane, 8);
        REQUIRE_MESSAGE(bytes, (bytes ? std::string() : bytes.error().describe()));
        frame.host.resize(bytes->size() / sizeof(std::uint16_t));
        std::memcpy(frame.host.data(), bytes->data(), frame.host.size() * sizeof(std::uint16_t));
        return frame;
    }

    // Frame 0 of `uri` as host planes (its original's unpacked planes in host memory): a view
    // that is not a display view.
    [[nodiscard]] dvv::media::ReadLease hostPlanesLease(std::string_view uri) {
        const dvv::media::OriginId origin = attach(uri);
        auto read = dvv::syncWait(store_->acquireOriginal(
            dvv::media::FrameKey{.origin = origin, .index = 0},
            dvv::media::OriginalRequest{.domain = store_->hostDomain()}, *env_.cpu));
        REQUIRE_MESSAGE(read, (read ? std::string() : read.error().describe()));
        dvv::media::ReadLease lease;
        if (read) {
            lease = read->lease().share();
        }
        REQUIRE(lease.planes() != nullptr);
        return lease;
    }

    void submit(dvv::gpu::RecordFunction record) {
        auto done = rawreel::testing::submitAndWait(*context_, *env_.cpu, std::move(record));
        REQUIRE_MESSAGE(done, (done ? std::string() : done.error().describe()));
    }

    [[nodiscard]] std::vector<std::byte> read(const std::shared_ptr<dvv::gpu::GpuImage>& image) {
        auto bytes = rawreel::testing::readImage(*context_, *env_.cpu, image, 4);
        REQUIRE_MESSAGE(bytes, (bytes ? std::string() : bytes.error().describe()));
        return std::move(*bytes);
    }

    // render(), submit, read back the RGBA8 output.
    [[nodiscard]] std::vector<std::byte> renderAndRead(ViewportRenderer& renderer, PixelSize extent,
                                                       const FrameSet& frames,
                                                       const DisplayParams& params) {
        auto frame = renderer.render(extent, frames, params);
        REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
        REQUIRE(static_cast<bool>(frame->record));
        REQUIRE(frame->image != nullptr);
        CHECK(frame->image->width() == extent.width);
        CHECK(frame->image->height() == extent.height);
        CHECK(dvv::gpu::vulkanFormat(*frame->image) == VK_FORMAT_R8G8B8A8_UNORM);
        submit(std::move(frame->record));
        return read(frame->image);
    }

private:
    rawreel::testing::GpuTestEnvironment env_;
    std::shared_ptr<dvv::gpu::ExecutionContext> context_;
    std::shared_ptr<dvv::media::FrameStore> store_;
    std::unique_ptr<dvv::media::SourceFactory> factory_;
};

std::unique_ptr<ViewportRenderer> makeRenderer(
    const std::shared_ptr<dvv::gpu::ExecutionContext>& context, std::uint64_t epoch) {
    auto renderer = ViewportRenderer::create(context, epoch);
    REQUIRE_MESSAGE(renderer, (renderer ? std::string() : renderer.error().describe()));
    return std::move(*renderer);
}

// Collects the Error lines logged while it lives; the other sinks stay.
class ErrorLog {
public:
    ErrorLog() { dvv::Logger::instance().addSink(sink_, dvv::LogLevel::Error); }
    ErrorLog(const ErrorLog&) = delete;
    ErrorLog& operator=(const ErrorLog&) = delete;
    ErrorLog(ErrorLog&&) = delete;
    ErrorLog& operator=(ErrorLog&&) = delete;
    ~ErrorLog() { dvv::Logger::instance().setSinkLevel(*sink_, dvv::LogLevel::Off); }

    [[nodiscard]] std::vector<std::string> lines() const {
        dvv::Logger::instance().flush();
        return sink_->lines();
    }

private:
    std::shared_ptr<dvv::MemorySink> sink_ = std::make_shared<dvv::MemorySink>();
};

}  // namespace

TEST_CASE_FIXTURE(CompositorTest, "compositor: matches the CPU reference") {
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame g = displayFrame(G);
    const DisplayFrame n = displayFrame(N);
    constexpr std::array<PixelSize, 6> VIEWPORTS{PixelSize{37, 23}, PixelSize{211, 97},
                                                 PixelSize{74, 80}, PixelSize{30, 19},
                                                 PixelSize{11, 7},  PixelSize{1, 1}};
    for (const auto& [name, frame] : {std::pair{"G", &g}, std::pair{"N", &n}}) {
        for (const PixelSize viewport : VIEWPORTS) {
            INFO("image ", name, ", viewport ", describe(viewport), ", dark background");
            const std::vector<std::byte> bytes =
                renderAndRead(*renderer, viewport, frameSet(*frame, 0, 1), DARK);
            const std::size_t wrong = mismatches(
                bytes,
                rawreel::compositeConstants(DARK, ViewTransform::fit(viewport, IMAGE), {}, false),
                frame->image());
            CHECK(wrong <= allowance(viewport));
            if (viewport == IMAGE) {
                CHECK(wrong == 0);
            }
        }
    }
    for (const PixelSize viewport : {PixelSize{211, 97}, PixelSize{11, 7}}) {
        INFO("image N, viewport ", describe(viewport), ", light background");
        const std::vector<std::byte> bytes =
            renderAndRead(*renderer, viewport, frameSet(n, 0, 1), LIGHT);
        CHECK(mismatches(bytes,
                         rawreel::compositeConstants(LIGHT, ViewTransform::fit(viewport, IMAGE), {},
                                                     false),
                         n.image()) <= allowance(viewport));
    }
    for (const bool dark : {true, false}) {
        INFO("no frame, viewport 211x97, ", dark ? "dark" : "light", " background");
        const PixelSize viewport{211, 97};
        const std::vector<std::byte> bytes = renderAndRead(
            *renderer, viewport, FrameSet{}, DisplayParams{.overlay = {.dark = dark}});
        std::size_t wrong = 0;
        for (std::uint32_t y = 0; y < viewport.height; ++y) {
            for (std::uint32_t x = 0; x < viewport.width; ++x) {
                wrong += pixelAt(bytes, viewport, x, y) == (dark ? DARK_BG : LIGHT_BG) ? 0 : 1;
            }
        }
        CHECK(wrong == 0);
    }
    {
        INFO("exact geometry: image G, viewport 211x97, dark background");
        const PixelSize viewport{211, 97};
        const std::vector<std::byte> bytes =
            renderAndRead(*renderer, viewport, frameSet(g, 0, 1), DARK);
        std::size_t wrong = 0;
        for (std::uint32_t y = 0; y < viewport.height; ++y) {
            for (std::uint32_t x = 0; x < viewport.width; ++x) {
                if (x <= 26 || x >= 183) {
                    wrong += pixelAt(bytes, viewport, x, y) == DARK_BG ? 0 : 1;
                }
            }
        }
        CHECK(wrong == 0);
    }
    {
        INFO("exact geometry: image N, viewport 74x80, dark background");
        const PixelSize viewport{74, 80};
        const std::vector<std::byte> bytes =
            renderAndRead(*renderer, viewport, frameSet(n, 0, 1), DARK);
        std::size_t wrong_background = 0;
        for (std::uint32_t y = 0; y < viewport.height; ++y) {
            for (std::uint32_t x = 0; x < viewport.width; ++x) {
                if (y <= 16 || y >= 63) {
                    wrong_background += pixelAt(bytes, viewport, x, y) == DARK_BG ? 0 : 1;
                }
            }
        }
        CHECK(wrong_background == 0);
        std::size_t wrong_blocks = 0;
        for (std::uint32_t y = 0; y < IMAGE.height; ++y) {
            for (std::uint32_t x = 0; x < IMAGE.width; ++x) {
                const Rgba8 corner = pixelAt(bytes, viewport, 2 * x, 17 + (2 * y));
                const bool same = pixelAt(bytes, viewport, (2 * x) + 1, 17 + (2 * y)) == corner &&
                                  pixelAt(bytes, viewport, 2 * x, 18 + (2 * y)) == corner &&
                                  pixelAt(bytes, viewport, (2 * x) + 1, 18 + (2 * y)) == corner;
                wrong_blocks += same ? 0 : 1;
            }
        }
        CHECK(wrong_blocks == 0);
    }
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: 200 % panned matches the CPU reference") {
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame g = displayFrame(G);
    const PixelSize viewport{30, 19};
    const DisplayParams params{.view = {ViewMode::Zoom, 2, {12, 8}, IMAGE},
                               .overlay = {.dark = true}};
    const std::vector<std::byte> bytes =
        renderAndRead(*renderer, viewport, frameSet(g, 0, 1), params);
    const auto view = resolveView(params.view, viewport, IMAGE);
    CHECK(mismatches(bytes, rawreel::compositeConstants(params, view, {}, false), g.image()) == 0);
    const RecordedInfo info = renderer->lastRecorded();
    CHECK(info.zoom == 2.0);
    CHECK(info.mode == ViewMode::Zoom);
    CHECK(info.origin == view.origin());

    // Through frame(): a setView that changes only the view records again, at the new view.
    rawreel::DisplayState state;
    state.show(frameSet(g, 0, 1));
    state.setView(params.view);
    state.setOverlay(params.overlay);
    auto panned = renderer->frame(
        frameParams(viewport, dvv::gpu::PresentEncoding::Linear, 0, false), state.snapshot());
    REQUIRE_MESSAGE(panned, (panned ? std::string() : panned.error().describe()));
    REQUIRE(static_cast<bool>(panned->record));
    CHECK(renderer->lastRecorded().origin == view.origin());
    submit(std::move(panned->record));
    const DisplayParams moved{.view = {ViewMode::Zoom, 4, {20, 14}, IMAGE},
                              .overlay = {.dark = true}};
    state.setView(moved.view);
    auto next = renderer->frame(frameParams(viewport, dvv::gpu::PresentEncoding::Linear, 1, true),
                                state.snapshot());
    REQUIRE_MESSAGE(next, (next ? std::string() : next.error().describe()));
    REQUIRE(static_cast<bool>(next->record));
    const auto moved_view = resolveView(moved.view, viewport, IMAGE);
    REQUIRE(moved_view.origin() != view.origin());
    const RecordedInfo moved_info = renderer->lastRecorded();
    CHECK(moved_info.zoom == 4.0);
    CHECK(moved_info.mode == ViewMode::Zoom);
    CHECK(moved_info.origin == moved_view.origin());
    CHECK(moved_info.index == 0);
    submit(std::move(next->record));
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: frame() records only when something changed") {
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame g = displayFrame(G);
    const PixelSize wide{211, 97};
    const PixelSize tall{74, 80};
    CHECK(renderer->lastRecorded() == RecordedInfo{});

    const DisplaySnapshot first_snapshot{frameSet(g, 0, 1), DARK, 1};
    auto first = renderer->frame(frameParams(wide, dvv::gpu::PresentEncoding::Linear, 0, false),
                                 first_snapshot);
    REQUIRE_MESSAGE(first, (first ? std::string() : first.error().describe()));
    REQUIRE(static_cast<bool>(first->record));
    REQUIRE(first->image != nullptr);
    const RecordedInfo first_info{
        .epoch = 1,
        .index = 0,
        .extent = wide,
        .zoom = ViewTransform::fit(wide, IMAGE).zoom(),
        .origin = ViewTransform::fit(wide, IMAGE).origin(),
        .picture = {0, 0, wide.width, wide.height},
        .pictureDrawn = {0, 0, wide.width, wide.height},
        // Hidden rulers: empty rects around the picture.
        .rulers = dvv::media::rulerRects(wide, {}, ViewTransform::fit(wide, IMAGE)),
        .pictureRecords = 1,
        .pictureFlags = rawreel::COMPOSITE_DARK,
        .version = 1};  // the snapshot version this record drew
    CHECK(renderer->lastRecorded() == first_info);
    submit(std::move(first->record));

    // Nothing changed and the previous frame completed: the same image, no commands.
    auto same = renderer->frame(frameParams(wide, dvv::gpu::PresentEncoding::Linear, 1, true),
                                first_snapshot);
    REQUIRE(same);
    CHECK_FALSE(static_cast<bool>(same->record));
    CHECK(same->image == first->image);
    CHECK(renderer->lastRecorded() == first_info);

    // A new version (the next FrameSet): recorded into the same image.
    auto next = renderer->frame(frameParams(wide, dvv::gpu::PresentEncoding::Linear, 2, true),
                                DisplaySnapshot{frameSet(g, 1, 1), DARK, 2});
    REQUIRE(next);
    REQUIRE(static_cast<bool>(next->record));
    CHECK(next->image == first->image);
    CHECK(renderer->lastRecorded().index == 1);
    CHECK(renderer->lastRecorded().version == 2);
    submit(std::move(next->record));

    // A new extent: a new image.
    const DisplaySnapshot second_snapshot{frameSet(g, 1, 1), DARK, 2};
    auto resized = renderer->frame(frameParams(tall, dvv::gpu::PresentEncoding::Linear, 3, true),
                                   second_snapshot);
    REQUIRE(resized);
    REQUIRE(static_cast<bool>(resized->record));
    REQUIRE(resized->image != nullptr);
    CHECK(resized->image != first->image);
    CHECK(resized->image->width() == tall.width);
    CHECK(resized->image->height() == tall.height);
    CHECK(renderer->lastRecorded().extent == tall);
    CHECK(renderer->lastRecorded().zoom == 2.0);
    submit(std::move(resized->record));

    // The previous frame was dropped: recorded again.
    auto dropped = renderer->frame(frameParams(tall, dvv::gpu::PresentEncoding::Linear, 4, false),
                                   second_snapshot);
    REQUIRE(dropped);
    REQUIRE(static_cast<bool>(dropped->record));
    CHECK(dropped->image == resized->image);
    submit(std::move(dropped->record));

    // The same version with a new view (a zoom): recorded again. The same params once more: the
    // same image, no commands.
    const DisplaySnapshot zoomed{
        frameSet(g, 1, 1),
        DisplayParams{.view = {ViewMode::Zoom, 4, {18, 11}, IMAGE}, .overlay = {.dark = true}}, 2};
    auto zoom =
        renderer->frame(frameParams(tall, dvv::gpu::PresentEncoding::Linear, 5, true), zoomed);
    REQUIRE(zoom);
    REQUIRE(static_cast<bool>(zoom->record));
    CHECK(zoom->image == resized->image);
    CHECK(renderer->lastRecorded().zoom == 4.0);
    CHECK(renderer->lastRecorded().mode == ViewMode::Zoom);
    submit(std::move(zoom->record));
    auto unchanged =
        renderer->frame(frameParams(tall, dvv::gpu::PresentEncoding::Linear, 6, true), zoomed);
    REQUIRE(unchanged);
    CHECK_FALSE(static_cast<bool>(unchanged->record));
    CHECK(unchanged->image == resized->image);

    // The same version and view with only the theme changed: the picture pass records. Only the
    // cursor changed, with the rulers hidden: nothing records.
    const DisplaySnapshot light{
        frameSet(g, 1, 1),
        DisplayParams{.view = {ViewMode::Zoom, 4, {18, 11}, IMAGE}, .overlay = {.dark = false}}, 2};
    auto theme =
        renderer->frame(frameParams(tall, dvv::gpu::PresentEncoding::Linear, 6, true), light);
    REQUIRE(theme);
    REQUIRE(static_cast<bool>(theme->record));
    CHECK(renderer->lastRecorded().pictureRecords == 6);
    submit(std::move(theme->record));
    DisplaySnapshot pointed{frameSet(g, 1, 1), light.params, 2};
    pointed.params.cursor = dvv::media::PointD{10.5, 20.5};
    auto cursor =
        renderer->frame(frameParams(tall, dvv::gpu::PresentEncoding::Linear, 6, true), pointed);
    REQUIRE(cursor);
    CHECK_FALSE(static_cast<bool>(cursor->record));
    CHECK(renderer->lastRecorded().pictureRecords == 6);
    CHECK(renderer->lastRecorded().rulerRecords == 0);

    // No frame: the background only.
    auto cleared = renderer->frame(frameParams(tall, dvv::gpu::PresentEncoding::Linear, 7, true),
                                   DisplaySnapshot{FrameSet{}, DARK, 3});
    REQUIRE(cleared);
    REQUIRE(static_cast<bool>(cleared->record));
    CHECK(renderer->lastRecorded() ==
          RecordedInfo{.epoch = 1,
                       .index = -1,
                       .extent = tall,
                       .zoom = 0.0,
                       .picture = {0, 0, tall.width, tall.height},
                       .pictureDrawn = {0, 0, tall.width, tall.height},
                       .rulers = dvv::media::rulerRects(tall, {}, resolveView({}, tall, {})),
                       .pictureRecords = 7,
                       .pictureFlags = rawreel::COMPOSITE_DARK,
                       .version = 3});
    submit(std::move(cleared->record));
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: a FrameSet of another epoch draws the background") {
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame g = displayFrame(G);
    const PixelSize viewport{211, 97};
    const std::vector<std::byte> bytes =
        renderAndRead(*renderer, viewport, frameSet(g, 0, 2), DARK);
    std::size_t wrong = 0;
    for (std::uint32_t y = 0; y < viewport.height; ++y) {
        for (std::uint32_t x = 0; x < viewport.width; ++x) {
            wrong += pixelAt(bytes, viewport, x, y) == DARK_BG ? 0 : 1;
        }
    }
    CHECK(wrong == 0);
    CHECK(renderer->lastRecorded().index == -1);
    CHECK(renderer->lastRecorded().zoom == 0.0);
}

TEST_CASE_FIXTURE(CompositorTest,
                  "compositor: a non-display view draws the background without an error") {
    const auto renderer = makeRenderer(context(), 1);
    const dvv::media::ReadLease planes = hostPlanesLease(G);
    const ErrorLog errors;
    const PixelSize viewport{211, 97};
    const FrameSet frames{.index = 0, .count = 1, .views = {planes.share()}, .epoch = 1};
    const std::vector<std::byte> bytes = renderAndRead(*renderer, viewport, frames, DARK);
    std::size_t wrong = 0;
    for (std::uint32_t y = 0; y < viewport.height; ++y) {
        for (std::uint32_t x = 0; x < viewport.width; ++x) {
            wrong += pixelAt(bytes, viewport, x, y) == DARK_BG ? 0 : 1;
        }
    }
    CHECK(wrong == 0);
    CHECK(renderer->lastRecorded().index == -1);
    CHECK(errors.lines().empty());
    // The helper sees Error lines, so the empty check above is not vacuous.
    DVV_LOG_ERROR("compositor test: the error log is live");
    CHECK(errors.lines().size() == 1);
}

TEST_CASE_FIXTURE(CompositorTest,
                  "compositor: a display view with another binding logs one Error per epoch") {
    const dvv::media::ReadLease view = hostDisplayLease(0);
    REQUIRE(dvv::media::isDisplay(view.view().format));
    REQUIRE(dvv::media::vulkan::image(view, 0) == nullptr);
    const ErrorLog errors;
    const PixelSize viewport{211, 97};
    const auto first = makeRenderer(context(), 1);
    for (int i = 0; i < 3; ++i) {
        const FrameSet frames{.index = 0, .count = 1, .views = {view.share()}, .epoch = 1};
        const std::vector<std::byte> bytes = renderAndRead(*first, viewport, frames, DARK);
        CHECK(pixelAt(bytes, viewport, 0, 0) == DARK_BG);
        CHECK(first->lastRecorded().index == -1);
    }
    std::vector<std::string> lines = errors.lines();
    REQUIRE(lines.size() == 1);
    CHECK(lines.front().find("unexpected binding (epoch 1)") != std::string::npos);
    // A renderer of the next epoch reports it again, once.
    const auto second = makeRenderer(context(), 2);
    for (int i = 0; i < 2; ++i) {
        const FrameSet frames{.index = 0, .count = 1, .views = {view.share()}, .epoch = 2};
        [[maybe_unused]] const std::vector<std::byte> bytes =
            renderAndRead(*second, viewport, frames, DARK);
    }
    lines = errors.lines();
    REQUIRE(lines.size() == 2);
    CHECK(lines.back().find("unexpected binding (epoch 2)") != std::string::npos);
}

TEST_CASE_FIXTURE(CompositorTest,
                  "the record function keeps the shown image through its lease share") {
    const auto renderer = makeRenderer(context(), 1);
    dvv::gpu::RecordFunction record;
    std::shared_ptr<dvv::gpu::GpuImage> output;
    std::vector<std::uint16_t> host;
    {
        const DisplayFrame g = displayFrame(G);
        host = g.host;
        auto frame = renderer->render(IMAGE, frameSet(g, 0, 1), DARK);
        REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
        if (frame) {
            record = std::move(frame->record);
            output = frame->image;
        }
    }  // the FrameSet and the frame's own lease are gone; the record function holds a share
    const std::vector<dvv::media::LeaseInfo> leases = outstandingLeases();
    REQUIRE(leases.size() == 1);
    CHECK(std::string_view(leases.front().where.file_name()).ends_with("viewport_renderer.cpp"));
    REQUIRE(static_cast<bool>(record));
    REQUIRE(output != nullptr);
    submit(std::move(record));
    const std::vector<std::byte> bytes = read(output);
    const HostDisplayImage shown{.width = IMAGE.width, .height = IMAGE.height, .rgba16f = host};
    CHECK(mismatches(bytes,
                     rawreel::compositeConstants(DARK, ViewTransform::fit(IMAGE, IMAGE), {}, false),
                     shown) == 0);
}

TEST_CASE_FIXTURE(CompositorTest, "an sRGB-encoding presenter skips the compositor's encode") {
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame n = displayFrame(N);
    const PixelSize viewport{211, 97};
    auto frame = renderer->frame(frameParams(viewport, dvv::gpu::PresentEncoding::Srgb, 0, false),
                                 DisplaySnapshot{frameSet(n, 0, 1), DARK, 1});
    REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
    REQUIRE(static_cast<bool>(frame->record));
    submit(std::move(frame->record));
    const std::vector<std::byte> bytes = read(frame->image);
    CHECK(
        mismatches(bytes,
                   rawreel::compositeConstants(DARK, ViewTransform::fit(viewport, IMAGE), {}, true),
                   n.image()) <= allowance(viewport));
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: lastRecorded() may be read while frame() records") {
    // Race case (Debug x10 in Task 7, Windows ASan x10 at the checkpoint, TSan once Linux returns).
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame g = displayFrame(G);
    const PixelSize viewport{211, 97};
    const double zoom = ViewTransform::fit(viewport, IMAGE).zoom();
    const auto origin = ViewTransform::fit(viewport, IMAGE).origin();
    std::atomic<std::uint64_t> reads{0};
    std::atomic<std::uint64_t> violations{0};
    {
        std::jthread reader([&](const std::stop_token& stop) {
            while (!stop.stop_requested()) {
                const RecordedInfo info = renderer->lastRecorded();
                const bool valid =
                    info == RecordedInfo{} || (info.epoch == 1 && info.extent == viewport &&
                                               (info.index == 0 || info.index == 1) &&
                                               info.zoom == zoom && info.origin == origin);
                violations.fetch_add(valid ? 0 : 1, std::memory_order_relaxed);
                reads.fetch_add(1, std::memory_order_relaxed);
            }
        });
        // Record only once the reader runs: in Release the 200 frames can finish before the
        // thread is first scheduled, so the reads would not overlap the recording at all.
        while (reads.load(std::memory_order_relaxed) == 0) {
            std::this_thread::yield();
        }
        for (std::uint64_t i = 0; i < 200; ++i) {
            auto frame = renderer->frame(
                frameParams(viewport, dvv::gpu::PresentEncoding::Linear, i, true),
                DisplaySnapshot{frameSet(g, static_cast<std::int64_t>(i % 2), 1), DARK, i + 1});
            REQUIRE(frame);
            CHECK(static_cast<bool>(frame->record));  // dropped unsubmitted
        }
    }
    CHECK(violations.load() == 0);
    CHECK(reads.load() > 0);
    CHECK(renderer->lastRecorded().index == 1);
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: the grid matches the CPU reference") {
    // frame() on a new renderer, submitted and read back.
    const auto draw = [this](PixelSize extent, const FrameSet& frames, const DisplayParams& params,
                             dvv::gpu::PresentEncoding encoding) {
        const auto renderer = makeRenderer(context(), 1);
        auto frame = renderer->frame(frameParams(extent, encoding, 0, false),
                                     DisplaySnapshot{frames.share(), params, 1});
        REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
        REQUIRE(static_cast<bool>(frame->record));
        submit(std::move(frame->record));
        return read(frame->image);
    };
    for (const auto encoding :
         {dvv::gpu::PresentEncoding::Linear, dvv::gpu::PresentEncoding::Srgb}) {
        const bool skip = encoding == dvv::gpu::PresentEncoding::Srgb;
        CAPTURE(skip);
        SUBCASE("400 %: exact") {
            const DisplayFrame g =
                displayFrame("synthetic:gradient?w=64&h=48&fmt=yuv420p&frames=1", {64, 48});
            const PixelSize p{256, 192};
            const DisplayParams params{.view = {ViewMode::Zoom, 4, {32, 24}, {64, 48}},
                                       .overlay = testGrid(24, 4)};
            const ViewTransform view = resolveView(params.view, p, {64, 48});
            REQUIRE(view.offset() == dvv::media::PixelOffset{0, 0});
            const std::vector<std::byte> bytes = draw(p, frameSet(g, 0, 1), params, encoding);
            const CompositeConstants constants =
                rawreel::compositeConstants(params, view, {}, skip);
            REQUIRE((constants.levelShape[2] & rawreel::LEVEL_DRAWN) != 0);
            const RegionMatch match = regionMatch(bytes, p, constants, g.image());
            MESSAGE("400 %: " << (std::size_t{p.width} * p.height) - match.exact
                              << " pixels off the reference at the same pixel");
            CHECK(match.exact == std::size_t{p.width} * p.height);
        }
        SUBCASE("Fit 47.3 %: within one pixel") {
            const DisplayFrame g =
                displayFrame("synthetic:gradient?w=1920&h=1080&fmt=yuv420p&frames=1", {1920, 1080});
            const PixelSize p{908, 511};
            const DisplayParams params{.overlay = testGrid(12, 2)};
            const ViewTransform view = resolveView(params.view, p, {1920, 1080});
            REQUIRE(std::abs(view.zoom() - 0.473) < 0.001);
            const std::vector<std::byte> bytes = draw(p, frameSet(g, 0, 1), params, encoding);
            const CompositeConstants constants =
                rawreel::compositeConstants(params, view, {}, skip);
            REQUIRE((constants.levelShape[1] & rawreel::LEVEL_DRAWN) != 0);
            const RegionMatch match = regionMatch(bytes, p, constants, g.image());
            MESSAGE("Fit 47.3 %: " << (std::size_t{p.width} * p.height) - match.exact
                                   << " pixels off the reference at the same pixel, "
                                   << match.missed << " off every neighbour too");
            CHECK(match.missed == 0);
        }
    }
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: P below the strips") {
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame g = displayFrame(G);
    const PixelSize extent{211, 97};
    DisplayParams params{.overlay = testGrid(4, 2)};
    auto whole = renderer->frame(frameParams(extent, dvv::gpu::PresentEncoding::Linear, 0, false),
                                 DisplaySnapshot{frameSet(g, 0, 1), params, 1});
    REQUIRE_MESSAGE(whole, (whole ? std::string() : whole.error().describe()));
    REQUIRE(static_cast<bool>(whole->record));
    CHECK(renderer->lastRecorded().picture == dvv::media::PixelRect{0, 0, 211, 97});
    submit(std::move(whole->record));
    const std::vector<std::byte> first = read(whole->image);
    const CompositeConstants first_constants = rawreel::compositeConstants(
        params, resolveView(params.view, extent, IMAGE),
        rawreel::PassRegions{.picture = {0, 0, 211, 97}, .pictureDrawn = {0, 0, 211, 97}}, false);
    CHECK(regionMatch(first, extent, first_constants, g.image()).exact ==
          std::size_t{extent.width} * extent.height);

    params.overlay.strips = {.top = 20, .left = 20};
    auto below = renderer->frame(frameParams(extent, dvv::gpu::PresentEncoding::Linear, 1, true),
                                 DisplaySnapshot{frameSet(g, 0, 1), params, 1});
    REQUIRE_MESSAGE(below, (below ? std::string() : below.error().describe()));
    REQUIRE(static_cast<bool>(below->record));
    CHECK(below->image == whole->image);
    const dvv::media::PixelRect picture{20, 20, 191, 77};
    CHECK(renderer->lastRecorded().picture == picture);
    CHECK(renderer->lastRecorded().pictureRecords == 2);
    CHECK(renderer->lastRecorded().rulerRecords == 1);  // the strips changed
    submit(std::move(below->record));
    const std::vector<std::byte> second = read(below->image);
    // The strips hold the ruler pass (no image: the frame is G, so its bands with ticks).
    CHECK(rulerMismatches(second, extent,
                          rawreel::rulerConstants(
                              params.overlay, resolveView(params.view, picture.size(), IMAGE),
                              regionsOf(extent, params.overlay,
                                        resolveView(params.view, picture.size(), IMAGE)),
                              std::nullopt, false),
                          false) == 0);
    const CompositeConstants constants = rawreel::compositeConstants(
        params, resolveView(params.view, picture.size(), IMAGE),
        regionsOf(extent, params.overlay, resolveView(params.view, picture.size(), IMAGE)), false);
    CHECK(regionMatch(second, extent, constants, g.image()).exact ==
          std::size_t{picture.width - constants.drawX} * (picture.height - constants.drawY));

    // The strips disappear: the picture pass over the grown P overwrites the old strip pixels, and
    // no ruler pass records.
    params.overlay.strips = {};
    auto whole_again =
        renderer->frame(frameParams(extent, dvv::gpu::PresentEncoding::Linear, 2, true),
                        DisplaySnapshot{frameSet(g, 0, 1), params, 1});
    REQUIRE_MESSAGE(whole_again, (whole_again ? std::string() : whole_again.error().describe()));
    REQUIRE(static_cast<bool>(whole_again->record));
    CHECK(whole_again->image == whole->image);
    CHECK(renderer->lastRecorded().pictureRecords == 3);
    CHECK(renderer->lastRecorded().rulerRecords == 1);
    submit(std::move(whole_again->record));
    CHECK(regionMatch(read(whole_again->image), extent, first_constants, g.image()).exact ==
          std::size_t{extent.width} * extent.height);
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: the grid in a P taller than wide") {
    // The prepass's row entries at y >= P's width are read too.
    const DisplayFrame g =
        displayFrame("synthetic:gradient?w=64&h=48&fmt=yuv420p&frames=1", {64, 48});
    const PixelSize p{97, 211};
    const DisplayParams params{.view = {ViewMode::Zoom, 4, {12, 26}, {64, 48}},
                               .overlay = testGrid(24, 4)};
    const auto renderer = makeRenderer(context(), 1);
    auto frame = renderer->frame(frameParams(p, dvv::gpu::PresentEncoding::Linear, 0, false),
                                 DisplaySnapshot{frameSet(g, 0, 1), params, 1});
    REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
    REQUIRE(static_cast<bool>(frame->record));
    submit(std::move(frame->record));
    const std::vector<std::byte> bytes = read(frame->image);
    const CompositeConstants constants =
        rawreel::compositeConstants(params, resolveView(params.view, p, {64, 48}), {}, false);
    REQUIRE((constants.levelShape[2] & rawreel::LEVEL_DRAWN) != 0);
    CHECK(regionMatch(bytes, p, constants, g.image()).exact == std::size_t{p.width} * p.height);
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: a cursor move records only the ruler pass") {
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame g = displayFrame(G);
    const PixelSize extent{211, 97};
    DisplaySnapshot snapshot{frameSet(g, 0, 1), DisplayParams{.overlay = testRulers(4)}, 1};
    auto first =
        renderer->frame(frameParams(extent, dvv::gpu::PresentEncoding::Linear, 0, false), snapshot);
    REQUIRE_MESSAGE(first, (first ? std::string() : first.error().describe()));
    REQUIRE(static_cast<bool>(first->record));
    submit(std::move(first->record));
    CHECK(renderer->lastRecorded().pictureRecords == 1);
    CHECK(renderer->lastRecorded().rulerRecords == 1);
    const RecordedInfo picture_info = renderer->lastRecorded();
    // Fit of 37 x 23 in P = 181 x 67: about 2.9 output pixels per image pixel, the image from P x
    // 36; each point lies on the image.
    for (std::uint64_t i = 0; i < 5; ++i) {
        snapshot.params.cursor = dvv::media::PointD{80.5 + (10.0 * static_cast<double>(i)), 50.5};
        auto moved = renderer->frame(
            frameParams(extent, dvv::gpu::PresentEncoding::Linear, i + 1, true), snapshot);
        REQUIRE(moved);
        REQUIRE(static_cast<bool>(moved->record));
        CHECK(moved->image == first->image);
        submit(std::move(moved->record));
    }
    RecordedInfo expected = picture_info;
    expected.rulerRecords = 6;
    // A cursor move dispatches only the rulers of the bands.
    const ViewTransform fit = resolveView(snapshot.params.view, {181, 67}, IMAGE);
    expected.rulerDispatched = regionsOf(extent, snapshot.params.overlay, fit).bandRulers;
    CHECK(picture_info.rulerDispatched == regionsOf(extent, snapshot.params.overlay, fit).bands);
    CHECK(renderer->lastRecorded() == expected);  // the picture fields are kept
    // Inside the same image pixel: nothing records.
    snapshot.params.cursor = dvv::media::PointD{120.9, 50.9};
    auto same =
        renderer->frame(frameParams(extent, dvv::gpu::PresentEncoding::Linear, 6, true), snapshot);
    REQUIRE(same);
    CHECK_FALSE(static_cast<bool>(same->record));
    CHECK(renderer->lastRecorded() == expected);
    // The theme alone: both passes.
    snapshot.params.overlay.dark = false;
    auto theme =
        renderer->frame(frameParams(extent, dvv::gpu::PresentEncoding::Linear, 7, true), snapshot);
    REQUIRE(theme);
    REQUIRE(static_cast<bool>(theme->record));
    submit(std::move(theme->record));
    CHECK(renderer->lastRecorded().pictureRecords == 2);
    CHECK(renderer->lastRecorded().rulerRecords == 7);
    CHECK(renderer->lastRecorded().rulerDispatched ==
          regionsOf(extent, snapshot.params.overlay, fit).bands);
}

// An RGBA gradient whose alpha ramp is x + y + 192 (mod 256) over a span of 255: every 8-bit
// alpha, exact 0 and 1 and the two steps beside 0.5, beside the wrap from 1 to 0.
constexpr std::string_view ALPHA_URI = "synthetic:gradient?w=160&h=97&fmt=rgba&frames=1";
constexpr PixelSize ALPHA_IMAGE{160, 97};

struct AlphaShown {
    std::size_t clear = 0;   // alpha exactly 0
    std::size_t opaque = 0;  // alpha exactly 1
    std::size_t half = 0;    // alpha 127/255 or 128/255
};

// The alpha of the texels P shows at a zoom >= 1, where each pixel shows one texel unmixed.
AlphaShown alphaShown(const CompositeConstants& k, const DisplayFrame& frame) {
    const dvv::media::AxisView ax{
        .offset = k.offsetX, .zoom = k.zoom, .size = k.imageW, .extent = k.viewportW};
    const dvv::media::AxisView ay{
        .offset = k.offsetY, .zoom = k.zoom, .size = k.imageH, .extent = k.viewportH};
    const dvv::media::PixelSpan xs = dvv::media::imageSpan(ax);
    const dvv::media::PixelSpan ys = dvv::media::imageSpan(ay);
    AlphaShown shown;
    for (std::int32_t y = ys.begin; y < ys.end; ++y) {
        const auto v = static_cast<std::size_t>(std::floor(dvv::media::imageCoordinateAt(ay, y)));
        for (std::int32_t x = xs.begin; x < xs.end; ++x) {
            const auto u =
                static_cast<std::size_t>(std::floor(dvv::media::imageCoordinateAt(ax, x)));
            const float alpha =
                dvv::media::halfToFloat(frame.host[(((v * frame.size.width) + u) * 4) + 3]);
            shown.clear += alpha == 0.0F ? 1 : 0;
            shown.opaque += alpha == 1.0F ? 1 : 0;
            shown.half += std::abs(alpha - 0.5F) < 0.003F ? 1 : 0;
        }
    }
    return shown;
}

struct AlphaView {
    double zoom = 1;
    dvv::media::PointD centre;
    bool ends = false;  // P shows texels of alpha exactly 0 and 1
    bool half = false;  // P shows the texels beside alpha 0.5
};

TEST_CASE_FIXTURE(CompositorTest, "compositor: alpha over the checker matches the CPU reference") {
    const DisplayFrame a = displayFrame(ALPHA_URI, ALPHA_IMAGE);
    const PixelSize p{61, 43};
    const auto render = [&](const DisplayParams& params, dvv::gpu::PresentEncoding encoding) {
        const auto renderer = makeRenderer(context(), 1);
        auto frame = renderer->frame(frameParams(p, encoding, 0, false),
                                     DisplaySnapshot{frameSet(a, 0, 1), params, 1});
        REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
        REQUIRE(static_cast<bool>(frame->record));
        submit(std::move(frame->record));
        return read(frame->image);
    };
    // Centred on (100, 61) at every zoom; at 100 % and 200 % also on (32, 32), which shows
    // alpha 1 and 0 at x + y = 63 and 64, and on (110, 82), which shows 127/255 and 128/255 at
    // x + y = 191 and 192. Each view is panned to negative offsets at 50 % and above.
    const std::array<AlphaView, 9> views{{{.zoom = 1.0, .centre = {100, 61}},
                                          {.zoom = 2.0, .centre = {100, 61}},
                                          {.zoom = 0.5, .centre = {100, 61}},
                                          {.zoom = 0.25, .centre = {100, 61}},
                                          {.zoom = 0.375, .centre = {100, 61}},
                                          {.zoom = 1.0, .centre = {32, 32}, .ends = true},
                                          {.zoom = 2.0, .centre = {32, 32}, .ends = true},
                                          {.zoom = 1.0, .centre = {110, 82}, .half = true},
                                          {.zoom = 2.0, .centre = {110, 82}, .half = true}}};
    for (const auto encoding :
         {dvv::gpu::PresentEncoding::Linear, dvv::gpu::PresentEncoding::Srgb}) {
        const bool skip = encoding == dvv::gpu::PresentEncoding::Srgb;
        for (const bool dark : {false, true}) {
            for (const bool alpha : {false, true}) {
                for (const AlphaView& shown_view : views) {
                    const double zoom = shown_view.zoom;
                    CAPTURE(skip);
                    CAPTURE(dark);
                    CAPTURE(alpha);
                    CAPTURE(zoom);
                    CAPTURE(shown_view.centre.x);
                    CAPTURE(shown_view.centre.y);
                    const DisplayParams params{
                        .view = {ViewMode::Zoom, zoom, shown_view.centre, ALPHA_IMAGE},
                        .overlay = {.dark = dark, .checkerSize = 8},
                        .alpha = alpha};
                    const ViewTransform view = resolveView(params.view, p, ALPHA_IMAGE);
                    const CompositeConstants constants =
                        rawreel::compositeConstants(params, view, {}, skip);
                    REQUIRE(((constants.flags & rawreel::COMPOSITE_ALPHA) != 0) == alpha);
                    if (zoom >= 0.5) {
                        REQUIRE(constants.offsetX < 0);
                        REQUIRE(constants.offsetY < 0);
                    }
                    if (shown_view.ends || shown_view.half) {
                        const AlphaShown shown = alphaShown(constants, a);
                        REQUIRE((!shown_view.ends || (shown.clear > 0 && shown.opaque > 0)));
                        REQUIRE((!shown_view.half || shown.half > 0));
                    }
                    const std::vector<std::byte> bytes = render(params, encoding);
                    if (zoom == 0.375) {  // not a power of two: the rule of the Fit cases
                        CHECK(mismatches(bytes, constants, a.image()) <= allowance(p));
                        CHECK(regionMatch(bytes, p, constants, a.image()).missed == 0);
                    } else {
                        CHECK(mismatches(bytes, constants, a.image()) == 0);
                    }
                }
            }
        }
    }
    // Squares other than 8 (an odd one, the 150 % and the 200 % size): the shader reads the
    // size; the reference with square 8 tells the sizes apart.
    for (const std::uint32_t size : {3U, 12U, 16U}) {
        CAPTURE(size);
        const DisplayParams params{.view = {ViewMode::Zoom, 1.0, {32, 32}, ALPHA_IMAGE},
                                   .overlay = {.checkerSize = size},
                                   .alpha = true};
        const ViewTransform view = resolveView(params.view, p, ALPHA_IMAGE);
        const CompositeConstants constants = rawreel::compositeConstants(params, view, {}, false);
        REQUIRE(constants.checkerSize == size);
        CompositeConstants eight = constants;
        eight.checkerSize = 8;
        const std::vector<std::byte> bytes = render(params, dvv::gpu::PresentEncoding::Linear);
        CHECK(mismatches(bytes, constants, a.image()) == 0);
        CHECK(mismatches(bytes, eight, a.image()) > 0);
    }
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: toggling alpha re-records only the picture pass") {
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame g = displayFrame(G);
    const PixelSize extent{211, 97};
    DisplaySnapshot snapshot{frameSet(g, 0, 1), DisplayParams{.overlay = testRulers(4)}, 1};
    auto first =
        renderer->frame(frameParams(extent, dvv::gpu::PresentEncoding::Linear, 0, false), snapshot);
    REQUIRE_MESSAGE(first, (first ? std::string() : first.error().describe()));
    REQUIRE(static_cast<bool>(first->record));
    submit(std::move(first->record));
    CHECK(renderer->lastRecorded().pictureRecords == 1);
    CHECK(renderer->lastRecorded().rulerRecords == 1);
    CHECK((renderer->lastRecorded().pictureFlags & rawreel::COMPOSITE_ALPHA) == 0);
    // The same version: only the params change, on and off again.
    for (std::uint64_t i = 1; i <= 2; ++i) {
        const bool on = i == 1;
        CAPTURE(on);
        snapshot.params.alpha = on;
        auto toggled = renderer->frame(
            frameParams(extent, dvv::gpu::PresentEncoding::Linear, i, true), snapshot);
        REQUIRE(toggled);
        REQUIRE(static_cast<bool>(toggled->record));
        CHECK(toggled->image == first->image);
        submit(std::move(toggled->record));
        const RecordedInfo info = renderer->lastRecorded();
        CHECK(info.pictureRecords == 1 + i);
        CHECK(info.rulerRecords == 1);
        CHECK(((info.pictureFlags & rawreel::COMPOSITE_ALPHA) != 0) == on);
    }
}

TEST_CASE_FIXTURE(CompositorTest,
                  "compositor: a cursor-colour change records only the ruler pass") {
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame g = displayFrame(G);
    const PixelSize extent{211, 97};
    DisplaySnapshot snapshot{
        frameSet(g, 0, 1),
        DisplayParams{.overlay = testRulers(4), .cursor = dvv::media::PointD{80.5, 50.5}}, 1};
    auto first =
        renderer->frame(frameParams(extent, dvv::gpu::PresentEncoding::Linear, 0, false), snapshot);
    REQUIRE_MESSAGE(first, (first ? std::string() : first.error().describe()));
    REQUIRE(static_cast<bool>(first->record));
    submit(std::move(first->record));
    snapshot.params.overlay.cursorColour = 0x34c759;
    auto recoloured =
        renderer->frame(frameParams(extent, dvv::gpu::PresentEncoding::Linear, 1, true), snapshot);
    REQUIRE(recoloured);
    REQUIRE(static_cast<bool>(recoloured->record));
    CHECK(recoloured->image == first->image);
    submit(std::move(recoloured->record));
    CHECK(renderer->lastRecorded().pictureRecords == 1);
    CHECK(renderer->lastRecorded().rulerRecords == 2);
    const ViewTransform fit = resolveView(snapshot.params.view, {181, 67}, IMAGE);
    const rawreel::PassRegions regions = regionsOf(extent, snapshot.params.overlay, fit);
    CHECK(renderer->lastRecorded().rulerDispatched == regions.bands);
    const std::optional<dvv::media::ImagePixel> cursor =
        dvv::media::imagePixelAt(fit, {80.5 - 30, 50.5 - 30});
    REQUIRE(cursor.has_value());
    const auto constants =
        rawreel::rulerConstants(snapshot.params.overlay, fit, regions, cursor, false);
    REQUIRE((constants[0].flags & rawreel::RULER_LIT) != 0);
    CHECK(rulerMismatches(read(recoloured->image), extent, constants, false) == 0);
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: a dropped frame records both passes") {
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame g = displayFrame(G);
    const PixelSize extent{211, 97};
    const DisplaySnapshot snapshot{frameSet(g, 0, 1), DisplayParams{.overlay = testRulers(4)}, 1};
    for (std::uint64_t i = 0; i < 2; ++i) {
        auto frame = renderer->frame(
            frameParams(extent, dvv::gpu::PresentEncoding::Linear, i, false), snapshot);
        REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
        REQUIRE(static_cast<bool>(frame->record));
        submit(std::move(frame->record));
        CHECK(renderer->lastRecorded().pictureRecords == i + 1);
        CHECK(renderer->lastRecorded().rulerRecords == i + 1);
    }
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: each pass records on its own inputs only") {
    // What reruns which pass, with the rulers shown.
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame g = displayFrame(G);
    const PixelSize extent{211, 97};
    DisplaySnapshot snapshot{frameSet(g, 0, 1), DisplayParams{.overlay = testRulers(4)}, 1};
    dvv::gpu::PresentEncoding encoding = dvv::gpu::PresentEncoding::Linear;
    std::uint64_t number = 0;
    const auto step = [&](std::uint64_t pictures, std::uint64_t rulers) {
        auto frame = renderer->frame(frameParams(extent, encoding, number, number != 0), snapshot);
        ++number;
        REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
        REQUIRE(static_cast<bool>(frame->record));
        submit(std::move(frame->record));
        CHECK(renderer->lastRecorded().pictureRecords == pictures);
        CHECK(renderer->lastRecorded().rulerRecords == rulers);
    };
    step(1, 1);
    // Playback: the next FrameSet records the picture pass only.
    snapshot = DisplaySnapshot{frameSet(g, 1, 1), snapshot.params, 2};
    step(2, 1);
    snapshot = DisplaySnapshot{frameSet(g, 2, 1), snapshot.params, 3};
    step(3, 1);
    // The encode rule alone switches: both passes.
    encoding = dvv::gpu::PresentEncoding::Srgb;
    step(4, 2);
    encoding = dvv::gpu::PresentEncoding::Linear;
    step(5, 3);
    // A grid look alone (colour and opacity, halo, adaptive, a level shown): the picture pass.
    snapshot.params.overlay.levels[0].colour = 0x8040C0FFU;
    step(6, 3);
    snapshot.params.overlay.levels[0].halo = false;
    step(7, 3);
    snapshot.params.overlay.levels[0].adaptive = !snapshot.params.overlay.levels[0].adaptive;
    step(8, 3);
    snapshot.params.overlay.levels[2].shown = false;
    step(9, 3);
    // A ruler setting alone: the ruler pass.
    snapshot.params.overlay.tickLength = {18, 10, 4};
    step(9, 4);
    snapshot.params.overlay.markerDepth = 4;
    step(9, 5);
    snapshot.params.overlay.litDepth = 24;
    step(9, 6);
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: rulers hidden dispatch no ruler pass") {
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame g = displayFrame(G);
    const PixelSize extent{211, 97};
    DisplaySnapshot snapshot{frameSet(g, 0, 1), DisplayParams{.overlay = testGrid(4, 2)}, 1};
    for (std::uint64_t i = 0; i < 3; ++i) {
        snapshot.params.cursor = dvv::media::PointD{20.5 + (30.0 * static_cast<double>(i)), 40.5};
        auto frame = renderer->frame(
            frameParams(extent, dvv::gpu::PresentEncoding::Linear, i, i != 0), snapshot);
        REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
        CHECK(static_cast<bool>(frame->record) == (i == 0));
        if (frame->record) {
            submit(std::move(frame->record));
        }
    }
    CHECK(renderer->lastRecorded().pictureRecords == 1);
    CHECK(renderer->lastRecorded().rulerRecords == 0);
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: the ruler pass matches its CPU reference") {
    const auto draw = [this](PixelSize extent, const FrameSet& frames, const DisplayParams& params,
                             dvv::gpu::PresentEncoding encoding) {
        const auto renderer = makeRenderer(context(), 1);
        auto frame = renderer->frame(frameParams(extent, encoding, 0, false),
                                     DisplaySnapshot{frames.share(), params, 1});
        REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
        REQUIRE(static_cast<bool>(frame->record));
        submit(std::move(frame->record));
        return read(frame->image);
    };
    // The bands as frame() builds them: the cursor mapped in P.
    const auto bands = [](PixelSize extent, const DisplayParams& params, PixelSize image,
                          bool skip) {
        const dvv::media::PixelRect p = dvv::media::pictureRegion(extent, params.overlay.strips);
        const ViewTransform view = resolveView(params.view, p.size(), image);
        const std::optional<dvv::media::ImagePixel> cursor =
            dvv::media::imagePixelAt(view, {params.cursor->x - p.x, params.cursor->y - p.y});
        REQUIRE(cursor.has_value());
        return rawreel::rulerConstants(params.overlay, view,
                                       regionsOf(extent, params.overlay, view), cursor, skip);
    };
    for (const auto encoding :
         {dvv::gpu::PresentEncoding::Linear, dvv::gpu::PresentEncoding::Srgb}) {
        for (const bool dark : {false, true}) {
            const bool skip = encoding == dvv::gpu::PresentEncoding::Srgb;
            CAPTURE(skip);
            CAPTURE(dark);
            SUBCASE("400 %: exact, the lit tick at tick widths 1 and 2") {
                const DisplayFrame g =
                    displayFrame("synthetic:gradient?w=64&h=48&fmt=yuv420p&frames=1", {64, 48});
                const PixelSize extent{286, 222};  // P = 256 x 192
                for (const std::uint32_t width : {1U, 2U}) {
                    CAPTURE(width);
                    DisplayParams params{.view = {ViewMode::Zoom, 4, {32, 24}, {64, 48}},
                                         .overlay = testRulers(24),
                                         .cursor = dvv::media::PointD{160.5, 80.5}};
                    params.overlay.dark = dark;
                    params.overlay.tickWidth = width;
                    const auto constants = bands(extent, params, {64, 48}, skip);
                    REQUIRE((constants[0].flags & rawreel::RULER_TICKS) != 0);
                    REQUIRE((constants[0].levelShape[2] & rawreel::LEVEL_DRAWN) != 0);
                    REQUIRE((constants[0].flags & rawreel::RULER_LIT) != 0);
                    REQUIRE((constants[1].flags & rawreel::RULER_LIT) != 0);
                    // The lit tick reaches past the tick band into the label band: the
                    // outermost lit row (depth 27) is compared too. The column lit_x - 1 is lit
                    // at both widths and lies left of the marker, so its depth-0 pixel is the
                    // lit colour alone.
                    const rawreel::RulerConstants& top = constants[0];
                    const std::uint32_t lit_x = top.pictureStart + (4 * top.litTick) - 1;
                    REQUIRE(top.litDepth == 28);
                    REQUIRE(
                        rawreel::rulerPixel(top, nullptr, lit_x, top.rulerEdge - top.litDepth) ==
                        rawreel::rulerPixel(top, nullptr, lit_x, top.rulerEdge - 1));
                    REQUIRE(rawreel::rulerPixel(top, nullptr, lit_x,
                                                top.rulerEdge - top.litDepth - 1) !=
                            rawreel::rulerPixel(top, nullptr, lit_x, top.rulerEdge - 1));
                    const std::vector<std::byte> bytes =
                        draw(extent, frameSet(g, 0, 1), params, encoding);
                    CHECK(rulerMismatches(bytes, extent, constants, false) == 0);
                }
            }
            SUBCASE("Fit 47.3 %: within one pixel") {
                const DisplayFrame g = displayFrame(
                    "synthetic:gradient?w=1920&h=1080&fmt=yuv420p&frames=1", {1920, 1080});
                const PixelSize extent{938, 541};  // P = 908 x 511
                DisplayParams params{.overlay = testRulers(12),
                                     .cursor = dvv::media::PointD{500.5, 300.5}};
                params.overlay.dark = dark;
                const auto constants = bands(extent, params, {1920, 1080}, skip);
                REQUIRE((constants[1].flags & rawreel::RULER_TICKS) != 0);
                const std::vector<std::byte> bytes =
                    draw(extent, frameSet(g, 0, 1), params, encoding);
                const std::size_t exact = rulerMismatches(bytes, extent, constants, false);
                MESSAGE("Fit 47.3 %: " << exact << " band pixels off the reference");
                CHECK(rulerMismatches(bytes, extent, constants, true) == 0);
            }
            SUBCASE("a smaller picture: exact") {
                // The 100 x 50 image at zoom 1 in P = {30, 30, 270, 170}: the rulers hug
                // H = {115, 90, 100, 50}, the rest of the bands is the background.
                const DisplayFrame g = displayFrame(HUG_IMAGE_URI, HUG_IMAGE);
                const PixelSize extent{300, 200};
                DisplayParams params{.view = {ViewMode::Zoom, 1, {50, 25}, HUG_IMAGE},
                                     .overlay = testRulers(24),
                                     .cursor = dvv::media::PointD{150.5, 110.5}};
                params.overlay.dark = dark;
                const auto constants = bands(extent, params, HUG_IMAGE, skip);
                REQUIRE(constants[0].rulerEdge == 90);
                REQUIRE(constants[0].rulerBegin == 115);
                REQUIRE(constants[0].rulerEnd == 215);
                REQUIRE(constants[1].rulerEdge == 115);
                REQUIRE((constants[0].flags & rawreel::RULER_CURSOR) != 0);
                const std::vector<std::byte> bytes =
                    draw(extent, frameSet(g, 0, 1), params, encoding);
                CHECK(rulerMismatches(bytes, extent, constants, false) == 0);
            }
        }
    }
}

TEST_CASE_FIXTURE(CompositorTest,
                  "compositor: the picture pass from the hugged rect matches the reference") {
    // The 100 x 50 image, centred in P = {30, 30, 270, 170} of 300 x 200: the picture pass starts
    // at the hugged rect H and draws each pixel as the reference draws that pixel of P, so the
    // grid lines stay at their image boundaries.
    const DisplayFrame g = displayFrame(HUG_IMAGE_URI, HUG_IMAGE);
    const PixelSize extent{300, 200};
    rawreel::OverlayParams overlay = testGrid(8, 2);  // three levels, the major one with a halo
    overlay.levels[2].minimumSpacing = 2;             // the second minor drawn at zoom 1 too
    overlay.strips = {.top = 30, .left = 30};
    struct Case {
        std::string name;
        dvv::media::ViewRequest view;
        bool exact;  // a zoom that is a power of two: each pixel at its own reference pixel
    };
    for (const Case& c : {Case{"zoom 1", {ViewMode::Zoom, 1, {50, 25}, HUG_IMAGE}, true},
                          Case{"Fit 270 %", {}, false}}) {
        for (const auto encoding :
             {dvv::gpu::PresentEncoding::Linear, dvv::gpu::PresentEncoding::Srgb}) {
            const bool skip = encoding == dvv::gpu::PresentEncoding::Srgb;
            CAPTURE(c.name);
            CAPTURE(skip);
            const DisplayParams params{.view = c.view, .overlay = overlay};
            const dvv::media::PixelRect p = dvv::media::pictureRegion(extent, overlay.strips);
            const ViewTransform view = resolveView(params.view, p.size(), HUG_IMAGE);
            const rawreel::PassRegions regions = regionsOf(extent, overlay, view);
            REQUIRE(regions.pictureDrawn != regions.picture);
            const auto renderer = makeRenderer(context(), 1);
            auto frame = renderer->frame(frameParams(extent, encoding, 0, false),
                                         DisplaySnapshot{frameSet(g, 0, 1), params, 1});
            REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
            REQUIRE(static_cast<bool>(frame->record));
            // No column of [P.x, H.x) and no row of [P.y, H.y) is dispatched.
            CHECK(renderer->lastRecorded().pictureDrawn == regions.pictureDrawn);
            CHECK(renderer->lastRecorded().rulers == regions.rulers);
            submit(std::move(frame->record));
            const std::vector<std::byte> bytes = read(frame->image);
            const CompositeConstants constants =
                rawreel::compositeConstants(params, view, regions, skip);
            for (std::size_t l = 0; l < 3; ++l) {
                REQUIRE((constants.levelShape[l] & rawreel::LEVEL_DRAWN) != 0);
            }
            const RegionMatch match = regionMatch(bytes, extent, constants, g.image());
            const std::size_t drawn =
                std::size_t{regions.pictureDrawn.width} * regions.pictureDrawn.height;
            MESSAGE(c.name << ": " << drawn - match.exact
                           << " pixels off the reference at the same pixel");
            if (c.exact) {
                CHECK(match.exact == drawn);
            } else {
                CHECK(match.missed == 0);
            }
        }
    }
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: the reserved space is the picture's background") {
    // The 100 x 50 image at zoom 1 in P = {30, 30, 270, 170} of 300 x 200: H = {115, 90, 100, 50}.
    // The ruler pass fills the space it reserves beside the rulers with the very grey the picture
    // pass draws around the image.
    const DisplayFrame g = displayFrame(HUG_IMAGE_URI, HUG_IMAGE);
    const PixelSize extent{300, 200};
    for (const auto encoding :
         {dvv::gpu::PresentEncoding::Linear, dvv::gpu::PresentEncoding::Srgb}) {
        for (const bool dark : {false, true}) {
            CAPTURE(encoding == dvv::gpu::PresentEncoding::Srgb);
            CAPTURE(dark);
            DisplayParams params{.view = {ViewMode::Zoom, 1, {50, 25}, HUG_IMAGE},
                                 .overlay = testRulers(24)};
            params.overlay.dark = dark;
            const auto renderer = makeRenderer(context(), 1);
            auto frame = renderer->frame(frameParams(extent, encoding, 0, false),
                                         DisplaySnapshot{frameSet(g, 0, 1), params, 1});
            REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
            REQUIRE(static_cast<bool>(frame->record));
            REQUIRE(renderer->lastRecorded().rulers.hugged ==
                    dvv::media::PixelRect{115, 90, 100, 50});
            submit(std::move(frame->record));
            const std::vector<std::byte> bytes = read(frame->image);
            // Fit-border pixels of P, beside and below the image.
            const Rgba8 border = pixelAt(bytes, extent, 250, 100);
            CHECK(pixelAt(bytes, extent, 120, 170) == border);
            // The top band above the ruler and left of the corner, the left band below the ruler.
            for (const auto [x, y] :
                 {std::pair{100U, 10U}, std::pair{50U, 75U}, std::pair{10U, 40U},
                  std::pair{50U, 185U}, std::pair{100U, 150U}}) {
                CAPTURE(x);
                CAPTURE(y);
                CHECK(pixelAt(bytes, extent, x, y) == border);
            }
        }
    }
}

TEST_CASE_FIXTURE(CompositorTest,
                  "compositor: a pan records both passes, a new frame only the picture") {
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame g = displayFrame(G);
    const PixelSize extent{211, 97};
    DisplaySnapshot snapshot{
        frameSet(g, 0, 1),
        DisplayParams{.view = {ViewMode::Zoom, 8, {18.5, 11.5}, IMAGE}, .overlay = testRulers(4)},
        1};
    std::uint64_t number = 0;
    const auto step = [&](std::uint64_t pictures, std::uint64_t rulers) {
        auto frame = renderer->frame(
            frameParams(extent, dvv::gpu::PresentEncoding::Linear, number, number != 0), snapshot);
        ++number;
        REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
        REQUIRE(static_cast<bool>(frame->record));
        submit(std::move(frame->record));
        CHECK(renderer->lastRecorded().pictureRecords == pictures);
        CHECK(renderer->lastRecorded().rulerRecords == rulers);
    };
    step(1, 1);
    // A pan moves the picture and the rulers' scale: both passes.
    snapshot.params.view.centre = {14.5, 11.5};
    step(2, 2);
    // A new frame with the same view: the picture pass alone.
    snapshot = DisplaySnapshot{frameSet(g, 1, 1), snapshot.params, 2};
    step(3, 2);
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: a new frame leaves the hugging rulers intact") {
    // The 100 x 50 image at zoom 1 in P = {30, 30, 270, 170} of 300 x 200: H = {115, 90, 100, 50},
    // so the left ruler, columns [85, 115), lies inside P's columns. A picture-only record over
    // that layout must not touch a band pixel.
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame g = displayFrame(HUG_IMAGE_URI, HUG_IMAGE);
    const PixelSize extent{300, 200};
    DisplaySnapshot snapshot{
        frameSet(g, 0, 1),
        DisplayParams{.view = {ViewMode::Zoom, 1, {50, 25}, HUG_IMAGE}, .overlay = testRulers(24)},
        1};
    auto first =
        renderer->frame(frameParams(extent, dvv::gpu::PresentEncoding::Linear, 0, false), snapshot);
    REQUIRE_MESSAGE(first, (first ? std::string() : first.error().describe()));
    REQUIRE(static_cast<bool>(first->record));
    REQUIRE(renderer->lastRecorded().rulers.hugged == dvv::media::PixelRect{115, 90, 100, 50});
    submit(std::move(first->record));
    snapshot = DisplaySnapshot{frameSet(g, 1, 1), snapshot.params, 2};
    auto next =
        renderer->frame(frameParams(extent, dvv::gpu::PresentEncoding::Linear, 1, true), snapshot);
    REQUIRE(next);
    REQUIRE(static_cast<bool>(next->record));
    submit(std::move(next->record));
    CHECK(renderer->lastRecorded().pictureRecords == 2);
    CHECK(renderer->lastRecorded().rulerRecords == 1);
    CHECK(renderer->lastRecorded().rulerDispatched == std::array<dvv::media::PixelRect, 2>{});
    const dvv::media::PixelRect p =
        dvv::media::pictureRegion(extent, snapshot.params.overlay.strips);
    const ViewTransform view = resolveView(snapshot.params.view, p.size(), HUG_IMAGE);
    const auto constants = rawreel::rulerConstants(snapshot.params.overlay, view,
                                                   regionsOf(extent, snapshot.params.overlay, view),
                                                   std::nullopt, false);
    CHECK(rulerMismatches(read(next->image), extent, constants, false) == 0);
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: a cursor move dispatches only the rulers") {
    // The hugging layout of the test above: the bands hold far more background than ruler.
    const DisplayFrame g = displayFrame(HUG_IMAGE_URI, HUG_IMAGE);
    const PixelSize extent{300, 200};
    for (const auto encoding :
         {dvv::gpu::PresentEncoding::Linear, dvv::gpu::PresentEncoding::Srgb}) {
        const bool skip = encoding == dvv::gpu::PresentEncoding::Srgb;
        CAPTURE(skip);
        const auto renderer = makeRenderer(context(), 1);
        DisplaySnapshot snapshot{frameSet(g, 0, 1),
                                 DisplayParams{.view = {ViewMode::Zoom, 1, {50, 25}, HUG_IMAGE},
                                               .overlay = testRulers(24),
                                               .cursor = dvv::media::PointD{120.5, 95.5}},
                                 1};
        auto first = renderer->frame(frameParams(extent, encoding, 0, false), snapshot);
        REQUIRE_MESSAGE(first, (first ? std::string() : first.error().describe()));
        REQUIRE(static_cast<bool>(first->record));
        submit(std::move(first->record));
        const dvv::media::PixelRect p =
            dvv::media::pictureRegion(extent, snapshot.params.overlay.strips);
        const ViewTransform view = resolveView(snapshot.params.view, p.size(), HUG_IMAGE);
        const rawreel::PassRegions regions = regionsOf(extent, snapshot.params.overlay, view);
        REQUIRE(regions.rulers.hugged == dvv::media::PixelRect{115, 90, 100, 50});
        CHECK(renderer->lastRecorded().rulerDispatched == regions.bands);
        // Image pixel (5, 5) to (60, 40): the old marker and lit ticks go, the new ones appear.
        snapshot.params.cursor = dvv::media::PointD{175.5, 130.5};
        auto moved = renderer->frame(frameParams(extent, encoding, 1, true), snapshot);
        REQUIRE(moved);
        REQUIRE(static_cast<bool>(moved->record));
        submit(std::move(moved->record));
        CHECK(renderer->lastRecorded().pictureRecords == 1);
        CHECK(renderer->lastRecorded().rulerRecords == 2);
        CHECK(renderer->lastRecorded().rulerDispatched == regions.bandRulers);
        const auto constants = rawreel::rulerConstants(snapshot.params.overlay, view, regions,
                                                       dvv::media::ImagePixel{60, 40}, skip);
        const auto old_constants = rawreel::rulerConstants(snapshot.params.overlay, view, regions,
                                                           dvv::media::ImagePixel{5, 5}, skip);
        for (std::size_t b = 0; b < 2; ++b) {
            CAPTURE(b);
            REQUIRE((constants[b].flags & rawreel::RULER_LIT) != 0);
            REQUIRE((old_constants[b].flags & rawreel::RULER_LIT) != 0);
            CHECK(constants[b].litTick != old_constants[b].litTick);
        }
        CHECK(rulerMismatches(read(moved->image), extent, constants, false) == 0);
        // A cursor move and a zoom that moves H in one frame (a pan cannot: a picture smaller than
        // P stays centred): the whole new bands, since the old rulers lie where the new bands hold
        // background.
        snapshot.params.view.zoom = 2;
        snapshot.params.cursor = dvv::media::PointD{176.0, 126.0};
        auto zoomed = renderer->frame(frameParams(extent, encoding, 2, true), snapshot);
        REQUIRE(zoomed);
        REQUIRE(static_cast<bool>(zoomed->record));
        submit(std::move(zoomed->record));
        const ViewTransform zoomed_view = resolveView(snapshot.params.view, p.size(), HUG_IMAGE);
        const rawreel::PassRegions zoomed_regions =
            regionsOf(extent, snapshot.params.overlay, zoomed_view);
        REQUIRE(zoomed_regions.rulers.hugged == dvv::media::PixelRect{65, 65, 200, 100});
        CHECK(renderer->lastRecorded().pictureRecords == 2);
        CHECK(renderer->lastRecorded().rulerRecords == 3);
        CHECK(renderer->lastRecorded().rulerDispatched == zoomed_regions.bands);
        const auto zoomed_constants =
            rawreel::rulerConstants(snapshot.params.overlay, zoomed_view, zoomed_regions,
                                    dvv::media::ImagePixel{55, 30}, skip);
        for (std::size_t b = 0; b < 2; ++b) {
            CAPTURE(b);
            REQUIRE((zoomed_constants[b].flags & rawreel::RULER_LIT) != 0);
            CHECK(zoomed_constants[b].litTick != constants[b].litTick);
        }
        CHECK(rulerMismatches(read(zoomed->image), extent, zoomed_constants, false) == 0);
        // The cursor leaves the picture: the marker and the lit ticks go, within the rulers.
        snapshot.params.cursor = std::nullopt;
        auto left = renderer->frame(frameParams(extent, encoding, 3, true), snapshot);
        REQUIRE(left);
        REQUIRE(static_cast<bool>(left->record));
        submit(std::move(left->record));
        CHECK(renderer->lastRecorded().pictureRecords == 2);
        CHECK(renderer->lastRecorded().rulerRecords == 4);
        CHECK(renderer->lastRecorded().rulerDispatched == zoomed_regions.bandRulers);
        const auto left_constants = rawreel::rulerConstants(snapshot.params.overlay, zoomed_view,
                                                            zoomed_regions, std::nullopt, skip);
        CHECK(rulerMismatches(read(left->image), extent, left_constants, false) == 0);
    }
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: a cursor move on a 4K output dispatches little") {
    // 3840 x 2160 with 60 px strips, a 1920 x 1080 picture at zoom 1: P = {60, 60, 3780, 2100},
    // H = {990, 570, 1920, 1080}. The whole bands hold about 45 % of the output; a cursor move
    // dispatches the two rulers, a few percent. The dispatched threads are counted by group.
    constexpr PixelSize CLIP{1920, 1080};
    const DisplayFrame g =
        displayFrame("synthetic:gradient?w=1920&h=1080&fmt=yuv420p&frames=1", CLIP);
    const PixelSize extent{3840, 2160};
    rawreel::OverlayParams overlay = testRulers(24);
    overlay.strips = {.top = 60, .left = 60};
    DisplaySnapshot snapshot{frameSet(g, 0, 1),
                             DisplayParams{.view = {ViewMode::Zoom, 1, {960, 540}, CLIP},
                                           .overlay = overlay,
                                           .cursor = dvv::media::PointD{1500.5, 1000.5}},
                             1};
    const auto renderer = makeRenderer(context(), 1);
    const auto threads = [](const std::array<dvv::media::PixelRect, 2>& rects) {
        std::uint64_t count = 0;
        for (const dvv::media::PixelRect& rect : rects) {
            const std::uint64_t groups_x = (rect.width + 7) / 8;
            const std::uint64_t groups_y = (rect.height + 7) / 8;
            count += groups_x * groups_y * 64;  // 8 x 8 threads per group
        }
        return count;
    };
    auto first =
        renderer->frame(frameParams(extent, dvv::gpu::PresentEncoding::Linear, 0, false), snapshot);
    REQUIRE_MESSAGE(first, (first ? std::string() : first.error().describe()));
    REQUIRE(static_cast<bool>(first->record));
    submit(std::move(first->record));
    REQUIRE(renderer->lastRecorded().rulers.hugged == dvv::media::PixelRect{990, 570, 1920, 1080});
    const std::uint64_t output = std::uint64_t{extent.width} * extent.height;
    CHECK(threads(renderer->lastRecorded().rulerDispatched) * 100 > output * 40);
    snapshot.params.cursor = dvv::media::PointD{1600.5, 1100.5};
    auto moved =
        renderer->frame(frameParams(extent, dvv::gpu::PresentEncoding::Linear, 1, true), snapshot);
    REQUIRE(moved);
    REQUIRE(static_cast<bool>(moved->record));
    submit(std::move(moved->record));
    const std::array<dvv::media::PixelRect, 2> rulers = renderer->lastRecorded().rulerDispatched;
    CHECK(rulers[0] == dvv::media::PixelRect{930, 510, 1980, 60});
    CHECK(rulers[1] == dvv::media::PixelRect{930, 570, 60, 1080});
    // 1984 x 64 + 64 x 1080 = 196096 threads, 2.4 % of the output.
    CHECK(threads(rulers) == 196096);
    CHECK(threads(rulers) * 100 < output * 3);
}
TEST_CASE_FIXTURE(CompositorTest, "compositor: an extent smaller than the strips") {
    // A fast window shrink. P is empty: no picture dispatch; the top band, clipped to the extent,
    // writes the whole output. At 8 x 8 both strips clamp to 8, so H = P = {8, 8, 0, 0}: the
    // top ruler has no pixel and the whole output is the corner. At 100 x 8 only the top strip
    // clamps: P = {30, 8, 70, 0}, the ruler spans x in [30, 100) with its separator on row 7,
    // the window's inner edge, and the corner x in [0, 30). A
    // 0-wide extent never reaches frame() (the presenter skips it and createImage refuses it);
    // pictureRegion's 0-wide case is dvv-media's test.
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame g = displayFrame(G);
    const DisplaySnapshot snapshot{frameSet(g, 0, 1), DisplayParams{.overlay = testRulers(4)}, 1};
    const PixelSize small{8, 8};
    auto shrunk =
        renderer->frame(frameParams(small, dvv::gpu::PresentEncoding::Linear, 0, true), snapshot);
    REQUIRE_MESSAGE(shrunk, (shrunk ? std::string() : shrunk.error().describe()));
    REQUIRE(static_cast<bool>(shrunk->record));
    // A ruler-only record sets the layout fields: a selftest waiting for this renderer's epoch
    // or the new extent does not wait forever. P is empty: no index, no view.
    const RecordedInfo info = renderer->lastRecorded();
    CHECK(info.epoch == 1);
    CHECK(info.extent == small);
    CHECK(info.picture == dvv::media::pictureRegion(small, snapshot.params.overlay.strips));
    CHECK(info.picture.empty());
    CHECK(info.index == -1);
    CHECK(info.zoom == 0.0);
    submit(std::move(shrunk->record));
    const auto constants = rawreel::rulerConstants(
        snapshot.params.overlay, resolveView(snapshot.params.view, {}, IMAGE),
        regionsOf(small, snapshot.params.overlay, resolveView(snapshot.params.view, {}, IMAGE)),
        std::nullopt, false);
    CHECK(constants[0].regionW * constants[0].regionH == 64);
    CHECK(constants[0].strip == 8);
    CHECK(constants[0].rulerEdge == 8);
    CHECK(constants[0].rulerBegin == 8);  // no ruler pixel: all corner
    CHECK(constants[0].rulerEnd == 8);
    CHECK(rulerMismatches(read(shrunk->image), small, constants, false) == 0);

    const PixelSize normal{211, 97};
    auto grown =
        renderer->frame(frameParams(normal, dvv::gpu::PresentEncoding::Linear, 1, true), snapshot);
    REQUIRE(grown);
    REQUIRE(static_cast<bool>(grown->record));
    submit(std::move(grown->record));
    CHECK(renderer->lastRecorded().pictureRecords == 1);
    CHECK(renderer->lastRecorded().rulerRecords == 2);
    CHECK(renderer->lastRecorded().picture == dvv::media::PixelRect{30, 30, 181, 67});

    // Wider than the left strip, shorter than the top one: the ruler at the inner edge.
    const auto wide_renderer = makeRenderer(context(), 1);
    const PixelSize wide{100, 8};
    auto flat = wide_renderer->frame(frameParams(wide, dvv::gpu::PresentEncoding::Linear, 0, true),
                                     snapshot);
    REQUIRE_MESSAGE(flat, (flat ? std::string() : flat.error().describe()));
    REQUIRE(static_cast<bool>(flat->record));
    submit(std::move(flat->record));
    const dvv::media::PixelRect wide_p =
        dvv::media::pictureRegion(wide, snapshot.params.overlay.strips);
    REQUIRE(wide_p == dvv::media::PixelRect{30, 8, 70, 0});
    const ViewTransform wide_view = resolveView(snapshot.params.view, wide_p.size(), IMAGE);
    const auto wide_constants = rawreel::rulerConstants(
        snapshot.params.overlay, wide_view, regionsOf(wide, snapshot.params.overlay, wide_view),
        std::nullopt, false);
    CHECK(wide_constants[0].rulerBegin == 30);
    CHECK(wide_constants[0].rulerEnd == 100);
    const std::vector<std::byte> wide_bytes = read(flat->image);
    CHECK(rulerMismatches(wide_bytes, wide, wide_constants, false) == 0);
    const Rgba8 separator{0x3d, 0x3d, 0x3d, 255};  // the dark theme's, no image: no tick
    CHECK(pixelAt(wide_bytes, wide, 50, 7) == separator);
    CHECK(pixelAt(wide_bytes, wide, 50, 6) != separator);
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: a new frame does not rebuild the line table") {
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame g = displayFrame(G);
    const PixelSize extent{211, 97};
    DisplaySnapshot snapshot{frameSet(g, 0, 1), DisplayParams{.overlay = testRulers(4)}, 1};
    std::uint64_t number = 0;
    const auto step = [&](std::uint64_t pictures, std::uint64_t lines) {
        auto frame = renderer->frame(
            frameParams(extent, dvv::gpu::PresentEncoding::Linear, number, number != 0), snapshot);
        ++number;
        REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
        REQUIRE(static_cast<bool>(frame->record));
        submit(std::move(frame->record));
        CHECK(renderer->lastRecorded().pictureRecords == pictures);
        CHECK(renderer->lastRecorded().linePasses == lines);
    };
    step(1, 1);
    for (std::uint64_t i = 2; i < 5; ++i) {  // playback: the same view, a new FrameSet
        snapshot =
            DisplaySnapshot{frameSet(g, static_cast<std::int64_t>(i), 1), snapshot.params, i};
        step(i, 1);
    }
    // A cursor move records the ruler pass alone, and a frame after it keeps the table.
    snapshot.params.cursor = dvv::media::PointD{100.5, 50.5};
    step(4, 1);
    snapshot.version = 5;
    step(5, 1);
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: the line table is rebuilt when its inputs change") {
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame g = displayFrame(G);
    PixelSize extent{211, 97};
    DisplaySnapshot snapshot{
        frameSet(g, 0, 1),
        DisplayParams{.view = {ViewMode::Zoom, 8, {18, 11}, IMAGE}, .overlay = testGrid(4, 2)}, 1};
    std::uint64_t number = 0;
    bool completed = false;
    // Each step shows a new FrameSet, so the picture pass records whenever P is not empty.
    const auto step = [&](std::uint64_t pictures, std::uint64_t lines) {
        auto frame = renderer->frame(
            frameParams(extent, dvv::gpu::PresentEncoding::Linear, number, completed), snapshot);
        ++number;
        completed = true;
        REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
        if (frame->record) {
            submit(std::move(frame->record));
        }
        CHECK(renderer->lastRecorded().pictureRecords == pictures);
        CHECK(renderer->lastRecorded().linePasses == lines);
        ++snapshot.version;
        return frame->image;
    };
    step(1, 1);
    step(2, 1);
    SUBCASE("the image size") {
        // At 500 % an image 42 wide is centred at x offset 0 and ends at column 210; one 44 wide
        // keeps the zoom, the centre and both offsets, so the second step changes the image size
        // alone. The line at x 42 lies at column 210: outside the narrow image, inside the wide.
        const PixelSize narrow_size{42, 23};
        const PixelSize wide_size{44, 23};
        const DisplayFrame narrow =
            displayFrame("synthetic:gradient?w=42&h=23&fmt=yuv420p&frames=1", narrow_size);
        const DisplayFrame wide =
            displayFrame("synthetic:gradient?w=44&h=23&fmt=yuv420p&frames=1", wide_size);
        snapshot.frames = frameSet(narrow, 1, 1);
        snapshot.params.view = {ViewMode::Zoom, 5, {21, 11}, narrow_size};
        step(3, 2);
        const ViewTransform narrow_view = resolveView(snapshot.params.view, extent, narrow_size);
        snapshot.frames = frameSet(wide, 2, 1);
        snapshot.params.view.image = wide_size;  // the zoom and the centre stay
        const ViewTransform wide_view = resolveView(snapshot.params.view, extent, wide_size);
        REQUIRE(wide_view.offset() == narrow_view.offset());
        REQUIRE(narrow_view.offset().x == 0);
        const auto image = step(4, 3);
        const CompositeConstants constants =
            rawreel::compositeConstants(snapshot.params, wide_view, {}, false);
        REQUIRE((constants.levelShape[2] & rawreel::LEVEL_DRAWN) != 0);
        const RegionMatch match = regionMatch(read(image), extent, constants, wide.image());
        CHECK(match.exact == std::size_t{extent.width} * extent.height);
        step(5, 3);
    }
    SUBCASE("the view") {
        snapshot.params.view.zoom = 6;  // the image stays wider and taller than P
        step(3, 2);
        snapshot.params.view.centre = {14, 9};
        step(4, 3);
        step(5, 3);
    }
    SUBCASE("the output") {
        extent = {200, 90};  // new targets
        step(3, 2);
        step(4, 2);  // the same size again
    }
    SUBCASE("a level's shape") {
        snapshot.params.overlay.levels[0].stepLog2 = 4;
        step(3, 2);
        snapshot.params.overlay.levels[0].thickness = 3;
        step(4, 3);
        snapshot.params.overlay.levels[0].halo = false;
        step(5, 4);
        snapshot.params.overlay.levels[2].shown = false;  // level 2 no longer drawn
        step(6, 5);
        step(7, 5);
    }
    SUBCASE("the grid off, then on") {
        snapshot.params.overlay.levels[0].shown = false;
        step(3, 1);
        snapshot.params.overlay.levels[0].shown = true;
        step(4, 2);
        step(5, 2);
    }
    SUBCASE("a dropped frame") {
        completed = false;
        step(3, 2);
        step(4, 2);
    }
    SUBCASE("a FrameSet of another epoch") {
        snapshot.frames = frameSet(g, 1, 2);  // the background only
        step(3, 1);
        snapshot.frames = frameSet(g, 2, 1);
        step(4, 2);
    }
    SUBCASE("an empty P") {
        snapshot.params.overlay.strips = {.top = 200, .left = 0};
        step(2, 1);
        snapshot.params.overlay.strips = {};
        step(3, 2);
    }
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: a colour or opacity change keeps the line table") {
    const DisplayFrame g =
        displayFrame("synthetic:gradient?w=64&h=48&fmt=yuv420p&frames=1", {64, 48});
    const PixelSize p{256, 192};
    for (const auto encoding :
         {dvv::gpu::PresentEncoding::Linear, dvv::gpu::PresentEncoding::Srgb}) {
        const bool skip = encoding == dvv::gpu::PresentEncoding::Srgb;
        CAPTURE(skip);
        const auto renderer = makeRenderer(context(), 1);
        DisplaySnapshot snapshot{frameSet(g, 0, 1),
                                 DisplayParams{.view = {ViewMode::Zoom, 4, {32, 24}, {64, 48}},
                                               .overlay = testGrid(24, 4)},
                                 1};
        std::uint64_t number = 0;
        const auto step = [&](std::uint64_t pictures, std::uint64_t lines) {
            auto frame = renderer->frame(frameParams(p, encoding, number, number != 0), snapshot);
            ++number;
            ++snapshot.version;
            REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
            REQUIRE(static_cast<bool>(frame->record));
            submit(std::move(frame->record));
            CHECK(renderer->lastRecorded().pictureRecords == pictures);
            CHECK(renderer->lastRecorded().linePasses == lines);
            const ViewTransform view = resolveView(snapshot.params.view, p, {64, 48});
            const CompositeConstants constants =
                rawreel::compositeConstants(snapshot.params, view, {}, skip);
            REQUIRE((constants.levelShape[2] & rawreel::LEVEL_DRAWN) != 0);
            const RegionMatch match = regionMatch(read(frame->image), p, constants, g.image());
            CHECK(match.exact == std::size_t{p.width} * p.height);
        };
        step(1, 1);
        snapshot.params.overlay.levels[0].colour = 0xD9C0402AU;  // the colour
        step(2, 1);
        snapshot.params.overlay.levels[1].colour = 0x20FFFFFFU;  // the opacity
        step(3, 1);
        snapshot.params.overlay.levels[1].adaptive = false;
        step(4, 1);
    }
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: the cached line table draws the same pixels") {
    const DisplayFrame g =
        displayFrame("synthetic:gradient?w=64&h=48&fmt=yuv420p&frames=1", {64, 48});
    const PixelSize p{128, 96};  // a quarter of the image at 400 %: the view pans
    for (const auto encoding :
         {dvv::gpu::PresentEncoding::Linear, dvv::gpu::PresentEncoding::Srgb}) {
        const bool skip = encoding == dvv::gpu::PresentEncoding::Srgb;
        CAPTURE(skip);
        const auto renderer = makeRenderer(context(), 1);
        DisplaySnapshot snapshot{frameSet(g, 0, 1),
                                 DisplayParams{.view = {ViewMode::Zoom, 4, {32, 24}, {64, 48}},
                                               .overlay = testGrid(24, 4)},
                                 1};
        std::uint64_t number = 0;
        const auto step = [&](std::uint64_t lines) {
            auto frame = renderer->frame(frameParams(p, encoding, number, number != 0), snapshot);
            ++number;
            ++snapshot.version;
            REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
            REQUIRE(static_cast<bool>(frame->record));
            submit(std::move(frame->record));
            CHECK(renderer->lastRecorded().pictureRecords == number);
            CHECK(renderer->lastRecorded().linePasses == lines);
            const ViewTransform view = resolveView(snapshot.params.view, p, {64, 48});
            const CompositeConstants constants =
                rawreel::compositeConstants(snapshot.params, view, {}, skip);
            const RegionMatch match = regionMatch(read(frame->image), p, constants, g.image());
            CHECK(match.exact == std::size_t{p.width} * p.height);
        };
        step(1);
        snapshot.params.view.centre = {28, 21};  // panned by 16 and 12 backing pixels
        step(2);
        step(2);  // a new frame: the cached table of the panned view
        snapshot.params.view.centre = {32, 24};
        step(3);
        step(3);
    }
}

namespace {

// A strip of ten 3-pixel cells, 5 rows, its coverage a pattern of `seed`.
std::shared_ptr<const dvv::gpu::GlyphStrip> syntheticStrip(std::uint8_t seed) {
    auto strip = std::make_shared<dvv::gpu::GlyphStrip>();
    strip->width = 30;
    strip->height = 5;
    strip->advance = 3;
    strip->ascent = 4;
    strip->descent = 1;
    strip->coverage.resize(std::size_t{strip->width} * strip->height);
    for (std::size_t i = 0; i < strip->coverage.size(); ++i) {
        strip->coverage[i] = static_cast<std::uint8_t>((i * 7) + seed);
    }
    return strip;
}

// The coverage of `strip` in every channel of the RGBA8 `bytes`.
bool holdsStrip(const std::vector<std::byte>& bytes, const dvv::gpu::GlyphStrip& strip) {
    if (bytes.size() != strip.coverage.size() * 4) {
        return false;
    }
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (std::to_integer<std::uint8_t>(bytes[i]) != strip.coverage[i / 4]) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE_FIXTURE(CompositorTest, "compositor: glyph strip uploads") {
    const auto renderer = makeRenderer(context(), 1);
    const DisplayFrame g = displayFrame(G);
    const PixelSize extent{211, 97};
    DisplaySnapshot snapshot{frameSet(g, 0, 1), DisplayParams{.overlay = testRulers(4)}, 1};
    snapshot.params.overlay.numbers = true;
    snapshot.params.overlay.glyphs = syntheticStrip(0);
    std::uint64_t number = 0;
    const auto next = [&](PixelSize size, bool completed) {
        auto frame = renderer->frame(
            frameParams(size, dvv::gpu::PresentEncoding::Linear, number++, completed), snapshot);
        REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
        return std::move(*frame);
    };
    const auto bound = [&] {
        REQUIRE(renderer->glyphImage() != nullptr);
        CHECK(renderer->glyphImage()->width() == 30);
        CHECK(renderer->glyphImage()->height() == 5);
        CHECK(dvv::gpu::vulkanFormat(*renderer->glyphImage()) == VK_FORMAT_R8G8B8A8_UNORM);
        return read(renderer->glyphImage());
    };

    // The first frame: one upload, both passes.
    dvv::gpu::PresentFrame first = next(extent, false);
    REQUIRE(static_cast<bool>(first.record));
    submit(std::move(first.record));
    CHECK(renderer->lastRecorded().glyphUploads == 1);
    CHECK(renderer->lastRecorded().rulerNumbers);
    CHECK(renderer->lastRecorded().rulerRecords == 1);
    CHECK(renderer->lastRecorded().pictureRecords == 1);
    CHECK(holdsStrip(bound(), *snapshot.params.overlay.glyphs));
    // The same strip again: nothing records.
    CHECK_FALSE(static_cast<bool>(next(extent, true).record));
    CHECK(renderer->lastRecorded().glyphUploads == 1);

    // A new strip (a display scale change): one upload, a ruler record, no picture record.
    snapshot.params.overlay.glyphs = syntheticStrip(1);
    dvv::gpu::PresentFrame scaled = next(extent, true);
    REQUIRE(static_cast<bool>(scaled.record));
    submit(std::move(scaled.record));
    CHECK(renderer->lastRecorded().glyphUploads == 2);
    CHECK(renderer->lastRecorded().rulerRecords == 2);
    CHECK(renderer->lastRecorded().pictureRecords == 1);
    CHECK(holdsStrip(bound(), *snapshot.params.overlay.glyphs));

    // A strip change whose frame is dropped (its record never runs): the next frame uploads again.
    snapshot.params.overlay.glyphs = syntheticStrip(2);
    REQUIRE(static_cast<bool>(next(extent, true).record));  // dropped unrun
    CHECK(renderer->lastRecorded().glyphUploads == 3);
    dvv::gpu::PresentFrame again = next(extent, false);
    REQUIRE(static_cast<bool>(again.record));
    submit(std::move(again.record));
    CHECK(renderer->lastRecorded().glyphUploads == 4);
    CHECK(holdsStrip(bound(), *snapshot.params.overlay.glyphs));

    // A cursor move and a new extent with the same strip: no upload.
    snapshot.params.cursor = dvv::media::PointD{80.5, 50.5};
    dvv::gpu::PresentFrame moved = next(extent, true);
    REQUIRE(static_cast<bool>(moved.record));
    submit(std::move(moved.record));
    CHECK(renderer->lastRecorded().rulerRecords == 5);
    CHECK(renderer->lastRecorded().glyphUploads == 4);
    dvv::gpu::PresentFrame resized = next({300, 120}, true);
    REQUIRE(static_cast<bool>(resized.record));
    submit(std::move(resized.record));
    CHECK(renderer->lastRecorded().glyphUploads == 4);

    // A strip change while no band shows numbers (an extent smaller than the strips: no picture,
    // no image on the rulers), then a larger extent: one upload, in the larger frame.
    snapshot.params.overlay.glyphs = syntheticStrip(3);
    dvv::gpu::PresentFrame small = next({8, 8}, true);
    REQUIRE(static_cast<bool>(small.record));
    submit(std::move(small.record));
    CHECK(renderer->lastRecorded().glyphUploads == 4);
    dvv::gpu::PresentFrame grown = next(extent, true);
    REQUIRE(static_cast<bool>(grown.record));
    submit(std::move(grown.record));
    CHECK(renderer->lastRecorded().glyphUploads == 5);
    CHECK(holdsStrip(bound(), *snapshot.params.overlay.glyphs));

    // Numbers off: the glyph dummy is bound, the strip released, nothing uploaded.
    snapshot.params.overlay.numbers = false;
    dvv::gpu::PresentFrame off = next(extent, true);
    REQUIRE(static_cast<bool>(off.record));
    submit(std::move(off.record));
    CHECK(renderer->lastRecorded().glyphUploads == 5);
    CHECK(renderer->glyphImage() == nullptr);
    const auto fresh = makeRenderer(context(), 1);
    auto unnumbered =
        fresh->frame(frameParams(extent, dvv::gpu::PresentEncoding::Linear, 0, false), snapshot);
    REQUIRE(unnumbered);
    REQUIRE(static_cast<bool>(unnumbered->record));
    submit(std::move(unnumbered->record));
    CHECK(fresh->lastRecorded().glyphUploads == 0);
    CHECK(fresh->glyphImage() == nullptr);
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: a glyph strip that fails to upload") {
    // An empty strip is refused: the rulers draw without numbers, with the glyph dummy bound, and
    // the failure is logged once for that strip. The same strip is not tried again; another one is.
    const auto warnings = std::make_shared<dvv::MemorySink>();
    dvv::Logger::instance().addSink(warnings, dvv::LogLevel::Warn);
    const DisplayFrame g = displayFrame(G);
    const PixelSize extent{211, 97};
    DisplaySnapshot snapshot{frameSet(g, 0, 1), DisplayParams{.overlay = testRulers(4)}, 1};
    snapshot.params.overlay.numbers = true;
    snapshot.params.overlay.glyphs = std::make_shared<const dvv::gpu::GlyphStrip>();
    // A fresh renderer's frame of `params` at the last cursor below, read back.
    const auto drawn = [&](const DisplayParams& params) {
        const auto fresh = makeRenderer(context(), 1);
        auto frame = fresh->frame(frameParams(extent, dvv::gpu::PresentEncoding::Linear, 0, false),
                                  DisplaySnapshot{frameSet(g, 0, 1), params, 1});
        REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
        REQUIRE(static_cast<bool>(frame->record));
        submit(std::move(frame->record));
        return read(frame->image);
    };
    const auto renderer = makeRenderer(context(), 1);
    std::vector<std::byte> last;
    for (std::uint64_t i = 0; i < 2; ++i) {
        snapshot.params.cursor = dvv::media::PointD{80.5 + (10.0 * static_cast<double>(i)), 50.5};
        auto frame = renderer->frame(
            frameParams(extent, dvv::gpu::PresentEncoding::Linear, i, i != 0), snapshot);
        REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
        REQUIRE(static_cast<bool>(frame->record));
        submit(std::move(frame->record));
        CHECK(renderer->lastRecorded().rulerRecords == i + 1);
        CHECK(renderer->lastRecorded().glyphUploads == 0);
        CHECK(renderer->lastRecorded().glyphFailures == 1);  // the cursor move does not retry
        CHECK_FALSE(renderer->lastRecorded().rulerNumbers);  // the numbers cleared
        CHECK(renderer->glyphImage() == nullptr);
        last = read(frame->image);
    }
    // The numbers cleared: the pixels of a frame with the numbers off, which a strip changes.
    DisplayParams plain = snapshot.params;
    plain.overlay.numbers = false;
    CHECK(last == drawn(plain));
    DisplayParams labelled = snapshot.params;
    labelled.overlay.glyphs = syntheticStrip(0);
    CHECK(last != drawn(labelled));
    // Another strip that fails: tried, and warned, once.
    snapshot.params.overlay.glyphs = std::make_shared<const dvv::gpu::GlyphStrip>();
    for (std::uint64_t i = 2; i < 4; ++i) {
        auto frame = renderer->frame(
            frameParams(extent, dvv::gpu::PresentEncoding::Linear, i, true), snapshot);
        REQUIRE(frame);
        if (frame->record) {
            submit(std::move(frame->record));
        }
        CHECK(renderer->lastRecorded().glyphFailures == 2);
        CHECK(renderer->glyphImage() == nullptr);
    }
    dvv::Logger::instance().flush();
    dvv::Logger::instance().setSinkLevel(*warnings, dvv::LogLevel::Off);
    CHECK(std::ranges::count_if(warnings->lines(), [](const std::string& line) {
              return line.find("glyph strip") != std::string::npos;
          }) == 2);
}

TEST_CASE_FIXTURE(CompositorTest, "compositor: a dropped glyph upload is never bound") {
    // A strip change whose frame is dropped (its upload never runs), then a frame after it that
    // binds no strip: the rulers hidden, no image on them, or another strip that fails to upload
    // (the glyph dummy bound). With the strip back, it is uploaded again, not bound unfilled.
    const DisplayFrame g = displayFrame(G);
    const PixelSize extent{211, 97};
    for (const std::string_view between : {"hidden", "no image", "failed upload"}) {
        CAPTURE(between);
        const auto renderer = makeRenderer(context(), 1);
        DisplaySnapshot snapshot{frameSet(g, 0, 1), DisplayParams{.overlay = testRulers(4)}, 1};
        snapshot.params.overlay.numbers = true;
        snapshot.params.overlay.glyphs = syntheticStrip(0);
        std::uint64_t number = 0;
        const auto next = [&](bool completed) {
            auto frame = renderer->frame(
                frameParams(extent, dvv::gpu::PresentEncoding::Linear, number++, completed),
                snapshot);
            REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
            return std::move(*frame);
        };
        dvv::gpu::PresentFrame first = next(false);
        REQUIRE(static_cast<bool>(first.record));
        submit(std::move(first.record));
        const std::shared_ptr<const dvv::gpu::GlyphStrip> strip = syntheticStrip(1);
        snapshot.params.overlay.glyphs = strip;
        REQUIRE(static_cast<bool>(next(true).record));  // dropped unrun
        CHECK(renderer->lastRecorded().glyphUploads == 2);

        if (between == "hidden") {
            snapshot.params.overlay.strips = {};
        } else if (between == "no image") {
            snapshot.frames = FrameSet{};
        } else {
            snapshot.params.overlay.glyphs = std::make_shared<const dvv::gpu::GlyphStrip>();
        }
        dvv::gpu::PresentFrame after = next(false);
        REQUIRE(static_cast<bool>(after.record));
        submit(std::move(after.record));
        CHECK(renderer->lastRecorded().glyphUploads == 2);
        CHECK(renderer->glyphImage() == nullptr);

        snapshot.frames = frameSet(g, 0, 1);
        snapshot.params.overlay.strips = testRulers(4).strips;
        snapshot.params.overlay.glyphs = strip;
        dvv::gpu::PresentFrame back = next(true);
        REQUIRE(static_cast<bool>(back.record));
        submit(std::move(back.record));
        CHECK(renderer->lastRecorded().glyphUploads == 3);
        REQUIRE(renderer->glyphImage() != nullptr);
        CHECK(holdsStrip(read(renderer->glyphImage()), *strip));
    }
}

namespace {

// The band pixels of `bytes` that differ from rulerPixel with `glyphs`, and those that the labels
// change (rulerPixel with and without `glyphs` differ). A channel may be one step off, as in
// withinOne, at a text pixel (one where the reference with the strip all clear and all covered
// differs) and at a pixel the labels leave as the ruler pass draws it (rulerMismatches allows the
// same): the GPU may round a blend, of the text over its box or of the cursor marker, the other
// way (NVIDIA rounds it one step lower). Tag fills and box edges stay exact. With `channel` (the
// sRGB encode, whose pow decode may not be correctly rounded) every pixel may be one step off. With
// `placement` (a zoom that is not dyadic, where a divide that is not correctly rounded may move a
// boundary) a pixel that matches the reference of a neighbour on the band's axis counts as a match.
// The coverage rows differ by 5 per row, 11 per column and 37 per cell before the blend, so a wrong
// row, column or digit, or a text a pixel off, still shows; a box edge is no text pixel and stays
// exact. `first` names the first wrong pixel; `rounded` counts the pixels that match only within
// one step, `shifted` those that match only a neighbour's reference.
struct LabelTolerance {
    bool channel = false;
    bool placement = false;
};
struct LabelCount {
    std::size_t wrong = 0;
    std::size_t rounded = 0;
    std::size_t shifted = 0;
    std::size_t labelled = 0;
    std::string first;
};
LabelCount labelMismatches(const std::vector<std::byte>& bytes, PixelSize extent,
                           const std::array<rawreel::RulerConstants, 2>& bands,
                           const dvv::gpu::GlyphStrip& glyphs, LabelTolerance tolerance) {
    const auto hex = [](const Rgba8& c) {
        return std::format("{:02x}{:02x}{:02x}{:02x}", c[0], c[1], c[2], c[3]);
    };
    dvv::gpu::GlyphStrip clear = glyphs;
    std::ranges::fill(clear.coverage, std::uint8_t{0});
    dvv::gpu::GlyphStrip covered = glyphs;
    std::ranges::fill(covered.coverage, std::uint8_t{255});
    enum class Match : std::uint8_t { None, WithinOne, Exact };
    LabelCount count;
    for (const rawreel::RulerConstants& band : bands) {
        const bool top = band.axis == 0;
        const std::uint32_t begin = top ? band.regionX : band.regionY;
        const std::uint32_t end = top ? band.regionX + band.regionW : band.regionY + band.regionH;
        const auto step_allowed = [&](std::uint32_t rx, std::uint32_t ry, const Rgba8& want) {
            return tolerance.channel || rawreel::rulerPixel(band, nullptr, rx, ry) == want ||
                   rawreel::rulerPixel(band, &clear, rx, ry) !=
                       rawreel::rulerPixel(band, &covered, rx, ry);
        };
        for (std::uint32_t y = band.regionY; y < band.regionY + band.regionH; ++y) {
            for (std::uint32_t x = band.regionX; x < band.regionX + band.regionW; ++x) {
                const Rgba8 gpu = pixelAt(bytes, extent, x, y);
                const auto near = [&](std::uint32_t rx, std::uint32_t ry) {
                    const Rgba8 want = rawreel::rulerPixel(band, &glyphs, rx, ry);
                    if (gpu == want) {
                        return Match::Exact;
                    }
                    return withinOne(gpu, want) && step_allowed(rx, ry, want) ? Match::WithinOne
                                                                              : Match::None;
                };
                const Rgba8 expected = rawreel::rulerPixel(band, &glyphs, x, y);
                const std::uint32_t along = top ? x : y;
                const Match here = near(x, y);
                const bool shifted =
                    here == Match::None && tolerance.placement &&
                    ((along > begin && near(top ? x - 1 : x, top ? y : y - 1) != Match::None) ||
                     (along + 1 < end && near(top ? x + 1 : x, top ? y : y + 1) != Match::None));
                count.shifted += shifted ? 1 : 0;
                count.rounded += here == Match::WithinOne ? 1 : 0;
                if (here == Match::None && !shifted && count.wrong++ == 0) {
                    count.first = std::format("({}, {}): GPU {}, reference {}", x, y, hex(gpu),
                                              hex(expected));
                }
                count.labelled += rawreel::rulerPixel(band, nullptr, x, y) == expected ? 0 : 1;
            }
        }
    }
    return count;
}

// The kinds of label rulerLabels() gives the band `constants` drawn with cells `advance` wide:
// bit 1 << kind for each, and LABEL_CLIPPED for a box that reaches past the ruler's end.
constexpr unsigned LABEL_CLIPPED = 1U << 4U;
constexpr unsigned kindBit(rawreel::RulerLabelKind kind) {
    return 1U << static_cast<unsigned>(kind);
}
unsigned labelKinds(const rawreel::RulerConstants& constants, std::uint32_t advance) {
    unsigned kinds = 0;
    for (const rawreel::RulerLabel& label :
         rawreel::rulerLabels(rawreel::rulerLabelInput(constants, advance))) {
        kinds |= kindBit(label.kind);
        if (label.boxEnd > static_cast<std::int32_t>(constants.rulerEnd)) {
            kinds |= LABEL_CLIPPED;
        }
    }
    return kinds;
}

}  // namespace

TEST_CASE_FIXTURE(CompositorTest, "compositor: ruler numbers match the reference") {
    const auto draw = [this](PixelSize extent, const FrameSet& frames, const DisplayParams& params,
                             dvv::gpu::PresentEncoding encoding) {
        const auto renderer = makeRenderer(context(), 1);
        auto frame = renderer->frame(frameParams(extent, encoding, 0, false),
                                     DisplaySnapshot{frames.share(), params, 1});
        REQUIRE_MESSAGE(frame, (frame ? std::string() : frame.error().describe()));
        REQUIRE(static_cast<bool>(frame->record));
        submit(std::move(frame->record));
        return read(frame->image);
    };
    // The bands as frame() builds them: the cursor mapped in P.
    const auto bands = [](PixelSize extent, const DisplayParams& params, PixelSize image,
                          bool skip) {
        const dvv::media::PixelRect p = dvv::media::pictureRegion(extent, params.overlay.strips);
        const ViewTransform view = resolveView(params.view, p.size(), image);
        const std::optional<dvv::media::ImagePixel> cursor =
            dvv::media::imagePixelAt(view, {params.cursor->x - p.x, params.cursor->y - p.y});
        REQUIRE(cursor.has_value());
        return rawreel::rulerConstants(params.overlay, view,
                                       regionsOf(extent, params.overlay, view), cursor, skip);
    };
    struct Scene {
        const char* name;
        std::string_view uri;
        PixelSize image;
        double zoom;
        dvv::media::PointD cursor;
        bool ticks;
        std::array<unsigned, 2> kinds;  // labelKinds() of the top and the left band
        std::uint32_t glyphHeight;      // the strip's rows; the label band holds 10
    };
    // Exact at the dyadic zooms (k / 2^n): at 0.5 and 0.25 every u(p) is exact, at 3 it lies at
    // least 1/6 from an integer. At another zoom a divide that is not correctly rounded may move
    // a boundary by a pixel.
    const auto dyadic = [](double zoom) {
        const double scaled = std::ldexp(zoom, 16);
        return scaled == std::trunc(scaled);
    };
    constexpr unsigned NUMBER = kindBit(rawreel::RulerLabelKind::Number);
    constexpr unsigned FIRST = kindBit(rawreel::RulerLabelKind::FirstTag);
    constexpr unsigned LAST = kindBit(rawreel::RulerLabelKind::LastTag);
    constexpr unsigned CURSOR = kindBit(rawreel::RulerLabelKind::CursorTag);
    // P = 256 x 192 in a 286 x 222 output; at zoom 3 the 64 x 48 image is 192 x 144 from P's
    // (32, 24), so output (62, 54); at 0.37 the 1920 x 1080 one is 710 x 400 in P = 908 x 511,
    // with a major spacing of 8 (32 x 0.37 is below 24). The short rulers, with tags 17 ("0"
    // to "9") and 22 ("10" to "99") long and a margin of 4: at zoom 0.5 the image is 32 x 24
    // from output (142, 114), so R - L is 24 on the top (the first tag fits, the pair does not,
    // the last tag alone would) and 16 on the left (no end tag; the cursor tag starts at L); the
    // cursor tag of pixel 1 drops the first tag, and the last stays undrawn. At zoom 0.25 the
    // image is 16 x 12 from (150, 120): both rulers are shorter than the cursor tag, which starts
    // at L and is clipped at the ruler's end. The 15-row strip is taller than the 10-row label
    // band: its text starts 2 rows (the signed half of 10 - 15) before the band, past the ruler's
    // outer edge, and reaches 3 rows into the tick band, where the ticks must stay.
    const std::array scenes{
        Scene{"zoom 3",
              "synthetic:gradient?w=64&h=48&fmt=yuv420p&frames=1",
              {64, 48},
              3,
              {150.5, 100.5},
              true,
              {FIRST | LAST | CURSOR, NUMBER | FIRST | LAST | CURSOR},
              7},
        Scene{"zoom 3, a strip taller than the label band",
              "synthetic:gradient?w=64&h=48&fmt=yuv420p&frames=1",
              {64, 48},
              3,
              {150.5, 100.5},
              true,
              {FIRST | LAST | CURSOR, NUMBER | FIRST | LAST | CURSOR},
              15},
        Scene{"zoom 3, the cursor tag at the ends",
              "synthetic:gradient?w=64&h=48&fmt=yuv420p&frames=1",
              {64, 48},
              3,
              {62.5, 196.5},
              true,
              {NUMBER | LAST | CURSOR, NUMBER | FIRST | CURSOR},
              7},
        Scene{"zoom 0.37",
              "synthetic:gradient?w=1920&h=1080&fmt=yuv420p&frames=1",
              {1920, 1080},
              0.37,
              {500.5, 300.5},
              true,
              {NUMBER | FIRST | LAST | CURSOR, NUMBER | FIRST | LAST | CURSOR},
              7},
        Scene{"a disabled ruler: tags only",
              "synthetic:gradient?w=64&h=48&fmt=yuv420p&frames=1",
              {64, 48},
              3,
              {150.5, 100.5},
              false,
              {FIRST | LAST | CURSOR, FIRST | LAST | CURSOR},
              7},
        Scene{"zoom 0.5, short rulers",
              "synthetic:gradient?w=64&h=48&fmt=yuv420p&frames=1",
              {64, 48},
              0.5,
              {142.5, 114.5},
              true,
              {CURSOR, CURSOR},
              7},
        Scene{"zoom 0.25, rulers shorter than the cursor tag",
              "synthetic:gradient?w=64&h=48&fmt=yuv420p&frames=1",
              {64, 48},
              0.25,
              {150.5, 120.5},
              true,
              {CURSOR | LABEL_CLIPPED, CURSOR | LABEL_CLIPPED},
              7},
    };
    for (const Scene& scene : scenes) {
        const std::string name{scene.name};
        CAPTURE(name);
        const std::shared_ptr<const dvv::gpu::GlyphStrip> strip =
            rawreel::labelStrip(5, scene.glyphHeight);
        const DisplayFrame g = displayFrame(scene.uri, scene.image);
        const PixelSize extent =
            scene.image.width == 64 ? PixelSize{286, 222} : PixelSize{938, 541};
        for (const auto encoding :
             {dvv::gpu::PresentEncoding::Linear, dvv::gpu::PresentEncoding::Srgb}) {
            for (const bool dark : {false, true}) {
                for (const bool edge : {false, true}) {
                    const bool skip = encoding == dvv::gpu::PresentEncoding::Srgb;
                    CAPTURE(skip);
                    CAPTURE(dark);
                    CAPTURE(edge);
                    DisplayParams params{
                        .view = {ViewMode::Zoom,
                                 scene.zoom,
                                 {scene.image.width / 2.0, scene.image.height / 2.0},
                                 scene.image},
                        .overlay = testRulers(scene.zoom < 1 ? 8.0F : 24.0F),
                        .cursor = scene.cursor};
                    params.overlay.dark = dark;
                    params.overlay.numbers = true;
                    params.overlay.glyphs = strip;
                    params.overlay.labelSpacing = {
                        .numberGap = 4, .tagPad = 6, .tagInset = 2, .tagMargin = 4};
                    params.overlay.lastValue =
                        edge ? rawreel::LastValue::Edge : rawreel::LastValue::Pixel;
                    params.overlay.cursorTagTextBlack = dark;
                    if (!scene.ticks) {
                        params.overlay.levels[0].minimumSpacing = 100000;
                    }
                    const auto constants = bands(extent, params, scene.image, skip);
                    for (const rawreel::RulerConstants& band : constants) {
                        REQUIRE((band.flags & rawreel::RULER_NUMBERS) != 0);
                        REQUIRE(band.labelBand == 10);
                        REQUIRE(((band.flags & rawreel::RULER_TICKS) != 0) == scene.ticks);
                    }
                    // The scene still shows the labels it is for.
                    REQUIRE(labelKinds(constants[0], strip->advance) == scene.kinds[0]);
                    REQUIRE(labelKinds(constants[1], strip->advance) == scene.kinds[1]);
                    const std::vector<std::byte> bytes =
                        draw(extent, frameSet(g, 0, 1), params, encoding);
                    const LabelCount count =
                        labelMismatches(bytes, extent, constants, *strip,
                                        {.channel = skip, .placement = !dyadic(scene.zoom)});
                    CHECK(count.labelled > 0);
                    CHECK_MESSAGE(count.wrong == 0, count.first);
                    MESSAGE(count.rounded << " pixels match only within one step");
                    // A moved boundary shifts at most the two edges of a box, one pixel per
                    // row across the band; a label drawn a pixel off as a whole shifts more.
                    std::size_t bound = 0;
                    for (const rawreel::RulerConstants& band : constants) {
                        bound +=
                            rawreel::rulerLabels(rawreel::rulerLabelInput(band, strip->advance))
                                .size() *
                            2 * (band.axis == 0 ? band.regionH : band.regionW);
                    }
                    MESSAGE(count.shifted << " pixels match only a neighbour (at most " << bound
                                          << ")");
                    CHECK(count.shifted <= bound);
                }
            }
        }
    }
}

// Wall time per frame, recorded, submitted and waited for (dvv-gpu has no GPU timestamps), at 100 %
// of a 4K picture with every grid level drawn (the major one with its halo). Each number also holds
// the submit-and-wait round trip, measured alone as "empty". "full": a new frame in the same view
// (the line table read in place), the picture and the ruler passes; "cursor-only": the cursor
// moved, the ruler pass alone; "cursor-only, numbers": the same with the ruler numbers on, as on a
// display of backing scale 2 with the system font at 11 pt (the OS's glyphs, the strips from
// rulerStrip). The grid's own cost is measured in pairs below. The case prints its table to
// stdout (MESSAGE shows only with a failure); run it with ctest -V.
TEST_CASE_FIXTURE(CompositorTest, "compositor: frame cost (manual)") {
    if (dvv::getEnvironment("RAWREEL_MEASURE_FRAMES").value_or("") != "1") {
        MESSAGE("skipped: set RAWREEL_MEASURE_FRAMES=1");
        return;
    }
    constexpr PixelSize PICTURE{3840, 2160};
    const DisplayFrame g =
        displayFrame("synthetic:gradient?w=3840&h=2160&fmt=yuv420p&frames=1", PICTURE);
    constexpr std::size_t WARM_UP = 20;
    constexpr std::size_t SAMPLES = 200;
    const auto median_p95 = [](std::vector<double> ms) {
        std::ranges::sort(ms);
        return std::array<double, 2>{ms[ms.size() / 2], ms[(ms.size() * 95) / 100]};
    };
    // The spacings at 100 %: 32, 8 and 2 backing pixels, each at or above its minimum.
    rawreel::OverlayParams grid = testGrid(4, 2);
    grid.levels[2].minimumSpacing = 2;
    rawreel::OverlayParams rulers = grid;
    rulers.strips = {.top = 30, .left = 30};
    rulers.tickBand = 20;
    rulers.tickWidth = 2;
    rulers.markerDepth = 6;
    rulers.litDepth = 28;
    rulers.tickLength = {20, 12, 6};
    constexpr double SCALE = 2;
    constexpr double FONT_POINTS = 11;
    // The DIP per font point of native_policy::dipPerFontPoint(): 1 on macOS, 96 / 72 on Windows
    // and GTK, so the strips and glyphs are the app's on each OS.
#if defined(__APPLE__)
    constexpr double DIP_PER_POINT = 1;
#else
    constexpr double DIP_PER_POINT = 96.0 / 72.0;
#endif
    dvv::Result<dvv::gpu::GlyphStrip> glyphs = dvv::gpu::rasterGlyphStrip(
        {.family = "", .sizePoints = FONT_POINTS}, DIP_PER_POINT * SCALE, rawreel::RULER_DIGITS);
    REQUIRE_MESSAGE(glyphs, (glyphs ? std::string() : glyphs.error().describe()));
    rawreel::RulerConfig shown;
    shown.show = true;
    shown.numbers = true;
    const rawreel::RulerStrip strip = rawreel::rulerStrip(
        shown, FONT_POINTS * DIP_PER_POINT, {.backingPerDip = SCALE, .contentScale = SCALE});
    REQUIRE(strip.tickBand == rulers.tickBand);
    rawreel::OverlayParams numbers = rulers;
    numbers.numbers = true;
    numbers.strips = {.top = strip.backing, .left = strip.backing};
    numbers.glyphs = std::make_shared<const dvv::gpu::GlyphStrip>(std::move(*glyphs));
    numbers.labelSpacing = {.numberGap = 4, .tagPad = 6, .tagInset = 2, .tagMargin = 4};
    struct Cost {
        std::array<double, 2> ms;
        double picturePasses;  // per measured frame
        double rulerPasses;
    };
    std::println("frame cost: wall ms per submitted-and-waited frame, {} samples each", SAMPLES);
    for (const PixelSize extent : {PixelSize{1920, 1080}, PixelSize{3840, 2160}}) {
        const auto renderer = makeRenderer(context(), 1);
        const dvv::media::ViewRequest view{ViewMode::Zoom, 1, {1920, 1080}, PICTURE};
        const CompositeConstants constants = rawreel::compositeConstants(
            rawreel::DisplayParams{.overlay = grid}, resolveView(view, extent, PICTURE), {}, false);
        for (std::size_t l = 0; l < rawreel::GRID_LEVELS; ++l) {
            REQUIRE((constants.levelShape[l] & rawreel::LEVEL_DRAWN) != 0);
        }
        std::uint64_t version = 0;
        std::uint64_t number = 0;
        const auto measure = [&](const rawreel::OverlayParams& overlay, bool new_frame) {
            DisplaySnapshot snapshot{frameSet(g, 0, 1),
                                     DisplayParams{.view = view, .overlay = overlay}, ++version};
            std::vector<double> ms;
            std::uint64_t pictures = 0;
            std::uint64_t bands = 0;
            for (std::size_t i = 0; i < WARM_UP + SAMPLES; ++i) {
                if (new_frame) {
                    snapshot.version = ++version;
                }
                snapshot.params.cursor =
                    dvv::media::PointD{100.5 + (7.0 * static_cast<double>(i % 100)), 200.5};
                const RecordedInfo before = renderer->lastRecorded();
                const auto start = std::chrono::steady_clock::now();
                auto frame = renderer->frame(
                    frameParams(extent, dvv::gpu::PresentEncoding::Linear, number++, true),
                    snapshot);
                REQUIRE(frame);
                REQUIRE(static_cast<bool>(frame->record));
                submit(std::move(frame->record));
                const auto stop = std::chrono::steady_clock::now();
                if (i >= WARM_UP) {
                    const RecordedInfo after = renderer->lastRecorded();
                    pictures += after.pictureRecords - before.pictureRecords;
                    bands += after.rulerRecords - before.rulerRecords;
                    ms.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
                }
            }
            return Cost{median_p95(std::move(ms)), static_cast<double>(pictures) / SAMPLES,
                        static_cast<double>(bands) / SAMPLES};
        };
        std::vector<double> empty;
        for (std::size_t i = 0; i < WARM_UP + SAMPLES; ++i) {
            const auto start = std::chrono::steady_clock::now();
            submit([](dvv::gpu::CommandRecorder&) {});
            const auto stop = std::chrono::steady_clock::now();
            if (i >= WARM_UP) {
                empty.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
            }
        }
        const auto print = [&](std::string_view name, const Cost& cost) {
            std::println(
                "{}x{} {:<20} median {:7.3f} p95 {:7.3f}; passes per frame: picture "
                "{:.2f}, ruler {:.2f}",
                extent.width, extent.height, name, cost.ms[0], cost.ms[1], cost.picturePasses,
                cost.rulerPasses);
        };
        print("empty", Cost{median_p95(std::move(empty)), 0, 0});
        print("full", measure(rulers, true));
        print("cursor-only", measure(rulers, false));
        print("cursor-only, numbers", measure(numbers, false));
    }
    static_cast<void>(std::fflush(stdout));
}

// The grid's cost per frame, measured in pairs: two renderers, one with the grid off and one with
// it on, take turns frame by frame (ABAB), each drawing a new FrameSet with the picture pass alone,
// so a drift in the machine's load falls on both halves of a pair alike. Wall time per frame,
// recorded, submitted and waited for; the median and the 10th and 90th percentiles of the
// differences (on - off). Cases: "worst" (all three levels, the finest 2 backing pixels apart at
// 100 %), "typical" (the major level alone, 32 apart) and "50 %" (the worst grid at 50 %: the
// levels drawn there, as printed). "cached": the same view each frame, the line table read in
// place (playback); "rebuilt": the zoom alternates by 0.01 %, the table rebuilt every frame (a
// view change). The case checks the levels drawn at 100 % and the prepasses per frame. Prints to
// stdout; run it with ctest -V.
TEST_CASE_FIXTURE(CompositorTest, "compositor: grid cost in pairs (manual)") {
    if (dvv::getEnvironment("RAWREEL_MEASURE_FRAMES").value_or("") != "1") {
        MESSAGE("skipped: set RAWREEL_MEASURE_FRAMES=1");
        return;
    }
    constexpr PixelSize PICTURE{3840, 2160};
    const DisplayFrame g =
        displayFrame("synthetic:gradient?w=3840&h=2160&fmt=yuv420p&frames=1", PICTURE);
    constexpr std::size_t WARM_UP = 200;
    constexpr std::size_t PAIRS = 1000;
    rawreel::OverlayParams worst = testGrid(4, 2);
    worst.levels[2].minimumSpacing = 2;
    rawreel::OverlayParams typical = worst;
    typical.levels[1].shown = false;
    typical.levels[2].shown = false;
    rawreel::OverlayParams off = worst;
    for (rawreel::GridLevelParams& level : off.levels) {
        level.shown = false;
    }
    struct Case {
        std::string_view name;
        const rawreel::OverlayParams* grid;
        double zoom;
    };
    const std::array<Case, 3> cases{
        {{"worst", &worst, 1.0}, {"typical", &typical, 1.0}, {"50 %", &worst, 0.5}}};
    const auto percentile = [](const std::vector<double>& sorted, std::size_t p) {
        return sorted[((sorted.size() - 1) * p) / 100];
    };
    std::println("grid cost in pairs: wall ms per frame, {} pairs after {} warm-up pairs", PAIRS,
                 WARM_UP);
    for (const PixelSize extent : {PixelSize{1920, 1080}, PixelSize{3840, 2160}}) {
        for (const Case& c : cases) {
            for (const bool rebuilt : {false, true}) {
                const auto grid_off = makeRenderer(context(), 1);
                const auto grid_on = makeRenderer(context(), 1);
                std::uint64_t version = 0;
                std::uint64_t number = 0;
                const auto run = [&](ViewportRenderer& renderer,
                                     const rawreel::OverlayParams& overlay, std::size_t i) {
                    const double zoom = rebuilt && i % 2 == 1 ? c.zoom * 1.0001 : c.zoom;
                    const DisplaySnapshot snapshot{
                        frameSet(g, 0, 1),
                        DisplayParams{.view = {ViewMode::Zoom, zoom, {1920, 1080}, PICTURE},
                                      .overlay = overlay},
                        ++version};
                    const auto start = std::chrono::steady_clock::now();
                    auto frame = renderer.frame(
                        frameParams(extent, dvv::gpu::PresentEncoding::Linear, number++, true),
                        snapshot);
                    REQUIRE(frame);
                    REQUIRE(static_cast<bool>(frame->record));
                    submit(std::move(frame->record));
                    const auto stop = std::chrono::steady_clock::now();
                    return std::chrono::duration<double, std::milli>(stop - start).count();
                };
                std::vector<double> off_ms;
                std::vector<double> deltas;
                std::uint64_t lines = 0;
                for (std::size_t i = 0; i < WARM_UP + PAIRS; ++i) {
                    const std::uint64_t before = grid_on->lastRecorded().linePasses;
                    const double a = run(*grid_off, off, i);
                    const double b = run(*grid_on, *c.grid, i);
                    if (i >= WARM_UP) {
                        off_ms.push_back(a);
                        deltas.push_back(b - a);
                        lines += grid_on->lastRecorded().linePasses - before;
                    }
                }
                const ViewTransform view =
                    resolveView({ViewMode::Zoom, c.zoom, {1920, 1080}, PICTURE}, extent, PICTURE);
                const CompositeConstants constants = rawreel::compositeConstants(
                    rawreel::DisplayParams{.overlay = *c.grid}, view, {}, false);
                std::string drawn;
                for (std::size_t l = 0; l < rawreel::GRID_LEVELS; ++l) {
                    drawn += (constants.levelShape[l] & rawreel::LEVEL_DRAWN) != 0 ? '1' : '0';
                }
                REQUIRE(drawn != "000");
                if (c.zoom == 1.0) {  // 100 %: every shown level drawn
                    CHECK(drawn == (c.grid == &worst ? "111" : "100"));
                }
                // "cached" reads the table in place on every measured frame, "rebuilt" builds it.
                CHECK(lines == (rebuilt ? PAIRS : 0));
                std::ranges::sort(off_ms);
                std::ranges::sort(deltas);
                const double median = percentile(deltas, 50);
                std::println(
                    "{}x{} {:<8} {:<8} levels {}: off median {:6.3f}; on - off median {:+6.3f} "
                    "p10 {:+6.3f} p90 {:+6.3f} ms = {:+5.2f} % of 16.7 ms, {:+5.2f} % of 8.3 ms; "
                    "prepasses per frame {:.2f}",
                    extent.width, extent.height, c.name, rebuilt ? "rebuilt" : "cached", drawn,
                    percentile(off_ms, 50), median, percentile(deltas, 10), percentile(deltas, 90),
                    median * 100 / 16.7, median * 100 / 8.3, static_cast<double>(lines) / PAIRS);
            }
        }
    }
    static_cast<void>(std::fflush(stdout));
}
