#pragma once
#include "odh/world.hpp"
#include "odh/saved_architecture.hpp"
namespace odh {
struct ObjdWallAppearance {
    std::uint32_t version,guid;
    std::uint16_t wall_style;
    std::optional<std::uint16_t> sprite_base;
    std::uint64_t header_extent,remaining_bytes;
};
// Partial profiles136/138: style at92; sprite-base at162 exists in138 only.
// No inference from oversized136 tails; no full OBJD validation or mask loading.
ObjdWallAppearance read_objd_wall_appearance(const View&);
struct WallAppearanceInput {
    std::int16_t id;
    std::uint64_t generation;
    std::uint32_t guid;
    EntityKind kind;
    std::optional<EntityPosition> position;
    bool contained;
    std::uint16_t flags2,placement;
    std::optional<ObjdWallAppearance> binding;
};
struct WallAppearanceStyle {
    std::uint16_t raw_style;
    std::uint32_t resource_guid; //0 for global style; concrete GUID for object SPR#.
    std::array<std::uint16_t,6> sprites{}; //Up3 zooms, Down3; wraps uint16 like reference.
};
struct WallAppearanceFace {
    std::uint32_t x,y;
    std::uint8_t level,segment; //segment1 TL,2 TR; logical coordinates, not camera.
    WallAppearanceStyle style;
    std::int16_t owner,door_owner;
    std::uint64_t generation,door_generation;
    bool door,segment_present;
};
// An authenticated Pre style0 write has no remaining object resource owner.
// Keep the explicit clear after deletion instead of exposing the older raw wall
// style again. Door state, when retained by a window clear, has its own owner.
bool is_canonical_cleared_wall_face(const WallAppearanceFace&) noexcept;
struct WallAppearanceApplication {
    std::int16_t id;
    std::uint8_t applied_sides=0,ignored_sides=0; //bit(side); source sides1/2 ignored.
};
struct WallAppearance {
    std::vector<WallAppearanceFace> faces; //Stable level/y/x/segment order.
    // Diagnostics only: pure derivation uses eligible input order. Successful
    // live writes compact legacy history to the latest record per existing
    // ObjectID, in last-application order. These IDs have no generation and
    // never authorize faces, occupancy or replay. The wire cap remains 4096.
    std::vector<WallAppearanceApplication> applications;
    std::uint32_t overwritten_faces=0;
};
// Pure FreeSO reference profile: free positioned architectural objects, flags2
// bits14/15; placement bits0..3; cardinal direction or north fallback. No hidden
// filter. Caller order is explicit; last style write wins. Window writes retain
// prior Door state; door writes set Door iff style !=0. Style0 is a recorded clear.
// Raw architecture and input banks untouched. Does not execute Init, create room
// maps, normalize base styles, allocate dynamic IDs or load/replace missing masks.
WallAppearance derive_wall_appearance(const SavedArchitecture&,
    const std::vector<WallAppearanceInput>&,std::size_t input_limit=4096);
// Simitone's saved-lot restore inserts entities in ascending ObjectID order,
// then replays PositionChange in that entity order. This profile sorts a copy
// before applying the same pure wall-style writes; it does not model later
// runtime movement or style changes.
WallAppearance derive_wall_appearance_restore_order(const SavedArchitecture&,
    const std::vector<WallAppearanceInput>&,std::size_t input_limit=4096);
}
