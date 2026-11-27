#pragma once

// The frame set one Session publishes and its display sink. A FrameSet holds counted read leases
// per view role (the display image, the planes, the raw data); each holder has its own shares, so
// the presenter thread can keep using a set while a newer one arrives. Only std headers and the
// lease type: the render components may use it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <source_location>

#include "dvv/media/frame_store.h"

namespace rawreel {

inline constexpr std::uint32_t MAX_STREAMS = 1;  // D2: one stream at a time; v1.x tiles raise it

// What a lease shows: the picture the viewport draws, the plane views, the unconverted data.
enum class ViewRole : std::uint8_t { Display, Planes, Raw };
inline constexpr std::size_t VIEW_ROLE_COUNT = 3;

// A set of roles.
struct ViewRoles {
    std::uint8_t bits = 0;

    [[nodiscard]] static constexpr ViewRoles of(ViewRole role) noexcept {
        return ViewRoles{static_cast<std::uint8_t>(1U << static_cast<unsigned>(role))};
    }
    [[nodiscard]] static constexpr ViewRoles all() noexcept {
        return ViewRoles{static_cast<std::uint8_t>((1U << VIEW_ROLE_COUNT) - 1U)};
    }
    [[nodiscard]] constexpr bool has(ViewRole role) const noexcept {
        return (bits & of(role).bits) != 0;
    }
    [[nodiscard]] constexpr ViewRoles operator|(ViewRoles other) const noexcept {
        return ViewRoles{static_cast<std::uint8_t>(bits | other.bits)};
    }
    [[nodiscard]] constexpr bool operator==(const ViewRoles&) const noexcept = default;
};

using RoleLeases = std::array<dvv::media::ReadLease, MAX_STREAMS>;

// One shown frame: the leases of every stream for one index, per role (one stream at a time; the
// array keeps the v1.x tile shape, D2). FrameSet is move-only (ReadLease is).
struct FrameSet {
    std::int64_t index = -1;
    std::uint32_t count = 0;
    std::array<RoleLeases, VIEW_ROLE_COUNT> leases{};  // each holder has its own leases
    std::uint64_t epoch = 0;                           // the Session's stackEpoch (D7)
    std::uint64_t streamSet = 0;                       // the stream set the frame belongs to

    [[nodiscard]] const dvv::media::ReadLease& lease(ViewRole role,
                                                     std::size_t stream = 0) const noexcept {
        return leases[static_cast<std::size_t>(role)][stream];
    }
    // A second, counted set on the leases of `roles` (index, count, epoch, streamSet copied),
    // listed in outstandingLeases() at `where`. The other roles stay empty.
    [[nodiscard]] FrameSet share(
        ViewRoles roles = ViewRoles::all(),
        std::source_location where = std::source_location::current()) const {
        FrameSet shared{
            .index = index, .count = count, .leases = {}, .epoch = epoch, .streamSet = streamSet};
        for (std::size_t r = 0; r < VIEW_ROLE_COUNT; ++r) {
            if (!roles.has(static_cast<ViewRole>(r))) {
                continue;
            }
            for (std::size_t s = 0; s < MAX_STREAMS; ++s) {
                shared.leases[r][s] = leases[r][s].share(where);
            }
        }
        return shared;
    }
};

class IDisplaySink {  // thread-safe, non-blocking (the FrameFeed)
public:
    virtual ~IDisplaySink() = default;
    virtual void show(FrameSet frames) = 0;
    virtual void clear() = 0;  // drops every lease it holds
};

}  // namespace rawreel
