#include "inspect_support.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "core/frame_set.h"
#include "core/session.h"
#include "dvv/base/error.h"
#include "dvv/base/json.h"
#include "dvv/base/task.h"
#include "dvv/base/uri.h"
#include "dvv/gpu/gpu_instance.h"
#include "dvv/media/format_json.h"
#include "dvv/media/frame_store.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/reference.h"
#include "dvv/media/sample_grid.h"
#include "dvv/media/source.h"
#include "dvv/media/source_factory.h"
#include "dvv/media/types.h"
#include "dvv/platform/pixel_size.h"
#include "gpu_support.h"
#include "temporary_directory.h"

namespace rawreel::testing {

namespace {

// Seeded codes in every component's range, the same for the same format and size.
dvv::media::CodeValues seededCodes(dvv::media::FormatId format, dvv::platform::PixelSize size) {
    dvv::media::CodeValues codes{.format = format, .width = size.width, .height = size.height};
    std::mt19937 random(0x5EEDU ^ (std::uint32_t{format} << 16U) ^ (size.width << 8U) ^
                        size.height);
    const dvv::media::PixelFormatDesc& desc = dvv::media::formatDesc(format);
    for (std::size_t k = 0; k < desc.componentCount; ++k) {
        const auto [width, height] = dvv::media::componentSize(format, k, size.width, size.height);
        const std::uint32_t mask = (1U << desc.components.at(k).bits) - 1U;
        std::vector<std::uint16_t>& plane = codes.components.at(k);
        plane.resize(static_cast<std::size_t>(width) * height);
        for (std::uint16_t& code : plane) {
            code = static_cast<std::uint16_t>(random() & mask);
        }
    }
    return codes;
}

// ORs `planted.bits` into its stored word in `bytes`, byte order applied.
void plant(std::vector<std::byte>& bytes, dvv::media::FormatId format,
           dvv::platform::PixelSize size, const PlantedBits& planted) {
    const dvv::Result<dvv::media::StoredWord> word =
        dvv::media::storedWord(format, size.width, size.height, planted.component, planted.sample);
    REQUIRE_MESSAGE(word, (word ? std::string() : word.error().describe()));
    REQUIRE(word->byteOffset + word->wordBytes <= bytes.size());
    for (std::uint32_t i = 0; i < word->wordBytes; ++i) {
        const std::uint32_t at = word->order == dvv::media::ByteOrder::Little
                                     ? i
                                     : static_cast<std::uint32_t>(word->wordBytes) - 1U - i;
        bytes.at(word->byteOffset + at) |=
            static_cast<std::byte>((planted.bits >> (8U * i)) & 0xFFU);
    }
}

}  // namespace

InspectFrame openInspectFrame(GpuTestEnvironment& env, dvv::media::FormatId format,
                              dvv::platform::PixelSize size, std::span<const PlantedBits> planted) {
    InspectFrame frame;
    const std::optional<dvv::gpu::DeviceId> device = env.gpuInstance->bestDevice();
    REQUIRE(device.has_value());
    if (device) {
        auto context = env.gpuInstance->createContext(*device, {});
        REQUIRE_MESSAGE(context, (context ? std::string() : context.error().describe()));
        frame.context = std::move(*context);
    }
    auto store = dvv::media::FrameStore::create(
        dvv::media::FrameStoreOptions{.cpu = env.cpu, .io = env.io, .gpu = frame.context});
    REQUIRE_MESSAGE(store, (store ? std::string() : store.error().describe()));
    frame.store = std::move(*store);

    auto packed = dvv::media::pack(seededCodes(format, size));
    REQUIRE_MESSAGE(packed, (packed ? std::string() : packed.error().describe()));
    frame.bytes = std::move(*packed);
    for (const PlantedBits& bits : planted) {
        plant(frame.bytes, format, size, bits);
    }
    auto reference = dvv::media::unpack(format, size.width, size.height, frame.bytes);
    REQUIRE_MESSAGE(reference, (reference ? std::string() : reference.error().describe()));
    frame.reference = std::move(*reference);

    frame.directory = std::make_unique<TemporaryDirectory>("rawreel-inspect");
    const std::filesystem::path path = frame.directory->path() / "frame.raw";
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(frame.bytes.data()),  // NOLINT: byte I/O
                   static_cast<std::streamsize>(frame.bytes.size()));
        REQUIRE(file.good());
    }
    // A custom format is not known by name to the file source: it reads it from a format file.
    std::string format_param(dvv::media::formatDesc(format).name);
    if (std::ranges::find(dvv::media::catalogue(), format) == dvv::media::catalogue().end()) {
        const std::filesystem::path format_path = frame.directory->path() / "format.json";
        std::ofstream file(format_path);
        file << dvv::json::dump(dvv::media::formatToJson(format));
        REQUIRE(file.good());
        format_param = "@" + format_path.generic_string();
    }
    const dvv::Uri uri{
        .scheme = "file",
        .body = {},
        .path = path.generic_string(),
        .params = {{"fmt", format_param}, {"size", std::format("{}x{}", size.width, size.height)}}};
    INFO("uri ", dvv::formatUri(uri));

    auto factory = dvv::media::SourceFactory::create(*env.components, env.io);
    REQUIRE_MESSAGE(factory, (factory ? std::string() : factory.error().describe()));
    auto opened =
        dvv::syncWait((*factory)->open(dvv::media::SourceRequest{.uri = uri, .hint = {}}));
    REQUIRE_MESSAGE(opened, (opened ? std::string() : opened.error().describe()));
    auto* source = std::get_if<std::unique_ptr<dvv::media::ISource>>(&*opened);
    REQUIRE(source != nullptr);
    REQUIRE(*source != nullptr);
    const dvv::media::FrameDesc desc = (*source)->info().desc;
    REQUIRE(desc.format == format);
    auto origin = frame.store->attach(std::shared_ptr<dvv::media::ISource>(std::move(*source)));
    REQUIRE_MESSAGE(origin, (origin ? std::string() : origin.error().describe()));

    const std::optional<dvv::media::Domain> domain = frame.store->deviceDomain();
    REQUIRE(domain.has_value());
    // The dereferences stay in checked branches: clang-tidy does not know that REQUIRE throws.
    MediaStack::ViewFor views = gpuStackViews(domain ? *domain : dvv::media::Domain{});
    frame.frames.index = 0;
    frame.frames.count = 1;
    frame.frames.epoch = 1;
    for (const ViewRole role : {ViewRole::Display, ViewRole::Planes, ViewRole::Raw}) {
        const std::optional<dvv::media::ViewSpec> view = views(desc, role);
        REQUIRE(view.has_value());
        if (view) {
            auto lease = dvv::syncWait(frame.store->acquire(
                dvv::media::FrameKey{.origin = *origin, .index = 0}, *view, *env.cpu));
            REQUIRE_MESSAGE(lease, (lease ? std::string() : lease.error().describe()));
            frame.frames.leases.at(static_cast<std::size_t>(role)).at(0) = std::move(*lease);
        }
    }
    return frame;
}

