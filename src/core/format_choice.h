#pragma once

// The format dialog's model (design M3 v2 D21, D5 design §1.8 and §4.3, D5R design §3 and §4.4):
// what the user picks for a source whose format is unknown or wrong, and how the pick is written
// into the source URI.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/session_types.h"
#include "dvv/base/error.h"
#include "dvv/media/colour_preset.h"
#include "dvv/media/display.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/source.h"
#include "dvv/media/source_factory.h"
#include "dvv/media/types.h"

namespace rawreel {

// Where the colour standard that applies without a colour key comes from (D5R §4.4, §4.5).
enum class ColourOrigin : std::uint8_t {
    Default,   // nothing gives a colour: BT.709
    File,      // container tags (or a file colour that matches no standard)
    FileName,  // the raw file name's colour token (dvv::media::parseFileName)
    Clip,      // the clip description (clip=@ or the sidecar), ColourBase::clipName
    Graph,     // a lavfi graph
    Auto,      // Auto detect: dvv::media::guessStandard(height)
};

// What applies without a colour key: the suffixed entry of the Colour list (D5R §4.4).
struct ColourBase {
    // nullopt: the file's colour matches no standard ("From file (no standard)").
    std::optional<dvv::media::ColourPreset> standard = dvv::media::ColourPreset::Bt709;
    ColourOrigin origin = ColourOrigin::Default;
    std::string clipName{};  // ColourOrigin::Clip: the clip description's file name
    // The four values in use without a key (the matrix Rgb on RGB and gray formats).
    dvv::media::ColourParams values{};
    bool rangeFromFile = false;  // values.range comes from the file (or the clip description)
    bool operator==(const ColourBase&) const = default;
};

// The Colour part of a pick (D5R §4.4): the URI's colour keys.
struct ColourChoice {
    std::optional<dvv::media::ColourPreset> standard{};  // colour=<token>; nullopt: no colour key
    dvv::media::ColourRange range{};                     // range=<r>; Unspecified: no range key
    // The standard was guessed from the height (Auto detect); the entry shows " (auto)".
    // applyChoice() ignores it.
    bool autoDetect = false;
    bool operator==(const ColourChoice&) const = default;
};

struct FormatChoice {
    std::string format = "yuv420p";  // catalogue name or "@<UTF-8 path to .json>"
    std::uint32_t width = 1920, height = 1080;
    dvv::media::Rational fps{25, 1};
    ColourChoice colour{};
    bool operator==(const FormatChoice&) const = default;
};

// Format, size and fps of the candidate; no colour key.
[[nodiscard]] FormatChoice choiceFromCandidate(const dvv::media::Candidate& candidate);
// View > Format: the stream's format ("@<path>" when the resolved URI names one), size and fps;
// the colour keys of info.resolvedUri (colourChoiceFromUri).
[[nodiscard]] FormatChoice choiceFromInfo(const dvv::media::SourceInfo& info);
// The format dialog's prefill for a request URI (D5 design §4.4): for a file: URI,
// `fallback` with the size and catalogue format of the file name (<name>_WxH[_fmt]...), then the
// format, size and fps of the clip description of clip=@<path> (dvv::media::loadClipJson; an
// error is ignored, the open names it), then the query's fmt, size (WxH), w, h and
// fps on top (design §4.2 order); `fallback`'s format, size and fps for other URIs. The colour is
// colourChoiceFromUri(uri): no colour key gives none, never the fallback's.
[[nodiscard]] FormatChoice choiceFromUri(std::string_view uri, FormatChoice fallback);
// The URI's colour= and range= (D5R §4.4 "Reading a URI"); none for an unparsable URI or colour.
[[nodiscard]] ColourChoice colourChoiceFromUri(std::string_view uri);

// The base of a request URI (the open path, D5R §4.4): for a file: URI the colour of the clip
// description of clip=@<path>, else of the sidecar <path>.json, else the file name's colour token,
// else the BT.709 default; any other URI: the default. A clip description's range is "from file".
// `format` and `height` give the model and the default values (defaultColour()).
[[nodiscard]] ColourBase colourBaseFromUri(std::string_view uri, std::string_view format,
                                           std::uint32_t height);
// The base of an open stream opened without colour keys: StreamInfo::colour's standard and
// values. Origin: Default when nothing gave a colour (BT.709 with the matrix, transfer and
// primaries assumed), Graph for lavfi, for the "raw" provider Clip when the clip description
// (SourceInfo::clipFile) gives a colour, else FileName; else File.
[[nodiscard]] ColourBase streamColourBase(const StreamInfo& stream);
// Auto detect: a Default base becomes dvv::media::guessStandard(height) with origin Auto
// (its matrix, transfer and primaries; the range stays); any other base is returned as it is.
[[nodiscard]] ColourBase autoBase(ColourBase base, std::uint32_t height);

// What OK writes (D5R §4.4): no colour key when `standard` and `range` equal the base (for a base
// without a standard, `standard` nullopt); else colour=<standard> (when given) plus range=<range>
// when it differs from base.values.range.
[[nodiscard]] ColourChoice keyedColour(std::optional<dvv::media::ColourPreset> standard,
                                       dvv::media::ColourRange range, const ColourBase& base);
// The four values `colour` renders with: the base's values; a standard replaces matrix (YUV only;
// RGB and gray keep the base's matrix), transfer and primaries; a range key replaces the range.
[[nodiscard]] dvv::media::ColourParams colourValues(const ColourChoice& colour,
                                                    const ColourBase& base,
                                                    dvv::media::ColourModel model) noexcept;
// The size-mismatch hint for a standard that no file gives (Default, Auto or a colour=
// key): dvv::media::sizeHint(standard, height); "" otherwise.
[[nodiscard]] std::string_view mismatchHint(std::optional<dvv::media::ColourPreset> standard,
                                            bool from_file, std::uint32_t height) noexcept;

// The dvv::media::AssumedColour bits of the fields a file gives (D5 design §1.5):
// every field `own` (the stream's own colour, SourceInfo::desc.colour) sets that
// dvv::media::merged(<the URI's colour keys>, model) leaves Unspecified. Never the matrix bit for
// RGB and gray (the matrix is not used). An unparsable URI or colour counts as no colour key.
[[nodiscard]] std::uint8_t fileColourMask(std::string_view resolved_uri,
                                          const dvv::media::ColourParams& own,
                                          dvv::media::ColourModel model);

// What a file gives for entry 0 of the Colour list (View > Format): the values in use and the
// dvv::media::AssumedColour bits of the fields the file gives.
struct FileColour {
    dvv::media::ResolvedColour colour{};
    std::uint8_t fromFile = 0;
};

// The file's own colour per stream. The providers merge the URI's colour keys
// over the file's colour, so a stream opened with colour keys no longer shows what the file gives.
// remember() keeps the colour of each stream opened without colour keys, by its resolved URI
// without fmt, size, w, h, fps and the colour keys; base() then fills the fields the URI's keys
// hide from that entry. A stream first opened with colour keys has no entry (known limit).
class FileColourMemory {
public:
    static constexpr std::size_t MAX_ENTRIES = 32;  // the oldest entry goes first

