#include "odh/neighbor_info.hpp"

namespace odh {
namespace {
std::int16_t low16(std::int32_t value) {
    const auto raw=static_cast<std::uint32_t>(value)&65535U;
    return static_cast<std::int16_t>(raw<32768 ? static_cast<std::int32_t>(raw) : static_cast<std::int32_t>(raw)-65536);
}
}
std::optional<std::int16_t> ts1_neighbor_info(const NeighborContext& neighbors,const World& world,
    const FamilyContext* families,std::optional<std::uint16_t> current_family_id,
    const RelationshipState* relationships,std::int16_t neighbor,std::int16_t field) {
    const auto* record=neighbors.by_id(neighbor);
    if(!record)return std::int16_t{0};
    switch(field) {
    case 0:return neighbors.instance_id(world,neighbor);
    case 2:return record->person_data ? (*record->person_data)[58] : std::int16_t{0};
    case 3:case 4:case 8:return std::int16_t{0};
    case 5:return relationships ? relationships->mutual_neighbor_friend_count(neighbors,neighbor) : std::nullopt;
    case 7:return std::int16_t{1};
    case 1:case 6:case 9: {
        if(!families)return {};
        const auto* family=record->person_data
            ? families->by_id(static_cast<std::uint16_t>((*record->person_data)[61])) : nullptr;
        if(field==6)return family ? low16(family->family.house_number) : std::int16_t{0};
        if(field==9)return family ? low16(family->family.family_friends) : std::int16_t{0};
        const FamilyEntry* current=nullptr;
        if(current_family_id) {
            current=families->by_id(*current_family_id);
            if(!current)return {}; // nonnull current identity was not supplied.
        }
        return static_cast<std::int16_t>(current==family ? 1 : 0);
    }
    default:return {};
    }
}
} // namespace odh
