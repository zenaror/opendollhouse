#include "odh/session_entry.hpp"

#include "odh/character_catalog.hpp"
#include "odh/lot.hpp"

#include <fstream>
#include <utility>

namespace odh {
namespace {
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

SessionEntry load_session_entry(const std::filesystem::path& user_data,
        unsigned mode, std::uint16_t house, std::uint8_t language) {
    if (!neighborhood_mode_supported(mode))
        throw FormatError("selected neighborhood mode is unsupported");
    if (house == 0 || house >= 99) throw FormatError("selected house is outside the selectable range");
    if (language < 1 || language > 20) throw FormatError("selected language is outside 1..20");

    std::optional<LotLocations> locations;
    with_iff_file(user_data / "LotLocations.iff", [&](const auto& view) {
        iff_chunks(view, [&](const Chunk& chunk) {
            if (chunk.type != "STR#" || chunk.id != mode) return;
            if (locations) throw FormatError("ambiguous LotLocations mode table");
            locations = read_lot_locations(chunk.payload, language);
        });
    });
    if (!locations) throw FormatError("selected mode has no LotLocations table");

    std::vector<FamilyEntry> family_entries;
    std::optional<Nbrs> raw_neighbors;
    with_iff_file(user_data / "Neighborhood.iff", [&](const auto& view) {
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
    const DefinitionIndex characters(read_character_catalog(user_data / "Characters"));
    return resolve_session_entry(mode, house, house_file.string(), *locations,
                                 families, neighbors, characters);
}

} // namespace odh
