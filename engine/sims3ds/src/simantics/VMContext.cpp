/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.VMContext
#include "simantics/VMContext.h"
#include "simantics/VM.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMRoutingFrame.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMAmbientSound.h"
#include "simantics/engine/VMChangeActionString.h"
#include "simantics/model/VMBuildableAreaInfo.h"
#include "simantics/model/TSOPlatform/VMTSOLotState.h"
#include "content/TS1NeighbourProvider.h"
#include <random>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace FSO { namespace SimAntics {
using namespace FSO::LotView::Model;
using namespace FSO::SimAntics::Model;
using namespace FSO::Files::Formats::IFF::Chunks;
using FSO::LotView::Components::ObjectComponent;
using FSO::LotView::Components::AvatarComponent;
using Engine::VMPrimitiveRegistration;

bool VMContext::UseWorld = true;

// The complete engine requires both primitive tables. Keep these calls unresolved
// here so static-library linking must pull in their real implementations. Empty
// weak defaults silently discarded the tables and made missing opcodes succeed.

void VMContext::Architecture(std::unique_ptr<FSO::SimAntics::VMArchitecture> value) {
    if (_Arch != nullptr) _Arch->WallsChanged = nullptr;
    value->WallsChanged = [this](FSO::SimAntics::VMArchitecture* caller) { WallsChanged(caller); };
    _Arch = std::move(value);
}

VMContext::VMContext(FSO::LotView::World* world) : VMContext(world, nullptr) {}

VMContext::VMContext(FSO::LotView::World* world, VMContext* oldContext)
    : VMContext(world, oldContext, true) {}

VMContext::VMContext(FSO::LotView::World* world, VMContext* oldContext, bool consumeDebugSeed) {
    //oldContext is passed in case we need to inherit certain things, like the ambient sound player
    this->World = world;
    this->Clock.reset(new FSO::SimAntics::VMClock());
    this->SetToNextCache.reset(new VMSetToNextCache(this));
    this->ObjectQueries.reset(new VMObjectQueries(this));

    if (oldContext == nullptr) {
        this->Ambience = std::make_shared<Engine::VMAmbientSound>();
    } else {
        this->Ambience = oldContext->Ambience;
    }

    std::random_device rd;
    RandomSeed = ((uint64_t)rd() << 32) ^ (uint64_t)rd(); //when resuming state, this should be set.
    if (RandomSeed == 0) RandomSeed = 0x2545F4914F6CDD1DULL;
    if (consumeDebugSeed && DebugSeed != 0) RandomSeed = DebugSeed++ * 2654435761ULL + 1; // FIX(sims) QA: repeatable host runs
    Clock->TicksPerMinute = 30; //1 minute per irl second

    // Primitive table (C# registers each primitive inline here; see README.md for the full opcode list).
    RegisterDefaultPrimitives(this);
    RegisterTS1ExpansionPrimitives(this); // TS1(sims): primitives/VMTS1Primitives.cpp (25 Alter Budget, 51 Manage Inventory, ...)
    if (Primitives[50] == nullptr) {
        auto reg = new VMPrimitiveRegistration(new Primitives::VMChangeActionString());
        reg->Opcode = 50;
        reg->Name = "change_action_string";
        reg->OperandModel = VMPrimitiveRegistration::Model<Primitives::VMChangeActionStringOperand>();
        AddPrimitive(reg);
    }
}

VMContext::~VMContext() {}

/// Returns a random number between 0 and less than the specified maximum.
uint64_t VMContext::DebugSeed = 0; // FIX(sims) QA

uint64_t VMContext::NextRandom(uint64_t max) {
    if (max == 0) return 0;
    RandomSeed ^= RandomSeed >> 12;
    RandomSeed ^= RandomSeed << 25;
    RandomSeed ^= RandomSeed >> 27;
    return (RandomSeed * (uint64_t)(2685821657736338717ULL)) % max;
}

void VMContext::WallsChanged(FSO::SimAntics::VMArchitecture* /*caller*/) {
    RegeneratePortalInfo();

    //TODO: this could get very slow! find a way to make this quicker.
    for (auto obj : VM->Entities) {
        if (dynamic_cast<VMAvatar*>(obj) != nullptr && obj->Thread != nullptr) {
            for (auto& frame : obj->Thread->Stack) {
                if (frame->IsRoutingFrame()) {
                    ((Engine::VMRoutingFrame*)frame.get())->InvalidateRoomRoute();
                }
            }
        }
    }
}

ObjectComponent* VMContext::MakeObjectComponent(FSO::Content::GameObject* obj) {
    // if (UseWorld) return World.MakeObjectComponent(obj);
    return new ObjectComponent(obj);
}

bool VMContext::IsUserOutOfBounds(const LotTilePos& pos) {
    // TS1(sims): map_edit cheat: objects may go anywhere on the lot (street, sidewalk)
    if (_Arch->MapEdit) return pos.TileX() < 0 || pos.TileY() < 0 || pos.TileX() >= _Arch->Width || pos.TileY() >= _Arch->Height || pos.Level < 1 || pos.Level > _Arch->BuildableFloors;
    auto& fine = Architecture()->FineBuildableArea;
    if (!fine.empty()) {
        if (pos.TileX() < 0 || pos.TileX() >= _Arch->Width) return false;
        else if (pos.TileY() < 0 || pos.TileY() >= _Arch->Height) return false;
        else if (pos.Level < 1 || pos.Level > _Arch->BuildableFloors) return false;
        else {
            return !fine[pos.TileX() + pos.TileY() * _Arch->Width];
        }
    } else {
        auto area = Architecture()->BuildableArea;
        return (pos.TileX() < area.X || pos.TileY() < area.Y || pos.Level < 1 || pos.TileX() >= area.Right() || pos.TileY() >= area.Bottom() || pos.Level > _Arch->BuildableFloors);
    }
}

void VMContext::UpdateTSOBuildableArea() {
    //if (VM.TS1) return;
    VMBuildableAreaInfo::UpdateOverbudgetObjects(VM);
    auto lotSInfo = VM->TSOState()->Size;
    auto area = GetTSOBuildableArea(lotSInfo);
    Architecture()->UpdateBuildableArea(area, ((lotSInfo >> 8) & 255) + 2);
}

st::Rectangle VMContext::GetTSOBuildableArea(int32_t lotSInfo) {
    if (Architecture() == nullptr) return st::Rectangle();
    //note: sync on this DOES matter as the OOB check performs on some primitives, and objects are double checked before placement.

    auto lotSize = lotSInfo & 255;
    auto lotDir = (lotSInfo >> 16);

    auto dim = VMBuildableAreaInfo::BuildableSizes[std::min<int32_t>(10, lotSize)];

    //need to rotate the lot dir towards the road. bit weird cos we're rotating a rectangle

    float w = (float)Architecture()->Width;
    float h = (float)Architecture()->Height;
    st::Vector2 corners[4] = {
        st::Vector2(6, 6), // top, default orientation
        st::Vector2(w - 7, 6), // right
        st::Vector2(w - 7, h - 7), // bottom
        st::Vector2(6, h - 7) // left
    };
    st::Vector2 perpIncrease[4] = {
        st::Vector2(0, -1), //bottom left road side
        st::Vector2(-1, 0),
        st::Vector2(0, -1),
        st::Vector2(-1, 0)
    };

    //rotation 0: move perp from closer point to top bottom -> left (90 degree ccw of perp)
    //rotation 1: choose closer pt to top left->top (90 degree ccw of perp)
    //rotation 2: choose closer pt to top top->right (90 degree cw of perp)

    auto pt1 = corners[(lotDir + 2) % 4];
    auto pt2 = corners[(lotDir + 3) % 4];

    auto ctr = (pt1 + pt2) / 2;
    auto lotBase = ctr + perpIncrease[((3 - lotDir) % 4 + 4) % 4] * (float)dim / 2;
    if (lotDir == 0 || lotDir == 3) lotBase += perpIncrease[lotDir] * (float)(dim);

    return st::Rectangle((int)lotBase.X, (int)lotBase.Y, dim, dim);
}

