// SPDX-License-Identifier: MPL-2.0
#include "architecture_edit_session_adapter.hpp"
#include "linux_session_archive_store.hpp"
#include "odh/world_snapshot_codec.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
using namespace odh;using namespace odh_host;
namespace {
void require(bool b,const std::string&s){if(!b)throw std::runtime_error(s);}
PreparedArchitecture prepared(){PreparedArchitecture p;p.width=p.height=4;p.buildable.assign(16,1);for(auto&l:p.levels){l.floors.assign(16,1);l.walls.resize(16);}return p;}
SessionBundle source(){
 World w;require(w.create_saved_entity(1,EntityKind::avatar,123456,{},std::vector<std::int16_t>(80),std::vector<std::int16_t>(16),{},{},0),"resident");
 require(w.set_person_data(1,31,41)&&w.set_person_data(1,61,21),"family identity");Vm vm(RoutineSet{});EntityThreadControllers c(w,1);require(c.bind(vm),"controllers");
 EntityVmScheduler scheduler(8);require(scheduler.add(1,vm,[](auto){return true;},[]{return true;},[]{return true;}),"scheduler");
 SessionBundle b;b.generic_execution=true;b.resources.language_code=1;b.resources.expansion_mask=127;b.resources.resource_fingerprint.fill(0x5a);
 b.host.neighborhood_mode=1;b.host.house_number=7;b.host.family_chunk_id=21;b.host.family_number=12;b.host.resident_guid=123456;b.host.resident_neighbor_id=41;b.host.runtime_avatar_id=1;b.host.random_seed=1234;b.host.family_budget=8000;
 SessionHouseholdMember m;m.id=1;m.generation=w.find(1)->generation;m.guid=123456;m.neighbor_id=41;m.object_definition_id=128;m.character_asset="PlayerData/Characters/User1.iff";m.character_fingerprint.fill(0x31);b.household_roster.push_back(m);
 b.world=w.snapshot();b.relationships=RelationshipState{}.snapshot();b.runtime_entities={{1,vm.snapshot(),c.snapshot()}};b.scheduler=scheduler.snapshot();return b;
}
struct Profile{
 std::filesystem::path path;std::optional<std::string>old;
 Profile(){if(auto*v=std::getenv("XDG_CONFIG_HOME"))old=v;path=std::filesystem::temp_directory_path()/("odh-architecture-command-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));require(std::filesystem::create_directory(path),"isolated profile");require(setenv("XDG_CONFIG_HOME",path.c_str(),1)==0,"isolated config");}
 ~Profile(){if(old)setenv("XDG_CONFIG_HOME",old->c_str(),1);else unsetenv("XDG_CONFIG_HOME");std::error_code e;std::filesystem::remove_all(path,e);}
};
}
int main(){
 auto p=prepared();SavedArchitecture raw;raw.width=raw.height=4;raw.altitude=std::vector<std::array<std::uint8_t,4>>(16);auto rooms=generate_prepared_room_map(p);DefinitionIndex defs({});auto b=source();
 ArchitectureEditValidationContext context;context.world=&b.world;context.prepared=&p;context.terrain=&raw;context.rooms=&rooms;context.definitions=&defs;context.rectangle_clip={0,0,4,4,2};
 context.material=[](auto,std::uint16_t id){return ArchitectureEditMaterial{ArchitectureEditLookup::available,id==1?0:10,true};};
 auto services=make_architecture_edit_services(context);ArchitectureEditCommand floor{ArchitectureEditType::floor_rect,1,1,0,0,1,7,0};
 auto edit=prepare_architecture_edit(p,{floor},services);require(edit.status==ArchitectureEditStatus::ready&&edit.cost==10,"real snapshot validation and immutable terrain factory");
 auto upper=floor;upper.level=2;edit=prepare_architecture_edit(p,{upper},services);require(edit.status==ArchitectureEditStatus::unsupported&&edit.diagnostic.find("Supported")!=std::string::npos,"upper support unknown cannot be treated as empty or free");
 context.object_support=std::vector<std::uint8_t>(16,0);(*context.object_support)[5]=1;services=make_architecture_edit_services(context);edit=prepare_architecture_edit(p,{upper},services);require(edit.status==ArchitectureEditStatus::ready&&edit.cost==10,"real explicit source ObjectSupport admits supported upper edit");
 auto occupied=b.world;Entity e{};e.id=2;e.generation=2;e.kind=EntityKind::object;e.position=EntityPosition{24,24,1,0};e.object_words={{1,0},{13,1},{42,1}};occupied.entities.push_back(e);context.world=&occupied;services=make_architecture_edit_services(context);
 auto wall=SavedWallTile{};require(services.wall_valid(p,1,1,1,wall)==false,"removing required wall rejected by actual entity words");
 wall.segments=1;require(services.wall_valid(p,1,1,1,wall)==false,"rotated required source wall checked, not generic nonzero test");
 wall.segments=2;require(services.wall_valid(p,1,1,1,wall)==true,"source quarter rotation honors actual front requirement");
 require(services.floor_valid(p,1,1,1,0)==false,"existing floor-only object prevents terrain removal");
 occupied.entities.back().object_words.erase(42);require(!services.floor_valid(p,1,1,1,7).has_value(),"unknown object flags are not a legitimate floor rejection");
 (*raw.altitude)[6][1]=1;wall.segments=8;require(services.wall_valid(p,1,1,1,wall)==false,"nonzero walls denied on actual sloping source terrain");(*raw.altitude)[6][1]=0;
 context.world=&b.world;context.object_support.reset();
 Profile profile;std::array<std::uint8_t,32>identity{};identity[0]=0x75;Family family;family.family_number=12;family.budget=9000;family.member_guids={123456};std::vector<FamilyEntry>families{{21,family}};
 auto managed=plan_family_managed_move_in_overlay(families,identity,nullptr,21,7,1000);FamilySessionJournal pending(families,identity,std::nullopt,7,21,managed);pending.commit_move_in();FamilySessionJournal journal(families,identity,managed,7,21);b.family_lease=journal.lease();
 bool reject=false,stale=false;unsigned captures=0,geometry_checks=0,reopens=0;
 ArchitectureEditSessionHooks hooks;hooks.capture_current=[&]{++captures;auto copy=b;if(stale&&captures%2==0)++*copy.host.family_budget;return copy;};
 hooks.validate_candidate=[&](const SessionBundle&candidate,const PreparedArchitecture&geometry,const GeometricRoomMap&map){++geometry_checks;require(candidate.architecture.has_value()&&geometry.width==map.width,"candidate includes canonical geometry and regenerated room map");require(candidate.runtime_entities.size()==b.runtime_entities.size(),"preserve actual runtime owners");if(reject)throw std::runtime_error("native architecture event/route consumer rejected");};
 hooks.validate_reopen=[&](const SessionBundle&candidate,const std::vector<std::uint8_t>&bytes){++reopens;require(encode_session_bundle(candidate)==bytes&&encode_session_bundle(decode_session_bundle(bytes))==bytes,"fresh codec can retain full canonical architecture candidate");};
 ArchitectureResourceCatalog catalog;ArchitectureEditSessionAdapter adapter(journal,catalog,context,hooks);
 auto result=adapter.execute({});require(result.state==ArchitectureEditSessionState::cancelled&&captures==0,"cancel no capture or publication");
 auto stale_support_context=context;stale_support_context.object_support=std::vector<std::uint8_t>(16,1);
 ArchitectureEditSessionAdapter stale_support_adapter(journal,catalog,stale_support_context,hooks);
 result=stale_support_adapter.execute({upper});require(result.state==ArchitectureEditSessionState::unsupported&&!journal.checkpoint_path()&&*b.host.family_budget==8000,"captured unknown support overrides stale constructor grid without debit");
 reject=true;result=adapter.execute({floor});require(result.state==ArchitectureEditSessionState::unsupported&&!journal.checkpoint_path()&&*b.host.family_budget==8000,"geometry consumer failure publishes no debit or candidate");reject=false;
 b.host.family_budget=9;result=adapter.execute({floor});require(result.state==ArchitectureEditSessionState::rejected&&!journal.checkpoint_path(),"insufficient funds cannot publish geometry");b.host.family_budget=8000;
 captures=0;stale=true;result=adapter.execute({floor});require(result.state==ArchitectureEditSessionState::unsupported&&!journal.checkpoint_path(),"changed live state before CAS cannot publish candidate");stale=false;
 // Existing ARCT is newer than the constructor geometry. Preserve its
 // independent edit instead of reverting it through a stale same-size view.
 auto prior=p;prior.levels[0].floors[2]=8;b.architecture=bind_architecture_snapshot(prior,catalog);
 const auto world=encode_world_snapshot(b.world);result=adapter.execute({floor});require(result.state==ArchitectureEditSessionState::published_reload_required,"architecture real journal publish: "+result.diagnostic);
 auto loaded=decode_session_bundle(result.archive);require(*loaded.host.family_budget==7990&&loaded.architecture&&loaded.architecture->prepared.levels[0].floors[5]==7&&loaded.architecture->prepared.levels[0].floors[2]==8,"net source charge and changed floor survive full native archive");
 require(encode_world_snapshot(loaded.world)==world&&encode_world_snapshot(b.world)==world&&*b.host.family_budget==8000,"candidate command never mutates live World or budget");
 require(encode_session_archive(read_session_archive(result.checkpoint))==result.archive&&geometry_checks>=3&&reopens>=2,"actual immutable journal archive equals validated reload bytes");
 {
   Profile isolated;auto linked=source();World owner;
   require(owner.restore_snapshot(linked.world),"linked registry World");
   for(const auto field:{1,7,8,13,29,34,39,40,51,59,63})require(owner.declare_object_word(1,field,field==29?-1:0),"linked source facts");
   require(owner.set_avatar_kill_timeout(1,-1),"source OOW avatar footprint remains present");
   require(owner.define_entry_points(1,std::vector<EntryPoint>(27)),"source OOW avatar entry metadata");
   ObjectPositionRegistryServices registry_services;registry_services.definitions=&defs;
   registry_services.group=[](auto id)->std::optional<std::vector<ObjectPositionIdentity>>{return std::vector<ObjectPositionIdentity>{id};};
   registry_services.routing_slot_count=[](auto)->std::optional<std::size_t>{return 0;};
   registry_services.moved_self=[](auto)->std::optional<bool>{return false;};
   ObjectPositionRegistry registry(owner,p,rooms,registry_services);
   const ObjectPositionIdentity id{1,owner.find(1)->generation};
   require(registry.register_position(id,0,true,[]{return true;})&&registry.finish_initial_load({id},[]{return true;}),"complete initial OOW registry");
   ArchitectureObjectState object_state;object_state.object_support=make_architecture_object_support(4,4);
   for(auto& grid:object_state.occupied_walls)grid.assign(16,0);
   object_state.position_registry=registry.snapshot();
   require(object_state.position_registry->footprints.front().present,"source OOW footprint is represented");
   linked.world=owner.snapshot();linked.architecture_object_state=object_state;
   FamilySessionJournal first(families,identity,std::nullopt,7,21,managed);first.commit_move_in();
   FamilySessionJournal other(families,identity,managed,7,21);linked.family_lease=other.lease();
   auto linked_context=context;linked_context.world=&linked.world;
   linked_context.registry_services=[&](World&,const ObjectPositionRegistrySnapshot&){return registry_services;};
   ArchitectureEditSessionHooks linked_hooks;linked_hooks.capture_current=[&]{return linked;};
   linked_hooks.validate_candidate=[](const auto&,const auto&,const auto&){};
   linked_hooks.validate_reopen=[](const auto& candidate,const auto& bytes){require(encode_session_bundle(candidate)==bytes,"regenerated registry exact archive");};
   ArchitectureEditSessionAdapter linked_adapter(other,catalog,linked_context,linked_hooks);
   ArchitectureEditCommand enclose{ArchitectureEditType::wall_rect,1,1,1,1,1,0,1};
   const auto result=linked_adapter.execute({enclose});require(result.state==ArchitectureEditSessionState::published_reload_required,"room registry regenerates: "+result.diagnostic);
   const auto decoded=decode_session_bundle(result.archive);const auto& updated=*decoded.architecture_object_state->position_registry;
   require(updated.rooms.size()!=object_state.position_registry->rooms.size(),"enclosed room changes actual room count");
   require(updated.footprints.front().present&&updated.footprints.front().room==0&&updated.footprints.front().instance==object_state.position_registry->footprints.front().instance,"OOW instance remains bound to room0 after geometry change");
 }
 std::cout<<"architecture real validation and journal adapter passed\n";
}
