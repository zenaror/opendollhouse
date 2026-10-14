#include "odh/avatar_neighbor.hpp"
#include <algorithm>

namespace odh {
NeighborInheritance inherit_neighbor(World& world, std::int16_t actor,
    const NeighborRecord& neighbor, std::optional<std::uint16_t> current_family) {
    const auto* entity = world.find(actor);
    if (!entity || entity->kind != EntityKind::avatar || !entity->definition ||
        entity->definition->guid != neighbor.guid)
        throw FormatError("neighbor inheritance requires matching avatar GUID");
    if (entity->person_data.size() < 69)
        throw FormatError("neighbor inheritance requires person words0..68");
    if (neighbor.version != 4 && neighbor.version != 10)
        throw FormatError("neighbor inheritance requires NBRS record4/10");
    const bool saved = neighbor.person_mode > 0;
    if (saved != neighbor.person_data.has_value() ||
        (saved && neighbor.person_data->size() != (neighbor.version == 4 ? 80U : 256U)))
        throw FormatError("neighbor inheritance has inconsistent raw person bank");
    NeighborInheritance result{neighbor.neighbor_id, neighbor.guid, current_family,
        entity->person_data.size(), saved ? 88U : entity->person_data.size(), saved,
        entity->person_data[65], entity->person_data[32], entity->person_data[35], 0, 0, 0};
    auto projected = saved ? std::vector<std::int16_t>(88, 0) : entity->person_data;
    if (saved) std::copy_n(neighbor.person_data->begin(),
        std::min<std::size_t>(88, neighbor.person_data->size()), projected.begin());
    result.neighbor_family = projected[61];
    // C# promotes short to int before comparing to nullable ushort: negative
    // saved family values never match IDs32768..65535 by wrapping.
    result.selected_person_type = result.retained_person_type != 0 ? result.retained_person_type :
        (current_family && static_cast<std::int32_t>(projected[61]) ==
            static_cast<std::int32_t>(*current_family) ? 0 : 1);
    projected[65] = result.retained_gender;
    projected[31] = neighbor.neighbor_id;
    projected[32] = result.selected_person_type;
    projected[35] = result.retained_visitor_schedule;
    projected[34] = 0;
    // Reapplying IsGhost updates the renderer in FreeSO. This headless World
    // retains the loaded word and reports it without claiming that effect.
    result.ghost = projected[68];
    if (!world.replace_person_data(actor, projected))
        throw FormatError("neighbor inheritance replacement exceeds World budget");
    return result;
}
}
