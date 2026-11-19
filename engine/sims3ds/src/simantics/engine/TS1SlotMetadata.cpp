// SPDX-License-Identifier: MPL-2.0
#include "simantics/engine/TS1SlotMetadata.h"
#include "files/formats/iff/chunks/SLOT.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/entities/VMAvatar.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace FSO { namespace SimAntics { namespace Engine {
namespace {
void SetHeight(TS1SlotMetadata& slot, int32_t height) {
    slot.Height=height;
    //DVD46e014, not the routing adapter SLOT::HeightOffsets table.
    switch (height) {
    case 0: slot.Height=slot.Offset.Z==0.0f?1:5;break;
    case 1: slot.Offset.Z=0.0f;break;
    case 2: slot.Offset.Z=2.5f;break;
    case 3: case 8: slot.Offset.Z=4.0f;break;
    case 4: {
        const uint32_t bits=0x4081999a;
        std::memcpy(&slot.Offset.Z,&bits,sizeof(bits));
        break;
    }
    default: break;
    }
}
}

std::vector<TS1SlotMetadata> TS1BuildSlotMetadata(
    const std::vector<Files::Formats::IFF::Chunks::SLOTItem>& rows,
    bool person, size_t targetExtent) {
    constexpr size_t maximumExtent=32767; //signed hierarchy index0..32766
    size_t count=1+(person?5:0);
    for (const auto& row:rows) if (row.Type==0) {
        if (count==maximumExtent) throw std::runtime_error("Original slot metadata extent exceeds source domain");
        ++count;
    }
    if (targetExtent>maximumExtent)
        throw std::runtime_error("Original saved slot metadata extent exceeds source domain");
    std::vector<TS1SlotMetadata> result;
    result.reserve(std::max(count,targetExtent));
    result.emplace_back();
    for (const auto& row:rows) if (row.Type==0) {
        if (!std::isfinite(row.Offset.X) || !std::isfinite(row.Offset.Y) || !std::isfinite(row.Offset.Z))
            throw std::runtime_error("Original slot metadata has non-finite coordinates");
        TS1SlotMetadata slot;
        slot.Offset=row.Offset;slot.ResourceRow=row.SourceRowIndex;
        slot.Capacity=row.PhysicalCapacity;slot.Flags=row.PhysicalFlags;
        SetHeight(slot,row.RawHeight);
        result.push_back(std::move(slot));
    }
    if (person) {
        static constexpr int32_t heights[5]={6,0,1,1,1};
        for (size_t i=0;i<5;++i) {
            TS1SlotMetadata slot;slot.Height=heights[i];
            if (i==1) { slot.Flags|=2;slot.BoneName="HEAD"; }
            result.push_back(std::move(slot));
        }
    }
    result.resize(std::max(result.size(),targetExtent));
    return result;
}

std::vector<TS1SlotMetadata> TS1BuildEntitySlotMetadata(const VMEntity& entity, size_t targetExtent) {
    static const std::vector<Files::Formats::IFF::Chunks::SLOTItem> empty;
    const auto* rows=&empty;
    if(entity.Slots){
        const auto found=entity.Slots->Slots.find(0);
        if(found!=entity.Slots->Slots.end())rows=&found->second;
    }
    return TS1BuildSlotMetadata(*rows,dynamic_cast<const VMAvatar*>(&entity)!=nullptr,targetExtent);
}

}}}
