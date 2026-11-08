#include "linux_travel_session.hpp"
#include "linux_family_overlay_store.hpp"
#include "linux_session_archive_store.hpp"
#include "travel_session_fixture.hpp"
#include "odh/sha256.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
using travel_test::require;
template<class F>void rejects(F f,const char* why){try{f();}catch(const std::exception&){return;}throw std::runtime_error(why);}
void original_frontend_global_phase(){
 auto destination=travel_test::travel_state(travel_test::managed_home());
 destination.protocol=odh::Ts1TravelProtocol::original_6e598;destination.current_house=23;destination.current_mode=2;
 std::vector<std::int16_t> simi(64);simi[32]=1;simi[9]=0;simi[18]=73;
 odh::RuntimeGlobals globals(simi,odh::Ts1GlobalActivation::saved_new,23);
 odh_host::reset_original_frontend_load_globals(globals,destination,false);
 require(globals.read(32)==0&&globals.read(9)==0&&globals.read(18)==73,
   "actual original preload clears imported Simless without replacing synthetic family0 or unrelated globals");
 require(globals.write(32,1)&&globals.write(9,21),"simulate source load globals before the actual postload boundary");
 odh_host::reset_original_frontend_load_globals(globals,destination,false);
 require(globals.read(32)==0&&globals.read(9)==21,"postload reset preserves whole-family selection as well");
 require(globals.write(32,1),"seed an exact saved native continuation");const auto before=globals.snapshot();
 rejects([&]{odh_host::reset_original_frontend_load_globals(globals,destination,true);},"cold restore cannot invoke frontend reset");
 require(globals.snapshot().words==before.words&&globals.snapshot().selected_sim_is_runtime_id==before.selected_sim_is_runtime_id,
   "refused cold reset preserves all exact continuation values");
 auto legacy=destination;legacy.protocol=odh::Ts1TravelProtocol::legacy_pin;
 rejects([&]{odh_host::reset_original_frontend_load_globals(globals,legacy,false);},"legacy frontend is not silently changed");
 destination.family_activated=true;
 rejects([&]{odh_host::reset_original_frontend_load_globals(globals,destination,false);},"ordinary active gameplay is not a fresh load phase");
 require(globals.read(32)==1,"refused phases do not normalize historical Simless state");
}
int main(){try{
 original_frontend_global_phase();
 auto home=travel_test::managed_home();odh::Family f;f.house_number=7;f.family_number=12;f.budget=7000;f.member_guids={123456};std::vector<odh::FamilyEntry> source{{21,f}};
 const auto directory=std::filesystem::temp_directory_path()/("odh-travel-checkpoint-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(directory);setenv("XDG_DATA_HOME",directory.c_str(),1);setenv("XDG_CONFIG_HOME",directory.c_str(),1);
 const auto identity=home.family_lease->neighborhood_identity;home.family_lease.reset();
 auto journal=odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,7,21,home,[](const auto&,const auto& bytes){(void)odh::decode_session_bundle(bytes);});
 {
    const odh_host::LinuxNeighborhoodStorage third(3),fourth(4);
    const auto validate=[](const auto&,const auto& bytes){(void)odh::decode_session_bundle(bytes);};
    auto first=odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,7,21,home,validate,third);
    auto second=odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,7,21,home,validate,fourth);
    require(first.checkpoint_path()!=second.checkpoint_path() && first.checkpoint_path()!=journal.checkpoint_path() &&
        first.checkpoint_path()->filename()==second.checkpoint_path()->filename(),
        "identical households and captures retain separate archive directories and unchanged archive digest");
    auto traveled=home;traveled.family_lease=first.lease();
    traveled=travel_test::pending_travel_home(traveled,travel_test::travel_state(traveled));
    first.publish(traveled);
    second.verify_current();journal.verify_current();
    require(first.checkpoint_path()->filename()!=second.checkpoint_path()->filename() &&
        std::filesystem::exists(*second.checkpoint_path()),"travel in town three preserves town four and legacy town one");
    rejects([&]{odh_host::adopt_private_checkpoint_namespace(directory/"home.odhs",directory/"UserData4"/"home.odhs",identity,{},fourth);},
        "an ambiguous legacy private checkpoint cannot be adopted outside town one");
    require(!std::filesystem::exists(directory/"UserData4"),"rejected private adoption creates no files");
 }
 odh::FamilySpellsSnapshot spells;spells.family_chunk_id=home.host.family_chunk_id;spells.family_number=home.host.family_number;
 spells.loaded_presence=true;spells.extension_dword=0x1234abcd;spells.words={-32768,-9,0,1,2,3};home.family_spells=spells;home.type_attributes=odh::TypeAttributesSnapshot{};home.type_attributes->by_guid[123]={4,-2,9};
 home.family_lease=journal.lease();home=travel_test::pending_travel_home(home,travel_test::travel_state(home));journal.publish(home);const auto original=odh::encode_session_bundle(home);
 const auto pending_managed=odh_host::reopen_travel_session_checkpoint(journal,original);require(pending_managed.suspended_home==original&&pending_managed.active.travel->pending&&pending_managed.active.runtime_entities.front().vm.status==odh::VmStatus::next_tick,"managed restart recovers exact sealed HOME departure and advanced VM");
 const auto pending_head=odh::sha256(original.data(),original.size());
 rejects([&]{odh_host::authenticate_managed_visited_restore(journal,pending_head,original);},"sealed pending HOME is not an authenticated visited payload");
 auto active=home;active.travel=travel_test::travel_state(home);active.travel->current_house=23;active.host.house_number=23;active.host.family_budget=6990;active.neighbor_inventory->inventory_by_id[41]={{2,7,20},{2,8,30},{6,123,1}};
 active.family_spells->words={-4,-8,0,5,6,7};active.type_attributes->by_guid[123][1]=27;
 const auto envelope=odh_host::make_travel_session_envelope(original,active);require(envelope.host.house_number==7&&envelope.host.family_budget==6990,"journal envelope retains residence with actual trip budget");
 const auto encoded=odh::encode_session_bundle(envelope);auto reopened=odh_host::reopen_travel_session_envelope(odh::decode_session_bundle(encoded));require(reopened.suspended_home==original&&reopened.active.host.house_number==23,"fresh decode retains exact suspended home and active visited lot");
 require(envelope.family_spells&&reopened.active.family_spells&&
     envelope.family_spells->words==active.family_spells->words&&reopened.active.family_spells->words==active.family_spells->words,
     "active family spell progress survives outer envelope and fresh TVIS extraction without rewriting THOM");
 require(envelope.type_attributes&&reopened.active.type_attributes&&envelope.type_attributes->by_guid.at(123)[1]==27&&reopened.active.type_attributes->by_guid.at(123)[1]==27,"shared type attributes follow active destination without altering suspended home");
 auto missing_types=active;missing_types.type_attributes.reset();rejects([&]{odh_host::make_travel_session_envelope(original,missing_types);},"visited session cannot drop shared type attributes");
 auto dropped=active;dropped.family_spells.reset();rejects([&]{odh_host::make_travel_session_envelope(original,dropped);},"visited carrier cannot silently drop originating family spells");
 auto changed_source=active;++changed_source.family_spells->extension_dword;
 rejects([&]{odh_host::make_travel_session_envelope(original,changed_source);},"visited carrier cannot change immutable FAMI extension provenance");
 auto spell_mismatch=envelope;spell_mismatch.family_spells->words[0]++;
 rejects([&]{odh::encode_session_bundle(spell_mismatch);},"outer spell carrier must exactly match authoritative active TVIS");
 auto corrupt=envelope;corrupt.world.entities.front().person_data[0]++;rejects([&]{odh::encode_session_bundle(corrupt);},"home simulation mutation rejected");
 corrupt=envelope;corrupt.host.family_budget=5;rejects([&]{odh::encode_session_bundle(corrupt);},"outer budget mismatch rejected");
 corrupt=envelope;corrupt.travel_visited_session=encoded;rejects([&]{odh::encode_session_bundle(corrupt);},"nested envelope rejected before recursion");
 auto path=odh_host::publish_travel_session_checkpoint(journal,original,active);std::ifstream in(path,std::ios::binary);std::vector<std::uint8_t> disk{std::istreambuf_iterator<char>(in),{}};auto durable=odh_host::reopen_travel_session_checkpoint(journal,disk);require(durable.suspended_home==original&&durable.active.host.family_budget==6990,"authenticated durable reopen");
 auto foreign=original;foreign.back()^=1;rejects([&]{odh_host::publish_travel_session_checkpoint(journal,foreign,active);},"home substitution rejected without publication");
 const auto envelope_head=odh::sha256(disk.data(),disk.size());const auto exact_inner=odh::encode_session_bundle(active);
 const auto authorized=odh_host::authenticate_managed_visited_restore(journal,envelope_head,exact_inner);
 require(authorized.suspended_home==original&&odh::encode_session_bundle(authorized.active)==exact_inner,
     "managed visited restore authenticates exact inner bytes through its current residential envelope");
 rejects([&]{journal.validate_restore(exact_inner);},"standalone visited payload is not the journal checkpoint");
 rejects([&]{odh_host::authenticate_managed_visited_restore(journal,envelope_head,disk);},"outer envelope cannot masquerade as its visited payload");
 auto altered_inner=active;altered_inner.host.family_budget=6989;
 rejects([&]{odh_host::authenticate_managed_visited_restore(journal,envelope_head,odh::encode_session_bundle(altered_inner));},"valid but altered visited archive is not the embedded payload");
 auto wrong_head=envelope_head;wrong_head[0]^=1;
 rejects([&]{odh_host::authenticate_managed_visited_restore(journal,wrong_head,exact_inner);},"visited payload cannot substitute an expected envelope head");
 auto stale_journal=journal;active.host.family_budget=6980;odh_host::publish_travel_session_checkpoint(journal,original,active);rejects([&]{odh_host::publish_travel_session_checkpoint(stale_journal,original,active);},"stale journal head rejected");
 rejects([&]{odh_host::authenticate_managed_visited_restore(stale_journal,envelope_head,exact_inner);},"stale journal cannot authenticate an old visited payload");
 rejects([&]{odh_host::authenticate_managed_visited_restore(journal,envelope_head,exact_inner);},"current journal rejects a previously valid visited envelope head");
 auto installed_home=home;installed_home.family_lease.reset();auto origin=travel_test::travel_state(home);origin.occupancy_epoch=0;installed_home=travel_test::pending_travel_home(installed_home,origin);
 const auto installed_bytes=odh::encode_session_bundle(installed_home);auto visited=installed_home;visited.travel->current_house=23;visited.travel->pending.reset();visited.host.house_number=23;visited.host.family_budget=6000;
 const auto private_path=directory/"installed-family.odhs";
 auto source_head=odh_host::publish_private_home_session_checkpoint(private_path,std::nullopt,installed_home,origin);
 auto pending_installed=odh_host::reopen_private_travel_session_checkpoint(private_path,source_head,origin);require(pending_installed.suspended_home==installed_bytes&&pending_installed.active.travel->pending&&pending_installed.active.runtime_entities.front().vm.status==odh::VmStatus::next_tick,"unmanaged restart recovers exact sealed HOME departure after Generic17");
 auto head=odh_host::publish_private_travel_session_checkpoint(private_path,source_head,installed_bytes,visited,origin);
 auto installed=odh_host::reopen_private_travel_session_checkpoint(private_path,head,origin);require(!installed.active.family_lease&&installed.suspended_home==installed_bytes,"unmanaged family reopens without adopting occupancy");
 const auto old_head=head;visited.host.family_budget=5990;head=odh_host::publish_private_travel_session_checkpoint(private_path,head,installed_bytes,visited,origin);
 rejects([&]{odh_host::publish_private_travel_session_checkpoint(private_path,old_head,installed_bytes,visited,origin);},"unmanaged stale expected head rejected");
 rejects([&]{odh_host::publish_private_travel_session_checkpoint(private_path,std::nullopt,installed_bytes,visited,origin);},"unmanaged null head cannot overwrite existing checkpoint");
 auto wrong_origin=origin;wrong_origin.neighborhood_identity[0]^=1;
 rejects([&]{odh_host::reopen_private_travel_session_checkpoint(private_path,head,wrong_origin);},"unmanaged changed installed source hash rejected");
 wrong_origin=origin;wrong_origin.permanent_roster.front().character_fingerprint[0]^=1;
 rejects([&]{odh_host::reopen_private_travel_session_checkpoint(private_path,head,wrong_origin);},"unmanaged changed character resource rejected");
 auto changed_home=installed_home;changed_home.resources.resource_fingerprint[0]^=1;const auto changed_bytes=odh::encode_session_bundle(changed_home);
 rejects([&]{odh_host::publish_private_travel_session_checkpoint(private_path,head,changed_bytes,visited,origin);},"unmanaged original home resource hash substitution rejected");
 require(odh_host::reopen_private_travel_session_checkpoint(private_path,head,origin).active.host.family_budget==5990,"failed unmanaged publication preserves prior valid head and budget");
 auto returned=installed_home;returned.travel.reset();returned.host.family_budget=5990;returned.neighbor_inventory=visited.neighbor_inventory;
 const auto scoped_path=directory/"neighborhood-test"/private_path.filename();
 odh_host::adopt_private_checkpoint_namespace(private_path,scoped_path,head);
 odh_host::adopt_private_checkpoint_namespace(private_path,scoped_path,head); // An identical competing adoption is safe.
 require(odh_host::reopen_private_travel_session_checkpoint(scoped_path,head,origin).suspended_home==installed_bytes,
     "migrated active trip retains its authenticated suspended HOME and can reopen");
 rejects([&]{odh_host::adopt_private_checkpoint_namespace(private_path,scoped_path,old_head);},
     "changed legacy head cannot be adopted");
 const auto trip_head=head;head=odh_host::publish_private_home_session_checkpoint(private_path,head,returned,origin);
 rejects([&]{odh_host::adopt_private_checkpoint_namespace(private_path,scoped_path,head);},
     "a new legacy head cannot replace a different accepted namespaced head");
 require(odh_host::reopen_private_travel_session_checkpoint(scoped_path,trip_head,origin).active.travel.has_value(),
     "a rejected legacy adoption preserves the accepted trip");
 rejects([&]{odh_host::publish_private_home_session_checkpoint(private_path,trip_head,returned,origin);},"duplicate return expected head cannot repeat publication");
 const auto returned_bundle=odh::decode_session_bundle(odh::encode_session_archive(odh_host::read_session_archive(private_path)));require(!returned_bundle.travel&&returned_bundle.host.house_number==7&&returned_bundle.host.family_budget==5990,"unmanaged return uses same guarded home archive without occupancy changes");
 auto bad_epoch=installed_home;bad_epoch.travel->occupancy_epoch=1;rejects([&]{odh::encode_session_bundle(bad_epoch);},"unmanaged travel cannot impersonate occupancy epoch");
 auto magic_home=installed_home;magic_home.host.neighborhood_mode=7;magic_home.travel->home_mode=7;magic_home.travel->current_mode=7;auto magic_active=magic_home;magic_active.host.house_number=23;magic_active.host.neighborhood_mode=2;magic_active.travel->current_house=23;magic_active.travel->current_mode=2;magic_active.travel->pending.reset();
 const auto magic=odh_host::make_travel_session_envelope(odh::encode_session_bundle(magic_home),magic_active);require(magic.host.neighborhood_mode==7,"magic permanent residence keeps original home mode");
 // A visited runtime owns a genuine subset, never the permanent FAMI roster.
 auto whole_home=travel_test::managed_home();auto single_visited=whole_home;whole_home.family_lease.reset();single_visited.family_lease.reset();
 odh::World two_world;require(two_world.restore_snapshot(whole_home.world)&&two_world.create_saved_entity(2,odh::EntityKind::avatar,123457,{},std::vector<std::int16_t>(80),std::vector<std::int16_t>(16),{},{},0),"create second permanent resident");
 require(two_world.set_person_data(2,31,42)&&two_world.set_person_data(2,61,21),"bind second permanent resident identity");
 auto second_member=whole_home.household_roster.front();second_member.id=2;second_member.generation=two_world.find(2)->generation;second_member.guid=123457;second_member.neighbor_id=42;second_member.character_asset="PlayerData/Characters/User2.iff";second_member.character_fingerprint.fill(0x32);whole_home.household_roster.push_back(second_member);whole_home.world=two_world.snapshot();
 odh::Vm second_vm(odh::RoutineSet{});odh::EntityThreadControllers second_controllers(two_world,2);require(second_controllers.bind(second_vm),"bind second permanent resident controller");odh::SessionRuntimeEntity second_runtime;second_runtime.id=2;second_runtime.vm=second_vm.snapshot();second_runtime.controllers=second_controllers.snapshot();whole_home.runtime_entities.push_back(second_runtime);
 odh::EntityVmScheduler second_scheduler(4);require(second_scheduler.add(2,second_vm,[](auto){return true;},[]{return true;},[]{return true;}),"register second permanent resident");whole_home.scheduler.entities.push_back(second_scheduler.snapshot().entities.front());whole_home.simulation->avatars.push_back({2,whole_home.simulation->avatars.front().decay});
 auto subset=travel_test::travel_state(home);subset.occupancy_epoch=0;subset.permanent_roster.push_back({second_member.guid,second_member.neighbor_id,second_member.object_definition_id,second_member.character_asset,second_member.character_fingerprint});subset.travelers={subset.permanent_roster.front()};subset.current_house=23;
 single_visited.host.house_number=23;single_visited.travel=subset;
 const auto single_envelope=odh_host::make_travel_session_envelope(odh::encode_session_bundle(whole_home),single_visited);const auto single_reopen=odh_host::reopen_travel_session_envelope(odh::decode_session_bundle(odh::encode_session_bundle(single_envelope)));
 require(single_envelope.household_roster.size()==2&&single_reopen.active.household_roster.size()==1&&single_reopen.active.runtime_entities.size()==1&&single_reopen.active.travel->permanent_roster.size()==2,"single traveler visited subset preserves full permanent home roster without phantom runtime resident");
 auto wrong_subset=single_visited;wrong_subset.household_roster=whole_home.household_roster;rejects([&]{odh::validate_ts1_travel_bundle(wrong_subset);},"visited roster cannot expand to nontraveling permanent family");
 std::cout<<"Linux travel checkpoint tests passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