static Common::TS1::SpatialEmitter SpatialSource(VMContext& ctx, VMEntity* entity);

void VMContext::RegeneratePortalInfo() {
    RoomInfo.assign(Architecture()->RoomData.size(), VMRoomInfo());
    for (size_t i = 0; i < RoomInfo.size(); i++) {
        RoomInfo[i].Entities.clear();
        RoomInfo[i].Portals.clear();
        RoomInfo[i].Room = Architecture()->RoomData[i];
        // TS1(sims): FreeSO keys a room's lighting and room score by its "light base room" (the first of a group
        // of rooms joined by open divisions). This port does not group rooms, so every room is its own base. FreeSims
        // left it 0, so GetRoomScore always returned room 0's score (0): the Room motive was always 0.
        RoomInfo[i].Room.LightBaseRoom = (uint16_t)i;
        RoomInfo[i].Light = RoomLighting();
    }

    for (auto obj : VM->Entities) {
        auto room = GetObjectRoom(obj);
        VM::AddToObjList(RoomInfo[room].Entities, obj);
        if (obj->EntryPoints[15].ActionFunction != 0) { //portal object
            AddRoomPortal(obj, room);
        }
        obj->SetRoom(room);
    }

    RebuildEnvironmentGeometry();
    EnvironmentPeriod = -1;
    EnvironmentObjectsDirty = true;
    for (uint16_t i = 0; i < RoomInfo.size(); i++) {
        RefreshLighting(i, i == (RoomInfo.size() - 1));
    }
    if (VM && VM->TS1) {
        // F0 updates every object's Room29 after the new flood, before the
        // synchronous manager pass. Preserve head/lifetime history; only the
        // canonical room identity changes through a split or merge.
        for (auto* entity : VM->Entities) {
            const auto found = SpatialPlacements.find(entity);
            if (found != SpatialPlacements.end()) found->second.Room = SpatialSource(*this, entity).ObjectRoom;
        }
        FlushSpatialLighting();
    }
    if (VM::UseWorld() && World != nullptr) World->InvalidateZoom();
}

void VMContext::RefreshLighting(uint16_t room, bool commit) {
    if (RoomInfo.empty() || room >= RoomInfo.size()) return;
    auto& info = RoomInfo[room];
    info.Light.AmbientLight = 0;
    info.Light.OutsideLight = 0;
    if (info.Room.IsOutside) {
        info.Light.OutsideLight = 100;
    } else {
        float areaScale = std::max(1.0f, info.Room.Area / 100.0f);
        for (auto ent : info.Entities) {
            auto flags2 = (VMEntityFlags2)ent->GetValue(VMStackObjectVariable::FlagField2);
            auto cont = ent->GetValue(VMStackObjectVariable::LightingContribution);
            if (cont > 0) {
                if ((flags2 & (VMEntityFlags2::ArchitectualWindow | VMEntityFlags2::ArchitectualDoor)) > 0)
                    info.Light.OutsideLight += (uint16_t)std::min(100.0f, cont / areaScale);
                else
                    info.Light.AmbientLight += (uint16_t)std::min(100.0f, cont / areaScale);
            }
        }
        if (info.Light.OutsideLight > 100) info.Light.OutsideLight = 100;
    }

    RefreshRoomScore(room); // TS1(sims): FreeSO computes the room score with the lighting

    if (commit && UseWorld && Blueprint != nullptr) {
        Blueprint->Light.resize(RoomInfo.size());
        for (size_t i = 0; i < RoomInfo.size(); i++) {
            Blueprint->Light[i] = RoomInfo[i].Light;
        }
        Blueprint->Damage.push_back(BlueprintDamage(BlueprintDamageType::LIGHTING_CHANGED));
    }
}

// Legacy non-TS1 consumer retains its existing visual-room score. TS1 uses the
// separately cached original environment partitions, never native room aliases.
void VMContext::RefreshRoomScore(uint16_t room) {
    VisualLightingDirty = true;
    EnvironmentObjectsDirty = true;
    if (VM && VM->TS1) return;
    if (RoomInfo.empty() || room == 0 || room >= RoomInfo.size()) return;
    room = RoomInfo[room].Room.LightBaseRoom;
    if (room >= RoomInfo.size()) return;
    auto& info = RoomInfo[room];
    int32_t roomScore = 0;
    for (auto ent : info.Entities) {
        auto roomImpact = ent->GetValue(VMStackObjectVariable::RoomImpact);
        if (roomImpact != 0) roomScore += roomImpact;
    }
    float areaRScale = std::max(1.0f, info.Room.Area / 12.0f);
    if (info.Room.IsOutside) areaRScale = 30;
    roomScore = (int16_t)(roomScore / areaRScale);
    roomScore -= (info.Room.IsOutside) ? 15 : 10;
    info.Light.RoomScore = (int16_t)std::min<int32_t>(100, std::max<int32_t>(-100, roomScore));
}

void VMContext::RefreshEnvironmentScores() {
    const int period = Clock ? Clock->TimeOfDay() : 0;
    if (!EnvironmentObjectsDirty && period == EnvironmentPeriod) return;
    ++EnvironmentScoreRefreshes;
    if (EnvironmentObjectsDirty) {
        ++EnvironmentObjectPasses;
        for (auto& room : EnvironmentRooms) {
            auto& input = room.Environment;
            input.RoomImpact = input.WindowLight = input.ArtificialLight = input.GoodObjectCount = 0;
        }
        auto architecture = Architecture();
        // One entity pass per coalesced invalidation; no geometry scans per actor.
        if (VM && architecture) for (auto ent : VM->Entities) {
            if (!ent || ent->Dead || ent->GetValue(VMStackObjectVariable::Hidden) != 0 ||
                ent->GetValue(VMStackObjectVariable::Room) < 0) continue;
            const auto pos = ent->Position();
            if (GetEnvironmentRoomAt(pos) < 0 || pos.Level < 1 ||
                size_t(pos.Level) > EnvironmentTileRooms.size()) continue;
            const auto& tiles = EnvironmentTileRooms[pos.Level - 1];
            const auto entries = tiles.find(pos.TileY() * architecture->Width + pos.TileX());
            if (entries == tiles.end()) continue;
            // The PC scans every object at a visited coordinate. A split tile
            // can contribute to both adjacent environment rooms.
            for (const auto& entry : entries->second) {
                const auto id = entry.first;
                if (id == 0 || id >= EnvironmentRooms.size()) continue;
                auto& room = EnvironmentRooms[id];
                const int32_t weight = int32_t(entry.second);
                auto& input = room.Environment;
                const auto impact = ent->GetValue(VMStackObjectVariable::RoomImpact);
                if (!room.Outside || impact < 0) input.RoomImpact += impact * weight;
                if (impact >= 0) input.GoodObjectCount += weight;
                if ((uint16_t(ent->GetValue(VMStackObjectVariable::FlagField2)) & 0x400) != 0 &&
                    pos.TileX() > 0 && pos.TileY() > 0 && pos.TileX() < architecture->Width - 1 &&
                    pos.TileY() < architecture->Height - 1) {
                    const auto light = ent->GetValue(VMStackObjectVariable::LightingContribution);
                    if (ent->Object->OBJ->ObjectType == OBJDType::Portal) input.WindowLight += light * weight;
                    else input.ArtificialLight += light * weight;
                }
            }
        }
    }
    const Common::TS1::EnvironmentTunings defaults;
    auto content = Content::Content::Get();
    auto provider = content ? content->WorldObjectGlobals.get() : nullptr;
    if (provider) provider->InitCurves();
    const auto& tuning = provider ? provider->Environment : defaults;
    float outdoorSum = 0;
    for (size_t id = 1; id < EnvironmentRooms.size(); ++id) {
        auto& room = EnvironmentRooms[id];
        const auto result = Common::TS1::EvaluateEnvironment(room.Environment, tuning, room.Outside,
            Common::TS1::EnvironmentDaylight(period));
        room.Score = result.Score; room.LightFactor = result.LightFactor;
        if (room.Outside) outdoorSum += room.Score;
    }
    OutdoorEnvironmentScore = Common::TS1::EvaluateOutdoorEnvironment(outdoorSum, tuning, period);
    EnvironmentPeriod = period;
    EnvironmentObjectsDirty = false;
}

