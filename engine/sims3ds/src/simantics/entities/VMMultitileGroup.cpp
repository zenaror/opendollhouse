/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.Entities.VMMultitileGroup
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/VMContext.h"
#include "simantics/VM.h"
#include "simantics/VMArchitecture.h"
#include <cmath>
#include <algorithm>
namespace FSO { namespace SimAntics { namespace Entities {
using namespace FSO::LotView::Model;
using namespace FSO::SimAntics::Model;

VMEntity* VMMultitileGroup::BaseObject() {
    for (size_t i = 0; i < Objects.size(); i++) {
        auto sub = Objects[i];
        if (sub->Object->OBJ->MyLeadObject > 0) return sub;
    }
    for (size_t i = 0; i < Objects.size(); i++) {
        auto sub = Objects[i];
        if (Offsets[i] == LotTilePos()) return sub;
    }
    return Objects.empty() ? nullptr : Objects[0];
}

void VMMultitileGroup::AddObject(VMEntity* obj) {
    AddDynamicObject(obj,
        LotTilePos((int16_t)((int8_t)(((uint16_t)obj->Object->OBJ->SubIndex) >> 8) * 16),
        (int16_t)((int8_t)(((uint16_t)obj->Object->OBJ->SubIndex) & 0xFF) * 16),
        (int8_t)obj->Object->OBJ->LevelOffset));
}

void VMMultitileGroup::AddDynamicObject(VMEntity* obj, const LotTilePos& offset) {
    Objects.push_back(obj);
    Offsets.push_back(offset);
}

void VMMultitileGroup::RemoveObject(VMEntity* obj) {
    auto it = std::find(Objects.begin(), Objects.end(), obj);
    if (it != Objects.end()) {
        auto index = it - Objects.begin();
        Objects.erase(it);
        Offsets.erase(Offsets.begin() + index);
    }
}

std::vector<st::Vector3> VMMultitileGroup::GetBasePositions() {
    std::vector<st::Vector3> positions(Objects.size());
    for (size_t i = 0; i < Objects.size(); i++) {
        positions[i] = st::Vector3((float)(Offsets[i].x / 16), (float)(Offsets[i].y / 16), 0);
    }
    return positions;
}

VMEntity* VMMultitileGroup::GetInteractionGroupLeader(VMEntity* obj) {
    auto group = obj->Object->OBJ->InteractionGroupID;
    if (group < 1) return obj;
    else {
        //find master for this group
        for (auto x : Objects) if (x->Object->OBJ->InteractionGroupID == -group) return x;
        return obj;
    }
}

static int DirToNotch(Direction direction) {
    switch (direction) {
        case Direction::NORTH: return 0;
        case Direction::EAST: return 2;
        case Direction::SOUTH: return 4;
        case Direction::WEST: return 6;
    }
    return 0;
}

VMPlacementResult VMMultitileGroup::ChangePosition(const LotTilePos& pos, Direction direction, VMContext* context) {
    return ChangePositionInternal(pos, direction, context, false);
}

VMPlacementResult VMMultitileGroup::ChangePositionForUser(const LotTilePos& pos, Direction direction, VMContext* context, const std::function<void()>& settle) {
    return ChangePositionInternal(pos, direction, context, false, &settle);
}

bool VMMultitileGroup::CanTranslate() const {
    if (Objects.empty()) return false;
    for (auto part : Objects) {
        if (part->Position() == LotTilePos::OUT_OF_WORLD) return false;
        auto expected = part->GetObstacle(part->Position(), part->Direction());
        auto published = part->Footprint.get();
        if ((published == nullptr) != (expected == nullptr)) return false;
        if (published != nullptr && (published->x1 != expected->x1 || published->x2 != expected->x2
            || published->y1 != expected->y1 || published->y2 != expected->y2)) return false;
    }
    return true;
}

VMPlacementResult VMMultitileGroup::Translate(const LotTilePos& delta, VMContext* context) {
    auto base = BaseObject();
    if (base == nullptr) return VMPlacementResult(VMPlacementError::Success);
    // An unresolved local rotation is not a rigid translation. Snap chooses
    // ordinary placement in this case; direct callers must meet the same gate.
    if (!CanTranslate()) return VMPlacementResult(VMPlacementError::CantFindSlot);
    return ChangePositionInternal(base->Position() + delta, base->Direction(), context, true);
}

VMPlacementResult VMMultitileGroup::ChangePositionInternal(const LotTilePos& pos, Direction direction, VMContext* context, bool translate, const std::function<void()>* settle) {
    if (context->Architecture() == nullptr)
        return VMPlacementResult(VMPlacementError::Success);
    if (pos.Level > context->Architecture()->Stories) return VMPlacementResult(VMPlacementError::NotAllowedOnFloor);

    const bool source=context->VM && context->VM->TS1 &&
        std::all_of(Objects.begin(),Objects.end(),[](VMEntity* object){return !object->GhostImage;});
    std::vector<VMEntity*> OldContainers(Objects.size());
    std::vector<int16_t> OldSlotNum(Objects.size());
    // Preserve the actual published masks, not copies recomputed from a facing
    // that an earlier script may have changed without committing a rotation.
    std::vector<std::unique_ptr<Routing::VMObstacle>> oldFootprints(Objects.size());
    for (size_t i = 0; i < Objects.size(); i++) {
        OldContainers[i] = Objects[i]->Container;
        OldSlotNum[i] = Objects[i]->ContainerSlot;
        if(!source) {
            oldFootprints[i] = std::move(Objects[i]->Footprint);
            Objects[i]->PrePositionChange(context);
        }
    }

    int Dir = DirToNotch(direction);
    st::Matrix rotMat = st::Matrix::CreateRotationZ((float)(Dir * M_PI / 4.0));
    std::vector<VMPlacementResult> places(Objects.size());

    if (Objects.size() > 0) {
        auto bObj = BaseObject();
        size_t bIndex = std::find(Objects.begin(), Objects.end(), bObj) - Objects.begin();
        auto bOff = Offsets[bIndex];
        auto leadOff = st::Vector3(bOff.x, bOff.y, 0);
        // Capture before committing any part: BaseObject need not be first.
        const auto basePosition = bObj->Position();
        std::vector<LotTilePos> targetPositions(Objects.size());
        std::vector<Direction> targetDirections(Objects.size());
        for (size_t i = 0; i < Objects.size(); ++i) {
            auto off = st::Transform(st::Vector3(Offsets[i].x, Offsets[i].y, 0) - leadOff, rotMat);
            targetPositions[i] = pos == LotTilePos::OUT_OF_WORLD ? LotTilePos::OUT_OF_WORLD
                : translate ? pos + (Objects[i]->Position() - basePosition)
                : LotTilePos((int16_t)std::nearbyint(pos.x + off.X), (int16_t)std::nearbyint(pos.y + off.Y), (int8_t)(pos.Level + Offsets[i].Level));
            targetDirections[i] = translate ? Objects[i]->Direction() : direction;
        }

        //TODO: optimize so we don't have to recalculate all of this
        if (pos != LotTilePos::OUT_OF_WORLD) {
            for (size_t i = 0; i < Objects.size(); i++) {
                auto sub = Objects[i];
                places[i] = sub->PositionValid(targetPositions[i], targetDirections[i], context);
                if (places[i].Status != VMPlacementError::Success) {
                    //432a70 validates before committing any placement links.
                    //A rejected source move must retain its old source0 order.
                    if(source)return places[i];
                    //go back to where we started: we're no longer out of world.
                    for (size_t j = 0; j < Objects.size(); j++) {
                        //need to restore slot we were in
                        if (OldContainers[j] != nullptr) {
                            OldContainers[j]->PlaceInSlot(Objects[j], OldSlotNum[j], false, context);
                        }
                        if (settle) {
                            if (auto go = dynamic_cast<VMGameObject*>(Objects[j])) go->RestorePositionAfterUserPlacementFailure(context);
                            else Objects[j]->PositionChange(context, true);
                        } else Objects[j]->PositionChange(context, false);
                    }
                    for (size_t j = 0; j < Objects.size(); ++j)
                        Objects[j]->Footprint = std::move(oldFootprints[j]);
                    return places[i];
                }
            }
        }

        //verification success
        if(source)for(size_t i=0;i<Objects.size();++i) {
            oldFootprints[i]=std::move(Objects[i]->Footprint);
            Objects[i]->PrePositionChange(context);
        }

        for (size_t i = 0; i < Objects.size(); i++) {
            auto sub = Objects[i];
            sub->SetIndivPosition(targetPositions[i], targetDirections[i], context, places[i]);
        }
        if (settle) (*settle)();
        for (size_t i = 0; i < Objects.size(); i++) Objects[i]->PositionChange(context, false);
        return VMPlacementResult(VMPlacementError::Success);
    }
    return VMPlacementResult(VMPlacementError::Success);
}

void VMMultitileGroup::SetVisualPosition(const st::Vector3& pos, Direction direction, VMContext* /*context*/) {
    int Dir = DirToNotch(direction);
    st::Matrix rotMat = st::Matrix::CreateRotationZ((float)(Dir * M_PI / 4.0));
    auto bObj = BaseObject();
    size_t bIndex = std::find(Objects.begin(), Objects.end(), bObj) - Objects.begin();
    auto bOff = Offsets[bIndex];
    auto leadOff = st::Vector3(bOff.x / 16.0f, bOff.y / 16.0f, 0);

    for (size_t i = 0; i < Objects.size(); i++) {
        auto sub = Objects[i];
        auto off = st::Vector3(Offsets[i].x / 16.0f, Offsets[i].y / 16.0f, sub->Object->OBJ->LevelOffset * 2.95f);
        off = st::Transform(off - leadOff, rotMat);

        sub->Direction(direction);
        sub->VisualPosition(pos + off);
    }
}

void VMMultitileGroup::Combine(VMMultitileGroup* other) {
    auto bObj = BaseObject();
    int Dir = DirToNotch(bObj->Direction());
    st::Matrix rotMat = st::Matrix::CreateRotationZ((float)(-Dir * M_PI / 4.0));
    auto others = other->Objects;
    for (auto obj : others) {
        auto diff = obj->Position() - bObj->Position();
        auto vec = st::Vector3(diff.x, diff.y, 0);
        st::Transform(vec, rotMat); // NOTE: result discarded in the original as well.

        AddDynamicObject(obj, LotTilePos((int16_t)std::nearbyint(vec.X), (int16_t)std::nearbyint(vec.Y), diff.Level));
        obj->MultitileGroup = this;
        obj->Direction(bObj->Direction());
    }
}

void VMMultitileGroup::ExecuteEntryPoint(int32_t num, VMContext* context) {
    for (size_t i = 0; i < Objects.size(); i++) Objects[i]->ExecuteEntryPoint(num, context, true);
}

void VMMultitileGroup::Delete(VMContext* context) {
    while (Objects.size() > 0) {
        auto obj = Objects[0];
        obj->Delete(false, context);
    }
}

void VMMultitileGroup::Init(VMContext* context) {
    if (context->VM->TS1 && std::all_of(Objects.begin(),Objects.end(),[](VMEntity* object) {
        return !object->GhostImage && dynamic_cast<VMGameObject*>(object)!=nullptr;
    })) {
        // Init/Main can remove a part or transfer an already initialized new
        // object into this group. Never retain iterators across authored code
        // or initialize that newly transferred object for a second time.
        const auto parts=Objects;
        for (auto object:parts) object->PrepareTS1Fresh(context);
        for (auto object:parts)
            if (!object->Dead && object->MultitileGroup==this) object->CompleteTS1Fresh(context);
        return;
    }
    for (size_t i = 0; i < Objects.size(); i++) {
        Objects[i]->Init(context);
    }
}
}}}