namespace {

// The format of `text` (formatFromJson), interned on the first call of each name.
dvv::media::FormatId customFormat(std::string_view text) {
    const dvv::Result<dvv::json::Value> value = dvv::json::parse(text);
    REQUIRE_MESSAGE(value, (value ? std::string() : value.error().describe()));
    dvv::media::FormatId id{};
    if (value) {
        const dvv::Result<dvv::media::FormatId> format = dvv::media::formatFromJson(*value);
        REQUIRE_MESSAGE(format, (format ? std::string() : format.error().describe()));
        id = format ? *format : dvv::media::FormatId{};
    }
    return id;
}

}  // namespace

dvv::media::FormatId testYuv411p() {
    static constexpr std::string_view TEXT = R"({
        "name": "test-yuv411p", "model": "yuv",
        "components": [{"kind": "Y", "bits": 8}, {"kind": "U", "bits": 8, "log2SubX": 2},
                       {"kind": "V", "bits": 8, "log2SubX": 2}],
        "planes": [
            {"groupPixels": 1, "groupBytes": 1, "wordBytes": 1,
             "slots": [{"component": 0, "index": 0, "word": 0, "shift": 0}]},
            {"groupPixels": 4, "groupBytes": 1, "wordBytes": 1,
             "slots": [{"component": 1, "index": 0, "word": 0, "shift": 0}]},
            {"groupPixels": 4, "groupBytes": 1, "wordBytes": 1,
             "slots": [{"component": 2, "index": 0, "word": 0, "shift": 0}]}]})";
    return customFormat(TEXT);
}

