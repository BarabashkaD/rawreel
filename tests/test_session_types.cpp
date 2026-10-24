#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "core/session_types.h"

namespace {

class Sink final : public rawreel::IDisplaySink {
public:
    void show(rawreel::FrameSet frames) override { last = frames.index; }
    void clear() override { last = -1; }
    std::int64_t last = -1;
};

class Events final : public rawreel::ISessionEvents {
public:
    void streamsChanged(std::vector<rawreel::StreamInfo> streams) override {
        count = static_cast<int>(streams.size());
    }
    void formatNeeded(std::uint32_t /*slot*/, dvv::media::SourceRequest /*request*/,
                      dvv::media::Ambiguous /*candidates*/, std::string /*reason*/) override {
        ++count;
    }
    void stackChanged(std::weak_ptr<dvv::gpu::ExecutionContext> /*context*/,
                      std::uint64_t /*epoch*/) override {
        ++count;
    }
    void error(dvv::Error /*error*/) override { ++count; }
    int count = 0;
};

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("session_types: defaults and interfaces") {
        const rawreel::FrameSet frames{};
        CHECK(frames.index == -1);
        CHECK(frames.count == 0);
        CHECK(frames.epoch == 0);
        for (const auto& view : frames.views) {
            CHECK_FALSE(view);
        }
        static_assert(rawreel::MAX_STREAMS == 1);
        static_assert(std::tuple_size_v<decltype(rawreel::FrameSet::views)> == 1);
        CHECK_FALSE(rawreel::SessionStatus{}.gpuAvailable);

        Sink sink;
        rawreel::IDisplaySink& display = sink;
        display.show(rawreel::FrameSet{.index = 7});
        CHECK(sink.last == 7);
        display.clear();
        CHECK(sink.last == -1);

        Events events;
        rawreel::ISessionEvents& session_events = events;
        session_events.streamsChanged({rawreel::StreamInfo{}});
        session_events.stackChanged({}, 1);
        session_events.error(dvv::Error{});
        CHECK(events.count == 3);
    }
}
