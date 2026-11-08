#pragma once

// The developer option --debug-seek-delay: a source that waits before producing each frame, so a
// seek takes long enough to watch the transport bar.

#include <memory>

#include "core/session_types.h"
#include "dvv/base/executor.h"
#include "dvv/media/source.h"

namespace rawreel {

// produce() reads delay->get() once. At 0 it forwards at once (no thread hop, so the perf scenario
// is unchanged); else it hops to `io`, calls delay->waitFor(ms), then forwards. info() forwards.
[[nodiscard]] std::unique_ptr<dvv::media::ISource> delayedSource(
    std::unique_ptr<dvv::media::ISource> inner, std::shared_ptr<ProduceDelay> delay,
    std::shared_ptr<dvv::IExecutor> io);

}  // namespace rawreel
