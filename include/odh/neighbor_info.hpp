#pragma once
#include "odh/family.hpp"
#include "odh/relationship.hpp"

namespace odh {
// Full TS1 GetNeighborInStackObject query, pinned FreeSO7e76 VMMemory304-355.
// Fields0..9 only; missing neighbor returns0 before field validation. Provider
// absence differs from an absent record: fields1/6/9 require families, field5
// requires mutable relationships. current_family_id is the FAMI chunk ID, not
// FamilyNumber; nullopt means CurrentFamily is actually absent. No mutation.
std::optional<std::int16_t> ts1_neighbor_info(const NeighborContext&,const World&,
    const FamilyContext* families,std::optional<std::uint16_t> current_family_id,
    const RelationshipState* relationships,std::int16_t neighbor,std::int16_t field);
} // namespace odh
