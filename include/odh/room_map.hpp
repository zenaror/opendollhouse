#pragma once

#include "odh/saved_architecture.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace odh {
struct PreparedArchitecture;
struct RoomMapLimits {
    std::uint32_t dimension = 64; // The supported profile never exceeds 64.
    std::size_t rooms = 32767; // Excludes reserved room 0; packed IDs use 15 bits.
};
struct GeometricRoomBounds {
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
};
struct GeometricRoom {
    std::uint16_t id = 0;
    std::uint8_t level = 0; // Zero based, as in VMRoom.Floor.
    std::uint16_t area = 0; // Flood-fill items: one per tile or diagonal half.
    GeometricRoomBounds bounds;
    bool outside = false, pool = false;
    // Discovery order; reciprocal links are added when later rooms are made.
    std::vector<std::uint16_t> adjacent;
    std::uint16_t light_base_room = 0; // Simitone VMRoom.LightBaseRoom after adjacency is complete.
};
struct GeometricRoomMap {
    std::uint32_t width = 0, height = 0;
    // False for the raw saved API. The prepared API sets this when each used
    // FLRm-declared alias has an unambiguous final value; undeclared IDs keep
    // their source byte under Simitone's failed-TryGetValue fallback. This
    // does not prove catalog/name fidelity or a complete runtime room map.
    bool floor_resources_resolved = false;
    // Set only for prepared maps whose floor identity is resolved, with the
    // reference's connected-room lighting base computed.
    bool light_base_rooms_resolved = false;
    // Row major. Bits 0..15: first room, 16..30: second room; bit 31 marks
    // horizontal diagonals. Normal tiles repeat the same ID in both halves.
    // Horizontal: first=top (x+y<=15), second=bottom. Vertical: first=right
    // (x>y), second=left. These are geometric IDs; each room's lighting base
    // is stored separately in GeometricRoom::light_base_room.
    std::array<std::vector<std::uint32_t>, 2> levels;
    std::vector<GeometricRoom> rooms; // Index 0 is the reference's dummy room.
};

// Bounded GenerateMap/SpreadOnto profile over both saved levels. Throws
// std::runtime_error for invalid grids, unsupported segment bits, two diagonal
// orientations on one tile, dimensions over 64, or an exhausted room limit.
// Preserves wraparound and seed order, including the first outside room on
// each level. A local copy applies TS1 DecodeWalls/DecodeAdvWalls normalization:
// non-diagonal present styles outside {2,12,13,14} become 1; absent styles become
// 0. Diagonal styles bypass that normalization. Input is never modified.
// Both-zero diagonal floor fields inherit the raw floor value before applying
// first-story pool/water overlays (nonzero, non-255 bytes; water wins). Present
// overlays require exact extents. Named floor remapping/flags stay unresolved.
// Remaining style != 1 segments separate adjacent rooms (forceAdj).
// No VMContext, objects, doors/portals, lighting, live obstacles, or routing.
GeometricRoomMap generate_saved_room_map(
    const SavedArchitecture&, const RoomMapLimits& limits = {});

// Flood-fill already prepared grids without repeating normalization, diagonal
// fallback, remapping, overlays or crop. Also resolves the reference's lighting
// base from room adjacency; live entities, doors, lighting values and runtime
// placement remain absent.
GeometricRoomMap generate_prepared_room_map(
    const PreparedArchitecture&, const RoomMapLimits& limits = {});

// Resolves the geometric room ID at 1/16-tile world coordinates using the
// packed tile halves and the reference's distinct diagonal selection rules.
// Invalid coordinates, malformed grids, and unassigned/unknown room IDs return
// absence; room ID 0 is a value when explicitly present in the packed map.
std::optional<std::uint16_t> geometric_room_at(
    const GeometricRoomMap&, std::int32_t x, std::int32_t y, std::int16_t level);

// Simitone VMContext.GetRoomAt fallback for a validated two-story room map:
// out-of-lot coordinates and levels outside `stories` resolve to room 0.
// Malformed map metadata and unresolved in-lot cells remain absent. `stories`
// must be in [1, 2], matching this adapter's bounded architecture profile.
std::optional<std::uint16_t> geometric_room_at_runtime(
    const GeometricRoomMap&, std::int32_t x, std::int32_t y,
    std::int16_t level, std::uint8_t stories = 2);

// Simitone RoomByTemp0 read for a prepared room map. Uses clamp(Temp0 + 1),
// then returns the requested room/base-room field; unresolved maps fail closed.
std::optional<std::int16_t> room_by_temp0_data(
    const GeometricRoomMap&, std::int16_t temp0, std::int16_t data_index);
} // namespace odh