dvv::media::FormatId testGray14() {
    static constexpr std::string_view TEXT = R"({
        "name": "test-gray14", "model": "gray",
        "components": [{"kind": "Y", "bits": 14}],
        "planes": [{"groupPixels": 1, "groupBytes": 2, "wordBytes": 2,
                    "slots": [{"component": 0, "index": 0, "word": 0, "shift": 0}]}]})";
    return customFormat(TEXT);
}

dvv::media::FormatId testYa10() {
    static constexpr std::string_view TEXT = R"({
        "name": "test-ya10", "model": "gray",
        "components": [{"kind": "Y", "bits": 10}, {"kind": "A", "bits": 10}],
        "planes": [
            {"groupPixels": 1, "groupBytes": 2, "wordBytes": 2,
             "slots": [{"component": 0, "index": 0, "word": 0, "shift": 0}]},
            {"groupPixels": 1, "groupBytes": 2, "wordBytes": 2,
             "slots": [{"component": 1, "index": 0, "word": 0, "shift": 0}]}]})";
    return customFormat(TEXT);
}

// --- shared by the inspect and padding GPU tests ---

dvv::media::FormatId formatNamed(std::string_view name) {
    const std::optional<dvv::media::FormatId> format = dvv::media::findFormat(name);
    REQUIRE(format.has_value());
    return format.value_or(dvv::media::FormatId{});
}

std::unique_ptr<ViewRenderer> inspectRenderer(const rawreel::testing::InspectFrame& frame) {
    auto renderer = ViewRenderer::create(frame.context, frame.frames.epoch);
    REQUIRE_MESSAGE(renderer, (renderer ? std::string() : renderer.error().describe()));
    return std::move(*renderer);
}

// render() of `params`, submitted and read back (RGBA8, row-major).
std::vector<std::byte> renderInspect(GpuTestEnvironment& env,
                                     const rawreel::testing::InspectFrame& frame,
                                     ViewRenderer& renderer, dvv::platform::PixelSize extent,
                                     const ViewParams& params) {
    auto rendered = renderer.render(extent, frame.frames, params);
    REQUIRE_MESSAGE(rendered, (rendered ? std::string() : rendered.error().describe()));
    REQUIRE(static_cast<bool>(rendered->record));
    auto done =
        rawreel::testing::submitAndWait(*frame.context, *env.cpu, std::move(rendered->record));
    REQUIRE_MESSAGE(done, (done ? std::string() : done.error().describe()));
    auto bytes = rawreel::testing::readImage(*frame.context, *env.cpu, rendered->image, 4);
    REQUIRE_MESSAGE(bytes, (bytes ? std::string() : bytes.error().describe()));
    REQUIRE(bytes->size() == std::size_t{extent.width} * extent.height * 4);
    return std::move(*bytes);
}

// A strip of ten 3-pixel digit cells, 5 rows, each cell's coverage a solid block.
std::shared_ptr<const dvv::gpu::GlyphStrip> digitStrip() {
    auto strip = std::make_shared<dvv::gpu::GlyphStrip>();
    strip->width = 30;
    strip->height = 5;
    strip->advance = 3;
    strip->ascent = 4;
    strip->descent = 1;
    strip->coverage.assign(std::size_t{strip->width} * strip->height, 255);
    return strip;
}

std::array<std::uint8_t, 4> pixelOf(const std::vector<std::byte>& bytes,
                                    dvv::platform::PixelSize extent, std::uint32_t x,
                                    std::uint32_t y) {
    const std::size_t at = ((std::size_t{y} * extent.width) + x) * 4;
    return {std::to_integer<std::uint8_t>(bytes.at(at)),
            std::to_integer<std::uint8_t>(bytes.at(at + 1)),
            std::to_integer<std::uint8_t>(bytes.at(at + 2)),
            std::to_integer<std::uint8_t>(bytes.at(at + 3))};
}

ViewParams planesParams() {
    ViewParams params;
    params.inspect.on = true;
    params.inspect.panels = PanelKind::Planes;
    return params;
}

