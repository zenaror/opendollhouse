#include "linux_family_session_journal.hpp"
#include "linux_family_overlay_store.hpp"
#include "linux_session_archive_store.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F&& operation, const char* message) {
    try { operation(); } catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        path = std::filesystem::temp_directory_path() / ("odh-family-journal-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(std::filesystem::create_directory(path), "create isolated test directory");
    }
    ~TemporaryDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};
// Synthetic native bundle, constructed with the actual world/controller and
// scheduler snapshot APIs so publication exercises the complete bundle codec.
odh::SessionBundle bundle_fixture() {
    odh::World world;
    require(world.create_saved_entity(1, odh::EntityKind::avatar, 123456,
        {}, std::vector<std::int16_t>(80), std::vector<std::int16_t>(16), {}, {}, 0), "create fixture actor");
    odh::Vm vm(odh::RoutineSet{});
    odh::EntityThreadControllers controllers(world, 1);
    require(controllers.bind(vm), "bind fixture controllers");
    odh::EntityVmScheduler scheduler(4);
    require(scheduler.add(1, vm, [](std::uint64_t) { return true; },
        [] { return true; }, [] { return true; }), "register fixture VM");
    odh::SessionBundle bundle;
    bundle.generic_execution = true;
    odh::SessionHouseholdMember member;
    member.id = 1; member.generation = world.find(1)->generation;
    member.guid = 123456; member.neighbor_id = 41; member.object_definition_id = 128;
    member.character_asset = "PlayerData/Characters/User1.iff";
    member.character_fingerprint.fill(0x31);
    bundle.household_roster.push_back(member);
    bundle.resources.language_code = 7;
    bundle.resources.expansion_mask = 0x7f;
    bundle.resources.resource_fingerprint.fill(0x5a);
    bundle.host.neighborhood_mode = 1;
    bundle.host.house_number = 7;
    bundle.host.family_chunk_id = 21;
    bundle.host.family_number = 12;
    bundle.host.resident_guid = 123456;
    bundle.host.resident_neighbor_id = 41;
    bundle.host.runtime_avatar_id = 1;
    bundle.host.random_seed = 1234;
    bundle.host.family_budget = 7000;
    bundle.host.global_values = {{9, 21}};
    bundle.world = world.snapshot();
    bundle.relationships = odh::RelationshipState{}.snapshot();
    odh::SessionRuntimeEntity runtime;
    runtime.id = 1; runtime.vm = vm.snapshot(); runtime.controllers = controllers.snapshot();
    bundle.runtime_entities.push_back(std::move(runtime));
    bundle.scheduler = scheduler.snapshot();
    return bundle;
}
}
int main() {
    try {
        TemporaryDirectory temporary;
        require(::setenv("XDG_CONFIG_HOME", temporary.path.c_str(), 1) == 0, "set private test config");
        std::array<std::uint8_t, 32> identity{}; identity[0] = 0x71;
        odh::Family family;
        family.family_number = 12; family.budget = 9000; family.member_guids = {123456};
        const std::vector<odh::FamilyEntry> source{{21, family}};
        const auto managed = odh::plan_family_managed_move_in_overlay(source, identity, nullptr, 21, 7, 1000);
        const auto overlay_path = odh_host::linux_family_overlay_path(identity);
        odh_host::write_linux_family_overlay_atomic(overlay_path, managed);
        odh_host::FamilySessionJournal journal(source, identity, managed, 7, 21);
        odh_host::FamilySessionJournal stale_same_epoch(source, identity, managed, 7, 21);
        require(journal.lease().occupancy_epoch == 1 && !journal.checkpoint_path(), "fresh occupancy has no resume head");
        auto bundle = bundle_fixture();
        rejects([&] { journal.validate_restore(odh::encode_session_bundle(bundle)); }, "explicit restore without committed head rejects");
        auto wrong = bundle; wrong.host.house_number = 8;
        rejects([&] { journal.publish(wrong); }, "wrong host house rejects");
        wrong = bundle; wrong.family_lease = journal.lease(); ++wrong.family_lease->occupancy_epoch;
        rejects([&] { journal.publish(wrong); }, "wrong archive lease rejects");
        wrong = bundle; wrong.host.family_chunk_id = 22;
        rejects([&] { journal.publish(wrong); }, "wrong host family rejects");
        wrong = bundle; ++wrong.household_roster.front().guid;
        ++wrong.world.entities.front().definition->guid;
        ++wrong.host.resident_guid;
        wrong.family_lease = journal.lease();
        (void)odh::encode_session_bundle(wrong); // Internally valid, but belongs to another family.
        rejects([&] { journal.publish(wrong); }, "wrong family roster rejects");
        const auto path = journal.publish(bundle);
        require(journal.checkpoint_path() == path && std::filesystem::is_regular_file(path), "publish commits immutable checkpoint");
        const auto encoded = odh::encode_session_archive(odh_host::read_session_archive(path));
        journal.validate_restore(encoded);
        const auto committed = odh_host::read_linux_family_overlay(overlay_path);
        require(committed && committed->assignments.size() == 2, "publish appends exactly one checkpoint");
        require(journal.publish(bundle) == path &&
            odh::encode_family_overlay(*odh_host::read_linux_family_overlay(overlay_path)) == odh::encode_family_overlay(*committed),
            "same digest and budget publication is idempotent");
        odh_host::FamilySessionJournal reopened(source, identity, committed, 7, 21);
        reopened.validate_restore(encoded);
        require(reopened.checkpoint_path() == path, "new process state resolves current immutable checkpoint");
        rejects([&] { stale_same_epoch.publish(bundle); }, "same-epoch stale overlay cannot overwrite a newer checkpoint");
        wrong = odh::decode_session_bundle(encoded); ++*wrong.host.family_budget;
        rejects([&] { reopened.validate_restore(odh::encode_session_bundle(wrong)); }, "non-head archive rejects despite matching occupancy");
        const auto moved_out = odh::plan_family_managed_move_out_overlay(source, identity, &*committed, 21);
        const auto returned = odh::plan_family_managed_move_in_overlay(source, identity, &moved_out, 21, 7, 1000);
        odh_host::compare_exchange_linux_family_overlay_atomic(overlay_path, committed, returned);
        rejects([&] { reopened.verify_current(); }, "old occupancy journal becomes stale");
        odh_host::FamilySessionJournal new_occupancy(source, identity, returned, 7, 21);
        require(new_occupancy.lease().occupancy_epoch == 2 && !new_occupancy.checkpoint_path(),
            "same house reentry retains carry ticket without enabling old full restore");
        rejects([&] { new_occupancy.validate_restore(encoded); }, "past occupancy archive cannot resume new occupants");
        wrong = bundle; wrong.family_lease = new_occupancy.lease();
        rejects([&] { new_occupancy.publish(wrong); }, "unconsumed carry ticket cannot be replaced with default Sim state");
        rejects([&] { odh_host::FamilySessionJournal invalid(source, identity, moved_out, 7, 21); },
            "unhoused family cannot prepare a live journal");
        std::cout << "Linux family session journal checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
