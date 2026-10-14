#pragma once
#include "odh/world.hpp"
#include "odh/neighborhood.hpp"

namespace odh {
struct NeighborInheritance {
    std::int16_t neighbor_id;
    std::uint32_t guid;
    std::optional<std::uint16_t> current_family;
    std::size_t bank_words_before, bank_words_after;
    bool saved_data_present;
    std::int16_t retained_gender, retained_person_type, retained_visitor_schedule;
    std::int16_t selected_person_type, ghost, neighbor_family;
};
// Headless FreeSO InheritNeighbor reference subset. The caller explicitly
// supplies an activated FAMI chunk ID; absence means no activated family.
// Validates identity and raw NBRS bank shape before atomically publishing an
// owned person bank. No rendering side effects or other entity banks change.
NeighborInheritance inherit_neighbor(World&, std::int16_t actor,
    const NeighborRecord&, std::optional<std::uint16_t> current_family = {});
}