static void CollectVisualCounts(VMContext& ctx, std::array<int, 2048>& people, std::array<int, 2048>& lamps, bool refreshGraphics = false) {
    const auto* arch = ctx.Architecture();
    for (auto* ent : ctx.VM->Entities) {
        if (refreshGraphics) if (auto* object = dynamic_cast<VMGameObject*>(ent)) object->RefreshLight();
        if (!ent || ent->Dead || !ent->Object || !ent->Object->OBJ ||
            ent->GetValue(VMStackObjectVariable::Hidden) != 0 || ent->GetValue(VMStackObjectVariable::Room) < 0) continue;
        const auto pos = ent->Position();
        if (pos.Level < 1 || size_t(pos.Level) > ctx.EnvironmentTileRooms.size()) continue;
        if (pos.TileX() < 0 || pos.TileY() < 0 || pos.TileX() >= arch->Width || pos.TileY() >= arch->Height) continue;
        const auto& tiles = ctx.EnvironmentTileRooms[pos.Level - 1];
        const auto found = tiles.find(pos.TileY() * arch->Width + pos.TileX());
        if (found == tiles.end()) continue;
        const auto type = ent->Object->OBJ->ObjectType;
        const bool valid = pos.TileX() > 0 && pos.TileY() > 0 && pos.TileX() < arch->Width - 1 && pos.TileY() < arch->Height - 1;
        for (const auto& visit : found->second) {
            const auto id = ctx.EnvironmentRooms[visit.first].CanonicalID;
            if (type == OBJDType::Person) people[id] += int(visit.second);
            if (valid && type != OBJDType::Portal &&
                (uint16_t(ent->GetValue(VMStackObjectVariable::FlagField2)) & 0x400)) lamps[id] += int(visit.second);
        }
    }
}

void VMContext::ObserveVisualActor(VMEntity* entity, bool removing) {
    auto* actor = dynamic_cast<VMAvatar*>(entity);
    if (!actor || (!removing && actor->Dead) || !VM || !VM->TS1 || !Blueprint || !Architecture()) return;
    const int env = removing ? -5 : GetEnvironmentRoomAt(actor->Position());
    const int room = env > 0 ? int(EnvironmentRooms[size_t(env)].CanonicalID) : -5;
    const bool human = actor->GetPersonData(VMPersonDataVariable::Gender) < 2;
    auto& previous = VisualActorRooms[actor];
    const int old = previous.Room;
    previous = {room, human};
    if (old != room) VisualLightingDirty = true;
    if (old == room || !human) return;
    // Observe at the actor tick boundary. Several VM ticks may happen before
    // one draw; publication must not erase an intermediate visit to a pet room.
    RefreshEnvironmentScores();
    std::array<int, 2048> people{}, lamps{};
    CollectVisualCounts(*this, people, lamps);
    const auto setOverhead = [&](int id, bool value) {
        if (id < 0 || VisualRoomFlags[size_t(id)] != 0 || bool(VisualOverhead[size_t(id)]) == value) return;
        VisualOverhead[size_t(id)] = value;
        for (auto& record : EnvironmentRooms) if (record.Story && record.CanonicalID == id) {
            // The original setter tests the last manager collection, not a
            // newly counted render snapshot. Keep source events independent.
            if (record.SpatialLamps == 0 && record.SpatialLightFactor < .8f) SpatialSchedule.Mark(id);
            if (lamps[size_t(id)] == 0 && record.LightFactor < .8f) {
                record.VisualEventByte = Common::TS1::VisualByte(value ? .8f : record.LightFactor);
                record.VisualEventScoreVersion = EnvironmentScoreRefreshes;
            }
            break;
        }
    };
    if (old >= 0 && people[size_t(old)] == 0) setOverhead(old, false);
    if (room >= 0) setOverhead(room, true);
    VisualLightingDirty = true;
}

void VMContext::RemoveVisualActor(VMEntity* entity) {
    ObserveVisualActor(entity, true);
    VisualActorRooms.erase(entity);
    VisualLightingDirty = true;
}

static Common::TS1::SpatialEmitter SpatialSource(VMContext& ctx, VMEntity* entity) {
    Common::TS1::SpatialEmitter result;
    const auto pos = entity->Position();
    result.Position = {pos.TileX(), pos.TileY(), pos.Level};
    const auto env = ctx.GetEnvironmentRoomAt(pos);
    result.ObjectRoom = entity->GetValue(VMStackObjectVariable::Room) >= 0 && env > 0 && size_t(env) < ctx.EnvironmentRooms.size() ?
        int16_t(ctx.EnvironmentRooms[size_t(env)].CanonicalID) : int16_t(-5);
    result.Hidden = entity->GetValue(VMStackObjectVariable::Hidden);
    result.SourceMode = entity->GetValue(VMStackObjectVariable::LightSource);
    result.Contribution = entity->GetValue(VMStackObjectVariable::LightingContribution);
    result.Flags2 = uint16_t(entity->GetValue(VMStackObjectVariable::FlagField2));
    return result;
}

void VMContext::RefreshSpatialRoom(VMEntity* entity) {
    if (!VM || !VM->TS1 || !entity || entity->GetValue(VMStackObjectVariable::Room) < 0) return;
    const auto env = GetEnvironmentRoomAt(entity->Position());
    if (env > 0 && size_t(env) < EnvironmentRooms.size()) SpatialSchedule.Mark(EnvironmentRooms[size_t(env)].CanonicalID);
}

void VMContext::RefreshSpatialObject(VMEntity* entity) {
    // F2 resolves the object's current room and immediately broadcasts F1.
    RefreshSpatialRoom(entity);
}

void VMContext::ObserveSpatialPeriod() {
    if (!VM || !VM->TS1 || !Architecture()) return;
    const int period = Clock ? Clock->TimeOfDay() : 0;
    if (SpatialPeriod == period) return;
    SpatialPeriod = period;
    for (const auto& room : EnvironmentRooms) if (room.Story) SpatialSchedule.Mark(room.CanonicalID);
    // 00416050 DD is followed immediately by 0046716c. Do not lose A->B->A
    // phase transitions when multiple simulation ticks precede a single draw.
    FlushSpatialLighting();
}

