// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "core/math.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace FSO { namespace Files { namespace Formats { namespace IFF { namespace Chunks {
class SLOTItem;
}}}}}

namespace FSO { namespace SimAntics {
class VMEntity;
namespace Engine {

//Deterministic source36B-slot metadata. Occupant IDs remain in the independent
//hierarchy vector; no original pointer or extra save payload is stored here.
struct TS1SlotMetadata {
    st::Vector3 Offset;
    int32_t ResourceRow = -1;
    int32_t Height = 0;
    int32_t Capacity = 50;
    int32_t Flags = 0;
    std::string BoneName;
};

//431b5e defaultsource0 + ordered resource Type0 records;459797 person
//appends;434578 grows with default records without shrinking this prefix.
std::vector<TS1SlotMetadata> TS1BuildSlotMetadata(
    const std::vector<Files::Formats::IFF::Chunks::SLOTItem>& rows,
    bool person, size_t targetExtent = 0);

// Rebuild constructor-owned metadata without inferring it from mutable raw67.
std::vector<TS1SlotMetadata> TS1BuildEntitySlotMetadata(const VMEntity& entity, size_t targetExtent = 0);

}}}
