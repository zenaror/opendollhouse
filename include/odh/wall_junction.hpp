#pragma once
#include "odh/saved_architecture.hpp"
#include "odh/wall_sprite.hpp"
#include "odh/wall_view.hpp"
#include "odh/wall_appearance.hpp"

namespace odh {
struct WallJunction {
    std::uint32_t x,y;
    std::uint8_t level,raw_flags,rotated_flags,frame;
    bool down=false;
};
struct WallJunctionResult {
    std::vector<WallJunction> junctions; // Stable level/y/x order; only mapped flags.
    std::uint32_t unmapped=0; // Nonzero sites excluded by FreeSO JunctionMap.
};
// Pinned FreeSO 4c6b3e8 standing/up 2D profile, not original-game equivalence.
// Two exact square grids <=64 per axis; unknown/mixed diagonal segments fail.
// Cardinal working styles: present fences2/12/13/14 retained, others become1;
// absent sides0. Diagonal TopRightStyle1/255 contributes; other styles do not.
// Paint, appearance/door/window overrides, altitude and raw metadata are ignored.
// Endpoint addressing preserves FreeSO's LINEAR INDEX SPILL: last-column +1
// may reach the next row when still inside the array, including diagonals.
// All grids validated before allocation; no inputs changed. Rotation0..3 acts
// on two independent circular nibbles. At most8192 sites are returned/counted.
// Placement remains caller-owned: source tile(x-.5,y-.5), up Z=level*2.95+alt.
WallJunctionResult standing_wall_junctions(const SavedArchitecture&,std::uint8_t rotation);

struct WallCuts {std::uint8_t top_left=0,top_right=0;}; //0Up or3Down, no partials.
// Alltrue uniform Cutaway: top story working style1 cuts, fences remain up;
// diagonal rawTR1 cuts,255 does not. Seven source LINEAR neighbor addresses
// must be in-range for EVERY nonzero top-story tile in down mode, including
// fences. Invalid source accesses throw instead of inventing exterior values.
std::array<std::vector<WallCuts>,2> wall_view_cuts(const SavedArchitecture&,WallView={});
// Appearance identities, unique faces and door markers are validated. Cut doors
// omit cardinal endpoints; diagonals do not use the cardinal door guard. At each
// site Up flags take precedence whenever nonzero, even if their map is missing;
// otherwise select Down flags. Down placement Z=.3+(level-1)*2.95+alt; buffers
// and sprite frame map remain the same. Higher stories are omitted.
WallJunctionResult wall_junctions(const SavedArchitecture&,const WallAppearance&,
    std::uint8_t rotation,WallView={});

// Map all46 source JunctionMap keys; zero/unsupported flags return nullopt.
std::optional<std::uint8_t> wall_junction_frame(std::uint8_t flags);
std::uint8_t rotate_wall_junction(std::uint8_t flags,std::uint8_t rotation);

// zoom1 far,2 medium,3 near; source config20/19/18 respectively. Start263,
// dx0, iterative binary32 dy=-1.525f*(4/2/1), min255 then explicit ties-even.
// Values are all positive; results independent of the host rounding mode.
// No GPU/original-game depth-calibration claim is implied.
WallDepth wall_junction_depth(std::uint8_t zoom);
} // namespace odh