void VMContext::FlushSpatialLighting() {
    if (!VM || !VM->TS1 || !Architecture() || !SpatialSchedule.StoryMask) return;
    RefreshEnvironmentScores(); // independent existing logical-score cache
    const auto* arch = Architecture();
    Common::TS1::SpatialRoomMapView map{arch->Width, arch->Height, &EnvironmentMaps, &SpatialCanonicalIds};
    std::vector<uint16_t> ordered;
    std::array<size_t, 2048> indices{};
    for (size_t i = 1; i < EnvironmentRooms.size(); ++i) {
        const auto id = EnvironmentRooms[i].CanonicalID;
        ordered.push_back(id); indices[id] = i;
    }
    std::sort(ordered.begin(), ordered.end());
    // Allocate before consuming any dirty bit or mutating source cells. Reuse
    // this bounded DFS workspace across occupied visits, rather than allocate
    // once per tile during a partially completed collection.
    std::vector<VMEntity*> pending;
    pending.reserve(VM->Entities.size() + 1);
    ++SpatialCollectorPasses;
    SpatialSchedule.Flush(ordered, [&](uint16_t id) {
        auto& room = EnvironmentRooms[indices[id]];
        // 00466d14 snapshots BEFORE resetting counters. Overhead can have
        // changed since the last collection, so caching the old effective
        // result would introduce an extra F1 and alter future source order.
        const float previousFactor = Common::TS1::DirectVisualFactor(room.SpatialLightFactor,
            room.SpatialPool, VisualOverhead[id], room.SpatialLamps, room.SpatialPeople);
        int people = 0, lamps = 0;
        for (const auto tile : room.SpatialTileOrder) {
            if (tile < 0 || size_t(tile) >= SpatialTileHeads[room.Story - 1].size()) continue;
            auto* root = VM->GetObjectById(SpatialTileHeads[room.Story - 1][size_t(tile)]);
            if (!root) continue;
            // 0043f371/0043f3b9: root followed by ascending-slot preorder DFS.
            // Never use the sorted TileToObjects cache or current entity order.
            pending.clear(); pending.push_back(root);
            size_t remaining = VM->Entities.size() + 1;
            while (!pending.empty() && remaining--) {
                auto* entity = pending.back(); pending.pop_back();
                if (!entity || entity->Dead) continue;
                const auto source = SpatialSource(*this, entity);
                ++SpatialObjectVisits;
                if (!source.Hidden && source.ObjectRoom >= 0 && entity->Object && entity->Object->OBJ) {
                    const auto type = entity->Object->OBJ->ObjectType;
                    if (type == OBJDType::Person) ++people;
                    if ((source.Flags2 & 0x400) && SpatialLights.Interior(source.Position) && type != OBJDType::Portal) ++lamps;
                }
                SpatialLights.CollectVisit(source, map);
                for (int slot = entity->TotalSlots() - 1; slot >= 0; --slot)
                    if (auto* child = entity->GetSlot(slot)) pending.push_back(child);
            }
        }
        const float factor = Common::TS1::DirectVisualFactor(room.LightFactor, room.Pool,
            VisualOverhead[id], lamps, people);
        room.SpatialLightFactor = room.LightFactor; room.SpatialPool = room.Pool;
        room.SpatialLamps = lamps; room.SpatialPeople = people;
        if (previousFactor != factor) SpatialSchedule.Mark(id); // synchronous original changed-factor F1
    });
    SpatialSnapshotDirty = true;
}

void VMContext::RemoveSpatialEntity(VMEntity* entity) {
    // Normal removal already ran PrePositionChange. The fallback is for direct
    // RemoveEntity users, without inventing a second duplicate-pair erasure.
    const auto found = SpatialPlacements.find(entity);
    if (found != SpatialPlacements.end() && found->second.Registered) {
        const auto source = SpatialSource(*this, entity);
        SpatialLights.Unregister(source);
        if (SpatialLights.Interior(source.Position) && !entity->Container)
            SpatialTileHeads[size_t(source.Position.Story - 1)][size_t(source.Position.Y) * SpatialLights.Width + source.Position.X] = 0;
        if (source.Flags2 & 0x400) RefreshSpatialObject(entity);
        SpatialSnapshotDirty = true;
    }
    InvalidateRoofObject(entity);
    // 0044127f captures old room before removing and sends F3 afterward,
    // including the normal path that has already unregistered its placement.
    if (found != SpatialPlacements.end() && entity->GetValue(VMStackObjectVariable::RoomImpact) != 0)
        SpatialSchedule.Mark(found->second.Room);
    SpatialPlacements.erase(entity);
}

Common::TS1::VisualLighting VMContext::BuildPictureVisualLighting(int period) {
    if (period < 0 || period > 3) throw std::invalid_argument("Invalid picture lighting period");
    if (!Blueprint || !Architecture()) return {};
    // PrepareVisualLighting must settle the actual live period before this
    // detached snapshot. Picture capture never changes the simulation's clock,
    // room scores, event bytes, occupancy history or refresh counters.
    auto visual = Blueprint->VisualLight;
    visual.Period=period;
    if (!visual.Enabled || !VM || !VM->TS1) return visual;
    const Common::TS1::EnvironmentTunings defaults;
    const auto content = Content::Content::Get();
    const auto provider = content ? content->WorldObjectGlobals.get() : nullptr;
    const auto& tuning = provider ? provider->Environment : defaults;
    std::array<int,2048> people{},lamps{};
    CollectVisualCounts(*this,people,lamps,false);
    visual.Tones.assign(EnvironmentRooms.size()*2,0);
    visual.Intensities.assign(EnvironmentRooms.size()*2,0);
    visual.Grid.assign(size_t(2)*visual.Width*visual.Height,0);
    for (size_t i=1;i<EnvironmentRooms.size();++i) {
        const auto& room=EnvironmentRooms[i];
        const auto light=Common::TS1::EvaluateEnvironment(room.Environment,tuning,room.Outside,
            Common::TS1::EnvironmentDaylight(period)).LightFactor;
        const auto id=room.CanonicalID;
        const auto direct=Common::TS1::DirectVisualFactor(light,room.Pool,
            VisualOverhead[id],lamps[id],people[id]);
        const auto byte=Common::TS1::VisualByte(room.Outside?light:direct);
        constexpr float inv255=1.0f/255.0f;
        visual.Tones[i*2]=Common::TS1::VisualTone(float(byte*inv255),period);
        visual.Tones[i*2+1]=Common::TS1::VisualTone(direct,period);
        visual.Intensities[i*2]=Common::TS1::VisualIntensity(float(byte*inv255),period);
        visual.Intensities[i*2+1]=Common::TS1::VisualIntensity(direct,period);
        for (const auto& tile:room.EnvironmentTileVisits)
            visual.Grid[size_t(room.Story-1)*visual.Width*visual.Height+size_t(tile.first)]=byte;
    }
    visual.Period=period;
    // A different renderer snapshot must not inherit a token for cached pixels
    // with different colors. Capture restores the original snapshot and tokens.
    ++visual.Generation;
    ++visual.IntensityGeneration;
    return visual;
}

