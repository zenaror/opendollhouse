/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "simantics/VM.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "files/formats/iff/chunks/OBJM.h"
#include <algorithm>
#include <map>
#include <set>
#include <memory>
#include <unordered_set>
#include <stdexcept>

namespace FSO { namespace SimAntics { namespace Utils {
// Reconstruct the classic instance groups before optional name/runtime overlays.
// FreeSO VMTS1ActivatorNew builds these from MultitileParentID and GroupX/Y/Level,
// rather than deriving them from proximity or a GUID->one-ID creation hint.
class VMTS1ObjectGroups {
    using OBJM = Files::Formats::IFF::Chunks::OBJM;
    using Group = Entities::VMMultitileGroup;
    using Pos = LotView::Model::LotTilePos;
    struct Part { VMEntity* Entity; Pos Offset; VMEntity::DirectionT Facing; };
    struct Plan {
        int16_t Lead = 0;
        bool Multi = false;
        std::vector<Part> Parts;
        std::unique_ptr<Group> Prepared;
        Group* Published = nullptr;
    };
    // C# FromBigTile narrows to signed 16 bits after scaling. Do the same
    // explicitly, including signed offsets, without left-shifting a negative.
    static int16_t Coordinate(int32_t tile) {
        if (tile < -32768 || tile > 32767) throw std::runtime_error("ObjM: invalid group offset");
        uint32_t raw = (uint32_t)((uint16_t)tile) * 16u + 8u;
        int32_t wrapped = int32_t(raw & 65535u);
        return int16_t(wrapped >= 32768 ? wrapped - 65536 : wrapped);
    }
public:
    static size_t Restore(VM* vm, const OBJM& objm, const std::unordered_set<uint32_t>& recreatedControllers) {
        std::map<int16_t, Plan> plans;
        std::set<VMEntity*> members;
        for (const auto& kv : objm.ObjectData) {
            const auto& inst = kv.second.Instance;
            if (!inst || !inst->OBJT || recreatedControllers.count(inst->OBJT->GUID)) continue;
            auto entity = vm->GetObjectById(int16_t(kv.first));
            // The existing legacy loader can omit an unavailable unplaced
            // object. It cannot contribute a fabricated part to another group.
            if (!entity || entity->Dead || !entity->Object || !entity->Object->OBJ || entity->Object->OBJ->GUID != inst->OBJT->GUID) {
                if (inst->HasMultitileData) throw std::runtime_error("ObjM: missing group part " + std::to_string(kv.first));
                continue;
            }
            if (kv.first <= 0 || kv.first > 32767 || !members.insert(entity).second)
                throw std::runtime_error("ObjM: duplicate or invalid group instance");
            int16_t lead = int16_t(kv.first);
            Pos offset;
            if (inst->HasMultitileData) {
                const auto& mt = inst->MultitileData;
                if (mt.MultitileParentID != 0) lead = mt.MultitileParentID;
                if (lead <= 0 || mt.GroupLevel < -128 || mt.GroupLevel > 127)
                    throw std::runtime_error("ObjM: invalid group parent or level");
                offset = Pos(Coordinate(mt.GroupX), Coordinate(mt.GroupY), int8_t(mt.GroupLevel));
            }
            auto& plan = plans[lead];
            if (!plan.Parts.empty() && plan.Multi != inst->HasMultitileData)
                throw std::runtime_error("ObjM: incompatible group records");
            plan.Lead = lead; plan.Multi = inst->HasMultitileData;
            if (inst->ObjectData.size() < 2) throw std::runtime_error("ObjM: saved direction is missing");
            Part part{entity, offset, VMEntity::DirectionT(1 << (inst->ObjectData[1] & 7))};
            if (entity->ObjectID == lead) plan.Parts.insert(plan.Parts.begin(), part);
            else plan.Parts.push_back(part);
        }
        // Validate every group and prepare all storage before changing any live
        // membership. Positions, instances, slots, flags, and threads stay intact.
        for (auto& kv : plans) {
            auto& plan = kv.second;
            if (plan.Parts.empty() || plan.Parts.front().Entity->ObjectID != plan.Lead)
                throw std::runtime_error("ObjM: group lead is missing");
            auto lead = plan.Parts.front().Entity;
            auto old = lead->MultitileGroup;
            if (!old) throw std::runtime_error("ObjM: live group is missing");
            plan.Prepared = std::make_unique<Group>();
            auto& group = *plan.Prepared;
            group.MultiTile = plan.Multi;
            group.Name = old->Name;
            group.Price = old->Price;
            group.SalePrice = old->SalePrice;
            for (const auto& part : plan.Parts) {
                auto entity = part.Entity;
                auto previous = entity->MultitileGroup;
                // Classic membership follows the saved parent ID. A group can
                // contain parts from independently resolved master definitions.
                if (!previous || previous->Objects.size() != previous->Offsets.size() ||
                    std::count(previous->Objects.begin(), previous->Objects.end(), entity) != 1)
                    throw std::runtime_error("ObjM: incompatible live group part");
                group.Objects.push_back(entity);
                group.Offsets.push_back(part.Offset);
            }
        }
        for (auto& kv : plans) {
            auto& plan = kv.second;
            plan.Published = vm->OwnGroup(plan.Prepared.get());
            plan.Prepared.release();
        }
        for (auto& kv : plans) for (const auto& part : kv.second.Parts)
            part.Entity->MultitileGroup->RemoveObject(part.Entity);
        for (auto& kv : plans) for (const auto& part : kv.second.Parts) {
            part.Entity->MultitileGroup = kv.second.Published;
            // Init/placement may have combined dynamic groups and rotated a
            // previously placed part. Reassert its classic saved orientation
            // with membership, before ODHI restores more recent runtime state.
            part.Entity->Direction(part.Facing);
            // Direction also governs the cached routing obstacle. Refresh it
            // without virtual placement callbacks, regrouping or slot changes.
            auto entity = part.Entity;
            if (!entity->GhostImage && !entity->Container && entity->Position() != Pos::OUT_OF_WORLD)
                entity->VMEntity::PositionChange(vm->Context, true);
        }
        return plans.size();
    }
};
}}}
