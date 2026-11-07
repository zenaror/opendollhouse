#include "odh/world.hpp"
#include "odh/ts1_variable.hpp"
#include "odh/avatar_containment.hpp"
#include "odh/avatar_person_data.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
odh::VmFrame frame(std::int16_t caller=1,std::int16_t object=2) {
    return {odh::routine_key(4096),0,{caller,object,object},{},{}};
}
void native_physical_parent_family(){
    using namespace odh;
    for(bool known:{false,true})for(int mode=0;mode<10;++mode){
        World world;
        require(world.create(2,EntityKind::object,{0,0,2})&&world.create(3,EntityKind::object,{0,0,2})&&
            world.set_position(2,{40,40,1,0})&&world.set_position(3,{56,56,1,0}),"physical native family parents");
        require(known?world.create_runtime_object(1,1001,{},{}):world.create(1,EntityKind::object),"physical native family child");
        if(!known)require(world.declare_object_word(1,1,0)&&world.declare_object_word(1,14,7)&&world.declare_object_word(1,26,222),"legacy unknown raw fields");
        require(world.set_position(1,{24,24,1,0})&&world.attach(2,0,1),"real initial physical attach");
        const auto matches=[&](int slot,int parent){const auto*e=world.find(1);return e&&e->object_words.at(14)==slot&&e->object_words.at(26)==parent;};
        require(known?matches(1,2):matches(7,222),"attach publishes only known native physical metadata");
        bool removed=false,transferred=false;
        if(mode==0)require(world.detach(2,0),"physical detach");
        if(mode==1)require(world.place_on_floor(1,{24,40,1,0}),"floor detaches physical root");
        if(mode==2)require(world.place_out_of_world(1,0),"OOW detaches physical root");
        if(mode==3)require(world.place_individual_on_floor(1,EntityPosition{24,40,1,0},0),"individual floor detaches root");
        if(mode==4){require(world.move_to_slot(3,1,1),"direct physical transfer");transferred=true;}
        if(mode==5){auto plan=world.prepare_move_to_slot(3,1,1);require(plan&&world.can_commit_move_to_slot(*plan)&&world.commit_move_to_slot(*plan),"prepared physical transfer");transferred=true;}
        if(mode==6)require(world.remove(2),"remove parent detaches physical children");
        if(mode==7)require(world.remove_retaining_body({2,world.find(2)->generation}),"retain removed parent detaches children");
        if(mode==8||mode==9){const WorldEntityReference parent{2,world.find(2)->generation},child{1,world.find(1)->generation};
            require(world.remove_retaining_contents(parent,[&](const auto& token){
                if(mode==9){removed=true;return world.remove(1);}
                return world.detach_retained_slot(token,0,child);
            }),"retired container child drop");}
        if(removed){require(!world.find(1),"retired child removed");continue;}
        require(known?matches(transferred?2:-1,transferred?3:0):matches(7,222),"every physical transition publishes actual native relation and preserves unknown");
        require(world.find(1)->native_slot0.has_value()==known,"physical mutations never backfill native backing");
        require(transferred?world.find(1)->attachment&&world.find(1)->attachment->container==3:!world.find(1)->attachment,"physical transition reaches actual containment");
    }
    // Geometry-only native publication must not erase an independent zero chain.
    World chain;require(chain.create_runtime_object(1,1001,{0,0,1}, {})&&chain.create_runtime_object(2,1002,{}, {})&&chain.set_position(1,{40,40,1,0}),"native zero chain source");
    const WorldEntityReference parent{1,chain.find(1)->generation},child{2,chain.find(2)->generation};
    require(chain.declare_object_word(2,14,0)&&chain.declare_object_word(2,26,1)&&chain.publish_native_slot0(parent,child)&&
        chain.publish_native_recovery_position(child,EntityPosition{40,40,1,0},false)&&
        chain.find(2)->object_words.at(14)==0&&chain.find(2)->object_words.at(26)==1&&chain.native_slot0(parent)==std::optional<WorldEntityReference>{child},
        "native scalar publication preserves independently owned zero chain");
    require(!chain.move_to_slot(1,0,2)&&!chain.prepare_move_to_slot(1,0,2),"physical transfer cannot steal an incoming zero-chain member");
}
void original_load_traversal_contract(){
    using namespace odh;
    const auto saved=[](World& world,std::int16_t id){
        std::array<std::int16_t,68> words{};words[3]=-1;words[11]=id;
        return world.create_saved_entity(id,EntityKind::object,static_cast<std::uint32_t>(1000+id),
            {},{},{},words,{},0);
    };
    const auto ref=[](const World& world,std::int16_t id){const auto* e=world.find(id);return WorldEntityReference{id,e?e->generation:0};};
    const auto order=[](const World& world,const OriginalLoadTraversal& lease){
        std::vector<std::int16_t> result;auto cursor=world.original_load_head(lease);
        for(std::size_t n=0;cursor.status==OriginalLoadLinkStatus::member && n<32;++n){
            result.push_back(cursor.reference.id);cursor=world.original_load_next(lease,cursor.reference);
        }
        require(cursor.status==OriginalLoadLinkStatus::end,"original traversal has authenticated end");return result;
    };
    World world;require(saved(world,30)&&saved(world,10)&&saved(world,20),"original source identity fixture");
    const std::vector<WorldEntityReference> source{ref(world,30),ref(world,10),ref(world,20)};
    const auto baseline=world.snapshot();
    require(!world.bind_original_load_order({source[0],source[0],source[2]}) &&
        !world.bind_original_load_order({source[0],source[1]}) &&
        !world.bind_original_load_order({source[0],source[1],{20,999}}),"binding validates whole exact serialized cohort before effects");
    auto lease=world.bind_original_load_order(source,12);require(lease.has_value(),"bind explicit original serialized order");
    require(order(world,*lease)==std::vector<std::int16_t>({30,10,20}) &&
        world.spatial_revision()==baseline.spatial_revision && world.word_count()==baseline.word_count,
        "binding uses source order rather than numeric IDs and does not mutate saved state");
    require(!world.bind_original_load_order(source),"only one original-load authority owns this World");
    require(world.create(1,EntityKind::object),"bootstrap ordinary constructor prepends");
    unsigned constructor_observations=0;bool constructor_link_observed=false;
    require(world.set_spatial_change_observer([&](World& actual,std::uint64_t){
        ++constructor_observations;const auto head=actual.original_load_head(*lease);
        constructor_link_observed=head.status==OriginalLoadLinkStatus::member && head.reference==ref(actual,2);
    }),"bind runtime constructor link observer");
    require(world.create_runtime_object(2,2002,{},{}),"actual runtime snapshot transaction preserves and prepends authority");
    require(world.set_spatial_change_observer({}) && constructor_observations==1 && constructor_link_observed,
        "constructor observer saw committed exact live links before notification returned");
    require(saved(world,40) && order(world,*lease)==std::vector<std::int16_t>({2,1,30,10,20,40}),
        "saved constructor appends while both ordinary constructors prepend");
    const auto captured=world.original_load_head(*lease);const auto first=ref(world,1),thirty=ref(world,30),ten=ref(world,10);
    require(world.create_runtime_object(3,2003,{},{}),"creation during a pass prepends before captured head");
    require(captured.reference==ref(world,2) && world.original_load_next(*lease,captured.reference).reference==first,
        "a pass retains captured head and does not retroactively visit prepended object");
    require(world.remove(30) && world.original_load_next(*lease,first).reference==ten &&
        world.original_load_next(*lease,thirty).status==OriginalLoadLinkStatus::unavailable,
        "future removal splices live predecessor and never invents removed-cursor END");
    require(world.remove(10) && world.remove(3) && world.remove(40) &&
        order(world,*lease)==std::vector<std::int16_t>({2,1,20}),"middle head and tail removals update live traversal");
    require(saved(world,50) && world.original_load_next(*lease,ref(world,20)).reference==ref(world,50),
        "tail removal preserves subsequent saved append");
    require(world.remove(1) && world.create(1,EntityKind::object) && ref(world,1)!=first &&
        world.original_load_next(*lease,first).status==OriginalLoadLinkStatus::unavailable &&
        world.original_load_head(*lease).reference==ref(world,1),"ID reuse never redirects removed source cursor");
    require(world.mark_dead(ref(world,20)) && world.original_load_next(*lease,ref(world,20)).reference==ref(world,50),
        "deferred-dead still-indexed node retains actual live-list link ownership");
    auto moved=std::move(*lease);
    require(world.original_load_head(*lease).status==OriginalLoadLinkStatus::unavailable &&
        order(world,moved)==std::vector<std::int16_t>({1,2,20,50}),"move transfers sole scoped authority");
    const auto active_snapshot=world.snapshot();auto invalid=active_snapshot;invalid.generation_sequence=0;
    require(!world.publish_snapshot(invalid) && world.original_load_head(moved).status==OriginalLoadLinkStatus::member,
        "failed snapshot publication leaves authority intact");
    require(world.publish_snapshot(active_snapshot) && world.original_load_head(moved).status==OriginalLoadLinkStatus::unavailable,
        "successful general snapshot publication invalidates runtime authority");
    World restored;require(restored.restore_snapshot(baseline) &&
        !restored.bind_original_load_order(source) && restored.original_load_head(moved).status==OriginalLoadLinkStatus::unavailable,
        "ordinary snapshot restore never recreates or accepts original-load authority");
    {
        World bounded;require(saved(bounded,7)&&saved(bounded,8),"bounded original fixture");
        auto guard=bounded.bind_original_load_order({ref(bounded,8),ref(bounded,7)},2);require(guard.has_value(),"bounded scoped binding");
        const auto before=bounded.entity_identity_revision();const auto words=bounded.word_count();
        require(!bounded.create(9,EntityKind::object) && !bounded.create_runtime_object(9,9009,{}, {}) && !saved(bounded,9) &&
            bounded.entity_identity_revision()==before && bounded.word_count()==words &&
            order(bounded,*guard)==std::vector<std::int16_t>({8,7}),"all constructors reject link capacity before publishing World effects");
        require(bounded.remove(8) && saved(bounded,9) && order(bounded,*guard)==std::vector<std::int16_t>({7,9}),
            "removal frees bounded link capacity for an actual append");
        guard->close();require(bounded.original_load_head(*guard).status==OriginalLoadLinkStatus::unavailable &&
            bounded.create(10,EntityKind::object),"closing scope disables authority and its temporary capacity restriction");
    }
    {
        World clean;require(saved(clean,4),"publication rebinding fixture");const auto four=ref(clean,4);
        auto guard=clean.bind_original_load_order({four});require(guard.has_value(),"bind before general publication");
        require(clean.publish_snapshot(clean.snapshot()) && !clean.bind_original_load_order({four}),
            "general publication cannot silently preserve or rebind stale source authority");
    }
    {
        World retired;require(saved(retired,4)&&saved(retired,5),"retained original fixture");
        const auto four=ref(retired,4),five=ref(retired,5);auto guard=retired.bind_original_load_order({four,five});
        require(guard.has_value() && retired.remove_retaining_body(four) && retired.resolve_reference(four) &&
            retired.original_load_head(*guard).reference==five &&
            retired.original_load_next(*guard,four).status==OriginalLoadLinkStatus::member &&
            retired.original_load_next(*guard,four).reference==five,
            "retained body owns its exact original outgoing NEXT after unlink");
        require(retired.retire(5) && retired.original_load_head(*guard).status==OriginalLoadLinkStatus::end &&
            retired.original_load_next(*guard,five).status==OriginalLoadLinkStatus::unavailable &&
            retired.original_load_next(*guard,four).status==OriginalLoadLinkStatus::unavailable,
            "hard-removed successor makes retained outgoing pointer unavailable, never rewritten END");
    }
    {
        World scoped;require(saved(scoped,6)&&saved(scoped,7),"scoped original fixture");
        const auto six=ref(scoped,6),seven=ref(scoped,7);
        {auto guard=scoped.bind_original_load_order({six,seven});require(guard.has_value(),"enter scoped authority");}
        auto rebound=scoped.bind_original_load_order({six,seven});require(rebound.has_value(),"lease destruction releases sole active ownership");
        bool outgoing_during_callback=false;
        require(scoped.remove_retaining_contents(six,[&](const auto& token){
                outgoing_during_callback=scoped.original_load_next(*rebound,six).reference==seven && scoped.resolve_reference(six);
                return scoped.retired_container_active(token);
            }) && outgoing_during_callback && scoped.original_load_head(*rebound).reference==seven &&
            scoped.original_load_next(*rebound,six).reference==seven,
            "retained-contents current owns exact outgoing NEXT during and after removal callback");
        require(scoped.remove_retaining_contents(seven,[&](const auto& token){return scoped.retired_container_active(token);}) &&
            scoped.original_load_next(*rebound,seven).status==OriginalLoadLinkStatus::end &&
            scoped.original_load_next(*rebound,six).reference==seven,
            "retained-contents chain preserves removed successor and authenticated tail END");
    }
    {
        World held;require(saved(held,1)&&saved(held,2)&&saved(held,3),"owned outgoing pointer fixture");
        const auto one=ref(held,1),two=ref(held,2),three=ref(held,3);
        auto guard=held.bind_original_load_order({one,two,three},6);require(guard.has_value(),"bind owned pointer cohort");
        require(held.remove_retaining_body(one) && held.create_runtime_object(1,2001,{}, {}) && ref(held,1)!=one &&
            held.original_load_next(*guard,one).reference==two,
            "retained current NEXT survives numeric ID reuse without redirecting to replacement");
        require(held.remove_retaining_body(two) && held.original_load_head(*guard).reference==ref(held,1) &&
            held.original_load_next(*guard,one).reference==two && held.original_load_next(*guard,two).reference==three,
            "retained current still points to exact retained successor after future deletion");
        const auto before=held.word_count();
        require(!held.collect_retained_bodies({{99,999}}) && held.original_load_next(*guard,one).reference==two,
            "failed collection leaves body and outgoing authority intact");
        require(held.collect_retained_bodies({one}) && held.word_count()<before && held.resolve_reference(one) && !held.resolve_reference(two) &&
            held.original_load_next(*guard,one).status==OriginalLoadLinkStatus::unavailable &&
            held.original_load_next(*guard,two).status==OriginalLoadLinkStatus::unavailable,
            "successor collection never silently bypasses it or redirects by numeric ID");
        require(saved(held,2) && held.original_load_next(*guard,one).status==OriginalLoadLinkStatus::unavailable,
            "recreated successor ID cannot revive collected original outgoing pointer");
        require(held.collect_retained_bodies({}) && !held.resolve_reference(one) &&
            held.original_load_next(*guard,one).status==OriginalLoadLinkStatus::unavailable,
            "collecting current releases its exact outgoing authority");
    }
    {
        World budget;require(saved(budget,1)&&saved(budget,2),"retained link budget fixture");
        const auto one=ref(budget,1),two=ref(budget,2);auto guard=budget.bind_original_load_order({one,two},2);
        require(guard.has_value() && budget.remove_retaining_body(one),"transfer existing bounded node into retained ownership");
        const auto state=budget.entity_identity_revision();const auto words=budget.word_count();
        require(!budget.create(3,EntityKind::object) && !budget.create_runtime_object(3,3003,{}, {}) && !saved(budget,3) &&
            budget.entity_identity_revision()==state && budget.word_count()==words &&
            budget.original_load_next(*guard,one).reference==two,
            "retained nodes share aggregate bound; all constructors reject before World effects");
        require(budget.collect_retained_bodies({one}) && !saved(budget,3),"live retained root keeps its link capacity occupied");
        require(budget.collect_retained_bodies({}) && saved(budget,3) &&
            budget.original_load_next(*guard,two).reference==ref(budget,3),
            "collection releases retained node capacity for subsequent actual saved append");
    }
}
void current_value_runtime_contract(){
    odh::World world;require(world.create_runtime_object(1,1001,{},std::vector<odh::EntryPoint>(27)),"CurrentValue constructor fixture");
    require(world.create(2,odh::EntityKind::avatar)&&world.declare_object_word(2,41,17),"untracked avatar CurrentValue fixture");
    std::int16_t price=33;bool unknown=false,unhandled=false;unsigned writes=0,reads=0;
    require(world.set_current_value_read_resolver([&](const odh::World&,auto id){++reads;require(id.id==1,"only live object read is delegated");
        return odh::World::CurrentValueReadResult{!unhandled,unknown?std::optional<std::int16_t>{}:std::optional<std::int16_t>{price}};
    }),"bind single read owner");
    std::function<bool()> retained;
    require(world.set_current_value_write_observer([&](const odh::World& actual,auto id,auto before,auto after,const auto& store){
        ++writes;require(id.id==1&&actual.find(1)->object_words.at(41)==before,"observer runs before raw storage");retained=store;
        if(!store())return false;price=after;return true;
    }),"bind single write owner");
    const auto revision=world.spatial_revision();require(world.write(frame(1,1),3,41,0)&&writes==1&&price==0&&
        world.spatial_revision()==revision,"same-value write invokes group side effect without spurious raw spatial change");
    require(retained&&!retained(),"CurrentValue store capability expires after synchronous callback");
    require(world.declare_object_word(1,41,7)&&writes==1&&price==0,"declaration never seeds or mutates group price");
    require(world.read(frame(1,1),3,41)==0,"handled read selects group value rather than raw field");
    unknown=true;require(!world.read(frame(1,1),4,41),"handled unknown is distinct from raw fallback");
    unhandled=true;require(world.read(frame(1,1),3,41)==7,"unhandled read preserves legacy raw field");
    const auto read_calls=reads;require(world.read(frame(2,2),3,41)==17&&world.write(frame(2,2),3,41,19)&&writes==1&&reads==read_calls,
        "avatar values keep prior raw semantics outside object valuation");
    odh::World restored;require(restored.restore_snapshot(world.snapshot())&&restored.read(frame(1,1),3,41)==7,
        "snapshot restoration carries raw fields without runtime callbacks or price metadata");
    require(world.set_current_value_write_observer({})&&world.set_current_value_write_observer([](const auto&,auto,auto,auto,const auto&){return false;}),"replace explicitly detached observer");
    require(!world.write(frame(1,1),3,41,99)&&world.find(1)->object_words.at(41)==7,"observer refusal leaves original word intact");
    require(world.set_current_value_write_observer({})&&world.set_current_value_write_observer([&](const auto&,auto,auto,auto,const auto& store){
        require(!world.declare_object_word(1,6,123),"observer cannot mutate World outside its fixed store");return store();
    }),"bind adversarial reentry observer");
    require(!world.write(frame(1,1),3,41,99)&&world.find(1)->object_words.at(41)==7,"reentry rejects store and preserves complete original field");
    require(world.set_current_value_write_observer({})&&world.set_current_value_write_observer([](const auto&,auto,auto,auto,const auto& store){
        const auto first=store();(void)store();return first;
    }),"bind repeated-store observer");
    require(!world.write(frame(1,1),3,41,99)&&world.find(1)->object_words.at(41)==7,"repeated fixed store is rejected and raw word rolled back");
}
void suppress_referenced_reset_policy() {
    using namespace odh;
    for(const auto version:std::array<std::optional<std::uint32_t>,3>{std::nullopt,55,62}){
        World world;std::array<std::int16_t,68> words{};words[3]=-1;words[11]=1;
        require(world.create_saved_entity(1,EntityKind::object,1001,{},{},{},words,{},0),"policy source object");
        const WorldEntityReference reference{1,world.find(1)->generation};
        auto wrong=reference;++wrong.generation;
        require(!world.suppress_referenced_reset(reference)&&
            !world.bind_original_load_order({wrong},4,version)&&!world.suppress_referenced_reset(reference),
            "unknown policy and rejected source binding do not invent constructor zero");
        auto lease=world.bind_original_load_order({reference},4,version);
        require(lease.has_value(),"authenticate policy source cohort");
        require(world.suppress_referenced_reset(reference)==(version&&*version!=55?
            std::optional<bool>{false}:std::nullopt),"only explicit non0x37 source initializes known false");
        const auto initial_policy=world.suppress_referenced_reset(reference);
        lease->close();lease=world.bind_original_load_order({reference},4,62);
        require(lease&&world.suppress_referenced_reset(reference)==initial_policy,
            "later explicit source version cannot manufacture constructor proof on rebind");
        require(world.set_suppress_referenced_reset(reference,true)&&
            world.suppress_referenced_reset(reference)==std::optional<bool>{true}&&
            world.set_suppress_referenced_reset(reference,false)&&
            world.suppress_referenced_reset(reference)==std::optional<bool>{false},
            "proven native writers publish either policy value independently of unknown");
        require(!world.set_suppress_referenced_reset(wrong,true)&&!world.suppress_referenced_reset(wrong),
            "stale generation cannot read or set another instance policy");
        require(world.set_suppress_referenced_reset(reference,true),"preserve policy before in-memory copy");
        lease->close();lease=world.bind_original_load_order({reference},4,62);
        require(lease&&world.suppress_referenced_reset(reference)==std::optional<bool>{true},
            "source lease rebind preserves proven live true policy instead of constructor reset");
        World copied;require(copied.restore_snapshot(world.snapshot())&&
            copied.suppress_referenced_reset(reference)==std::optional<bool>{true},
            "in-memory snapshot retains proven policy without claiming disk codec support");
        require(world.remove_retaining_body(reference)&&world.create(1,EntityKind::object),"retain policy body before ID reuse");
        const WorldEntityReference replacement{1,world.find(1)->generation};
        require(!world.suppress_referenced_reset(replacement)&&
            world.suppress_referenced_reset(reference)==std::optional<bool>{true}&&
            world.set_suppress_referenced_reset(reference,false)&&!world.suppress_referenced_reset(replacement),
            "retained body policy remains independent of a replacement numeric ID");
        require(world.collect_retained_bodies({})&&!world.set_suppress_referenced_reset(reference,true)&&
            !world.suppress_referenced_reset(reference),"collected body cannot retain policy authority");
        require(world.create_runtime_object(2,1002,{},std::vector<EntryPoint>(27))&&
            !world.suppress_referenced_reset({2,world.find(2)->generation}),
            "runtime constructor does not inherit original import policy");
    }
}
void native_scalar_position_publication(){
    using namespace odh;
    World world;std::array<std::int16_t,68> words{};words[1]=2;words[3]=7;words[11]=1;
    require(world.create_saved_entity(1,EntityKind::avatar,100,{},{},{},words,{},0),"native scalar imported avatar");
    require(world.set_radian_direction(1,1.61f),"nonquantized source angle");
    const WorldEntityReference ref{1,world.find(1)->generation};const auto angle=world.find(1)->radian_direction;
    require(world.publish_native_recovery_position(ref,EntityPosition{40,56,1,2},false)&&
        world.find(1)->radian_direction==angle&&world.find(1)->object_words.at(1)==2&&world.find(1)->unattached_slot==7,
        "native scalar changes XYZ without quantizing or clearing a preserved slot");
    auto wrong=ref;++wrong.generation;
    const auto before=world.spatial_revision();
    require(!world.publish_native_recovery_position(wrong,{},true)&&
        !world.publish_native_recovery_position(ref,EntityPosition{24,24,1,3},true)&&
        world.spatial_revision()==before&&world.find(1)->unattached_slot==7,
        "stale identity or changed heading rejects before scalar or slot publication");
    require(world.publish_native_recovery_position(ref,{},true)&&!world.find(1)->position&&
        world.find(1)->unattached_slot==-1&&world.find(1)->radian_direction==angle&&world.find(1)->object_words.at(1)==2,
        "native OOW explicitly clears uncontained saved slot and preserves both angles");
    require(world.create(2,EntityKind::object,{0,0,1})&&world.attach(2,0,1),"native scalar reciprocal containment");
    require(!world.publish_native_recovery_position(ref,{},true)&&world.find(2)->slots[0]==1&&world.find(1)->attachment,
        "scalar cannot silently substitute native parent detach");
}
void native_recovery_position_policy() {
    using namespace odh;
    for(const auto version:std::array<std::optional<std::uint32_t>,3>{std::nullopt,55,62}){
        World world;std::array<std::int16_t,68> words{};words[3]=-1;words[11]=1;
        require(world.create_saved_entity(1,EntityKind::object,1001,{},{},{},words,{},0),"policy source object");
        const WorldEntityReference reference{1,world.find(1)->generation};
        auto wrong=reference;++wrong.generation;
        require(!world.skip_native_recovery_position(reference)&&
            !world.bind_original_load_order({wrong},4,version)&&!world.skip_native_recovery_position(reference),
            "unknown policy and rejected source binding do not invent constructor zero");
        auto lease=world.bind_original_load_order({reference},4,version);
        require(lease.has_value(),"authenticate policy source cohort");
        require(world.skip_native_recovery_position(reference)==(version&&*version!=55?
            std::optional<bool>{false}:std::nullopt),"only explicit non0x37 source initializes known false");
        const auto initial_policy=world.skip_native_recovery_position(reference);
        const auto independent_reset_policy=world.suppress_referenced_reset(reference);
        lease->close();lease=world.bind_original_load_order({reference},4,62);
        require(lease&&world.skip_native_recovery_position(reference)==initial_policy,
            "later explicit source version cannot manufacture constructor proof on rebind");
        require(world.set_skip_native_recovery_position(reference,true)&&
            world.skip_native_recovery_position(reference)==std::optional<bool>{true}&&
            world.set_skip_native_recovery_position(reference,false)&&
            world.skip_native_recovery_position(reference)==std::optional<bool>{false},
            "proven native writers publish either policy value independently of unknown");
        require(world.suppress_referenced_reset(reference)==independent_reset_policy,
            "native bit6 writes never mutate independent bit12 policy");
        require(!world.set_skip_native_recovery_position(wrong,true)&&!world.skip_native_recovery_position(wrong),
            "stale generation cannot read or set another instance policy");
        require(world.set_skip_native_recovery_position(reference,true),"preserve policy before in-memory copy");
        lease->close();lease=world.bind_original_load_order({reference},4,62);
        require(lease&&world.skip_native_recovery_position(reference)==std::optional<bool>{true},
            "source lease rebind preserves proven live true policy instead of constructor reset");
        World copied;require(copied.restore_snapshot(world.snapshot())&&
            copied.skip_native_recovery_position(reference)==std::optional<bool>{true},
            "in-memory snapshot retains proven policy without claiming disk codec support");
        require(world.remove_retaining_body(reference)&&world.create(1,EntityKind::object),"retain policy body before ID reuse");
        const WorldEntityReference replacement{1,world.find(1)->generation};
        require(!world.skip_native_recovery_position(replacement)&&
            world.skip_native_recovery_position(reference)==std::optional<bool>{true}&&
            world.set_skip_native_recovery_position(reference,false)&&!world.skip_native_recovery_position(replacement),
            "retained body policy remains independent of a replacement numeric ID");
        require(world.collect_retained_bodies({})&&!world.set_skip_native_recovery_position(reference,true)&&
            !world.skip_native_recovery_position(reference),"collected body cannot retain policy authority");
        require(world.create_runtime_object(2,1002,{},std::vector<EntryPoint>(27))&&
            !world.skip_native_recovery_position({2,world.find(2)->generation}),
            "runtime constructor does not inherit original import policy");
        const WorldEntityReference runtime{2,world.find(2)->generation};
        require(world.set_ui_ghost(2,true)&&!world.skip_native_recovery_position(runtime)&&
            world.set_ui_ghost(2,false)&&world.mark_dead(runtime)&&!world.skip_native_recovery_position(runtime),
            "ghost and generic dead transitions do not manufacture native bit6 knowledge");

    }
}
void original_person_registration_order() {
    using namespace odh;
    World world;std::array<std::int16_t,68> words{};words[3]=-1;
    for(const auto id:{20,10})require(world.create_saved_entity(id,EntityKind::avatar,1000+id,
        {},std::vector<std::int16_t>(101),std::vector<std::int16_t>(16),words,{},0),"import people");
    const WorldEntityReference first{20,world.find(20)->generation},second{10,world.find(10)->generation};
    auto lease=world.bind_original_load_order({first,second},4);require(lease.has_value(),"bind person registry");
    auto notifier=world.begin_original_person_notifications(*lease);
    auto nested=world.begin_original_person_notifications(*lease);
    require(notifier&&nested&&world.original_load_person_at(*notifier,0).reference==first,
        "independent notifier captures actual registered PERSON storage");
    require(world.original_load_person_at(*lease,0).reference==first&&
        world.original_load_person_at(*lease,1).reference==second,"person registry follows original map registration");
    require(world.create(30,EntityKind::avatar),"construct another person");
    const WorldEntityReference third{30,world.find(30)->generation};
    require(world.original_load_head(*lease).reference==third&&world.original_load_person_at(*lease,2).reference==third,
        "person appends to registration vector while prepending to World list");
    require(world.remove_retaining_body(first)&&world.original_load_person_at(*lease,1).reference==third,
        "removing current person shifts slots before the notifier increments its ordinal");
    require(world.original_load_person_at(*notifier,1).reference==third&&
        world.original_load_person_at(*nested,0).reference==second&&
        world.original_load_person_at(*notifier,2).status==OriginalLoadLinkStatus::end&&
        world.original_load_person_at(*notifier,3).status==OriginalLoadLinkStatus::unavailable,
        "notifiers retain storage but reread shifted members and live end independently");
    require(world.mark_dead(second)&&world.original_load_person_at(*lease,0).reference==second,
        "native notification registry does not filter indexed deferred-dead people");
    require(world.original_load_person_at(*lease,2).status==OriginalLoadLinkStatus::end&&
        world.original_load_person_at(*lease,3).status==OriginalLoadLinkStatus::unavailable,
        "only exact current vector end is END");
    lease->close();require(world.original_load_person_at(*lease,0).status==OriginalLoadLinkStatus::unavailable,
        "closed original source authority cannot expose a fabricated person vector");
    require(world.original_load_person_at(*notifier,0).status==OriginalLoadLinkStatus::unavailable,
        "closed import invalidates borrowed notifier without extending its lifetime");
    World empty;require(empty.create_saved_entity(1,EntityKind::object,1001,{},{},{},words,{},0),"empty registry object");
    auto fresh=empty.bind_original_load_order({{1,empty.find(1)->generation}},2);
    require(fresh.has_value(),"bind empty reserved person registry");
    auto initially_empty=empty.begin_original_person_notifications(*fresh);
    require(fresh&&empty.original_load_person_at(*fresh,0).status==OriginalLoadLinkStatus::end&&
        empty.create(2,EntityKind::avatar)&&empty.original_load_person_at(*fresh,0).reference.id==2,
        "first person constructed after binding is visible to a newly begun notifier");
    require(initially_empty&&empty.original_load_person_at(*initially_empty,0).reference.id==2&&
        empty.original_load_person_at(*initially_empty,1).status==OriginalLoadLinkStatus::end,
        "captured initially empty reserved storage sees later append");
    const auto state=empty.snapshot();require(empty.publish_snapshot(state)&&
        empty.original_load_person_at(*initially_empty,0).status==OriginalLoadLinkStatus::unavailable,
        "snapshot publication invalidates captured PERSON storage");
    World rebound;
    for(const auto id:{9,2})require(rebound.create_saved_entity(id,EntityKind::avatar,1000+id,
        {},std::vector<std::int16_t>(101),std::vector<std::int16_t>(16),words,{},0),"rebind person source fixture");
    const WorldEntityReference nine{9,rebound.find(9)->generation},two{2,rebound.find(2)->generation};
    auto original=rebound.bind_original_load_order({nine,two},4);
    require(original.has_value(),"initial person source lease");
    const auto old_notifier=rebound.begin_original_person_notifications(*original);
    require(old_notifier.has_value(),"capture notifier before source lease replacement");
    original->close();auto replacement=rebound.bind_original_load_order({two,nine},4);
    require(replacement&&rebound.original_load_person_at(*replacement,0).reference==two&&
        rebound.original_load_person_at(*old_notifier,0).status==OriginalLoadLinkStatus::unavailable,
        "old notifier never follows a replacement source lease even at same World address");
    auto current_notifier=rebound.begin_original_person_notifications(*replacement);
    require(current_notifier&&rebound.remove(9)&&rebound.create(9,EntityKind::avatar)&&
        rebound.original_load_person_at(*current_notifier,1).reference.generation!=nine.generation&&
        rebound.original_load_person_at(*current_notifier,1).reference.id==9,
        "live notifier observes newly registered generation after numeric ID reuse");
    World foreign;require(foreign.original_load_person_at(*current_notifier,0).status==OriginalLoadLinkStatus::unavailable,
        "captured notifier cannot enumerate a different World");
}
void neighborhood_data_reference_contract() {
    odh::World world;
    require(world.create(1,odh::EntityKind::avatar,{1,88,0}),"neighborhood-data reference fixture");
    const auto before=world.snapshot();
    unsigned callback_reads=0;
    odh::VmMemory memory;memory.read=[&](const auto&,auto,auto)->std::optional<std::int16_t>{++callback_reads;return 777;};
    const odh::Ts1Temps temps{};
    for(const auto context:{frame(1,1),frame(0,0),frame(5,32)})
        for(const auto index:{std::int16_t{-32768},std::int16_t{-1},std::int16_t{0},std::int16_t{2},std::int16_t{32767}}) {
            require(world.read(context,34,index)==std::optional<std::int16_t>{0}&&
                odh::read_ts1_variable(world,memory,context,temps,34,index)==std::optional<std::int16_t>{0}&&callback_reads==0,
                "World and read-only variable query share unconditional zero NeighborhoodData with absent participants");
            require(!world.write(context,34,index,1234),"World cannot write reference NeighborhoodData");
        }
    const auto after=world.snapshot();
    require(before.entities.size()==after.entities.size()&&before.generation_sequence==after.generation_sequence&&
        before.entities.front().person_data==after.entities.front().person_data&&
        before.entities.front().attributes==after.entities.front().attributes&&
        before.entities.front().object_words==after.entities.front().object_words,
        "reference NeighborhoodData does not allocate or alter entity metadata");
}
void atomic_attribute_max() {
    odh::World world;
    require(world.create(1,odh::EntityKind::object,{6,0,0}),"create fixed attribute bank for atomic merge");
    const odh::WorldEntityReference reference{1,world.find(1)->generation};
    const std::array<std::int16_t,6> values{1,2,3,4,5,6};
    unsigned notifications=0;bool complete=false;
    require(world.set_spatial_change_observer([&](odh::World& observed,std::uint64_t){
        ++notifications;
        const auto* entity=observed.find(1);
        complete=entity&&entity->attributes==std::vector<std::int16_t>(values.begin(),values.end());
        // A notification is post-publication: nested removal cannot interrupt
        // the outer merge or leave the prefix partially written.
        if(entity){observed.remove(1);observed.create(1,odh::EntityKind::object,{6,0,0});}
    }),"install atomic merge observer");
    require(world.merge_attributes_max(reference,values.data(),values.size())&&notifications==1&&complete,
        "one observer sees the complete maximum batch before removing old identity");
    require(world.find(1)->generation!=reference.generation&&world.find(1)->attributes==std::vector<std::int16_t>(6),
        "outer batch never writes into replacement created by postcommit observer");
    const auto revision=world.spatial_revision();
    require(!world.merge_attributes_max(reference,values.data(),values.size())&&world.spatial_revision()==revision,
        "stale generation rejects before mutation or notification");
    require(world.set_spatial_change_observer({}),"remove merge observer");
    const odh::WorldEntityReference fresh{1,world.find(1)->generation};
    require(!world.merge_attributes_max(fresh,nullptr,1)&&!world.merge_attributes_max(fresh,values.data(),7)&&
        world.spatial_revision()==revision,"null and oversized prefixes reject without allocating or publishing");
    require(world.merge_attributes_max(fresh,nullptr,0)&&world.spatial_revision()==revision,"empty prefix is a known no-op");
    bool rejected=false;
    require(!world.evaluate_appearance_lookup([&]{rejected=!world.merge_attributes_max(fresh,values.data(),values.size());})&&rejected&&
        world.find(1)->attributes==std::vector<std::int16_t>(6),"read-only callback admission rejects entire batch");
    require(world.merge_attributes_max(fresh,values.data(),values.size()),"valid fresh batch merges");
    const auto after=world.spatial_revision();
    const std::array<std::int16_t,6> lower{-32768,-1,0,1,2,3};
    require(world.merge_attributes_max(fresh,lower.data(),lower.size())&&world.spatial_revision()==after,
        "signed max never lowers a word and unchanged batch sends no notification");
}
void indirect_variable_generations() {
    odh::World world;
    require(world.create(1,odh::EntityKind::avatar,{3,88,0,16}),"create indirect variable actor");
    require(world.set_motive(1,2,44),"initialize indirect motive");
    const odh::Bhav code{0x8002,{{2,0,255,{19,0,0,0,0,5,8,17}}},{},
        odh::Bhav::Metadata{0,4,4,0}};
    odh::Vm vm({{odh::routine_key(4096),code}},world.vm_memory());
    vm.start(odh::routine_key(4096),{1,1,1},{2});vm.temps()[0]=2;
    std::vector<std::unique_ptr<odh::Vm>> writers;
    for(const auto scope:{17,22,41}) {
        odh::Bhav write{0x8002,{{2,254,255,{0,0,99,0,0,5,static_cast<std::uint8_t>(scope),7}}},{},
            odh::Bhav::Metadata{0,4,4,0}};
        writers.push_back(std::make_unique<odh::Vm>(
            odh::RoutineSet{{odh::routine_key(4096),write}},world.vm_memory()));
        writers.back()->start(odh::routine_key(4096),{1,1,1},{2});writers.back()->temps()[0]=2;
    }
    const auto bound=vm.stack().back();
    require(odh::read_ts1_variable(world,world.vm_memory(),bound,vm.temps(),17,0)==44,
        "shared variable helper reads indirect motive through bound World identity");
    require(vm.run(1)==odh::VmStatus::budget_exhausted&&vm.temps()[19]==44,
        "VM reads same canonical motive as shared helper");
    require(world.remove(1)&&world.create(1,odh::EntityKind::avatar,{3,88,0,16}),
        "reuse numeric ID with a fresh generation");
    for(const auto scope:{17,22,41}) {
        require(!odh::read_ts1_variable(world,world.vm_memory(),bound,vm.temps(),
            static_cast<std::uint8_t>(scope),0),"indirection cannot bind a stale StackObject generation");
    }
    require(vm.run(1)==odh::VmStatus::fault,"VM indirect read rejects reused entity ID");
    for(auto& writer:writers)require(writer->run(1)==odh::VmStatus::fault,
        "VM indirect writes reject stale generations instead of modifying replacement");
    require(world.find(1)->attributes[2]==0&&world.find(1)->motives[2]==0,
        "reused entity banks remain unchanged by stale writers");
    // Frame-local reads require no live entity, and resolving never mutates banks.
    auto local=bound;local.locals={10,20,30};auto temps=vm.temps();temps[0]=2;
    require(odh::read_ts1_variable(world,{},local,temps,40,0)==30&&temps[0]==2&&local.locals[2]==30,
        "LocalByTemp remains frame-local and nonmutating after entity retirement");
    for(const auto scope:{17,22,40,41}) {
        require(!odh::read_ts1_variable(world,{},local,temps,static_cast<std::uint8_t>(scope),-1),
            "shared helper rejects negative selectors");
        auto bad=temps;bad[0]=-1;auto args=local;args.arguments={-1};
        require(!odh::read_ts1_variable(world,{},args,bad,static_cast<std::uint8_t>(scope),0),
            "shared helper rejects negative indirect addresses");
    }
    require(odh::read_ts1_variable(world,{},local,temps,7,-1)==-1,
        "signed direct literals retain their meaning");
}
void retired_container_contents(){
    odh::World world;
    require(world.create(1,odh::EntityKind::object,{0,0,3})&&
        world.create(2,odh::EntityKind::object,{0,0,1})&&world.create(3,odh::EntityKind::object,{})&&
        world.create(4,odh::EntityKind::object,{}),"create retained container identities");
    require(world.set_position(1,{32,48,1,6})&&world.set_position(2,{64,80,1,2})&&
        world.set_position(3,{96,48,1,4})&&world.set_position(4,{80,88,1,3}),"position every source item before positioned slot publication");
    require(world.place_in_slot(2,0,4),"positioned nested child slot");
    require(world.place_in_slot(1,0,2),"positioned first child slot");
    require(world.place_in_slot(1,2,3),"positioned later child slot");
    const odh::WorldEntityReference parent{1,world.find(1)->generation};
    const odh::WorldEntityReference child{2,world.find(2)->generation};
    const auto original=world.find(2)->position;
    odh::World::RetiredContainer expired;
    require(world.remove_retaining_contents(parent,[&](const auto& token){
        expired=token;
        require(!world.find(1)&&world.resolve_reference(parent)&&world.retired_container_active(token),"parent index removed before children");
        require(world.retired_slot_count(token)==std::optional<std::size_t>{3}&&
            world.retired_slot(token,0)==std::optional<odh::WorldEntityReference>{child}&&
            world.retired_slot(token,1)==std::optional<odh::WorldEntityReference>{odh::WorldEntityReference{}},"slot extent and holes remain source-shaped");
        odh::World refused;require(!refused.restore_snapshot(world.snapshot()),"partial containment cannot restore as a valid save");
        require(!world.collect_retained_bodies({}),"GC cannot remove a parent still referenced by children");
        require(world.create(1,odh::EntityKind::object,{0,0,3}),"numeric parent ID immediately reusable");
        require(world.attachment_parent(child)==std::optional<odh::WorldEntityReference>{parent}&&
            world.copy_retired_position(token,child),"old attachment resolves retained generation rather than replacement");
        require(world.resolve_reference(parent)->position->direction==6&&
            world.find(2)->position==original&&world.find(4)->attachment->container==2,"parent copy preserves heading and nested live subtree");
        require(!world.detach_retained_slot(token,0,{2,child.generation+1})&&
            world.detach_retained_slot(token,0,child)&&world.retired_child_authorized(token,child),"detach exact child in actual source phase");
        require(!world.find(2)->attachment&&world.find(4)->attachment&&
            world.find(1)->slots[0]==0,"detach never touches new parent or grandchild");
        const auto third=world.retired_slot(token,2);require(third&&third->id==3,"later slot re-read");
        require(world.remove(3)&&world.create(3,odh::EntityKind::object,{}),"nested callback can remove and reuse later child ID");
        require(world.retired_slot(token,2)==std::optional<odh::WorldEntityReference>{odh::WorldEntityReference{}},"removed old child does not rebind its replacement");
        require(world.create(9,odh::EntityKind::object,{})&&world.remove_retaining_body({9,world.find(9)->generation}),"independent collectible body");
        std::vector<odh::WorldEntityReference> collected;
        require(world.collect_retained_bodies({},&collected)&&collected.size()==1&&collected.front().id==9&&
            world.resolve_reference(parent)&&world.retired_container_active(token),"exact active lease pins parent after last detach while other dead bodies collect");
        return true;
    }),"complete source container removal");
    require(!world.retired_container_active(expired)&&!world.retired_child_authorized(expired,child)&&
        !world.copy_retired_position(expired,child)&&!world.retired_slot_count(expired),"all retained capabilities expire after scope");
    odh::World resumed;require(resumed.restore_snapshot(world.snapshot())&&resumed.find(2)&&
        resumed.find(4)->attachment&&resumed.resolve_reference(parent),"surviving child and retained parent restore together");
    require(world.collect_retained_bodies({})&&!world.resolve_reference(parent),"parent becomes collectible only after Delete continuation lease expires");
    odh::World unsupported;require(unsupported.create(7,odh::EntityKind::object,{0,0,1})&&
        unsupported.create(8,odh::EntityKind::object,{})&&unsupported.attach(7,0,8),"failure fixture with reciprocal unpositioned containment");
    const odh::WorldEntityReference dead{7,unsupported.find(7)->generation};
    require(!unsupported.remove_retaining_contents(dead,[&](const auto& token){expired=token;return false;})&&
        !unsupported.find(7)&&unsupported.resolve_reference(dead)&&!unsupported.retired_container_active(expired),"unsupported preserves already removed body and expires capability");
}
void source_direction_setters(){
    odh::World world;
    require(world.create(1,odh::EntityKind::avatar,{0,88,1,16})&&world.create(2,odh::EntityKind::object,{0,0,1})&&
        world.create(3,odh::EntityKind::object)&&world.declare_object_word(1,1,0)&&
        world.declare_object_word(2,1,2)&&world.declare_object_word(2,23,2)&&
        world.declare_object_word(3,1,5)&&world.declare_object_word(3,23,1)&&
        world.set_position(1,{80,96,1,0})&&world.set_position(2,{16,32,1,2})&&
        world.set_position(3,{16,32,1,5})&&world.attach(2,0,3),"source heading setter family has real constructor fields and contained child");
    const auto object_generation=world.find(2)->generation;unsigned calls=0;bool publishing=false,observed=false;
    std::function<bool()> retained_callback;
    require(world.set_direction_publisher([&](odh::World& emitter,const odh::VmFrame&,auto target,auto heading,const auto& fixed){
        require(&emitter==&world&&target.id==2&&target.generation==object_generation&&heading<=7,"semantic direction event owns exact reference and effective quantized heading");
        ++calls;retained_callback=fixed;publishing=true;const bool done=fixed();publishing=false;return done;
    })&&world.set_spatial_change_observer([&](odh::World&,std::uint64_t){if(publishing)observed=true;}),"source semantic publisher binds without a fabricated primitive");
    const auto child=*world.find(3)->position;
    require(world.write(frame(1,2),4,1,-1)&&world.find(2)->position->direction==6&&
        world.find(2)->position->x==16&&world.find(2)->position->y==32&&world.find(2)->radian_direction==0&&
        *world.find(3)->position==child&&calls==1&&observed,"loose object normalizes ushort then RotationNotches without movement, child propagation or raw-radian changes");
    require(!retained_callback(),"retained semantic setter callback expires before source locals do");
    require(world.attach(1,0,2),"object carried by real reciprocal avatar slot");
    const auto held=*world.find(2)->position;
    require(world.write(frame(2,2),3,1,9)&&world.find(2)->position->direction==0&&
        world.find(2)->position->x==held.x&&world.find(2)->position->y==held.y&&
        world.find(2)->attachment->container==1&&world.find(1)->slots[0]==2&&world.find(3)->position->direction==5,
        "held ordinary object's own direction changes through Caller without detaching or rotating its child");
    require(world.detach(1,0)&&world.place_individual_on_floor(2,{},0)&&world.write(frame(2,2),3,1,7)&&
        !world.find(2)->position&&world.read(frame(2,2),3,1)==6,"OOW object has the same normalized quantized direction setter");
    const auto calls_before=calls;
    require(world.set_direction_publisher({})&&world.write(frame(1,1),3,1,-1)&&world.read(frame(1,1),3,1)==7&&
        world.find(1)->radian_direction>5.f,"avatar uses common signed-short normalization and source notch radians");
    odh::VmContext old_context{2,2,2};require(world.bind_context_live(old_context),"capture exact old object reference before deletion");
    odh::VmFrame old_frame;old_frame.context=old_context;
    require(world.remove_retaining_body({2,object_generation})&&world.create(2,odh::EntityKind::object)&&
        world.declare_object_word(2,1,1)&&world.declare_object_word(2,23,1)&&
        world.write(old_frame,3,1,3)&&world.resolve_reference({2,object_generation})->object_words.at(1)==2&&
        world.find(2)->object_words.at(1)==1&&calls==calls_before,"retained exact body rotates without addressing a reused live ID");
    require(world.set_direction_publisher([](auto&,const auto&,auto,auto,const auto&){return true;})&&
        !world.write(frame(2,2),3,1,4)&&world.find(2)->object_words.at(1)==1,"publisher cannot claim a direction without running the fixed setter");
    require(world.set_direction_publisher([](auto&,const auto&,auto,auto,const auto& fixed){const bool first=fixed();return fixed()||first;})&&
        !world.write(frame(2,2),3,1,4)&&world.find(2)->object_words.at(1)==4,"repeated callback is rejected while retaining the first actual source write");
    require(world.set_direction_publisher([](auto&,const auto&,auto,auto,const auto& fixed)->bool{fixed();throw std::runtime_error("host rejection");}),"bind throwing direction publisher");
    bool threw=false;try{world.write(frame(2,2),3,1,5);}catch(const std::runtime_error&){threw=true;}
    require(threw&&world.find(2)->object_words.at(1)==5&&world.set_direction_publisher({}),"throw preserves the published heading and expires publication scope");
    require(world.set_direction_publisher([&](auto& emitter,const auto&,auto target,auto,const auto& fixed){
        emitter.set_position(target.id,{48,48,1,5});return fixed();
    })&&!world.write(frame(2,2),3,1,7)&&world.find(2)->position->direction==5,
        "fixed setter rejects an unrelated physical mutation before publication");
}
void runtime_constructor_observation(){
    odh::World world;unsigned calls=0;std::uint64_t generation=0;bool valid=true;
    const auto revision=world.spatial_revision();
    require(world.set_spatial_change_observer([&](const odh::World& current,std::uint64_t seen){
        ++calls;const auto* object=current.find(7);
        valid=valid&&object&&object->definition&&object->definition->guid==123&&seen==current.spatial_revision();
        if(!object)return;
        if(calls==1){valid=valid&&!object->position;generation=object->generation;}
        else valid=valid&&object->generation==generation&&object->position.has_value();
    })&&world.create_runtime_object(7,123,{},{}),"publish runtime constructor AddEntity event");
    require(valid&&calls==1&&generation&&world.spatial_revision()==revision+1,
        "constructor emits one actual OOW identity observation without a second revision increment");
    require(world.set_position(7,{16,32,1,2})&&valid&&calls==2,
        "first position is a separate publication after the OOW constructor baseline");
}
void retained_reference_bodies(){
    odh::World world;
    require(world.create(137,odh::EntityKind::avatar,{0,88,1})&&world.create(181,odh::EntityKind::object,{2,0,0})&&
        world.declare_object_word(181,60,21)&&world.declare_object_word(181,8,0),"create retained Main StackObject body fixture");
    odh::VmFrame main{odh::routine_key(8233),0,{137,137,181,137},{},{}};
    require(world.bind_context_live(main.context),"bind original frame roles before deletion");
    const auto original=odh::vm_context_reference(main.context,odh::VmEntityRole::stack_object);
    unsigned dead_footprint_calls=0;
    require(world.set_footprint_observer([&](const odh::World& observed,std::int16_t id,std::uint64_t generation){
        ++dead_footprint_calls;
        return id==181&&generation==original.generation&&observed.find(id)&&observed.reference_is_dead(original);
    })&&world.mark_dead(original)&&world.find(181)&&world.reference_is_dead(original),
        "source Dead flag precedes actual dictionary removal");
    const auto dead_revision=world.spatial_revision();
    require(world.write(main,4,8,4)&&dead_footprint_calls==1&&world.spatial_revision()>dead_revision&&
        world.write(main,4,8,0)&&dead_footprint_calls==2,
        "indexed Dead body still runs ordinary Flags footprint callbacks before Remove");
    require(world.set_footprint_observer({}),"release indexed Dead footprint fixture observer");
    const auto words=world.snapshot().word_count;
    require(world.remove_retaining_body(original)&&!world.find(181)&&world.reference_is_dead(original)&&
        world.resolve_reference(original)&&world.snapshot().word_count==words,"removed body is retained outside the live index without losing its budget");
    require(world.create(181,odh::EntityKind::object,{2,0,0})&&world.declare_object_word(181,60,99),"new removal permits immediate numeric ID reuse");
    const auto replacement=world.find(181)->generation;
    require(replacement!=original.generation&&world.frame_entity(main,odh::VmEntityRole::stack_object)->generation==original.generation&&
        !world.frame_entity(main,odh::VmEntityRole::stack_object,true),"existing frame resolves the old generation while live-only mutations refuse it");
    const auto revision=world.snapshot().spatial_revision;
    require(world.read(main,4,60)==std::optional<std::int16_t>{21}&&world.write(main,4,60,22)&&
        world.write(main,1,0,7)&&world.read(main,1,0)==std::optional<std::int16_t>{7}&&
        world.find(181)->object_words.at(60)==99&&world.find(181)->attributes[0]==0&&
        world.snapshot().spatial_revision==revision,"dead bank reads/writes mutate only the retained body without live spatial publication");
    require(world.write(main,4,8,static_cast<std::int16_t>(0x8000))&&!world.resolve_reference(original)->static_footprint&&
        world.find(181)->static_footprint&&!world.write(main,4,8,static_cast<std::int16_t>(0x8004)),
        "retained Flags preserve class assignment while unrepresented footprint side effects reject without changing a replacement");
    odh::VmContext nil{137,137,42,137};require(world.bind_context_live(nil)&&nil.entity_references_bound&&!nil.stack_object_generation,
        "new missing numeric lookup is explicit nil");
    require(world.create(42,odh::EntityKind::object),"create object after a numeric nil was captured");
    odh::VmFrame nil_frame;nil_frame.context=nil;
    require(!world.frame_entity(nil_frame,odh::VmEntityRole::stack_object),"positive explicit nil never resolves a later object with that number");
    odh::VmContext legacy{137,181,42,137},before=legacy;
    require(!odh::vm_context_set_reference(legacy,odh::VmEntityRole::stack_object,original)&&odh::vm_context_equal(legacy,before),
        "partial setter cannot convert unrelated legacy roles into bound nil");
    require(world.bind_context_live(legacy)&&legacy.caller_generation&&legacy.callee_generation==replacement&&legacy.stack_object_generation==world.find(42)->generation,
        "context binding captures all three different roles atomically");
    require(odh::vm_context_copy_reference(legacy,odh::VmEntityRole::callee,main.context,odh::VmEntityRole::stack_object)&&
        odh::vm_context_reference(legacy,odh::VmEntityRole::callee)==original,"role propagation preserves the existing old-generation reference");
    require(!world.collect_retained_bodies({{181,replacement+100}})&&world.resolve_reference(original),"unknown collector roots cannot discard retained bodies");
    require(world.collect_retained_bodies({original})&&world.resolve_reference(original)&&world.collect_retained_bodies({})&&
        !world.resolve_reference(original)&&!world.frame_entity(main,odh::VmEntityRole::stack_object)&&world.find(181)->generation==replacement,
        "safe-point collection releases only unreferenced bodies and never falls back to a replacement");
    odh::World limited({2,100,10});require(limited.create(1,odh::EntityKind::object)&&limited.create(2,odh::EntityKind::object),"fill aggregate live-body capacity");
    const auto generation=limited.find(2)->generation;
    require(limited.remove_retaining_body({2,generation})&&!limited.create(2,odh::EntityKind::object)&&
        limited.collect_retained_bodies({})&&limited.create(2,odh::EntityKind::object),"retained bodies count toward the same finite entity budget");
}
void position_room_publication_tests(){
    odh::World world;
    require(world.create(1,odh::EntityKind::avatar,{0,88,1})&&world.create(2,odh::EntityKind::object,{0,0,1})&&
        world.create(3,odh::EntityKind::object)&&world.create(4,odh::EntityKind::object,{0,0,1})&&
        world.set_position(1,{16,16,1,0})&&world.set_position(2,{20,16,1,3})&&
        world.set_position(3,{22,16,1,5})&&world.set_position(4,{80,16,1,0})&&
        world.place_in_slot(2,0,3)&&world.place_in_slot(1,0,2)&&world.declare_object_word(1,29,99),
        "build positioned avatar carried object and nested child before geometry binding");
    unsigned calls=0;
    const auto rooms=[&](const odh::EntityPosition& point)->std::optional<std::uint16_t>{
        ++calls;return point.level==2?6:point.x<64?2:4;};
    const auto before_bind=world.snapshot();
    require(world.set_room_resolver(rooms)&&world.snapshot().spatial_revision==before_bind.spatial_revision&&
        world.word_count()==before_bind.word_count&&world.read(frame(1,1),3,29)==99&&calls==0,
        "runtime room capability binds without repairing old saved fields or changing serialized state");
    require(world.set_radian_direction(1,0.2f)&&world.read(frame(1,1),3,29)==99&&calls==0,
        "heading-only publication does not perform source PositionChange or overwrite writable Room29");
    const auto words=world.word_count();
    require(world.set_position(1,*world.find(1)->position)&&calls==3&&world.word_count()==words+2&&
        world.read(frame(1,1),3,29)==1&&world.read(frame(2,2),3,29)==1&&world.read(frame(3,3),3,29)==1,
        "same-pose PositionChange atomically adds missing metadata and updates every positioned descendant");
    require(world.set_position(1,{80,32,2,6})&&world.read(frame(1,1),3,29)==5&&
        world.read(frame(2,2),3,29)==5&&world.read(frame(3,3),3,29)==5&&
        world.find(2)->position->direction==3&&world.find(3)->position->direction==5,
        "cross-room and floor movement refreshes the complete tree while preserving child headings");
    require(world.write(frame(1,1),3,29,123)&&world.read(frame(1,1),3,29)==123&&
        world.set_position(1,*world.find(1)->position)&&world.read(frame(1,1),3,29)==5,
        "Room29 remains writable until the next actual PositionChange, not dynamically replaced on read");
    require(world.create(6,odh::EntityKind::object)&&world.declare_object_word(6,29,66)&&world.set_ui_ghost(6,true),
        "create source ghost with its own historical Room word");
    const auto before_ghost=calls;
    require(world.set_position(6,{96,48,1,0})&&calls==before_ghost&&world.read(frame(6,6),3,29)==66,
        "source ghosts change their pose without SetRoom or geometry lookup");
    odh::World restored;
    require(restored.restore_snapshot(world.snapshot())&&restored.read(frame(1,1),3,29)==5&&
        restored.set_room_resolver(rooms)&&restored.set_position(1,{32,48,1,2})&&restored.read(frame(3,3),3,29)==1,
        "fresh World restores stored rooms exactly then binds runtime geometry for later movement");
    require(world.move_to_slot(4,0,2,2)&&world.find(1)->slots[0]==0&&world.find(4)->slots[0]==2&&
        world.read(frame(2,2),3,29)==3&&world.read(frame(3,3),3,29)==3&&world.find(3)->position->direction==5,
        "slot transfer publishes contained subtree rooms with reciprocal containment and stable child direction");
    const auto unchanged=[&](const odh::WorldSnapshot& before){const auto after=world.snapshot();
        require(before.generation_sequence==after.generation_sequence&&before.spatial_revision==after.spatial_revision&&
            before.word_count==after.word_count&&before.entities.size()==after.entities.size(),
            "failed room publication preserves identities counters and word budget");
        for(std::size_t n=0;n<before.entities.size();++n){const auto& a=before.entities[n];const auto& b=after.entities[n];
            require(a.position==b.position&&a.slots==b.slots&&a.object_words==b.object_words&&
                a.attachment.has_value()==b.attachment.has_value()&&
                (!a.attachment||(a.attachment->container==b.attachment->container&&a.attachment->slot==b.attachment->slot)),
                "failed room publication leaves positions rooms and containment unchanged");}
    };
    require(world.set_room_resolver([](const auto&)->std::optional<std::uint16_t>{return {};}),"bind explicit unknown geometry");
    auto before=world.snapshot();const auto registration=world.room_registration_revision();
    require(!world.set_position(1,{96,48,1,0})&&!world.place_on_floor(2,{96,48,1,0})&&
        !world.move_to_slot(1,0,2)&&!world.place_in_slot(1,0,2)&&!world.prepare_move_to_slot(1,0,2)&&
        world.room_registration_revision()==registration,"unknown room rejects all position/slot paths before coordinates or detach");unchanged(before);
    require(world.set_room_resolver([](const auto&)->std::optional<std::uint16_t>{throw std::runtime_error("unknown rooms");})&&
        !world.set_position(1,{96,48,1,0}),"resolver exceptions fail before publication");unchanged(before);
    for(unsigned kind=0;kind<5;++kind){
        require(world.set_room_resolver([&,kind](const auto&)->std::optional<std::uint16_t>{
            if(kind==0)world.set_position(4,{112,16,1,0});
            if(kind==1)world.declare_object_word(1,29,999);
            if(kind==2)world.create(99,odh::EntityKind::object);
            if(kind==3)world.set_room_resolver(rooms);
            if(kind==4)(void)world.read(frame(1,1),3,25);
            return 0;
        })&&!world.set_position(1,{96,48,1,0}),
            "reentrant World mutation or stateful lockout read cannot authorize fake room zero");unchanged(before);
    }
    require(world.set_room_resolver([](const auto&)->std::optional<std::uint16_t>{return 32769;})&&
        !world.set_position(1,{96,48,1,0}),"room outside signed stored-field domain is unsupported");unchanged(before);
    require(world.set_room_resolver([](const auto&)->std::optional<std::uint16_t>{return 0;})&&
        world.set_position(1,{96,48,1,0})&&world.read(frame(1,1),3,29)==-1,
        "known geometric room zero is preserved as source outdoor Room minus one");
    for(const auto budget:{2U,3U}){
        odh::WorldLimits limits;limits.words=budget;odh::World bounded(limits);
        require(bounded.create(1,odh::EntityKind::object,{0,0,1})&&bounded.create(2,odh::EntityKind::object)&&
            bounded.set_position(1,{16,16,1,0})&&bounded.set_position(2,{20,16,1,0})&&bounded.place_in_slot(1,0,2)&&
            bounded.set_room_resolver([](const auto&)->std::optional<std::uint16_t>{return 2;}),"bounded missing Room metadata fixture");
        const auto initial=bounded.snapshot();const auto accepted=bounded.set_position(1,{32,16,1,0});
        require(accepted==(budget==3)&&bounded.word_count()==(budget==3?3:1)&&
            (accepted?bounded.read(frame(2,2),3,29)==1:bounded.snapshot().spatial_revision==initial.spatial_revision&&
                bounded.find(1)->position==initial.entities[0].position&&!bounded.find(1)->object_words.count(29)&&
                !bounded.find(2)->object_words.count(29)),"all missing Room nodes fit together or reject atomically without partial insertion");
    }
    odh::World slots;
    require(slots.create(1,odh::EntityKind::object,{0,0,1})&&slots.create(2,odh::EntityKind::object,{0,0,1})&&
        slots.create(3,odh::EntityKind::object)&&slots.create(5,odh::EntityKind::object,{0,0,1})&&
        slots.set_position(1,{16,16,1,0})&&slots.set_position(2,{80,16,1,0})&&slots.set_position(3,{24,16,1,3})&&
        slots.declare_object_word(3,1,3)&&slots.place_in_slot(1,0,3)&&slots.set_room_resolver(rooms),"copyable prepared room publication fixture");
    auto plan=slots.prepare_move_to_slot(2,0,3);require(plan.has_value(),"prepare allocates missing Room node privately");
    const auto prepared_words=slots.word_count();auto copied=*plan;
    require(slots.word_count()==prepared_words&&slots.commit_move_to_slot(*plan)&&slots.word_count()==prepared_words+1&&
        slots.read(frame(3,3),3,29)==3&&!slots.can_commit_move_to_slot(copied)&&!slots.commit_move_to_slot(copied),
        "copied prepared moves share private nodes safely and only first commit publishes a Room word");
    auto stale=slots.prepare_move_to_slot(1,0,3);require(stale.has_value(),"prepare against original geometry capability");
    const auto bound_state=slots.snapshot();
    require(slots.set_room_resolver([](const auto&)->std::optional<std::uint16_t>{return 6;})&&
        slots.snapshot().spatial_revision==bound_state.spatial_revision&&!slots.can_commit_move_to_slot(*stale)&&
        !slots.commit_move_to_slot(*stale),"changing geometry invalidates prepared capability without changing serialized fields");
    auto oow=slots.prepare_move_to_slot(5,0,3);
    require(oow&&slots.commit_move_to_slot(*oow)&&!slots.find(3)->position&&slots.find(3)->object_words.at(29)==-1&&
        slots.read(frame(3,3),3,29)==-5,"OOW slot publication stores room minus one while GetValue retains source minus-five sentinel");
    require(slots.find(3)->attachment->positioned&&slots.write(frame(3,3),3,29,123)&&
        slots.set_position(5,{96,48,2,4})&&slots.find(3)->position&&slots.find(3)->position->x==96&&
        slots.find(3)->position->y==48&&slots.find(3)->position->level==2&&slots.find(3)->position->direction==3&&
        slots.read(frame(3,3),3,29)==5&&slots.read(frame(5,5),3,29)==5,
        "physical OOW containment follows later parent movement and refreshes writable child Room29 and preserved direction");
}
void room_registration_tests(){
    odh::WorldLimits limits;limits.room_registration_events=3;odh::World world(limits);
    require(world.create(1,odh::EntityKind::avatar,{1,88,0})&&world.create(2,odh::EntityKind::object)&&
        world.set_position(1,{16,16,1,0})&&world.set_position(2,{40,16,1,0})&&world.declare_object_word(1,8,0),
        "create runtime registration history identities");
    const auto initial=*world.room_registration_revision();
    require(world.write(frame(1,1),3,8,16)&&world.write(frame(1,1),0,0,27)&&
        world.set_radian_direction(1,0.37f)&&world.set_static_footprint(1,false)&&
        world.set_avatar_kill_timeout(1,0)&&world.room_registration_revision()==initial,
        "flags, attributes, heading, obstacle collection and kill timer do not change RoomInfo.Entities history");
    require(world.set_position(1,*world.find(1)->position),"same-pose source PositionChange still registers its entity");
    std::vector<odh::WorldRoomRegistrationEvent> events;
    require(world.room_registration_events_since(initial,events)&&events.size()==1&&events[0].entity==1&&
        events[0].generation==world.find(1)->generation&&events[0].before==events[0].after,
        "same-pose remove/reinsert remains observable even with identical final position");
    require(world.set_position(2,{20,16,1,0})&&world.set_position(2,{40,16,1,0})&&
        world.room_registration_events_since(initial,events)&&events.size()==3&&
        events[1].before->x==40&&events[1].after->x==20&&events[2].before->x==20&&events[2].after->x==40,
        "bounded history preserves outsider room entry and exit despite net-zero final state");
    require(!world.room_registration_events_since(0,events)&&events.empty(),"evicted registration history is unavailable, never a partial proof");
    const auto current=*world.room_registration_revision();
    require(world.room_registration_events_since(current,events)&&events.empty(),"unchanged registration serial has a complete empty history");
    require(world.create(3,odh::EntityKind::object,{0,0,1})&&world.set_position(3,{24,16,1,0}),
        "create source slot destination for registration publication");
    auto transfer=world.prepare_move_to_slot(3,0,2);const auto before_transfer=*world.room_registration_revision();
    require(transfer&&world.commit_move_to_slot(*transfer)&&world.room_registration_events_since(before_transfer,events)&&
        events.size()==1&&events[0].entity==2&&events[0].before->x==40&&events[0].after->x==24,
        "prepared slot publication records registration without allocating during commit");
    const auto saved=world.snapshot();odh::World restored(limits);
    require(restored.restore_snapshot(saved)&&restored.room_registration_events_since(0,events)&&
        events.size()==1&&events[0].all_rooms,"whole snapshot publication starts fresh runtime history and invalidates existing room watches");
}
void lockout_count_tests(){
    odh::World world;const auto f=frame(1,1);
    require(world.create(1,odh::EntityKind::object)&&world.create(2,odh::EntityKind::object,{0,0,1})&&
        world.create(3,odh::EntityKind::object)&&world.declare_object_word(1,25,12)&&
        world.declare_object_word(3,1,0)&&world.set_position(2,{10,10,1,0}),"lockout known raw banks and transfer proof fixture");
    require(world.read(f,3,25)==12&&world.declare_object_word(1,25,-5)&&world.read(f,4,25)==-5,
        "default legacy model preserves raw signed LockoutCount compatibility");
    require(world.declare_object_word(1,25,12)&&world.set_lockout_count_clock(100)&&
        !world.read(f,3,25)&&!world.write(f,3,25,0)&&world.find(1)->object_words.at(25)==12,
        "strict model never invents Thread presence or silently writes an unknown stamp");
    const auto generation=world.find(1)->generation;
    require(world.admit_lockout_count_state(1,generation,true)&&!world.read(f,3,25)&&
        world.declare_object_word(1,25,-5)&&world.read(f,3,25)==0&&
        world.declare_object_word(1,25,0)&&world.read(f,4,25)==0&&
        !world.find(1)->lockout_count->timestamp,
        "known threaded nonpositive counts prove zero while unknown history stays unknown");
    require(world.declare_object_word(1,25,12)&&world.admit_lockout_count_state(1,generation,true,90)&&
        world.read(f,3,25)==2&&world.read(f,4,25)==2,"both ObjectData scopes subtract source elapsed ticks");
    const auto spatial=world.spatial_revision();const auto words=world.word_count();
    auto plan=world.prepare_move_to_slot(2,0,3);
    require(plan&&world.set_lockout_count_clock(103)&&!world.can_commit_move_to_slot(*plan)&&
        world.spatial_revision()==spatial&&world.word_count()==words&&world.read(f,3,25)==0&&
        world.find(1)->object_words.at(25)==12,"clock invalidates prepared publications without rewriting raw count or spatial caches");
    require(world.write(f,3,25,12)&&world.read(f,3,25)==12&&
        world.find(1)->lockout_count->timestamp==std::optional<std::uint32_t>{103}&&world.spatial_revision()==spatial,
        "same-value source write stamps the current tick without a spatial change");
    require(world.set_lockout_count_clock(105)&&world.read(f,3,25)==10&&world.write(f,4,25,12)&&
        world.read(f,3,25)==12&&world.find(1)->lockout_count->timestamp==std::optional<std::uint32_t>{105},
        "every repeated same-value write restarts elapsed lockout timing");
    require(world.set_lockout_count_clock(110)&&world.declare_object_word(1,25,99)&&world.read(f,3,25)==94&&
        world.find(1)->lockout_count->timestamp==std::optional<std::uint32_t>{105},
        "raw declaration/import is not a timestamped SetValue operation");
    require(world.create(4,odh::EntityKind::object)&&world.declare_object_word(4,25,20)&&
        world.admit_lockout_count_state(4,world.find(4)->generation,true,1000),"future timestamp fixture");
    auto future_plan=world.prepare_move_to_slot(2,0,3);const auto future_spatial=world.spatial_revision();
    const odh::World& constant=world;
    require(future_plan&&constant.read(frame(4,4),3,25)==0&&
        world.find(4)->lockout_count->timestamp==std::optional<std::uint32_t>{0}&&
        world.find(4)->object_words.at(25)==20&&world.spatial_revision()==future_spatial&&
        !world.can_commit_move_to_slot(*future_plan),"const GetValue resets future timestamp logically and invalidates prepared proof only");
    require(world.set_lockout_count_clock(UINT32_MAX)&&world.write(f,3,25,10)&&
        world.find(1)->lockout_count->timestamp==std::optional<std::uint32_t>{UINT32_MAX}&&
        world.set_lockout_count_clock(0)&&world.read(f,3,25)==10&&world.find(1)->lockout_count->timestamp==std::optional<std::uint32_t>{0},
        "uint32 scheduler wrap follows the source future-timestamp reset rather than unsigned elapsed guessing");
    require(world.create(5,odh::EntityKind::object)&&world.declare_object_word(5,25,-2)&&
        world.admit_lockout_count_state(5,world.find(5)->generation,false,7)&&
        !world.read(frame(5,5),3,25)&&world.write(frame(5,5),3,25,-8)&&
        world.find(5)->object_words.at(25)==-8&&world.find(5)->lockout_count->timestamp==std::optional<std::uint32_t>{7},
        "strict null-Thread read stays unsupported while setter retains raw value without a stamp");
    const auto previous=world.find(4)->generation;
    require(world.remove(4)&&world.create(4,odh::EntityKind::object)&&world.declare_object_word(4,25,20)&&
        !world.admit_lockout_count_state(4,previous,true,0)&&!world.find(4)->lockout_count,
        "recycled IDs cannot inherit or admit an earlier generation's timestamp");
    require(!world.admit_lockout_count_state(1,generation,false,0)&&
        !world.admit_lockout_count_state(1,generation,true,9)&&world.admit_lockout_count_state(1,generation,true)&&
        world.find(1)->lockout_count->timestamp==std::optional<std::uint32_t>{0},
        "admission never overwrites established Thread proof or runtime timestamp");
    require(world.create(6,odh::EntityKind::object)&&world.set_ui_ghost(6,true)&&!world.next_entity(5)&&
        world.next_entity(5,{},true)==6&&world.next_entity(5,odh::EntityKind::object,true)==6,
        "cheap identity inventory can explicitly include UI ghosts while ordinary enumeration remains unchanged");
    auto malformed=world.snapshot();malformed.lockout_count_clock.reset();odh::World invalid;
    require(!invalid.restore_snapshot(malformed)&&!world.publish_snapshot(malformed),
        "timestamp metadata requires its clock and active strict model cannot silently revert to raw compatibility");
}
void prepared_slot_moves(){
    odh::World world;
    require(world.create(1,odh::EntityKind::avatar,{0,88,3})&&
        world.create(2,odh::EntityKind::object,{0,0,2})&&world.create(3,odh::EntityKind::object,{0,0,1})&&
        world.create(4,odh::EntityKind::object,{0,0,1})&&world.create(5,odh::EntityKind::avatar,{0,88,1})&&
        world.create(6,odh::EntityKind::object)&&world.create(7,odh::EntityKind::object,{0,0,1})&&
        world.set_position(1,{80,96,2,6})&&world.set_position(2,{16,24,1,2})&&
        world.set_position(3,{16,24,1,3})&&world.set_position(4,{55,66,1,4})&&
        world.set_position(5,{55,66,1,5})&&world.set_position(6,{16,24,1,7})&&
        world.declare_object_word(2,1,2)&&world.declare_object_word(3,1,3)&&
        world.declare_object_word(4,1,4)&&world.declare_object_word(6,1,7)&&world.declare_object_word(7,1,1)&&
        world.attach(2,0,3)&&world.place_in_slot(3,0,6)&&world.attach(4,0,5)&&world.attach(3,0,6)==false,
        "prepare source transfer graph with known independent orientations");
    // Reuse both positioned and initially unrelated, unpositioned child links.
    require(world.detach(3,0)&&world.place_in_slot(3,0,4)&&world.attach(2,1,6)&&
        world.set_radian_direction(5,0.31f),"create deep mixed positioned and unpositioned contained subtree");
    const auto original=world.snapshot();auto move=world.prepare_move_to_slot(1,0,2);
    require(move&&move->container()==1&&move->occupant()==2&&move->slot()==0&&
        move->container_generation()==world.find(1)->generation&&move->occupant_generation()==world.find(2)->generation&&
        !move->source_attachment()&&!move->source_generation()&&!move->already_in_slot()&&
        world.spatial_revision()==original.spatial_revision&&world.find(2)->position==original.entities[1].position,
        "preparation owns complete geometry without publishing attachment or spatial state");
    const auto duplicated=*move;
    require(world.can_commit_move_to_slot(*move)&&world.commit_move_to_slot(*move)&&
        !world.can_commit_move_to_slot(*move)&&!world.can_commit_move_to_slot(duplicated),
        "one effectful publication invalidates both consumed and copied preparations");
    for(const auto id:{2,3,4,5,6}){
        const auto* item=world.find(id);
        require(item->position&&item->position->x==80&&item->position->y==96&&item->position->level==2&&
            item->attachment&&item->attachment->positioned,"source PositionChange propagates ALL contained links");
    }
    require(world.find(2)->position->direction==2&&world.find(3)->position->direction==3&&
        world.find(4)->position->direction==4&&world.find(6)->position->direction==7&&
        world.find(5)->radian_direction==0.31f&&world.find(5)->position->direction==0,
        "slot transfer copies no container direction and preserves continuous avatar angle");
    auto same=world.prepare_move_to_slot(1,0,2);const auto before_same=world.spatial_revision();
    require(same&&same->already_in_slot()&&world.commit_move_to_slot(*same)&&world.spatial_revision()==before_same,
        "already occupied by the same reciprocal entity is a source no-op");
    require(!world.prepare_move_to_slot(4,0,2)&&!world.prepare_move_to_slot(1,0,7)&&
        !world.prepare_move_to_slot(2,0,2)&&!world.prepare_move_to_slot(7,5,2),
        "cycles, occupied slots, self containment and absent slots reject before publication");
    auto oow=world.prepare_move_to_slot(7,0,2);
    require(oow&&oow->source_attachment()&&oow->source_attachment()->container==1&&
        oow->source_generation()==world.find(1)->generation&&world.commit_move_to_slot(*oow)&&world.find(1)->slots[0]==0,
        "prepared transfer removes the exact old reciprocal slot before attaching OOW");
    for(const auto id:{2,3,4,5,6})require(!world.find(id)->position&&world.find(id)->attachment->positioned,
        "OOW container clears physical position through the complete subtree without dropping containment");
    require(world.find(5)->radian_direction==0.31f&&world.read(frame(2,2),3,1)==2,
        "OOW transfer preserves stored object direction and avatar continuous radians");
    odh::World restored;const auto checkpoint=world.snapshot();
    require(restored.restore_snapshot(checkpoint),"both-OOW positioned attachments roundtrip through World snapshot");
    auto malformed=checkpoint;malformed.entities[1].position=odh::EntityPosition{1,1,1,2};
    odh::World invalid;
    require(!invalid.restore_snapshot(malformed),"positioned attachment cannot have only one endpoint OOW");
    require(restored.set_position(7,{100,104,1,1})&&restored.find(2)->position&&
        restored.find(5)->radian_direction==0.31f&&restored.find(3)->position->direction==3,
        "existing movement restores OOW positioned descendants without null dereference or orientation loss");
    auto back=world.prepare_move_to_slot(1,1,2);
    require(back&&world.commit_move_to_slot(*back)&&world.find(2)->position->x==80&&
        world.find(2)->position->direction==2,"OOW to positioned transfer recovers each verified stored direction");
    auto stale=world.prepare_move_to_slot(7,0,2);require(stale.has_value(),"prepare stale identity test");
    require(world.set_position(7,{120,120,1,1}),"change destination after resource preflight");
    const auto changed=world.snapshot();
    require(!world.can_commit_move_to_slot(*stale)&&!world.commit_move_to_slot(*stale)&&
        world.spatial_revision()==changed.spatial_revision&&world.find(1)->slots[1]==2&&world.find(7)->slots[0]==0,
        "destination changes invalidate preparation without partial detach or spatial mutation");
    auto published=world.prepare_move_to_slot(7,0,2);require(published.has_value(),"prepare publication invalidation test");
    require(world.publish_snapshot(world.snapshot())&&!world.can_commit_move_to_slot(*published),
        "snapshot publication invalidates preparations even when serialized spatial revision is unchanged");
    auto replaced=world.prepare_move_to_slot(7,0,2);require(replaced.has_value(),"prepare generation invalidation test");
    require(world.remove(7)&&world.create(7,odh::EntityKind::object,{0,0,1})&&
        !world.commit_move_to_slot(*replaced),"recreated destination cannot reuse a prepared old generation");
    odh::World unrelated;require(!unrelated.can_commit_move_to_slot(*replaced),"plan cannot publish into another World");
    require(world.create(8,odh::EntityKind::object)&&!world.prepare_move_to_slot(1,0,8),
        "unknown OOW object direction is unsupported rather than invented north");
    require(world.set_ui_ghost(8,true)&&!world.prepare_move_to_slot(1,0,8),
        "native live transfer does not silently apply source ghost rendering semantics");
}
}
void legacy_avatar_person_bank(){
    odh::World world;std::array<std::int16_t,68> raw{};raw[3]=-1;raw[11]=1;
    const std::vector<std::int16_t> original(80,-22);
    require(world.create_saved_entity(1,odh::EntityKind::avatar,123,{},original,{},raw,{},1)&&
        world.create(2,odh::EntityKind::avatar,{0,80,1})&&world.create(3,odh::EntityKind::avatar,{0,101,3}),"legacy and constructor person-bank profiles");
    const auto before=world.snapshot();const auto current=frame(1,1);
    for(std::int16_t index=80;index<88;++index)require(world.read(current,18,index)==0&&world.read(current,19,index)==0,
        "all eight omitted TS1 expansion fields have the NBRS zero default");
    require(!world.read(current,18,88)&&!world.read(frame(2,2),18,84)&&
        world.find(1)->person_data==original&&world.word_count()==before.word_count&&world.spatial_revision()==before.spatial_revision,
        "logical tail requires saved provenance and preserves raw80 bytes/accounting; no blanket101 bank");
    odh::World resumed;require(resumed.restore_snapshot(before)&&resumed.find(1)->person_data==original&&resumed.read(current,18,84)==0,
        "legacy person bank restoration never eagerly normalizes data");
    require(world.write(current,19,84,7)&&world.find(1)->person_data.size()==88&&world.word_count()==before.word_count+8&&
        world.read(current,18,84)==7&&world.read(current,18,87)==0&&
        std::equal(original.begin(),original.end(),world.find(1)->person_data.begin()),"real tail write atomically materializes88 and preserves original words");
    require(world.set_person_data(1,87,9)&&world.read(current,18,87)==9&&!world.set_person_data(1,88,1)&&
        world.set_person_data(3,100,44)&&world.find(3)->person_data.size()==101,"TS1 inherited88 and constructor101 retain distinct supported extents");
    odh::WorldLimits limits;limits.words=before.word_count+7;odh::World bounded(limits);
    require(bounded.restore_snapshot(before)&&!bounded.set_person_data(1,84,1)&&bounded.find(1)->person_data==original&&
        bounded.word_count()==before.word_count&&bounded.spatial_revision()==before.spatial_revision,
        "tail growth budget failure preserves the entire original bank and accounting");
    auto opaque=before;opaque.word_count+=176;opaque.entities[0].person_data.resize(256,13);odh::World preserved;
    require(preserved.restore_snapshot(opaque)&&preserved.find(1)->person_data.size()==256&&preserved.find(1)->person_data[200]==13,
        "raw256 history is preserved without invented truncation or projection");
}
void legacy_avatar_slots(){
    const auto avatar=[](odh::World& world){std::array<std::int16_t,68> raw{};raw[3]=-1;raw[11]=1;
        require(world.create_saved_entity(1,odh::EntityKind::avatar,123,{},std::vector<std::int16_t>(80),{},raw,{},1),"legacy saved avatar profile");};
    odh::World world;avatar(world);require(world.create(2,odh::EntityKind::object)&&world.create(3,odh::EntityKind::avatar,{0,88,1}),"slot fixture");
    const auto original=world.snapshot();const auto memory=frame(1,1);
    require(world.read(memory,3,67)==3&&world.read(memory,16,2)==0&&world.read(memory,20,1)==0&&
        !world.read(memory,16,3)&&!world.read(frame(3,2),20,2)&&!world.read(frame(3,3),16,2),
        "only proven imported avatars expose the three source slots");
    require(world.find(1)->slots.size()==1&&world.word_count()==original.word_count&&world.spatial_revision()==original.spatial_revision,
        "logical reads preserve legacy storage, words and revision");
    odh::World resumed;require(resumed.restore_snapshot(original)&&resumed.find(1)->slots.size()==1&&resumed.read(memory,16,2)==0,
        "old checkpoint restores logical slots without resizing");
    require(world.attach(1,2,2)&&world.find(1)->slots==std::vector<std::int16_t>({0,0,2})&&world.word_count()==original.word_count+2&&
        world.find(2)->attachment->slot==2,"physical append materializes exactly three slots and reciprocal occupant");
    auto materialized=world.snapshot();odh::World complete;require(complete.restore_snapshot(materialized)&&complete.read(memory,16,2)==2,
        "new occupied slot survives exact stored reciprocity");
    auto forged=original;forged.entities[1].attachment=odh::Attachment{1,2,false};odh::World rejected;
    require(!rejected.restore_snapshot(forged),"logical empty suffix cannot authorize a dangling unstored occupant");
    odh::World positioned;avatar(positioned);
    require(positioned.create(2,odh::EntityKind::object)&&positioned.set_position(1,{16,16,1,0})&&positioned.set_position(2,{32,16,1,4})&&
        positioned.place_in_slot(1,1,2)&&positioned.find(2)->position->x==16&&positioned.find(1)->slots.size()==3,
        "positioned PlaceInSlot uses head slot and materializes before publication");
    odh::World moving;avatar(moving);
    require(moving.create(2,odh::EntityKind::object)&&moving.create(4,odh::EntityKind::object,{0,0,1})&&
        moving.set_position(1,{16,16,1,0})&&moving.set_position(2,{32,32,1,2})&&moving.set_position(4,{32,32,1,0})&&
        moving.place_in_slot(4,0,2)&&moving.move_to_slot(1,2,2,5)&&moving.find(4)->slots[0]==0&&moving.find(2)->position->direction==5,
        "MoveToSlot stages legacy growth before detaching previous container");
    odh::World prepared;avatar(prepared);
    require(prepared.create(2,odh::EntityKind::object)&&prepared.set_position(1,{16,16,1,0})&&prepared.set_position(2,{32,16,1,2}),"prepared profile");
    auto plan=prepared.prepare_move_to_slot(1,2,2);require(bool(plan),"prepare legacy pelvis slot");auto copied=*plan;
    require(prepared.find(1)->slots.size()==1&&prepared.commit_move_to_slot(*plan)&&prepared.find(1)->slots.size()==3&&
        !prepared.commit_move_to_slot(copied),"prepared slots remain copyable; first allocation-free commit invalidates copies");
    const auto used=original.word_count;odh::WorldLimits limits;limits.words=used+1;odh::World bounded(limits);
    require(bounded.restore_snapshot(original),"bounded legacy checkpoint");const auto before=bounded.snapshot();
    require(!bounded.attach(1,2,2)&&bounded.find(1)->slots.size()==1&&!bounded.find(2)->attachment&&
        bounded.word_count()==before.word_count&&bounded.spatial_revision()==before.spatial_revision,"insufficient append budget leaves both sides unchanged");
    odh::World room_source;avatar(room_source);
    require(room_source.create(2,odh::EntityKind::object)&&room_source.set_position(1,{16,16,1,0})&&room_source.set_position(2,{32,32,1,0}),"combined room source");
    const auto room_snapshot=room_source.snapshot();limits.words=room_snapshot.word_count+2;odh::World room_budget(limits);
    require(room_budget.restore_snapshot(room_snapshot)&&room_budget.set_room_resolver([](const auto&){return std::optional<std::uint16_t>{2};}),"combined slot+room budget");
    require(!room_budget.prepare_move_to_slot(1,2,2)&&room_budget.find(1)->slots.size()==1&&!room_budget.find(2)->attachment&&
        room_budget.word_count()==room_snapshot.word_count,"slot and missing Room29 growth share one atomic budget");
}
void native_object_heading_publication(){
    using namespace odh;World world;
    require(world.create(7,EntityKind::object,{0,0,0})&&world.declare_object_word(7,1,0)&&
        world.declare_object_word(7,23,4)&&world.set_position(7,{8,8,1,0}),"native heading fixture");
    const WorldEntityReference ref{7,world.find(7)->generation};unsigned calls=0;
    std::function<bool()> expired;
    require(world.set_direction_publisher([&](auto&,const auto&,auto actual,std::uint8_t direction,const auto& setter){
        ++calls;require(actual==ref&&direction<8,"native publication owns exact reference and direction");
        expired=setter;return setter();}),"native heading actual publisher");
    require(world.publish_native_object_heading(ref,3)&&calls==1&&world.find(7)->position->direction==3&&
        world.find(7)->object_words.at(1)==3&&!expired(),"native group member bypasses its own notch quantization and setter expires");
    require(world.write(frame(7,7),3,1,3)&&calls==2&&world.find(7)->object_words.at(1)==0,
        "managed direction retains per-object notch quantization");
    require(!world.publish_native_object_heading(ref,8)&&world.find(7)->object_words.at(1)==0,
        "native heading rejects out-of-range input before effects");
    require(world.remove(7)&&world.create(7,EntityKind::object,{0,0,0})&&
        !world.publish_native_object_heading(ref,2)&&calls==2,"native heading never aliases reused object ID");
}
void atomic_personal_projection(){
    using namespace odh;World world;
    for(const auto id:{std::int16_t{5},std::int16_t{9}})
        require(world.create_saved_entity(id,EntityKind::avatar,1000+id,{},std::vector<std::int16_t>(88),
            std::vector<std::int16_t>(16),{},{},0)&&world.set_position(id,{16*id,24,1,0}),"personal projection exact avatar fixture");
    const auto* first=world.find(5);const auto* second=world.find(9);
    auto lease=world.bind_original_load_order({{5,first->generation},{9,second->generation}});
    require(lease.has_value(),"personal projection coexists with original traversal authority");
    std::vector<AvatarPersonalStateChange> changes;
    for(const auto* actor:{first,second}){
        AvatarPersonalStateChange change;change.reference={actor->id,actor->generation};
        change.expected={actor->person_data,actor->motives,actor->avatar_appearance};
        change.replacement=change.expected;change.replacement.person_data[51]=412;change.replacement.motives[9]=63;
        AvatarOutfitReference body;body.literal="test-body";
        change.replacement.appearance=AvatarAppearanceState{body,body,{}};
        change.expected_position=actor->position;change.expected_attachment=actor->attachment;changes.push_back(change);
    }
    const auto revision=world.spatial_revision();
    for(unsigned invalid=0;invalid<9;++invalid){
        auto bad=changes;
        switch(invalid){
        case 0:++bad.back().reference.generation;break;
        case 1:bad.back().expected.person_data[0]=1;break;
        case 2:bad.back().expected.motives[0]=1;break;
        case 3:bad.back().expected.appearance=bad.back().replacement.appearance;break;
        case 4:++bad.back().expected_position->x;break;
        case 5:bad.back().expected_attachment=Attachment{5,0};break;
        case 6:bad.back().replacement.person_data.pop_back();break;
        case 7:bad.push_back(bad.front());break;
        case 8:bad.back().replacement.appearance->body->literal.push_back('\0');break;
        }
        require(!world.publish_avatar_personal_states(bad)&&world.find(5)==first&&world.find(9)==second&&
            first->person_data[51]==0&&second->person_data[51]==0&&first->motives[9]==0&&
            !first->avatar_appearance&&world.spatial_revision()==revision,
            "all personal-state conflicts reject before any first-member effect");
    }
    require(world.publish_avatar_personal_states(changes)&&world.find(5)==first&&world.find(9)==second&&
        first->person_data==changes[0].replacement.person_data&&second->motives==changes[1].replacement.motives&&
        first->avatar_appearance==changes[0].replacement.appearance&&first->position==changes[0].expected_position&&
        world.original_load_head(*lease).reference==changes[0].reference&&world.original_load_next(*lease,changes[0].reference).reference==changes[1].reference,
        "personal batch preserves Entity pointers, positions and exact native traversal");
}