void VMContext::PrepareVisualLighting() {
    if (!VM || !VM->TS1 || !Blueprint || !Architecture()) return;
    PrepareRoofGeometry();
    ObserveSpatialPeriod();
    FlushSpatialLighting();
    const int period = Clock ? Clock->TimeOfDay() : 0;
    if (!VisualLightingDirty && !EnvironmentObjectsDirty && !SpatialSnapshotDirty && VisualLightingPeriod == period) return;
    RefreshEnvironmentScores();
    ++VisualLightingRefreshes;
    const auto* arch = Architecture();
    if (!Blueprint->VisualLight.Enabled) {
        for (auto* ent : VM->Entities) ObserveVisualActor(ent);
        FlushSpatialLighting(); // bootstrap actor F1 belongs before this snapshot
    }
    std::array<int, 2048> people{}, lamps{};
    CollectVisualCounts(*this, people, lamps, true);
    for (size_t i = 1; i < EnvironmentRooms.size(); ++i) {
        auto& room = EnvironmentRooms[i];
        const auto id = room.CanonicalID;
        if (VisualScoreVersion != EnvironmentScoreRefreshes || room.VisualPeople != people[id] ||
            room.VisualLamps != lamps[id] || room.VisualInputLight != room.LightFactor) {
            const float direct = Common::TS1::DirectVisualFactor(room.LightFactor, room.Pool,
                VisualOverhead[id], lamps[id], people[id]);
            room.VisualMapByte = Common::TS1::VisualByte(room.Outside ? room.LightFactor : direct);
            room.VisualPeople = people[id]; room.VisualLamps = lamps[id]; room.VisualInputLight = room.LightFactor;
        }
    }
    for (auto& record : EnvironmentRooms)
        if (record.VisualEventByte >= 0 && record.VisualEventScoreVersion == EnvironmentScoreRefreshes)
            record.VisualMapByte = uint8_t(record.VisualEventByte);
    auto& visual = Blueprint->VisualLight;
    const bool wasEnabled = visual.Enabled;
    const bool periodChanged = visual.Period != period;
    const bool geometryChanged = visual.GeometryGeneration != VisualGeometryVersion;
    std::vector<std::vector<uint32_t>> maps;
    if (geometryChanged) maps.assign(EnvironmentMaps.begin(), EnvironmentMaps.begin() + std::min<size_t>(2, EnvironmentMaps.size()));
    if (EnvironmentRooms.size() * 2 >= Common::TS1::VisualLighting::BaseKey - Common::TS1::VisualLighting::FirstKey ||
        Blueprint->Light.size() >= Common::TS1::VisualLighting::FirstKey)
        throw std::runtime_error("TS1 visual sample capacity exceeded");
    std::vector<uint8_t> tones(EnvironmentRooms.size() * 2);
    std::vector<uint8_t> intensities(EnvironmentRooms.size() * 2);
    std::vector<uint8_t> grid(size_t(2) * arch->Width * arch->Height, 0);
    const bool spatialChanged = !wasEnabled || visual.Width != arch->Width || visual.Height != arch->Height || visual.SpatialMaps != SpatialLights.Maps;
    std::vector<std::vector<Common::TS1::SpatialCell>> spatialMaps;
    if (spatialChanged) spatialMaps = SpatialLights.Maps;
    for (size_t i = 1; i < EnvironmentRooms.size(); ++i) {
        const auto& room = EnvironmentRooms[i];
        const float direct = Common::TS1::DirectVisualFactor(room.LightFactor, room.Pool,
            VisualOverhead[room.CanonicalID], lamps[room.CanonicalID], people[room.CanonicalID]);
        const uint8_t byte = room.VisualMapByte;
        constexpr float inv255 = 1.0f / 255.0f;
        tones[i * 2] = Common::TS1::VisualTone(float(byte * inv255), period);
        tones[i * 2 + 1] = Common::TS1::VisualTone(direct, period);
        intensities[i * 2] = Common::TS1::VisualIntensity(float(byte * inv255), period);
        intensities[i * 2 + 1] = Common::TS1::VisualIntensity(direct, period);
        for (const auto& tile : room.EnvironmentTileVisits)
            grid[size_t(room.Story - 1) * arch->Width * arch->Height + size_t(tile.first)] = byte;
    }
    // Publish one coherent renderer-owned snapshot after all allocations succeed.
    visual.Width = arch->Width; visual.Height = arch->Height;
    if (geometryChanged) { visual.Maps.swap(maps); visual.GeometryGeneration = VisualGeometryVersion; }
    visual.Grid.swap(grid);
    visual.Enabled = true;
    visual.Period = period;
    const bool intensityChanged = !wasEnabled || visual.Intensities != intensities;
    const bool tonesChanged = !wasEnabled || visual.Tones != tones;
    if (spatialChanged) { visual.SpatialMaps.swap(spatialMaps); ++visual.SpatialGeneration; }
    if (intensityChanged) {
        visual.Intensities.swap(intensities); ++visual.IntensityGeneration;
    }
    if (tonesChanged) {
        visual.Tones.swap(tones); ++visual.Generation;
    }
    if (tonesChanged || intensityChanged || spatialChanged || periodChanged)
        Blueprint->Damage.push_back(BlueprintDamage(BlueprintDamageType::LIGHTING_CHANGED));
    SpatialSnapshotDirty = false;
    VisualLightingDirty = false;
    VisualLightingPeriod = period;
    VisualScoreVersion = EnvironmentScoreRefreshes;
    for (auto& record : EnvironmentRooms) record.VisualEventByte = -1;
}

int32_t VMContext::GetEnvironmentRoomAt(const LotTilePos& pos) const {
    if (!_Arch || pos.TileX() <= 0 || pos.TileY() <= 0 || pos.TileX() >= _Arch->Width - 1 ||
        pos.TileY() >= _Arch->Height - 1 || pos.Level < 1 || size_t(pos.Level) > EnvironmentMaps.size()) return -5;
    const auto& map = EnvironmentMaps[pos.Level - 1];
    const size_t off = size_t(pos.TileY() * _Arch->Width + pos.TileX());
    if (off >= map.size()) return -5;
    const auto segments = uint8_t(_Arch->Walls[pos.Level - 1][off].Segments);
    return Common::TS1::ResolveEnvironmentRoom(map, _Arch->Width, _Arch->Height,
        pos.TileX(), pos.TileY(), pos.x & 15, pos.y & 15, segments);
}

float VMContext::GetEnvironmentScore(const LotTilePos& pos) {
    RefreshEnvironmentScores();
    const auto room = GetEnvironmentRoomAt(pos);
    if (room <= 0 || size_t(room) >= EnvironmentRooms.size()) return 0;
    float score = EnvironmentRooms[room].Outside ? OutdoorEnvironmentScore : EnvironmentRooms[room].Score;
    if (room == 1 && score < 0) { // original logical room zero only
        const auto house = VM ? VM->GetGlobalValue(10) : 0;
        const auto content = Content::Content::Get();
        const bool residential = house == 0 ||
            (content && content->Neighborhood && content->Neighborhood->GetZoning(house) == 0);
        if (!residential) score = 0;
    }
    return score;
}

void VMContext::AddRoomPortal(VMEntity* obj, uint16_t room) {
    //find other portal part, must be in other room to count...
    for (auto obj2 : obj->MultitileGroup->Objects) {
        auto room2 = GetObjectRoom(obj2);
        if (obj != obj2 && room2 != room && obj2->EntryPoints[15].ActionFunction != 0) {
            RoomInfo[room].Portals.push_back(VMRoomPortal(obj->ObjectID, room2));
            break;
        }
    }
}

void VMContext::RemoveRoomPortal(VMEntity* obj, uint16_t room) {
    auto& portals = RoomInfo[room].Portals;
    for (size_t i = 0; i < portals.size(); i++) {
        if (portals[i].ObjectID == obj->ObjectID) {
            portals.erase(portals.begin() + i);
            break;
        }
    }
}

