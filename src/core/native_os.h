#pragma once
namespace rawreel {
enum class Os : unsigned char { Windows, MacOs, Linux };
// The OS this build targets; rawreel_core's CMake sets RAWREEL_NATIVE_OS from CMAKE_SYSTEM_NAME.
inline constexpr Os NATIVE_OS = Os::RAWREEL_NATIVE_OS;
}  // namespace rawreel
