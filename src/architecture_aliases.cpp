#include "odh/architecture_aliases.hpp"

#include <stdexcept>
#include <unordered_map>

namespace odh {
namespace {
constexpr std::uint16_t first_dynamic_id = 256;
constexpr std::uint16_t last_dynamic_id = 65533;

struct NameBudget {
    std::size_t used = 0;
    const ArchitectureAliasResolverLimits& limits;
    void add(std::size_t size) {
        if (size > limits.name_length || size > limits.name_bytes - used)
            throw std::runtime_error("architecture alias resolver name budget exceeded");
        used += size;
    }
};

bool lowercase_ascii(std::string& name) {
    for (auto& byte : name) {
        const auto value = static_cast<unsigned char>(byte);
        if (value == 0 || value >= 0x80) return false;
        if (value >= 'A' && value <= 'Z') byte = static_cast<char>(value + ('a' - 'A'));
    }
    return true;
}

std::optional<std::string> normalize_name(const std::string& name, bool map_name,
                                          NameBudget& budget,
                                          std::size_t& unsupported) {
    budget.add(name.size());
    const auto stop = map_name ? name.find('.') : std::string::npos;
    std::string result = name.substr(0, stop);
    if (!lowercase_ascii(result)) {
        ++unsupported;
        return std::nullopt;
    }
    return result;
}

using Catalog = std::unordered_map<std::string, std::uint16_t>;

Catalog make_catalog(const std::vector<ArchitectureCatalogResource>& entries,
                     const ArchitectureAliasResolverLimits& limits,
                     NameBudget& budget, std::size_t& duplicates,
                     std::size_t& unsupported) {
    if (entries.size() > limits.catalog_entries)
        throw std::runtime_error("architecture alias resolver catalog entry limit exceeded");
    Catalog result;
    result.reserve(entries.size());
    for (const auto& entry : entries) {
        auto name = normalize_name(entry.name, false, budget, unsupported);
        if (!name) continue;
        const auto [found, inserted] = result.emplace(*name, entry.id);
        if (!inserted) {
            ++duplicates;
            found->second = entry.id; // Explicit vector order: later provider wins.
        }
    }

    std::unordered_map<std::uint16_t, std::string> id_owners;
    id_owners.reserve(result.size());
    for (const auto& [name, id] : result) {
        if (id < first_dynamic_id || id > last_dynamic_id)
            throw std::runtime_error("architecture alias resolver resource ID outside ODH range");
        const auto [found, inserted] = id_owners.emplace(id, name);
        if (!inserted && found->second != name)
            throw std::runtime_error("architecture alias resolver resource ID collision");
    }
    return result;
}

void resolve_map(const ArchitectureMap& map, const Catalog& catalog,
                 NameBudget& budget, std::size_t entry_limit,
                 std::array<std::optional<std::uint16_t>, 256>& aliases,
                 std::size_t& resolved, std::size_t& misses,
                 std::size_t& overwrites, std::array<bool, 256>& missing,
                 std::size_t& unsupported) {
    if (map.entries.size() > entry_limit)
        throw std::runtime_error("architecture alias resolver map entry limit exceeded");
    for (const auto& entry : map.entries) {
        auto name = normalize_name(entry.name, true, budget, unsupported);
        if (!name) {
            ++misses;
            missing[entry.id] = true;
            continue;
        }
        const auto found = catalog.find(*name);
        if (found == catalog.end()) {
            ++misses;
            missing[entry.id] = true;
            continue;
        }
        if (aliases[entry.id]) ++overwrites;
        aliases[entry.id] = found->second;
        ++resolved;
    }
}
} // namespace

ResolvedArchitectureAliases resolve_architecture_aliases(
    const ArchitectureMap& floor_map, const ArchitectureMap& wall_map,
    const std::vector<ArchitectureCatalogResource>& floor_catalog,
    const std::vector<ArchitectureCatalogResource>& wall_catalog,
    const ArchitectureAliasResolverLimits& limits) {
    ResolvedArchitectureAliases result;
    auto& diagnostics = result.diagnostics;
    NameBudget budget{0, limits};
    const auto floors = make_catalog(floor_catalog, limits, budget,
        diagnostics.floor_catalog_duplicates, diagnostics.unsupported_floor_catalog_names);
    const auto walls = make_catalog(wall_catalog, limits, budget,
        diagnostics.wall_catalog_duplicates, diagnostics.unsupported_wall_catalog_names);
    diagnostics.floor_map_entries = floor_map.entries.size();
    diagnostics.wall_map_entries = wall_map.entries.size();
    resolve_map(floor_map, floors, budget, limits.map_entries, result.aliases.floors,
        diagnostics.floor_map_resolved, diagnostics.floor_map_misses,
        diagnostics.floor_map_overwrites, diagnostics.unresolved_floor_aliases,
        diagnostics.unsupported_floor_map_names);
    resolve_map(wall_map, walls, budget, limits.map_entries, result.aliases.walls,
        diagnostics.wall_map_resolved, diagnostics.wall_map_misses,
        diagnostics.wall_map_overwrites, diagnostics.unresolved_wall_aliases,
        diagnostics.unsupported_wall_map_names);
    return result;
}
} // namespace odh
