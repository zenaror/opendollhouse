#include "odh/room_info_obstacles.hpp"

#include <algorithm>
#include <limits>

namespace odh {
namespace {
bool valid_box(const CollisionBox& box) noexcept {
    return box.x1 < box.x2 && box.y1 < box.y2;
}
} // namespace

RoomInfoObstacleIndex::RoomInfoObstacleIndex(std::size_t limit) noexcept
    : limit_(limit) {
    if (limit_ == 0 || limit_ > 4096)
        reason_ = RoomInfoObstacleIndexReason::inventory_over_budget;
}

bool RoomInfoObstacleIndex::valid(const RoomInfoObstacle& obstacle) const noexcept {
    return obstacle.owner_id > 0 &&
        obstacle.owner_id <= std::numeric_limits<std::int16_t>::max() &&
        obstacle.room <= 32767 && valid_box(obstacle.box) &&
        (obstacle.collection == RoomInfoObstacleCollection::static_set ||
         obstacle.collection == RoomInfoObstacleCollection::dynamic_list);
}

void RoomInfoObstacleIndex::invalidate(RoomInfoObstacleIndexReason reason) noexcept {
    obstacles_.clear();
    status_ = RoomInfoObstacleIndexStatus::unsupported;
    reason_ = reason;
}

void RoomInfoObstacleIndex::mark_incomplete(RoomInfoObstacleIndexReason reason) noexcept {
    invalidate(reason == RoomInfoObstacleIndexReason::none
                   ? RoomInfoObstacleIndexReason::inventory_incomplete
                   : reason);
}

bool RoomInfoObstacleIndex::rebuild(const std::vector<RoomInfoObstacle>& source,
                                    bool source_complete) {
    if (!source_complete) {
        invalidate(RoomInfoObstacleIndexReason::inventory_incomplete);
        return false;
    }
    if (limit_ == 0 || limit_ > 4096 || source.size() > limit_) {
        invalidate(RoomInfoObstacleIndexReason::inventory_over_budget);
        return false;
    }

    auto candidate = source;
    for (const auto& obstacle : candidate) {
        if (!valid(obstacle)) {
            const auto reason = obstacle.owner_id <= 0 ||
                                obstacle.owner_id > std::numeric_limits<std::int16_t>::max()
                ? RoomInfoObstacleIndexReason::owner_identity_invalid
                : obstacle.room > 32767 ? RoomInfoObstacleIndexReason::room_invalid
                                         : RoomInfoObstacleIndexReason::geometry_invalid;
            invalidate(reason);
            return false;
        }
    }
    std::sort(candidate.begin(), candidate.end(), [](const auto& left, const auto& right) {
        return left.owner_id < right.owner_id;
    });
    for (std::size_t i = 1; i < candidate.size(); ++i) {
        if (candidate[i - 1].owner_id == candidate[i].owner_id) {
            invalidate(RoomInfoObstacleIndexReason::owner_identity_invalid);
            return false;
        }
    }
    obstacles_ = std::move(candidate);
    status_ = RoomInfoObstacleIndexStatus::complete;
    reason_ = RoomInfoObstacleIndexReason::none;
    return true;
}

bool RoomInfoObstacleIndex::register_footprint(const RoomInfoObstacle& obstacle) {
    if (status_ != RoomInfoObstacleIndexStatus::complete) return false;
    if (!valid(obstacle)) {
        invalidate(obstacle.owner_id <= 0 ||
                   obstacle.owner_id > std::numeric_limits<std::int16_t>::max()
            ? RoomInfoObstacleIndexReason::owner_identity_invalid
            : obstacle.room > 32767 ? RoomInfoObstacleIndexReason::room_invalid
                                     : RoomInfoObstacleIndexReason::geometry_invalid);
        return false;
    }
    const auto found = std::lower_bound(obstacles_.begin(), obstacles_.end(), obstacle.owner_id,
        [](const auto& current, std::int32_t id) { return current.owner_id < id; });
    if (found != obstacles_.end() && found->owner_id == obstacle.owner_id) {
        *found = obstacle;
    } else {
        if (obstacles_.size() >= limit_) {
            invalidate(RoomInfoObstacleIndexReason::inventory_over_budget);
            return false;
        }
        obstacles_.insert(found, obstacle);
    }
    return true;
}

bool RoomInfoObstacleIndex::move_footprint(std::int32_t owner_id, std::uint16_t room,
                                           CollisionBox box) {
    if (status_ != RoomInfoObstacleIndexStatus::complete) return false;
    if (owner_id <= 0 || owner_id > std::numeric_limits<std::int16_t>::max()) {
        invalidate(RoomInfoObstacleIndexReason::owner_identity_invalid);
        return false;
    }
    const auto found = std::lower_bound(obstacles_.begin(), obstacles_.end(), owner_id,
        [](const auto& current, std::int32_t id) { return current.owner_id < id; });
    if (found == obstacles_.end() || found->owner_id != owner_id) {
        invalidate(RoomInfoObstacleIndexReason::owner_missing);
        return false;
    }
    auto moved = *found;
    moved.room = room;
    moved.box = box;
    if (!valid(moved)) {
        invalidate(room > 32767 ? RoomInfoObstacleIndexReason::room_invalid
                                : RoomInfoObstacleIndexReason::geometry_invalid);
        return false;
    }
    *found = moved;
    return true;
}

bool RoomInfoObstacleIndex::set_collection(std::int32_t owner_id,
                                           RoomInfoObstacleCollection collection) {
    if (status_ != RoomInfoObstacleIndexStatus::complete) return false;
    if (owner_id <= 0 || owner_id > std::numeric_limits<std::int16_t>::max()) {
        invalidate(RoomInfoObstacleIndexReason::owner_identity_invalid);
        return false;
    }
    const auto found = std::lower_bound(obstacles_.begin(), obstacles_.end(), owner_id,
        [](const auto& current, std::int32_t id) { return current.owner_id < id; });
    if (found == obstacles_.end() || found->owner_id != owner_id) {
        invalidate(RoomInfoObstacleIndexReason::owner_missing);
        return false;
    }
    if (collection != RoomInfoObstacleCollection::static_set &&
        collection != RoomInfoObstacleCollection::dynamic_list) {
        invalidate(RoomInfoObstacleIndexReason::geometry_invalid);
        return false;
    }
    found->collection = collection;
    return true;
}

bool RoomInfoObstacleIndex::unregister_footprint(std::int32_t owner_id) {
    if (status_ != RoomInfoObstacleIndexStatus::complete) return false;
    if (owner_id <= 0 || owner_id > std::numeric_limits<std::int16_t>::max()) {
        invalidate(RoomInfoObstacleIndexReason::owner_identity_invalid);
        return false;
    }
    const auto found = std::lower_bound(obstacles_.begin(), obstacles_.end(), owner_id,
        [](const auto& current, std::int32_t id) { return current.owner_id < id; });
    if (found != obstacles_.end() && found->owner_id == owner_id)
        obstacles_.erase(found);
    return true;
}

RoomInfoObstacleQuery RoomInfoObstacleIndex::query(
    std::uint16_t room, const CollisionBox& candidate, std::int32_t target_id) const {
    RoomInfoObstacleQuery result;
    if (status_ != RoomInfoObstacleIndexStatus::complete) {
        result.reason = reason_;
        return result;
    }
    if (room > 32767) {
        result.reason = RoomInfoObstacleIndexReason::room_invalid;
        return result;
    }
    if (target_id <= 0 || target_id > std::numeric_limits<std::int16_t>::max()) {
        result.reason = RoomInfoObstacleIndexReason::owner_identity_invalid;
        return result;
    }
    if (!valid_box(candidate)) {
        result.reason = RoomInfoObstacleIndexReason::geometry_invalid;
        return result;
    }
    for (const auto& obstacle : obstacles_) {
        if (obstacle.room != room ||
            (obstacle.collection == RoomInfoObstacleCollection::dynamic_list &&
             obstacle.owner_id == target_id) ||
            !collision_boxes_intersect(candidate, obstacle.box))
            continue;
        result.intersections.push_back(obstacle);
    }
    result.status = RoomInfoObstacleIndexStatus::complete;
    result.reason = RoomInfoObstacleIndexReason::none;
    return result;
}

const char* room_info_obstacle_index_status_name(RoomInfoObstacleIndexStatus status) noexcept {
    switch (status) {
    case RoomInfoObstacleIndexStatus::complete: return "complete";
    case RoomInfoObstacleIndexStatus::unsupported: return "unsupported";
    }
    return "invalid_status";
}

const char* room_info_obstacle_index_reason_name(RoomInfoObstacleIndexReason reason) noexcept {
    switch (reason) {
    case RoomInfoObstacleIndexReason::none: return "none";
    case RoomInfoObstacleIndexReason::inventory_incomplete: return "inventory_incomplete";
    case RoomInfoObstacleIndexReason::inventory_over_budget: return "inventory_over_budget";
    case RoomInfoObstacleIndexReason::owner_identity_invalid: return "owner_identity_invalid";
    case RoomInfoObstacleIndexReason::owner_missing: return "owner_missing";
    case RoomInfoObstacleIndexReason::room_invalid: return "room_invalid";
    case RoomInfoObstacleIndexReason::geometry_invalid: return "geometry_invalid";
    }
    return "invalid_reason";
}

} // namespace odh
