// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "world/model/LotTilePos.h"
#include "simantics/engine/TS1SlotMetadata.h"

namespace FSO { namespace SimAntics { namespace Engine {

// Original432728 ->432771. A physical request uses logical slot0 for the
// first support/hand slot. It must be normalized exactly once before use.
struct VMTS1PhysicalPlacement {
    bool Valid = false;
    FSO::LotView::Model::LotTilePos Position;
    int16_t ParentID = 0;
    int16_t PhysicalSlot = -1;
};

// Original4328f3/4327d9: source0 is a linked successor, source1 is the first
// physical slot. A raw capture/removal must never apply another +1 shift.
struct VMTS1RawPlacementLink {
    bool Valid = false;
    FSO::LotView::Model::LotTilePos Position;
    int16_t ParentID = 0;
    int16_t SourceSlot = -1;
};

// Only the child vector is owned here. Parent26/Hier14 remain independent
// entity words, including direct script writes that do not relink children.
struct VMTS1HierarchyNode {
    std::vector<int16_t> Slots;
    // Constructor/resource metadata, rebuilt on load; never another ID graph.
    std::vector<TS1SlotMetadata> Metadata;
    // Native adapter transaction state, not an original parent/link mirror.
    // Cache re-registration never infers placement from mutable raw words.
    bool NeedsPlacement = false;
};

// A staged value snapshot, not another runtime parent map. Classic ObjM
// already persists these words and source slot IDs independently.
struct VMTS1HierarchyRecord {
    int16_t ObjectID = 0;
    uint32_t GUID = 0;
    FSO::LotView::Model::LotTilePos Position;
    int16_t RawParent = 0, RawHierarchySlot = -1;
    int16_t RawContainer = 0, RawPhysicalSlot = -1, RawSlotCount = 0;
    VMTS1HierarchyNode Node;
};

struct VMTS1HierarchyState {
    std::unordered_map<int16_t, VMTS1HierarchyRecord> Records;
    std::array<std::vector<int16_t>, 2> TileHeads;
};

}}}
