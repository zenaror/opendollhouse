/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.VMContext
#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <memory>
#include "core/math.h"
#include "content/ContentManager.h"
#include "world/World.h"
#include "world/model/Blueprint.h"
#include "world/model/LotTilePos.h"
#include "world/model/Direction.h"
#include "simantics/VMPlacementResult.h"
#include "simantics/VMClock.h"
#include "simantics/VMArchitecture.h"
#include "simantics/model/VMRoomInfo.h"
#include "simantics/model/VMObjectQueries.h"
#include "simantics/model/VMSetToNextCache.h"
#include "simantics/engine/VMPrimitiveRegistration.h"
#include "common/TS1/TS1SpatialLighting.h"

namespace FSO { namespace SimAntics {
class VM;
class VMEntity;
namespace Engine { class VMAmbientSound; }
namespace Entities { class VMMultitileGroup; }

class VMContext {
public:
    static bool UseWorld;
    FSO::LotView::Model::Blueprint* Blueprint = nullptr;         // owned by OwnedBlueprint when the activator created it
    std::unique_ptr<FSO::LotView::Model::Blueprint> OwnedBlueprint;
    std::unique_ptr<Model::VMObjectQueries> ObjectQueries;
    std::unique_ptr<FSO::SimAntics::VMClock> Clock;

    FSO::SimAntics::VMArchitecture* Architecture() const { return _Arch.get(); }
    /// Takes ownership; hooks WallsChanged like the C# property setter.
    void Architecture(std::unique_ptr<FSO::SimAntics::VMArchitecture> value);
    bool Ready() const { return (_Arch != nullptr); }

    FSO::LotView::World* World = nullptr;
    /// Indexed by opcode (0-255). Register with AddPrimitive.
    std::array<std::unique_ptr<Engine::VMPrimitiveRegistration>, 256> Primitives;
    std::shared_ptr<Engine::VMAmbientSound> Ambience;
    uint64_t RandomSeed = 0;
    int EnvironmentPeriod = -1;
    float OutdoorEnvironmentScore = 0;
    static uint64_t DebugSeed; // FIX(sims) QA: when non-zero, new VMs get repeatable seeds (host tools only)

    FSO::Content::GameGlobal* Globals = nullptr;
    std::unique_ptr<Model::VMSetToNextCache> SetToNextCache;
    std::vector<Model::VMRoomInfo> RoomInfo;
    std::vector<Model::VMEnvironmentRoom> EnvironmentRooms;
    std::vector<std::vector<uint32_t>> EnvironmentMaps;
    std::vector<std::unordered_map<int32_t, std::vector<std::pair<uint16_t, uint32_t>>>> EnvironmentTileRooms;
    bool EnvironmentObjectsDirty = true;
    uint64_t EnvironmentScoreRefreshes = 0, EnvironmentObjectPasses = 0;
    bool VisualLightingDirty = true;
    int VisualLightingPeriod = -1;
    uint64_t VisualLightingRefreshes = 0;
    uint64_t VisualGeometryVersion = 1;
    // Renderer-owned original roof inputs. Relevant architecture/object edits
    // coalesce here; camera, time and unrelated actor ticks do not rebuild it.
    bool RoofAdmissionDirty = true;
    uint64_t RoofAdmissionPublishes = 0;
    uint64_t RoofAdmissionGeometryVersion = 0, RoofAdmissionHeightVersion = 0;
    std::vector<uint8_t> RoofGroundStairs;
    uint64_t VisualScoreVersion = 0;
    std::array<uint8_t, 2048> VisualOverhead{}; // original records survive ID reuse
    std::array<uint8_t, 2048> VisualRoomFlags{}; // last record outside/pool flags, including inactive IDs
    struct VisualActorRoom { int Room = -5; bool Human = false; };
    std::unordered_map<const VMEntity*, VisualActorRoom> VisualActorRooms;
    FSO::Common::TS1::SpatialPublisher SpatialLights;
    FSO::Common::TS1::SpatialRoomSchedule SpatialSchedule;
    std::vector<int16_t> SpatialCanonicalIds;
    std::array<std::vector<int16_t>, 2> SpatialTileHeads;
    struct SpatialPlacement { int Room = -5; bool Registered = false; };
    std::unordered_map<const VMEntity*, SpatialPlacement> SpatialPlacements;
    int SpatialPeriod = -1;
    bool SpatialSnapshotDirty = true;
    uint64_t SpatialCollectorPasses = 0, SpatialObjectVisits = 0;

    FSO::SimAntics::VM* VM = nullptr;

    explicit VMContext(FSO::LotView::World* world);
    VMContext(FSO::LotView::World* world, VMContext* oldContext);
    /// Private UI contexts need no mutation of the shared QA seed sequence.
    VMContext(FSO::LotView::World* world, VMContext* oldContext, bool consumeDebugSeed);
    ~VMContext();

    /// Returns a random number between 0 and less than the specified maximum.
    uint64_t NextRandom(uint64_t max);

