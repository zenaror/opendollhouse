#include "linux_cas_catalog.hpp"
#include "linux_family_overlay_store.hpp"
#include "linux_private_family.hpp"
#include "linux_simitone_cas_member.hpp"
#include "resource.hpp"
#include "odh/family.hpp"
#include "odh/family_overlay.hpp"
#include "odh/lot.hpp"
#include "odh/lot_locations.hpp"
#include "odh/session_entry.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace {
template<class F> void iff(const fs::path& path, F&& callback) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + path.string());
    callback(odh::View(input, fs::file_size(path)));
}

std::vector<odh::FamilyEntry> source_families(const fs::path& path) {
    std::vector<odh::FamilyEntry> result;
    iff(path, [&](const auto& view) {
        odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
            if (chunk.type == "FAMI") result.push_back({chunk.id, odh::read_fami(chunk.payload)});
        });
    });
    return result;
}

odh::LotLocations locations(const fs::path& path) {
    std::optional<odh::LotLocations> result;
    iff(path, [&](const auto& view) {
        odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
            if (chunk.type == "STR#" && chunk.id == 1) {
                if (result) throw odh::FormatError("ambiguous LotLocations mode 1");
                result = odh::read_lot_locations(chunk.payload, 1);
            }
        });
    });
    if (!result) throw odh::FormatError("LotLocations mode 1 is absent");
    return *result;
}

std::int32_t purchase_price(const fs::path& path) {
    std::optional<odh::Simi> selected;
    iff(path, [&](const auto& view) {
        odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
            if (chunk.type != "SIMI" || chunk.id != 1) return;
            if (selected) throw odh::FormatError("ambiguous SIMI1");
            selected = odh::read_simi(chunk.payload);
        });
    });
    if (!selected) throw odh::FormatError("SIMI1 is absent");
    return selected->purchase_value();
}

std::vector<std::uint8_t> template_person(const fs::path& game) {
    std::vector<std::uint8_t> result;
    with_far_entry((game / "GameData/Objects/Objects.far").string(),
        R"(People\TemplatePerson.iff)", [&](const auto& view) {
            if (view.size() > 16u * 1024u * 1024u)
                throw odh::FormatError("TemplatePerson.iff exceeds 16 MiB");
            result.resize(static_cast<std::size_t>(view.size()));
            if (!result.empty()) view.read(0, result.data(), result.size());
        });
    return result;
}
}

int main(int argc, char** argv) try {
    if (argc != 3) throw std::runtime_error("usage: odh-cas-movein-e2e GAME_ROOT PRIVATE_PROFILE_ROOT");
    const fs::path game = fs::canonical(argv[1]);
    const fs::path profile = fs::absolute(argv[2]);
    const fs::path user = game / "UserData";
    const auto families = source_families(user / "Neighborhood.iff");
    const odh::FamilyContext family_context(families);

    std::uint16_t house = 0;
    fs::path house_path;
    std::int32_t price = 0;
    for (const auto& lot : locations(user / "LotLocations.iff").lots) {
        if (lot.house < 1 || lot.house > 98 || family_context.by_house(static_cast<std::int16_t>(lot.house))) continue;
        std::ostringstream name;
        name << "House" << (lot.house < 10 ? "0" : "") << lot.house << ".iff";
        const auto candidate = user / "Houses" / name.str();
        if (!fs::is_regular_file(candidate)) continue;
        try { price = purchase_price(candidate); }
        catch (const std::exception&) { continue; }
        house = lot.house;
        house_path = candidate;
        break;
    }
    if (!house) throw std::runtime_error("no vacant selectable base-game house with SIMI1 found");

    const auto cas = odh_host::scan_linux_cas_catalog(game);
    const auto heads = cas.heads("ma", "lgt");
    const auto bodies = cas.bodies("ma", "lgt");
    if (heads.empty() || bodies.empty()) throw std::runtime_error("real CAS catalog has no adult male light-skin choices");
    const auto& head = heads.front();
    const auto& body = bodies.front();
    if (price > std::numeric_limits<std::int32_t>::max() - 20000)
        throw std::runtime_error("selected lot price leaves no safe budget headroom");
    odh_host::SimitoneCasPersonInput input;
    input.name_utf8 = "ODH CAS Gate";
    input.biography_utf8 = "Opt-in integration gate";
    input.language = 1;
    input.skin = "lgt";
    input.body_type = body.body_type;
    input.body_appearance = body.appearance;
    input.body_texture = body.texture;
    input.head_appearance = head.appearance;
    input.head_texture = head.texture;
    input.handgroup_texture = body.handgroup_texture.empty() ? head.handgroup_texture : body.handgroup_texture;
    input.personality_points = {5, 5, 5, 5, 5};
    std::uint32_t cas_random = 0;
    auto draft = odh_host::make_simitone_cas_member(input,
        [&cas_random](std::uint32_t upper) { return (cas_random++) % upper; });

    const auto neighborhood = user / "Neighborhood.iff";
    const auto identity = odh_host::linux_neighborhood_identity(neighborhood);
    const auto overlay_path = odh_host::linux_family_overlay_path(identity);
    if (fs::exists(overlay_path)) throw std::runtime_error("temporary profile unexpectedly already has a family overlay");
    const auto characters = profile / "data/opendollhouse/Characters";
    const auto created = odh_host::create_linux_private_family(template_person(game),
        user / "Characters", characters, neighborhood, overlay_path,
        "ODH CAS Gate", {draft}, price + 20000);
    auto overlay = odh_host::read_linux_family_overlay(overlay_path);
    if (!overlay) throw std::runtime_error("CAS creation did not persist a player overlay");
    const auto moved = odh::plan_family_move_in_overlay(families, identity, &*overlay,
        created.family_chunk_id, house, price);
    odh_host::compare_exchange_linux_family_overlay_atomic(overlay_path, overlay, moved);
    overlay = moved;

    const auto session = odh::load_session_entry(user, 1, house, 1, &*overlay, characters);
    if (session.members.size() != 1 || session.members.front().guid != created.members.front().guid ||
        session.members.front().character_source != "PlayerData/Characters")
        throw std::runtime_error("load_session_entry did not resolve the created private CAS member");

    std::cout << "{\"status\":\"passed\",\"mode\":1,\"house\":" << house
              << ",\"purchase_price\":" << price << ",\"family_chunk_id\":" << created.family_chunk_id
              << ",\"family_number\":" << created.family_number << ",\"guid\":" << created.members.front().guid
              << ",\"neighbor_id\":" << session.members.front().neighbor_id
              << ",\"member_source\":\"" << session.members.front().character_source
              << "\",\"private_character\":\"" << created.members.front().path.string()
              << "\",\"house_file\":\"" << house_path.string() << "\"}\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "odh-cas-movein-e2e: " << error.what() << '\n';
    return 1;
}
