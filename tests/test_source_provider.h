#pragma once

// The Session tests' source provider (component "test", schemes slow: and fail:). Both schemes take
// the synthetic parameters w (32), h (16), fmt (yuv420p), frames (40) and fps (25), plus
//   slow:<pattern>?index=N&gate=<name>  produce(N) waits until releaseGate(name) (at most 10 s);
//   slow:<pattern>?opengate=<name>      open() waits the same way;
//   fail:<pattern>?index=N&errc=io|devicelost|oom&times=K
//                                       produce(N) fails for its first K calls (0: always);
//   fail:<pattern>?index=N&errc=...&shots=K
//                                       the first K produce(N) calls fail, counted per URI text
//                                       across every source opened from it (times is ignored).
// It also matches a file: whose path ends in ".refused" (confidence 100) and refuses it in open()
// with Unsupported "codec test is not supported in v1.0 (lossless codecs only)"; other files get
// confidence 0.
// Waits happen on the I/O executor, never on the caller's thread.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "dvv/media/types.h"

namespace rawreel::testing {

// Named gates of slow: sources (process-wide). closeGate() makes waiters block; releaseGate() lets
// every current and later waiter pass. gateWaiters() counts threads waiting right now.
void closeGate(std::string_view name);
void releaseGate(std::string_view name);
[[nodiscard]] std::size_t gateWaiters(std::string_view name);
// Waits while gate `name` is closed, at most `limit`; false on timeout. (slow: sources use it too.)
[[nodiscard]] bool passGate(std::string_view name, std::chrono::milliseconds limit);
// Clears the per-URI counters of `fail:...&shots=K` (SessionFixture's constructor calls it).
void resetShots();
// The code value a slow:/fail: source writes for component c at (x, y) of its grid, frame `index`.
[[nodiscard]] std::uint16_t testCode(dvv::media::FormatId format, std::size_t component,
                                     std::uint32_t x, std::uint32_t y, std::int64_t index);

}  // namespace rawreel::testing
