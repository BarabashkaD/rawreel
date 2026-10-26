#include "core/format_choice.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "core/format_presets.h"
#include "dvv/base/json.h"
#include "dvv/base/uri.h"
#include "dvv/media/clip_json.h"
#include "dvv/media/colour_preset.h"
#include "dvv/media/display.h"
#include "dvv/media/file_name.h"
#include "dvv/media/format_json.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/uri_colour.h"

namespace rawreel {

namespace {

using dvv::media::ColourMatrix;
using dvv::media::ColourModel;
using dvv::media::ColourParams;
using dvv::media::ColourRange;
using dvv::media::Primaries;
using dvv::media::TransferFn;

// The parameters applyChoice() replaces (matrix, kr, kb, transfer and primaries: old URIs).
constexpr std::array<std::string_view, 12> REPLACED{"fmt", "size",   "w",        "h",
                                                    "fps", "colour", "matrix",   "kr",
                                                    "kb",  "range",  "transfer", "primaries"};

// The NTSC decimals of the fps field (D5 design §1.4), in thousandths, and their numerators over
// 1001.
struct NtscRate {
    std::int64_t milli;
    std::int64_t num;
};
constexpr std::array<NtscRate, 6> NTSC_RATES{{{.milli = 23976, .num = 24000},
                                              {.milli = 23980, .num = 24000},
                                              {.milli = 29970, .num = 30000},
                                              {.milli = 47952, .num = 48000},
                                              {.milli = 59940, .num = 60000},
                                              {.milli = 119880, .num = 120000}}};
constexpr std::int64_t NTSC_DEN = 1001;
constexpr std::int64_t MILLI = 1000;

std::string formatFps(dvv::media::Rational fps) {
    if (fps.den == 1) {
        return std::format("{}", fps.num);
    }
    return std::format("{}/{}", fps.num, fps.den);
}

constexpr std::size_t MAX_FPS_DECIMALS = 6;

// A non-empty run of ASCII digits as a positive or zero int64; nullopt on anything else or an
// overflow.
std::optional<std::int64_t> parseDigits(std::string_view text) noexcept {
    if (text.empty() || !std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

// A positive dimension (1 .. uint32 max) from digits.
std::optional<std::uint32_t> parseDimension(std::string_view text) noexcept {
    const std::optional<std::int64_t> value = parseDigits(text);
    if (!value || *value <= 0 || *value > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

// "WxH" as two positive dimensions.
bool parseSizeText(std::string_view text, FormatChoice& choice) noexcept {
    const std::size_t x = text.find('x');
    if (x == std::string_view::npos) {
        return false;
    }
    const std::optional<std::uint32_t> width = parseDimension(text.substr(0, x));
    const std::optional<std::uint32_t> height = parseDimension(text.substr(x + 1));
    if (!width || !height) {
        return false;
    }
    choice.width = *width;
    choice.height = *height;
    return true;
}

// The format, size and fps of the clip description of clip=@<path>; nothing when it cannot be
// read (the open names the error).
void applyClipFormat(const dvv::Uri& parsed, FormatChoice& choice) {
    const auto clip = parsed.param("clip");
    if (!clip || !clip->starts_with('@') || clip->size() == 1) {
        return;
    }
    const dvv::Result<dvv::media::ClipDescription> loaded =
        dvv::media::loadClipJson(clip->substr(1));
    if (!loaded) {
        return;
    }
    const dvv::media::ClipDescription& description = *loaded;
    if (description.format) {
        choice.format = *description.format;
    }
    if (description.size) {
        choice.width = description.size->first;
        choice.height = description.size->second;
    }
    if (description.fps) {
        choice.fps = *description.fps;
    }
}

// The file name's size and format, then the clip description's (clip=@), then the query's fmt,
// size, w, h and fps (file: URIs only).
void applyUriFormat(const dvv::Uri& parsed, FormatChoice& choice) {
    std::string_view name = parsed.path;
    if (const std::size_t slash = name.find_last_of("/\\"); slash != std::string_view::npos) {
        name.remove_prefix(slash + 1);
    }
    name = name.substr(0, name.rfind('.'));
    // <name>_WxH[_fmt]: the first size token, then a catalogue format right behind it.
    bool sized = false;
    for (std::size_t begin = 0; begin <= name.size();) {
        const std::size_t end = std::min(name.find('_', begin), name.size());
        const std::string_view token = name.substr(begin, end - begin);
        if (sized) {
            if (dvv::media::findFormat(token)) {
                choice.format = std::string(token);
            }
            break;
        }
        sized = parseSizeText(token, choice);
        begin = end + 1;
    }
    applyClipFormat(parsed, choice);
    if (const auto fmt = parsed.param("fmt"); fmt && !fmt->empty()) {
        choice.format = std::string(*fmt);
    }
    if (const auto size = parsed.param("size")) {
        static_cast<void>(parseSizeText(*size, choice));
    }
    if (const auto w = parsed.param("w")) {
        choice.width = parseDimension(*w).value_or(choice.width);
    }
    if (const auto h = parsed.param("h")) {
        choice.height = parseDimension(*h).value_or(choice.height);
    }
    if (const auto fps = parsed.param("fps")) {
        choice.fps = parseFpsText(*fps).value_or(choice.fps);
    }
}

// The URI's colour keys; none for an unparsable URI or colour (the open names the error).
dvv::media::ColourSpec colourKeys(std::string_view uri) {
    if (const dvv::Result<dvv::Uri> parsed = dvv::parseUri(uri)) {
        if (dvv::Result<dvv::media::ColourSpec> spec = dvv::media::colourSpecFromQuery(*parsed)) {
            return *spec;
        }
    }
    return {};
}

bool hasColourKeys(const dvv::media::ColourSpec& keys) noexcept {
    return keys.preset || keys.range != ColourRange::Unspecified;
}

// FileColourMemory's key: the URI without the keys applyChoice() replaces; nullopt when it does
// not parse.
std::optional<std::string> fileKey(std::string_view uri) {
    dvv::Result<dvv::Uri> parsed = dvv::parseUri(uri);
    if (!parsed) {
        return std::nullopt;
    }
    std::erase_if(parsed->params, [](const auto& param) {
        return std::ranges::find(REPLACED, param.first) != REPLACED.end();
    });
    return dvv::formatUri(*parsed);
}

ColourModel streamModel(const StreamInfo& stream) {
    return dvv::media::descriptor(stream.info.desc.format).model;
}

std::string fileNameOf(std::string_view path) {
    if (const std::size_t slash = path.find_last_of("/\\"); slash != std::string_view::npos) {
        path.remove_prefix(slash + 1);
    }
    return std::string(path);
}

// The colour group of a clip description file; nullopt when it cannot be read or has none.
std::optional<dvv::media::ColourSpec> clipColour(std::string_view utf8_path) {
    const dvv::Result<dvv::media::ClipDescription> loaded = dvv::media::loadClipJson(utf8_path);
    if (!loaded) {
        return std::nullopt;
    }
    return loaded->colour;
}

// `base` with the standard's matrix (YUV only, and not over the GBR matrix of a YUV-model
// stream: design §3.3 rule 5), transfer and primaries.
ColourParams withStandard(ColourParams base, dvv::media::ColourPreset standard,
                          ColourModel model) noexcept {
    const ColourParams fields =
        dvv::media::merged(dvv::media::ColourSpec{.preset = standard, .range = {}}, model);
    if (model == ColourModel::Yuv && base.matrix != ColourMatrix::Rgb) {
        base.matrix = fields.matrix;
        base.kr = fields.kr;
        base.kb = fields.kb;
    }
    base.transfer = fields.transfer;
    base.primaries = fields.primaries;
    return base;
}

// An "@<json>" format the provider can read (it ignores a sidecar whose
// format cannot be read); any other format text counts as readable here.
bool formatReadable(std::string_view format) {
    if (!format.starts_with('@')) {
        return true;
    }
    const dvv::Result<std::string> text = dvv::media::readSmallFile(format.substr(1));
    if (!text) {
        return false;
    }
    const auto root = dvv::json::parse(*text);
    return root && dvv::media::formatFromJson(*root).has_value();
}

}  // namespace

std::uint8_t fileColourMask(std::string_view resolved_uri, const ColourParams& own,
                            ColourModel model) {
    const ColourParams given = dvv::media::merged(colourKeys(resolved_uri), model);
    std::uint8_t mask = 0;
    if (model == ColourModel::Yuv && own.matrix != ColourMatrix::Unspecified &&
        given.matrix == ColourMatrix::Unspecified) {
        mask |= dvv::media::AssumedMatrix;
    }
    if (own.range != ColourRange::Unspecified && given.range == ColourRange::Unspecified) {
        mask |= dvv::media::AssumedRange;
    }
    if (own.transfer != TransferFn::Unspecified && given.transfer == TransferFn::Unspecified) {
        mask |= dvv::media::AssumedTransfer;
    }
    if (own.primaries != Primaries::Unspecified && given.primaries == Primaries::Unspecified) {
        mask |= dvv::media::AssumedPrimaries;
    }
    return mask;
}

void FileColourMemory::remember(const StreamInfo& stream) {
    if (hasColourKeys(colourKeys(stream.resolvedUri))) {
        return;  // the URI's keys hide the file's values
    }
    std::optional<std::string> key = fileKey(stream.resolvedUri);
    if (!key) {
        return;
    }
    const Entry entry{
        .colour = {.colour = stream.colour,
                   .fromFile = fileColourMask(stream.resolvedUri,
                                              dvv::media::colourParams(stream.info.desc.colour),
                                              streamModel(stream))},
        .base = streamColourBase(stream)};
    std::erase_if(entries_, [&key](const auto& old) { return old.first == *key; });
    if (entries_.size() >= MAX_ENTRIES) {
        entries_.erase(entries_.begin());
    }
    entries_.emplace_back(std::move(*key), entry);
}

FileColour FileColourMemory::base(const StreamInfo& stream) const {
    const ColourModel model = streamModel(stream);
    FileColour result{
        .colour = stream.colour,
        .fromFile = fileColourMask(stream.resolvedUri,
                                   dvv::media::colourParams(stream.info.desc.colour), model)};
    const std::optional<std::string> key = fileKey(stream.resolvedUri);
    const auto entry = std::ranges::find_if(
        entries_, [&key](const auto& old) { return key && old.first == *key; });
    if (entry == entries_.end()) {
        return result;
    }
    const std::uint8_t mask = entry->second.colour.fromFile;
    const ColourParams& file = entry->second.colour.colour.params;
    ColourParams& values = result.colour.params;
    const auto take = [&](dvv::media::AssumedColour bit, auto& field, auto value) {
        if ((mask & bit) != 0 && (result.fromFile & bit) == 0) {
            field = value;
            result.fromFile |= bit;
        }
    };
    if (model == ColourModel::Yuv) {
        if ((mask & dvv::media::AssumedMatrix) != 0 &&
            (result.fromFile & dvv::media::AssumedMatrix) == 0) {
            values.kr = file.kr;
            values.kb = file.kb;
        }
        take(dvv::media::AssumedMatrix, values.matrix, file.matrix);
    }
    take(dvv::media::AssumedRange, values.range, file.range);
    take(dvv::media::AssumedTransfer, values.transfer, file.transfer);
    take(dvv::media::AssumedPrimaries, values.primaries, file.primaries);
    return result;
}

ColourBase FileColourMemory::colourBase(const StreamInfo& stream) const {
    if (!hasColourKeys(colourKeys(stream.resolvedUri))) {
        return streamColourBase(stream);
    }
    const std::optional<std::string> key = fileKey(stream.resolvedUri);
    const auto entry = std::ranges::find_if(
        entries_, [&key](const auto& old) { return key && old.first == *key; });
    if (entry != entries_.end()) {
        return entry->second.base;
    }
    const std::string format(dvv::media::descriptor(stream.info.desc.format).name);
    return ColourBase{.standard = dvv::media::ColourPreset::Bt709,
                      .origin = ColourOrigin::Default,
                      .clipName = {},
                      .values = defaultColour(format, stream.info.desc.height).params,
                      .rangeFromFile = false};
}

FormatChoice choiceFromUri(std::string_view uri, FormatChoice fallback) {
    if (const dvv::Result<dvv::Uri> parsed = dvv::parseUri(uri);
        parsed && parsed->scheme == "file") {
        applyUriFormat(*parsed, fallback);
    }
    fallback.colour = colourChoiceFromUri(uri);
    return fallback;
}

FormatChoice choiceFromCandidate(const dvv::media::Candidate& candidate) {
    const dvv::media::FormatGuess& guess = candidate.guess;
    FormatChoice choice;
    if (guess.format) {
        choice.format = dvv::media::descriptor(*guess.format).name;
    }
    if (guess.width != 0 && guess.height != 0) {
        choice.width = guess.width;
        choice.height = guess.height;
    }
    if (guess.fps) {
        choice.fps = *guess.fps;
    }
    return choice;
}

FormatChoice choiceFromInfo(const dvv::media::SourceInfo& info) {
    FormatChoice choice;
    choice.format = dvv::media::descriptor(info.desc.format).name;
    if (const auto uri = dvv::parseUri(info.resolvedUri)) {
        if (const auto fmt = uri->param("fmt"); fmt && fmt->starts_with('@')) {
            choice.format = std::string(*fmt);  // a custom JSON format
        }
    }
    choice.width = info.desc.width;
    choice.height = info.desc.height;
    choice.fps = info.fps;
    choice.colour = colourChoiceFromUri(info.resolvedUri);
    return choice;
}

ColourChoice colourChoiceFromUri(std::string_view uri) {
    const dvv::media::ColourSpec keys = colourKeys(uri);
    return ColourChoice{.standard = keys.preset, .range = keys.range, .autoDetect = false};
}

ColourBase colourBaseFromUri(std::string_view uri, std::string_view format, std::uint32_t height) {
    const ColourModel model = formatModel(format);
    ColourBase base{.standard = dvv::media::ColourPreset::Bt709,
                    .origin = ColourOrigin::Default,
                    .clipName = {},
                    .values = defaultColour(format, height).params,
                    .rangeFromFile = false};
    const dvv::Result<dvv::Uri> parsed = dvv::parseUri(uri);
    if (!parsed || parsed->scheme != "file") {
        return base;
    }
    std::optional<dvv::media::ColourSpec> clip;
    std::string clip_path;
    if (const auto param = parsed->param("clip")) {
        if (param->starts_with('@') && param->size() > 1) {
            clip_path = std::string(param->substr(1));
            clip = clipColour(clip_path);
        }
    } else {
        clip_path = std::string(parsed->path) + ".json";
        // A sidecar whose "@" format cannot be read is ignored, colour included, as the provider
        // does (unless fmt= gives the format).
        const dvv::Result<dvv::media::ClipDescription> loaded = dvv::media::loadClipJson(clip_path);
        if (loaded) {
            const dvv::media::ClipDescription& description = *loaded;
            if (description.colour && (!description.format || parsed->param("fmt") ||
                                       formatReadable(*description.format))) {
                clip = description.colour;
            }
        }
    }
    if (clip && clip->preset) {
        base.standard = clip->preset;
        base.origin = ColourOrigin::Clip;
        base.clipName = fileNameOf(clip_path);
        base.values = withStandard(base.values, *clip->preset, model);
        if (clip->range != ColourRange::Unspecified) {
            base.values.range = clip->range;
            base.rangeFromFile = true;
        }
        return base;
    }
    if (const auto name = dvv::media::parseFileName(parsed->path); name && name->colour) {
        base.standard = name->colour;
        base.origin = ColourOrigin::FileName;
        base.values = withStandard(base.values, *name->colour, model);
    }
    return base;
}

ColourBase streamColourBase(const StreamInfo& stream) {
    const ColourModel model = streamModel(stream);
    const dvv::media::ResolvedColour& resolved = stream.colour;
    const bool lavfi = stream.resolvedUri.starts_with("lavfi:");
    ColourBase base{.standard = resolved.standard,
                    .origin = lavfi ? ColourOrigin::Graph : ColourOrigin::File,
                    .clipName = {},
                    .values = resolved.params,
                    .rangeFromFile = (resolved.assumed & dvv::media::AssumedRange) == 0};
    if (!resolved.standard) {
        return base;
    }
    std::uint8_t unsignalled = dvv::media::AssumedTransfer | dvv::media::AssumedPrimaries;
    if (model == ColourModel::Yuv) {
        unsignalled |= dvv::media::AssumedMatrix;
    }
    if (*resolved.standard == dvv::media::ColourPreset::Bt709 &&
        (resolved.assumed & unsignalled) == unsignalled) {
        base.origin = ColourOrigin::Default;
        return base;
    }
    if (stream.info.provider == "raw") {
        const std::optional<dvv::media::ColourSpec> clip =
            stream.info.clipFile.empty() ? std::nullopt : clipColour(stream.info.clipFile);
        if (clip && clip->preset) {
            base.origin = ColourOrigin::Clip;
            base.clipName = fileNameOf(stream.info.clipFile);
        } else {
            base.origin = ColourOrigin::FileName;
        }
    }
    return base;
}

ColourBase autoBase(ColourBase base, std::uint32_t height) {
    if (base.origin != ColourOrigin::Default) {
        return base;
    }
    const dvv::media::ColourPreset guess = dvv::media::guessStandard(height);
    const ColourModel model =
        base.values.matrix == ColourMatrix::Rgb || base.values.matrix == ColourMatrix::Unspecified
            ? ColourModel::Rgb
            : ColourModel::Yuv;
    base.standard = guess;
    base.origin = ColourOrigin::Auto;
    base.values = withStandard(base.values, guess, model);
    return base;
}

ColourChoice keyedColour(std::optional<dvv::media::ColourPreset> standard, ColourRange range,
                         const ColourBase& base) {
    const ColourRange range_key = range == base.values.range ? ColourRange::Unspecified : range;
    if (standard == base.standard && range_key == ColourRange::Unspecified) {
        return {};
    }
    return ColourChoice{.standard = standard, .range = range_key, .autoDetect = false};
}

ColourParams colourValues(const ColourChoice& colour, const ColourBase& base,
                          ColourModel model) noexcept {
    ColourParams values = base.values;
    if (colour.standard) {
        values = withStandard(values, *colour.standard, model);
    }
    if (colour.range != ColourRange::Unspecified) {
        values.range = colour.range;
    }
    return values;
}

std::string_view mismatchHint(std::optional<dvv::media::ColourPreset> standard, bool from_file,
                              std::uint32_t height) noexcept {
    if (!standard || from_file) {
        return {};
    }
    return dvv::media::sizeHint(*standard, height);
}

dvv::Result<std::string> applyChoice(std::string_view uri, const FormatChoice& choice) {
    DVV_TRY_VOID(validateChoice(choice));
    auto parsed = dvv::parseUri(uri);
    if (!parsed) {
        return dvv::makeError(dvv::Errc::Format, std::format("cannot apply the format to '{}': {}",
                                                             uri, parsed.error().message));
    }
    if (!parsed->body.empty()) {  // an opaque scheme (e.g. lavfi:) has no format parameters
        return dvv::makeError(dvv::Errc::Format,
                              std::format("cannot apply a format to a '{}:' URI", parsed->scheme));
    }
    std::erase_if(parsed->params, [](const auto& param) {
        return std::ranges::find(REPLACED, param.first) != REPLACED.end();
    });
    auto& params = parsed->params;
    params.emplace_back("fmt", choice.format);
    params.emplace_back("size", std::format("{}x{}", choice.width, choice.height));
    params.emplace_back("fps", formatFps(choice.fps));
    const std::vector<std::pair<std::string, std::string>> colour = dvv::media::colourParameters(
        dvv::media::ColourSpec{.preset = choice.colour.standard, .range = choice.colour.range});
    params.insert(params.end(), colour.begin(), colour.end());
    return dvv::formatUri(*parsed);
}

dvv::Result<void> validateChoice(const FormatChoice& choice) {
    const bool custom_json = choice.format.starts_with('@');
    std::optional<dvv::media::FormatId> format;
    if (custom_json) {
        if (choice.format.size() == 1) {
            return dvv::makeError(dvv::Errc::Format, "the custom format needs a JSON file path");
        }
    } else {
        format = dvv::media::findFormat(choice.format);
        if (!format) {
            return dvv::makeError(dvv::Errc::Format,
                                  std::format("unknown pixel format '{}'", choice.format));
        }
    }
    if (choice.width == 0 || choice.height == 0) {
        return dvv::makeError(dvv::Errc::Format,
                              std::format("bad size {}x{}: width and height must "
                                          "not be 0",
                                          choice.width, choice.height));
    }
    if (choice.fps.num <= 0 || choice.fps.den <= 0) {
        return dvv::makeError(dvv::Errc::Format,
                              std::format("bad fps {}/{}", choice.fps.num, choice.fps.den));
    }
    if (format) {
        // The JSON of a custom format is loaded when the URI is opened.
        const auto geometry = dvv::media::geometry(*format, choice.width, choice.height);
        if (!geometry) {
            return dvv::makeError(dvv::Errc::Format, geometry.error().message);
        }
    }
    return {};
}

std::optional<dvv::media::Rational> parseFpsText(std::string_view text) noexcept {
    std::int64_t num = 0;
    std::int64_t den = 1;
    bool decimal = false;
    if (const std::size_t slash = text.find('/'); slash != std::string_view::npos) {
        const std::optional<std::int64_t> n = parseDigits(text.substr(0, slash));
        const std::optional<std::int64_t> d = parseDigits(text.substr(slash + 1));
        if (!n || !d) {
            return std::nullopt;
        }
        num = *n;
        den = *d;
    } else {
        const std::size_t dot = text.find('.');
        const std::string_view whole = text.substr(0, dot);
        const std::string_view decimals =
            dot == std::string_view::npos ? std::string_view() : text.substr(dot + 1);
        if (dot != std::string_view::npos &&
            (decimals.empty() || decimals.size() > MAX_FPS_DECIMALS)) {
            return std::nullopt;
        }
        const std::optional<std::int64_t> w = parseDigits(whole);
        const std::optional<std::int64_t> f =
            decimals.empty() ? std::optional<std::int64_t>(0) : parseDigits(decimals);
        if (!w || !f) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < decimals.size(); ++i) {
            den *= 10;  // at most 10^6
        }
        if (*w > (std::numeric_limits<std::int64_t>::max() - *f) / den) {
            return std::nullopt;
        }
        num = *w * den + *f;
        decimal = !decimals.empty();
    }
    if (num <= 0 || den <= 0) {
        return std::nullopt;
    }
    const std::int64_t divisor = std::gcd(num, den);
    num /= divisor;
    den /= divisor;
    if (decimal && MILLI % den == 0 && num <= std::numeric_limits<std::int64_t>::max() / MILLI) {
        const std::int64_t milli = num * (MILLI / den);
        if (const auto ntsc = std::ranges::find(NTSC_RATES, milli, &NtscRate::milli);
            ntsc != NTSC_RATES.end()) {
            return dvv::media::Rational{ntsc->num, NTSC_DEN};
        }
    }
    return dvv::media::Rational{num, den};
}

dvv::Result<FormatJson> loadFormatJson(std::string_view utf8_path) {
    DVV_TRY(const std::string text, dvv::media::readSmallFile(utf8_path));
    const dvv::Result<dvv::json::Value> root = dvv::json::parse(text);
    if (!root) {
        return dvv::makeError(dvv::Errc::Format,
                              std::format("{}: not JSON: {}", utf8_path, root.error().message));
    }
    FormatJson loaded;
    if (!root->is_object() || !root->contains("rawreelClip")) {
        loaded.format = "@" + std::string(utf8_path);
        return loaded;
    }
    DVV_TRY(dvv::media::ClipDescription clip, dvv::media::parseClipJson(text, utf8_path));
    loaded.clip = true;
    loaded.format = std::move(clip.format);
    loaded.size = clip.size;
    loaded.fps = clip.fps;
    if (clip.colour) {
        loaded.colour = ColourChoice{
            .standard = clip.colour->preset, .range = clip.colour->range, .autoDetect = false};
    }
    return loaded;
}

std::optional<std::string> formatRefusal(std::string_view resolved_uri) {
    if (!resolved_uri.starts_with("lavfi:")) {
        return std::nullopt;
    }
    // "\xE2\x80\xA6" is U+2026 (horizontal ellipsis) in UTF-8.
    return std::string{
        "View > Format... does not apply to lavfi: streams: set the colour in the graph "
        "(setparams=\xE2\x80\xA6)"};
}

}  // namespace rawreel
