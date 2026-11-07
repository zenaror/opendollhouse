// SPDX-License-Identifier: MPL-2.0
#include "odh/dynamic_object_groups.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
namespace {
using namespace odh;
void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Fixture {
    World world;SessionObjectTradeLedger ledger;ObjectGroupSnapshot groups;
    std::unique_ptr<ObjectValueRegistry> values;std::unique_ptr<DynamicObjectGroups> dynamic;
    std::map<WorldEntityReference,ObjectGroup> constructor_sources;
    unsigned entries=0,changes=0;bool authority=true;
    explicit Fixture(bool authored_post=false,bool distinct_definitions=false){ledger.version=2;ledger.revision=1;SessionObjectValueGroup price;price.group_id=price.revision=1;price.multitile=false;price.initial_price=300;
        price.price_origin={SessionObjectPriceSource::constructed_catalog,100,"fixture","part.iff",128,{},{}};
        ObjectGroup physical;physical.kind=ObjectGroupKind::live_association;physical.base_id=2;
        for(const std::int16_t n:{8,2,4}){
            const auto guid=std::uint32_t(distinct_definitions?100+n:100);
            const std::optional<std::uint32_t> master=distinct_definitions?std::optional<std::uint32_t>{9000}:std::nullopt;
            std::vector<EntryPoint> ep(27);ep[8].action=500;if(authored_post&&n==8)ep[9].action=509;
            need(world.create_runtime_object(n,guid,{},ep,master)&&world.set_position(n,{n==2?80:64,n==4?80:64,1,0}),"actual dynamic source constructor/pose");
            ObjectGroupMember m{n,world.find(n)->generation,std::int16_t(n==2?16:0),std::int16_t(n==4?16:0),0};
            m.origin=ObjectGroupMemberOrigin{ObjectGroupMemberSource::original_import,guid,master,"fixture","part.iff",128};physical.members.push_back(m);
            SessionObjectValueMember v;v.object={n,m.generation,guid,"fixture","part.iff",128,master};v.offset_x=m.offset_x;v.offset_y=m.offset_y;
            v.lead_object=std::uint16_t(n==2?1:0);price.members.push_back(v);
        }
        price.base=ObjectTradeIdentity{2,world.find(2)->generation};ledger.groups={price};groups.groups={physical};
        values=std::make_unique<ObjectValueRegistry>(world,ledger);need(values->bind(),"real shared value registry owner");
        DynamicObjectGroupServices s;s.authority=[this]{return authority;};
        s.source_group=[this](auto p)->std::optional<ObjectGroup>{const auto i=constructor_sources.find({p.id,p.generation});return i==constructor_sources.end()?std::nullopt:std::optional<ObjectGroup>{i->second};};
        s.origin=[this](auto p)->std::optional<ObjectGroupMemberOrigin>{const auto created=constructor_sources.find({p.id,p.generation});
            if(created!=constructor_sources.end())return created->second.members.front().origin;
            for(const auto& g:groups.groups)for(const auto& m:g.members)if(m.id==p.id&&m.generation==p.generation)return m.origin;return {};};
        s.definition_offset=[](auto p)->std::optional<ObjectGroupMember>{return ObjectGroupMember{p.id,p.generation,32,-16,0};};
        s.execute_entry8=[this](auto p,std::int16_t mask){++entries;VmFrame f;f.context={p.id,p.id,0};return world.bind_context_live(f.context)&&world.write(f,3,0,mask);};
        s.publish_direction=[this](auto p,std::uint8_t direction){VmFrame f;f.context={p.id,p.id,0};return world.bind_context_live(f.context)&&world.write(f,3,1,direction);};
        s.publish_native_direction=[this](auto p,std::uint8_t direction){return world.publish_native_object_heading(p,direction);};
        s.changed=[this]{++changes;};dynamic=std::make_unique<DynamicObjectGroups>(world,*values,groups,std::move(s));
    }
    WorldEntityReference ref(std::int16_t n)const{return {n,world.find(n)->generation};}
    ObjectPositionIdentity id(std::int16_t n)const{const auto r=ref(n);return {r.id,r.generation};}
    std::vector<NativeObjectGroupSourceMember> rows()const{return {{ref(8),{2,3,1},{91,92,93}},{ref(2),{3,3,1},{94,95,96}},{ref(4),{2,4,2},{97,98,99}}};}
    void bind(){need(dynamic->bind_original_native_group(id(8),ref(8),rows()),"bind actual supplied original first/second triples and parent order");}
    void new_constructor(std::int16_t n){
        need(world.create_runtime_object(n,100,{},std::vector<EntryPoint>(27)),"actual later constructor");
        const auto r=ref(n);ObjectGroupMember m{n,r.generation,0,0,0};m.origin=ObjectGroupMemberOrigin{ObjectGroupMemberSource::native_created,100,{},"fixture","part.iff",128};
        constructor_sources[r]=ObjectGroup{0,false,n,{m},ObjectGroupKind::live_association};
        SessionObjectValueMember v;v.object={n,r.generation,100,"fixture","part.iff",128,{}};v.lead_object=std::uint16_t{0};
        SessionObjectValueGroup price;price.members={v};price.base=ObjectTradeIdentity{n,r.generation};price.initial_price=400;
        price.price_origin={SessionObjectPriceSource::constructed_catalog,100,"fixture","part.iff",128,{},{}};
        need(values->admit(price),"actual later constructor value admission");
    }
};
void admission_and_rotation(){Fixture f;
    need(f.dynamic->group_head(f.ref(8)).status==NativeObjectGroupLinkStatus::unavailable,"unknown legacy never infers native head from base/order");
    const auto legacy=encode_object_group_snapshot(f.groups);auto bad=f.rows();std::swap(bad[0],bad[1]);
    need(!f.dynamic->bind_original_native_group(f.id(8),f.ref(8),bad)&&encode_object_group_snapshot(f.groups)==legacy&&f.changes==0,
        "failed whole-source admission is atomic before owner publication");
    f.bind();need(f.dynamic->group_head(f.ref(4)).reference==f.ref(8)&&f.dynamic->group_next(f.ref(8)).reference==f.ref(2)&&
        f.dynamic->group_next(f.ref(4)).status==NativeObjectGroupLinkStatus::end&&f.dynamic->native_offset(f.ref(2))->x==94,
        "native source head differs from value BaseObject; imported current fields are not silently recomputed");
    const std::int32_t expected_x[8]={1,0,0,0,-1,0,0,0},expected_y[8]={0,0,1,0,0,0,-1,0};
    for(std::int16_t heading=0;heading<8;++heading){need(f.world.declare_object_word(8,1,heading)&&f.dynamic->recompute_current_offsets(f.ref(8)),"native actual-head recomposition");
        const auto current=f.dynamic->native_offset(f.ref(2)),upper=f.dynamic->native_offset(f.ref(4));
        need(current&&current->x==expected_x[heading]&&current->y==expected_y[heading]&&current->level==0&&upper->level==1,
            "independent cardinal/odd-zero matrix and signed relative level");}
    const auto before=encode_object_group_snapshot(f.groups);need(!f.dynamic->recompute_current_offsets(f.ref(2))&&encode_object_group_snapshot(f.groups)==before,
        "BaseObject cannot replace explicit native head");
    need(f.values->query({2,f.ref(2).generation})->initial_price==300,"native recomposition preserves actual shared price ownership");
    f.authority=false;need(f.dynamic->group_head(f.ref(8)).status==NativeObjectGroupLinkStatus::unavailable&&
        !f.dynamic->recompute_current_offsets(f.ref(8)),"expired authority never publishes native fields");
}
void mutable_unlink_boundaries(){
    Fixture f;f.bind();const auto original=f.rows();const auto future=f.ref(2),head=f.ref(8),tail=f.ref(4);
    need(f.world.remove_retaining_body(future)&&f.values->retire_member({future.id,future.generation})&&
        f.dynamic->erase_after_retire({future.id,future.generation}),"actual future-member unlink");
    need(f.dynamic->group_next(head).reference==tail&&f.dynamic->group_next(future).status==NativeObjectGroupLinkStatus::end&&
        f.dynamic->group_head(future).reference==future,"live predecessor observes splice and removed exact body owns cleared parent/NEXT");
    need(f.world.remove(tail.id)&&f.values->retire_member({tail.id,tail.generation})&&
        f.dynamic->erase_after_retire({tail.id,tail.generation}),"actual hard removal updates same physical owner");
    need(f.dynamic->group_next(head).status==NativeObjectGroupLinkStatus::end&&
        f.dynamic->group_next(tail).status==NativeObjectGroupLinkStatus::unavailable,
        "hard removed cursor remains unavailable while surviving chain owns actual END");
    const auto& bad=original; // Retired source identities cannot be rebound through numeric IDs.
    need(!f.dynamic->bind_original_native_group(f.id(8),head,bad),"retired source admission rejected");
}
void mutations_and_lifetime(){Fixture f;f.bind();need(f.world.declare_object_word(8,1,2)&&f.dynamic->recompute_current_offsets(f.ref(8)),"east-oriented native source fields");
    const auto old_head=f.ref(8);const auto authored=f.groups.groups.front().members.front().native->authored;
    const auto split=f.dynamic->split_for_move(f.id(8));need(split&&split->size()==2&&f.dynamic->group_head(f.ref(4)).reference==f.ref(2)&&
        f.dynamic->group_head(old_head).reference==old_head&&f.dynamic->group_next(old_head).status==NativeObjectGroupLinkStatus::end,
        "actual split splices remaining chain and clears removed member NEXT in its new singleton");
    need(f.dynamic->native_offset(old_head)->x==0,"source unlink preserves exact current fields");
    need(f.dynamic->update(f.id(8))&&f.entries>0&&f.dynamic->group_head(f.ref(4)).reference==old_head&&
        f.dynamic->group_next(old_head).reference==f.ref(2)&&f.dynamic->group_next(f.ref(2)).reference==f.ref(4),
        "actual dynamic combine appends donor order through same owner and source EP8 consumer");
    need(f.dynamic->native_offset(f.ref(2))->x==1&&f.dynamic->native_offset(f.ref(2))->y==0,
        "native combine computes world-oriented current offsets independently of construction-space record");
    need(f.world.remove_retaining_body(old_head)&&f.values->retire_member({old_head.id,old_head.generation})&&
        f.dynamic->erase_after_retire({old_head.id,old_head.generation}),"actual retirement updates value/member owner once");
    need(f.dynamic->group_head(f.ref(4)).reference==f.ref(2)&&f.dynamic->group_next(old_head).status==NativeObjectGroupLinkStatus::end&&
        f.groups.retired_native_members.size()==1&&f.groups.retired_native_members.front().fields.authored.x==authored.x,
        "native unlink promotes source next and exact retained body owns cleared NEXT plus its original fields");
    f.new_constructor(8);need(f.dynamic->group_next(old_head).status==NativeObjectGroupLinkStatus::end&&
        f.dynamic->group_head(f.ref(8)).status==NativeObjectGroupLinkStatus::unavailable,"ID reuse cannot redirect retained native cursor or infer new native metadata");
    need(f.dynamic->bind_native_created_group(f.id(8),f.ref(8),{{f.ref(8),{0,0,0},{0,0,0}}})&&
        f.dynamic->group_head(f.ref(8)).reference==f.ref(8),"actual constructor consumes explicit authenticated fields");
    const auto with_retired=encode_object_group_snapshot(f.groups);need(with_retired[0]==3,"mutation state persists native v3 fields and retained ownership");
    need(f.world.collect_retained_bodies({})&&f.dynamic->collect_retired_native_members()&&
        f.dynamic->group_next(old_head).status==NativeObjectGroupLinkStatus::unavailable&&f.groups.retired_native_members.empty(),
        "collection releases native retained ownership; new numeric ID never revives it");
    validate_object_groups(f.groups,f.world.snapshot());
}
void prospective_rotation_and_refusal(){
    for(std::uint8_t direction=0;direction<8;++direction)for(const bool refuse:{false,true}){
        Fixture f(!refuse&&direction==2);f.bind();need(f.dynamic->recompute_current_offsets(f.ref(8)),"initialize native current offsets through actual owner");
        for(const auto id:{8,2,4})for(const auto word:std::vector<std::pair<std::int16_t,std::int16_t>>{{4,1},{13,128},{23,2},{27,1},{28,1},{42,1}})
            need(f.world.declare_object_word(id,word.first,word.second),"actual native member placement profile");
        ObjectPositionChangeServices services;
        services.group=[&](auto id)->std::optional<ObjectGroup>{for(const auto& g:f.groups.groups)for(const auto&m:g.members)if(m.id==id.id&&m.generation==id.generation)return g;return {};};
        services.authenticate_live_group=[&](const auto& group){return group.native_head.has_value()&&f.authority;};
        services.admit_group_member=services.admit_single_ordinary=[&](auto id)->std::optional<bool>{return f.world.is_live_reference({id.id,id.generation});};
        services.story_count=[]()->std::optional<std::int16_t>{return 3;};services.room_at=[](const auto&)->std::optional<std::uint16_t>{return 1;};
        services.unregister_position=[](auto,bool){return true;};services.register_position=[](auto,auto,bool){return true;};
        services.wall_use=[](auto,bool,bool){return true;};services.object_supported=[](auto,const auto&,bool){return true;};
        services.dynamic_multitile=[&](auto id){return f.dynamic->update(id,false);};
        unsigned authored_post=0;
        if(!refuse&&direction==2){
            services.entry_point9=[&](auto id){need(id.id==8,"source head Post executes first");++authored_post;
                need(f.world.publish_native_object_heading(f.ref(4),6),"authored Post heading mutation");
                return f.dynamic->split_for_move(f.id(4)).has_value();};
        }
        services.begin_native_rotation=[&](auto id,auto heading){return f.dynamic->begin_native_rotation(id,heading);};
        services.rollback_native_rotation=[&](const auto& rotation){return f.dynamic->rollback_native_rotation(rotation);};
        services.native_current_offset=[&](auto id){return f.dynamic->native_offset(id);};
        services.native_group_head=[&](auto id){return f.dynamic->group_head(id);};services.native_group_next=[&](auto id){return f.dynamic->group_next(id);};
        ObjectPositionValidServices checks;unsigned checked=0;
        checks.architecture=[&](auto id,const auto&)->std::optional<ObjectPositionArchitecture>{
            ++checked;
            for(const auto member:{8,2,4})need(f.world.find(member)->object_words.at(1)==direction,
                "ALL prospective raw headings precede EVERY placement callback, including odd headings with OD23=2");
            const auto offset=f.dynamic->native_offset(f.ref(2));
            const std::int32_t xs[8]={1,0,0,0,-1,0,0,0},ys[8]={0,0,1,0,0,0,-1,0};
            need(offset&&offset->x==xs[direction]&&offset->y==ys[direction],"actual current triples recomputed before checks");
            ObjectPositionArchitecture a;a.precise_floor=1;a.level_offset=0;a.out_of_bounds=refuse&&id.id==4;return a;
        };
        checks.intersections=[](auto,const auto&,auto)->std::optional<ObjectPositionIntersections>{ObjectPositionIntersections i;i.footprint_present=true;i.query.status=RoomInfoObstacleIndexStatus::complete;return i;};
        checks.same_group=[](auto,auto)->std::optional<bool>{return true;};checks.ignore_intersection=checks.same_group;
        checks.ghost_image=[](auto)->std::optional<bool>{return false;};
        ObjectPositionValid valid(f.world,checks);ObjectPositionChange move(f.world,valid,services);
        const auto old8=*f.world.find(8)->position,old2=*f.world.find(2)->position;
        const auto result=move.apply_existing_group({f.id(2),{96,96,1,direction},direction,1,[&]{return f.authority;}});
        need(result.status==(refuse?ObjectPositionChangeStatus::blocked:ObjectPositionChangeStatus::success),result.diagnostic.c_str());
        need(checked==3,"native placement traverses actual source head8 then base2 then member4");
        if(refuse){need(f.world.find(8)->object_words.at(1)==0&&f.world.find(2)->object_words.at(1)==0&&
            *f.world.find(8)->position==old8&&*f.world.find(2)->position==old2&&f.dynamic->native_offset(f.ref(2))->x==1,
            "refusal restores headings and actual current triples before source Post, retaining old XYZ");}
        else{
            const auto a=f.dynamic->native_offset(f.ref(8)),b=f.dynamic->native_offset(f.ref(2));
            need(f.world.find(2)->position->x==96&&f.world.find(2)->position->y==96&&
                f.world.find(8)->position->x==96+(a->x-b->x)*16&&f.world.find(8)->position->y==96+(a->y-b->y)*16,
                "candidate anchor is selected body, never BaseObject or first native head");
            if(direction==2)need(authored_post==1&&f.world.find(4)->object_words.at(1)==6&&
                f.dynamic->group_head(f.ref(4)).reference==f.ref(4)&&f.dynamic->native_offset(f.ref(4))->x==-1,
                "successful Post preserves actual split/headings/dual fields and reads changed outgoing links");
        }
    }
    Fixture callback;callback.bind();const auto rotation=callback.dynamic->begin_native_rotation(callback.id(2),2);
    need(rotation&&callback.world.publish_native_object_heading(callback.ref(4),6),"authored check changes a live member heading");
    const auto split=callback.dynamic->split_for_move(callback.id(4));need(split&&split->size()==2,"authored check changes real live membership through same owner");
    need(callback.dynamic->rollback_native_rotation(*rotation)&&callback.world.find(4)->object_words.at(1)==6&&
        callback.world.find(8)->object_words.at(1)==0&&callback.dynamic->group_head(callback.ref(4)).reference==callback.ref(4),
        "refusal subtracts rotation over CURRENT membership without reviving or overwriting detached authored member");
}
void interaction_group_selector(){
    // Distinct exact definition identities: shared group membership does not
    // imply the same InteractionGroupID, resource GUID or master fields.
    const auto provider=[](Fixture& f,std::map<std::uint32_t,std::array<std::int16_t,12>>& words,
            std::vector<std::pair<std::uint32_t,std::int16_t>>& reads){
        f.world.set_definition_word_resolver([&words,&reads](const Entity& e,std::int16_t index)->std::optional<std::int16_t>{
            if(!e.definition)return {};reads.emplace_back(e.definition->guid,index);
            const auto found=words.find(e.definition->guid);
            return found!=words.end()&&index>=0&&index<12?std::optional<std::int16_t>{found->second[index]}:std::nullopt;
        });
    };
    const auto banks=[](std::int16_t group){
        std::map<std::uint32_t,std::array<std::int16_t,12>> words;
        for(const auto guid:{108U,102U,104U}){words[guid][10]=77;words[guid][11]=group>0?std::int16_t(-group):std::int16_t(7);}
        words[104][8]=group;words[108][8]=-7;words[102][8]=-7;return words;
    };
    for(const std::int16_t group:{std::int16_t(-32768),std::int16_t(-7),std::int16_t(0),std::int16_t(7)}){
        Fixture f(false,true);f.bind();auto words=banks(group);std::vector<std::pair<std::uint32_t,std::int16_t>> reads;provider(f,words,reads);
        const auto before=encode_object_group_snapshot(f.groups);
        const auto resolved=f.dynamic->resolve_interaction_group(f.ref(4));
        need(resolved&&*resolved==(group>0?f.ref(8):f.ref(4)),
            "signed InteractionGroupID chooses first physical member8, not numeric minimum/base2 or SubIndex11");
        need(encode_object_group_snapshot(f.groups)==before&&reads.size()>=2&&reads[0]==std::make_pair(104U,std::int16_t(10))&&
            reads[1]==std::make_pair(104U,std::int16_t(8)),"selector reads actual World resource words and never changes owned metadata");
    }
    for(const unsigned profile:{0U,1U,2U,3U}){
        Fixture f(false,true);if(profile!=2)f.bind();auto words=banks(7);std::vector<std::pair<std::uint32_t,std::int16_t>> reads;
        if(profile==0){words[108][8]=-9;words[102][10]=0;} // No sibling master filter in raw43532b.
        if(profile==1){words[108][8]=-8;words[102][8]=-9;}
        if(profile==3)words[104][10]=0;provider(f,words,reads);
        const auto resolved=f.dynamic->resolve_interaction_group(f.ref(4));
        if(profile==2)need(!resolved&&!f.dynamic->diagnostic().empty(),"unknown legacy MT ownership remains unavailable");
        else need(resolved&&*resolved==(profile==0?f.ref(2):f.ref(4)),
            "native selector handles later partner, absent partner and non-MT without invented group fallback");
        if(profile==3)need(reads.size()==1&&reads.front().second==10,"non-MT returns before reading signed InteractionGroupID");
    }
    {
        Fixture f(false,true);f.bind();auto words=banks(7);std::vector<std::pair<std::uint32_t,std::int16_t>> reads;provider(f,words,reads);
        need(f.world.declare_definition_word(4,8,9),"actual owned scope21 word takes precedence");words[108][8]=-9;
        need(f.dynamic->resolve_interaction_group(f.ref(4))==std::optional<WorldEntityReference>{f.ref(8)},
            "selector reuses existing World owned-word/provider precedence");
        const auto old=f.ref(4);need(!f.dynamic->resolve_interaction_group({old.id,old.generation+1}),"stale selected generation never resolves numerically");
        need(f.world.remove(4)&&f.world.create_runtime_object(4,104,{},std::vector<EntryPoint>(27),9000),"actual ID reuse fixture");
        need(!f.dynamic->resolve_interaction_group(old),"reused numeric ID never revives original body selector");
    }
    for(const unsigned corruption:{0U,1U,2U}){
        Fixture f(false,true);f.bind();auto words=banks(7);
        f.world.set_definition_word_resolver([&](const Entity& e,std::int16_t index)->std::optional<std::int16_t>{
            if(!e.definition)return {};
            if(corruption==2&&e.definition->guid==108&&index==8)f.groups.groups.front().members.front().native->next=f.ref(8);
            if(corruption==1&&e.definition->guid==104&&index==8)return {};
            return index>=0&&index<12?std::optional<std::int16_t>{words[e.definition->guid][index]}:std::nullopt;
        });
        if(corruption==0)f.groups.groups.front().members.back().native->next=f.ref(8);
        need(!f.dynamic->resolve_interaction_group(f.ref(4))&&!f.dynamic->diagnostic().empty(),
            "cycles, missing definition words and a chain mutated by a provider fail explicitly instead of falling back to self");
    }
    {
        Fixture f(false,true);f.bind();auto words=banks(7);std::vector<std::pair<std::uint32_t,std::int16_t>> reads;provider(f,words,reads);
        bool queried=false;
        Vm vm({{routine_key(4096),Bhav{0x8002,{{98,254,255,{}}},{},Bhav::Metadata{0,4,0,0}}}},f.world.vm_memory(),{},
            [&](const auto&,const auto&,const auto&,const auto&){
                queried=f.dynamic->resolve_interaction_group(f.ref(4))==std::optional<WorldEntityReference>{f.ref(8)};
                return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
            });
        need(vm.start(routine_key(4096),{4,4,0})==VmStatus::running&&vm.run(4)==VmStatus::returned_true&&queried,
            "read-only exact selector remains usable during actual dispatch with host authority");
        f.authority=false;need(!f.dynamic->resolve_interaction_group(f.ref(4)),"expired actual host authority rejects lookup");
    }
}
}
int main(){try{admission_and_rotation();mutable_unlink_boundaries();mutations_and_lifetime();prospective_rotation_and_refusal();interaction_group_selector();std::cout<<"native group recovery ownership checks passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