int main() {
    try {
        neighborhood_data_reference_contract();
        retired_container_contents();
        source_direction_setters();
        runtime_constructor_observation();
        retained_reference_bodies();
        legacy_avatar_person_bank();
        legacy_avatar_slots();
        position_room_publication_tests();
        room_registration_tests();lockout_count_tests();
        {
            odh::World identities;
            const auto empty=identities.entity_identity_revision();
            require(identities.create(1,odh::EntityKind::avatar),"create identity revision member");
            const auto first=identities.entity_identity_revision();
            require(first!=empty&&identities.set_position(1,{16,16,1,0})&&
                identities.declare_object_word(1,25,0)&&identities.set_lockout_count_clock(10)&&
                identities.admit_lockout_count_state(1,identities.find(1)->generation,true,0)&&
                identities.entity_identity_revision()==first,
                "positions, data and clocks preserve the cheap membership identity token");
            require(identities.remove(1)&&identities.entity_identity_revision()!=first&&
                identities.create(1,odh::EntityKind::avatar)&&identities.entity_identity_revision()!=first,
                "removal and same-ID recycling invalidate the identity token");
            const auto restored=identities.snapshot();
            const auto stable=identities.entity_identity_revision();
            require(identities.publish_snapshot(restored)&&identities.entity_identity_revision()==stable,
                "equivalent snapshot publication preserves membership identity");
            require(identities.set_ui_ghost(1,true)&&identities.entity_identity_revision()!=stable,
                "UI-only visibility toggle invalidates source enumeration identity");
            const auto hidden=identities.entity_identity_revision();
            auto shown=identities.snapshot();shown.entities.front().ui_ghost=false;
            require(identities.publish_snapshot(shown)&&identities.entity_identity_revision()!=hidden,
                "snapshot visibility changes also invalidate source enumeration identity");
        }

        prepared_slot_moves();
        {
            odh::World definitions;
            require(definitions.create(1,odh::EntityKind::avatar,{}) &&
                definitions.create(2,odh::EntityKind::object,{}) &&
                definitions.define_type(2,0x12345678U),"definition resolver fixture");
            const auto f=frame();
            require(!definitions.read(f,21,18),"missing definition has no zero fallback");
            unsigned calls=0;
            definitions.set_definition_word_resolver([&](const odh::Entity& entity,std::int16_t index)
                ->std::optional<std::int16_t> {
                ++calls;
                if(entity.definition&&entity.definition->guid==0x12345678U&&index==18)return 321;
                return {};
            });
            const auto initial=definitions.snapshot();
            require(definitions.read(f,21,18)==321 && calls==1 &&
                definitions.find(2)->definition_words.empty() && definitions.word_count()==initial.word_count,
                "resource fallback reads live StackObject without adding serialized state");
            require(!definitions.read(f,21,19)&&!definitions.read(frame(2,1),21,18),
                "unknown field and unrelated entity remain unavailable");
            const auto before_invalid=calls;
            require(!definitions.read(f,21,-1)&&!definitions.read(f,21,256)&&
                !definitions.read(frame(1,99),21,18)&&calls==before_invalid,
                "invalid indices and missing entities never reach resolver");
            require(definitions.declare_definition_word(2,18,777),"explicit definition fixture");
            const auto before_explicit=calls;
            require(definitions.read(f,21,18)==777&&calls==before_explicit,
                "explicit definition bank precedes resource fallback");
            odh::World restored;
            require(restored.restore_snapshot(initial)&&!restored.read(f,21,18),
                "snapshot cannot serialize or retain the resource resolver");
            // Binding once covers objects created later using their current GUID.
            require(definitions.create_runtime_object(3,0x12345678U,{}, {}) &&
                definitions.read(frame(1,3),21,18)==321,
                "runtime-created objects use the same resource definition path");
            require(definitions.publish_snapshot(definitions.snapshot())&&definitions.read(frame(1,3),21,18)==321,
                "publication preserves host resolver while replacing owned banks");
            definitions.set_definition_word_resolver({});
            require(!definitions.read(frame(1,3),21,18)&&definitions.read(f,21,18)==777,
                "cleared resolver restores fail-closed default and preserves explicit bank");
        }
        {
            odh::World parents;
            require(parents.create(1,odh::EntityKind::object,{0,0,1}) &&
                parents.create(2,odh::EntityKind::object) && parents.create(3,odh::EntityKind::object,{0,0,1}) &&
                parents.declare_object_word(2,26,777) && parents.declare_object_word(3,26,888),"ParentId fixture");
            const auto f=frame(3,2);
            require(parents.read(f,3,26)==0 && parents.read(f,4,26)==0,"ParentId uncontained ignores raw shadow for both scopes");
            require(parents.attach(1,0,2) && parents.read(f,4,26)==1 && parents.read(f,4,2)==1 &&
                parents.read(f,3,26)==0,"ParentId follows actual stack-object containment, not caller's raw word");
            require(parents.write(f,4,26,999) && parents.find(2)->object_words.at(26)==999 &&
                parents.read(f,4,26)==1 && parents.find(1)->slots[0]==2,"ParentId raw setter preserves shadow without moving object");
            require(parents.detach(1,0) && parents.attach(3,0,2) && parents.read(f,4,26)==3 &&
                parents.read(f,3,26)==0,"ParentId follows reparenting immediately");
            require(parents.retire(3) && parents.read(f,4,26)==0 && parents.find(2)->object_words.at(26)==999,
                "retired parent cannot remain authoritative through stale raw ParentId");
            require(!parents.read(frame(3,99),4,26),"missing ParentId entity stays unsupported");
        }
        {
            odh::World retired;
            require(retired.create(1,odh::EntityKind::object,{0,0,1,0}) &&
                retired.create(2,odh::EntityKind::object) && retired.attach(1,0,2),"retirement fixture");
            const auto generation=retired.snapshot().generation_sequence;
            require(retired.retire(2) && !retired.find(2) && retired.find(1)->slots[0]==0 &&
                !retired.entity_id_available(2) && !retired.create(2,odh::EntityKind::object) &&
                retired.snapshot().generation_sequence==generation,"retirement detaches and permanently reserves ID");
            require(!retired.retire(2) && !retired.retire(0) && !retired.retire(-1) &&
                retired.reserved_entity_ids()==std::vector<std::int16_t>({2}),"invalid retirement has no effect");
            std::vector<std::int16_t> reservations;
            for(std::int16_t id=2;id<=4097;++id)reservations.push_back(id);
            odh::WorldLimits capacity;capacity.reserved_entity_ids=reservations;
            odh::World full(capacity);require(full.create(1,odh::EntityKind::object),"full reservation fixture");
            const auto before=full.snapshot();
            require(!full.retire(1) && full.find(1) && full.snapshot().word_count==before.word_count &&
                full.snapshot().spatial_revision==before.spatial_revision && full.reserved_entity_ids()==reservations,
                "reservation exhaustion leaves original entity and counters intact");
        }
        {
            odh::WorldLimits policy;
            policy.reserved_entity_ids = {3, 1, 2};
            odh::World guarded(policy);
            require(guarded.reserved_entity_ids() == std::vector<std::int16_t>({1,2,3}),
                "reservation policy is canonicalized without mutating caller limits");
            require(policy.reserved_entity_ids == std::vector<std::int16_t>({3,1,2}) &&
                !guarded.entity_id_available(0) && !guarded.entity_id_available(-1) &&
                !guarded.entity_id_available(1) && guarded.entity_id_available(4) && guarded.entity_id_available(32767),
                "availability separates invalid, reserved and free runtime IDs");
            require(!guarded.create(1,odh::EntityKind::avatar,{0,88,0,16}) &&
                !guarded.create_runtime_object(2,0x12345678U,{}, {}) &&
                !guarded.create_saved_entity(3,odh::EntityKind::object,0x12345678U,{}, {}, {}, {}, {},0),
                "every entity constructor rejects retired IDs");
            require(!guarded.next_entity(0) && guarded.snapshot().generation_sequence == 0 &&
                guarded.snapshot().spatial_revision == 0 && guarded.snapshot().word_count == 0,
                "reservations do not enumerate or consume entity generations and banks");
            std::int16_t first_available = 0;
            for (std::int32_t value = 1; value <= 32767; ++value)
                if (guarded.entity_id_available(static_cast<std::int16_t>(value))) {
                    first_available = static_cast<std::int16_t>(value); break;
                }
            require(first_available == 4 && guarded.create(4,odh::EntityKind::object,{1}) &&
                !guarded.entity_id_available(4), "allocation search skips reserved IDs before choosing a fresh ID");
            odh::World ordinary;
            require(ordinary.create(1,odh::EntityKind::object,{1}), "unreserved snapshot fixture");
            odh::World fresh_guarded(policy);
            require(!fresh_guarded.restore_snapshot(ordinary.snapshot()) && !fresh_guarded.next_entity(0) &&
                fresh_guarded.snapshot().generation_sequence == 0,
                "restore rejects a reserved live ID atomically before publishing any history");
            require(fresh_guarded.restore_snapshot(guarded.snapshot()) &&
                !fresh_guarded.entity_id_available(1) && fresh_guarded.find(4),
                "restore accepts compatible entities while retaining external launch reservations");
            odh::World default_restore;
            require(default_restore.restore_snapshot(guarded.snapshot()) && default_restore.entity_id_available(1) &&
                default_restore.reserved_entity_ids().empty(),
                "snapshot format carries entity state without inventing a reservation wire field");
            // A full-session restore can bind VMs before the host rederives
            // exclusions. Install them in place without replacing World/entities.
            const auto* stable_actor = default_restore.find(4);
            const auto rebound_memory = default_restore.vm_memory();
            const auto before_install = default_restore.snapshot();
            require(default_restore.install_reserved_entity_ids({3,1,2}) &&
                default_restore.reserved_entity_ids() == std::vector<std::int16_t>({1,2,3}) &&
                default_restore.find(4) == stable_actor &&
                default_restore.snapshot().word_count == before_install.word_count &&
                default_restore.snapshot().generation_sequence == before_install.generation_sequence &&
                default_restore.snapshot().spatial_revision == before_install.spatial_revision,
                "post-restore policy installation preserves live entity pointers and all snapshot counters");
            require(rebound_memory.write(frame(4,4),0,0,19) && stable_actor->attributes[0] == 19,
                "VM memory callbacks bound before policy installation still address the same World");
            require(!default_restore.install_reserved_entity_ids({8,4}) &&
                default_restore.reserved_entity_ids() == std::vector<std::int16_t>({1,2,3}) &&
                default_restore.entity_id_available(8) && default_restore.find(4) == stable_actor &&
                stable_actor->attributes[0] == 19,
                "a live reserved ID rejects the entire installation without reserving its other IDs");
            require(default_restore.install_reserved_entity_ids({7,3}) &&
                default_restore.install_reserved_entity_ids({3,7}) && default_restore.install_reserved_entity_ids({}) &&
                default_restore.reserved_entity_ids() == std::vector<std::int16_t>({1,2,3,7}),
                "installation unions exclusions idempotently and empty input cannot clear old reservations");
            require(!default_restore.install_reserved_entity_ids({9,9}) &&
                !default_restore.install_reserved_entity_ids({0}) && !default_restore.install_reserved_entity_ids({-1}) &&
                !default_restore.install_reserved_entity_ids(std::vector<std::int16_t>(4097,10)) &&
                default_restore.entity_id_available(9) && default_restore.reserved_entity_ids().size() == 4,
                "invalid installation inputs reject atomically under the same bounds as construction");
            std::vector<std::int16_t> full_policy;
            for (std::int32_t id = 1; id <= 4097; ++id)
                if (id != 4) full_policy.push_back(static_cast<std::int16_t>(id));
            require(default_restore.install_reserved_entity_ids(full_policy) &&
                default_restore.reserved_entity_ids().size() == 4096 &&
                !default_restore.install_reserved_entity_ids({5000}) && default_restore.entity_id_available(5000) &&
                default_restore.install_reserved_entity_ids({1}),
                "combined reservation bound is enforced without rejecting overlap with a full existing policy");
            odh::World restored_old_id;
            require(restored_old_id.restore_snapshot(ordinary.snapshot()) &&
                !restored_old_id.install_reserved_entity_ids({1,2}) && restored_old_id.find(1) &&
                restored_old_id.reserved_entity_ids().empty() && restored_old_id.entity_id_available(2),
                "an archive containing a retired live ID cannot acquire derived occupancy authority");
            const auto before_publish = guarded.snapshot();
            auto conflicting = before_publish;
            auto extra = ordinary.snapshot().entities.front();
            extra.generation = before_publish.generation_sequence + 1;
            conflicting.entities.insert(conflicting.entities.begin(),extra);
            conflicting.word_count += extra.attributes.size();
            conflicting.generation_sequence = extra.generation;
            ++conflicting.spatial_revision;
            require(!guarded.publish_snapshot(conflicting) && !guarded.find(1) && guarded.find(4) &&
                guarded.snapshot().generation_sequence == before_publish.generation_sequence &&
                guarded.snapshot().spatial_revision == before_publish.spatial_revision &&
                guarded.snapshot().word_count == before_publish.word_count,
                "publish rejects reserved IDs without partially changing valid resident state or counters");
            conflicting.entities.front().id = 5;
            std::swap(conflicting.entities[0],conflicting.entities[1]);
            require(guarded.publish_snapshot(conflicting) && guarded.find(5),
                "the same otherwise valid candidate publishes with a nonreserved fresh ID");
            require(guarded.remove(4) && guarded.entity_id_available(4) && !guarded.remove(1) &&
                !guarded.entity_id_available(1) && !guarded.create(1,odh::EntityKind::object),
                "ordinary removals cannot release or reuse retired occupancy IDs");
            const auto rejects_policy = [&](std::vector<std::int16_t> ids) {
                auto limits = policy; limits.reserved_entity_ids = std::move(ids);
                try { odh::World invalid(limits); }
                catch (const std::invalid_argument&) { return true; }
                return false;
            };
            require(rejects_policy({1,1}) && rejects_policy({0}) && rejects_policy({-1}) &&
                rejects_policy(std::vector<std::int16_t>(4097,1)),
                "invalid, duplicate and oversized reservation policies reject at construction");
        }
        odh::World spatial;
        require(spatial.create(1,odh::EntityKind::object,{0,0,1}) &&
                    spatial.create(2,odh::EntityKind::avatar,{0,88,0}) &&
                    spatial.set_position(1,{16,16,1,0}) && spatial.set_position(2,{32,16,1,0}),
                "spatial revision fixture setup");
        auto spatial_revision=spatial.spatial_revision();
        require(spatial.set_position(2,{48,16,1,0}) && spatial.spatial_revision()>spatial_revision,
                "moving an entity advances spatial revision");
        spatial_revision=spatial.spatial_revision();
        require(spatial.attach(1,0,2) && spatial.spatial_revision()>spatial_revision,
                "changing containment advances spatial revision");
        spatial_revision=spatial.spatial_revision();
        require(spatial.set_static_footprint(1,false) && spatial.spatial_revision()>spatial_revision,
                "changing an entity footprint advances spatial revision");

        odh::World carried_object;
        require(carried_object.create(105,odh::EntityKind::avatar,{0,88,3,16})&&
                    carried_object.create(112,odh::EntityKind::object,{0,0,1})&&
                    carried_object.create(180,odh::EntityKind::object,{0,0,1})&&
                    carried_object.set_position(105,{360,456,1,6})&&
                    carried_object.set_position(112,{360,456,1,2})&&
                    carried_object.set_position(180,{360,456,1,2})&&
                    carried_object.attach(112,0,180),"carried object fixture setup");
        require(carried_object.move_to_slot(105,0,180,6)&&
                    carried_object.find(112)->slots[0]==0&&carried_object.find(105)->slots[0]==180&&
                    carried_object.find(180)->attachment&&carried_object.find(180)->attachment->container==105&&
                    carried_object.find(180)->position->direction==6,
                "Grab moves an object from its old container into the avatar hand slot");
        require(carried_object.create(181,odh::EntityKind::object,{0,0,0})&&
                    carried_object.attach(105,1,181)&&
                    !carried_object.move_to_slot(105,1,180)&&
                    carried_object.find(105)->slots[1]==181&&carried_object.find(105)->slots[0]==180,
                "Grab rejects an occupied hand slot without changing either object");

        odh::World motive_state;
        require(motive_state.create(1,odh::EntityKind::avatar,{0,0,0,16}),
                "motive revision fixture setup");
        const auto motive_revision=motive_state.spatial_revision();
        require(motive_state.set_motive(1,7,70) &&
                    motive_state.spatial_revision()==motive_revision,
                "motive updates do not invalidate room and route geometry");

        odh::World ui_ghosts;
        require(ui_ghosts.create(1,odh::EntityKind::avatar) &&
                    ui_ghosts.create(2,odh::EntityKind::object) &&
                    ui_ghosts.create(3,odh::EntityKind::object) &&
                    ui_ghosts.define_type(2,0x7c4) && ui_ghosts.define_type(3,0x7c4) &&
                    ui_ghosts.set_position(1,{24,24,1,0}) &&
                    ui_ghosts.set_position(2,{24,24,1,0}) &&
                    ui_ghosts.set_position(3,{24,24,1,0}) &&
                    ui_ghosts.set_ui_ghost(2,true),
                "temporary UI destination fixture setup");
        auto ghost_revision=ui_ghosts.spatial_revision();
        require(ui_ghosts.find(2)->ui_ghost && ui_ghosts.set_ui_ghost(2,true) &&
                    ui_ghosts.spatial_revision()==ghost_revision &&
                    ui_ghosts.next_entity(1)==std::optional<std::int16_t>{3} &&
                    ui_ghosts.next_entity(1,odh::EntityKind::object)==std::optional<std::int16_t>{3} &&
                    ui_ghosts.next_entity_of_guid(1,0x7c4)==std::optional<std::int16_t>{3} &&
                    ui_ghosts.next_entity_on_same_tile(1)==std::optional<std::int16_t>{3},
                "UI-only ghost remains directly addressable but stays out of object searches");
        require(ui_ghosts.set_ui_ghost(2,false) &&
                    ui_ghosts.next_entity(1)==std::optional<std::int16_t>{2} &&
                    ui_ghosts.next_entity_of_guid(1,0x7c4)==std::optional<std::int16_t>{2} &&
                    ui_ghosts.next_entity_on_same_tile(1)==std::optional<std::int16_t>{2},
                "restoring a normal entity makes it visible to object searches");

        odh::World w;
        require(w.create(1,odh::EntityKind::avatar,{2,88,1}),"create avatar");
        require(w.find(1)->kill_timeout==std::optional<std::int32_t>{-1} &&
                    !w.set_avatar_kill_timeout(2,0) && !w.set_avatar_kill_timeout(1,-2) &&
                    w.set_avatar_kill_timeout(1,0) && w.find(1)->kill_timeout==std::optional<std::int32_t>{0} &&
                    w.set_avatar_kill_timeout(1,-1),
                "avatar lifecycle starts alive and accepts only valid runtime updates");
        require(w.create(2,odh::EntityKind::object,{3,0,2}),"create object");
        require(w.create(3,odh::EntityKind::avatar,{0,88,0}),"create second avatar");
        const auto f=frame();
        require(w.read(f,3,11)==1 && w.read(f,4,11)==2,"derived identity follows context");
        require(w.read(f,4,67)==2 && w.read(f,3,67)==1,"derived slot count");
        require(w.read(f,3,2)==0 && w.read(f,3,3)==-1,"unattached container/slot profile");
        require(w.read(f,16,0)==0 && !w.read(f,16,2),"empty differs from invalid slot");
        require(!w.read(f,3,0) && !w.write(f,3,0,9),"undeclared object word is unsupported");
        require(w.declare_object_word(1,0,7) && w.read(f,3,0)==7,"explicit word declaration");
        require(w.write(f,3,0,9) && w.read(f,3,0)==9,"raw stored word write");
        const auto revision_before_position=w.spatial_revision();
        require(w.declare_object_word(1,1,0) && w.set_position(1,{16,16,1,0}) &&
                    w.spatial_revision()>revision_before_position &&
                    w.write(f,3,1,2) && w.find(1)->position->direction==2 && w.read(f,3,1)==2,
                "position and ObjectData direction writes advance live spatial revision");
        require(w.write(f,3,1,8) && w.find(1)->position->direction==0,
                "source ObjectData direction normalizes a full turn to north");
        require(w.set_position(1,{16,16,1,4}) && w.read(f,3,1)==4,
                "live position updates the ObjectData direction");
        constexpr float fractional_turn=0.3f;
        require(w.set_radian_direction(1,fractional_turn) &&
                    w.find(1)->radian_direction==fractional_turn &&
                    w.find(1)->position->direction==0 && w.read(f,3,1)==0,
                "avatar keeps continuous radians while exposing the rounded direction notch");
        require(w.set_position(1,{17,16,1,0}) &&
                    w.find(1)->radian_direction==fractional_turn,
                "movement within the same direction notch preserves continuous avatar angle");
        require(!w.set_radian_direction(2,0.5f) &&
                    !w.set_radian_direction(1,std::numeric_limits<float>::quiet_NaN()) &&
                    w.find(1)->radian_direction==fractional_turn,
                "radian direction rejects non-avatars and non-finite values atomically");
        require(w.set_radian_direction(1,0.9f) && w.find(1)->position->direction==1 &&
                    w.read(f,3,1)==1,
                "continuous angle rounds to the source eight-notch direction");
        require(w.set_position(1,{18,16,1,3}) &&
                    std::abs(w.find(1)->radian_direction-3.0f*0.7853981633974483f)<0.00001f,
                "explicit notch changes reset avatar radians to the canonical notch angle");
        for (const auto index : {2,3,11,67}) {
            require(!w.declare_object_word(1,static_cast<std::int16_t>(index),9),"derived words cannot be replaced");
            require(!w.write(f,3,static_cast<std::int16_t>(index),9),"derived identity/relation cannot be written");
        }
        require(w.write(f,0,1,17) && w.read(f,0,1)==17 && w.read(f,1,1)==0,"attributes belong to proper entity");
        require(!w.write(f,0,-1,17) && !w.write(f,0,2,17),"attribute bounds");
        require(w.set_person_data(1,0,1) && w.read(f,18,0)==1,"posture word");
        require(w.set_person_data(3,0,2) && w.read(frame(1,3),19,0)==2 &&
                w.read(frame(1,3),18,0)==1,"person scopes separate caller and stack object");
        require(w.attach(1,0,3) && w.read(f,20,0)==3 && w.read(f,16,0)==0 && w.detach(1,0),
                "caller slots separate from stack object slots");
        require(!w.read(f,19,0) && !w.set_person_data(2,0,1),"objects have no person data");
        require(!w.read(frame(9,9),18,0) && !w.write(frame(9,9),18,0,1),"missing entity");
        require(!w.read(f,18,88) && !w.write(f,18,-1,1),"person data bounds");
        require(!w.read(f,6,0) && !w.write(f,16,0,1),"unsupported scopes and direct slot writes");
        require(w.attach(2,0,1),"attach occupant");
        require(w.read(f,16,0)==1 && w.read(f,3,2)==2 && w.read(f,3,3)==0,"bidirectional attachment");
        require(!w.attach(2,1,1) && !w.attach(2,0,3),"one container and no overwrite");
        require(!w.attach(1,0,2),"containment cycle rejected");
        require(!w.attach(2,0,2) && !w.attach(2,2,3) && !w.attach(2,1,99),"invalid attachment unchanged");
        require(w.read(f,16,0)==1 && w.read(f,16,1)==0,"failed attach leaves both slots unchanged");
        require(w.detach(2,0) && w.read(f,16,0)==0 && w.read(f,3,3)==-1,"detach clears both ends");
        require(w.detach(2,0) && !w.detach(2,2),"idempotent empty detach and bounds");
        require(w.attach(2,1,3) && w.remove(3) && w.read(f,16,1)==0,"remove occupant detaches");
        require(w.attach(2,0,1) && w.remove(2) && !w.find(1)->attachment,"remove container releases occupant");
        require(w.word_count()==93,"word accounting after removals");
        require(w.remove(1) && w.word_count()==0 && !w.remove(1),"remove accounting and missing ID");
        require(!w.create(0,odh::EntityKind::object) && !w.create(-1,odh::EntityKind::object),"null/negative ID rejected");
        require(!w.create(1,odh::EntityKind::object,{0,1,0}),"person bank requires avatar");
        odh::World limited({1,5,5});
        require(!limited.create(1,odh::EntityKind::avatar,{3,3,0}) && limited.word_count()==0,"aggregate budget failure atomic");
        require(limited.create(1,odh::EntityKind::avatar,{2,2,0}) && !limited.create(2,odh::EntityKind::object),"entity limit");
        require(!limited.create(1,odh::EntityKind::object),"duplicate ID");
        require(limited.declare_object_word(1,0,7) && !limited.declare_object_word(1,1,8),"sparse word budget");
        require(limited.declare_object_word(1,0,9) && limited.word_count()==5,"replace word does not allocate");
        require(limited.remove(1) && limited.word_count()==0,"budget recovered");
        odh::World overflow({2,std::numeric_limits<std::size_t>::max(),std::numeric_limits<std::size_t>::max()});
        require(!overflow.create(1,odh::EntityKind::avatar,{std::numeric_limits<std::size_t>::max(),1,1}),"oversized layout rejected before allocation");
        require(!overflow.create(1,static_cast<odh::EntityKind>(99)),"unknown entity kind rejected");
        odh::World chain;
        require(chain.create(1,odh::EntityKind::object,{0,0,1}) &&
                chain.create(2,odh::EntityKind::object,{0,0,1}) &&
                chain.create(3,odh::EntityKind::object,{0,0,1}) &&
                chain.attach(1,0,2) && chain.attach(2,0,3),"nested container chain");
        require(!chain.attach(3,0,1) && chain.find(3)->slots[0]==0,"long containment cycle rejected");

        odh::World banks;
        require(banks.create(1,odh::EntityKind::avatar,{1,1,1,16}) &&
                banks.create(2,odh::EntityKind::avatar,{0,0,0,7}) &&
                banks.create(3,odh::EntityKind::object),"explicit motive bank layouts");
        require(banks.read(f,14,6)==0 && banks.read(f,15,6)==0,
                "allocated comfort words start at zero");
        require(banks.write(f,14,6,45) && banks.write(f,15,6,-35) &&
                banks.read(f,14,6)==45 && banks.read(f,15,6)==-35,
                "motive scopes separate caller and stack avatar");
        require(banks.write(f,14,6,std::numeric_limits<std::int16_t>::max()) &&
                banks.read(f,14,6)==100 && banks.write(f,15,6,std::numeric_limits<std::int16_t>::min()) &&
                banks.read(f,15,6)==-100,"motive writes clamp to fixed default bounds");
        require(banks.set_motive(1,6,25) && banks.read(f,14,6)==25 &&
                banks.write(f,14,6,120) && banks.read(f,14,6)==100,
                "direct motive setter shares clipping and allows decreases");
        require(banks.write(f,14,15,1) && banks.read(f,14,15)==1 &&
                !banks.read(f,14,16) && !banks.write(f,14,16,7) &&
                !banks.read(f,15,7) && !banks.write(f,15,7,7) &&
                !banks.read(f,14,-1) && !banks.set_motive(1,-1,7),"motive allocated index bounds");
        require(!banks.read(frame(3,3),14,0) && !banks.read(frame(3,3),15,0) &&
                !banks.write(frame(3,3),14,0,1) && !banks.write(frame(3,3),15,0,1) &&
                !banks.set_motive(3,0,1),"objects have no motive bank");
        require(!banks.read(frame(9,9),14,0) && !banks.write(frame(9,9),15,0,1),
                "missing motive entity is unsupported");
        require(banks.create(4,odh::EntityKind::avatar,{1,1,1}) && banks.find(4)->motives.empty() &&
                !banks.read(frame(4,4),14,0) && !banks.write(frame(4,4),15,0,1),
                "legacy three field layout leaves motives absent");
        const auto before_invalid=banks.word_count();
        require(!banks.create(5,odh::EntityKind::avatar,{0,0,0,17}) &&
                !banks.create(5,odh::EntityKind::object,{0,0,0,1}) &&
                banks.word_count()==before_invalid && !banks.find(5),"invalid motive layouts fail atomically");
        require(!banks.read(f,21,9) && !banks.read(frame(3,3),21,9),
                "definition object type is never inferred from entity kind");
        require(banks.declare_definition_word(1,9,2) && !banks.read(f,21,9) &&
                banks.declare_definition_word(2,9,42) && banks.read(f,21,9)==42 &&
                banks.read(frame(2,1),21,9)==2,"definition scope selects exact stack object bank");
        const auto before_repeat=banks.word_count();
        require(banks.declare_definition_word(2,9,42) && !banks.declare_definition_word(2,9,2) &&
                banks.word_count()==before_repeat && banks.read(f,21,9)==42,
                "definition declaration is immutable and idempotent");
        require(!banks.write(f,21,9,42) && !banks.write(f,21,9,7) && !banks.write(f,21,10,7) &&
                banks.read(f,21,9)==42 && !banks.read(f,21,10),"definition scope is always read only");
        require(!banks.read(f,21,11) && banks.read(f,4,11)==2 &&
                banks.declare_definition_word(2,11,81) && banks.read(f,21,11)==81 && banks.read(f,4,11)==2,
                "definition index never falls back to derived object fields");
        require(banks.declare_definition_word(2,0,123) && banks.declare_definition_word(2,255,-123) &&
                banks.read(f,21,0)==123 && banks.read(f,21,255)==-123 &&
                !banks.declare_definition_word(2,-1,1) && !banks.declare_definition_word(2,256,1) &&
                !banks.read(f,21,-1) && !banks.read(f,21,256) &&
                !banks.declare_definition_word(99,0,1),"explicit raw definition index profile bounds");
        const auto generation=banks.find(2)->generation;
        const auto before_remove=banks.word_count();
        require(banks.remove(2) && banks.word_count()==before_remove-11 &&
                banks.create(2,odh::EntityKind::avatar,{0,0,0,7}) &&
                banks.find(2)->generation!=generation && banks.read(f,15,6)==0 && !banks.read(f,21,9),
                "removal releases motives and definitions and reused IDs inherit no values");

        odh::World bank_limit({4,20,2});
        require(!bank_limit.create(1,odh::EntityKind::avatar,{0,0,0,3}) && bank_limit.word_count()==0 &&
                bank_limit.create(1,odh::EntityKind::avatar,{0,0,0,2}),"motive per bank budget");
        require(bank_limit.declare_definition_word(1,9,2) && bank_limit.declare_definition_word(1,255,8) &&
                !bank_limit.declare_definition_word(1,0,7) && bank_limit.word_count()==4 &&
                !bank_limit.read(frame(1,1),21,0),"definition per bank budget failure leaves no entry");
        require(bank_limit.declare_definition_word(1,9,2) && !bank_limit.declare_definition_word(1,9,3) &&
                bank_limit.word_count()==4,"full definition bank still allows identical declaration");
        odh::World total_limit({3,4,16});
        require(!total_limit.create(1,odh::EntityKind::avatar,{1,1,1,2}) &&
                total_limit.word_count()==0 && !total_limit.find(1),"motive global budget failure atomic");
        require(total_limit.create(1,odh::EntityKind::avatar,{0,0,0,3}) &&
                total_limit.create(2,odh::EntityKind::object) && total_limit.declare_definition_word(2,9,7) &&
                !total_limit.declare_definition_word(1,9,2) && total_limit.word_count()==4 &&
                !total_limit.read(frame(1,1),21,9),"definition global budget failure atomic");
        require(total_limit.declare_definition_word(2,9,7) && total_limit.set_motive(1,0,12) &&
                total_limit.word_count()==4,"existing definition and motive writes consume no words");
        require(total_limit.remove(1) && total_limit.remove(2) && total_limit.word_count()==0 &&
                total_limit.create(1,odh::EntityKind::avatar,{0,0,0,4}),"new banks release global budget");

        // Independent synthetic program reads caller ID and a stack object slot.
        require(w.create(1,odh::EntityKind::avatar,{0,88,0}) && w.create(2,odh::EntityKind::object,{0,0,1}),"VM world setup");
        odh::Bhav r{0x8002,{{2,254,255,{0,0,11,0,0,2,16,3}}},{},odh::Bhav::Metadata{0,4,0,0}};
        odh::Vm vm({{odh::routine_key(4096),r}},w.vm_memory());
        vm.start(odh::routine_key(4096),f.context);
        require(vm.run(1)==odh::VmStatus::returned_false,"empty slot does not match caller");
        require(w.attach(2,0,1),"VM occupy slot");
        vm.start(odh::routine_key(4096),f.context);
        require(vm.run(1)==odh::VmStatus::returned_true,"bound world reads live occupation");
        w.remove(2); vm.start(odh::routine_key(4096),f.context);
        require(vm.run(1)==odh::VmStatus::fault,"deleted entity never becomes implicit zero");
        odh::World out_of_world_room;
        require(out_of_world_room.create(7,odh::EntityKind::avatar) &&
                    out_of_world_room.declare_object_word(7,29,-1) &&
                    out_of_world_room.read(frame(7,7),3,29)==std::optional<std::int16_t>{-5} &&
                    out_of_world_room.set_position(7,{16,16,1,0}) &&
                    out_of_world_room.read(frame(7,7),3,29)==std::optional<std::int16_t>{-1},
                "Room reads out-of-world sentinel before placement and stored room after placement");
        // Synthetic copy of the observed Doors.iff 4113 -> 4115 Route45 decision.
        odh::World door_world;
        require(door_world.create(40,odh::EntityKind::object,{2,0,0,0}),
                "create object with bounded attributes for the portal Route45 profile");
        odh::Bhav portal_condition{0x8002,{{2,255,254,{1,0,0,0,0,2,1,7}}},{},
                                   odh::Bhav::Metadata{0,0,0,0}};
        odh::Bhav portal_routes{0x8002,{
            {4115,1,2,{255,255,255,255,255,255,255,255}},
            {45,254,255,{0,0,1,0,1,0,0,0}},
            {45,254,255,{1,0,1,0,1,0,0,0}}},{},odh::Bhav::Metadata{0,0,0,0}};
        unsigned route_calls=0,route_data=99,route_scope=99,route_flags=0;
        std::int16_t route_stack_object=0;
        odh::VmPrimitive route_probe=[&](const odh::VmFrame& vm_frame,const odh::Instruction& instruction,
                                         const auto&,const auto&) {
            if(instruction.opcode!=45)return odh::PrimitiveOutcome{};
            ++route_calls;
            route_data=static_cast<unsigned>(instruction.operands[0]|(instruction.operands[1]<<8));
            route_scope=static_cast<unsigned>(instruction.operands[2]|(instruction.operands[3]<<8));
            route_flags=instruction.operands[4];
            route_stack_object=vm_frame.context.stack_object;
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        };
        odh::Vm portal_vm({{odh::routine_key(4113),portal_routes},
                           {odh::routine_key(4115),portal_condition}},
                          door_world.vm_memory(),{},route_probe);
        const auto portal_context=frame(1,40).context;
        portal_vm.start(odh::routine_key(4113),portal_context);
        require(portal_vm.run(10)==odh::VmStatus::returned_true&&route_calls==1&&
                    route_data==1&&route_scope==1&&route_flags==1&&route_stack_object==40,
                "4113 routes to literal slot 1 when StackObject attribute 1 equals zero");
        require(door_world.write(frame(1,40),1,1,1),"change portal StackObject attribute");
        route_calls=0;portal_vm.start(odh::routine_key(4113),portal_context);
        require(portal_vm.run(10)==odh::VmStatus::returned_true&&route_calls==1&&
                    route_data==0&&route_scope==1&&route_flags==1&&route_stack_object==40,
                "4113 routes to literal slot 0 when StackObject attribute 1 is nonzero");
        require(door_world.remove(40),"remove StackObject used by the 4113 profile");
        route_calls=0;portal_vm.start(odh::routine_key(4113),portal_context);
        require(portal_vm.run(10)==odh::VmStatus::fault&&route_calls==0,
                "4113 condition faults closed when the StackObject identity is unavailable");
        {
            odh::World levels;
            require(levels.create(1,odh::EntityKind::avatar,{}) &&
                    levels.create(2,odh::EntityKind::object,{}) &&
                    levels.set_position(1,{16,16,1,0}) &&
                    levels.set_position(2,{32,16,2,0}) &&
                    levels.declare_object_word(1,29,1500), "room Expression world setup");
            const odh::Bhav less{0x8002,{{2,254,255,{29,0,0,4,0,1,3,7}}},{},
                odh::Bhav::Metadata{0,4,0,0}};
            odh::Vm vm({{odh::routine_key(4096),less}},levels.vm_memory());
            vm.start(odh::routine_key(4096),{1,2,2});
            require(vm.run(2)==odh::VmStatus::fault,"missing level offset is not guessed as zero");
            require(levels.declare_definition_word(2,56,0),"bind room level metadata");
            vm.start(odh::routine_key(4096),{1,2,2});
            require(vm.run(2)==odh::VmStatus::returned_true,
                    "room sentinel compares caller and callee floors rather than room number");
            require(levels.create(3,odh::EntityKind::object,{}) &&
                    levels.set_position(3,{48,16,2,0}) &&
                    levels.declare_definition_word(3,56,-1),"bind unsigned65535 level offset");
            vm.start(odh::routine_key(4096),{1,3,3});
            require(vm.run(2)==odh::VmStatus::returned_false,"level offset preserves unsigned OBJD word");
            auto missing=frame(1,2);missing.context.callee=99;
            require(!levels.vm_memory().expression_levels(missing),"missing callee level remains unavailable");
        }
        atomic_personal_projection();
        native_physical_parent_family();
        original_load_traversal_contract();
        original_person_registration_order();
        suppress_referenced_reset_policy();
        native_recovery_position_policy();
        native_scalar_position_publication();
        native_object_heading_publication();
        current_value_runtime_contract();
        atomic_attribute_max();
        indirect_variable_generations();
        std::cout<<checks<<" world checks passed (synthetic fixtures only)\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"World test failed: "<<e.what()<<'\n'; return 1; }
}
