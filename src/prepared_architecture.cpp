#include "odh/prepared_architecture.hpp"

#include <stdexcept>

namespace odh {
namespace {
constexpr std::uint32_t source_size = 64;
constexpr std::size_t source_cells = std::size_t(source_size) * source_size;
constexpr std::uint8_t any_diagonal = wall_horizontal_diagonal | wall_vertical_diagonal;

void validate_aliases(const ArchitectureResourceAliases& aliases) {
    for (const auto* dictionary : {&aliases.floors, &aliases.walls})
        for (const auto value : *dictionary)
            if (value && (*value < 256 || *value > 65533))
                throw std::runtime_error("prepared architecture resource ID outside profile");
}

std::uint16_t remap(std::uint16_t value,
                    const std::array<std::optional<std::uint16_t>, 256>& aliases,
                    std::size_t& lookups, std::size_t& misses,
                    std::array<bool, 256>& missing) {
    const auto key = static_cast<std::uint8_t>(value);
    ++lookups;
    if (aliases[key]) return *aliases[key];
    ++misses;
    missing[key] = true;
    return value;
}

std::uint16_t remap_floor(std::uint16_t original, std::uint8_t flags, bool flip,
                          const ArchitectureResourceAliases& aliases,
                          ArchitecturePreparationDiagnostics& diagnostics,
                          bool& declared_unresolved_miss) {
    declared_unresolved_miss = false;
    auto result = original;
    if (flip) {
        if (original == 10) result = 11;
        else if (original == 11) result = 10;
    }
    // FloorTile is a struct in the source: its snapshot precedes road swapping.
    if ((original != 0 && (flags & 32U) == 0) || original > 30) {
        const auto key = static_cast<std::uint8_t>(original);
        ++diagnostics.floor_lookups;
        if (aliases.floors[key]) result = *aliases.floors[key];
        else {
            ++diagnostics.floor_alias_misses;
            diagnostics.missing_floor_aliases[key] = true;
            declared_unresolved_miss = aliases.declared_unresolved_floors[key];
        }
    }
    return result;
}

SavedWallTile remap_wall(SavedWallTile wall, std::uint16_t remapped_floor,
                         const ArchitectureResourceAliases& aliases,
                         ArchitecturePreparationDiagnostics& diagnostics) {
    const auto floor = [&](std::uint16_t value) {
        const auto mapped = remap(value, aliases.floors, diagnostics.floor_lookups,
                     diagnostics.floor_alias_misses, diagnostics.missing_floor_aliases);
        const auto key = static_cast<std::uint8_t>(value);
        if (!aliases.floors[key] && aliases.declared_unresolved_floors[key])
            ++diagnostics.unresolved_floor_alias_references;
        return mapped;
    };
    const auto pattern = [&](std::uint16_t value) {
        return remap(value, aliases.walls, diagnostics.wall_lookups,
                     diagnostics.wall_alias_misses, diagnostics.missing_wall_aliases);
    };
    if ((wall.segments & any_diagonal) == 0) {
        const auto style = [](std::uint16_t value, bool present) -> std::uint16_t {
            if (!present) return 0;
            return value == 2 || value == 12 || value == 13 || value == 14 ? value : 1;
        };
        wall.top_left_style = style(wall.top_left_style, wall.segments & wall_top_left);
        wall.top_right_style = style(wall.top_right_style, wall.segments & wall_top_right);
    }
    if (wall.bottom_left_pattern) wall.bottom_left_pattern = pattern(wall.bottom_left_pattern);
    if (wall.bottom_right_pattern) wall.bottom_right_pattern = pattern(wall.bottom_right_pattern);
    if (wall.segments & any_diagonal) {
        if (wall.top_left_pattern || wall.top_left_style) {
            if (wall.top_left_pattern) wall.top_left_pattern = floor(wall.top_left_pattern);
            if (wall.top_left_style) wall.top_left_style = floor(wall.top_left_style);
        } else {
            wall.top_left_pattern = wall.top_left_style = remapped_floor;
        }
    } else {
        if (wall.top_left_pattern) wall.top_left_pattern = pattern(wall.top_left_pattern);
        if (wall.top_right_pattern) wall.top_right_pattern = pattern(wall.top_right_pattern);
    }
    return wall;
}
} // namespace

PreparedArchitecture prepare_saved_architecture(
    const SavedArchitecture& saved, const ArchitectureResourceAliases& aliases,
    const ArchitecturePreparationOptions& options,
    const ArchitecturePreparationLimits& limits) {
    if (saved.width != source_size || saved.height != source_size ||
        !options.target_size || options.target_size > source_size)
        throw std::runtime_error("prepared architecture dimensions outside 64x64 crop profile");
    if (!saved.levels[0].flags || saved.levels[0].flags->size() != source_cells)
        throw std::runtime_error("prepared architecture requires exact ground flags");
    if (!saved.pools || !saved.water || saved.pools->size() != source_cells ||
        saved.water->size() != source_cells)
        throw std::runtime_error("prepared architecture requires exact pool/water grids");
    for (const auto& level : saved.levels) {
        if (level.floors.size() != source_cells || level.walls.size() != source_cells)
            throw std::runtime_error("prepared architecture floor/wall extent mismatch");
        for (const auto& wall : level.walls)
            if ((wall.segments & 0xc0U) || (wall.segments & any_diagonal) == any_diagonal)
                throw std::runtime_error("prepared architecture unsupported wall segments");
    }
    validate_aliases(aliases);
    const auto cells = std::size_t(options.target_size) * options.target_size;
    const auto retained = sizeof(PreparedArchitecture) + cells *
        (2 * (sizeof(std::uint16_t) + sizeof(SavedWallTile)) + sizeof(std::uint8_t));
    if (retained > limits.retained_bytes)
        throw std::runtime_error("prepared architecture retained byte limit exceeded");

    PreparedArchitecture result;
    result.width = result.height = options.target_size;
    result.buildable.resize(cells);
    for (auto& level : result.levels) {
        level.floors.resize(cells);
        level.walls.resize(cells);
    }
    for (std::size_t level = 0; level < saved.levels.size(); ++level) {
        for (std::uint32_t y = 0; y < source_size; ++y) {
            for (std::uint32_t x = 0; x < source_size; ++x) {
                const auto index = std::size_t(y) * source_size + x;
                const auto flags = (*saved.levels[0].flags)[index];
                bool floor_declared_unresolved = false;
                auto floor = remap_floor(saved.levels[level].floors[index], flags,
                                         options.flip_road, aliases, result.diagnostics,
                                         floor_declared_unresolved);
                const auto& saved_wall = saved.levels[level].walls[index];
                const bool diagonal = (saved_wall.segments & any_diagonal) != 0;
                const bool shared_diagonal_floor = diagonal &&
                    !saved_wall.top_left_pattern && !saved_wall.top_left_style;
                // Per-cell evaluation preserves the dependencies of the source
                // passes: walls see remapped floors before overlays or crop.
                const auto wall = remap_wall(saved.levels[level].walls[index], floor,
                                             aliases, result.diagnostics);
                bool floor_identity_used = !diagonal || shared_diagonal_floor;
                if (level == 0) {
                    const auto pool = (*saved.pools)[index], water = (*saved.water)[index];
                    const bool overlay = (pool != 0 && pool != 255) ||
                                         (water != 0 && water != 255);
                    floor = ts1_surface_pattern(floor, pool, water);
                    if (!diagonal && overlay) floor_identity_used = false;
                }
                if (floor_declared_unresolved && floor_identity_used)
                    ++result.diagnostics.unresolved_floor_alias_references;
                if (x < options.target_size && y < options.target_size) {
                    const auto destination = std::size_t(y) * options.target_size + x;
                    result.levels[level].floors[destination] = floor;
                    result.levels[level].walls[destination] = wall;
                    if (level == 0) result.buildable[destination] = (flags & 32U) == 0 ? 1 : 0;
                }
            }
        }
    }
    return result;
}
} // namespace odh