ViewParams valuesParams(std::shared_ptr<const dvv::gpu::GlyphStrip> strip, ValueMetrics metrics) {
    ViewParams params;
    params.inspect.on = true;
    params.inspect.numbers = true;
    params.inspect.glyphs = std::move(strip);
    params.inspect.metrics = metrics;
    return params;
}

ViewParams cellLineParams() {
    return valuesParams(valueStrip(100, 1,
                                   [](std::uint32_t, std::uint32_t, std::uint32_t) {
                                       return static_cast<std::uint8_t>(0);
                                   }),
                        {.advance = 100, .lineHeight = 9, .padX = 4, .padY = 4});
}

// The box of image pixel (x, y)'s cell at VALUES_ZOOM, inset by the pads, without a label row.
rawreel::testing::TextBox cellBox(std::uint32_t x, std::uint32_t y,
                                  const rawreel::ValueMetrics& metrics) {
    const auto zoom = static_cast<int>(VALUES_ZOOM);
    const auto pad_x = static_cast<int>(metrics.padX);
    const auto pad_y = static_cast<int>(metrics.padY);
    return {.x0 = (static_cast<int>(x) * zoom) + pad_x,
            .x1 = (static_cast<int>(x + 1) * zoom) - pad_x,
            .y0 = (static_cast<int>(y) * zoom) + pad_y,
            .y1 = (static_cast<int>(y + 1) * zoom) - pad_y};
}

std::array<float, 3> encodedOf(const std::array<std::uint8_t, 4>& pixel) {
    return {static_cast<float>(pixel[0]) / 255.0F, static_cast<float>(pixel[1]) / 255.0F,
            static_cast<float>(pixel[2]) / 255.0F};
}

// The encoded colour of a layer colour word (r | g << 8 | b << 16 | a << 24).
namespace {

std::array<float, 3> colourOf(std::uint32_t word) {
    return {static_cast<float>(word & 0xFFU) / 255.0F,
            static_cast<float>((word >> 8U) & 0xFFU) / 255.0F,
            static_cast<float>((word >> 16U) & 0xFFU) / 255.0F};
}

float lumaOf(const std::array<float, 3>& colour) {
    return (0.2126F * colour[0]) + (0.7152F * colour[1]) + (0.0722F * colour[2]);
}
}  // namespace

// The hit of the cell levels `shapes` (PictureConstants::cellShape: level 0 the chroma outline,
// level 1 the cell lines) at P pixel (px, py), the CPU mirror of rows 2 and 3 of the line table
// and the inspect pipeline's winner: the smaller level, else the column.
std::optional<dvv::media::LineHit> cellHit(const std::array<std::uint32_t, 3>& shapes,
                                           const dvv::media::AxisView& ax,
                                           const dvv::media::AxisView& ay, std::int32_t px,
                                           std::int32_t py) {
    const auto level = [](std::uint32_t shape) {
        return dvv::media::LineLevel{
            .stepLog2 = shape & rawreel::LEVEL_STEP_MASK,
            .thickness = (shape >> rawreel::LEVEL_THICKNESS_SHIFT) & rawreel::LEVEL_THICKNESS_MAX,
            .drawn = (shape & rawreel::LEVEL_DRAWN) != 0,
            .halo = false};
    };
    const std::array<dvv::media::LineLevel, 3> columns{level(shapes[0]), level(shapes[2]), {}};
    const std::array<dvv::media::LineLevel, 3> rows{level(shapes[1]), level(shapes[2]), {}};
    const std::optional<dvv::media::LineHit> hx = dvv::media::lineAt(ax, columns, px);
    const std::optional<dvv::media::LineHit> hy = dvv::media::lineAt(ay, rows, py);
    if (hy && (!hx || hy->level < hx->level)) {
        return hy;
    }
    return hx;
}

