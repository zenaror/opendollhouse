// SPDX-License-Identifier: MPL-2.0
#include "property_release_session.hpp"
#include "residential_property_scene.hpp"
#include "odh/property_valuation.hpp"
#include "odh/sha256.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
namespace {
using namespace odh;using namespace odh_host;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class F>void rejects(F f){try{f();}catch(const FormatError&){return;}throw std::runtime_error("invalid release accepted");}
DefinitionCandidate definition(std::uint32_t guid,std::uint16_t type){ObjectDefinition d{};d.version=138;d.guid=guid;d.object_type=type;d.build_mode_type=0;return {"fixture.far","fixture.iff",128,d};}
struct Fixture {
    World world;RelationshipState relations;DefinitionIndex definitions{{definition(101,2),definition(102,4),definition(103,4)}};
    RuntimeGlobals globals{std::vector<std::int16_t>(64),Ts1GlobalActivation::saved_new,7};
    Vm avatar{RoutineSet{}};std::unique_ptr<EntityThreadControllers> controllers;EntityVmScheduler scheduler{8};
    SessionBundle bundle;NeighborhoodFamilySource source;FamilyOverlay overlay;CommunityLotIdentity identity;
    std::vector<std::uint8_t> bytes;std::vector<std::int16_t> erased;
    bool active=true,no_progress=false,unknown_cursor=false,invalid_export=false;
    std::int32_t cash=1000;unsigned reads=0;
    Fixture(){
        check(world.create_saved_entity(1,EntityKind::avatar,101,{},std::vector<std::int16_t>(80),std::vector<std::int16_t>(16),{},{},0)&&world.set_person_data(1,31,41)&&world.set_person_data(1,61,21),"actual household avatar");
        check(world.create_runtime_object(2,102,{},std::vector<EntryPoint>(27))&&world.create_runtime_object(3,103,{},std::vector<EntryPoint>(27)),"actual quiet object constructors");
        for(std::int16_t id:{1,2,3})check(world.set_position(id,{24+16*(id-1),24,1,0})&&world.declare_object_word(id,15,0)&&world.declare_object_word(id,41,id==2?150:0)&&world.declare_object_word(id,43,id==3?16:0)&&world.declare_definition_word(id,9,id==1?2:4),"known actual predicate/value fields");
        controllers=std::make_unique<EntityThreadControllers>(world,1);check(controllers->bind(avatar),"actual resident controllers");
        check(scheduler.add(1,avatar,[](auto){return true;},[]{return true;},[]{return true;}),"actual resident scheduler");
        check(globals.select_avatar(world,{1,world.find(1)->generation}),"actual selected resident bank");
        bundle.generic_execution=true;bundle.resources.language_code=1;bundle.resources.expansion_mask=127;bundle.resources.resource_fingerprint.fill(0x71);
        bundle.host.house_number=7;bundle.host.neighborhood_mode=1;bundle.host.family_chunk_id=21;bundle.host.family_number=12;
        bundle.host.resident_guid=101;bundle.host.resident_neighbor_id=41;bundle.host.runtime_avatar_id=1;bundle.host.family_budget=cash;
        SessionHouseholdMember member;member.id=1;member.generation=world.find(1)->generation;member.guid=101;member.neighbor_id=41;
        member.object_definition_id=128;member.character_asset="PlayerData/Characters/User1.iff";member.character_fingerprint.fill(0x31);
        bundle.household_roster={member};bundle.world=world.snapshot();bundle.relationships=relations.snapshot();
        bundle.runtime_entities={{1,avatar.snapshot(),controllers->snapshot()}};bundle.scheduler=scheduler.snapshot();
        bundle.object_history=SessionObjectHistory{true,{},{}};bundle.runtime_globals=globals.snapshot();
        SessionSimulationState simulation;simulation.clock=Ts1Clock{}.snapshot();simulation.avatars.push_back({1,{}});bundle.simulation=simulation;
        source.identity[0]=0x55;Family family;family.family_number=12;family.budget=2000;family.member_guids={101};source.families={{21,family}};
        overlay=plan_family_managed_move_in_overlay(source.families,source.identity,nullptr,21,7,1000);
        const auto state=replay_family_overlay(source.families,source.identity,overlay).states.front();
        bundle.family_lease=SessionFamilyLease{source.identity,state.family_identity,1,21};
        bytes=encode_session_bundle(bundle);const auto digest=sha256(bytes.data(),bytes.size());
        overlay=plan_family_checkpoint_overlay(source.families,source.identity,&overlay,21,7,1,cash,digest);
        identity.neighborhood=1;identity.mode=1;identity.house=7;identity.neighborhood_sha256=source.identity;identity.lot_sha256[0]=0x66;
    }
    PropertyReleaseRuntime runtime(){
        PropertyReleaseRuntime r{world,definitions};r.profile=PropertyReleaseTraversalProfile::pinned_vm_id_order;r.source_still_matches=[&]{return active;};
        const auto at=[&](std::int16_t after){const auto next=world.next_entity(after);if(!next)return PropertyReleaseLink{PropertyReleaseLinkStatus::end,{}};return PropertyReleaseLink{PropertyReleaseLinkStatus::member,{*next,world.find(*next)->generation}};};
        r.first=[&,at]{++reads;return unknown_cursor?PropertyReleaseLink{}:at(0);};r.next=[at](auto ref){return at(ref.id);};
        r.delete_entity=[&](auto ref,std::string&){if(no_progress)return true;if(ref.id==1){check(scheduler.remove(1),"actual scheduler retirement");globals.clear_selection();}check(world.remove(ref.id),"actual World retirement");erased.push_back(ref.id);return true;};
        r.current_cash=[&]{return cash;};
        r.valuation=[&]{SavedArchitecture a;a.width=a.height=8;for(auto& level:a.levels){level.floors.resize(64);level.walls.resize(64);level.flags=std::vector<std::uint8_t>(64,0);}a.pools=std::vector<std::uint8_t>(64,255);PropertyArchitecturePrices prices;std::vector<SavedMultitileGroup> groups;for(const auto& e:world.snapshot().entities)groups.push_back({e.id,{}, {e.id}});return inspect_property_valuation({world,definitions,groups,true,a,prices,1000,0x3f333333,PropertyValuationProfile::comparison_pc24});};
        r.export_objects=[&]{check(globals.write(32,1),"fixture host activates its actorless bank owner");ActorlessObjectTradeState objects;objects.resources=bundle.resources;objects.world=world.snapshot();objects.relationships=relations.snapshot();objects.scheduler=scheduler.snapshot();objects.globals=globals.snapshot();objects.history.complete=true;objects.history.reusable_entity_ids=true;
            for(auto id:erased)if(id==1)objects.history.retired_household_avatars.push_back({bundle.household_roster.front(),*bundle.family_lease,sha256(bytes.data(),bytes.size())});
            objects.history.reserved_entity_ids=erased;std::sort(objects.history.reserved_entity_ids.begin(),objects.history.reserved_entity_ids.end());for(auto id:erased)if(id!=1){const auto& old=bundle.world.entities.at(static_cast<std::size_t>(id-1));objects.removed_original_objects.push_back({id,old.generation,old.definition->guid});}if(invalid_export)objects.world.entities.clear();return objects;};
        r.export_architecture=[] {ArchitectureSnapshot a;a.prepared.width=a.prepared.height=8;a.prepared.buildable.assign(64,1);for(auto& level:a.prepared.levels){level.floors.resize(64);level.walls.resize(64);}return a;};
        r.validate_reopen=[](const auto& doc){check(doc.objects.has_value(),"typed object document");const auto copy=decode_community_lot_document(encode_community_lot_document(doc));World restored;check(restored.restore_snapshot(copy.objects->world),"fresh World and actorless codecs reopen");};return r;
    }
};
}
int main(){try{
    Fixture f;const auto original=f.bytes;const auto prepared=prepare_property_release_session(f.source,f.overlay,21,f.bytes,f.bundle.resources,f.identity,f.runtime());const auto replay=replay_family_overlay(f.source.families,f.source.identity,prepared.replacement());
    check(f.erased==std::vector<std::int16_t>{1,2}&&f.reads>=4&&f.world.find(3)&&!f.world.find(1)&&!f.world.find(2),"retirement restarts HEAD and preserves retained object");
    check(prepared.release().policy==2&&prepared.refund()==1150&&prepared.release().vacant_purchase_value==1000&&prepared.source_cash()==1000&&replay.states.front().budget==2150&&replay.states.front().house_number==0&&replay.properties.size()==1,"before refund and after price are independent in one release plan");
    check(f.bytes==original&&prepared.document().objects->world.entities.size()==1,"private effects preserve immutable family source");
    const auto proof=prepare_residential_property_retirement(f.source,f.overlay,21,f.bytes,prepared.document(),f.definitions);
    check(proof.matches(*prepared.document().objects)&&proof.retired_original_avatar(f.bundle.world.entities.front()),
        "retirement proof derives original avatar identity from exact occupied source");
    const auto reopened_proof=restore_residential_property_retirement(f.source,prepared.replacement(),prepared.release(),f.bytes,
        prepared.document(),f.definitions);
    check(reopened_proof.matches(*prepared.document().objects),"committed lot head derives same historical retirement proof");
    {auto altered=prepared.release();++altered.vacant_purchase_value;
        rejects([&]{restore_residential_property_retirement(f.source,prepared.replacement(),altered,f.bytes,prepared.document(),f.definitions);});}
    {
        auto next=plan_family_managed_move_in_overlay(f.source.families,f.source.identity,&prepared.replacement(),21,
            prepared.release().house,1000);
        auto next_ticket=prepared.release().source_checkpoint_sha256;next_ticket[0]^=0x31;
        next=plan_family_checkpoint_overlay(f.source.families,f.source.identity,&next,21,prepared.release().house,2,1150,next_ticket,1000);
        auto successor=prepared.release();successor.assignment_index=static_cast<std::uint32_t>(next.assignments.size());
        successor.occupancy_epoch=2;successor.source_checkpoint_sha256=next_ticket;
        successor.expected_revision=1;successor.expected_head=prepared.release().head;successor.head[0]^=0x71;
        successor.cash_after_cleanup=1150;
        next=plan_family_property_release_overlay(f.source.families,f.source.identity,next,21,successor);
        rejects([&]{restore_residential_property_retirement(f.source,next,prepared.release(),f.bytes,prepared.document(),f.definitions);});
    }
    {auto changed=*prepared.document().objects;changed.random_seed=99;check(!proof.matches(changed),"proof cannot authorize another object state");}
    {auto wrong=f.bytes;wrong.back()^=1;rejects([&]{prepare_residential_property_retirement(f.source,f.overlay,21,wrong,prepared.document(),f.definitions);});}
    {auto release=prepared.release();release.head[0]^=1;rejects([&]{restore_residential_property_retirement(f.source,prepared.replacement(),release,f.bytes,prepared.document(),f.definitions);});}
    {auto document=prepared.document();document.objects->world.entities.push_back(f.bundle.world.entities.front());
        rejects([&]{prepare_residential_property_retirement(f.source,f.overlay,21,f.bytes,document,f.definitions);});}
    {auto document=prepared.document();document.objects->history.retired_npc_avatars.push_back({4,100,101,41,"fixture.far","fixture.iff",128,{}});
        rejects([&]{prepare_residential_property_retirement(f.source,f.overlay,21,f.bytes,document,f.definitions);});}
    {Fixture x;auto rt=x.runtime();rt.profile=PropertyReleaseTraversalProfile::unknown;rejects([&]{prepare_property_release_session(x.source,x.overlay,21,x.bytes,x.bundle.resources,x.identity,std::move(rt));});check(x.erased.empty(),"unknown traversal profile fails before effects");}
    {Fixture x;x.no_progress=true;rejects([&]{prepare_property_release_session(x.source,x.overlay,21,x.bytes,x.bundle.resources,x.identity,x.runtime());});}
    {Fixture x;x.unknown_cursor=true;rejects([&]{prepare_property_release_session(x.source,x.overlay,21,x.bytes,x.bundle.resources,x.identity,x.runtime());});check(x.erased.empty(),"unknown cursor fails before effects");}
    {Fixture x;rejects([&]{prepare_property_release_session(x.source,x.overlay,21,x.bytes,x.bundle.resources,x.identity,x.runtime(),1);});check(x.erased.empty(),"bounded preflight has no effects");}
    {Fixture x;x.invalid_export=true;rejects([&]{prepare_property_release_session(x.source,x.overlay,21,x.bytes,x.bundle.resources,x.identity,x.runtime());});}
    {Fixture x;x.active=false;rejects([&]{prepare_property_release_session(x.source,x.overlay,21,x.bytes,x.bundle.resources,x.identity,x.runtime());});check(x.erased.empty(),"expired owner fails before effects");}
    std::cout<<"property release head traversal, valuations and private failure checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
