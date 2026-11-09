#include "linux_family_session_journal.hpp"
#include "linux_family_transfer.hpp"
#include "linux_family_overlay_store.hpp"
#include "linux_session_archive_store.hpp"
#include "odh/world_snapshot_codec.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
// Each first-profile scenario owns its entire configuration root: different
// neighborhood identities still share the player-state parent directory.
struct PrivateConfigHome {
    TemporaryDirectory directory;
    std::string previous;
    bool had_previous = false;
    PrivateConfigHome() {
        if (const auto* value = std::getenv("XDG_CONFIG_HOME")) {
            previous = value; had_previous = true;
        }
        require(::setenv("XDG_CONFIG_HOME", directory.path.c_str(), 1) == 0,
            "set isolated first-profile config");
    }
    ~PrivateConfigHome() {
        if (had_previous) ::setenv("XDG_CONFIG_HOME", previous.c_str(), 1);
        else ::unsetenv("XDG_CONFIG_HOME");
    }
};
// Synthetic native bundle, constructed with the actual world/controller and
// scheduler snapshot APIs so publication exercises the complete bundle codec.
odh::SessionBundle bundle_fixture() {
    odh::World world;
    require(world.create_saved_entity(1, odh::EntityKind::avatar, 123456,
        {}, std::vector<std::int16_t>(80), std::vector<std::int16_t>(16), {}, {}, 0), "create fixture actor");
    require(world.set_person_data(1,31,41) && world.set_person_data(1,61,21),
        "bind fixture person neighbor and family identity");
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
    odh::SessionSimulationState simulation;
    simulation.clock = odh::Ts1Clock{}.snapshot();
    odh::Ts1MotiveSnapshot decay;
    decay.fractions = {{11,22,33,44,55,66,77}};
    simulation.avatars.push_back({1, decay});
    bundle.simulation = simulation;
    return bundle;
}
void first_save_after_last_body_retirement(){
    std::array<std::uint8_t,32> identity{};identity[0]=0x6d;
    odh::Family original;original.house_number=7;original.family_number=12;original.budget=9000;original.member_guids={123456};
    const std::vector<odh::FamilyEntry> source{{21,original}};
    auto capture=bundle_fixture();const auto member=capture.household_roster.front();
    const odh::SessionFamilyLease lease{identity,odh::family_overlay_identity(source.front()),1,21};
    odh::World world;require(world.restore_snapshot(capture.world)&&world.remove_retaining_body({member.id,member.generation})&&world.collect_retained_bodies({}),
        "actual last resident removal releases its body while preserving source neighborhood");
    capture.world=world.snapshot();capture.household_roster.clear();capture.runtime_entities.clear();capture.scheduler.entities.clear();capture.simulation->avatars.clear();
    capture.host.resident_guid=0;capture.host.resident_neighbor_id=0;capture.host.runtime_avatar_id=0;
    capture.object_history=odh::SessionObjectHistory{};capture.object_history->complete=true;capture.object_history->reusable_entity_ids=true;
    odh::SessionRetiredHouseholdAvatar origin;origin.member=member;origin.source_lease=lease;
    origin.source=odh::SessionHouseholdRetirementSource::resource_constructor;capture.object_history->retired_household_avatars={origin};
    capture.unanchored_family_lot=odh::SessionUnanchoredFamilyLot{{member.id,member.generation},0};
    capture.ui_state=odh::SessionUiState{0,0,0,true};
    odh::RuntimeGlobalsSnapshot globals;globals.words.resize(38);globals.words[3]=member.id;globals.selected_sim_is_runtime_id=true;
    capture.runtime_globals=globals;capture.host.global_values.clear();
    odh::SessionCharacterFamily family;family.shared.neighborhood_identity=identity;family.shared.revision=1;
    family.shared.rosters={{21,lease.family_identity,1,{}}};family.runtime_subset={member.guid};capture.character_family=family;
    unsigned validated=0;
    const auto validate=[&](const auto& candidate,const auto& bytes){++validated;const auto cold=odh::decode_session_bundle(bytes);
        require(cold.unanchored_family_lot&&cold.world.entities.empty()&&cold.runtime_entities.empty()&&cold.family_lease&&
            cold.character_family->shared.rosters.front().members.empty()&&candidate.host.family_budget==7000,
            "native first-save validation sees real empty lot and current retained FAMI provider");};
    auto journal=odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,7,21,capture,validate);
    const auto overlay=odh_host::read_linux_family_overlay(odh_host::linux_family_overlay_path(identity));
    require(validated==1&&overlay&&overlay->character_family&&overlay->character_family->rosters.front().members.empty(),
        "first adoption publishes current CFAM and native head together, including last Generic6 membership removal");
    const auto effective=odh::replay_family_overlay(source,identity,*overlay);
    require(effective.families.size()==1&&effective.families.front().family.member_guids.empty()&&original.member_guids==std::vector<std::uint32_t>{123456},
        "empty current FAMI remains present and original catalog is unchanged");
    const auto bytes=odh::encode_session_archive(odh_host::read_session_archive(*journal.checkpoint_path()));
    odh_host::FamilySessionJournal cold(source,identity,overlay,7,21);cold.validate_restore(bytes);
    require(cold.lease().occupancy_epoch==1&&odh::encode_session_bundle(odh::decode_session_bundle(bytes))==bytes,
        "cold journal reopens exact last-body state without creating member, Thread or occupancy epoch");
}
void quoted_original_adoption_cases(){
    std::array<std::uint8_t,32> identity{};identity[0]=0xaa;
    odh::Family family;family.house_number=7;family.family_number=12;family.budget=9000;
    family.value_in_arch=4321;family.member_guids={123456};
    const std::vector<odh::FamilyEntry> source{{21,family}};
    const auto overlay_path=odh_host::linux_family_overlay_path(identity);
    auto capture=bundle_fixture();capture.host.family_budget=8765;
    unsigned validations=0;
    const auto validate=[&](const odh::SessionBundle& actual,const std::vector<std::uint8_t>& bytes){
        ++validations;const auto decoded=odh::decode_session_bundle(bytes);
        require(decoded.family_lease&&decoded.host.family_budget==actual.host.family_budget&&
            odh::encode_world_snapshot(decoded.world)==odh::encode_world_snapshot(actual.world),
            "quoted adoption validates its actual full native capture before publication");
    };
    rejects([&]{odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,
        7,21,capture,validate,{},-1);},"negative original property rejects before native callback or I/O");
    auto wrong=capture;wrong.family_lease=odh::SessionFamilyLease{identity,{},2,21};
    rejects([&]{odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,
        7,21,wrong,validate,{},10653);},"quoted adoption cannot replace original occupancy lease");
    require(validations==0&&!odh_host::read_linux_family_overlay(overlay_path),
        "invalid original quote/lease creates no family authority and runs no restore callback");
    auto journal=odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,
        7,21,capture,validate,{},10653);
    const auto overlay=odh_host::read_linux_family_overlay(overlay_path);
    const auto state=odh::replay_family_overlay(source,identity,*overlay).states.front();
    require(validations==1&&overlay->assignments.size()==1&&state.value_in_arch==10653&&
        state.budget==8765&&state.house_number==7&&state.occupancy_epoch==1,
        "original property cache and first native head commit in exactly one checkpoint operation");
    const auto path=journal.checkpoint_path();require(path.has_value(),"quoted original head exists");
    const auto archive=odh::encode_session_archive(odh_host::read_session_archive(*path));
    journal.validate_restore(archive);
    const auto before=odh::encode_family_overlay(*overlay);
    rejects([&]{odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,
        7,21,capture,validate,{},20000);},"stale first-profile quoted adoption cannot overwrite committed cache");
    require(odh::encode_family_overlay(*odh_host::read_linux_family_overlay(overlay_path))==before&&
        odh::encode_session_archive(odh_host::read_session_archive(*path))==archive,
        "failed concurrent original adoption retains exact committed cache, cash and archive");
}
void property_checkpoint_cases(){
    std::array<std::uint8_t,32> identity{};identity[0]=0xa9;
    odh::Family family;family.family_number=12;family.budget=8000;family.member_guids={123456};
    const std::vector<odh::FamilyEntry> source{{21,family}};
    const auto managed=odh::plan_family_managed_move_in_overlay(source,identity,nullptr,21,7,1000);
    const auto overlay_path=odh_host::linux_family_overlay_path(identity);
    odh_host::compare_exchange_linux_family_overlay_atomic(overlay_path,std::nullopt,managed);
    odh_host::FamilySessionJournal journal(source,identity,managed,7,21);
    auto bundle=bundle_fixture();
    const auto path=journal.publish(bundle,5000);
    const auto archive=odh::encode_session_archive(odh_host::read_session_archive(path));
    auto committed=odh_host::read_linux_family_overlay(overlay_path);
    auto state=odh::replay_family_overlay(source,identity,*committed).states.front();
    require(state.budget==7000&&state.value_in_arch==5000&&state.occupancy_epoch==1,
        "quoted checkpoint publishes property cache without charging or refunding cash");
    odh_host::FamilySessionJournal stale_same_head(source,identity,committed,7,21);
    require(journal.publish(bundle,6000)==path,"property-only update preserves exact immutable session archive");
    committed=odh_host::read_linux_family_overlay(overlay_path);
    state=odh::replay_family_overlay(source,identity,*committed).states.front();
    require(state.value_in_arch==6000&&state.budget==7000&&committed->assignments.size()==3,
        "same archive digest still publishes a changed current property quote");
    const auto overlay_bytes=odh::encode_family_overlay(*committed);
    require(journal.publish(bundle,6000)==path&&journal.publish(bundle)==path&&
        odh::encode_family_overlay(*odh_host::read_linux_family_overlay(overlay_path))==overlay_bytes,
        "repeated quote and legacy omitted quote retain cache without another operation");
    rejects([&]{journal.publish(bundle,-1);},"negative property quote rejects before persistence");
    auto wrong=bundle;wrong.host.house_number=8;
    rejects([&]{journal.publish(wrong,9000);},"wrong occupancy cannot publish a current property quote");
    rejects([&]{stale_same_head.publish(bundle,9000);},"stale same-digest publisher cannot overwrite newer property cache");
    require(odh::encode_family_overlay(*odh_host::read_linux_family_overlay(overlay_path))==overlay_bytes&&
        odh::encode_session_archive(odh_host::read_session_archive(path))==archive&&
        odh::encode_family_overlay(*stale_same_head.expected_overlay())!=overlay_bytes,
        "failed quote publication retains disk authority, old archive and stale publisher before-state");
    odh_host::FamilySessionJournal reopened(source,identity,committed,7,21);reopened.validate_restore(archive);
    require(reopened.publish(bundle)==path,"fresh process preserves current quote through legacy publication");
    const auto moved=odh::plan_family_managed_move_out_overlay(source,identity,&*committed,21);
    const auto liquidated=odh::replay_family_overlay(source,identity,moved).states.front();
    require(liquidated.house_number==0&&liquidated.budget==13000&&liquidated.value_in_arch==0,
        "quoted cache persists to actual move-out planning and is added exactly once");
}
void original_adoption_cases(){
    PrivateConfigHome profile;
    std::array<std::uint8_t,32> identity{};identity[0]=0x79;
    odh::Family original;original.house_number=7;original.family_number=12;original.budget=9000;original.value_in_arch=4321;original.member_guids={123456};
    const std::vector<odh::FamilyEntry> source{{21,original}};
    const auto overlay_path=odh_host::linux_family_overlay_path(identity);
    auto capture=bundle_fixture();capture.host.family_budget=8765; // Actual live changes are captured, not a new purchase/refund.
    capture.type_attributes=odh::TypeAttributesSnapshot{1,{{700,{-32768,123,32767}}}};
    odh::World world;require(world.restore_snapshot(capture.world),"original adoption source World");
    require(world.set_position(1,{32,48,1,4})&&world.declare_object_word(1,0,71),"actual source resident runtime state");
    require(world.create_saved_entity(2,odh::EntityKind::object,0x1234,{},{},{},{},{},0),"original saved object preserved in capture");
    capture.world=world.snapshot();const auto source_world=odh::encode_world_snapshot(capture.world);
    unsigned validations=0;bool reject_restore=true;
    const auto validator=[&](const odh::SessionBundle& candidate,const std::vector<std::uint8_t>& bytes){
        ++validations;require(candidate.family_lease&&candidate.family_lease->occupancy_epoch==1&&
            odh::encode_world_snapshot(candidate.world)==source_world&&odh::encode_session_bundle(odh::decode_session_bundle(bytes))==bytes,
            "native adoption validator receives actual complete source state and source epoch");
        if(reject_restore)throw odh::FormatError("injected real native restore refusal");};
    require(!std::filesystem::exists(overlay_path.parent_path()),
        "restore refusal starts from an actual first-profile directory state");
    bool restore_refused = false;
    try {
        (void)odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,7,21,capture,validator);
    } catch (const odh::FormatError& error) {
        if (std::string(error.what()) != "injected real native restore refusal") throw;
        restore_refused = true;
    }
    require(restore_refused,"failed native restore preparation cannot adopt original family");
    require(validations==1&&!odh_host::read_linux_family_overlay(overlay_path)&&!std::filesystem::exists(overlay_path.parent_path()),"restore rejection creates neither authority nor first-profile directories");
    reject_restore=false;
    auto wrong=capture;wrong.host.house_number=8;
    rejects([&]{(void)odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,7,21,wrong,validator);},"wrong captured house cannot adopt source occupancy");
    wrong=capture;wrong.family_lease=odh::SessionFamilyLease{identity,odh::family_overlay_identity(source.front()),2,21};
    rejects([&]{(void)odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,7,21,wrong,validator);},"fabricated next epoch rejected");
    wrong=capture;wrong.host.family_number=13;
    rejects([&]{(void)odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,7,21,wrong,validator);},"different FamilyNumber rejected");
    rejects([&]{(void)odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,7,21,capture,{});},"missing native restore validator cannot authorize adoption");
    require(!odh_host::read_linux_family_overlay(overlay_path),"invalid adoption cases leave commercial occupancy unmanaged");
    auto journal=odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,7,21,capture,validator);
    const auto expected=odh_host::read_linux_family_overlay(overlay_path);
    require(expected&&expected->assignments.size()==1&&expected->assignments.front().action==odh::FamilyOverlayAction::checkpoint&&
        expected->assignments.front().state->expected_house==7&&expected->assignments.front().state->expected_occupancy_epoch==1&&
        expected->assignments.front().state->budget_before==9000&&expected->assignments.front().purchase_price==0,
        "adoption stores one source before-state checkpoint, no fake move-in or price");
    require(expected->type_attributes&&expected->type_attributes->revision==1&&
        expected->type_attributes->state.by_guid.at(700)==std::vector<std::int16_t>({-32768,123,32767}),
        "original adoption publishes complete shared TATT with first family head in the same CAS");
    const auto replay=odh::replay_family_overlay(source,identity,*expected);
    require(replay.states.front().managed&&replay.states.front().budget==8765&&replay.states.front().value_in_arch==4321&&
        replay.states.front().house_number==7&&replay.states.front().occupancy_epoch==1&&source.front().family.budget==9000,
        "adoption preserves captured finances/architecture/house/epoch and immutable source family");
    const auto path=journal.checkpoint_path();require(path.has_value(),"adoption published complete resumable original checkpoint");
    const auto bytes=odh::encode_session_archive(odh_host::read_session_archive(*path));journal.validate_restore(bytes);
    const auto decoded=odh::decode_session_bundle(bytes);
    require(decoded.host.family_budget==capture.host.family_budget&&odh::encode_world_snapshot(decoded.world)==source_world,
        "fresh canonical archive preserves original objects and exact captured World");
    odh_host::FamilySessionJournal fresh(source,identity,expected,7,21);fresh.validate_restore(bytes);
    require(fresh.lease().occupancy_epoch==1&&fresh.checkpoint_path()==path,"fresh process opens adopted source occupancy");
    rejects([&]{(void)odh_host::FamilySessionJournal::adopt_original_session(source,identity,expected,7,21,capture,validator);},"re-adoption cannot replace current history");
    // Expected=null is now stale even though the immutable original FAMI is
    // unchanged. The new journal must not silently rebase onto another writer.
    rejects([&]{(void)odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,7,21,capture,validator);},"stale original adoption loses CAS without replacing head");
    require(odh::encode_family_overlay(*odh_host::read_linux_family_overlay(overlay_path))==odh::encode_family_overlay(*expected)&&
        odh::encode_session_archive(odh_host::read_session_archive(*path))==bytes,"rejected duplicate/stale adoption retains exact authority and bytes");
    const std::string original_config=std::getenv("XDG_CONFIG_HOME");
    const auto blocked_profile=std::filesystem::path(original_config)/"blocked-adoption";
    require(::setenv("XDG_CONFIG_HOME",blocked_profile.c_str(),1)==0,"isolate first-profile invalid directory test");
    const auto blocked_overlay=odh_host::linux_family_overlay_path(identity);
    std::filesystem::create_directories(blocked_overlay.parent_path());
    const auto blocked_sessions=blocked_overlay.parent_path()/"sessions";
    {std::ofstream file(blocked_sessions);file<<"keep this non-directory";}
    rejects([&]{(void)odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,7,21,capture,validator);},
        "first adoption refuses an existing non-directory archive path");
    require(std::filesystem::is_regular_file(blocked_sessions)&&!odh_host::read_linux_family_overlay(blocked_overlay),
        "directory preparation cannot replace an existing file or create authority on failure");
    require(::setenv("XDG_CONFIG_HOME",original_config.c_str(),1)==0,"restore isolated main test profile");
}

