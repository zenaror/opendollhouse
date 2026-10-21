#include "odh/wall_route_state.hpp"

#include <array>
#include <optional>
#include <set>
#include <tuple>

namespace odh {
namespace {
constexpr std::uint8_t segment_top_left = 1;
constexpr std::uint8_t segment_top_right = 2;
constexpr std::uint32_t maximum_dimension = 64;
constexpr std::size_t maximum_faces = 16384;

WallEdgeBuildResult unsupported(const char* message) {
    return {WallEdgeBuildStatus::unsupported, {}, message};
}
}

WallEdgeBuildResult resolve_initial_wall_edges(
    const SavedArchitecture& architecture, const WallAppearance& appearance,
    std::int16_t level, std::size_t face_limit) {
    if (!architecture.width || !architecture.height ||
        architecture.width > maximum_dimension || architecture.height > maximum_dimension ||
        level < 1 || level > 2)
        return unsupported("wall edge architecture extent or level unsupported");
    const auto cells = std::size_t(architecture.width) * architecture.height;
    for (const auto& story : architecture.levels)
        if (story.floors.size() != cells || story.walls.size() != cells)
            return unsupported("wall edge architecture grid extent mismatch");
    if (face_limit > maximum_faces) face_limit = maximum_faces;
    if (appearance.faces.size() > face_limit)
        return unsupported("wall appearance face budget exceeded");

    using FaceKey = std::tuple<std::uint8_t, std::uint32_t, std::uint32_t, std::uint8_t>;
    std::set<FaceKey> seen;
    std::vector<std::array<std::optional<bool>, 2>> doors(cells);
    for (const auto& face : appearance.faces) {
        if (face.x >= architecture.width || face.y >= architecture.height ||
            face.level < 1 || face.level > 2 || (face.segment != segment_top_left && face.segment != segment_top_right) ||
            face.owner <= 0 || !face.generation || face.door_owner < 0 ||
            ((face.door_owner == 0) != (face.door_generation == 0)) ||
            (face.door && face.door_owner == 0))
            return unsupported("wall appearance face identity or extent invalid");
        const FaceKey key{face.level, face.y, face.x, face.segment};
        if (!seen.insert(key).second)
            return unsupported("duplicate wall appearance face");
        const auto index = std::size_t(face.y) * architecture.width + face.x;
        const auto& tile = architecture.levels[face.level - 1].walls[index];
        const bool segment_present = (tile.segments & face.segment) != 0;
        if (segment_present != face.segment_present)
            return unsupported("wall appearance face does not match architecture snapshot");
        if (face.level == level)
            doors[index][face.segment == segment_top_left ? 0 : 1] = face.door;
    }

    WallEdgeBuildResult result;
    result.status = WallEdgeBuildStatus::ready;
    result.edges.resize(cells);
    const auto& walls = architecture.levels[level - 1].walls;
    for (std::size_t index = 0; index < cells; ++index) {
        const bool top_left_present = (walls[index].segments & segment_top_left) != 0;
        const bool top_right_present = (walls[index].segments & segment_top_right) != 0;
        result.edges[index].top_left = !top_left_present || (doors[index][0] && *doors[index][0])
            ? WallEdgeKnowledge::non_solid : WallEdgeKnowledge::solid;
        result.edges[index].top_right = !top_right_present || (doors[index][1] && *doors[index][1])
            ? WallEdgeKnowledge::non_solid : WallEdgeKnowledge::solid;
    }
    result.diagnostic = "initial restore snapshot; unknown door faces remain solid";
    return result;
}
} // namespace odh
