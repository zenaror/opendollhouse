// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMAvatar.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace FSO { namespace SimAntics { namespace Engine { namespace Utils {

// Source room-manager state, separate from reconstructable GPU resources.
// The caller binds the original geometry/room signature before Fields and
// checks each reference against its prepared included-instance graph.
class VMTS1SpatialCheckpoint {
    static void Need(bool condition, const char* text) {
        if (!condition) throw std::runtime_error(text);
    }
    static bool RoomID(int room) { return room == -5 || (room >= 0 && room < 2048); }
    static void Identity(VM& vm, const VMEntity* entity, bool actor) {
        Need(entity && !entity->Dead && !entity->GhostImage && entity->ObjectID > 0 &&
            entity->Object && entity->Object->OBJ && entity->Object->OBJ->GUID &&
            vm.GetObjectById(entity->ObjectID) == entity,
            "spatial checkpoint has an invalid live identity");
        Need(!actor || dynamic_cast<const VMAvatar*>(entity),
            "spatial actor-room key is not an avatar");
    }
    static size_t CellCount(VM& vm, const Common::TS1::SpatialPublisher& lights) {
        Need(lights.Radius >= 0 && lights.Radius <= 126, "invalid spatial source radius");
        if (lights.Width == 0 && lights.Height == 0) return 0;
        const auto* arch = vm.Context->Architecture();
        Need(arch && lights.Width >= 3 && lights.Width <= 127 &&
            lights.Height >= 3 && lights.Height <= 127 &&
            lights.Width == arch->Width && lights.Height == arch->Height,
            "spatial source grid differs from the architecture");
        return size_t(lights.Width) * size_t(lights.Height);
    }
public:
    struct RoomState {
        uint16_t CanonicalID = 0;
        uint8_t Story = 0;
        float LightFactor = 0;
        int32_t People = 0, Lamps = 0;
        bool Pool = false;
    };
    struct State {
        Common::TS1::SpatialPublisher Lights;
        Common::TS1::SpatialRoomSchedule Schedule;
        int32_t Period = -1;
        std::array<uint8_t, 2048> Overhead{}, RoomFlags{};
        std::unordered_map<const VMEntity*, VMContext::VisualActorRoom> ActorRooms;
        std::unordered_map<const VMEntity*, VMContext::SpatialPlacement> Placements;
        std::array<std::vector<int16_t>, 2> TileHeads;
        std::vector<RoomState> Rooms;
    };

    static State Capture(VM& vm) {
        Need(vm.Context != nullptr, "spatial checkpoint has no context");
        const auto& context = *vm.Context;
        const size_t cells = CellCount(vm, context.SpatialLights);
        Need(context.SpatialLights.Maps.size() == (cells ? 2u : 0u),
            "invalid spatial source story count");
        for (const auto& map : context.SpatialLights.Maps)
            Need(map.size() == cells, "invalid spatial source map size");
        for (const auto& heads : context.SpatialTileHeads)
            Need(heads.size() == cells, "invalid spatial tile-head grid size");
        Need(context.VisualActorRooms.size() <= 32767 && context.SpatialPlacements.size() <= 32767 &&
            context.EnvironmentRooms.size() <= 2049, "spatial state exceeds its domain");
        State result;
        result.Lights = context.SpatialLights;
        result.Schedule = context.SpatialSchedule;
        result.Period = context.SpatialPeriod;
        result.Overhead = context.VisualOverhead;
        result.RoomFlags = context.VisualRoomFlags;
        result.ActorRooms = context.VisualActorRooms;
        result.Placements = context.SpatialPlacements;
        result.TileHeads = context.SpatialTileHeads;
        result.Rooms.reserve(context.EnvironmentRooms.size());
        for (const auto& room : context.EnvironmentRooms)
            result.Rooms.push_back({room.CanonicalID, room.Story, room.SpatialLightFactor,
                                   room.SpatialPeople, room.SpatialLamps, room.SpatialPool});
        return result;
    }