    void remember(const StreamInfo& stream);
    // The stream's colour with fileColourMask(); with an entry for the stream's file, each field
    // the entry gives that the stream does not (on RGB and gray never the matrix) is taken from
    // the entry and marked "from file".
    [[nodiscard]] FileColour base(const StreamInfo& stream) const;
    // streamColourBase() for a stream without colour keys; with keys the remembered entry's base,
    // else the BT.709 default with the stream's model (known limit).
    [[nodiscard]] ColourBase colourBase(const StreamInfo& stream) const;

private:
    struct Entry {
        FileColour colour{};
        ColourBase base{};
    };
    std::vector<std::pair<std::string, Entry>> entries_{};
};

// Removes fmt, size, w, h, fps, colour, range and the old keys matrix, kr, kb, transfer and
// primaries from the URI's query, keeps every other parameter (clip, impl, ...) in order, then
// appends fmt, size (WxH), fps (N or N/D), colour=<token> (when given) and range=<r> (when not
// Unspecified). Formats with dvv::formatUri. Errc::Format for an unparsable or opaque URI or an
// invalid choice.
[[nodiscard]] dvv::Result<std::string> applyChoice(std::string_view uri,
                                                   const FormatChoice& choice);
// Errc::Format: catalogue name unknown; geometry() of the format at the size fails; "@" with an
// empty path; width or height 0; fps num <= 0 or den <= 0.
[[nodiscard]] dvv::Result<void> validateChoice(const FormatChoice& choice);
// The format dialog's fps field: "25" -> 25/1, "30000/1001", a decimal (up to 6 decimals) reduced
// by the gcd ("25.5" -> 51/2), except the NTSC decimals 23.976 and 23.98, 29.97, 47.952, 59.94 and
// 119.88 (by value, trailing zeros allowed), which give N*1000/1001 (D5 design §1.4). nullopt for
// empty text, 0, negatives, a zero denominator, an overflow or any other character.
[[nodiscard]] std::optional<dvv::media::Rational> parseFpsText(std::string_view text) noexcept;

// What Load JSON... of the format dialog found in a file (D5 design §1.7): the groups it gives.
struct FormatJson {
    bool clip = false;  // a clip description ({"rawreelClip": ...}), else a pixel-format JSON
    std::optional<std::string> format{};                            // a catalogue name or "@<path>"
    std::optional<std::pair<std::uint32_t, std::uint32_t>> size{};  // width, height
    std::optional<dvv::media::Rational> fps{};
    std::optional<ColourChoice> colour{};  // the standard and the range of the colour group
};
// Reads the file (dvv::media::readSmallFile) and detects its kind by the top key rawreelClip: a
// clip description is parsed by dvv::media::parseClipJson (its errors as they are); any other JSON
// is a pixel-format JSON and gives only the format "@<utf8_path>" (checked when the stream opens).
// Errc::Format "<utf8_path>: not JSON: <parser message>" for a file that is not JSON.
[[nodiscard]] dvv::Result<FormatJson> loadFormatJson(std::string_view utf8_path);

// View -> Format... refusal (design M4 v2 D28): for a resolved URI starting with "lavfi:" the
// message "View > Format... does not apply to lavfi: streams: set the colour in the graph
// (setparams=…)"; nullopt for every other URI (the format dialog applies).
[[nodiscard]] std::optional<std::string> formatRefusal(std::string_view resolved_uri);

}  // namespace rawreel
