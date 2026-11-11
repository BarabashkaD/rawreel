#pragma once

// The shown name, the full path and the window title of a stream (spec §4, §5). Wx-free.
// The shown name of a file: URI is its base name (after the last '/' or '\\', no query); of any
// other URI the scheme, ':' and the path (or the opaque body); UTF-8 stays as it is; an unparsable
// text is returned as it is. The full path of a file: URI is absolute: a relative path is joined
// to the base folder, '.' and '..' segments are resolved lexically, and the Windows style writes
// backslashes; the work is on strings, so both styles work on any OS. Any other URI gives the
// resolved URI as it is, with its query. The window title is the name alone on macOS, "name -
// rawreel" on Windows, "name <em dash> rawreel" on Linux; without a name it is "rawreel".

#include <cstdint>
#include <string>
#include <string_view>

namespace rawreel {

enum class PathStyle : std::uint8_t { Posix, Windows };
enum class TitleStyle : std::uint8_t { MacOs, Windows, Linux };

#ifdef _WIN32
inline constexpr PathStyle NATIVE_PATH_STYLE = PathStyle::Windows;
inline constexpr TitleStyle NATIVE_TITLE_STYLE = TitleStyle::Windows;
#elif defined(__APPLE__)
inline constexpr PathStyle NATIVE_PATH_STYLE = PathStyle::Posix;
inline constexpr TitleStyle NATIVE_TITLE_STYLE = TitleStyle::MacOs;
#else
inline constexpr PathStyle NATIVE_PATH_STYLE = PathStyle::Posix;
inline constexpr TitleStyle NATIVE_TITLE_STYLE = TitleStyle::Linux;
#endif

[[nodiscard]] std::string streamName(std::string_view resolved_uri);
[[nodiscard]] std::string streamFullPath(std::string_view resolved_uri,
                                         std::string_view base_folder, PathStyle style);
[[nodiscard]] std::string windowTitle(std::string_view name, TitleStyle style);

}  // namespace rawreel