// OUT_OF_WORLD has an existing special cache bucket. A real coordinate
// outside the architecture must not alias a different tile/floor through the
// linear tile offset, while GUID/category identity indices remain available.
static bool HasObjectTileIndex(const LotTilePos& position, VMArchitecture* architecture) {
    if (position == LotTilePos::OUT_OF_WORLD) return true;
    return position.x >= 0 && position.x < architecture->Width * 16 &&
        position.y >= 0 && position.y < architecture->Height * 16 &&
        position.Level >= 1 && position.Level <= architecture->Stories;
}

static bool MovedObjectAffectsEnvironment(VMContext* context, VMEntity* obj) {
    if (!context->VM || !context->VM->TS1) return true;
    if (obj->GetValue(VMStackObjectVariable::RoomImpact) != 0 ||
        obj->GetValue(VMStackObjectVariable::LightingContribution) != 0 ||
        (uint16_t(obj->GetValue(VMStackObjectVariable::FlagField2)) & 0x400) != 0) return true;
    const auto room = context->GetEnvironmentRoomAt(obj->Position());
    // Zero-contribution objects cannot alter the interior formula when moving.
    // Outdoor good-object counts still depend on their tile/visit multiplicity.
    if (room <= 0 || size_t(room) >= context->EnvironmentRooms.size()) return true;
    const auto pos = obj->Position();
    if (pos.Level < 1 || size_t(pos.Level) > context->EnvironmentTileRooms.size()) return true;
    const auto& tiles = context->EnvironmentTileRooms[pos.Level - 1];
    const auto entries = tiles.find(pos.TileY() * context->Architecture()->Width + pos.TileX());
    if (entries == tiles.end()) return true;
    for (const auto& entry : entries->second)
        if (entry.first < context->EnvironmentRooms.size() &&
            context->EnvironmentRooms[entry.first].Outside) return true;
    return false;
}

void VMContext::RegisterObjectPos(VMEntity* obj) {
    VisualLightingDirty = true;
    auto pos = obj->Position();
    if (pos.Level < 1) return;

    //add object to room

    auto room = GetObjectRoom(obj);
    if (room < RoomInfo.size()) {
        VM::AddToObjList(RoomInfo[room].Entities, obj);
        if (obj->EntryPoints[15].ActionFunction != 0) { //portal
            AddRoomPortal(obj, room);
        }
    }
    obj->SetRoom(room);
    if (VM && VM->TS1) {
        const auto source = SpatialSource(*this, obj);
        auto& previous = SpatialPlacements[obj];
        if (previous.Room != source.ObjectRoom && obj->GetValue(VMStackObjectVariable::RoomImpact) != 0) {
            SpatialSchedule.Mark(previous.Room); SpatialSchedule.Mark(source.ObjectRoom);
        }
        previous = {source.ObjectRoom, true};
        if (!obj->Container && SpatialLights.Interior(source.Position))
            SpatialTileHeads[size_t(source.Position.Story - 1)][size_t(source.Position.Y) * SpatialLights.Width + source.Position.X] = obj->ObjectID;
    }
    InvalidateRoofObject(obj);
    if (obj->GetValue(VMStackObjectVariable::LightingContribution) > 0)
        RefreshLighting(room, true);
    else if (room < RoomInfo.size() && MovedObjectAffectsEnvironment(this, obj))
        EnvironmentObjectsDirty = true;

    if (HasObjectTileIndex(pos, Architecture())) SetToNextCache->RegisterObjectPos(obj);
}

void VMContext::UnregisterObjectPos(VMEntity* obj) {
    VisualLightingDirty = true;
    if (VM && VM->TS1) {
        const auto source = SpatialSource(*this, obj);
        SpatialLights.Unregister(source);
        if (!obj->Container && SpatialLights.Interior(source.Position))
            SpatialTileHeads[size_t(source.Position.Story - 1)][size_t(source.Position.Y) * SpatialLights.Width + source.Position.X] = 0;
        SpatialPlacements[obj].Registered = false;
        if (source.Flags2 & 0x400) RefreshSpatialObject(obj);
        SpatialSnapshotDirty = true;
    }
    InvalidateRoofObject(obj);
    //remove object from room

    auto room = GetObjectRoom(obj);
    if (room < RoomInfo.size()) {
        auto& ents = RoomInfo[room].Entities;
        auto it = std::find(ents.begin(), ents.end(), obj);
        if (it != ents.end()) ents.erase(it);
        if (obj->EntryPoints[15].ActionFunction != 0) { //portal
            RemoveRoomPortal(obj, room);
        }
    }
    if (obj->GetValue(VMStackObjectVariable::LightingContribution) > 0)
        RefreshLighting(room, true);
    else if (room < RoomInfo.size() && MovedObjectAffectsEnvironment(this, obj))
        EnvironmentObjectsDirty = true;

    if (HasObjectTileIndex(obj->Position(), Architecture())) SetToNextCache->UnregisterObjectPos(obj);
}

// TS1(sims): FreeSO VMContext.SlopeVertexCheck / CheckSlopeValid — a terrain vertex may only move when no wall,
// object (that needs level ground) or floor tile (TS1: floors are always level) touches it.
bool VMContext::SlopeVertexCheck(int32_t x, int32_t y) {
    for (int8_t i = 1; i <= 5; i++) {
        auto pos = LotTilePos::FromBigTile((int16_t)x, (int16_t)y, i);
        if (!CheckSlopeValid(pos)) return false;
        pos.x -= 16;
        if (!CheckSlopeValid(pos)) return false;
        pos.y -= 16;
        if (!CheckSlopeValid(pos)) return false;
        pos.x += 16;
        if (!CheckSlopeValid(pos)) return false;
    }
    return true;
}

bool VMContext::CheckSlopeValid(const LotTilePos& pos) {
    if (pos.x < 0 || pos.y < 0) return true;
    if (pos.TileX() >= _Arch->Width || pos.TileY() >= _Arch->Height || pos.Level > _Arch->Stories) return true;
    auto objs = SetToNextCache->GetObjectsAt(pos);
    if (objs != nullptr) {
        for (auto obj : *objs) {
            if (obj->SlopeValid() != VMPlacementError::Success) return false;
        }
    }
    if (_Arch->GetWall(pos.TileX(), pos.TileY(), pos.Level).Segments != 0) return false;
    if (_Arch->GetFloor(pos.TileX(), pos.TileY(), pos.Level).Pattern != 0) return false; // TS1(sims)
    return true;
}

bool VMContext::CheckWallValid(const LotTilePos& pos, const WallTile& wall) {
    // TS1(sims): FreeSO — no walls on sloped terrain
    if ((int)wall.Segments > 0 && pos.Level == 1 && _Arch->GetTerrainSloped(pos.TileX(), pos.TileY())) return false;
    auto objs = SetToNextCache->GetObjectsAt(pos);
    if (objs == nullptr) return true;
    for (auto obj : *objs) {
        if (obj->WallChangeValid(wall, obj->Direction(), false) != VMPlacementError::Success) return false;
    }
    return true;
}

bool VMContext::CheckFloorValid(const LotTilePos& pos, const FloorTile& floor) {
    // TS1(sims): floors and pools need level terrain
    if (floor.Pattern != 0 && pos.Level == 1 && _Arch->GetTerrainSloped(pos.TileX(), pos.TileY())) return false;
    auto objs = SetToNextCache->GetObjectsAt(pos);
    if (objs == nullptr) return true;
    for (auto obj : *objs) {
        if (obj->FloorChangeValid(floor, pos.Level) != VMPlacementError::Success) return false;
    }
    return true;
}

