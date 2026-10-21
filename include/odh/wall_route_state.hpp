#pragma once

#include "odh/route_geometry.hpp"
#include "odh/wall_appearance.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace odh {

enum class WallEdgeBuildStatus : std::uint8_t { ready, unsupported };
struct WallEdgeBuildResult {
    WallEdgeBuildStatus status = WallEdgeBuildStatus::unsupported;
    std::vector<ResolvedWallEdges> edges; // Row-major for the selected level.
    std::string diagnostic;
};

// Builds conservative initial-restore edge knowledge for saved_wall_raycast.
// Present segments default to solid; only a matching door face in the supplied
// appearance snapshot opens them. The architecture must be the same snapshot
// used to derive WallAppearance. This is not live portal or placement state.
WallEdgeBuildResult resolve_initial_wall_edges(
    const SavedArchitecture&, const WallAppearance&, std::int16_t level,
    std::size_t face_limit = 16384);

} // namespace odh
