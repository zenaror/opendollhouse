#include "odh/live_object_entry_point.hpp"

#include <iostream>
#include <stdexcept>

namespace {
unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
odh::RoutineKey key(std::uint16_t id){return odh::routine_key(id);}
odh::Bhav routine(std::initializer_list<odh::Instruction> instructions,std::uint8_t arguments=4){
    return {0x8002,instructions,{},odh::Bhav::Metadata{0,arguments,1,0}};
}
using State=odh::LiveObjectEntryPointState;
struct Fixture {
    odh::World world;
    std::shared_ptr<const odh::VmProgram> globals=std::make_shared<const odh::VmProgram>(
        odh::RoutineSet{{key(300),routine({{99,254,255,{}}},7)}});
    std::shared_ptr<const odh::VmProgram> bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
        {key(4096),routine({{0,254,255,{}}})},
        {key(4106),routine({{99,254,255,{}}},9)},
        {key(4108),routine({{99,254,255,{}}},11)},
        {key(4109),routine({})},
        {key(4110),routine({{4110,254,255,{}}})}});
    odh::Vm source;odh::Vm registers;
    bool scope=true,identities=true,expire_during=false;
    unsigned effects=0;
    odh::PrimitiveDisposition disposition=odh::PrimitiveDisposition::goto_true;
    std::vector<std::int16_t> arguments;
    odh::VmContext context{};
    std::size_t locals=0;
    explicit Fixture(std::optional<std::vector<odh::EntryPoint>> entries=std::nullopt,bool table_known=true):source({{key(4096),routine({{0,254,255,{}}})}},world.vm_memory(),{},
        [](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};}),
        registers(globals,odh::ShareVmProgramTag{},world.vm_memory(),{},
            [](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};},
            odh::SharedRoutineOwners{{701,bank}},odh::ShareVmOwnerProgramsTag{}) {
        require(world.create(1,odh::EntityKind::avatar)&&world.create(2,odh::EntityKind::object)&&world.create(3,odh::EntityKind::object)&&
            world.declare_object_word(2,25,0)&&world.define_type(2,0x12345678),"construct real ordinary object and explicit StackObject");
        std::vector<odh::EntryPoint> table(10);table[6].action=4106;table[8].action=4108;table[9].action=300;
        if(table_known)require(world.define_entry_points(2,entries.value_or(table)),"install actual object lifecycle entry table");
        source.start(key(4096),{1,1,1});registers.start(key(4096),{2,2,0,701});
        require(source.run(1)==odh::VmStatus::waiting&&registers.run(1)==odh::VmStatus::waiting,
            "park real creator and object Main threads before synchronous lifecycle call");
        registers.temps()[4]=10;registers.temp_xl()[1]=100;
    }
    odh::LiveObjectEntryPointRequest request(std::uint16_t entry=6){
        odh::LiveObjectEntryPointRequest r;r.world=&world;r.object=2;r.object_generation=world.find(2)->generation;
        r.entry_point=entry;const auto& table=world.find(2)->entry_points;
        r.action=table&&entry<table->size()?(*table)[entry].action:0;
        r.creation_source={&source,source.run_generation(),2,r.object_generation,[this]{return scope;}};
        r.code_owner=701;r.global_program=globals;r.owner_program=bank;r.live_memory=world.vm_memory();r.register_owner=&registers;
        r.identities_still_match=[this]{return identities;};
        r.check_primitive=[this](const auto& frame,const auto&,const auto&,const auto& dispatch){
            require(frame.exact_arguments&&dispatch.check_mode,"lifecycle evaluator executes a real exact-argument synchronous check");
            arguments=frame.arguments;context=frame.context;locals=frame.locals.size();++effects;
            require(world.write(frame,3,25,static_cast<std::int16_t>(70+effects)),"lifecycle EP writes actual Caller ObjectData");
            auto* vm=const_cast<odh::Vm*>(dispatch.origin);vm->temps()[4]++;vm->temp_xl()[1]+=2;
            if(expire_during)scope=false;
            return odh::PrimitiveOutcome{disposition,{}};
        };
        return r;
    }
};
void contexts_and_arguments(){
    for(const auto entry:{6,8,9})for(const auto& source_args:std::vector<std::optional<std::vector<std::int16_t>>>{
            std::nullopt,std::vector<std::int16_t>{},std::vector<std::int16_t>{7,-8}}){
        Fixture f;auto r=f.request(static_cast<std::uint16_t>(entry));r.source_arguments=source_args;
        if(entry==8){r.stack_object=3;r.stack_object_generation=f.world.find(3)->generation;}
        const auto source=f.source.snapshot(),main=f.registers.snapshot();
        const auto result=odh::evaluate_live_object_entry_point(r);
        require(result.state==State::evaluated_true&&result.steps==1&&f.arguments==source_args.value_or(std::vector<std::int16_t>(4,0))&&
            f.context.caller==2&&f.context.callee==2&&f.context.stack_object==(entry==8?3:0)&&f.context.code_owner==701&&
            f.locals==static_cast<std::size_t>(entry==6?9:entry==8?11:7),
            "EP6/8/9 preserve source Caller/Callee, nullable or explicit StackObject and exact Args independently of metadata");
        require(f.registers.temps()[4]==11&&f.registers.temp_xl()[1]==102&&f.world.find(2)->object_words.at(25)==71&&
            f.registers.pending()->ticket==main.pending->ticket&&f.registers.steps()==main.steps&&
            f.registers.run_generation()==main.run_generation&&f.registers.stack().back().pc==main.stack.back().pc&&
            f.source.pending()->ticket==source.pending->ticket&&f.source.steps()==source.steps,
            "lifecycle check shares real Thread registers and World effects while preserving both suspended continuations");
    }
}
void authority_and_false(){
    Fixture f;auto good=f.request();
    const auto reject=[&](const auto& change,const char* message){auto r=good;change(r);const auto prior=f.effects;
        require(odh::evaluate_live_object_entry_point(r).state==State::unsupported&&f.effects==prior,message);};
    reject([](auto& r){r.creation_source.source_still_matches={};},"creation lifecycle requires its real ephemeral capability");
    reject([](auto& r){r.creation_source.source_run_generation++;},"stale creator run cannot authorize lifecycle");
    reject([](auto& r){r.object_generation++;},"object epoch mismatch rejects before lifecycle effects");
    reject([&](auto& r){r.creation_source.created_entity=3;r.creation_source.created_generation=f.world.find(3)->generation;},
        "construction capability cannot grant lifecycle on neighboring objects");
    reject([](auto& r){r.action=4108;},"host action must equal the live selected entry table row");
    reject([](auto& r){r.register_owner=nullptr;},"ordinary object lifecycle needs its real Thread register owner");
    reject([&](auto& r){r.register_owner=&f.source;},"unrelated Thread does not contain the actual installed object banks");
    reject([&](auto& r){r.owner_program=std::make_shared<const odh::VmProgram>(*f.bank);},
        "resource-equivalent cloned owner banks do not replace the actual installed pointer");
    reject([&](auto& r){r.global_program=std::make_shared<const odh::VmProgram>(*f.globals);},
        "lifecycle global bank must be the actual installed immutable bank");
    reject([](auto& r){r.stack_object=3;},"explicit StackObject requires its exact generation");
    reject([&](auto& r){r.stack_object_generation=f.world.find(3)->generation;},"null StackObject cannot carry a stale generation");
    reject([](auto& r){r.source_arguments=std::vector<std::int16_t>(4097);},"source lifecycle Args are bounded before allocation or effects");
    reject([](auto& r){r.entry_point=11;r.action=0;},"EP11 architecture wrapper cannot be silently skipped by generic lifecycle execution");
    f.scope=false;reject([](auto&){},"expired construction scope cannot execute lifecycle callbacks");f.scope=true;
    f.identities=false;reject([](auto&){},"host resource and Thread provenance callback is mandatory and live");f.identities=true;
    require(f.world.set_ui_ghost(2,true),"mark unsupported ghost-image sandbox");
    reject([](auto&){},"ghost-image lifecycle sandboxes remain unsupported");require(f.world.set_ui_ghost(2,false),"restore ordinary object identity");
    const auto table=*f.world.find(2)->entry_points;
    Fixture short_table(std::vector<odh::EntryPoint>{});auto absent=short_table.request(60000);
    require(odh::evaluate_live_object_entry_point(absent).state==State::evaluated_false&&short_table.effects==0,
        "known short entry tables return source false even for a high literal entry index");
    for(const auto action:{std::uint16_t{250},std::uint16_t{4199},std::uint16_t{4109}}){auto changed=table;changed[6].action=action;
        Fixture selected(changed);auto r=selected.request();
        require(odh::evaluate_live_object_entry_point(r).state==State::evaluated_false&&selected.effects==0,
            "native, missing and empty entry routines return false without invented effects");}
    Fixture unknown(std::nullopt,false);auto missing=unknown.request();
    require(odh::evaluate_live_object_entry_point(missing).state==State::unsupported,"unknown entry table is not a known absent source entry");
}
void outcomes_and_idle_thread(){
    Fixture f;auto r=f.request();f.disposition=odh::PrimitiveDisposition::goto_false;
    require(odh::evaluate_live_object_entry_point(r).state==State::evaluated_false&&f.effects==1&&f.registers.temps()[4]==11,
        "false lifecycle return retains real Thread and World effects");
    f.disposition=odh::PrimitiveDisposition::wait;const auto nonreturn=odh::evaluate_live_object_entry_point(r);
    require(nonreturn.state==State::unsupported&&nonreturn.steps==1&&f.effects==2&&f.registers.temps()[4]==12&&
        f.world.find(2)->object_words.at(25)==72,"asynchronous lifecycle wait is refused without rolling back earlier effects");
    f.disposition=odh::PrimitiveDisposition::goto_true;f.expire_during=true;
    const auto expired=odh::evaluate_live_object_entry_point(r);
    require(expired.state==State::unsupported&&expired.steps==1&&f.effects==3&&f.registers.temp_xl()[1]==106,
        "capability expiry during lifecycle rejects publication but preserves source side effects");
    Fixture idle;odh::Vm no_main(idle.globals,odh::ShareVmProgramTag{},idle.world.vm_memory(),{}, {},
        odh::SharedRoutineOwners{{701,idle.bank}},odh::ShareVmOwnerProgramsTag{});
    auto idle_request=idle.request(9);idle_request.register_owner=&no_main;
    require(odh::evaluate_live_object_entry_point(idle_request).state==State::evaluated_true&&no_main.run_generation()==0&&
        no_main.status()==odh::VmStatus::ready&&no_main.stack().empty()&&no_main.temps()[4]==1,
        "real idle object Thread without Main retains lifecycle registers without an invented executable");
    auto table=*idle.world.find(2)->entry_points;table[6].action=4110;Fixture recursive(table);
    const auto limited=odh::evaluate_live_object_entry_point(recursive.request());
    require(limited.state==State::unsupported&&limited.steps>0&&recursive.effects==0,
        "native bounded stack rejects unbounded synchronous recursion without treating source List capacity as a hard limit");
}
}
int main(){try{contexts_and_arguments();authority_and_false();outcomes_and_idle_thread();
    std::cout<<checks<<" live object entry point checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"live object entry point: "<<error.what()<<'\n';return 1;}}
