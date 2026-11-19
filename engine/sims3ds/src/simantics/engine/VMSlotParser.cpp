/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.Engine.VMSlotParser
#include "simantics/engine/VMSlotParser.h"
#include "simantics/engine/VMSlotEnumeration.h"
#include "simantics/engine/VMTS1RelativePosition.h"
#include "simantics/engine/TS1SlotGeometry.h"
#include "simantics/engine/VMRoutingFrame.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/engine/VMThread.h"
#include "simantics/model/VMSetToNextCache.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/VM.h"
#include "common/utils/DirectionUtils.h"
#include <algorithm>
#include <cfloat>
#include <limits>
namespace FSO { namespace SimAntics { namespace Engine {
using namespace FSO::Files::Formats::IFF::Chunks;
using namespace FSO::SimAntics::Model;
using namespace FSO::SimAntics::Model::Routing;
using FSO::LotView::Model::LotTilePos;
using FSO::Common::Utils::DirectionUtils;
using st::Vector2;
using st::Vector3;

const VMRouteFailCode VMSlotParser::FailPrio[4] = {
    VMRouteFailCode::NoValidGoals,
    VMRouteFailCode::NoChair,
    VMRouteFailCode::DestTileOccupiedPerson,
    VMRouteFailCode::DestTileOccupied,
};

VMSlotParser::VMSlotParser(SLOTItem* slot) {
    Slot = slot;
    Flags = slot->Rsflags;
    MinProximity = slot->MinProximity;
    MaxProximity = slot->MaxProximity;
    DesiredProximity = slot->OptimalProximity;

    if (MaxProximity == 0) { MaxProximity = MinProximity; }
    if (DesiredProximity == 0) { DesiredProximity = MinProximity; }
}

namespace {
// DVD 4824b8 scopes the route's requested extension over all candidate
// acceptance, then clears PD73 and restores heading without republishing the
// occupied footprint. Both authored and generated TS1 slots use that wrapper.
struct TS1FacingGuard {
    VMEntity* Actor;
    VMAvatar* Avatar;
    float Radians;
    TS1FacingGuard(VMEntity* actor, bool enabled)
        : Actor(enabled ? actor : nullptr), Avatar(dynamic_cast<VMAvatar*>(Actor)),
          Radians(Actor ? Actor->RadianDirection() : 0.f) {
        if (Avatar) {
            const auto requested = Avatar->GetPersonData(VMPersonDataVariable::RouteFootprintMask);
            Avatar->SetPersonData(VMPersonDataVariable::FootprintExtension, requested);
        }
    }
    ~TS1FacingGuard() {
        if (Avatar) Avatar->SetPersonData(VMPersonDataVariable::FootprintExtension, 0);
        if (Actor) Actor->RadianDirection(Radians);
    }
};
}

std::vector<VMFindLocationResult> VMSlotParser::FindAvaliableLocations(VMEntity* obj, VMContext* context, VMEntity* caller) {
    TS1FacingGuard restore(caller, context && context->VM && context->VM->TS1);
    if (context && context->VM && context->VM->TS1)
        return FindTS1Locations(obj, context, caller);
    return FindReferenceLocations(obj, context, caller);
}

std::vector<VMFindLocationResult> VMSlotParser::FindSnapLocations(VMEntity* obj, VMContext* context, VMEntity* caller) {
    TS1FacingGuard restore(caller, context && context->VM && context->VM->TS1);
    return FindReferenceLocations(obj, context, caller);
}

std::vector<VMFindLocationResult> VMSlotParser::FindReferenceLocations(VMEntity* obj, VMContext* context, VMEntity* caller) {
    /**
     * Start at min proximity and circle around the object to find the avaliable locations.
     * Then pick the one nearest to the optimal value
     */

    /**
     * ------ MAJOR TODO: ------
     * Avoid vector math at all costs! Small differences in hardware could cause desyncs.
     * This really goes for all areas of the SimAntics engine, but here it's particularly bad.
     */
    Vector2 center;
    if (OnlySit()) FailCode = VMRouteFailCode::NoChair;

    // if we need to use the average location of an object group, it needs to be calculated.
    if (((Flags & SLOTFlags::UseAverageObjectLocation) > 0) && (obj->MultitileGroup->MultiTile)) {
        center = Vector2(0, 0);
        auto& objs = obj->MultitileGroup->Objects;
        for (size_t i = 0; i < objs.size(); i++) {
            center += Vector2(objs[i]->Position().x / 16.0f, objs[i]->Position().y / 16.0f);
        }
        center /= (float)objs.size();
    } else center = Vector2(obj->Position().x / 16.0f, obj->Position().y / 16.0f);

    const bool onPoint = ((int)Flags & 255) == 0 || Slot->Offset != Vector3();
    const float dir = SlotSearchRotation((int)Flags, onPoint, obj->RadianDirection());
    // Absolute only affects the search direction when this is not an exact point.
    auto rotOff = st::Transform(Slot->Offset, st::Matrix::CreateRotationZ(dir));
    auto circleCtr = Vector2(center.X + rotOff.X / 16, center.Y + rotOff.Y / 16);

    const int8_t level = SlotDestinationLevel(obj->Position().Level, Slot->Offset.Z);

    uint16_t room = context->VM->Context->GetRoomAt(obj->Position());
    Results.clear();

    if ((Flags & SLOTFlags::SnapToDirection) > 0) { //snap to the specified direction, on the specified point.
        // double baseRot;
        if ((int)Slot->Facing > (int)SLOTFacing::FaceAwayFromObject) {
            // bit of a legacy thing here. Facing field did not use to exist,
            // which is why SnapToDirection was hacked to use the directional flags.
            // now that it exists, it is used instead, to encode the same information...
            // just transform back into old format.
            Flags |= (SLOTFlags)(1 << (int)Slot->Facing);
        } else {
            if (((int)Flags & 255) == 0) Flags |= SLOTFlags::NORTH;
        }

        auto flagRot = DirectionUtils::PosMod(dir + FlagsAsRad(Flags), M_PI * 2);
        if (flagRot > M_PI) flagRot -= M_PI * 2;

        VerifyAndAddLocation(obj, circleCtr, center, Flags, DBL_MAX, context, caller, (float)flagRot, level);
        return Results;
    } else {
        if (onPoint) {
            //exact position
            //Flags |= (SLOTFlags)255;

            // special case, walk directly to point.
            VerifyAndAddLocation(obj, circleCtr, center, Flags, DBL_MAX, context, caller, std::numeric_limits<float>::quiet_NaN(), level);
            return Results;
        }
        auto maxScore = std::max(DesiredProximity - MinProximity, MaxProximity - DesiredProximity) + (LotTilePos::Distance(obj->Position(), caller->Position()) + MaxProximity) / 3 + 2;
        auto ignoreRooms = (Flags & SLOTFlags::IgnoreRooms) > 0;

        EnumerateSlotLocations(*Slot, MaxProximity, [&](int x, int y, double distance) {
            auto pos = Vector2(circleCtr.X + x / 16.0f, circleCtr.Y + y / 16.0f);
            if (SlotProximityValid(MinProximity, MaxProximity, distance) && (ignoreRooms || context->VM->Context->GetRoomAt(LotTilePos((int16_t)std::nearbyint(pos.X * 16), (int16_t)std::nearbyint(pos.Y * 16), obj->Position().Level)) == room)) //slot is within proximity
            {
                auto routeEntryFlags = (SLOTFlags)(GetSearchDirection(circleCtr, pos, dir) & Flags); //the route needs to know what conditions it fulfilled
                if (routeEntryFlags > 0) //within search location
                {
                    // DVD 45ad3a/45aecf -> 483572: ordinary authored slots
                    // do not draw score noise. Keep the reference behavior for
                    // TSO and explicit RandomScoring; its complete TS1 scoring
                    // and candidate-reduction policy are separate contracts.
                    const bool referenceNoise = !context->VM->TS1 || (Flags & SLOTFlags::RandomScoring) > 0;
                    double baseScore = SlotBaseScore((int)Flags, maxScore, DesiredProximity, distance,
                        [&](int max) { return context->VM->Context->NextRandom(max); }, referenceNoise);
                    VerifyAndAddLocation(obj, pos, center, routeEntryFlags, baseScore, context, caller, std::numeric_limits<float>::quiet_NaN(), level);
                }
            }
        });
    }
    /** Sort by how close they are to desired proximity **/

    if (Results.size() > 1) std::stable_sort(Results.begin(), Results.end(), [](const VMFindLocationResult& a, const VMFindLocationResult& b) { return -a.Score < -b.Score; }); //avoid sort because it acts incredibly unusually
    if (Results.size() > 0) FailCode = VMRouteFailCode::Success;
    return Results;
}

// C# Math.Round uses banker's rounding (MidpointRounding.ToEven); std::nearbyint does the same in the default FP mode.
static int16_t RoundEven(double v) { return (int16_t)std::nearbyint(v); }

void VMSlotParser::VerifyAndAddLocation(VMEntity* obj, Vector2 pos, Vector2 center, SLOTFlags entryFlags, double score, VMContext* context, VMEntity* caller, float facingDir, int8_t level) {
    //note: verification is not performed if snap target slot is enabled.
    auto tpos = LotTilePos(RoundEven(pos.X * 16), RoundEven(pos.Y * 16), level);

    if (context->IsOutOfBounds(tpos)) return;

    score = SlotCallerDistanceScore((int)Flags, score, LotTilePos::Distance(tpos, caller->Position()));

    if (Slot->SnapTargetSlot < 0 && context->Architecture()->RaycastWall(st::Point((int)pos.X, (int)pos.Y), st::Point(obj->Position().TileX(), obj->Position().TileY()), level)) {
        SetFail(VMRouteFailCode::WallInWay, nullptr);
        return;
    }

    bool faceAnywhere = false;
    if (std::isnan(facingDir)) {
        auto obj3P = obj->Position().ToVector3();
        auto objP = Vector2(obj3P.X, obj3P.Y);
        // Match the reference's short-circuit: ordinary facing needs no group.
        const bool averageGroup = (Flags & SLOTFlags::UseAverageObjectLocation) > 0
            && obj->MultitileGroup->MultiTile;
        auto facingGroupCenter = center;
        if (averageGroup) {
            // Earlier candidates can run collision checks that change the group.
            facingGroupCenter = Vector2(0, 0);
            const auto& objects = obj->MultitileGroup->Objects;
            for (size_t i = 0; i < objects.size(); ++i) {
                facingGroupCenter.X += objects[i]->Position().x / 16.0f;
                facingGroupCenter.Y += objects[i]->Position().y / 16.0f;
            }
            facingGroupCenter /= (float)objects.size();
        }
        objP = SlotFacingCenter((int)Flags, averageGroup, objP, facingGroupCenter);
        switch (Slot->Facing) {
            case SLOTFacing::FaceTowardsObject:
                facingDir = (float)GetDirectionTo(pos, objP); break;
            case SLOTFacing::FaceAwayFromObject:
                facingDir = (float)GetDirectionTo(objP, pos); break;
            case SLOTFacing::FaceAnywhere:
                faceAnywhere = true;
                facingDir = 0.0f; break;
            default: {
                int intDir = (int)std::nearbyint(std::log2((double)(int)obj->Direction()));
                auto rotatedF = ((int)Slot->Facing + intDir) % 8;
                facingDir = (float)(((int)rotatedF > 4) ? ((double)rotatedF * M_PI / 4.0) : (((double)rotatedF - 8.0) * M_PI / 4.0)); break;
            }
        }
    }

    VMEntity* chair = nullptr;
    bool avatarInWay = false; // TS1(sims): FreeSO
    if (Slot->SnapTargetSlot < 0) {
        auto acceptanceDirection = FSO::LotView::Model::Direction::NORTH;
        if (context->VM->TS1) {
            // DVD 483fc1 sets the actor heading before 481593 validates its
            // anisotropic footprint. Final route facing is a separate result.
            const unsigned targetBits = unsigned(obj->Direction());
            int targetHeading = 0;
            if (targetBits && !(targetBits & (targetBits - 1))) {
                unsigned bits = targetBits;
                while ((bits >>= 1) != 0) ++targetHeading;
            }
            const int base = (Flags & SLOTFlags::Absolute) > 0 ? 0 : targetHeading;
            int heading = -1;
            if ((int(Flags) & 255) == 0) heading = (base + 4) & 7;
            else if (int(Slot->Facing) >= 0) heading = (base + int(Slot->Facing)) & 7;
            else if (Slot->Facing != SLOTFacing::FaceAnywhere) {
                heading = (int(std::nearbyint(GetDirectionTo(center, pos) / (M_PI / 4))) + 8) & 7;
                if (Slot->Facing == SLOTFacing::FaceTowardsObject) heading = (heading + 4) & 7;
            }
            // FaceAnywhere keeps the actor's saved heading in the ordinary
            // region branch; it does not manufacture a north-facing pose.
            if (heading >= 0) caller->Direction(FSO::LotView::Model::Direction(1u << heading));
            acceptanceDirection = caller->Direction();
        }
        auto solid = caller->PositionValid(tpos, acceptanceDirection, context, true);
        if (solid.Status != VMPlacementError::Success) {
            if (solid.Object != nullptr && dynamic_cast<VMGameObject*>(solid.Object) != nullptr) {
                if (Slot->Sitting > 0 && solid.Object->EntryPoints[26].ActionFunction != 0) {
                    chair = solid.Object;
                } else {
                    SetFail(VMRouteFailCode::DestTileOccupied, solid.Object);
                    return;
                }
            } else if (solid.Object != nullptr) {
                avatarInWay = true; // TS1(sims): FreeSO
            }
        }

        // TS1(sims): FreeSO — "prefer not standing in front of a door". Without it, goals in door tiles (dancing
        // next to a stereo by the front door) left two Sims standing in the same doorway, each blocking the other's
        // only route for the rest of the run.
        {
            auto objsAt = context->SetToNextCache->GetObjectsAt(tpos);
            if (objsAt != nullptr) for (auto x : *objsAt)
                if (((int32_t)x->GetValue(VMStackObjectVariable::FlagField2) & (int32_t)VMEntityFlags2::ArchitectualDoor) > 0) { avatarInWay = true; break; }
        }

        if (chair != nullptr && (std::abs(DirectionUtils::Difference(chair->RadianDirection(), facingDir)) > M_PI / 4))
            return; //not a valid goal.
        if (chair == nullptr && OnlySit()) return;
    }

    VMFindLocationResult r;
    r.Position = LotTilePos(RoundEven(pos.X * 16), RoundEven(pos.Y * 16), level);
    r.Score = SlotStandingScore(*Slot, chair != nullptr, score); //todo: prefer closer?
    r.Chair = chair;
    if (Slot->SnapTargetSlot < 0) {
        //if an avatar is in or going to our destination positon, we this spot becomes low priority as getting into it will require a shoo.
        if (!avatarInWay) {
            for (auto avatar : context->SetToNextCache->Avatars) {
                if (avatar == caller || avatar->Thread == nullptr) continue;
                //search for routing frame. is its destination the same as ours?
                bool intersects = false;
                for (auto& f : avatar->Thread->Stack)
                    if (f->IsRoutingFrame() && ((VMRoutingFrame*)f.get())->IntersectsOurDestination(r)) { intersects = true; break; }
                if (intersects) { r.Score = r.Score / 100000; break; }
            }
        } else r.Score = r.Score / 100000;
    }
    r.RadianDirection = facingDir;
    r.Chair = chair;
    r.FaceAnywhere = faceAnywhere;
    r.RouteEntryFlags = entryFlags;
    Results.push_back(r);
}

void VMSlotParser::SetFail(VMRouteFailCode code, VMEntity* blocker) {
    auto indexOf = [](VMRouteFailCode c) { for (int i = 0; i < 4; i++) if (FailPrio[i] == c) return i; return -1; };
    if (indexOf(code) > indexOf(FailCode)) {
        FailCode = code;
        Blocker = blocker;
    }
}

namespace {
int TS1Heading(VMEntity* entity) {
    const unsigned direction = unsigned(entity->Direction());
    for (int i=0;i<8;++i) if (direction==(1u<<i)) return i;
    return -1;
}

uint8_t TS1WallDirections(VMContext* context, const LotTilePos& point, unsigned heading,
                        std::vector<VMEntity*>& pending) {
    if (context->IsOutOfBounds(point)) return 0;
    const auto* arch=context->Architecture();
    const auto walls=uint8_t(arch->Walls[point.Level-1][size_t(point.TileY())*arch->Width+point.TileX()].Segments);
    uint32_t mask=((walls&1)?64:0)|((walls&2)?1:0)|((walls&4)?4:0)|((walls&8)?16:0);
    const auto tile=size_t(point.TileY())*arch->Width+point.TileX();
    if (size_t(point.Level)<=context->SpatialTileHeads.size() &&
        tile<context->SpatialTileHeads[point.Level-1].size()) {
        pending.clear();
        if (auto* root=context->VM->GetObjectById(context->SpatialTileHeads[point.Level-1][tile])) pending.push_back(root);
        size_t remaining=context->VM->Entities.size()+1;
        while (!pending.empty() && remaining--) {
            auto* object=pending.back();pending.pop_back();
            if (!object || object->Dead) continue;
            const int direction=TS1Heading(object);
            if (direction>=0) mask|=TS1ObjectWallMask(object->GetValue(VMStackObjectVariable::WallBlockFlags),unsigned(direction));
            //435a8f uses43f3b9: ascending source0..N preorder.
            //Push in reverse because pending is a LIFO stack.
            for (size_t slot=context->TS1SourceVectorExtent(object);slot>0;--slot)
                if (auto* child=context->TS1SourceSlot(object,int(slot-1))) pending.push_back(child);
        }
    }
    return TS1RotateMask(mask,heading);
}

}

namespace {
// Original4324fc follows the tile-head/slot0 chain; it does not search all
// coincident objects or select an arbitrary chair from an unordered cache.
VMEntity* TS1ObjectAt(VMContext* context, const LotTilePos& point) {
    if (context->IsOutOfBounds(point)) return nullptr;
    const auto* arch=context->Architecture();
    const auto tile=size_t(point.TileY())*arch->Width+point.TileX();
    if (size_t(point.Level)>context->SpatialTileHeads.size() ||
        tile>=context->SpatialTileHeads[point.Level-1].size()) return nullptr;
    auto* object=context->VM->GetObjectById(context->SpatialTileHeads[point.Level-1][tile]);
    size_t remaining=context->VM->Entities.size()+1;
    while (object && remaining--) {
        const auto flags=uint16_t(object->GetValue(VMStackObjectVariable::Flags));
        if ((flags&4) ? bool(flags&2) : !(flags&16)) return object;
        object=context->TS1SourceSlot(object,0);
    }
    return nullptr;
}

bool TS1HasChairEntry(VMEntity* object) {
    return object && object->EntryPoints.size()>26 && object->EntryPoints[26].ActionFunction!=0;
}

bool TS1ChairBounds(VMContext* context, const LotTilePos& position) {
    const auto* arch=context->Architecture();
    return !context->IsOutOfBounds(position) && position.TileX()>0 && position.TileY()>0 &&
        position.TileX()<arch->Width-1 && position.TileY()<arch->Height-1;
}

const st::Point TS1Neighbor[8]={{0,-16},{16,-16},{16,0},{16,16},
                              {0,16},{-16,16},{-16,0},{-16,-16}};

}

std::vector<VMFindLocationResult> VMSlotParser::FindTS1Locations(
        VMEntity* obj, VMContext* context, VMEntity* caller) {
    Results.clear();Blocker=nullptr;FailCode=VMRouteFailCode::NoValidGoals;
    TS1MaximumScore=-1;
    if (!Slot || Slot->Type!=3 || !obj || !caller || !context || !context->VM ||
        !context->Architecture() || context->IsOutOfBounds(obj->Position()) ||
        context->IsOutOfBounds(caller->Position())) return Results;
    const int targetHeading=TS1Heading(obj);
    if (targetHeading<0) return Results;
    const auto flags=uint32_t(Slot->Rsflags);
    const unsigned base=flags&0x200?0:unsigned(targetHeading);
    const auto target=obj->Position();
    const auto callerRoom=context->GetEnvironmentRoomAt(caller->Position());
    auto* person=dynamic_cast<VMAvatar*>(caller);
    if (Slot->RoutingPolicy==SLOTRoutingPolicy::TS1RelativePosition) {
        // This is the OP27 producer's early room gate, not an authored SLOT
        // candidate restriction. A normal authored route can cross rooms.
        const bool exceptional=person && ((person->GetPersonData(VMPersonDataVariable::IsGhost)&1) ||
            (person->GetPersonData(static_cast<VMPersonDataVariable>(84))&1));
        if (!exceptional && context->GetEnvironmentRoomAt(target)!=callerRoom) {
            FailCode=VMRouteFailCode::NoRoomRoute;return Results;
        }
    }
    st::Point center(target.x,target.y);
    if ((flags&0x10000) && obj->MultitileGroup && obj->MultitileGroup->MultiTile &&
        !obj->MultitileGroup->Objects.empty()) {
        // DVD407200 accumulates integer part positions, then IDIV count.
        int64_t x=0,y=0;size_t count=0;
        for (auto* part:obj->MultitileGroup->Objects) if (part) {
            x+=part->Position().x;y+=part->Position().y;++count;
        }
        if (count) center=st::Point(int(x/int64_t(count)),int(y/int64_t(count)));
    }
    const int8_t level=caller->Position().Level;
    const float originalRadians=caller->RadianDirection();
    std::vector<VMEntity*> pending;
    auto admit=[&](st::Point position,int searchHeading) {
        if (position.X<std::numeric_limits<int16_t>::min() || position.X>std::numeric_limits<int16_t>::max() ||
            position.Y<std::numeric_limits<int16_t>::min() || position.Y>std::numeric_limits<int16_t>::max()) return 0;
        const LotTilePos pos(int16_t(position.X),int16_t(position.Y),level);
        if (context->IsOutOfBounds(pos)) return 0;
        if (!(flags&0x800)) {
            if (context->GetEnvironmentRoomAt(pos)!=callerRoom) { FailCode=VMRouteFailCode::NoRoomRoute;return 2; }
            if (searchHeading>=0) {
                const auto wall=TS1WallDirections(context,pos,unsigned(searchHeading+4)&7,pending);
                if ((wall&1) || ((searchHeading&1) && (wall&0x82))) { FailCode=VMRouteFailCode::WallInWay;return 3; }
            }
        }
        if (!(flags&0x8000)) {
            std::array<int,4> reference{},height{};
            const auto& terrain=context->Architecture()->Terrain;
            if (!terrain || !terrain->GetTS1HeightQuad(target.TileX(),target.TileY(),reference) ||
                !terrain->GetTS1HeightQuad(pos.TileX(),pos.TileY(),height) || reference!=height ||
                height[0]!=height[1] || height[0]!=height[2] || height[0]!=height[3]) {
                FailCode=VMRouteFailCode::AltsDontMatch;return 4;
            }
        }
        const auto placement=caller->PositionValid(pos,caller->Direction(),context,true);
        if (placement.Status==VMPlacementError::Success) return 6;
        if (placement.Status==VMPlacementError::CantBeThroughWall) { FailCode=VMRouteFailCode::WallInWay;return 3; }
        if (placement.Status!=VMPlacementError::CantIntersectOtherObjects) return 0;
        if (!placement.Object || placement.Object==TS1IgnoredBlocker) return 6;
        Blocker=placement.Object;
        const bool avatar=dynamic_cast<VMAvatar*>(Blocker)!=nullptr;
        FailCode=avatar?VMRouteFailCode::DestTileOccupiedPerson:VMRouteFailCode::DestTileOccupied;
        return avatar?5:1;
    };
    auto setFacing=[&](unsigned search,bool point) {
        // FaceAnywhere preserves even the actor's fractional radian cache.
        caller->RadianDirection(originalRadians);
        int heading=-1;
        if (point) heading=(base+4)&7;
        else if (int(Slot->Facing)==-2) heading=(search+4)&7;
        else if (int(Slot->Facing)==-1) heading=int(search);
        else if (int(Slot->Facing)>=0) heading=(base+unsigned(Slot->Facing))&7;
        if (heading>=0) caller->Direction(FSO::LotView::Model::Direction(1u<<heading));
    };
    // 483432 prefers the caller's position, or its nearest admissible chair
    // exit. These probes precede generation and consume no score-noise draws.
    const st::Point callerPoint(caller->Position().x,caller->Position().y);
    st::Point preferred=callerPoint;
    //483432 calls43270d/432664 once: collapse source0 ancestors,
    //then use the resolved physical parent; do not climb physical owners.
    const auto placement=context->TS1ResolvePhysicalPlacement(caller,true);
    auto* seated=context->VM->GetObjectById(placement.ParentID);
    if (TS1HasChairEntry(seated) && seated->Object && seated->Object->OBJ) {
        const int heading=TS1Heading(seated);
        const unsigned entry=seated->Object->OBJ->ChairEntryFlags;
        bool found=false;int64_t nearest=0;
        if (heading>=0) for (unsigned k=0;k<8;++k) if (entry&(1u<<k)) {
            const unsigned direction=(unsigned(heading)+k)&7;
            const auto offset=TS1Neighbor[direction];
            const st::Point point(callerPoint.X+offset.X,callerPoint.Y+offset.Y);
            const int status=admit(point,int(direction));
            if (status!=6 && status!=5) continue;
            const int64_t dx=int64_t(target.x)-point.X,dy=int64_t(target.y)-point.Y;
            const int64_t distance=dx*dx+dy*dy;
            if (!found || distance<nearest) { preferred=point;nearest=distance;found=true; }
        }
    }
    const auto walls=flags&0x800?0:TS1WallDirections(context,target,base,pending);
    TS1SlotGeometryContext geometry;
    geometry.Center=center;geometry.TargetHeading=unsigned(targetHeading);geometry.TargetWallMask=uint8_t(walls);
    geometry.AllowSitting=person && person->GetPersonData(VMPersonDataVariable::Gender)<2;
    const int32_t sitting=geometry.AllowSitting?Slot->Sitting:0;
    const int32_t standing=std::max(Slot->Standing,Slot->Ground);
    const bool onlySitting=(flags&255) && sitting>0 && standing==0;
    bool chairOccupied=false,sawPerson=false,sawObject=false,sawWall=false,sawAltitude=false;
    VisitTS1SlotRegionCells(*Slot,geometry,[&](const TS1SlotRegionCell& cell) {
        setFacing(cell.Heading,cell.PointOnly);
        bool stopAfterRow=false;
        const int last=cell.PointOnly?-1:8;
        for (int probe=-1;probe<=last;++probe) {
            VMEntity* chair=nullptr;
            st::Point radius=cell.RadiusPoint,comparison=cell.PreferredPosition;
            uint8_t entry=cell.Region;
            int32_t weight=standing;
            if (!cell.PointOnly) {
                if (probe==-1 || probe==8) {
                    if (probe!=(standing>=sitting?-1:8)) continue;
                } else {
                    if (!sitting) continue;
                    radius.X+=TS1Neighbor[probe].X;radius.Y+=TS1Neighbor[probe].Y;
                    const int iy=radius.Y>>4,ix=radius.X>>4;
                    const bool edge=iy==cell.Radial ? cell.Radial<=ix && ix<=cell.TangentEnd :
                        iy<=cell.Radial || (cell.Radial<ix && ix<cell.TangentEnd);
                    if (!edge || !ProjectTS1SlotPoint(center,cell.Heading,float(radius.X),float(radius.Y),comparison)) continue;
                    if (comparison.X<std::numeric_limits<int16_t>::min() || comparison.X>std::numeric_limits<int16_t>::max() ||
                        comparison.Y<std::numeric_limits<int16_t>::min() || comparison.Y>std::numeric_limits<int16_t>::max()) continue;
                    const LotTilePos lookup(int16_t(comparison.X),int16_t(comparison.Y),target.Level);
                    if (!TS1ChairBounds(context,lookup)) continue;
                    chair=TS1ObjectAt(context,lookup);
                    if (!TS1HasChairEntry(chair) || TS1Heading(chair)!=int((cell.Heading+4)&7) ||
                        !chair->Object || !chair->Object->OBJ || !(chair->Object->OBJ->ChairEntryFlags&(1u<<probe))) continue;
                    auto* occupant=context->TS1SourceSlot(chair,1);
                    if (occupant && occupant!=caller) { Blocker=occupant;chairOccupied=true;continue; }
                    entry=uint8_t(1u<<probe);weight=sitting;
                }
                if (!weight || !IsTS1SlotProbeInRange(*Slot,radius)) continue;
            }
            TS1SlotScoreInput input;
            input.RadiusPoint=radius;input.Weight=weight;input.Region=cell.Region;
            input.TargetWallMask=uint8_t(walls);input.Preferred=comparison==preferred;input.PointOnly=cell.PointOnly;
            if (!cell.PointOnly && (flags&0x2000)) input.RandomSample=uint32_t(context->NextRandom(UINT64_C(0x100000000)));
            int32_t score;
            if (!ScoreTS1SlotProbe(*Slot,input,score)) continue;
            int direction=cell.Heading;
            if (cell.PointOnly && ((cell.Position.X^center.X)&~15)==0 && ((cell.Position.Y^center.Y)&~15)==0) direction=-1;
            else if (cell.PointOnly) direction=TS1SlotHeadingBetween(center,cell.Position);
            const int status=admit(cell.Position,direction);
            sawObject |= status==1;sawPerson |= status==5;
            sawWall |= status==2 || status==3;sawAltitude |= status==4;
            if (status==3) stopAfterRow=true;
            if (status!=6) continue;
            VMFindLocationResult result;
            result.Position=LotTilePos(int16_t(cell.Position.X),int16_t(cell.Position.Y),level);
            result.Score=score;result.Chair=chair;result.RouteEntryFlags=SLOTFlags(entry);
            TS1MaximumScore=std::max(TS1MaximumScore,score);
            result.FaceAnywhere=int(Slot->Facing)==-3;
            // 45c4fa final-facing uses the actual target, independently of
            // the Absolute/group-center settings used by candidate admission.
            const st::Point finalTarget(target.x,target.y);
            int finalHeading=targetHeading;
            if (int(Slot->Facing)>=0) finalHeading=(targetHeading+int(Slot->Facing))&7;
            else if (int(Slot->Facing)!=-3) {
                finalHeading=cell.PointOnly && cell.Position==finalTarget ? targetHeading :
                    TS1SlotHeadingBetween(cell.Position,finalTarget);
                if (int(Slot->Facing)==-1) finalHeading=(finalHeading+4)&7;
            }
            result.RadianDirection=float(finalHeading*(M_PI/4));
            Results.push_back(result);break;
        }
        return stopAfterRow?TS1SlotRowControl::StopAfterRow:TS1SlotRowControl::Continue;
    });
    const size_t cap=callerRoom>=0 && size_t(callerRoom)<context->EnvironmentRooms.size() &&
        context->EnvironmentRooms[size_t(callerRoom)].Environment.HalfTiles/2>250?8:48;
    if (Results.size()>cap) {
        auto all=std::move(Results);Results.clear();Results.reserve(cap);
        for (auto& result:all) if (result.Chair) { Results.push_back(result);result.Score=0; }
        const size_t attempts=cap>Results.size()?cap-Results.size():0;
        for (size_t attempt=0;attempt<attempts;++attempt) {
            const size_t start=size_t(context->NextRandom(all.size()));
            for (size_t i=start;i<all.size();++i) if (all[i].Score!=0) {
                Results.push_back(all[i]);all[i].Score=0;break;
            }
        }
    }
    // DVD484015 aggregates failures after all probes, in this exact order.
    // Code0 also means that no explanatory rejection was found; result
    // presence, not the numeric failure field, decides route success.
    if (!Results.empty()) FailCode=VMRouteFailCode::Success;
    else if (chairOccupied) FailCode=VMRouteFailCode::DestChairOccupied;
    else if (sawPerson) FailCode=VMRouteFailCode::DestTileOccupiedPerson;
    else if (sawObject) FailCode=VMRouteFailCode::DestTileOccupied;
    else if (sawWall) FailCode=VMRouteFailCode::WallInWay;
    else if (sawAltitude) FailCode=VMRouteFailCode::AltsDontMatch;
    else FailCode=onlySitting?VMRouteFailCode::NoChair:VMRouteFailCode::Success;
    return Results;
}

double VMSlotParser::FlagsAsRad(SLOTFlags dir) {
    double value = std::nearbyint(std::log2((double)((int)dir & 255))) * M_PI / 4;
    //if (value > Math.PI) value -= 2*Math.PI;
    return value;
}

SLOTFlags VMSlotParser::GetSearchDirection(Vector2 pos1, Vector2 pos2, float rotShift) {
    double dir = std::atan2(std::floor(pos2.X) - std::floor(pos1.X), std::floor(pos1.Y) - std::floor(pos2.Y)) * (180.0 / M_PI);
    dir = DirectionUtils::NormalizeDegrees(dir - rotShift * (180.0 / M_PI));
    SLOTFlags result = (SLOTFlags)0;
    if (dir >= -45.0 - ANGLE_ERROR && dir <= 45.0 + ANGLE_ERROR) result |= SLOTFlags::NORTH;
    if (dir >= 0.0 - ANGLE_ERROR && dir <= 90.0 + ANGLE_ERROR) result |= SLOTFlags::NORTH_EAST;
    if (dir >= 45.0 - ANGLE_ERROR && dir <= 135.0 + ANGLE_ERROR) result |= SLOTFlags::EAST;
    if ((dir >= 90.0 - ANGLE_ERROR && dir <= 180.0 + ANGLE_ERROR) || (dir <= -180.0 + ANGLE_ERROR)) result |= SLOTFlags::SOUTH_EAST;
    if (dir >= 135.0 - ANGLE_ERROR || dir <= -135.0 + ANGLE_ERROR) result |= SLOTFlags::SOUTH;
    if ((dir >= -180.0 - ANGLE_ERROR && dir <= -90.0 + ANGLE_ERROR) || (dir >= 180.0 - ANGLE_ERROR)) result |= SLOTFlags::SOUTH_WEST;
    if (dir >= -135.0 - ANGLE_ERROR && dir <= -45.0 + ANGLE_ERROR) result |= SLOTFlags::WEST;
    if (dir >= -90.0 - ANGLE_ERROR && dir <= 0.0 + ANGLE_ERROR) result |= SLOTFlags::NORTH_WEST;
    return result;
}

SLOTFlags VMSlotParser::RadianToFlags(double rad) {
    int result = (int)(std::nearbyint((rad / (M_PI * 2)) * 8) + 80) % 8; //for best results, make sure rad is >-pi and <pi
    return (SLOTFlags)(1 << result);
}

double VMSlotParser::GetDirectionTo(Vector2 pos1, Vector2 pos2) {
    return std::atan2(pos2.X - pos1.X, -(pos2.Y - pos1.Y));
}
}}}
