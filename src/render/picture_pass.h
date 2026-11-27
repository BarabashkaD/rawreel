#pragma once

// The picture pass of the view renderer: the picture pipeline (with a layer on, the inspect
// pipeline; with the markers alone, the marker pass after it) over the drawn part of a panel's
// picture region P, with its grid prepass (the line table), and the inputs that decide when a
// panel's picture pass records again.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "dvv/base/error.h"
#include "dvv/gpu/context_vulkan.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/gpu/gpu_fwd.h"
#include "dvv/gpu/image.h"
#include "dvv/gpu/submission.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "dvv/platform/pixel_size.h"
#include "render/pass_regions.h"
#include "render/picture_constants.h"
#include "render/view_state.h"

namespace rawreel {

// = InspectConstants of shaders/picture_inspect.slang, 128 bytes: the picture's constants, then the
// layer block's device address and the frame-dependent addresses, which sit here so the block
// changes only with the layer params (0 while unused).
struct InspectConstants {
    PictureConstants picture{};
    std::uint64_t layers = 0;    // the LayerBlock (render/layer_block.h)
    std::uint64_t rawBase = 0;   // the frame's stored bytes
    std::uint64_t readback = 0;  // the probe's readback buffer
    std::uint64_t tiles = 0;     // the padding check's tile map (LAYER_PADDING_MARKS)
};
static_assert(sizeof(PictureConstants) == 96);
static_assert(offsetof(InspectConstants, layers) == 96);
static_assert(offsetof(InspectConstants, rawBase) == 104);
static_assert(offsetof(InspectConstants, readback) == 112);
static_assert(sizeof(InspectConstants) == 128);

// = PaddingConstants of shaders/padding_check.slang, 64 bytes: the padding check's addresses and
// the view of P.
struct PaddingConstants {
    std::uint64_t layers = 0;   // the LayerBlock: the stored-word layouts and the tile map's size
    std::uint64_t rawBase = 0;  // the frame's stored bytes (the Raw view)
    std::uint64_t counter = 0;  // the readback slot's counter
    std::uint64_t tiles = 0;    // the tile map
    std::int32_t offsetX = 0, offsetY = 0;
    float zoom = 0.0F;
    std::uint32_t imageW = 0, imageH = 0, viewportW = 0, viewportH = 0, pad = 0;
};
static_assert(sizeof(PaddingConstants) == 64);

// The plane images the inspect pipeline binds (3-6); null: a dummy.
inline constexpr std::size_t INSPECT_PLANES = 4;
// The inspect pipeline's storage images: 0 display, 1 output, 2 lines, 3-6 planes, 7 value strip,
// 8 test codes.
inline constexpr std::uint32_t INSPECT_BINDINGS = 9;

// The inspect inputs of one panel's picture pass: with a layer block, the inspect pipeline runs
// (picture_inspect.slang), else today's (picture.slang). A block with only the markers on keeps
// picture.slang, and the marker pass (picture_markers.slang) draws the markers over it.
struct InspectBinding {
    std::shared_ptr<dvv::gpu::GpuBuffer> layers;  // the immutable layer block; null: no layer on
    bool markersOnly = false;                     // the block's flags are LAYER_MARKERS alone
    std::array<dvv::gpu::GpuImage*, INSPECT_PLANES> planes{};  // the frame's planes; null: a dummy
    std::shared_ptr<dvv::gpu::GpuImage> valueStrip;  // the uploaded value strip; null: a dummy
    // The R32_UINT image of the image size LAYER_TEST_CODES writes (§7); null: a dummy. The
    // caller keeps it alive until the submission has completed.
    dvv::gpu::GpuImage* testCodes = nullptr;
    // The Raw view's stored bytes (InspectConstants::rawBase; 0: none, so no cell flags). The
    // caller's leases keep the buffer.
    std::uint64_t rawBase = 0;
    // The padding check (LAYER_PADDING_MARKS): before the picture pass the counter's 8 bytes at
    // `counterOffset` of `counter` are zeroed, the tile map filled with 0xFFFFFFFF and
    // padding_check run over samplesX x samplesY x components threads; null `tiles`: no check and
    // no marks; null `counter` with `tiles`: no check, the marks drawn from the map as the last
    // check left it (a redraw of the marker rects alone).
    std::shared_ptr<dvv::gpu::GpuBuffer> counter;
    std::uint64_t counterOffset = 0;
    std::shared_ptr<dvv::gpu::GpuBuffer> tiles;  // the LayerBlock's tilesX x tilesY uint32
    std::uint32_t samplesX = 0, samplesY = 0, components = 0;
};

// The inputs a panel's picture pass last recorded; it records again when they differ.
struct PictureInputs {
    std::uint64_t version = 0;
    dvv::media::ViewTransform view{};
    dvv::media::PixelRect picture{};
    dvv::media::PixelRect pictureDrawn{};
    PictureConstants constants{};
    dvv::platform::PixelSize extent{};
    bool skipEncode = false;
    std::uint64_t layers = 0;      // the layer block's serial (never reused); 0: no layer on
    std::uint64_t valueStrip = 0;  // the bound value strip's serial (never reused); 0: none
    // The serial of the layer block with its pin and outline fields cleared (LayerCache), and
    // the markers those fields hold.
    std::uint64_t layersBase = 0;
    MarkerParams markers{};
    bool operator==(const PictureInputs&) const = default;
    // Whether the inputs equal `other` but for the markers: compared with `layers` and
    // `markers` cleared (layersBase stands for the rest of the layer block).
    [[nodiscard]] bool equalButMarkers(const PictureInputs& other) const noexcept;
};

// The output pixels the markers of `markers` draw in a view `view` over `regions`, each rect
// clipped to regions.pictureDrawn (empty ones dropped): a pin span of at least 6 px on both axes
// gives the four edge strips of its 3 px ring (the 2 px box and the 1 px rim outside it), a
// smaller one the 8 x 8 rect of its dot and rim; the outline gives the four edge strips of its
// 2 px line outside the region. Nothing for a view without an image.
[[nodiscard]] std::vector<dvv::media::PixelRect> markerRects(const MarkerParams& markers,
                                                             const dvv::media::ViewTransform& view,
                                                             const PassRegions& regions);

// What the grid prepass reads (shaders/picture_lines.slang): P's size, the image size, the
// view's zoom and offset, and per level its step, thickness, halo and drawn bits; a level's
// colour, opacity and adaptive bit draw in the picture pass only.
struct LineInputs {
    std::uint32_t viewportW = 0, viewportH = 0;
    std::uint32_t imageW = 0, imageH = 0;
    float zoom = 0.0F;
    std::int32_t offsetX = 0, offsetY = 0;
    std::array<std::uint32_t, 4> levelShape{};  // without LEVEL_ADAPTIVE
    std::array<std::uint32_t, 3> cellShape{};   // PictureConstants::cellShape
    bool operator==(const LineInputs&) const = default;
    [[nodiscard]] static LineInputs of(const PictureConstants& constants) noexcept;
};

// Whether a grid level is drawn.
[[nodiscard]] bool gridDrawn(const PictureConstants& constants) noexcept;
// Whether a cell level is drawn (rows 2 and 3 of the line table, read by the inspect pipeline).
[[nodiscard]] bool cellLinesDrawn(const PictureConstants& constants) noexcept;
// Whether the picture pass reads the line table: a grid or a cell level drawn. Every path that
// records the picture pass builds the table on this, else rows of it hold stale memory.
[[nodiscard]] bool readsLines(const PictureConstants& constants) noexcept;

// The picture, inspect, marker and grid prepass pipelines, the 1x1 RGBA16F dummy bound when there
// is no frame, and the inspect pipeline's distinct 1x1 dummies. Created on the GUI thread, used on
// the presenter thread.
class PicturePass {
public:
    // The inspect pipeline and the dummies of its unused slots: one per plane (R16_UINT), the
    // value strip (RGBA8) and the test codes (R32_UINT).
    struct Inspect {
        std::shared_ptr<dvv::gpu::ComputePipeline> pipeline;
        std::array<std::shared_ptr<dvv::gpu::GpuImage>, INSPECT_PLANES> planes;
        std::shared_ptr<dvv::gpu::GpuImage> valueStrip;
        std::shared_ptr<dvv::gpu::GpuImage> testCodes;
        std::shared_ptr<dvv::gpu::ComputePipeline> padding;  // padding_check.slang
    };
    // What a panel's picture pass last recorded (frame() only).
    struct State {
        std::optional<PictureInputs> inputs;  // of the last frame() record
        // The inputs of the line table: set by each due picture pass that reads the table
        // (rebuilt or in place), cleared by one that does not (P or its drawn part empty, no grid
        // level drawn).
        std::optional<LineInputs> lines;
    };
    // What a frame()'s picture pass of one panel does.
    struct Due {
        bool record = false;      // the pass records: its inputs changed, or `all`
        bool buildLines = false;  // its prepass rebuilds the line table first
        // A change in the markers only: the rects to dispatch (markerRects of the old markers,
        // then of the new ones), one dispatch each; empty: the whole drawn part.
        std::vector<dvv::media::PixelRect> parts{};
    };
    // The commands of one picture pass: the prepass over P when it rebuilds the line table, then
    // the picture pipeline (the inspect pipeline when `inspect` has a layer block with more than
    // the markers on) over the drawn part of P, then with only the markers on the marker pass
    // over `markerParts`; nothing when that part is not dispatched.
    struct Dispatch {
        std::shared_ptr<dvv::gpu::ComputePipeline> pipeline;
        std::shared_ptr<dvv::gpu::ComputePipeline> linesPipeline;
        std::shared_ptr<dvv::gpu::GpuImage> dummy;
        std::shared_ptr<const Inspect> inspectPass;  // set only with more than the markers on
        // Set only with the markers alone on and markerParts not empty.
        std::shared_ptr<dvv::gpu::ComputePipeline> markerPass;
        InspectBinding inspect;
        dvv::gpu::GpuImage* image = nullptr;  // the display image; null: the dummy
        PictureConstants constants{};
        std::uint32_t groupsX = 0, groupsY = 0;  // groupsX 0: not dispatched
        std::uint32_t lineGroups = 0;            // 0: no prepass
        // Not empty: one dispatch per rect (output pixels, in the drawn part) instead of the
        // drawn part.
        std::vector<dvv::media::PixelRect> parts;
        // The marker pass's rects (output pixels, in the drawn part), one dispatch each.
        std::vector<dvv::media::PixelRect> markerParts;
        // Records into `output` and the line table `lines`, which outlive the submission.
        void record(dvv::gpu::CommandRecorder& recorder, dvv::gpu::GpuImage& output,
                    dvv::gpu::GpuImage& lines) const;
    };

