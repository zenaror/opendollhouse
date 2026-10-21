#include "odh/room_info_obstacles.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
std::size_t checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

odh::RoomInfoObstacle obstacle(std::int32_t id, std::uint16_t room,
                                odh::CollisionBox box,
                                odh::RoomInfoObstacleCollection collection) {
    return {id, room, box, collection};
}
} // namespace

int main() {
    try {
        odh::RoomInfoObstacleIndex index;
        const auto initial = index.query(1, {0, 0, 10, 10}, 5);
        require(initial.status == odh::RoomInfoObstacleIndexStatus::unsupported &&
                    initial.reason == odh::RoomInfoObstacleIndexReason::inventory_incomplete,
                "an unbuilt room inventory cannot answer placement queries");
        require(index.rebuild({
                    obstacle(7, 1, {10, 0, 20, 10}, odh::RoomInfoObstacleCollection::static_set),
                    obstacle(6, 1, {1, 1, 4, 4}, odh::RoomInfoObstacleCollection::dynamic_list),
                    obstacle(4, 1, {2, 2, 5, 5}, odh::RoomInfoObstacleCollection::dynamic_list),
                    obstacle(3, 1, {3, 3, 6, 6}, odh::RoomInfoObstacleCollection::static_set),
                    obstacle(2, 2, {1, 1, 4, 4}, odh::RoomInfoObstacleCollection::static_set)}, true),
                "a complete unique owner inventory is accepted");

        const auto query = index.query(1, {0, 0, 10, 10}, 6);
        require(query.status == odh::RoomInfoObstacleIndexStatus::complete &&
                    query.intersections.size() == 2 &&
                    query.intersections[0].owner_id == 3 &&
                    query.intersections[1].owner_id == 4,
                "query combines static and dynamic overlaps in owner-ID order");
        const auto target_static = index.query(1, {0, 0, 10, 10}, 3);
        require(target_static.intersections.size() == 3 &&
                    target_static.intersections[0].owner_id == 3,
                "static collection preserves target footprint for same-group policy");
        require(std::none_of(query.intersections.begin(), query.intersections.end(),
                    [](const auto& item) { return item.owner_id == 6; }),
                "dynamic target footprint is excluded from GetAvatarPlace query");
        require(index.query(2, {0, 0, 10, 10}, 5).intersections.size() == 1,
                "room partition excludes obstacles registered to other rooms");
        require(index.query(1, {0, 0, 10, 10}, 6).intersections.size() == 2,
                "touching edges are not strict intersections");

        require(index.move_footprint(4, 2, {20, 20, 24, 24}),
                "room-change update moves an owner footprint atomically");
        require(index.query(1, {0, 0, 10, 10}, 6).intersections.size() == 1 &&
                    index.query(2, {19, 19, 25, 25}, 5).intersections.size() == 1,
                "moved footprint leaves old room and appears in new room");
        require(index.set_collection(4, odh::RoomInfoObstacleCollection::static_set),
                "static/dynamic reclassification updates owner membership");
        const auto reclassified = index.query(2, {19, 19, 25, 25}, 4);
        require(reclassified.intersections.size() == 1 &&
                    reclassified.intersections[0].owner_id == 4,
                "reclassified static target remains in static query result");
        require(index.unregister_footprint(4) &&
                    index.query(2, {19, 19, 25, 25}, 5).intersections.empty(),
                "unregister removes the footprint from the index");
        require(index.unregister_footprint(99),
                "unregister of an owner with no footprint is a no-op");

        require(!index.move_footprint(99, 1, {1, 1, 2, 2}) &&
                    index.status() == odh::RoomInfoObstacleIndexStatus::unsupported &&
                    index.reason() == odh::RoomInfoObstacleIndexReason::owner_missing &&
                    index.query(1, {0, 0, 10, 10}, 5).status ==
                        odh::RoomInfoObstacleIndexStatus::unsupported,
                "missing move owner invalidates stale inventory and queries");
        require(index.rebuild({}, false) == false && index.size() == 0 &&
                    index.reason() == odh::RoomInfoObstacleIndexReason::inventory_incomplete,
                "partial rebuild clears entries instead of publishing a partial index");
        require(index.rebuild({obstacle(1, 1, {0, 0, 4, 4},
                    odh::RoomInfoObstacleCollection::static_set),
                    obstacle(1, 2, {4, 4, 8, 8},
                    odh::RoomInfoObstacleCollection::dynamic_list)}, true) == false &&
                    index.reason() == odh::RoomInfoObstacleIndexReason::owner_identity_invalid,
                "duplicate owner across collections fails closed");

        odh::RoomInfoObstacleIndex small(1);
        require(!small.rebuild({obstacle(1, 1, {0, 0, 4, 4},
                    odh::RoomInfoObstacleCollection::static_set),
                    obstacle(2, 1, {4, 4, 8, 8},
                    odh::RoomInfoObstacleCollection::dynamic_list)}, true) &&
                    small.reason() == odh::RoomInfoObstacleIndexReason::inventory_over_budget,
                "rebuild honors caller inventory budget");
        require(small.rebuild({}, true) &&
                    small.register_footprint(obstacle(1, 1, {0, 0, 4, 4},
                    odh::RoomInfoObstacleCollection::static_set)) &&
                    !small.register_footprint(obstacle(2, 1, {4, 4, 8, 8},
                    odh::RoomInfoObstacleCollection::dynamic_list)) &&
                    small.reason() == odh::RoomInfoObstacleIndexReason::inventory_over_budget,
                "event updates enforce the same inventory budget");

        require(index.rebuild({}, true) &&
                    index.register_footprint(obstacle(8, 3, {1, 1, 4, 4},
                    odh::RoomInfoObstacleCollection::dynamic_list)) &&
                    index.query(3, {0, 0, 5, 5}, 7).intersections.size() == 1,
                "explicit runtime registration creates a queryable footprint");
        require(index.query(3, {0, 0, 5, 5}, 0).reason ==
                    odh::RoomInfoObstacleIndexReason::owner_identity_invalid,
                "placement queries require a valid target identity");
        index.mark_incomplete();
        require(index.size() == 0 && index.query(3, {0, 0, 5, 5}, 7).status ==
                    odh::RoomInfoObstacleIndexStatus::unsupported,
                "invalidated index discards stale footprints");

        require(checks == 21, "all RoomInfo index assertions were counted");
        std::cout << "RoomInfo obstacle index: " << checks << " checks passed (explicit inventory only)\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