// The encoded `under` with the cell level line of `hit` over it, `colours` the layer block's
// colours0 (r | g << 8 | b << 16 | a << 24: [0] the cell line, [1] the chroma outline); `under`
// itself without a hit.
std::array<float, 3> overCellLine(std::array<float, 3> under,
                                  const std::optional<dvv::media::LineHit>& hit,
                                  const std::array<std::uint32_t, 4>& colours) {
    if (!hit) {
        return under;
    }
    const std::uint32_t word = hit->level == 0 ? colours[1] : colours[0];
    const float alpha = static_cast<float>(word >> 24U) / 255.0F;
    for (std::size_t c = 0; c < 3; ++c) {
        const float line = static_cast<float>((word >> (8U * c)) & 0xFFU) / 255.0F;
        under.at(c) = (under.at(c) * (1.0F - alpha)) + (line * alpha);
    }
    return under;
}

namespace {

// What the values layer draws at image pixel (x, y) at whole zoom `zoom`, as a function of P pixel
// (px, py) and the pixel `under` it (no
// offset, `block`'s strip `strip`), the CPU mirror of drawValues over `off` (the same view
// without the values, `extent` big): the stacked lines of a Structure cell in its box (shrunk by
// lineHeight + padY in a block's bottom row) and the chroma label of its block, or a Planes
// panel's code in its sample's box; the number colour from the cell's centre, a flagged sample's
// (LAYER_CELL_FLAGS, padding bits in its stored word) the warning colour and its label text
// underlined. `under` is the encoded pixel the values go over: `off`'s at (px, py) with the cell
// levels and the cell flags on it. The cell's lines are laid out once, not per pixel.
using CellValues = std::function<std::array<float, 3>(int, int, const std::array<float, 3>&)>;
CellValues expectedValues(const rawreel::testing::InspectFrame& frame, const LayerBlock& block,
                          const dvv::gpu::GlyphStrip& strip, std::uint32_t zoom,
                          const std::vector<std::byte>& off, dvv::platform::PixelSize extent,
                          std::uint32_t x, std::uint32_t y) {
    using rawreel::testing::codeAt;
    using rawreel::testing::codeCells;
    const rawreel::ValueMetrics metrics{.advance = block.metrics[0],
                                        .lineHeight = block.metrics[1],
                                        .padX = block.metrics[2],
                                        .padY = block.metrics[3]};
    const auto z = static_cast<int>(zoom);
    const auto pad_x = static_cast<int>(metrics.padX);
    const auto pad_y = static_cast<int>(metrics.padY);
    const auto line_height = static_cast<int>(metrics.lineHeight);
    const auto advance = static_cast<int>(strip.width / rawreel::testing::VALUE_GLYPH_CELLS);
    const std::uint32_t width = frame.reference.width;
    const std::uint32_t height = frame.reference.height;
    const auto sub_x = [&](std::size_t k) { return block.component.at(k) & 0xFU; };
    const auto sub_y = [&](std::size_t k) {
        return (block.component.at(k) >> rawreel::LAYER_SUB_Y_SHIFT) & 0xFU;
    };
    const auto letter = [&](std::size_t k) {
        return block.component.at(k) >> rawreel::LAYER_LETTER_SHIFT;
    };
    const auto append = [](std::vector<std::uint32_t>& cells, std::uint32_t code) {
        for (const std::uint32_t digit : codeCells(code)) {
            cells.push_back(digit);
        }
    };
    const auto text_at = [&](int x0, int x1, int y0, int y1) {
        const float grey = rawreel::testing::numberText(
            encodedOf(pixelOf(off, extent, static_cast<std::uint32_t>(x0 + ((x1 - x0) / 2)),
                              static_cast<std::uint32_t>(y0 + ((y1 - y0) / 2)))));
        return std::array<float, 3>{grey, grey, grey};
    };
    const auto flagged = [&](std::size_t k, dvv::media::SamplePosition sample) {
        return (block.flags & rawreel::LAYER_CELL_FLAGS) != 0 &&
               rawreel::testing::paddingFlagged(frame.reference.format, {width, height},
                                                frame.bytes, k, sample);
    };
    const std::array<float, 3> warning = colourOf(block.warning);
    if ((block.flags & rawreel::LAYER_PLANE_SOURCE) != 0) {
        const std::uint32_t k = block.sourceComponent;
        const std::uint32_t bx0 = (x >> sub_x(k)) << sub_x(k);
        const std::uint32_t by0 = (y >> sub_y(k)) << sub_y(k);
        const auto x0 = static_cast<int>(bx0) * z;
        const auto x1 = static_cast<int>(std::min(bx0 + (1U << sub_x(k)), width)) * z;
        const auto y0 = static_cast<int>(by0) * z;
        const auto y1 = static_cast<int>(std::min(by0 + (1U << sub_y(k)), height)) * z;
        const rawreel::testing::TextBox box{
            .x0 = x0 + pad_x, .x1 = x1 - pad_x, .y0 = y0 + pad_y, .y1 = y1 - pad_y};
        std::vector<std::vector<std::uint32_t>> lines(1);
        append(lines[0], codeAt(frame.reference, k, {.x = x, .y = y}));
        if (static_cast<int>(lines[0].size()) * advance > box.x1 - box.x0 ||
            line_height > box.y1 - box.y0) {
            return [](int, int, const std::array<float, 3>& under) {
                return under;
            };  // a cut code is not drawn
        }
        const std::array<std::array<float, 3>, 1> text{
            flagged(k, {.x = x >> sub_x(k), .y = y >> sub_y(k)}) ? warning
                                                                 : text_at(x0, x1, y0, y1)};
        return [&strip, metrics, box, lines, text](int px, int py,
                                                   const std::array<float, 3>& under) {
            return rawreel::testing::linesPixel(strip, metrics, box, lines, text, under, px, py);
        };
    }
    const std::size_t count = std::min<std::size_t>(block.componentCount, 4);
    std::size_t stacked = 0;
    std::uint32_t label_x = 0;
    std::uint32_t label_y = 0;
    bool labels = false;
    for (std::size_t k = 0; k < count; ++k) {
        if (sub_x(k) != 0 || sub_y(k) != 0) {
            labels = true;
            label_x = std::max(label_x, sub_x(k));
            label_y = std::max(label_y, sub_y(k));
        } else {
            ++stacked;
        }
    }
    std::vector<std::vector<std::uint32_t>> lines;
    std::vector<std::size_t> line_component;
    for (std::size_t k = 0; k < count; ++k) {
        if (sub_x(k) == 0 && sub_y(k) == 0) {
            line_component.push_back(k);
            std::vector<std::uint32_t>& cells = lines.emplace_back();
            if (stacked > 1) {
                cells = {letter(k), rawreel::testing::TEXT_SPACE};
            }
            append(cells, codeAt(frame.reference, k, {.x = x, .y = y}));
        }
    }
    const std::uint32_t bx0 = (x >> label_x) << label_x;
    const std::uint32_t by0 = (y >> label_y) << label_y;
    const std::uint32_t bx1 = std::min(bx0 + (1U << label_x), width);
    const std::uint32_t by1 = std::min(by0 + (1U << label_y), height);
    const bool bottom_row = labels && y + 1 == by1;
    const int x0 = static_cast<int>(x) * z;
    const int y0 = static_cast<int>(y) * z;
    const rawreel::testing::TextBox box{
        .x0 = x0 + pad_x,
        .x1 = x0 + z - pad_x,
        .y0 = y0 + pad_y,
        .y1 = y0 + z - pad_y - (bottom_row ? line_height + pad_y : 0)};
    std::vector<std::array<float, 3>> texts;
    texts.reserve(line_component.size());
    for (const std::size_t k : line_component) {
        texts.push_back(flagged(k, {.x = x, .y = y}) ? warning : text_at(x0, x0 + z, y0, y0 + z));
    }
    if (!labels) {
        return [&strip, metrics, box, lines, texts](int px, int py,
                                                    const std::array<float, 3>& under) {
            return rawreel::testing::linesPixel(strip, metrics, box, lines, texts, under, px, py);
        };
    }
    std::vector<std::uint32_t> label;
    std::vector<std::array<std::size_t, 2>> underlined;
    for (std::size_t k = 0; k < count; ++k) {
        if (sub_x(k) != 0 || sub_y(k) != 0) {
            if (!label.empty()) {
                label.push_back(rawreel::testing::TEXT_SPACE);
            }
            const std::size_t first = label.size();
            label.push_back(letter(k));
            label.push_back(rawreel::testing::TEXT_SPACE);
            append(label, codeAt(frame.reference, k, {.x = bx0, .y = by0}));  // the block's code
            if (flagged(k, {.x = bx0 >> sub_x(k), .y = by0 >> sub_y(k)})) {
                underlined.push_back({first, label.size()});
            }
        }
    }
    const rawreel::testing::LabelColours colours{
        .text = block.colours0[2], .box = block.colours0[3], .warning = block.warning};
    return [&strip, metrics, box, lines, texts, label, underlined, colours, bx0, bx1, by1, z](
               int px, int py, const std::array<float, 3>& under) {
        const std::array<float, 3> drawn =
            rawreel::testing::linesPixel(strip, metrics, box, lines, texts, under, px, py);
        return rawreel::testing::labelPixel(strip, metrics, static_cast<int>(bx0) * z,
                                            static_cast<int>(bx1) * z, static_cast<int>(by1) * z,
                                            label, colours, drawn, px, py, underlined);
    };
}

}  // namespace