    // Archive requires out/in, value(T&) and count(size_t,uint32_t).
    // reference(VMEntity*&,const char*) serializes/resolves ID+GUID and must
    // reject objects outside the caller's included graph. All reads allocate
    // into this State only; none of these operations publish live cache data.
    template<class Archive, class Reference>
    static void Fields(Archive& io, VM& vm, State& state, Reference reference) {
        Need(vm.Context != nullptr, "spatial checkpoint has no context");
        auto& context = *vm.Context;
        io.value(state.Lights.Width); io.value(state.Lights.Height); io.value(state.Lights.Radius);
        const size_t cells = CellCount(vm, state.Lights);
        const auto stories = io.count(state.Lights.Maps.size(), 2);
        Need(stories == (cells ? 2u : 0u), "invalid saved spatial story count");
        if (io.in) state.Lights.Maps.resize(stories);
        for (auto& story : state.Lights.Maps) {
            const auto count = io.count(story.size(), 127u * 127u);
            Need(count == cells, "invalid saved spatial map size");
            if (io.in) story.resize(count);
            for (auto& cell : story) for (auto& slot : cell.Slots) {
                io.value(slot.Dx); io.value(slot.Dy);
                // Empty slots are tagged solely by Dx127. Their Dy byte is
                // uninterpreted historical state and must survive unchanged.
                Need(slot.Dx == 127 || (int(slot.Dx) >= -state.Lights.Radius &&
                    int(slot.Dx) <= state.Lights.Radius && int(slot.Dy) >= -state.Lights.Radius &&
                    int(slot.Dy) <= state.Lights.Radius), "invalid saved spatial source offset");
            }
        }
        for (auto& dirty : state.Schedule.Dirty) {
            io.value(dirty); Need(dirty <= 1, "invalid spatial room dirty flag");
        }
        io.value(state.Schedule.StoryMask);
        Need(state.Schedule.StoryMask <= 3, "invalid spatial dirty story mask");
        io.value(state.Period);
        Need(state.Period >= -1 && state.Period <= 3, "invalid spatial clock period");
        for (auto& overhead : state.Overhead) {
            io.value(overhead); Need(overhead <= 1, "invalid visual overhead flag");
        }
        for (auto& flags : state.RoomFlags) {
            io.value(flags); Need(flags <= 3, "invalid visual room record flags");
        }

        const auto actors = io.count(state.ActorRooms.size(), 32767);
        std::unordered_map<const VMEntity*, VMContext::VisualActorRoom> actorRooms;
        if (io.in) actorRooms.reserve(actors);
        std::vector<std::pair<const VMEntity*, VMContext::VisualActorRoom>> orderedActors;
        if (io.out) {
            orderedActors.reserve(actors);
            for (const auto& entry : state.ActorRooms) {
                Identity(vm, entry.first, true); orderedActors.push_back(entry);
            }
            std::sort(orderedActors.begin(), orderedActors.end(),
                [](const auto& a, const auto& b) { return a.first->ObjectID < b.first->ObjectID; });
        }
        for (uint32_t i = 0; i < actors; ++i) {
            VMEntity* entity = io.out ? const_cast<VMEntity*>(orderedActors[i].first) : nullptr;
            VMContext::VisualActorRoom value = io.out ? orderedActors[i].second : VMContext::VisualActorRoom{};
            reference(entity, "spatial actor-room key"); Identity(vm, entity, true);
            io.value(value.Room); io.value(value.Human);
            Need(RoomID(value.Room), "invalid saved actor canonical room");
            if (io.in) Need(actorRooms.emplace(entity, value).second, "duplicate spatial actor-room key");
        }
        if (io.in) state.ActorRooms.swap(actorRooms);

        const auto placements = io.count(state.Placements.size(), 32767);
        std::unordered_map<const VMEntity*, VMContext::SpatialPlacement> placementMap;
        if (io.in) placementMap.reserve(placements);
        std::vector<std::pair<const VMEntity*, VMContext::SpatialPlacement>> orderedPlacements;
        if (io.out) {
            orderedPlacements.reserve(placements);
            for (const auto& entry : state.Placements) {
                Identity(vm, entry.first, false); orderedPlacements.push_back(entry);
            }
            std::sort(orderedPlacements.begin(), orderedPlacements.end(),
                [](const auto& a, const auto& b) { return a.first->ObjectID < b.first->ObjectID; });
        }
        for (uint32_t i = 0; i < placements; ++i) {
            VMEntity* entity = io.out ? const_cast<VMEntity*>(orderedPlacements[i].first) : nullptr;
            VMContext::SpatialPlacement value = io.out ? orderedPlacements[i].second : VMContext::SpatialPlacement{};
            reference(entity, "spatial placement key"); Identity(vm, entity, false);
            io.value(value.Room); io.value(value.Registered);
            Need(RoomID(value.Room), "invalid saved placement canonical room");
            if (io.in) Need(placementMap.emplace(entity, value).second, "duplicate spatial placement key");
        }
        if (io.in) state.Placements.swap(placementMap);

        for (auto& story : state.TileHeads) {
            const auto count = io.count(story.size(), 127u * 127u);
            Need(count == cells, "invalid saved spatial tile-head size");
            if (io.in) story.resize(count);
            for (auto& id : story) {
                VMEntity* entity = io.out && id ? vm.GetObjectById(id) : nullptr;
                Need(!io.out || id == 0 || entity, "unresolved spatial tile-head identity");
                reference(entity, "spatial tile head");
                if (entity) Identity(vm, entity, false);
                if (io.in) id = entity ? entity->ObjectID : 0;
            }
        }

        const auto rooms = io.count(state.Rooms.size(), 2049);
        Need(rooms == context.EnvironmentRooms.size(), "saved spatial room domain changed");
        if (io.in) state.Rooms.resize(rooms);
        for (size_t i = 0; i < rooms; ++i) {
            auto& room = state.Rooms[i];
            io.value(room.CanonicalID); io.value(room.Story);
            Need(room.CanonicalID == context.EnvironmentRooms[i].CanonicalID &&
                room.Story == context.EnvironmentRooms[i].Story && room.CanonicalID < 2048 && room.Story <= 2,
                "saved spatial room geometry identity changed");
            io.value(room.LightFactor); io.value(room.People); io.value(room.Lamps); io.value(room.Pool);
            Need(std::isfinite(room.LightFactor) && room.LightFactor >= 0 && room.People >= 0 && room.Lamps >= 0,
                "invalid saved spatial room collection");
        }
    }

