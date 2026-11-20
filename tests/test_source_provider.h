#pragma once

// The Session tests' source provider (component "test", schemes slow: and fail:). Both schemes take
// the synthetic parameters w (32), h (16), fmt (yuv420p), frames (40) and fps (25), plus
//   slow:<pattern>?index=N&gate=<name>  produce(N) waits until releaseGate(name) (at most 10 s),
//                                       or ends Errc::Cancelled when its stop token stops;
//   slow:<pattern>?opengate=<name>      open() waits until releaseGate(name) (at most 10 s);
//   slow:<pattern>?openstop=<name>      open() waits until its stop token stops (at most 5 s), then
//                                       ends Errc::Cancelled; past the bound it ends
//                                       Errc::Internal "open was not stopped";
//   fail:<pattern>?index=N&errc=io|devicelost|oom&times=K
//                                       produce(N) fails for its first K calls (0: always);
//   fail:<pattern>?index=N&errc=...&shots=K
//                                       the first K produce(N) calls fail, counted per URI text
//                                       across every source opened from it (times is ignored).
// It also matches a file: whose path ends in ".refused" (confidence 100) and refuses it in open()
// with Unsupported "codec test is not supported (lossless codecs only)"; other files get
// confidence 0.
// Waits happen on the I/O executor, never on the caller's thread.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "dvv/media/types.h"

namespace rawreel::testing {

// Named gates of slow: sources (process-wide). closeGate() makes waiters block; releaseGate() lets
// every current and later waiter pass. gateWaiters() counts threads waiting right now, openstop
// waits included. closeGate() also clears the name's outcome.
void closeGate(std::string_view name);
void releaseGate(std::string_view name);
[[nodiscard]] std::size_t gateWaiters(std::string_view name);
// Waits while gate `name` is closed, at most `limit`; false on timeout. (slow: sources use it too.)
[[nodiscard]] bool passGate(std::string_view name, std::chrono::milliseconds limit);
// How the last wait of a gate= produce or an openstop= open under `name` ended: "" while none has
// ended since closeGate(name), "passed", "cancelled" (its token stopped), or its timeout text.
[[nodiscard]] std::string waitOutcome(std::string_view name);
// Clears the per-URI counters of `fail:...&shots=K` (SessionFixture's constructor calls it).
void resetShots();
// The code value a slow:/fail: source writes for component c at (x, y) of its grid, frame `index`.
[[nodiscard]] std::uint16_t testCode(dvv::media::FormatId format, std::size_t component,
                                     std::uint32_t x, std::uint32_t y, std::int64_t index);

}  // namespace rawreel::testing