    // Whether the commands run the padding check (RecordedInfo::paddingPasses).
    [[nodiscard]] static bool checksPadding(const Dispatch& dispatch) noexcept {
        return dispatch.groupsX != 0 && dispatch.inspect.tiles != nullptr &&
               dispatch.inspect.counter != nullptr;
    }

    // The value strip of a view (frame(): one every panel binds), as a recorded frame uploaded it;
    // a dropped frame clears it.
    struct ValueStrip {
        std::shared_ptr<const dvv::gpu::GlyphStrip> source;
        std::shared_ptr<dvv::gpu::GpuImage> image;
        std::uint64_t serial = 0;  // never reused; 0: none
    };
    // An upload of the value strip.
    struct StripUpload {
        std::shared_ptr<dvv::gpu::GpuImage> image;  // null: none (failed, or tried before)
        dvv::gpu::RecordFunction upload;            // fills `image`
        bool failed = false;                        // this call's upload failed (one per strip)
    };

    // Errors of the free createComputePipeline and createImage pass through (DeviceLost on a
    // lost context).
    [[nodiscard]] static dvv::Result<PicturePass> create(dvv::gpu::ExecutionContext& context);
    // The prepass's R32_UINT line table of an output of `extent`: max(width, height) x 4, row 0
    // P's columns, row 1 its rows, rows 2 and 3 the same for the cell levels.
    [[nodiscard]] static dvv::Result<std::shared_ptr<dvv::gpu::GpuImage>> createLineTable(
        dvv::gpu::ExecutionContext& context, dvv::platform::PixelSize extent);
    // Whether a panel's pass records and rebuilds the line table, with `state` updated to
    // `inputs`: it records when `all` (a dropped frame, new targets) or its inputs changed. When
    // they changed but for the markers (equalButMarkers) and not `all`, it dispatches only the
    // parts: the old and the new markerRects (no record when both are empty). A
    // pass that dispatches with a grid level drawn rebuilds the table only when its LineInputs
    // differ from the table's, or when `all`; else it reads the table in place.
    [[nodiscard]] static Due due(State& state, const PictureInputs& inputs, bool all);
    // The pass over `drawn` (a part of P = `picture`) with `constants`, reading `image` (null: the
    // dummy); nothing dispatched when `dispatch` is false or `drawn` is empty; the prepass over
    // all of P before it when `build_lines`. With `inspect.layers` the inspect pipeline draws,
    // binding the layer block and `inspect.planes`; with `inspect.markersOnly` picture.slang
    // draws, then the marker pass over `marker_parts` (markerRects of the markers the block
    // draws, or the parts of a change in the markers only), reading the layer block. Non-empty
    // `parts` dispatch those rects only.
    [[nodiscard]] Dispatch dispatch(bool dispatch, dvv::gpu::GpuImage* image,
                                    const PictureConstants& constants,
                                    dvv::media::PixelRect picture, dvv::media::PixelRect drawn,
                                    bool build_lines, InspectBinding inspect,
                                    std::vector<dvv::media::PixelRect> parts = {},
                                    std::vector<dvv::media::PixelRect> marker_parts = {}) const;
    // uploadGlyphStrip of `strip` (the value strip, as the ruler strip); on an error the empty
    // upload (failed), with one warning per strip: the strip that failed last is not tried again.
    [[nodiscard]] StripUpload uploadValueStrip(
        dvv::gpu::ExecutionContext& context,
        const std::shared_ptr<const dvv::gpu::GlyphStrip>& strip);

private:
    PicturePass(std::shared_ptr<dvv::gpu::ComputePipeline> pipeline,
                std::shared_ptr<dvv::gpu::ComputePipeline> lines_pipeline,
                std::shared_ptr<dvv::gpu::GpuImage> dummy, std::shared_ptr<const Inspect> inspect,
                std::shared_ptr<dvv::gpu::ComputePipeline> marker_pipeline) noexcept;

    std::shared_ptr<dvv::gpu::ComputePipeline> pipeline_;
    std::shared_ptr<dvv::gpu::ComputePipeline> lines_pipeline_;
    std::shared_ptr<dvv::gpu::ComputePipeline> marker_pipeline_;
    std::shared_ptr<dvv::gpu::GpuImage> dummy_;
    std::shared_ptr<const Inspect> inspect_;
    // The last value strip whose upload failed (warned once); held, so its address is not reused.
    std::shared_ptr<const dvv::gpu::GlyphStrip> value_failed_;
};

}  // namespace rawreel