// The cell (image pixels x0, x1, y0, y1) that draws a level 2 padding flag for image pixel (x, y):
// with LAYER_CELL_FLAGS, a flagged sample of a Planes panel's plane (its sample block the cell) or
// of an unsubsampled component (the pixel the cell); nullopt for none.
std::optional<std::array<std::uint32_t, 4>> flaggedCell(const rawreel::testing::InspectFrame& frame,
                                                        const LayerBlock& block, std::uint32_t x,
                                                        std::uint32_t y) {
    if ((block.flags & rawreel::LAYER_CELL_FLAGS) == 0) {
        return std::nullopt;
    }
    const dvv::platform::PixelSize image{frame.reference.width, frame.reference.height};
    const auto sub_x = [&](std::size_t k) { return block.component.at(k) & 0xFU; };
    const auto sub_y = [&](std::size_t k) {
        return (block.component.at(k) >> rawreel::LAYER_SUB_Y_SHIFT) & 0xFU;
    };
    const auto flagged = [&](std::size_t k) {
        return rawreel::testing::paddingFlagged(frame.reference.format, image, frame.bytes, k,
                                                {.x = x >> sub_x(k), .y = y >> sub_y(k)});
    };
    const bool planes = (block.flags & rawreel::LAYER_PLANE_SOURCE) != 0;
    bool any = false;
    std::array<std::uint32_t, 4> cell{x, x + 1, y, y + 1};  // image pixels x0, x1, y0, y1
    if (planes) {
        const std::size_t k = block.sourceComponent;
        any = flagged(k);
        cell[0] = (x >> sub_x(k)) << sub_x(k);
        cell[1] = std::min(cell[0] + (1U << sub_x(k)), image.width);
        cell[2] = (y >> sub_y(k)) << sub_y(k);
        cell[3] = std::min(cell[2] + (1U << sub_y(k)), image.height);
    } else {
        for (std::size_t k = 0; k < std::min<std::size_t>(block.componentCount, 4); ++k) {
            any = any || (sub_x(k) == 0 && sub_y(k) == 0 && flagged(k));
        }
    }
    if (!any) {
        return std::nullopt;
    }
    return cell;
}