    // Precondition: Fields and the caller's complete graph/geometry preflight
    // succeeded, with no intervening geometry mutation. No allocation, actor
    // callbacks, source F1 scheduling or room collection occurs at publication.
    static void Publish(VM& vm, State&& state) noexcept {
        auto& context = *vm.Context;
        context.SpatialLights.Width = state.Lights.Width;
        context.SpatialLights.Height = state.Lights.Height;
        context.SpatialLights.Radius = state.Lights.Radius;
        context.SpatialLights.Maps.swap(state.Lights.Maps);
        context.SpatialSchedule = state.Schedule;
        context.SpatialPeriod = state.Period;
        context.VisualOverhead = state.Overhead;
        context.VisualRoomFlags = state.RoomFlags;
        context.VisualActorRooms.swap(state.ActorRooms);
        context.SpatialPlacements.swap(state.Placements);
        context.SpatialTileHeads.swap(state.TileHeads);
        for (size_t i = 0; i < state.Rooms.size(); ++i) {
            auto& room = context.EnvironmentRooms[i];
            const auto& saved = state.Rooms[i];
            room.SpatialLightFactor = saved.LightFactor;
            room.SpatialPeople = saved.People; room.SpatialLamps = saved.Lamps; room.SpatialPool = saved.Pool;
        }
        context.SpatialSnapshotDirty = true;
        context.VisualLightingDirty = true;
    }
};

}}}}
