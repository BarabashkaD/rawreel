#include "core/drop.h"

#include "dvv/base/uri.h"
#include "dvv/media/types.h"

namespace rawreel {

std::string fileUri(const std::filesystem::path& path) {
    return "file:" + dvv::percentEncode(dvv::media::utf8FromPath(path));
}

DropPlan routeDrop(std::span<const std::filesystem::path> files) {
    if (files.empty()) {
        return {};
    }
    // No file is opened, read or checked here: an unknown size or format is the Session's
    // formatNeeded (D21).
    return {.uri = fileUri(files.front()), .ignored = files.size() - 1};
}

}  // namespace rawreel
