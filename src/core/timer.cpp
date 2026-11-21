#include "core/timer.h"

#include <chrono>
#include <memory>

namespace rawreel {
namespace {

class SteadyClock final : public IClock {
public:
    [[nodiscard]] Clock::time_point now() const override { return Clock::now(); }
};

}  // namespace

std::shared_ptr<IClock> steadyClock() {
    return std::make_shared<SteadyClock>();
}

}  // namespace rawreel