VMSolidResult VMContext::SolidToAvatars(const LotTilePos& pos) {
    if (IsOutOfBounds(pos) || (pos.Level < 1) ||
        (pos.Level != 1 && Architecture()->GetFloor(pos.TileX(), pos.TileY(), pos.Level).Pattern == 0)) { VMSolidResult r; r.Solid = true; return r; }
    auto objs = SetToNextCache->GetObjectsAt(pos);
    if (objs == nullptr) return VMSolidResult();
    for (auto obj : *objs) {
        if (obj == nullptr) continue;
        auto flags = (VMEntityFlags)obj->GetValue(VMStackObjectVariable::Flags);
        if (((flags & VMEntityFlags::DisallowPersonIntersection) > 0) || (flags & (VMEntityFlags::AllowPersonIntersection | VMEntityFlags::HasZeroExtent)) == 0) {
            VMSolidResult r;
            r.Solid = true;
            r.Chair = (obj->EntryPoints[26].ActionFunction != 0) ? obj : nullptr;
            return r; //solid to people
        }
    }
    return VMSolidResult();
}

bool VMContext::IsOutOfBounds(const LotTilePos& pos) {
    return (pos.x < 0 || pos.y < 0 || pos.Level < 1 || pos.TileX() >= _Arch->Width || pos.TileY() >= _Arch->Height || pos.Level > _Arch->Stories);
}

VMPlacementResult VMContext::GetAvatarPlace(VMEntity* target, const LotTilePos& pos, Direction dir) {
    return GetAvatarPlace(target, pos, dir, false);
}

VMPlacementResult VMContext::GetAvatarPlace(VMEntity* target, const LotTilePos& pos, Direction dir, bool allAvatarsSolid) {
    //avatars cannot be placed in slots under any circumstances, so we skip a few steps.

    auto footprint = target->GetObstacle(pos, dir);
    uint16_t room = GetRoomAt(pos);

    VMPlacementError status = VMPlacementError::Success;
    VMEntity* statusObj = nullptr;

    if (footprint == nullptr || pos.Level < 1 || room >= RoomInfo.size()) {
        return VMPlacementResult(status);
    }

    auto objs = RoomInfo[room].Entities; //copy: entry points may change room membership
    for (auto obj : objs) {
        if (obj->MultitileGroup == target->MultitileGroup) continue;
        auto oFoot = obj->Footprint.get();

        std::vector<int16_t> a1 = { obj->ObjectID, 0, 0, 0 };
        std::vector<int16_t> a2 = { target->ObjectID, 0, 0, 0 };
        if (oFoot != nullptr && oFoot->Intersects(*footprint)
            && (!(target->ExecuteEntryPoint(5, this, true, obj, &a1)
                || obj->ExecuteEntryPoint(5, this, true, target, &a2)))) {
            auto flags = (VMEntityFlags)obj->GetValue(VMStackObjectVariable::Flags);
            bool allowAvatars = ((flags & VMEntityFlags::DisallowPersonIntersection) == 0) && ((flags & VMEntityFlags::AllowPersonIntersection) > 0);
            if (allAvatarsSolid && dynamic_cast<VMAvatar*>(obj) != nullptr) allowAvatars = false;
            if (!allowAvatars) {
                status = VMPlacementError::CantIntersectOtherObjects;
                statusObj = obj;
                if (obj->EntryPoints[26].ActionFunction != 0) break; //select chairs immediately.
            }
        }
    }
    return VMPlacementResult(status, statusObj);
}

VMPlacementResult VMContext::GetObjPlace(VMEntity* target, const LotTilePos& pos, Direction dir) {
    //ok, this might be confusing...
    int16_t allowedHeights = target->GetValue(VMStackObjectVariable::AllowedHeightFlags);
    int16_t weight = target->GetValue(VMStackObjectVariable::Weight);
    bool noFloor = (allowedHeights & 1) == 0;

    auto flags = (VMEntityFlags)target->GetValue(VMStackObjectVariable::Flags);
    bool allowAvatars = ((flags & VMEntityFlags::DisallowPersonIntersection) == 0) && ((flags & VMEntityFlags::AllowPersonIntersection) > 0);

    auto footprint = target->GetObstacle(pos, dir);
    uint16_t room = GetRoomAt(pos);

    VMPlacementError status = (noFloor) ? VMPlacementError::HeightNotAllowed : VMPlacementError::Success;
    VMEntity* statusObj = nullptr;

    if (footprint == nullptr || pos.Level < 1 || room >= RoomInfo.size()) {
        return VMPlacementResult(status);
    }

    auto objs = RoomInfo[room].Entities; //copy: entry points may change room membership
    for (auto obj : objs) {
        if (obj->MultitileGroup == target->MultitileGroup || (dynamic_cast<VMAvatar*>(obj) != nullptr && allowAvatars)
            || (target->GhostImage && target->GhostOriginal != nullptr &&
                std::find(target->GhostOriginal->Objects.begin(), target->GhostOriginal->Objects.end(), obj) != target->GhostOriginal->Objects.end())) continue;
        auto oFoot = obj->Footprint.get();

        std::vector<int16_t> a1 = { obj->ObjectID, 0, 0, 0 };
        std::vector<int16_t> a2 = { target->ObjectID, 0, 0, 0 };
        if (oFoot != nullptr && oFoot->Intersects(*footprint)
            && (!(target->ExecuteEntryPoint(5, this, true, obj, &a1)
                || obj->ExecuteEntryPoint(5, this, true, target, &a2)))) {
            statusObj = obj;
            status = VMPlacementError::CantIntersectOtherObjects;

            //this object is technically solid. Check if we can place on top of it
            // A move preview leaves the real object in its support slot. Its
            // own source group occupies that slot legitimately; other objects
            // still block it. This is the same source exclusion as collisions.
            auto occupant = obj->GetSlot(0);
            const bool ownPreviewSlot = occupant && target->GhostImage && target->GhostOriginal &&
                std::find(target->GhostOriginal->Objects.begin(), target->GhostOriginal->Objects.end(), occupant)
                    != target->GhostOriginal->Objects.end();
            if (allowedHeights > 1 && obj->TotalSlots() > 0 &&
                (occupant == nullptr || occupant == target || ownPreviewSlot)) {
                //first check if we have a slot 0, which is what we place onto. then check if it's empty,
                //then check if the object can support this one's weight.
                //we also need to make sure that the height of this specific slot is allowed.

                if (((1 << (obj->GetSlotHeight(0) - 1)) & allowedHeights) > 0) {
                    if (weight < obj->GetValue(VMStackObjectVariable::SupportStrength)) {
                        return VMPlacementResult(VMPlacementError::Success, obj);
                    } else {
                        status = VMPlacementError::CantSupportWeight;
                    }
                } else {
                    if (noFloor) {
                        if ((allowedHeights & (1 << 3)) > 0) status = VMPlacementError::CounterHeight;
                        else status = (obj->GetSlotHeight(0) == 8) ? VMPlacementError::CannotPlaceComputerOnEndTable : VMPlacementError::HeightNotAllowed;
                    }
                }
            }
        }
    }
    return VMPlacementResult(status, statusObj);
}

