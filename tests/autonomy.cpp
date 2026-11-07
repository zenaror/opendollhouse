#include "odh/autonomy.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class T>odh::AutonomyResult<T> ready(T value){return {odh::AutonomyStatus::ready,std::move(value),{}};}
std::shared_ptr<const odh::Ts1InteractionCurves> curves(bool flat=false){
    odh::StringTable table{odh::StringTableState::decoded,-3,{},0};
    for(unsigned i=0;i<9;++i)table.entries.push_back({1,flat?"(0;0)":"(0;0);(100;100)",""});
    return std::make_shared<const odh::Ts1InteractionCurves>(odh::read_ts1_interaction_curves(table));
}
struct Harness {
    odh::World world;
    std::unique_ptr<odh::Vm> vm;
    odh::FindBestActionRequest request;
    std::map<std::int16_t,odh::AutonomyObject> objects;
    std::map<std::int16_t,std::vector<odh::AutonomyVariant>> variants;
    std::map<std::int16_t,std::int16_t> users;
    std::vector<odh::AutonomyIdentity> inventory;
    std::optional<odh::AutonomyIdentity> higher;
    std::optional<odh::AutonomyChoice> queued;
    odh::PrimitiveOutcome outcome;
    unsigned rng_calls=0,checks=0,lookups=0,curve_calls=0;
    std::uint64_t random=0;
    bool allowed=true,strings_present=true,enqueue_allowed=true,intact=true,child_seen=false,flat_child=false;
    std::function<void(odh::AutonomyIdentity)> check_effect;
    Harness(){
        require(world.create(1,odh::EntityKind::avatar,{0,88,0,16})&&world.set_position(1,{0,0,1,0})&&
            world.set_person_data(1,58,27),"caller fixture");
        add_object(2,1000);add_object(3,500);
        request.world=&world;
        request.first_higher_priority=[&](std::int16_t p){require(p==world.find(1)->person_data[33],"actual current priority provided");return ready(higher);};
        request.ordered_with_autonomy=[&]{return ready(inventory);};
        request.resolve_object=[&](auto id){++lookups;return ready(objects.at(id.id));};
        request.check_action=[&](auto id,const auto&){++checks;if(check_effect)check_effect(id);
            return ready(odh::AutonomyCheck{allowed,variants[id.id]});};
        request.use_count=[&](auto id){return ready(users[id.id]);};
        request.enqueue=[&](const auto& choice){queued=choice;return ready(enqueue_allowed);};
        request.next_random=[&](auto range){require(range==10000,"one source weighted draw uses range10000");++rng_calls;return ready(random);};
        request.interaction_curves=[&](bool child){++curve_calls;child_seen=child;return ready(curves(flat_child&&child));};
        request.identities_still_match=[&]{return intact;};
    }
    odh::AutonomyIdentity identity(std::int16_t id)const{return {id,world.find(id)->generation};}
    void add_object(std::int16_t id,std::int16_t delta,odh::EntityKind kind=odh::EntityKind::object){
        require(world.create(id,kind,kind==odh::EntityKind::avatar?odh::EntityLayout{0,88,0,16}:odh::EntityLayout{})&&
            world.set_position(id,{id*16,0,1,0})&&world.declare_object_word(id,8,0),"candidate fixture");
        auto table=std::make_shared<odh::Ttab>();table->status=odh::TtabStatus::decoded;table->declared_count=1;
        odh::Interaction row{};row.action=4098;row.test=4099;row.attenuation_code=1;row.joining_index=-1;
        row.motives.resize(16);row.motives[5].delta=delta;table->interactions.push_back(row);
        objects[id]={identity(id),table,true,0,true};inventory.push_back(identity(id));
    }
    odh::Ttab& table(std::int16_t id){return const_cast<odh::Ttab&>(*objects.at(id).table);}
    odh::Interaction& row(std::int16_t id){return table(id).interactions.front();}
    odh::VmStatus run(std::uint16_t opcode=3){
        odh::Instruction instruction{opcode,1,2,{}};
        odh::Bhav root{0x8002,{instruction,{2,254,255,{}},{2,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}};
        vm=std::make_unique<odh::Vm>(odh::RoutineSet{{odh::routine_key(4096),root}},world.vm_memory(),odh::VmLimits{},
            [&](const auto& frame,const auto& op,const auto&,const auto& dispatch){
                request.source_vm=vm.get();request.source_frame=&frame;request.source_instruction=&op;request.source_dispatch=dispatch;
                outcome=odh::find_best_action(request);return outcome;});
        require(vm->start(odh::routine_key(4096),{1,1,1,0},{0,0,0,0})==odh::VmStatus::running,"authenticated VM start");
        return vm->run(1);
    }
    bool success(){return run()==odh::VmStatus::budget_exhausted&&outcome.disposition==odh::PrimitiveDisposition::goto_true;}
};
void scoring_and_shortcuts(){
    Harness base;require(base.success()&&base.queued->callee.id==2&&base.rng_calls==1&&
        std::abs(base.queued->score-1.0f/9.0f)<1e-6f&&base.vm->stack().back().context.stack_object==2,
        "real primitive scores advertisements, consumes weighted RNG, queues and publishes StackObject");
    Harness alias;require(alias.run(65)==odh::VmStatus::budget_exhausted&&alias.queued->callee.id==2,"opcode65 shares complete TS1 implementation");
    Harness shortcut;shortcut.higher=shortcut.identity(3);shortcut.request.interaction_curves={};shortcut.request.ordered_with_autonomy={};
    require(shortcut.world.replace_person_data(1,std::vector<std::int16_t>(34))&&shortcut.success()&&
        !shortcut.queued&&shortcut.rng_calls==0&&shortcut.lookups==0&&shortcut.vm->stack().back().context.stack_object==3,
        "first higher-priority action wins before full motive/curve/inventory requirements");
    Harness none;none.row(2).motives[5]={1000,0,0};none.row(3).motives[5].delta=0;
    require(none.run()==odh::VmStatus::budget_exhausted&&none.outcome.disposition==odh::PrimitiveDisposition::goto_false&&
        none.checks==0&&none.rng_calls==0,"initial AutoInteractions requires nonzero delta, not merely minimum");
    constexpr std::uint8_t weighted_motives[]{5,6,7,8,9,3,13,14,15};
    for(const auto motive:weighted_motives){Harness h;h.row(2).motives[5].delta=0;h.row(2).motives[motive].delta=1000;h.row(3).motives[5].delta=0;
        require(h.success()&&std::abs(h.queued->score-1.0f/9.0f)<1e-6f,"all nine weighted motives use corresponding curve contribution");}
    Harness unused;unused.row(2).motives[5].delta=0;unused.row(2).motives[0].delta=1000;
    require(unused.success()&&unused.queued->callee.id==3,"non-weighted motives do not add score");
    const auto small_curves=[](bool){odh::StringTable table{odh::StringTableState::decoded,-3,{},0};
        for(unsigned i=0;i<9;++i)table.entries.push_back({1,"(0;0);(1000;1)",""});
        return ready(std::make_shared<const odh::Ts1InteractionCurves>(odh::read_ts1_interaction_curves(table)));};
    Harness standing;standing.row(2).motives[5].delta=1;standing.row(3).motives[5].delta=0;standing.request.interaction_curves=small_curves;
    require(standing.success(),"standing uses strict double threshold1e-7");
    Harness sitting;sitting.row(2).motives[5].delta=1;sitting.row(3).motives[5].delta=0;sitting.request.interaction_curves=small_curves;
    require(sitting.world.set_person_data(1,0,1)&&sitting.run()==odh::VmStatus::budget_exhausted&&
        sitting.outcome.disposition==odh::PrimitiveDisposition::goto_false,"sitting uses higher strict threshold1e-6");
    Harness hidden;auto hidden_snapshot=hidden.world.snapshot();hidden_snapshot.entities[0].position.reset();
    require(hidden.world.publish_snapshot(hidden_snapshot)&&hidden.world.declare_object_word(1,34,1)&&hidden.success(),
        "autonomy does not inherit manual-menu Hidden/OOW caller suppression");
    Harness auto_first;auto_first.row(2).flags=1U<<8;auto_first.request.next_random={};
    require(auto_first.success()&&auto_first.rng_calls==0,"top AutoFirst requires no RNG provider");
    Harness later_auto;later_auto.row(3).flags=1U<<8;require(later_auto.success()&&later_auto.rng_calls==1,"only first ranked AutoFirst suppresses RNG");
    Harness dynamic;dynamic.check_effect=[&](auto){require(dynamic.world.set_motive(1,5,10),"live check effect");};
    require(dynamic.success()&&std::abs(dynamic.queued->score-11.0f/9.0f)<1e-5f,"live motive changes affect each score while original base happy stays frozen");
}
void variants_ads_and_personality(){
    Harness variants;variants.variants[2]={{"first",111,{}},{"second",222,{}}};variants.random=7000;
    require(variants.success()&&variants.queued->callee.id==2&&variants.queued->param0==111,
        "source repeats first variant weight rather than deduplicating or selecting later Param0");
    Harness empty_ads;empty_ads.variants[2]={{"empty ads",0,std::map<std::uint32_t,std::int16_t>{}}};
    require(empty_ads.success()&&empty_ads.queued->callee.id==3,"present empty ad dictionary resets all missing ad keys to zero");
    Harness override_min;override_min.row(2).motives[5].personality_modifier=2;
    require(override_min.world.set_person_data(1,2,1000),"inverted personality fixture");
    override_min.variants[2]={{"minimum only",0,std::map<std::uint32_t,std::int16_t>{{5,1000}}}};
    require(override_min.success()&&override_min.queued->callee.id==2,
        "missing personality key resets to zero; min-positive delta-zero fix precedes scoring");
    Harness wrapped;wrapped.row(2).motives[5].minimum=32760;
    require(wrapped.success()&&wrapped.queued->callee.id==3,"min+delta wraps source short before evaluating curve");
    constexpr unsigned indices[]{19,2,2,3,3,4,4,5,5,6,6,7,7,19,10,11,12,19,19,15,19,17,18};
    for(std::uint16_t personality=1;personality<23;++personality){
        Harness h;h.row(3).motives[5].delta=0;h.row(2).motives[5].personality_modifier=personality;
        require(h.world.set_person_data(1,static_cast<std::int16_t>(indices[personality]),750),"personality/skill input");
        const float modifier=personality<13?(personality%2?0.75f:0.25f):1.5f;
        require(h.success()&&std::abs(h.queued->score-modifier/9.0f)<1e-5f,"all source personality inversions and skill/unused selectors");
    }
    for(const auto personality:std::array<std::uint16_t,3>{{0,23,65535}}){Harness h;h.row(2).motives[5].personality_modifier=personality;
        require(h.success()&&std::abs(h.queued->score-1.0f/9.0f)<1e-6f,"unhandled or negative-short personality selectors retain multiplier one");}
    Harness no_strings;no_strings.row(2).test=0;no_strings.row(3).test=0;no_strings.objects[2].strings_present=false;no_strings.objects[3].strings_present=false;
    require(no_strings.run()==odh::VmStatus::budget_exhausted&&no_strings.outcome.disposition==odh::PrimitiveDisposition::goto_false,
        "check-less empty result needs TTAs table");
    Harness generated;generated.objects[2].strings_present=false;generated.variants[2]={{std::nullopt,9,{}}};
    require(generated.success()&&generated.queued->param0==9,"generated variant does not need original label table");
}
void filters_and_top_four(){
    Harness disabled;disabled.objects[2].disabled=1;require(disabled.success()&&disabled.queued->callee.id==3,"disabled object filtered");
    Harness occupied;require(occupied.world.declare_object_word(2,8,32),"Occupied source flag32");
    require(occupied.success()&&occupied.queued->callee.id==3,"Occupied rejects interaction without joining row");
    Harness joining;require(joining.world.declare_object_word(2,8,32),"joining Occupied");
    auto join=joining.row(2);join.motives[5].delta=0;join.label_index=1;join.joining_index=0;joining.table(2).interactions.push_back(join);joining.users[2]=2;
    require(joining.success()&&joining.queued->callee.id==2,"joining exception applies to both Occupied and active UseCount");
    Harness top;top.row(3).motives[5].delta=1000;for(std::int16_t id=4;id<=7;++id)top.add_object(id,1000);
    top.users[2]=1;top.users[3]=1;top.random=9999;
    require(top.success()&&top.queued->callee.id==7,"stable ties and TakeTop4 filter UseCount before limiting");
    Harness grouped;grouped.objects[3].leader=grouped.identity(2);require(grouped.success()&&grouped.checks==1,"interaction group leader deduplicates object pieces");
    Harness oow;auto unpositioned=oow.world.snapshot();unpositioned.entities[1].position.reset();require(oow.world.publish_snapshot(unpositioned),"OOW candidate");
    require(oow.success()&&oow.queued->callee.id==3&&oow.lookups==1,"OOW member is skipped before group/resource lookup");
    Harness child;child.flat_child=true;require(child.world.set_person_data(1,58,0),"source IsChild includes agezero");
    require(child.run()==odh::VmStatus::budget_exhausted&&child.child_seen&&child.outcome.disposition==odh::PrimitiveDisposition::goto_false,
        "child curve selection uses actual source age rule");
    Harness stray;require(stray.world.set_person_data(1,65,8)&&stray.world.set_person_data(1,32,1),"stray pet");
    stray.add_object(4,1000,odh::EntityKind::avatar);stray.objects[4].outside=false;
    require(stray.run()==odh::VmStatus::budget_exhausted&&stray.outcome.disposition==odh::PrimitiveDisposition::goto_false&&stray.checks==0,
        "stray pet ignores objects and indoor avatars");
    stray.objects[4].outside=true;require(stray.success()&&stray.queued->callee.id==4,"stray outdoor avatar remains eligible");
    Harness adult;require(adult.world.set_position(2,{256,0,1,0}),"attenuation distance16");adult.row(2).attenuation_code=2;
    require(adult.success()&&adult.queued->callee.id==3,"adult attenuation table");
    Harness visitor;require(visitor.world.set_position(2,{256,0,1,0})&&visitor.world.set_person_data(1,32,1),"visitor attenuation");visitor.row(2).attenuation_code=2;
    require(visitor.success()&&visitor.queued->callee.id==2,"visitor uses separate weaker attenuation");
    Harness custom;require(custom.world.set_position(2,{256,0,1,0})&&custom.world.set_person_data(1,32,1),"custom attenuation visitor");
    custom.row(2).attenuation_code=99;const float custom_atten=0.1f;std::memcpy(&custom.row(2).attenuation_bits,&custom_atten,sizeof(custom_atten));
    require(custom.success()&&custom.queued->callee.id==3,"out-of-table attenuation code uses authored bits rather than visitor preset");
    Harness floor;require(floor.world.set_position(2,{0,0,2,0}),"other floor");floor.row(2).attenuation_code=2;
    require(floor.success()&&floor.queued->callee.id==3,"floor difference contributes320 units before distance/16");
}
void boundaries_and_failures(){
    Harness zero;zero.random=0;require(zero.success()&&zero.queued->callee.id==2,"RNG zero belongs to first weighted bucket");
    Harness boundary;boundary.row(3).motives[5].delta=1000;boundary.add_object(4,1000);boundary.add_object(5,1000);boundary.random=2500;
    require(boundary.success()&&boundary.queued->callee.id==2,"weighted boundary uses <= rather than strict less-than");
    Harness inf;require(inf.world.set_position(2,{16,0,1,0}),"custom attenuation distance1");inf.row(2).attenuation_code=0;
    const float negative=-1;std::memcpy(&inf.row(2).attenuation_bits,&negative,sizeof(negative));inf.random=9999;
    require(inf.success()&&inf.queued->callee.id==2&&std::isinf(inf.queued->score)&&inf.rng_calls==1,
        "source positive infinity survives score threshold and NaN weighted comparison falls back to first");
    Harness nan;nan.row(2).attenuation_code=0;const float bad=std::numeric_limits<float>::quiet_NaN();
    std::memcpy(&nan.row(2).attenuation_bits,&bad,sizeof(bad));require(nan.success()&&nan.queued->callee.id==3,"NaN score fails strict threshold naturally");
    Harness unknown;unknown.request.check_action=[](auto,const auto&){return odh::AutonomyResult<odh::AutonomyCheck>{};};
    require(unknown.run()==odh::VmStatus::fault&&unknown.outcome.disposition==odh::PrimitiveDisposition::unsupported&&!unknown.queued,
        "unknown AutoCheck is not an authored rejection");
    Harness stale;stale.check_effect=[&](auto id){require(stale.world.remove(id.id)&&stale.world.create(id.id,odh::EntityKind::object),"recycle checked target");};
    require(stale.run()==odh::VmStatus::fault&&!stale.queued,"recycled candidate generation cannot enqueue");
    Harness budget;budget.request.limits.interactions=0;require(budget.run()==odh::VmStatus::fault&&!budget.queued&&budget.rng_calls==0,
        "incomplete candidate enumeration from budget cannot select a partial winner");
    Harness queue_missing;queue_missing.enqueue_allowed=false;require(queue_missing.run()==odh::VmStatus::budget_exhausted&&
        queue_missing.outcome.disposition==odh::PrimitiveDisposition::goto_false,"known missing final GetAction returns source false");
    Harness forged;require(odh::find_best_action(forged.request).disposition==odh::PrimitiveDisposition::unsupported&&forged.rng_calls==0,
        "unproven source dispatch cannot run providers or consume RNG");
}
}
int main(){try{scoring_and_shortcuts();variants_ads_and_personality();filters_and_top_four();boundaries_and_failures();
    std::cout<<"TS1 autonomous action source semantics passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
