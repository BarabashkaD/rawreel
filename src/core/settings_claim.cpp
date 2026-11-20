#include "core/settings_claim.h"

#include <cstdint>
#include <string_view>

namespace rawreel {

std::uint64_t fnv1a64(std::string_view text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char byte : text) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

}  // namespace rawreel