    FSO::LotView::Components::ObjectComponent* MakeObjectComponent(FSO::Content::GameObject* obj);
    bool IsUserOutOfBounds(const FSO::LotView::Model::LotTilePos& pos);
    void UpdateTSOBuildableArea();
    st::Rectangle GetTSOBuildableArea(int32_t lotSInfo);
    void RegeneratePortalInfo();
    void RefreshLighting(uint16_t room, bool commit);
    void RefreshRoomScore(uint16_t room);
    void RebuildEnvironmentGeometry(); // original logical map, independent of routing/graphics
    void RefreshEnvironmentScores();
    void PrepareVisualLighting(); // coalesced render preparation; never advances the VM
    Common::TS1::VisualLighting BuildPictureVisualLighting(int period);
    void PrepareRoofGeometry();
    void InvalidateRoofObject(VMEntity* entity);
    void ObserveSpatialPeriod(); // clock event boundary; never advances the clock
    void FlushSpatialLighting();
    void RefreshSpatialObject(VMEntity* entity); // original F2/refresh1 -> F1
    void RefreshSpatialRoom(VMEntity* entity); // original refresh2
    void RemoveSpatialEntity(VMEntity* entity);
    void RemoveVisualActor(VMEntity* entity);
    void ObserveVisualActor(VMEntity* entity, bool removing = false); // actor tick cadence, not draw cadence
    int32_t GetEnvironmentRoomAt(const FSO::LotView::Model::LotTilePos& pos) const;
    float GetEnvironmentScore(const FSO::LotView::Model::LotTilePos& pos);
    void AddRoomPortal(VMEntity* obj, uint16_t room);
    // Derive exactly the edge AddRoomPortal would publish, without mutations.
    bool FindRoomPortal(VMEntity* obj, uint16_t room, Model::VMRoomPortal& edge);
    void RemoveRoomPortal(VMEntity* obj, uint16_t room);
    void RegisterObjectPos(VMEntity* obj);
    void UnregisterObjectPos(VMEntity* obj);
    bool SlopeVertexCheck(int32_t x, int32_t y); // TS1(sims): FreeSO
    bool CheckSlopeValid(const FSO::LotView::Model::LotTilePos& pos);
    bool CheckWallValid(const FSO::LotView::Model::LotTilePos& pos, const FSO::LotView::Model::WallTile& wall);
    bool CheckFloorValid(const FSO::LotView::Model::LotTilePos& pos, const FSO::LotView::Model::FloorTile& floor);
    VMSolidResult SolidToAvatars(const FSO::LotView::Model::LotTilePos& pos);
    bool IsOutOfBounds(const FSO::LotView::Model::LotTilePos& pos);
    VMPlacementResult GetAvatarPlace(VMEntity* target, const FSO::LotView::Model::LotTilePos& pos, FSO::LotView::Model::Direction dir);
    VMPlacementResult GetAvatarPlace(VMEntity* target, const FSO::LotView::Model::LotTilePos& pos, FSO::LotView::Model::Direction dir, bool allAvatarsSolid);
    VMPlacementResult GetObjPlace(VMEntity* target, const FSO::LotView::Model::LotTilePos& pos, FSO::LotView::Model::Direction dir);
    uint16_t GetObjectRoom(VMEntity* obj);
    uint16_t GetRoomAt(const FSO::LotView::Model::LotTilePos& pos);
    int16_t GetRoomScore(uint16_t room);
    Entities::VMMultitileGroup* GhostCopyGroup(Entities::VMMultitileGroup* group);
    Entities::VMMultitileGroup* CreateObjectInstance(uint32_t GUID, const FSO::LotView::Model::LotTilePos& pos, FSO::LotView::Model::Direction direction, bool ghostImage, bool ts1);
    Entities::VMMultitileGroup* CreateObjectInstance(uint32_t GUID, const FSO::LotView::Model::LotTilePos& pos, FSO::LotView::Model::Direction direction, bool ts1);
    Entities::VMMultitileGroup* CreateObjectInstance(uint32_t GUID, const FSO::LotView::Model::LotTilePos& pos, FSO::LotView::Model::Direction direction, int16_t MainStackOBJ, int16_t MainParam, bool ghostImage, bool ts1);
    void RemoveObjectInstance(VMEntity* target);

    void AddPrimitive(std::unique_ptr<Engine::VMPrimitiveRegistration> primitive);
    void AddPrimitive(Engine::VMPrimitiveRegistration* primitive) { AddPrimitive(std::unique_ptr<Engine::VMPrimitiveRegistration>(primitive)); }

private:
    std::unique_ptr<FSO::SimAntics::VMArchitecture> _Arch;
    void WallsChanged(FSO::SimAntics::VMArchitecture* caller);
};

/// Registers the SimAntics primitive table (opcodes 0-67, see README.md) into a context.
/// Defined by primitives/VMPrimitiveTable.cpp; required by the complete engine.
/// There is no empty fallback: missing registration must fail at link time.
void RegisterDefaultPrimitives(VMContext* context);
void RegisterTS1ExpansionPrimitives(VMContext* context); // TS1(sims): primitives/VMTS1Primitives.cpp
}}
