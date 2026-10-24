#include "odh/room_score.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace odh {

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
        if (outside && *outside != candidate.outside) return std::nullopt;
        outside = candidate.outside;
    }
    if (!supported[*room] || !area || !outside) return std::nullopt;

    std::int64_t room_impact = 0;
    for (const auto& entity : entities) {
        const auto entity_room = geometric_room_at_runtime(map, entity.x, entity.y, entity.level);
        if (!entity_room || *entity_room >= supported.size()) return std::nullopt;
        if (supported[*entity_room]) room_impact += entity.impact;
    }
    if (room_impact < std::numeric_limits<std::int32_t>::min() ||
        room_impact > std::numeric_limits<std::int32_t>::max()) return std::nullopt;

    // Keep the reference's single-precision divisor and truncation order:
    // cast the divided contribution to a short, subtract the outside/inside
    // baseline, then clamp the resulting room score to [-100, 100].
    const float area_scale = *outside ? 30.0f : std::max(1.0f, static_cast<float>(area) / 12.0f);
    const float divided = static_cast<float>(room_impact) / area_scale;
    if (divided < static_cast<float>(std::numeric_limits<std::int16_t>::min()) ||
        divided > static_cast<float>(std::numeric_limits<std::int16_t>::max())) return std::nullopt;
    const auto scaled = static_cast<std::int32_t>(divided); // truncates toward zero, as the C# cast.
    const auto adjusted = scaled - (*outside ? 15 : 10);
    const auto score = std::clamp(adjusted, -100, 100);
    return RoomScoreUpdate{*room, base, static_cast<std::int16_t>(score), true};
}

} // namespace odh
