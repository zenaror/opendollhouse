#include "odh/session_entry.hpp"

#include "odh/character_catalog.hpp"
#include "odh/lot.hpp"
#include "odh/sha256.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

namespace odh {
namespace {
std::array<std::uint8_t, 32> identity_of(const View& view) {
    Sha256 hash;
    std::array<std::uint8_t, 64 * 1024> block{};
    for (std::uint64_t offset = 0; offset < view.size();) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            block.size(), view.size() - offset));
        view.read(offset, block.data(), count);
        hash.update(block.data(), count);
        offset += count;
    }
    return hash.finish();
}

template<class F>
void with_iff_file(const std::filesystem::path& path, F&& read) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || !std::filesystem::is_regular_file(path, error) || error)
        throw FormatError("required session IFF is not a readable regular file");
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw FormatError("cannot open required session IFF");
    read(View(stream, size));
}
}

NeighborhoodFamilySource load_neighborhood_family_source(
        const std::filesystem::path& neighborhood_iff) {
    NeighborhoodFamilySource result;
    std::uint32_t remaining_members = 65536;
    with_iff_file(neighborhood_iff, [&](const auto& view) {
        result.identity = identity_of(view);
        iff_chunks(view, [&](const Chunk& chunk) {
            if (chunk.type != "FAMI") return;
            if (result.families.size() >= 4096) throw FormatError("too many family records");
            FamiLimits limits;
            limits.members = std::min<std::uint32_t>(256, remaining_members);
            auto family = read_fami(chunk.payload, limits);
            remaining_members -= static_cast<std::uint32_t>(family.member_guids.size());
            result.families.push_back({chunk.id, std::move(family)});
        });
    });
    return result;
}

SessionEntry load_session_entry(const std::filesystem::path& user_data,
        unsigned mode, std::uint16_t house, std::uint8_t language,
        const FamilyOverlay* saved_overlay,
        const std::filesystem::path& player_character_directory) {
    if (!neighborhood_mode_supported(mode))
        throw FormatError("selected neighborhood mode is unsupported");
    if (house == 0 || house >= 99) throw FormatError("selected house is outside the selectable range");
    if (language < 1 || language > 20) throw FormatError("selected language is outside 1..20");

    std::optional<LotLocations> locations;
    with_iff_file(user_data / "LotLocations.iff", [&](const auto& view) {
        iff_chunks(view, [&](const Chunk& chunk) {
            if (chunk.type != "STR#" || chunk.id != mode) return;
            if (locations) throw FormatError("ambiguous LotLocations mode table");
            // LotLocations rows are map coordinates, not interface text. In
            // the installed TS1 tables, non-English groups contain an opaque
            // first entry where the first lot coordinate should be, so they
            // cannot be used to resolve selectable houses. Keep the selected
            // UI language for runtime text while reading the canonical map
            // coordinates from the English group.
            locations = read_lot_locations(chunk.payload, 1);
        });
    });
    if (!locations) throw FormatError("selected mode has no LotLocations table");

    std::vector<FamilyEntry> family_entries;
    std::optional<std::array<std::uint8_t, 32>> neighborhood_identity;
    std::optional<Nbrs> raw_neighbors;
    with_iff_file(user_data / "Neighborhood.iff", [&](const auto& view) {
        if (saved_overlay) neighborhood_identity = identity_of(view);
        iff_chunks(view, [&](const Chunk& chunk) {
            if (chunk.type == "FAMI") {
                if (family_entries.size() >= 4096) throw FormatError("too many family records");
                family_entries.push_back({chunk.id, read_fami(chunk.payload)});
            } else if (chunk.type == "NBRS" && chunk.id == 1) {
                if (raw_neighbors) throw FormatError("ambiguous NBRS1 neighborhood table");
                raw_neighbors = read_nbrs(chunk.payload);
            }
        });
    });
    if (!raw_neighbors) throw FormatError("Neighborhood.iff has no NBRS1 table");
    if (saved_overlay) {
        family_entries = apply_family_overlay(family_entries, *neighborhood_identity,
                                               *saved_overlay);
        if (!saved_overlay->creations.empty())
            *raw_neighbors = apply_neighbor_overlay(*raw_neighbors, *neighborhood_identity,
                                                     *saved_overlay);
    }

    const auto house_file = user_data / "Houses" /
        ("House" + (house < 10 ? std::string("0") : std::string()) + std::to_string(house) + ".iff");
    bool has_simi = false;
    with_iff_file(house_file, [&](const auto& view) {
        iff_chunks(view, [&](const Chunk& chunk) {
            if (chunk.type != "SIMI" || chunk.id != 1) return;
            if (has_simi) throw FormatError("ambiguous SIMI1 selected house");
            (void)read_simi(chunk.payload);
            has_simi = true;
        });
    });
    if (!has_simi) throw FormatError("selected house has no valid SIMI1");

    const FamilyContext families(family_entries);
    const NeighborContext neighbors(std::move(*raw_neighbors));
    auto character_candidates=read_character_catalog(user_data / "Characters");
    for (auto& candidate:character_candidates) candidate.source="UserData/Characters";
    if (!player_character_directory.empty()) {
        std::error_code error;
        const auto status=std::filesystem::symlink_status(player_character_directory,error);
        if (error==std::errc::no_such_file_or_directory ||
            (!error&&status.type()==std::filesystem::file_type::not_found)) {
            error.clear();
        } else {
            if (error || std::filesystem::is_symlink(status) ||
                !std::filesystem::is_directory(status))
                throw FormatError("player character catalog path is not a real directory");
            CharacterCatalogLimits private_limits;
            private_limits.files=1024;
            private_limits.total_file_bytes=64ull*1024ull*1024ull;
            private_limits.person_definitions=4096;
            auto private_candidates=read_character_catalog(player_character_directory,
                                                            private_limits);
            for (auto& candidate:private_candidates) candidate.source="PlayerData/Characters";
            if (private_candidates.size()>8192-character_candidates.size())
                throw FormatError("combined character definitions exceed configured limit");
            character_candidates.insert(character_candidates.end(),
                std::make_move_iterator(private_candidates.begin()),
                std::make_move_iterator(private_candidates.end()));
        }
    }
    const DefinitionIndex characters(character_candidates);
    return resolve_session_entry(mode, house, house_file.string(), *locations,
                                 families, neighbors, characters);
}

} // namespace odh
