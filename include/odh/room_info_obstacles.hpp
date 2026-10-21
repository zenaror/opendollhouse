#pragma once

#include "odh/collision.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace odh {

enum class RoomInfoObstacleCollection : std::uint8_t { static_set, dynamic_list };
enum class RoomInfoObstacleIndexStatus : std::uint8_t { complete, unsupported };
enum class RoomInfoObstacleIndexReason : std::uint8_t {
    none,
    inventory_incomplete,
    inventory_over_budget,
    owner_identity_invalid,
    owner_missing,
    room_invalid,
    geometry_invalid,
};

struct RoomInfoObstacle {
    std::int32_t owner_id = 0;
    std::uint16_t room = 0;
    CollisionBox box{};
    RoomInfoObstacleCollection collection = RoomInfoObstacleCollection::static_set;
};

struct RoomInfoObstacleQuery {
    RoomInfoObstacleIndexStatus status = RoomInfoObstacleIndexStatus::unsupported;
    RoomInfoObstacleIndexReason reason = RoomInfoObstacleIndexReason::inventory_incomplete;
    std::vector<RoomInfoObstacle> intersections;
};

// A bounded owner-keyed model of RoomInfo.StaticObstacles/DynamicObstacles.
// Membership and geometry must come from explicit runtime events or a complete
// enumeration; saved geometry alone must not be used to mark this complete.
class RoomInfoObstacleIndex {
public:
    explicit RoomInfoObstacleIndex(std::size_t limit = 4096) noexcept;

    RoomInfoObstacleIndexStatus status() const noexcept { return status_; }
    RoomInfoObstacleIndexReason reason() const noexcept { return reason_; }
    std::size_t size() const noexcept { return obstacles_.size(); }

    // Replaces the whole index atomically. An incomplete source publishes no
    // partial results. IDs must be unique and boxes normalized/nonempty.
    bool rebuild(const std::vector<RoomInfoObstacle>&, bool source_complete);
    void mark_incomplete(RoomInfoObstacleIndexReason =
                         RoomInfoObstacleIndexReason::inventory_incomplete) noexcept;

    bool register_footprint(const RoomInfoObstacle&);
    bool move_footprint(std::int32_t owner_id, std::uint16_t room,
                        CollisionBox box);
    bool set_collection(std::int32_t owner_id, RoomInfoObstacleCollection);
    bool unregister_footprint(std::int32_t owner_id);

    // Mirrors GetAvatarPlace's intersection query: static membership includes
    // the target; dynamic membership excludes it. Results are owner-ID sorted.
    RoomInfoObstacleQuery query(std::uint16_t room, const CollisionBox& candidate,
                                std::int32_t target_id) const;

private:
    bool valid(const RoomInfoObstacle&) const noexcept;
    void invalidate(RoomInfoObstacleIndexReason) noexcept;

    std::size_t limit_;
    RoomInfoObstacleIndexStatus status_ = RoomInfoObstacleIndexStatus::unsupported;
    RoomInfoObstacleIndexReason reason_ = RoomInfoObstacleIndexReason::inventory_incomplete;
    std::vector<RoomInfoObstacle> obstacles_;
};

const char* room_info_obstacle_index_status_name(RoomInfoObstacleIndexStatus) noexcept;
const char* room_info_obstacle_index_reason_name(RoomInfoObstacleIndexReason) noexcept;

} // namespace odh
