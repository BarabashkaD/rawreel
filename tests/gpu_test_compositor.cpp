// The viewport compositor on a real device (design M3 v2 §4.4, §8.3): composite.slang matches the
// CPU reference within 1 on store-made display images, frame() records only on change, other
// epochs draw the background, an sRGB swapchain skips the encode, and lastRecorded() is readable
// while frame() records.
#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
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
#include "core/session_types.h"
#include "core/view.h"
#include "dvv/base/log.h"
#include "dvv/base/task.h"
#include "dvv/base/uri.h"
#include "dvv/gpu/context_vulkan.h"
#include "dvv/gpu/gpu_instance.h"
#include "dvv/gpu/presenter_vulkan.h"
#include "dvv/media/display.h"
#include "dvv/media/display_vulkan.h"
#include "dvv/media/frame.h"
#include "dvv/media/frame_store.h"
#include "dvv/media/frame_vulkan.h"
#include "dvv/media/source_factory.h"
#include "gpu_support.h"
#include "render/display_state.h"
#include "render/viewport_renderer.h"

using dvv::platform::PixelSize;
using rawreel::CompositeConstants;
using rawreel::CompositeParams;
using rawreel::DisplaySnapshot;
using rawreel::FrameSet;
using rawreel::HostDisplayImage;
using rawreel::RecordedInfo;
using rawreel::resolveView;
using rawreel::ViewMode;
using rawreel::ViewportRenderer;
using rawreel::ViewTransform;

namespace {

using Rgba8 = std::array<std::uint8_t, 4>;

constexpr std::string_view G = "synthetic:gradient?w=37&h=23&fmt=yuv420p&frames=1";
constexpr std::string_view N = "synthetic:noise?w=37&h=23&fmt=gbrp&frames=1";
constexpr PixelSize IMAGE{37, 23};
constexpr CompositeParams DARK{.dark = true};
constexpr CompositeParams LIGHT{.dark = false};
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
            bool wrong = false;
            for (std::size_t c = 0; c < 4; ++c) {
                wrong = wrong || std::abs(static_cast<int>(gpu[c]) - static_cast<int>(cpu[c])) > 1;
            }
            if (wrong && count == 0) {
                MESSAGE("first mismatch at ("
                        << x << ", " << y << "): zoom " << constants.zoom << " origin ("
                        << constants.originX << ", " << constants.originY << ") flags "
                        << constants.flags << "; gpu " << int{gpu[0]} << ' ' << int{gpu[1]} << ' '
                        << int{gpu[2]} << ' ' << int{gpu[3]} << ", reference " << int{cpu[0]} << ' '
                        << int{cpu[1]} << ' ' << int{cpu[2]} << ' ' << int{cpu[3]});
            }
            count += wrong ? 1 : 0;
        }
    }
    return count;
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

    // Frame 0 of `uri` as a display view, with its host copy.
    [[nodiscard]] DisplayFrame displayFrame(std::string_view uri) {
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
        REQUIRE(frame.size == IMAGE);
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
                                                       const CompositeParams& params) {
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
                rawreel::compositeConstants(DARK, ViewTransform::fit(viewport, IMAGE), false),
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
        CHECK(mismatches(
                  bytes,
                  rawreel::compositeConstants(LIGHT, ViewTransform::fit(viewport, IMAGE), false),
                  n.image()) <= allowance(viewport));
    }
    for (const bool dark : {true, false}) {
        INFO("no frame, viewport 211x97, ", dark ? "dark" : "light", " background");
        const PixelSize viewport{211, 97};
        const std::vector<std::byte> bytes =
            renderAndRead(*renderer, viewport, FrameSet{}, CompositeParams{.dark = dark});
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
    const CompositeParams params{.dark = true, .view = {ViewMode::Zoom, 2, {12, 8}, IMAGE}};
    const std::vector<std::byte> bytes =
        renderAndRead(*renderer, viewport, frameSet(g, 0, 1), params);
    const auto view = resolveView(params.view, viewport, IMAGE);
    CHECK(mismatches(bytes, rawreel::compositeConstants(params, view, false), g.image()) == 0);
    const RecordedInfo info = renderer->lastRecorded();
    CHECK(info.zoom == 2.0);
    CHECK(info.mode == ViewMode::Zoom);
    CHECK(info.origin == view.origin());

    // Through frame(): a setParams that changes only the view records again, at the new view.
    rawreel::DisplayState state;
    state.show(frameSet(g, 0, 1));
    state.setParams(params);
    auto panned = renderer->frame(
        frameParams(viewport, dvv::gpu::PresentEncoding::Linear, 0, false), state.snapshot());
    REQUIRE_MESSAGE(panned, (panned ? std::string() : panned.error().describe()));
    REQUIRE(static_cast<bool>(panned->record));
    CHECK(renderer->lastRecorded().origin == view.origin());
    submit(std::move(panned->record));
    const CompositeParams moved{.dark = true, .view = {ViewMode::Zoom, 4, {20, 14}, IMAGE}};
    state.setParams(moved);
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
    const RecordedInfo first_info{.epoch = 1,
                                  .index = 0,
                                  .extent = wide,
                                  .zoom = ViewTransform::fit(wide, IMAGE).zoom(),
                                  .origin = ViewTransform::fit(wide, IMAGE).origin()};
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

    // No frame: the background only.
    auto cleared = renderer->frame(frameParams(tall, dvv::gpu::PresentEncoding::Linear, 5, true),
                                   DisplaySnapshot{FrameSet{}, DARK, 3});
    REQUIRE(cleared);
    REQUIRE(static_cast<bool>(cleared->record));
    CHECK(renderer->lastRecorded() ==
          RecordedInfo{.epoch = 1, .index = -1, .extent = tall, .zoom = 0.0});
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
                     rawreel::compositeConstants(DARK, ViewTransform::fit(IMAGE, IMAGE), false),
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
    CHECK(mismatches(bytes,
                     rawreel::compositeConstants(DARK, ViewTransform::fit(viewport, IMAGE), true),
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
