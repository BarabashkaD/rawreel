#include <doctest/doctest.h>

#include "core/native_os.h"

static_assert(rawreel::NATIVE_OS == rawreel::Os::Linux);

TEST_CASE("native os: the build's OS") {
    CHECK(rawreel::NATIVE_OS == rawreel::Os::Linux);
}
