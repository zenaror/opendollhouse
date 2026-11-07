// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/dialog.hpp"
#include "odh/neighbor_context.hpp"
#include "odh/relationship.hpp"
#include <functional>
namespace odh_host {
// Simitone UICallNeighborPanel: caller's directional relation keys, then
// NBRS PersonData[61] != 0 and [65] < 2. No score, age, same-family or
// reciprocity filter is added. Choices do not imply invitation acceptance.
struct PhonebookChoices {
    bool ready=false;
    std::vector<odh::DialogChoice> choices;
    std::string diagnostic;
};
using PhonebookDisplayName=std::function<std::optional<std::string>(std::int16_t,std::uint32_t)>;
// Names must come from the admitted character resource, never NBRS.name (an
// asset key). Missing/stale authorities refuse the whole result, not a partial
// list. Caller/reference and relationship state remain untouched.
PhonebookChoices phonebook_choices(const odh::World&,odh::WorldEntityReference caller,
    const odh::NeighborContext&,const odh::RelationshipState&,const PhonebookDisplayName&,
    std::size_t max_choices=4096,std::size_t max_label_bytes=1048576);
}