void shared_neighborhood_type_attribute_cases() {
    std::array<std::uint8_t,32> identity{};identity[0]=0x39;
    odh::Family a;a.family_number=12;a.budget=9000;a.member_guids={123456};
    auto b=a;b.family_number=13;b.member_guids={223456};
    const std::vector<odh::FamilyEntry> source{{21,a},{22,b}};
    auto initial=odh::plan_family_managed_move_in_overlay(source,identity,nullptr,21,7,1000);
    initial=odh::plan_family_managed_move_in_overlay(source,identity,&initial,22,9,2000);
    const auto overlay_path=odh_host::linux_family_overlay_path(identity);
    odh_host::compare_exchange_linux_family_overlay_atomic(overlay_path,std::nullopt,initial);
    odh_host::FamilySessionJournal first(source,identity,initial,7,21);
    odh_host::FamilySessionJournal stale_other(source,identity,initial,9,22);
    auto first_bundle=bundle_fixture();first_bundle.type_attributes=odh::TypeAttributesSnapshot{1,{{700,{71,-1}}}};
    const auto first_path=first.publish(first_bundle);
    const auto first_bytes=odh::encode_session_archive(odh_host::read_session_archive(first_path));
    auto head=odh_host::read_linux_family_overlay(overlay_path);
    require(head&&head->type_attributes&&head->type_attributes->revision==1&&head->type_attributes->state.by_guid.at(700)[0]==71,
        "first family checkpoint and neighborhood TATT publish in one v8 authority");
    auto other=bundle_fixture();other.host.house_number=9;other.host.family_chunk_id=22;other.host.family_number=13;
    other.host.resident_guid=223456;other.host.resident_neighbor_id=42;
    other.household_roster.front().guid=223456;other.household_roster.front().neighbor_id=42;
    other.household_roster.front().character_asset="PlayerData/Characters/User2.iff";
    other.household_roster.front().character_fingerprint.fill(0x32);other.resources.resource_fingerprint.fill(0x5b);
    other.world.entities.front().definition->guid=223456;other.world.entities.front().person_data[31]=42;other.world.entities.front().person_data[61]=22;
    other.type_attributes=first_bundle.type_attributes;other.type_attributes->by_guid[700][0]=72;
    rejects([&]{stale_other.publish(other);},"concurrent stale family journal cannot overwrite newer neighborhood TATT");
    require(odh::encode_family_overlay(*odh_host::read_linux_family_overlay(overlay_path))==odh::encode_family_overlay(*head),
        "conflict preserves exact combined checkpoint and TATT authority");
    odh_host::FamilySessionJournal second(source,identity,head,9,22);
    const auto other_path=second.publish(other);head=odh_host::read_linux_family_overlay(overlay_path);
    require(head->type_attributes->revision==2&&head->type_attributes->state.by_guid.at(700)[0]==72,
        "second family advances same neighborhood bank");
    odh_host::FamilySessionJournal reopened_first(source,identity,head,7,21);
    reopened_first.validate_restore(first_bytes);
    const auto selection=odh::select_neighborhood_type_attributes(identity,{},
        odh::decode_session_bundle(first_bytes).type_attributes,&*head);
    require(selection.superseded_session_state&&selection.private_revision==2&&selection.state.by_guid.at(700)[0]==72,
        "reopening old first-family checkpoint selects second family's latest shared state");
    auto reconciled=odh::decode_session_bundle(first_bytes);reconciled.type_attributes=selection.state;
    const auto reconciled_path=reopened_first.publish(reconciled);head=odh_host::read_linux_family_overlay(overlay_path);
    require(head->type_attributes->revision==2&&reconciled_path!=first_path&&
        odh::encode_session_archive(odh_host::read_session_archive(first_path))==first_bytes,
        "unchanged shared bank keeps revision while first family publishes reconciled checkpoint, retaining old archive");
    const auto unchanged=odh::encode_family_overlay(*head);
    auto missing=reconciled;missing.type_attributes.reset();
    rejects([&]{reopened_first.publish(missing);},"checkpoint cannot silently omit shared bank after authority exists");
    require(odh::encode_family_overlay(*odh_host::read_linux_family_overlay(overlay_path))==unchanged,
        "missing shared bank fails before replacing head");
    const auto archive_count=std::distance(std::filesystem::directory_iterator(reopened_first.archive_directory()),
        std::filesystem::directory_iterator{});
    auto invalid=reconciled;invalid.type_attributes->by_guid[700].resize(4097);
    rejects([&]{reopened_first.publish(invalid);},"oversized shared bank fails complete checkpoint preparation");
    require(std::distance(std::filesystem::directory_iterator(reopened_first.archive_directory()),
        std::filesystem::directory_iterator{})==archive_count&&
        odh::encode_family_overlay(*odh_host::read_linux_family_overlay(overlay_path))==unchanged,
        "invalid shared bank creates no archive and changes no combined authority");
    (void)other_path;
}


