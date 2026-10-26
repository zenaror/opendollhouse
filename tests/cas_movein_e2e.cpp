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
#include <charconv>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
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
    const bool source_occupied = argc == 5 && std::string(argv[3]) == "--create-only-occupied-house";
    if ((argc != 3 && argc != 4 && !source_occupied) ||
        (argc == 4 && std::string(argv[3]) != "--create-only"))
        throw std::runtime_error("usage: odh-cas-movein-e2e GAME_ROOT PRIVATE_PROFILE_ROOT [--create-only | --create-only-occupied-house HOUSE]");
    const bool create_only = argc >= 4;
    unsigned selected_source_house = 0;
    if (source_occupied) {
        const std::string text = argv[4];
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), selected_source_house);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || selected_source_house < 1 || selected_source_house > 98)
            throw std::runtime_error("source occupied house must be in 1..98");
    }
    const fs::path game = fs::canonical(argv[1]);
    const fs::path profile = fs::absolute(argv[2]);
    const fs::path user = game / "UserData";
    const auto families = source_families(user / "Neighborhood.iff");
    const odh::FamilyContext family_context(families);

    std::uint16_t house = 0;
    fs::path house_path;
    std::int32_t price = 0, second_price = 0;
    std::uint16_t second_house = 0;
    const odh::FamilyEntry* source_family = nullptr;
    std::optional<odh::SessionEntry> source_session;
    const auto lot_locations = locations(user / "LotLocations.iff");
    if (source_occupied) {
        house = static_cast<std::uint16_t>(selected_source_house);
        source_family = family_context.by_house(static_cast<std::int16_t>(house));
        if (!source_family) throw std::runtime_error("selected source house has no original family");
        bool listed = false;
        for (const auto& lot : lot_locations.lots) if (lot.house == house) listed = true;
        if (!listed) throw std::runtime_error("selected source house is absent from LotLocations");
        std::ostringstream name;
        name << "House" << (house < 10 ? "0" : "") << house << ".iff";
        house_path = user / "Houses" / name.str();
        price = purchase_price(house_path);
        if (price < 0) throw std::runtime_error("selected source house has negative purchase price");
        source_session = odh::load_session_entry(user, 1, house, 1);
    }
    for (const auto& lot : lot_locations.lots) {
        if (lot.house < 1 || lot.house > 98 || family_context.by_house(static_cast<std::int16_t>(lot.house))) continue;
        std::ostringstream name;
        name << "House" << (lot.house < 10 ? "0" : "") << lot.house << ".iff";
        const auto candidate = user / "Houses" / name.str();
        if (!fs::is_regular_file(candidate)) continue;
        std::int32_t candidate_price = 0;
        try { candidate_price = purchase_price(candidate); }
        catch (const std::exception&) { continue; }
        if (candidate_price < 0) continue;
        if (!house) {
            price = candidate_price;
            house = lot.house;
            house_path = candidate;
            if (!create_only) break;
        } else if (lot.house != house && (!second_house || candidate_price < second_price)) {
            second_house = lot.house;
            second_price = candidate_price;
        }
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
    const auto initial_budget_wide = create_only ?
        2LL * price + second_price + 20000 : static_cast<std::int64_t>(price) + 20000;
    if (initial_budget_wide > std::numeric_limits<std::int32_t>::max())
        throw std::runtime_error("vacant lot round trip exceeds safe initial CAS budget");
    const auto initial_budget = static_cast<std::int32_t>(initial_budget_wide);
    std::uint32_t cas_random = 0;
    const auto make_member = [&](std::string name) {
        odh_host::SimitoneCasPersonInput input;
        input.name_utf8 = std::move(name);
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
        return odh_host::make_simitone_cas_member(input,
            [&cas_random](std::uint32_t upper) { return (cas_random++) % upper; });
    };
    const auto first_draft = make_member("ODH CAS Gate A");
    const auto second_draft = make_member("ODH CAS Gate B");

    const auto neighborhood = user / "Neighborhood.iff";
    const auto identity = odh_host::linux_neighborhood_identity(neighborhood);
    const auto overlay_path = odh_host::linux_family_overlay_path(identity);
    if (fs::exists(overlay_path)) throw std::runtime_error("temporary profile unexpectedly already has a family overlay");
    const auto characters = profile / "data/opendollhouse/Characters";
    const auto created = odh_host::create_linux_private_family(template_person(game),
        user / "Characters", characters, neighborhood, overlay_path,
        "ODH CAS Gate", {first_draft, second_draft}, initial_budget);
    auto overlay = odh_host::read_linux_family_overlay(overlay_path);
    if (!overlay) throw std::runtime_error("CAS creation did not persist a player overlay");
    if (source_occupied) {
        // Explicit test setup: expire only the selected original occupancy via
        // the native legacy planner, leaving all commercial resources untouched.
        const auto vacated = odh::plan_family_move_out_overlay(families, identity, &*overlay, source_family->chunk_id);
        odh_host::compare_exchange_linux_family_overlay_atomic(overlay_path, overlay, vacated);
        overlay = vacated;
    }
    const auto moved = create_only ?
        odh::plan_family_managed_move_in_overlay(families, identity, &*overlay, created.family_chunk_id, house, price) :
        odh::plan_family_move_in_overlay(families, identity, &*overlay, created.family_chunk_id, house, price);
    if (!create_only) odh_host::compare_exchange_linux_family_overlay_atomic(overlay_path, overlay, moved);
    // Resolve prospective members in create-only mode without housing the
    // persisted family: the real runtime owns deferred purchase publication.
    overlay = moved;

    const auto session = odh::load_session_entry(user, 1, house, 1, &*overlay, characters);
    if (session.members.size() != 2 || created.members.size() != 2 ||
        session.members.front().guid != created.members.front().guid ||
        session.members.front().character_source != "PlayerData/Characters" ||
        session.members.back().guid != created.members.back().guid ||
        session.members.back().character_source != "PlayerData/Characters")
        throw std::runtime_error("load_session_entry did not resolve the created private CAS member");

    std::cout << "{\"status\":\"passed\",\"mode\":1,\"house\":" << house
              << ",\"purchase_price\":" << price << ",\"family_chunk_id\":" << created.family_chunk_id
              << ",\"family_number\":" << created.family_number << ",\"member_count\":" << created.members.size()
              << ",\"guid\":" << created.members.front().guid
              << ",\"neighbor_id\":" << session.members.front().neighbor_id
              << ",\"member_source\":\"" << session.members.front().character_source
              << "\",\"private_character\":\"" << created.members.front().path.string()
              << "\",\"house_file\":\"" << house_path.string()
              << "\",\"create_only\":" << (create_only ? "true" : "false")
              << ",\"initial_budget\":" << initial_budget
              << ",\"second_house\":" << second_house << ",\"second_purchase_price\":" << second_price
              << ",\"source_house\":" << (source_occupied ? house : 0)
              << ",\"source_vacated_family_id\":" << (source_family ? source_family->chunk_id : 0)
              << ",\"source_expired_guids\":[";
    if (source_session) for (std::size_t i = 0; i < source_session->members.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << source_session->members[i].guid;
    }
    std::cout << "],\"source_expired_neighbor_ids\":[";
    if (source_session) for (std::size_t i = 0; i < source_session->members.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << source_session->members[i].neighbor_id;
    }
    std::cout << "],\"members\":[";
    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < session.members.size(); ++i) {
        if (i) std::cout << ',';
        const auto& member = session.members[i];
        std::string fingerprint;
        for (const auto byte : created.members[i].content_sha256) {
            fingerprint += hex[byte >> 4]; fingerprint += hex[byte & 15];
        }
        std::cout << "{\"guid\":" << member.guid << ",\"neighbor_id\":" << member.neighbor_id
            << ",\"object_definition_id\":" << member.object_definition_id
            << ",\"character_asset\":\"" << member.character_source << '/' << member.character_asset
            << "\",\"character_sha256\":\"" << fingerprint << "\"}";
    }
    std::cout << "]}\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "odh-cas-movein-e2e: " << error.what() << '\n';
    return 1;
}
