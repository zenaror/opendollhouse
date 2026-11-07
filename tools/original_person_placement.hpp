// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/original_position_search.hpp"

namespace odh_host {
// Source432644 ->432664 ->422665 tests only signed-byte XY tile indices.
// The World models negative/OOW coordinates as an absent position. Z and
// heading do not participate in this query. Unknown lot size is not false.
std::optional<bool> original_person_is_in_world(
    const std::optional<odh::EntityPosition>&,std::int32_t lot_size);
// Source45ea39..45ebbc only. The host binds these services to one authenticated
// original-load person; this helper neither resolves IDs nor owns physical data.
struct OriginalPersonPlacementServices {
    std::function<bool()> source_still_matches;
    //43270d/432664 snapshot+0xc is nonnull. Resolve the actual container chain;
    // an absent/unknown snapshot is distinct from a known uncontained person.
    std::function<std::optional<bool>()> contained_snapshot;
    // Actual virtual+50(1), including its existing spatial/containment effects.
    std::function<bool(std::string&)> remove_from_world;
    // Actual f4/f8/110 after RemoveFromWorld, with the person's direction.
    std::function<std::optional<odh::EntityPosition>()> current_position;
    // Native virtual+54(point, level,0,0). Query only; EP5 effects may persist.
    std::function<odh::Ts1PlacementResult(const odh::EntityPosition&)> query54;
    //434895 query-only search, normally bound to search_original_position.
    // A found point must not publish placement, Pre/Post or a heading change.
    std::function<odh::OriginalPositionSearchResult(const odh::EntityPosition&,bool same_room)> search;
    // Exact current native+114 bit6. Never substitute ui_ghost or unknown=false.
    std::function<std::optional<bool>()> skip_native_recovery_position;
    std::function<std::optional<bool>()> is_in_world;
    // World size used by45eb0d/45eb28. This bounded host profile supports2..128.
    std::function<std::optional<std::int32_t>()> lot_size;
    // Virtual+88(point,level,0,0), followed by virtual+74. Boolean means the
    // operation was executed completely, NOT its native ignored collision return.
    // Bind force88 to actual shared original-avatar placement with check=false.
    std::function<bool(const odh::EntityPosition&,std::string&)> force88;
    std::function<bool(std::string&)> footprint74;
};
struct OriginalPersonPlacementResult {
    bool completed=false;
    // fallback_executed means force88 was attempted; only completed proves its
    // callback and the following footprint74 both completed under authority.
    bool effects_started=false, fallback_executed=false;
    std::size_t queries=0, searches=0;
    // Selected force candidate. On failure this does not attest publication.
    std::optional<odh::EntityPosition> forced_position;
    std::string diagnostic;
};
// Preserves completed effects on failure. In particular the first contained
// query/search result is discarded; only the later OOW branch forces a pose.
// Both exhausted fallback searches still force the original center candidate.
OriginalPersonPlacementResult place_original_person(const OriginalPersonPlacementServices&);
} // namespace odh_host