void terminal_deletion_authority_cases() {
    std::array<std::uint8_t,32> identity{};identity[0]=0x7a;
    odh::Family family;family.family_number=12;family.budget=9000;
    family.member_guids={123456};
    auto other=family;other.family_number=13;other.member_guids={654321};
    const std::vector<odh::FamilyEntry> source{{21,family},{22,other}};
    auto initial=odh::plan_family_managed_move_in_overlay(source,identity,nullptr,21,7,1000);
    const auto path=odh_host::linux_family_overlay_path(identity);
    odh_host::compare_exchange_linux_family_overlay_atomic(path,std::nullopt,initial);
    odh_host::FamilySessionJournal journal(source,identity,initial,7,21);
    const auto checkpoint=journal.publish(bundle_fixture());
    const auto bytes=odh::encode_session_archive(odh_host::read_session_archive(checkpoint));
    const auto saved=odh_host::read_linux_family_overlay(path);
    const auto out=odh::plan_family_managed_move_out_overlay(source,identity,&*saved,21);
    odh::NeighborRecord neighbor{};neighbor.version=4;neighbor.name="Deleted Sim";neighbor.neighbor_id=41;
    neighbor.guid=123456;neighbor.unknown_neg_one=-1;
    const odh::Nbrs neighbors{0,73,{{1,neighbor}},0};
    const auto deleted=odh::plan_family_delete_overlay(source,neighbors,identity,&out,21);
    odh_host::compare_exchange_linux_family_overlay_atomic(path,saved,deleted);
    rejects([&]{journal.verify_current();},"deleted family invalidates open journal");
    rejects([&]{journal.validate_restore(bytes);},"old checkpoint cannot restore deleted family");
    rejects([&]{journal.publish(bundle_fixture());},"old journal cannot publish after deletion");
    const auto reopened=odh_host::read_linux_family_overlay(path);
    require(reopened.has_value(),"terminal deletion persists on disk");
    bool transfer_absent=false;
    try {(void)odh_host::load_family_transfer({identity,source},*reopened,21,{},
        "UserData/Neighborhood.iff");}
    catch(const odh::FormatError& error){
        transfer_absent=std::string(error.what()).find("family transfer family is absent")!=std::string::npos;
    }
    require(transfer_absent,"deleted family's carry ticket is rejected before reading retained resources");
    rejects([&]{odh_host::FamilySessionJournal fresh(source,identity,reopened,7,21);},
        "fresh process cannot prepare deleted family's journal");
    rejects([&]{(void)odh::plan_family_managed_move_in_overlay(source,identity,&*reopened,21,7,1000);},
        "deleted family cannot buy a house from retained history");
    const auto next=odh::plan_family_managed_move_in_overlay(source,identity,&*reopened,22,7,1000);
    const auto current=odh::apply_family_overlay(source,identity,next);
    require(current.size()==1&&current.front().chunk_id==22&&current.front().family.house_number==7,
        "another family can occupy released house without resurrecting deleted family");
    require(odh::encode_session_archive(odh_host::read_session_archive(checkpoint))==bytes,
        "terminal deletion preserves immutable checkpoint bytes");
}

}
int main() {
    try {
        TemporaryDirectory temporary;
        require(::setenv("XDG_CONFIG_HOME", temporary.path.c_str(), 1) == 0, "set private test config");
        first_save_after_last_body_retirement();
        original_adoption_cases();
        property_checkpoint_cases();
        quoted_original_adoption_cases();
        shared_neighborhood_type_attribute_cases();
        terminal_deletion_authority_cases();
        std::array<std::uint8_t, 32> identity{}; identity[0] = 0x71;
        odh::Family family;
        family.family_number = 12; family.budget = 9000; family.member_guids = {123456};
        const std::vector<odh::FamilyEntry> source{{21, family}};
        const auto managed = odh::plan_family_managed_move_in_overlay(source, identity, nullptr, 21, 7, 1000);
        const auto overlay_path = odh_host::linux_family_overlay_path(identity);
        odh_host::FamilySessionJournal pending_first(source, identity, std::nullopt, 7, 21, managed);
        odh_host::FamilySessionJournal stale_pending(source, identity, std::nullopt, 7, 21, managed);
        require(!pending_first.expected_overlay() && pending_first.runtime_overlay() &&
            !odh_host::read_linux_family_overlay(overlay_path), "pending purchase is an unpublished runtime view");
        require(!pending_first.carry_ticket(), "first purchase has no carry ticket");
        rejects([&] { pending_first.publish(bundle_fixture()); }, "save cannot implicitly commit a pending purchase");
        auto fabricated = managed;
        ++fabricated.assignments.back().budget_after_purchase;
        rejects([&] { odh_host::FamilySessionJournal invalid(source, identity, std::nullopt, 7, 21, fabricated); },
            "fabricated pending finances reject");
        fabricated = managed;
        fabricated.assignments.push_back(fabricated.assignments.back());
        rejects([&] { odh_host::FamilySessionJournal invalid(source, identity, std::nullopt, 7, 21, fabricated); },
            "pending purchase cannot append multiple operations");
        require(!odh_host::read_linux_family_overlay(overlay_path), "failed pending preparation leaves authority absent");
        pending_first.commit_move_in();
        require(pending_first.expected_overlay() && odh::encode_family_overlay(*pending_first.expected_overlay()) ==
            odh::encode_family_overlay(managed), "first purchase commits after explicit runtime-ready boundary");
        pending_first.commit_move_in(); // Already committed: still verifies current authority.
        rejects([&] { stale_pending.commit_move_in(); }, "stale pending authority cannot commit");
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
        require(journal.carry_ticket() && journal.carry_ticket()->checkpoint_budget == 7000 &&
            journal.carry_ticket()->source_house == 7 && journal.carry_ticket()->source_lease.occupancy_epoch == 1 &&
            journal.retained_checkpoint_path() == path, "current checkpoint exposes exact move-out preflight ticket");
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
        odh_host::compare_exchange_linux_family_overlay_atomic(overlay_path, committed, moved_out);
        odh_host::FamilySessionJournal pending_return(source, identity, moved_out, 7, 21, returned);
        require(pending_return.carry_ticket() && pending_return.carry_ticket()->checkpoint_budget == 7000 &&
            pending_return.carry_ticket()->source_lease.occupancy_epoch == 1 &&
            pending_return.lease().occupancy_epoch == 2 && !pending_return.checkpoint_path() &&
            pending_return.retained_checkpoint_path() == path, "pending destination retains source ticket and finances");
        rejects([&] { pending_return.commit_move_in(); }, "historical pending purchase requires applied carry receipt");
        rejects([&] { pending_return.publish(bundle); }, "pending historical publication cannot charge purchase");
        require(odh::encode_family_overlay(*odh_host::read_linux_family_overlay(overlay_path)) ==
            odh::encode_family_overlay(moved_out), "failed carry preparation leaves family unhoused and uncharged");
        const auto& member = bundle.household_roster.front();
        const std::vector<odh::HouseholdTransferIdentity> transfer_roster{{member.guid, member.neighbor_id,
            member.object_definition_id, member.character_asset, member.character_fingerprint}};
        const auto prepared = odh::prepare_household_transfer(encoded, *pending_return.carry_ticket(), transfer_roster);
        odh::World destination_world;
        std::vector<std::int16_t> destination_person(88);
        destination_person[31] = member.neighbor_id;
        destination_person[61] = 21;
        require(destination_world.create_saved_entity(1, odh::EntityKind::avatar, member.guid,
            {}, destination_person, std::vector<std::int16_t>(16), {}, {}, 0), "create fresh receipt destination actor");
        require(destination_world.set_position(1, {32,48,1,0}), "position fresh receipt destination actor");
        const auto applied = odh::apply_household_transfer(prepared, destination_world.snapshot(),
            odh::RelationshipState{}.snapshot(), pending_return.lease(), transfer_roster, bundle.simulation->clock);
        pending_return.accept_carry(applied);
        require(odh::encode_family_overlay(*odh_host::read_linux_family_overlay(overlay_path)) ==
            odh::encode_family_overlay(moved_out), "accepting a receipt alone never charges purchase");
        pending_return.commit_move_in();
        require(odh::encode_family_overlay(*odh_host::read_linux_family_overlay(overlay_path)) ==
            odh::encode_family_overlay(returned), "applied receipt allows exact pending purchase commit");
        rejects([&] { reopened.verify_current(); }, "old occupancy journal becomes stale");
        odh_host::FamilySessionJournal new_occupancy(source, identity, returned, 7, 21);
        require(new_occupancy.lease().occupancy_epoch == 2 && !new_occupancy.checkpoint_path(),
            "same house reentry retains carry ticket without enabling old full restore");
        rejects([&] { new_occupancy.validate_restore(encoded); }, "past occupancy archive cannot resume new occupants");
        wrong = bundle; wrong.family_lease = new_occupancy.lease();
        rejects([&] { new_occupancy.publish(wrong); }, "unconsumed carry ticket cannot be replaced with default Sim state");
        rejects([&] { odh_host::FamilySessionJournal invalid(source, identity, moved_out, 7, 21); },
            "unhoused family cannot prepare a live journal");
        auto destination_bundle = bundle;
        destination_bundle.family_lease = pending_return.lease();
        destination_bundle.host.family_budget = 6000; // Current overlay purchase, never source checkpoint's 7000.
        destination_bundle.world = applied.world();
        destination_bundle.relationships = applied.relationships();
        destination_bundle.household_roster = applied.members();
        destination_bundle.simulation->avatars = applied.decays();
        (void)odh::encode_session_bundle(destination_bundle);
        const auto before_publish = odh::encode_family_overlay(*odh_host::read_linux_family_overlay(overlay_path));
        const auto require_old_head = [&] {
            require(odh::encode_family_overlay(*odh_host::read_linux_family_overlay(overlay_path)) == before_publish &&
                pending_return.retained_checkpoint_path() == path && !pending_return.checkpoint_path(),
                "rejected receipt mismatch preserves authority and historical head");
            require(odh::encode_session_archive(odh_host::read_session_archive(path)) == encoded,
                "rejected receipt mismatch preserves original archive bytes");
        };
        wrong = destination_bundle;
        wrong.household_roster.front().character_asset = "PlayerData/Characters/Other.iff";
        (void)odh::encode_session_bundle(wrong); // Valid codec identity, but not the accepted receipt.
        rejects([&] { pending_return.publish(wrong); }, "first save cannot change accepted character resource");
        require_old_head();
        wrong = destination_bundle;
        ++wrong.world.entities.front().generation;
        ++wrong.world.generation_sequence;
        ++wrong.household_roster.front().generation;
        ++wrong.runtime_entities.front().controllers.interactions.actor_generation;
        if (wrong.runtime_entities.front().controllers.sleep.actor)
            ++wrong.runtime_entities.front().controllers.sleep.actor->generation;
        (void)odh::encode_session_bundle(wrong); // World, HHRD and execution generation remain coherent.
        rejects([&] { pending_return.publish(wrong); }, "first save cannot replace accepted runtime generation");
        require_old_head();
        const auto destination_path = pending_return.publish(destination_bundle);
        require(destination_path != path && pending_return.checkpoint_path() == destination_path &&
            pending_return.carry_ticket()->source_lease.occupancy_epoch == 2 &&
            pending_return.carry_ticket()->checkpoint_budget == 6000,
            "first destination save advances checkpoint with destination occupancy and budget");
        const auto destination_bytes = odh::encode_session_archive(odh_host::read_session_archive(destination_path));
        pending_return.validate_restore(destination_bytes);
        require(odh::encode_session_archive(odh_host::read_session_archive(path)) == encoded,
            "successful destination save retains original immutable carry archive");
        const auto destination_overlay = odh_host::read_linux_family_overlay(overlay_path);
        odh_host::FamilySessionJournal fresh_process(source, identity, destination_overlay, 7, 21);
        fresh_process.validate_restore(destination_bytes);
        const auto restored_destination = odh::decode_session_bundle(destination_bytes);
        require(restored_destination.simulation->avatars.front().decay.fractions ==
            bundle.simulation->avatars.front().decay.fractions, "fractional motive progress survives carry and first checkpoint");
        std::cout << "Linux family session journal checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