namespace {

// Whether the level 2 padding flag covers P pixel (px, py) of image pixel (x, y): the CPU mirror
// of cellFlags, flagCell and onCellFlag: a 2-pixel inner frame and a top-right corner flag on the
// cell of flaggedCell, legs max(4, advance / 2).
bool onCellFlag(const rawreel::testing::InspectFrame& frame, const LayerBlock& block,
                const dvv::media::AxisView& ax, const dvv::media::AxisView& ay, std::uint32_t x,
                std::uint32_t y, int px, int py) {
    const std::optional<std::array<std::uint32_t, 4>> cell = flaggedCell(frame, block, x, y);
    if (!cell) {
        return false;
    }
    const int x0 = dvv::media::firstPixelOf(ax, (*cell)[0]);
    const int x1 = dvv::media::firstPixelOf(ax, (*cell)[1]);
    const int y0 = dvv::media::firstPixelOf(ay, (*cell)[2]);
    const int y1 = dvv::media::firstPixelOf(ay, (*cell)[3]);
    if (px < x0 || px >= x1 || py < y0 || py >= y1) {
        return false;
    }
    if (px < x0 + 2 || px >= x1 - 2 || py < y0 + 2 || py >= y1 - 2) {
        return true;
    }
    const int leg = std::max(4, static_cast<int>(block.metrics[0]) / 2);
    return (x1 - 1 - px) + (py - y0) < leg;
}

}  // namespace

