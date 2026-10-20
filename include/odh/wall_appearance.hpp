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
struct WallAppearanceApplication {
    std::int16_t id;
    std::uint8_t applied_sides=0,ignored_sides=0; //bit(side); source sides1/2 ignored.
};
struct WallAppearance {
    std::vector<WallAppearanceFace> faces; //Stable level/y/x/segment order.
    std::vector<WallAppearanceApplication> applications; //Eligible input order.
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
