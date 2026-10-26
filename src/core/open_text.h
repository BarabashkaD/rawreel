#pragma once

// wx-free helpers of the GUI's open paths (M4 GUI D6, D7, D4b).

#include <filesystem>
#include <string>
#include <string_view>

#include "dvv/base/error.h"

namespace rawreel {

// The folder of the file a URI opens (a file: URI or a scheme-less path, dvv::parseUri), for the
// file dialogs' start folder (D6), absolute: a relative path or a bare file name is read against
// the working directory. Empty for other schemes or a text that does not parse.
[[nodiscard]] std::filesystem::path uriFolder(std::string_view text);

// `text` without CR and LF: Enter in the multi-line Open URI field inserts a line break (D7).
[[nodiscard]] std::string withoutLineBreaks(std::string_view text);

// The Open URI field's value as it is opened: withoutLineBreaks() of `text` without the leading
// and trailing white space (space, tab, CR, LF, VT, FF) of a pasted URI.
[[nodiscard]] std::string cleanUriText(std::string_view text);

// True when `text` starts with a URI scheme as dvv::parseUri reads it: two or more characters,
// a letter first, then letters, digits, '+', '-' or '.', then ':'. "X:\..." is a drive.
[[nodiscard]] bool hasScheme(std::string_view text) noexcept;

// `text` + "; no scheme: opened as a file path; a URI needs e.g. synthetic: or lavfi:" when the
// opened text `typed` has no scheme, contains '=' or '&' or '?' with no '/' or '\' before the
// first of them, and failed with Errc::Io (the file is missing, D7 C); `text` otherwise.
[[nodiscard]] std::string withSchemeHint(std::string text, std::string_view typed,
                                         const dvv::Error& error);

// True when the last path segment of `path` has a Y4M or container extension (.y4m, .avi,
// .mkv, .mov, .mp4, .nut, .webm, ...; case-insensitive), i.e. a file the Y4M or the ffmpeg
// provider recognises.
[[nodiscard]] bool isContainerName(std::string_view path);

// True when the open error of `uri` offers the infobar's "Format..." button (D4b): a file: URI
// (or a scheme-less path) whose open failed with Errc::Format or matched no provider
// (dvv::media::isNoProviderMatch, e.g. ?fmt= without a size), with `impl=raw`, or without `impl`
// and not a container name (isContainerName). A missing file (Io), a refused codec (Unsupported,
// D4), other schemes, container providers (impl=y4m, impl=ffmpeg) and recognised container or
// Y4M files get none.
[[nodiscard]] bool offersFormat(std::string_view uri, const dvv::Error& error);

// `uri` with its `impl` parameter replaced by impl=raw (dvv::formatUri), so a choice of the raw
// format dialog reaches the raw provider. Unchanged when it is no file: URI.
[[nodiscard]] std::string withRawImpl(std::string_view uri);

}  // namespace rawreel
