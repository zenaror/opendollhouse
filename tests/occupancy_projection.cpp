#include "odh/occupancy_projection.hpp"
#include "odh/world_snapshot_codec.hpp"
#include "odh/relationship_snapshot_codec.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void rejects(F&& operation,const char* message){
    try{operation();}catch(const std::exception&){return;}throw std::runtime_error(message);
}
odh::SavedWorldEntity record(std::uint16_t id,std::uint32_t guid,bool person=false){
    odh::SavedWorldEntity r{};r.saved.object_id=id;r.saved.type_id=id;r.saved.guid=guid;
    auto& p=r.saved.prefix;p.x=id*16;p.y=id*32;p.level=1;p.object_data[1]=3;p.object_data[3]=-1;
    p.object_data[11]=static_cast<std::int16_t>(id);p.object_data[67]=124;p.object_data[4]=256;
    p.attributes={17,2,9};p.temps[2]=2;p.extras={11,22,33,44,55};
    p.consumed_bits=20;p.remaining_bits=24;p.block_offset=id*100;
    auto& c=r.common;c.block_offset=p.block_offset;c.start_bits=20;c.consumed_bits=30;
    c.remaining_bits=14;c.relationship_flag=-1;
    auto& t=r.tail;t.block_offset=p.block_offset;t.start_bits=30;t.consumed_bits=40;
    t.remaining_bits=4;t.remaining_data={0xa0};t.unknown_int=-33;t.unknown_int_present=true;
    r.definition={138,guid,static_cast<std::uint16_t>(person?2:4),0,0,false,156,0,0,0,16,0};
    r.serialized_object_type=person?2:4;
    if(person){t.person.emplace();t.person->person_data.resize(80);t.person->motive_data_bits.fill(0x42c80000U);}
    return r;
}
std::vector<odh::SavedWorldEntity> records(bool held=false){
    auto chair=record(1,101),old_a=record(2,102,true),old_b=record(3,103,true),visitor=record(4,104,true),lamp=record(5,105);
    chair.common.slot_count=2;chair.common.linked_slot=odh::ObjmSavedSlot{0,2};chair.common.slots={{0,2}};
    old_a.saved.prefix.object_data[2]=1;old_a.saved.prefix.object_data[3]=0;
    chair.common.relationship_count=2;chair.common.relationships={{1,2,1,{42}},{1,4,1,{71}}};
    chair.common.stack_count=1;chair.common.stack={{2,4096,3,1,1,{2},{3},0,4}};
    auto& p=*visitor.tail.person;p.active_interaction.caller_id=4;p.active_interaction.target_id=2;
    p.last_interaction.caller_id=3;p.last_interaction.target_id=4;
    p.interaction_queue_count=2;p.interaction_queue={p.active_interaction,{}};
    p.interaction_queue.back().caller_id=4;p.interaction_queue.back().target_id=5;
    p.object_use_count=2;p.object_uses={{2,0,0},{5,0,0}};
    if(held){old_b.common.slot_count=2;old_b.common.linked_slot=odh::ObjmSavedSlot{0,0};
        old_b.common.slots={{0,5}};lamp.saved.prefix.object_data[2]=3;lamp.saved.prefix.object_data[3]=0;}
    return {lamp,old_b,visitor,chair,old_a};
}
std::unique_ptr<odh::SavedWorld> fixture(bool held=false){
    const auto data=records(held);std::vector<odh::DefinitionCandidate> definitions;
    for(const auto& value:data)definitions.push_back({"fixture","objects.iff",value.saved.object_id,value.definition});
    auto world=odh::make_saved_world(data,odh::DefinitionIndex(definitions));
    auto snapshot=world->relationships->snapshot();
    const auto chair=std::find_if(snapshot.sources.begin(),snapshot.sources.end(),[](const auto& item){return item.id==1;});
    chair->pairs.push_back({65534,{88}});++snapshot.pair_count;++snapshot.value_count;
    snapshot.sources.push_back({odh::RelationshipMatrix::neighbor,2,0,{{3,{99}},{65534,{77}}}});
    snapshot.pair_count+=2;snapshot.value_count+=2;
    auto relationships=std::make_unique<odh::RelationshipState>();
    require(relationships->restore_snapshot(snapshot),"install valid full-width and neighbor relation fixture");
    world->relationships=std::move(relationships);return world;
}
odh::NeighborhoodFamilySource source_families(){
    odh::NeighborhoodFamilySource source;source.identity.fill(1);
    odh::Family original;original.family_number=100;original.house_number=7;original.budget=1000;
    original.member_guids={102,103};
    odh::Family incoming;incoming.family_number=101;incoming.budget=5000;incoming.member_guids={201};
    odh::Family visitor;visitor.family_number=102;visitor.house_number=8;visitor.budget=1000;visitor.member_guids={104};
    source.families={{10,original},{11,incoming},{12,visitor}};return source;
}
odh::SessionFamilyLease lease(const odh::NeighborhoodFamilySource& source,const odh::FamilyOverlay& overlay,std::uint16_t id){
    const auto replay=odh::replay_family_overlay(source.families,source.identity,overlay);
    const auto state=std::find_if(replay.states.begin(),replay.states.end(),[&](const auto& item){return item.family_chunk_id==id;});
    require(state!=replay.states.end(),"fixture family is present");
    return {source.identity,state->family_identity,state->occupancy_epoch,id};
}
const odh::SavedWorldEntity& find_record(const odh::SavedWorld& world,std::uint16_t id){
    const auto found=std::find_if(world.records.begin(),world.records.end(),[&](const auto& r){return r.saved.object_id==id;});
    require(found!=world.records.end(),"effective record exists");return *found;
}
}
int main(){try{
    const auto source=source_families();std::array<std::uint8_t,32> house_sha{};house_sha.fill(2);
    const auto moved=odh::plan_family_move_out_overlay(source.families,source.identity,nullptr,10);
    const auto bought=odh::plan_family_managed_move_in_overlay(source.families,source.identity,&moved,11,7,500);
    const auto destination=lease(source,bought,11);
    const auto plan=odh::prepare_installed_occupancy_retirement(source,bought,destination,7,house_sha);
    require(plan&&plan->expired_guids()==std::vector<std::uint32_t>({102,103}),"only source-house family is authorized");
    auto world=fixture();const auto before_world=odh::encode_world_snapshot(world->world->snapshot());
    const auto before_relations=odh::encode_relationship_snapshot(world->relationships->snapshot());
    const auto projection=odh::project_installed_occupancy_retirement(*world,*plan,house_sha);
    require(projection.retired_ids==std::vector<std::int16_t>({2,3})&&projection.retired.size()==2&&
        projection.invalidated_saved_execution,"both expired avatars retire with explicit execution invalidation");
    const auto& candidate=*projection.candidate;
    require(!candidate.world->find(2)&&!candidate.world->find(3)&&candidate.world->find(4),"visitor survives exact-GUID retirement");
    require(candidate.world->find(1)->slots[0]==0&&find_record(candidate,1).common.slots[0].object_id==0&&
        find_record(candidate,1).common.linked_slot->object_id==0,"chair and effective linked slot no longer contain expired Sim");
    require(candidate.world->find(1)->attributes==world->world->find(1)->attributes&&
        candidate.world->find(5)->position==world->world->find(5)->position,"furniture banks and positions survive");
    require(find_record(candidate,1).saved.prefix.temps==find_record(*world,1).saved.prefix.temps&&
        find_record(candidate,1).tail.remaining_data==find_record(*world,1).tail.remaining_data&&
        find_record(candidate,1).common.stack.front().parameters==std::vector<std::int16_t>({2})&&
        find_record(candidate,1).common.stack.front().stack_object_id==0,"opaque/raw numeric values retained while typed dead execution links are inert");
    const auto& visitor=*find_record(candidate,4).tail.person;
    require(visitor.active_interaction.target_id==0&&visitor.last_interaction.caller_id==0&&
        visitor.interaction_queue.size()==1&&visitor.interaction_queue.front().target_id==5&&
        visitor.object_uses.size()==1&&visitor.object_uses.front().target_id==5,"typed visitor queue and object-use links are pruned precisely");
    const auto relationships=candidate.relationships->snapshot();
    bool saw_neighbor=false,saw_wide=false;
    for(const auto& row:relationships.sources){
        if(row.matrix==odh::RelationshipMatrix::neighbor){saw_neighbor=true;
            require(row.id==2&&row.pairs.size()==2&&row.pairs.front().target==3,"neighbor family identity relationships preserved");continue;}
        require(row.id!=2&&row.id!=3,"expired OBJECT source removed");
        for(const auto& pair:row.pairs){require(pair.target!=2&&pair.target!=3,"inbound OBJECT target removed");
            if(pair.target==65534)saw_wide=true;}
    }
    require(saw_neighbor&&saw_wide,"neighbor and full-width unrelated targets retained");
    require(!candidate.world->entity_id_available(2)&&!candidate.world->entity_id_available(3)&&
        candidate.world->entity_id_available(6),"expired IDs cannot be reused");
    require(candidate.world->snapshot().generation_sequence==world->world->snapshot().generation_sequence,
        "retirement does not rewind generations");
    require(projection.source_records.size()==5&&world->records.size()==5&&
        odh::encode_world_snapshot(world->world->snapshot())==before_world&&
        odh::encode_relationship_snapshot(world->relationships->snapshot())==before_relations,
        "successful projection leaves caller World relationships and raw records unchanged");
    require(projection.source_records[3].common.stack.front().stack_object_id==2,"source record provenance retains original typed reference");
    auto wrong_sha=house_sha;wrong_sha[0]^=1;
    rejects([&]{(void)odh::project_installed_occupancy_retirement(*world,*plan,wrong_sha);},"stale house bytes reject");
    auto wrong_lease=destination;++wrong_lease.occupancy_epoch;
    rejects([&]{(void)odh::prepare_installed_occupancy_retirement(source,bought,wrong_lease,7,house_sha);},"stale occupancy epoch rejects");
    rejects([&]{(void)odh::prepare_installed_occupancy_retirement(source,bought,destination,8,house_sha);},"cross-house plan rejects");
    auto changed_source=source;changed_source.identity[0]^=1;
    rejects([&]{(void)odh::prepare_installed_occupancy_retirement(changed_source,bought,destination,7,house_sha);},"cross-neighborhood authority rejects");
    const auto differently_bound=odh::prepare_installed_occupancy_retirement(source,bought,destination,7,wrong_sha);
    require(differently_bound&&differently_bound->plan_identity()!=plan->plan_identity(),"plan digest binds exact house bytes");
    // A later visit by another family does not broaden installed-lot deletion authority.
    auto history=odh::plan_family_managed_move_out_overlay(source.families,source.identity,&bought,11);
    history=odh::plan_family_managed_move_out_overlay(source.families,source.identity,&history,12);
    history=odh::plan_family_managed_move_in_overlay(source.families,source.identity,&history,12,7,0);
    history=odh::plan_family_managed_move_out_overlay(source.families,source.identity,&history,12);
    history=odh::plan_family_managed_move_in_overlay(source.families,source.identity,&history,11,7,0);
    const auto later=odh::prepare_installed_occupancy_retirement(source,history,lease(source,history,11),7,house_sha);
    require(later&&later->expired_guids()==plan->expired_guids()&&later->plan_identity()!=plan->plan_identity(),
        "later rosters never authorize visitor deletion and changed overlay changes provenance");
    auto returning=odh::plan_family_managed_move_out_overlay(source.families,source.identity,&bought,11);
    returning=odh::plan_family_managed_move_in_overlay(source.families,source.identity,&returning,10,7,0);
    const auto return_plan=odh::prepare_installed_occupancy_retirement(source,returning,lease(source,returning,10),7,house_sha);
    require(return_plan&&return_plan->destination_lease().occupancy_epoch==2,
        "original family return has a fresh occupancy lease");
    const auto returned=odh::project_installed_occupancy_retirement(*world,*return_plan,house_sha);
    require(returned.retired_ids==projection.retired_ids&&returned.candidate->world->find(4),
        "original family return retires its installed epoch-one actors without deleting visitors");
    odh::SavedWorldLimits tiny;tiny.retained_words=1;
    rejects([&]{(void)odh::project_installed_occupancy_retirement(*world,*plan,house_sha,{},tiny);},
        "provenance word bounds reject before candidate copies");
    require(odh::encode_world_snapshot(world->world->snapshot())==before_world,
        "bounded failure leaves source unchanged");
    auto held=fixture(true);const auto held_before=odh::encode_world_snapshot(held->world->snapshot());
    rejects([&]{(void)odh::project_installed_occupancy_retirement(*held,*plan,house_sha);},"held object requires explicit drop rule");
    rejects([&]{(void)odh::project_installed_occupancy_retirement(*held,*plan,house_sha,
        [](auto&,const auto&,const auto&){return false;});},"failed drop callback rejects whole projection");
    require(odh::encode_world_snapshot(held->world->snapshot())==held_before,"failed projection never detaches caller held object");
    const auto dropped=odh::project_installed_occupancy_retirement(*held,*plan,house_sha,
        [](odh::World& candidate,const odh::Entity& previous,const auto& children){
            if(!previous.position)return false;
            for(auto id:children)if(!candidate.set_position(id,*previous.position))return false;
            return true;
        });
    require(dropped.candidate->world->find(5)&&!dropped.candidate->world->find(5)->attachment&&
        dropped.candidate->world->find(5)->position==held->world->find(3)->position,"explicit drop keeps held object and places it");
    std::cout<<"Occupancy projection checks passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
