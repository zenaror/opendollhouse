#include "linux_family_overlay_store.hpp"
#include "odh/sha256.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
template<class F>
void rejects(F&& operation, const char* message) {
    try { operation(); }
    catch (const std::exception&) { return; }
    require(false, message);
}
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
            ("odh-family-overlay-store-" + std::to_string(stamp));
        if (!std::filesystem::create_directory(path))
            throw std::runtime_error("cannot create temporary family-overlay directory");
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
odh::FamilyOverlay overlay(std::uint8_t identity, std::uint16_t family,
        std::uint16_t house, std::int32_t price, std::int32_t budget) {
    odh::FamilyOverlay result;
    result.neighborhood_identity[0] = identity;
    odh::FamilyOverlayAssignment assignment;
    assignment.family_chunk_id = family;
    assignment.family_identity[0] = identity;
    assignment.house_number = house;
    assignment.purchase_price = price;
    assignment.budget_after_purchase = budget;
    result.assignments.push_back(assignment);
    return result;
}
}

int main() {
    TemporaryDirectory temp;
    const auto path = temp.path / "player-state" / "neighborhood.odhp";
    const auto first = overlay(0x41, 12, 7, 5500, 14500);
    const auto replacement = overlay(0x41, 18, 9, 2000, 8000);
    require(!odh_host::read_linux_family_overlay(path), "missing overlay remains an empty optional");

    odh_host::write_linux_family_overlay_atomic(path, first);
    const auto loaded = odh_host::read_linux_family_overlay(path);
    require(loaded && loaded->assignments.size() == 1 &&
            loaded->assignments[0].family_chunk_id == 12 &&
            loaded->assignments[0].house_number == 7 &&
            loaded->assignments[0].purchase_price == 5500,
            "saved overlay can be reopened from disk");
    auto stale_expected = *loaded;
    ++stale_expected.assignments[0].purchase_price;
    --stale_expected.assignments[0].budget_after_purchase;
    rejects([&] { odh_host::compare_exchange_linux_family_overlay_atomic(
                path, stale_expected, replacement); },
            "move-in compare-and-write rejects a stale player-state snapshot");
    const auto after_stale_reject = odh_host::read_linux_family_overlay(path);
    require(after_stale_reject && after_stale_reject->assignments[0].purchase_price == 5500,
            "stale move-in transaction leaves the previously saved overlay unchanged");
    odh_host::compare_exchange_linux_family_overlay_atomic(path, loaded, replacement);
    const auto exchanged = odh_host::read_linux_family_overlay(path);
    require(exchanged && exchanged->assignments[0].family_chunk_id == 18,
            "move-in compare-and-write publishes when its expected state still matches");
    struct stat info {};
    require(::stat(path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600,
            "overlay file is private to its owner");

    odh_host::write_linux_family_overlay_atomic(path, replacement);
    const auto replaced = odh_host::read_linux_family_overlay(path);
    require(replaced && replaced->assignments[0].family_chunk_id == 18 &&
            replaced->assignments[0].house_number == 9,
            "replacement publishes a complete new overlay");
    auto invalid = replacement;
    invalid.assignments[0].house_number = 0;
    rejects([&] { odh_host::write_linux_family_overlay_atomic(path, invalid); },
            "invalid overlay is rejected before touching the existing file");
    require(odh_host::read_linux_family_overlay(path)->assignments[0].family_chunk_id == 18,
            "failed replacement leaves the prior overlay intact");

    const auto damaged = temp.path / "damaged.odhp";
    { std::ofstream output(damaged, std::ios::binary); output << "short"; }
    rejects([&] { (void)odh_host::read_linux_family_overlay(damaged); },
            "truncated on-disk overlay is rejected");

    const auto directory_target = temp.path / "kept-directory";
    std::filesystem::create_directory(directory_target);
    const auto marker = directory_target / "marker";
    { std::ofstream output(marker); output << "keep"; }
    rejects([&] { odh_host::write_linux_family_overlay_atomic(directory_target, first); },
            "cannot replace a directory with an overlay file");
    require(std::filesystem::is_directory(directory_target) && std::filesystem::exists(marker),
            "failed atomic rename leaves the directory target intact");

    const auto simple_path = temp.path / "simple.odhp";
    if (::chmod(temp.path.c_str(), 0755) != 0)
        throw std::runtime_error("cannot set temporary directory permissions");
    struct stat directory_before {};
    require(::stat(temp.path.c_str(), &directory_before) == 0,
            "can inspect temporary directory before a simple file write");
    odh_host::write_linux_family_overlay_atomic(simple_path, first);
    struct stat directory_after {};
    require(::stat(temp.path.c_str(), &directory_after) == 0 &&
            (directory_before.st_mode & 0777) == (directory_after.st_mode & 0777),
            "writing a simple file does not change its parent directory permissions");
    struct stat simple_file_info {};
    require(::stat(simple_path.c_str(), &simple_file_info) == 0 &&
            (simple_file_info.st_mode & 0777) == 0600,
            "simple-path overlay file remains private to its owner");

    const auto outside = temp.path / "outside.odhp";
    odh_host::write_linux_family_overlay_atomic(outside, first);
    const auto link = temp.path / "link.odhp";
    std::filesystem::create_symlink(outside.filename(), link);
    rejects([&] { (void)odh_host::read_linux_family_overlay(link); },
            "overlay reader refuses symbolic links");
    const auto directory_link = temp.path / "directory-link";
    std::filesystem::create_directory_symlink(directory_target, directory_link);
    rejects([&] { odh_host::write_linux_family_overlay_atomic(directory_link / "child.odhp", first); },
            "overlay writer refuses a symbolic-link player-state directory");
    require(!std::filesystem::exists(directory_target / "child.odhp"),
            "refusing a linked directory does not write through it");

    const auto config_home = temp.path / "config-home";
    std::filesystem::create_directory(config_home);
    if (::setenv("XDG_CONFIG_HOME", config_home.c_str(), 1) != 0)
        throw std::runtime_error("cannot set test XDG_CONFIG_HOME");
    const auto neighborhood = temp.path / "Neighborhood.iff";
    const std::string source_bytes = "synthetic neighborhood source bytes";
    { std::ofstream output(neighborhood, std::ios::binary); output << source_bytes; }
    const auto identity = odh::sha256(source_bytes.data(), source_bytes.size());
    odh::Family free_family;
    free_family.family_number = 42;
    free_family.budget = 9000;
    free_family.member_guids = {0x12345678U, 0x87654321U};
    odh::Family resident_family;
    resident_family.family_number = 43;
    resident_family.house_number = 6;
    resident_family.budget = 5000;
    resident_family.value_in_arch = 2500;
    resident_family.member_guids = {0x11223344U};
    const std::vector<odh::FamilyEntry> synthetic_families{
        {700, free_family}, {701, resident_family}};
    const auto planned = odh::plan_family_move_in_overlay(
        synthetic_families, identity, nullptr, 700, 5, 3200);
    // A legitimate managed history can exceed the old assignment-only reader
    // cap while remaining below the shared codec/store byte and record limits.
    auto large_v6 = odh::plan_family_managed_move_in_overlay(
        synthetic_families, identity, nullptr, 700, 5, 3200);
    std::array<std::uint8_t, 32> checkpoint_digest{};
    checkpoint_digest[0] = 0x6a;
    large_v6 = odh::plan_family_checkpoint_overlay(
        synthetic_families, identity, &large_v6, 700, 5, 1, 5800, checkpoint_digest);
    const auto repeated_checkpoint = large_v6.assignments.back();
    large_v6.assignments.resize(2049, repeated_checkpoint);
    const auto large_v6_bytes = odh::encode_family_overlay(large_v6);
    require(large_v6_bytes[4] == 6 && large_v6_bytes.size() > 44 + 48 * 4096 &&
            large_v6_bytes.size() <= odh::family_overlay_max_bytes,
            "valid v6 history exceeds the obsolete assignment-only store cap");
    const auto large_v6_path = temp.path / "large-v6.odhp";
    odh_host::write_linux_family_overlay_atomic(large_v6_path, large_v6);
    const auto reopened_large_v6 = odh_host::read_linux_family_overlay(large_v6_path);
    require(reopened_large_v6 &&
            odh::encode_family_overlay(*reopened_large_v6) == large_v6_bytes,
            "store writes and reopens a v6 history above its former reader limit");
    const auto large_replay = odh::replay_family_overlay(
        synthetic_families, identity, *reopened_large_v6);
    require(large_replay.states[0].house_number == 5 &&
            large_replay.states[0].occupancy_epoch == 1 && large_replay.states[0].budget == 5800 &&
            large_replay.states[0].checkpoint_sha256 == checkpoint_digest,
            "large reopened v6 history retains a valid checkpoint and occupancy");
    const auto transaction_path = odh_host::linux_family_overlay_path(identity);
    odh_host::compare_exchange_linux_family_overlay_atomic(
        transaction_path, std::nullopt, planned);
    const auto committed = odh_host::read_linux_family_overlay_for_neighborhood(neighborhood);
    require(committed && committed->assignments.size() == 1 &&
            committed->assignments[0].purchase_price == 3200,
            "synthetic move-in plan is written and selected by the source neighborhood identity");
    const auto effective_families = odh::apply_family_overlay(
        synthetic_families, identity, *committed);
    const odh::FamilyContext effective_context(effective_families);
    require(effective_context.by_house(5) &&
            effective_context.by_house(5)->family.budget == 5800 &&
            odh_host::linux_neighborhood_identity(neighborhood) == identity,
            "saved synthetic move-in appears at its lot, charges the budget, and leaves source bytes unchanged");
    const auto moveout_plan = odh::plan_family_move_out_overlay(
        synthetic_families, identity, &planned, 701);
    odh_host::compare_exchange_linux_family_overlay_atomic(
        transaction_path, planned, moveout_plan);
    const auto moveout_committed = odh_host::read_linux_family_overlay_for_neighborhood(neighborhood);
    require(moveout_committed && moveout_committed->assignments.size() == 2 &&
            moveout_committed->assignments[1].action == odh::FamilyOverlayAction::move_out,
            "version 2 move-out operation survives the atomic player-save round trip");
    const auto after_moveout = odh::apply_family_overlay(
        synthetic_families, identity, *moveout_committed);
    require(after_moveout[1].family.house_number == 0 &&
            after_moveout[1].family.budget == 7500 &&
            after_moveout[1].family.value_in_arch == 0,
            "saved move-out gives back the recorded architecture value and clears the source marker");
    odh::Family created_family;
    created_family.version = 9;
    created_family.unknown = 24;
    created_family.family_number = 44;
    created_family.budget = 20000;
    created_family.member_guids = {0xa0000001U};
    odh::NeighborRecord created_neighbor{};
    created_neighbor.version = 10;
    created_neighbor.unknown3 = 9;
    created_neighbor.name = "iffname";
    created_neighbor.person_mode = 9;
    created_neighbor.person_data = std::vector<std::int16_t>(256, 0);
    (*created_neighbor.person_data)[61] = 702;
    created_neighbor.neighbor_id = 100;
    created_neighbor.guid = created_family.member_guids.front();
    created_neighbor.unknown_neg_one = -1;
    odh::FamilyOverlay created;
    created.neighborhood_identity = identity;
    created.creations.push_back({702, "Created in overlay",
        odh::encode_simitone_create_family_fami(created_family), {created_neighbor}});
    odh_host::compare_exchange_linux_family_overlay_atomic(
        transaction_path, moveout_committed, created);
    const auto reopened_creation = odh_host::read_linux_family_overlay_for_neighborhood(neighborhood);
    require(reopened_creation && reopened_creation->creations.size() == 1 &&
            reopened_creation->creations[0].family_name == "Created in overlay" &&
            reopened_creation->creations[0].members[0].guid == created_family.member_guids[0] &&
            odh::encode_family_overlay(*reopened_creation) == odh::encode_family_overlay(created),
            "created-family v3 survives private atomic compare-and-write and neighborhood reload");
    auto persisted = first;
    persisted.neighborhood_identity = identity;
    odh_host::write_linux_family_overlay_atomic(
        odh_host::linux_family_overlay_path(identity), persisted);
    const auto selected = odh_host::read_linux_family_overlay_for_neighborhood(neighborhood);
    require(selected && selected->neighborhood_identity == identity,
            "neighborhood source selects the matching saved overlay");
    const odh_host::LinuxNeighborhoodStorage third(3),fourth(4);
    const auto parent=neighborhood.parent_path();
    if(::setenv("XDG_DATA_HOME",temp.path.c_str(),1)!=0)
        throw std::runtime_error("cannot isolate test character directories");
    for(const auto invalid:{"UserData0","UserData1","UserData03","UserData9"})
        rejects([&]{(void)odh_host::LinuxNeighborhoodStorage::from_user_data(parent/invalid);},
            "ambiguous installation directory cannot select a storage namespace");
    const auto alias_base=parent/"alias-base";
    std::filesystem::create_directory(alias_base);
    std::filesystem::create_directory_symlink(parent,alias_base/"UserData3");
    rejects([&]{(void)third.qualify(alias_base);},"a linked namespace cannot redirect saves to another town");
    for(const auto scope:{third,fourth}){
        const auto folder=parent/scope.user_data_name();
        std::filesystem::create_directory(folder);
        std::filesystem::copy_file(neighborhood,folder/"Neighborhood.iff");
        require(odh_host::LinuxNeighborhoodStorage::from_user_data(folder).number()==scope.number() &&
            !odh_host::read_linux_family_overlay_for_neighborhood(folder/"Neighborhood.iff",scope),
            "byte-identical installed town does not inherit the first town's overlay");
    }
    const auto third_path=odh_host::linux_family_overlay_path(identity,third);
    const auto fourth_path=odh_host::linux_family_overlay_path(identity,fourth);
    odh_host::compare_exchange_linux_family_overlay_atomic(third_path,std::nullopt,created);
    odh_host::compare_exchange_linux_family_overlay_atomic(fourth_path,std::nullopt,persisted);
    require(odh::encode_family_overlay(*odh_host::read_linux_family_overlay_for_neighborhood(
            parent/"UserData3"/"Neighborhood.iff",third))==odh::encode_family_overlay(created) &&
        odh::encode_family_overlay(*odh_host::read_linux_family_overlay_for_neighborhood(
            parent/"UserData4"/"Neighborhood.iff",fourth))==odh::encode_family_overlay(persisted) &&
        odh::encode_family_overlay(*odh_host::read_linux_family_overlay_for_neighborhood(neighborhood))==
            odh::encode_family_overlay(persisted),"three independent overlay heads, including unchanged legacy head");
    require(odh_host::linux_player_character_directory(third)!=odh_host::linux_player_character_directory(fourth) &&
        odh_host::linux_player_character_directory().parent_path().filename()=="opendollhouse",
        "created character catalogs are separated without moving the legacy catalog");
    rejects([&]{(void)odh_host::read_linux_family_overlay_for_neighborhood(parent/"UserData3"/"Neighborhood.iff",fourth);},
        "matching commercial digests cannot substitute a different installed neighborhood");
    auto mismatched = persisted;
    mismatched.neighborhood_identity[0] ^= 1U;
    odh_host::write_linux_family_overlay_atomic(
        odh_host::linux_family_overlay_path(identity), mismatched);
    rejects([&] { (void)odh_host::read_linux_family_overlay_for_neighborhood(neighborhood); },
            "overlay content identity must match the path selected from Neighborhood.iff");
    odh_host::write_linux_family_overlay_atomic(
        odh_host::linux_family_overlay_path(identity), persisted);
    { std::ofstream output(neighborhood, std::ios::binary | std::ios::trunc); output << "changed"; }
    require(!odh_host::read_linux_family_overlay_for_neighborhood(neighborhood),
            "changed neighborhood source cannot load a stale saved overlay");
    std::cout << "Linux family overlay store checks passed\n";
}