// expectedValues against `on` for every P pixel of the image pixels [x0, x1) x [y0, y1) at
// `zoom`, skipping a cell whose centre luma is within 1/255 of the number colour's threshold (the
// GPU decides on the float, the reference on its 8-bit read). `cell_shapes` are the cell levels
// drawn under the values (PictureConstants::cellShape; all 0 for none). `near_flags_only`: only the
// cells of a flagged sample and their 4 neighbours (the flag tests: the other cells are the values
// tests' and take most of the time).
CellCheck checkCells(const rawreel::testing::InspectFrame& frame, const LayerBlock& block,
                     const dvv::gpu::GlyphStrip& strip, std::uint32_t zoom,
                     const std::vector<std::byte>& off, const std::vector<std::byte>& on,
                     dvv::platform::PixelSize extent, std::array<std::uint32_t, 2> xs,
                     std::array<std::uint32_t, 2> ys,
                     const std::array<std::uint32_t, 3>& cell_shapes, bool near_flags_only) {
    const dvv::media::AxisView ax{.offset = 0,
                                  .zoom = static_cast<float>(zoom),
                                  .size = frame.reference.width,
                                  .extent = extent.width};
    const dvv::media::AxisView ay{.offset = 0,
                                  .zoom = static_cast<float>(zoom),
                                  .size = frame.reference.height,
                                  .extent = extent.height};
    const auto near_flag = [&](std::uint32_t x, std::uint32_t y) {
        for (const std::array<int, 2> d : {std::array{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
            const auto nx = static_cast<std::uint32_t>(static_cast<int>(x) + d[0]);
            const auto ny = static_cast<std::uint32_t>(static_cast<int>(y) + d[1]);
            if (nx < frame.reference.width && ny < frame.reference.height &&
                flaggedCell(frame, block, nx, ny)) {
                return true;
            }
        }
        return false;
    };
    CellCheck check;
    for (std::uint32_t y = ys[0]; y < ys[1]; ++y) {
        for (std::uint32_t x = xs[0]; x < xs[1]; ++x) {
            if (near_flags_only && !near_flag(x, y)) {
                continue;
            }
            const std::array<float, 3> centre =
                encodedOf(pixelOf(off, extent, (x * zoom) + (zoom / 2), (y * zoom) + (zoom / 2)));
            if (std::abs(lumaOf(centre) - 0.5F) < 1.0F / 255.0F) {
                continue;
            }
            ++check.cells;
            const CellValues values = expectedValues(frame, block, strip, zoom, off, extent, x, y);
            for (std::uint32_t py = y * zoom; py < (y + 1) * zoom; ++py) {
                for (std::uint32_t px = x * zoom; px < (x + 1) * zoom; ++px) {
                    const bool frame_pixel = onCellFlag(frame, block, ax, ay, x, y,
                                                        static_cast<int>(px), static_cast<int>(py));
                    const std::array<float, 3> under =
                        frame_pixel ? colourOf(block.warning)
                                    : overCellLine(encodedOf(pixelOf(off, extent, px, py)),
                                                   cellHit(cell_shapes, ax, ay,
                                                           static_cast<std::int32_t>(px),
                                                           static_cast<std::int32_t>(py)),
                                                   block.colours0);
                    check.flagged += frame_pixel ? 1 : 0;
                    const std::array<float, 3> expected =
                        values(static_cast<int>(px), static_cast<int>(py), under);
                    const auto shown = pixelOf(on, extent, px, py);
                    bool inked = false;
                    for (std::size_t c = 0; c < 3; ++c) {
                        const int base = static_cast<int>(std::lround(under.at(c) * 255.0F));
                        inked = inked || std::abs(int{shown.at(c)} - base) > 1;
                    }
                    check.inked += inked ? 1 : 0;
                    for (std::size_t c = 0; c < 3; ++c) {
                        const int want = static_cast<int>(std::lround(expected.at(c) * 255.0F));
                        check.wrong += std::abs(int{shown.at(c)} - want) <= 1 ? 0 : 1;
                    }
                }
            }
        }
    }
    return check;
}

}  // namespace rawreel::testing
