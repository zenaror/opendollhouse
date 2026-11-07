#include "odh/room_score.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace odh {

std::int16_t source_room_score(std::int32_t total_impact,std::uint64_t area,bool outside) noexcept {
    const float divisor=outside?30.0f:std::max(1.0f,static_cast<float>(area)/12.0f);
    const auto truncated=static_cast<std::int64_t>(static_cast<float>(total_impact)/divisor);
    const auto bits=static_cast<std::uint16_t>(static_cast<std::uint64_t>(truncated));
    const auto narrowed=bits<=32767?static_cast<std::int32_t>(bits):static_cast<std::int32_t>(bits)-65536;
    return static_cast<std::int16_t>(std::clamp(narrowed-(outside?15:10),-100,100));
}

std::optional<RoomScoreUpdate> calculate_room_score(
    const GeometricRoomMap& map, const std::vector<RoomScoreContribution>& entities,
    std::int32_t target_x, std::int32_t target_y, std::int16_t target_level) {
    if (!map.floor_resources_resolved || !map.light_base_rooms_resolved || map.rooms.empty())
        return std::nullopt;

    const auto room = geometric_room_at_runtime(map, target_x, target_y, target_level);
    if (!room || *room >= map.rooms.size() || map.rooms[*room].id != *room)
        return std::nullopt;
    if (*room == 0) return RoomScoreUpdate{0, 0, 0, false};

    const auto base = map.rooms[*room].light_base_room;
    if (base == 0 || base >= map.rooms.size() || map.rooms[base].id != base ||
        map.rooms[base].light_base_room != base)
        return std::nullopt;

    std::vector<bool> supported(map.rooms.size(), false);
    std::uint64_t area = 0;
    std::optional<bool> outside;
    for (const auto& candidate : map.rooms) {
        if (candidate.id >= map.rooms.size() || map.rooms[candidate.id].id != candidate.id)
            return std::nullopt;
        if (candidate.light_base_room != base) continue;
        supported[candidate.id] = true;
        area += candidate.area;
        outside = candidate.outside; // Source RefreshRoomScore retains the last support room.
    }
    if (!supported[*room] || !outside) return std::nullopt;

    std::int64_t room_impact = 0;
    for (const auto& entity : entities) {
        const auto entity_room = geometric_room_at_runtime(map, entity.x, entity.y, entity.level);
        if (!entity_room) {
            if(entity.impact==0)continue;
            return std::nullopt;
        }
        if (*entity_room >= supported.size()) return std::nullopt;
        if (supported[*entity_room]) room_impact += entity.impact;
    }
    if (room_impact < std::numeric_limits<std::int32_t>::min() ||
        room_impact > std::numeric_limits<std::int32_t>::max()) return std::nullopt;

    return RoomScoreUpdate{*room,base,source_room_score(static_cast<std::int32_t>(room_impact),area,*outside),true};
}

std::vector<RoomScoreContribution> room_score_contributions(const World& world) {
    std::vector<RoomScoreContribution> contributions;
    auto cursor=std::int16_t{-1};
    while(const auto id=world.next_entity(cursor)){
        cursor=*id;
        const auto* entity=world.find(*id);
        if(!entity||!entity->position)continue;
        const auto impact=entity->object_words.find(7);
        contributions.push_back({entity->position->x,entity->position->y,entity->position->level,
            impact==entity->object_words.end()?std::int16_t{0}:impact->second});
    }
    return contributions;
}

} // namespace odh
