// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/ts1_placement.hpp"

namespace odh {
struct OriginalPositionSearchRequest {
    EntityPosition start{}; // VM sixteenth-tile units; preserve its direction.
    bool same_room=true;
};
struct OriginalPositionSearchServices {
    std::function<bool()> source_still_matches;
    // This bounded native profile supports square lots2..128 and levels1/2.
    // Host authority keeps the selected map/dimensions fixed across the query.
    std::function<std::optional<std::int32_t>()> lot_size;
    // Tile coordinates, not VM units. Return actual current map values.
    std::function<std::optional<std::uint16_t>(std::int16_t,std::int16_t,std::int16_t)> room_at;
    //479737 tile-info mask; its0x30 bits come from raw wall segments (ARRY12/112).
    std::function<std::optional<std::uint8_t>(std::int16_t,std::int16_t,std::int16_t)> tile_mask;
    // Exact4790e8 Boolean: true rejects. Level1 consults map+60 when present
    // and returns true for raw grid byte<0x10; other levels return false.
    // The host supplies the actual ARRY9 pool grid; no floor/obstacle inference.
    std::function<std::optional<bool>(std::int16_t,std::int16_t,std::int16_t)> native_4790e8;
    std::function<Ts1PlacementResult(const EntityPosition&)> position_valid;
};
enum class OriginalPositionSearchStatus {found,exhausted,unsupported};
struct OriginalPositionSearchResult {
    OriginalPositionSearchStatus status=OriginalPositionSearchStatus::unsupported;
    std::optional<EntityPosition> position;
    std::size_t attempts=0,position_checks=0;
    std::string diagnostic;
};
// Original434895 fixed-position recovery profile only: byte0=1, direction=-1,
// flags+15/+16/+17=0, +14=same_room. No random source, occupancy predicate,
// SetPosition or Pre/Post callbacks. PositionValid may retain its native EP5
// effects; an unsupported result does not promise rollback.
OriginalPositionSearchResult search_original_position(const OriginalPositionSearchRequest&,
    const OriginalPositionSearchServices&);
} // namespace odh