uint16_t VMContext::GetObjectRoom(VMEntity* obj) {
    if (obj->Position() == LotTilePos::OUT_OF_WORLD) return 0;
    if (obj->Position().Level < 1 || obj->Position().Level > _Arch->Stories) return 0;
    const auto position = obj->Position();
    if (position.x < 0 || position.y < 0 || position.TileX() >= _Arch->Width || position.TileY() >= _Arch->Height) return 0;
    auto& map = Architecture()->Rooms[obj->Position().Level - 1].Map;
    size_t off = (size_t)(obj->Position().TileX() + obj->Position().TileY() * _Arch->Width);
    if (off >= map.size()) return 0;
    return (uint16_t)map[off];
}

uint16_t VMContext::GetRoomAt(const LotTilePos& pos) {
    if (pos.TileX() < 0 || pos.TileX() >= _Arch->Width) return 0;
    else if (pos.TileY() < 0 || pos.TileY() >= _Arch->Height) return 0;
    else if (pos.Level < 1 || pos.Level > _Arch->Stories) return 0;
    else {
        auto& map = Architecture()->Rooms[pos.Level - 1].Map;
        size_t off = (size_t)(pos.TileX() + pos.TileY() * _Arch->Width);
        if (off >= map.size()) return 0;
        return (uint16_t)map[off];
    }
}

int16_t VMContext::GetRoomScore(uint16_t room) {
    if (room >= RoomInfo.size()) return 0;
    const auto base = RoomInfo[room].Room.LightBaseRoom;
    return base < RoomInfo.size() ? RoomInfo[base].Light.RoomScore : 0;
}

Entities::VMMultitileGroup* VMContext::GhostCopyGroup(Entities::VMMultitileGroup* group) {
    auto newGroup = CreateObjectInstance(((group->MultiTile) ? group->BaseObject()->MasterDefinition->GUID : group->BaseObject()->Object->OBJ->GUID), LotTilePos::OUT_OF_WORLD, group->BaseObject()->Direction(), true, false);

    if (newGroup != nullptr) {
        newGroup->Price = group->Price;
        for (size_t i = 0; i < std::min(newGroup->Objects.size(), group->Objects.size()); i++) {
            newGroup->Objects[i]->GhostOriginal = group;
            newGroup->Objects[i]->SetValue(VMStackObjectVariable::Graphic, group->Objects[i]->GetValue(VMStackObjectVariable::Graphic));
            newGroup->Objects[i]->DynamicSpriteFlags = group->Objects[i]->DynamicSpriteFlags;
            newGroup->Objects[i]->DynamicSpriteFlags2 = group->Objects[i]->DynamicSpriteFlags2;
            newGroup->Objects[i]->SetDynamicSpriteFlag(0, group->Objects[i]->IsDynamicSpriteFlagSet(0));
            newGroup->Objects[i]->PlatformState = group->Objects[i]->PlatformState;
            auto go = dynamic_cast<VMGameObject*>(newGroup->Objects[i]);
            if (go != nullptr) go->RefreshGraphic();
        }
    }

    return newGroup;
}

Entities::VMMultitileGroup* VMContext::CreateObjectInstance(uint32_t GUID, const LotTilePos& pos, Direction direction, bool ghostImage, bool ts1) {
    return CreateObjectInstance(GUID, pos, direction, 0, 0, ghostImage, ts1);
}

Entities::VMMultitileGroup* VMContext::CreateObjectInstance(uint32_t GUID, const LotTilePos& pos, Direction direction, bool ts1) {
    return CreateObjectInstance(GUID, pos, direction, 0, 0, false, ts1);
}

Entities::VMMultitileGroup* VMContext::CreateObjectInstance(uint32_t GUID, const LotTilePos& pos, Direction direction, int16_t MainStackOBJ, int16_t MainParam, bool ghostImage, bool ts1) {
    auto objDefinition = FSO::Content::Content::Get()->WorldObjects->Get(GUID, ts1);
    if (objDefinition == nullptr) {
        return nullptr;
    }
    auto group = VM->OwnGroup(new Entities::VMMultitileGroup());

    auto master = objDefinition->OBJ->MasterID;
    if (master != 0 && objDefinition->OBJ->SubIndex == -1) {
        group->MultiTile = true;
        auto objd = objDefinition->Resource->List<OBJD>();

        for (size_t i = 0; i < objd.size(); i++) {
            if (objd[i]->MasterID == master && objd[i]->SubIndex != -1) //if sub-part of this object, make it!
            {
                auto subObjDefinition = FSO::Content::Content::Get()->WorldObjects->Get(objd[i]->GUID, ts1);
                if (subObjDefinition != nullptr) {
                    auto worldObject = new ObjectComponent(subObjDefinition);
                    auto vmObject = (VMGameObject*)VM->OwnEntity(new VMGameObject(subObjDefinition, worldObject));
                    vmObject->GhostImage = ghostImage;
                    if (UseWorld && Blueprint != nullptr) Blueprint->AddObject(worldObject);

                    vmObject->MasterDefinition = objDefinition->OBJ;
                    vmObject->UseTreeTableOf(objDefinition, ts1);

                    vmObject->MainParam = MainParam;
                    vmObject->MainStackOBJ = MainStackOBJ;
                    group->AddObject(vmObject);

                    vmObject->MultitileGroup = group;
                    if (!ghostImage) VM->AddEntity(vmObject);
                }
            }
        }

        group->Init(this);
        VMPlacementError couldPlace = group->ChangePosition(pos, direction, this).Status;
        (void)couldPlace;
        return group;
    } else {
        if (objDefinition->OBJ->ObjectType == OBJDType::Person) //person
        {
            auto vmObject = (VMAvatar*)VM->OwnEntity(new VMAvatar(objDefinition));
            vmObject->MultitileGroup = group;
            group->AddObject(vmObject);

            vmObject->GhostImage = ghostImage;
            if (!ghostImage) VM->AddEntity(vmObject);

            if (UseWorld && Blueprint != nullptr && vmObject->WorldUI) Blueprint->AddAvatar((AvatarComponent*)vmObject->WorldUI.get());

            vmObject->MainParam = MainParam;
            vmObject->MainStackOBJ = MainStackOBJ;

            group->Init(this);
            vmObject->SetPosition(pos, direction, this);

            return group;
        } else {
            auto worldObject = new ObjectComponent(objDefinition);
            auto vmObject = (VMGameObject*)VM->OwnEntity(new VMGameObject(objDefinition, worldObject));

            vmObject->MultitileGroup = group;

            group->AddObject(vmObject);

            vmObject->GhostImage = ghostImage;
            if (!ghostImage) VM->AddEntity(vmObject);
            if (UseWorld && Blueprint != nullptr) Blueprint->AddObject(worldObject);

            vmObject->MainParam = MainParam;
            vmObject->MainStackOBJ = MainStackOBJ;

            group->Init(this);
            vmObject->SetPosition(pos, direction, this);

            return group;
        }
    }
}

void VMContext::RemoveObjectInstance(VMEntity* target) {
    target->PrePositionChange(this);
    if (!target->GhostImage) {
        VM->RemoveEntity(target);
    }
    if (UseWorld && Blueprint != nullptr && target->WorldUI) {
        if (dynamic_cast<VMGameObject*>(target) != nullptr) Blueprint->RemoveObject((ObjectComponent*)target->WorldUI.get());
        else Blueprint->RemoveAvatar((AvatarComponent*)target->WorldUI.get());
    }
    // STUB(3ds): VM.EODHost.ForceDisconnectObj(target) — no EOD host.
}

void VMContext::AddPrimitive(std::unique_ptr<VMPrimitiveRegistration> primitive) {
    auto op = primitive->Opcode;
    if (op > 255) return;
    Primitives[op] = std::move(primitive);
}

}}
