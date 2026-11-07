// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/world.hpp"
namespace odh {
inline constexpr std::size_t ts1_avatar_slot_count=3;
// SavedObjectBanks attest the TS1 import profile. Ordinary synthetic World
// avatars retain their caller-declared layout; object slot bounds never change.
inline bool ts1_saved_avatar_containment(const Entity& entity) noexcept {
    return entity.kind==EntityKind::avatar&&entity.saved_object_banks&&entity.slots.size()<=ts1_avatar_slot_count;
}
inline std::size_t logical_containment_slots(const Entity& entity) noexcept {
    return ts1_saved_avatar_containment(entity)?ts1_avatar_slot_count:entity.slots.size();
}
inline std::optional<std::int16_t> containment_slot_value(const Entity& entity,std::size_t index) noexcept {
    if(index<entity.slots.size())return entity.slots[index];
    if(ts1_saved_avatar_containment(entity)&&index<ts1_avatar_slot_count)return std::int16_t{0};
    return std::nullopt;
}
inline std::size_t imported_ts1_avatar_slots(std::size_t raw_slots) {
    if(raw_slots>ts1_avatar_slot_count)throw FormatError("saved TS1 avatar has more than three physical slots");
    return ts1_avatar_slot_count;
}
// Extent compatibility only: live occupants may change through real actions.
// World snapshot validation still requires exact stored reciprocal links.
inline bool compatible_containment_extent(const Entity& a,const Entity& b) noexcept {
    if(a.slots.size()==b.slots.size())return true;
    return ts1_saved_avatar_containment(a)&&ts1_saved_avatar_containment(b)&&a.id==b.id&&a.generation==b.generation&&
        a.definition&&b.definition&&a.definition->guid==b.definition->guid&&a.definition->master_guid==b.definition->master_guid&&
        a.saved_object_banks->object_data==b.saved_object_banks->object_data&&a.saved_object_banks->extras==b.saved_object_banks->extras;
}
}
