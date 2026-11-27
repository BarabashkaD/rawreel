#include "render/ruler_pass.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>

#include "dvv/base/error.h"
#include "dvv/base/log.h"
#include "dvv/gpu/context_vulkan.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/gpu/glyph_strip_vulkan.h"
#include "dvv/gpu/image.h"
#include "dvv/gpu/submission.h"
#include "dvv/media/view_lines.h"
#include "rawreel_shaders/rulers.h"
#include "render/ruler_constants.h"

namespace rawreel {

namespace {

void clearNumbers(std::array<RulerConstants, 2>& bands) noexcept {
    for (RulerConstants& band : bands) {
        band.flags &= ~RULER_NUMBERS;
    }
}

}  // namespace

bool numbered(const std::array<RulerConstants, 2>& bands) noexcept {
    return ((bands[0].flags | bands[1].flags) & RULER_NUMBERS) != 0;
}

bool RulerInputs::equalButCursor(const RulerInputs& other) const noexcept {
    RulerInputs mine = *this;
    RulerInputs theirs = other;
    for (std::size_t b = 0; b < mine.constants.size(); ++b) {
        mine.constants[b].flags &= ~(RULER_CURSOR | RULER_LIT);
        mine.constants[b].cursor = 0;
        mine.constants[b].litTick = 0;
        theirs.constants[b].flags &= ~(RULER_CURSOR | RULER_LIT);
        theirs.constants[b].cursor = 0;
        theirs.constants[b].litTick = 0;
    }
    return mine == theirs;
}

bool RulerPass::Bands::numbers() const noexcept {
    return std::ranges::any_of(constants, [](const RulerConstants& band) {
        return band.regionW != 0 && band.regionH != 0 && (band.flags & RULER_NUMBERS) != 0;
    });
}

RulerPass::RulerPass(std::shared_ptr<dvv::gpu::ComputePipeline> pipeline,
                     std::shared_ptr<dvv::gpu::GpuImage> glyph_dummy) noexcept
    : pipeline_(std::move(pipeline)), glyph_dummy_(std::move(glyph_dummy)) {}

dvv::Result<RulerPass> RulerPass::create(dvv::gpu::ExecutionContext& context) {
    DVV_TRY(std::shared_ptr<dvv::gpu::ComputePipeline> pipeline,
            dvv::gpu::createComputePipeline(context, rawreel_shaders::rulers(),
                                            static_cast<std::uint32_t>(sizeof(RulerConstants)),
                                            dvv::gpu::ComputeBindings{.storageImages = 2}));
    // The ruler pass's glyph binding is rgba8: the picture pass's RGBA16F dummy must not be bound
    // there.
    DVV_TRY(std::shared_ptr<dvv::gpu::GpuImage> glyph_dummy,
            dvv::gpu::createImage(context, {.width = 1,
                                            .height = 1,
                                            .format = VK_FORMAT_R8G8B8A8_UNORM,
                                            .usage = VK_IMAGE_USAGE_STORAGE_BIT,
                                            .name = "glyph dummy"}));
    return RulerPass(std::move(pipeline), std::move(glyph_dummy));
}

RulerPass::Part RulerPass::due(State& state, const RulerInputs& inputs, bool strips,
                               bool all) noexcept {
    Part part = Part::None;
    if (strips && (all || state.inputs != inputs)) {
        // Only the cursor moved (its marker, the lit tick): the ruler pixels alone. Any other
        // change redraws the whole bands, whose background changes only together with the ruler
        // rects.
        part = !all && state.inputs && state.inputs->equalButCursor(inputs) ? Part::Rulers
                                                                            : Part::Bands;
    }
    state.inputs = inputs;
    return part;
}

RulerPass::Bands RulerPass::bandsOf(const std::array<RulerConstants, 2>& rulers,
                                    const std::array<dvv::media::PixelRect, 2>& band_rulers,
                                    Part part) noexcept {
    // The shader places every pixel by its output position, so any part of a band draws as the
    // whole band does there.
    Bands bands{.constants = rulers};
    for (std::size_t b = 0; b < bands.constants.size(); ++b) {
        RulerConstants& band = bands.constants[b];
        const dvv::media::PixelRect part_rect = part == Part::Rulers
                                                    ? band_rulers[b]
                                                    : dvv::media::PixelRect{.x = band.regionX,
                                                                            .y = band.regionY,
                                                                            .width = band.regionW,
                                                                            .height = band.regionH};
        if (part != Part::None && !part_rect.empty()) {
            bands.parts[b] = part_rect;
        }
        band.regionX = bands.parts[b].x;
        band.regionY = bands.parts[b].y;
        band.regionW = bands.parts[b].width;  // 0: not dispatched
        band.regionH = bands.parts[b].height;
    }
    return bands;
}

RulerPass::Dispatch RulerPass::dispatch(const Bands& bands,
                                        std::shared_ptr<dvv::gpu::GpuImage> glyphs) const {
    if (glyphs == nullptr) {
        glyphs = glyph_dummy_;
    }
    return Dispatch{.pipeline = pipeline_, .glyphs = std::move(glyphs), .bands = bands.constants};
}

void RulerPass::Dispatch::record(dvv::gpu::CommandRecorder& recorder,
                                 dvv::gpu::GpuImage& output) const {
    const std::array<dvv::gpu::GpuImage*, 2> bound{&output, glyphs.get()};
    for (const RulerConstants& band : bands) {
        if (band.regionW == 0 || band.regionH == 0) {
            continue;
        }
        recorder.dispatch(
            *pipeline, std::as_bytes(std::span(&band, 1)), bound,
            dvv::gpu::GroupCount{.x = (band.regionW + 7) / 8, .y = (band.regionH + 7) / 8});
    }
}

RulerPass::GlyphBinding RulerPass::upload(
    dvv::gpu::ExecutionContext& context, const std::shared_ptr<const dvv::gpu::GlyphStrip>& strip) {
    if (strip == glyph_failed_) {
        return {};  // failed before: only another strip is tried
    }
    dvv::Result<dvv::gpu::GlyphStripUpload> upload = dvv::gpu::uploadGlyphStrip(context, *strip);
    if (!upload) {
        glyph_failed_ = strip;
        DVV_LOG_WARN("viewport: the ruler glyph strip did not upload, no ruler numbers: {}",
                     upload.error().describe());
        return {.image = nullptr, .upload = {}, .failed = true};
    }
    return {.image = std::move(upload->image), .upload = std::move(upload->record)};
}

RulerPass::GlyphBinding RulerPass::frameGlyphs(
    Glyphs& state, dvv::gpu::ExecutionContext& context,
    const std::shared_ptr<const dvv::gpu::GlyphStrip>& strip, bool numbers_on,
    std::array<RulerConstants, 2>& rulers, Part& part) {
    GlyphBinding glyphs;
    if (numbered(rulers)) {
        if (strip != state.source) {
            glyphs = upload(context, strip);
            if (glyphs.image == nullptr) {
                clearNumbers(rulers);
            } else {
                part = Part::Bands;  // every label redrawn with the new strip
            }
        } else {
            glyphs.image = state.image;
        }
    } else if (strip == nullptr || !numbers_on) {
        dropUploaded(state);  // a recording frame still holds its own share of the image
    }
    return glyphs;
}

void RulerPass::uploaded(Glyphs& state, std::shared_ptr<const dvv::gpu::GlyphStrip> strip,
                         std::shared_ptr<dvv::gpu::GpuImage> image) noexcept {
    state.source = std::move(strip);
    state.image = std::move(image);
}

void RulerPass::dropUploaded(Glyphs& state) noexcept {
    state.source.reset();
    state.image.reset();
}

RulerPass::GlyphBinding RulerPass::renderGlyphs(
    dvv::gpu::ExecutionContext& context, const std::shared_ptr<const dvv::gpu::GlyphStrip>& strip,
    std::array<RulerConstants, 2>& rulers) {
    GlyphBinding glyphs;
    if (numbered(rulers)) {
        glyphs = upload(context, strip);
        if (glyphs.image == nullptr) {
            clearNumbers(rulers);
        }
    }
    return glyphs;
}

}  // namespace rawreel
