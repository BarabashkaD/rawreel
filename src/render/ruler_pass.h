#pragma once

// The ruler pass of the view renderer: the ruler pipeline over a panel's two ruler bands, the
// glyph strip it binds for the numbers, and the inputs that decide when a panel's ruler pass
// records again and which part of the bands it dispatches.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

#include "dvv/base/error.h"
#include "dvv/gpu/context_vulkan.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/gpu/image.h"
#include "dvv/gpu/submission.h"
#include "dvv/media/view_lines.h"
#include "dvv/platform/pixel_size.h"
#include "render/ruler_constants.h"

namespace rawreel {

// The inputs a panel's ruler pass last recorded; it records again when they differ.
struct RulerInputs {
    std::array<RulerConstants, 2> constants{};
    dvv::platform::PixelSize extent{};
    bool skipEncode = false;
    // Owned, so a new strip at a freed strip's address never compares equal.
    std::shared_ptr<const dvv::gpu::GlyphStrip> glyphs;
    bool operator==(const RulerInputs&) const = default;
    // These inputs equal `other` once the cursor fields (RULER_CURSOR, cursor) and the lit
    // tick's (RULER_LIT, litTick) are cleared: every pixel they change lies on a ruler.
    [[nodiscard]] bool equalButCursor(const RulerInputs& other) const noexcept;
};

// Whether a ruler band draws numbers: only then does the ruler pass read the glyph strip.
[[nodiscard]] bool numbered(const std::array<RulerConstants, 2>& bands) noexcept;

// The ruler pipeline, the 1x1 RGBA8 glyph dummy bound without numbers, and the glyph strip
// frame() uploaded last (an RGBA8 image), shared by every panel. Created on the GUI thread, used
// on the presenter thread.
class RulerPass {
public:
    // Which part of each band a ruler pass dispatches.
    enum class Part : std::uint8_t {
        None,    // not due
        Bands,   // the whole bands
        Rulers,  // only a cursor field changed: the ruler pixels of each band (bandRulers)
    };
    // What a panel's ruler pass last recorded (frame() only).
    struct State {
        std::optional<RulerInputs> inputs;
    };
    // frame() only, one per view (every panel binds it): the strip `image` holds, as uploaded by a
    // recorded frame (a dropped frame releases both).
    struct Glyphs {
        std::shared_ptr<const dvv::gpu::GlyphStrip> source;
        std::shared_ptr<dvv::gpu::GpuImage> image;
    };
    // The glyph strip a ruler pass binds.
    struct GlyphBinding {
        std::shared_ptr<dvv::gpu::GpuImage> image;  // null: the glyph dummy
        dvv::gpu::RecordFunction upload;  // fills `image`; empty when it holds the strip already
        bool failed = false;              // an upload of this call failed (one per strip)
    };
    // The bands of one ruler pass: each band's constants with its region set to the part it
    // dispatches (width 0: not dispatched).
    struct Bands {
        std::array<RulerConstants, 2> constants{};
        std::array<dvv::media::PixelRect, 2> parts{};  // the dispatched parts, empty: none
        [[nodiscard]] bool dispatched() const noexcept {
            return !parts[0].empty() || !parts[1].empty();
        }
        // Whether a band it dispatches draws numbers (RULER_NUMBERS).
        [[nodiscard]] bool numbers() const noexcept;
    };
    // The commands of one ruler pass: the pipeline over each dispatched band part.
    struct Dispatch {
        std::shared_ptr<dvv::gpu::ComputePipeline> pipeline;
        std::shared_ptr<dvv::gpu::GpuImage> glyphs;  // the strip, or the glyph dummy
        std::array<RulerConstants, 2> bands{};
        // Records into `output`, which outlives the submission.
        void record(dvv::gpu::CommandRecorder& recorder, dvv::gpu::GpuImage& output) const;
    };

    // Errors of the free createComputePipeline and createImage pass through (DeviceLost on a
    // lost context).
    [[nodiscard]] static dvv::Result<RulerPass> create(dvv::gpu::ExecutionContext& context);

    // Which part a panel's pass dispatches, with `state` updated to `inputs`: none without
    // `strips`; else when `all` (a dropped frame, new targets) or its inputs changed, only the
    // rulers when they differ from the last record's in the cursor fields only (and not `all`),
    // else the whole bands.
    [[nodiscard]] static Part due(State& state, const RulerInputs& inputs, bool strips,
                                  bool all) noexcept;
    // The part `part` of the bands of `rulers`: their regions, or `band_rulers` for Part::Rulers;
    // nothing for Part::None.
    [[nodiscard]] static Bands bandsOf(const std::array<RulerConstants, 2>& rulers,
                                       const std::array<dvv::media::PixelRect, 2>& band_rulers,
                                       Part part) noexcept;
    // The pass over `bands`, binding `glyphs` (null: the glyph dummy).
    [[nodiscard]] Dispatch dispatch(const Bands& bands,
                                    std::shared_ptr<dvv::gpu::GpuImage> glyphs) const;

    // frame(), for a due pass: the strip it binds. With numbers drawn (`rulers`), the strip is
    // uploaded when it is another than the uploaded one (none after dropUploaded()); a new
    // output keeps it. A failed upload clears the numbers in `rulers`; a new upload sets `part`
    // to Part::Bands (every label redrawn with the new strip). Without numbers, the uploaded
    // strip is released when `strip` is null or `numbers_on` is false.
    [[nodiscard]] GlyphBinding frameGlyphs(Glyphs& state, dvv::gpu::ExecutionContext& context,
                                           const std::shared_ptr<const dvv::gpu::GlyphStrip>& strip,
                                           bool numbers_on, std::array<RulerConstants, 2>& rulers,
                                           Part& part);
    // frame(): the record carries the upload of `strip` into `image`, which binds from now on
    // (it replaces and releases the old one).
    static void uploaded(Glyphs& state, std::shared_ptr<const dvv::gpu::GlyphStrip> strip,
                         std::shared_ptr<dvv::gpu::GpuImage> image) noexcept;
    // A dropped frame: its upload may not have run, so the strip is uploaded again before it is
    // bound (a dropped record held its own share of the image).
    static void dropUploaded(Glyphs& state) noexcept;
    // render(): the upload of `strip` when `rulers` draw numbers, which a failed upload clears.
    [[nodiscard]] GlyphBinding renderGlyphs(
        dvv::gpu::ExecutionContext& context,
        const std::shared_ptr<const dvv::gpu::GlyphStrip>& strip,
        std::array<RulerConstants, 2>& rulers);

private:
    RulerPass(std::shared_ptr<dvv::gpu::ComputePipeline> pipeline,
              std::shared_ptr<dvv::gpu::GpuImage> glyph_dummy) noexcept;
    // uploadGlyphStrip of `strip`; on an error the empty binding (failed), with one warning per
    // strip. The strip that failed last is not tried again: the empty binding at once.
    [[nodiscard]] GlyphBinding upload(dvv::gpu::ExecutionContext& context,
                                      const std::shared_ptr<const dvv::gpu::GlyphStrip>& strip);

    std::shared_ptr<dvv::gpu::ComputePipeline> pipeline_;
    std::shared_ptr<dvv::gpu::GpuImage> glyph_dummy_;  // 1x1 RGBA8, bound without numbers
    // The last strip whose upload failed (warned once, not tried again); held, so its address is
    // not reused.
    std::shared_ptr<const dvv::gpu::GlyphStrip> glyph_failed_;
};

}  // namespace rawreel
