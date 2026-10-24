#pragma once

// The media clock's time sources (design M3 D11, §4.3): the steady clock and ThreadTimer, a timer
// with one pending deadline on its own std::jthread. Tests use fakes (tests/session_fixture.h).

#include <memory>

#include "core/session_types.h"
#include "dvv/base/error.h"

namespace rawreel {

[[nodiscard]] std::shared_ptr<IClock> steadyClock();
// ThreadTimer (ITimer): schedule() replaces the pending deadline; the callback runs once on the
// timer's own thread when the deadline has passed and must only post work (it must not throw).
// cancel() blocks until a running callback has returned and throws std::logic_error when called
// from inside a callback; the destructor cancels. Windows waits on a high-resolution waitable
// timer, Linux on a condition variable. Errc::Internal when the Windows timer or event cannot be
// created or the thread cannot start.
[[nodiscard]] dvv::Result<std::shared_ptr<ITimer>> makeThreadTimer();

}  // namespace rawreel
