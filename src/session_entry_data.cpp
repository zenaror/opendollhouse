#include "odh/session_entry.hpp"

#include "odh/character_catalog.hpp"
#include "odh/lot.hpp"
#include "odh/sha256.hpp"
#include "odh/session_bundle.hpp"
#include "odh/native_personal_catalog.hpp"

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

SessionEntry load_visit_session_entry(const std::filesystem::path& user_data,
        unsigned home_mode, std::uint16_t home_house, unsigned mode,
        std::uint16_t house, const std::vector<std::uint32_t>& travelers,
        std::uint8_t language, const FamilyOverlay* overlay,
        const std::filesystem::path& player_characters, const SessionBundle* checkpoint) {
    std::optional<SessionBundle> resource_selection;
    if(checkpoint && checkpoint->character_family){
        if(checkpoint->host.house_number!=house || checkpoint->host.neighborhood_mode!=mode)
            throw FormatError("visited execution checkpoint differs from selected destination");
        // Only the resource lookup uses the permanent lot's FAMI assignment.
        // Original accepted bytes and the actual visited HOST remain untouched.
        resource_selection=*checkpoint;resource_selection->host.house_number=home_house;
        resource_selection->host.neighborhood_mode=static_cast<std::uint8_t>(home_mode);
    }
    const auto home = load_session_entry(user_data, home_mode, home_house,
        language, overlay, player_characters,resource_selection?&*resource_selection:nullptr);
    if (!neighborhood_mode_supported(mode) || !house || house >= 99)
        throw FormatError("temporary visit mode or house is unsupported");
    std::optional<LotLocations> locations;
    with_iff_file(user_data / "LotLocations.iff", [&](const auto& view) {
        iff_chunks(view, [&](const Chunk& chunk) {
            if (chunk.type != "STR#" || chunk.id != mode) return;
            if (locations) throw FormatError("ambiguous temporary visit LotLocations table");
            locations = read_lot_locations(chunk.payload, 1);
        });
    });
    if (!locations) throw FormatError("temporary visit mode has no LotLocations table");
    const auto file = user_data / "Houses" /
        ("House" + (house < 10 ? std::string("0") : std::string()) + std::to_string(house) + ".iff");
    bool simi = false;
    with_iff_file(file, [&](const auto& view) {
        iff_chunks(view, [&](const Chunk& chunk) {
            if (chunk.type != "SIMI" || chunk.id != 1) return;
            if (simi) throw FormatError("ambiguous temporary visit SIMI1");
            (void)read_simi(chunk.payload); simi = true;
        });
    });
    if (!simi) throw FormatError("temporary visit destination has no valid SIMI1");
    if(resource_selection&&home.npc_primary){
        if(house==home_house||std::count_if(locations->lots.begin(),locations->lots.end(),[&](const auto& lot){return lot.house==house;})!=1)
            throw FormatError("NPC visit destination differs from selected lot");
        auto visit=home;visit.neighborhood_mode=mode;visit.house_number=house;visit.house_file=file.string();return visit;
    }
    if(resource_selection){
        std::vector<std::uint32_t> execution;for(const auto& member:home.members)execution.push_back(member.guid);
        return resolve_visit_session_entry(home,mode,house,file.string(),*locations,execution);
    }
    return resolve_visit_session_entry(home, mode, house, file.string(), *locations, travelers);
}

SessionEntry load_session_entry(const std::filesystem::path& user_data,
        unsigned mode, std::uint16_t house, std::uint8_t language,
        const FamilyOverlay* saved_overlay,
        const std::filesystem::path& player_character_directory,
        const SessionBundle* checkpoint, bool identity_only) {
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
    const auto original_neighbors=*raw_neighbors;
    if (saved_overlay) {
        family_entries = apply_family_overlay(family_entries, *neighborhood_identity,
                                               *saved_overlay);
        if (!saved_overlay->creations.empty() || saved_overlay->character_family)
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

    if(checkpoint&&checkpoint->npc_primary){
        if(!saved_overlay)throw FormatError("NPC execution entry requires the accepted owned overlay");
        const auto owned=family_overlay_owned_neighbor_seeds(*saved_overlay);
        std::set<std::int16_t> retired;
        for(const auto& member:retired_family_members(*saved_overlay))retired.insert(member.neighbor_id);
        *raw_neighbors=checkpoint->personal_state&&checkpoint->personal_state->catalog?
            hydrate_owned_native_personal_neighbors(original_neighbors,*checkpoint->personal_state->catalog,owned,retired):
            merge_owned_neighbor_seeds(original_neighbors,owned);
        // Replay the original allocator first, then hide archived identities in
        // place. Erasing slots would renumber a persisted NPC catalog binding.
        for(auto& row:raw_neighbors->entries)if(row.record){
            if(retired.count(row.record->neighbor_id)){row={0,{}};continue;}
            auto& links=row.record->relationships;
            links.erase(std::remove_if(links.begin(),links.end(),[&](const auto& link){
                return link.key_count==1&&link.key>=1&&link.key<=4096&&retired.count(static_cast<std::int16_t>(link.key));
            }),links.end());
        }
    }
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
    const auto current=resolve_session_entry(mode,house,house_file.string(),*locations,
        families,neighbors,characters,identity_only || (checkpoint && checkpoint->character_family));
    if (identity_only || !checkpoint) return current;
    const auto execution=resolve_session_execution_entry(current,*checkpoint,neighbors,characters);
    for (const auto& member:checkpoint->household_roster) {
        const auto& resolved=*std::find_if(execution.members.begin(),execution.members.end(),
            [&](const auto& value){return value.guid==member.guid;});
        const auto root=resolved.character_source=="UserData/Characters"?user_data/"Characters":
            resolved.character_source=="PlayerData/Characters"?player_character_directory:std::filesystem::path{};
        if (root.empty()) throw FormatError("execution character has an unknown catalog root");
        with_iff_file(root/resolved.character_asset,[&](const auto& view){
            if(identity_of(view)!=member.character_fingerprint)
                throw FormatError("accepted HHRD character bytes changed");
        });
    }
    return execution;
}

} // namespace odh
