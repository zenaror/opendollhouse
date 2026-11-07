#include "odh/find_best_object_for_function.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
int checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
odh::RoutineKey key(unsigned id){return odh::routine_key(static_cast<std::uint16_t>(id));}
odh::Bhav bhav(std::vector<odh::Instruction> instructions,unsigned args=4){
    return {0x8002,std::move(instructions),{},odh::Bhav::Metadata{0,
        static_cast<std::uint8_t>(args),static_cast<std::uint16_t>(args),0}};
}
odh::Instruction find(unsigned function,unsigned yes=1,unsigned no=2){
    odh::Instruction result{14,static_cast<std::uint8_t>(yes),static_cast<std::uint8_t>(no),{}};
    result.operands[0]=static_cast<std::uint8_t>(function&255);
    result.operands[1]=static_cast<std::uint8_t>((function>>8)&255);
    return result;
}
constexpr unsigned entries[]{18,19,20,21,22,23,24,25,26,27,14,28,16,17,29};
constexpr int score_words[]{31,32,33,36,-1,-1,37,38,-1,-1,61,39,64,65,15};

struct Harness {
    static constexpr odh::CodeOwner condition_owner=901;
    odh::World world;
    std::shared_ptr<const odh::VmProgram> globals;
    std::shared_ptr<const odh::VmProgram> condition_bank;
    std::unique_ptr<odh::Vm> vm;
    std::map<std::pair<std::int16_t,std::uint8_t>,std::int16_t> scores;
    std::map<std::int16_t,std::int16_t> lockouts;
    std::map<std::int16_t,std::uint32_t> disabled;
    std::map<std::int16_t,bool> slot_empty,in_use;
    std::vector<std::int16_t> order;
    unsigned function=0;
    bool has_condition=false,condition_true=true,check_context=false;
    bool move_during_check=false;
    bool recycle_winner_during_check=false,recycled_winner=false;
    odh::FunctionalTreeCodeStatus condition_status=odh::FunctionalTreeCodeStatus::resolved;
    bool condition_routine_missing=false,condition_routine_empty=false;
    unsigned disabled_lookups=0;
    std::uint64_t check_budget=64;
    odh::VmPrimitiveFactory condition_factory;
    bool scheduler_running_now=true;
    const odh::Vm* observed_source=nullptr;

    Harness(unsigned f=10):function(f){
        require(world.create(1,odh::EntityKind::avatar)&&world.create(2,odh::EntityKind::object)&&
                world.create(3,odh::EntityKind::object),"create caller and candidates");
        require(world.define_type(1,1)&&world.define_type(2,2)&&world.define_type(3,3),"type entities");
        auto pos=[&](int id,int x,int y,int level=1){return world.set_position(id,{x,y,static_cast<std::int16_t>(level),0});};
        require(pos(1,0,0)&&pos(2,0,0)&&pos(3,0,0),"position caller and candidates");
        order={1,2,3};
        for(int id:{1,2,3}){
            std::vector<odh::EntryPoint> table(33,{0,0});
            if(id!=1)table[entries[function]]={0,4101};
            require(world.define_entry_points(static_cast<std::int16_t>(id),table),"bind candidate entries");
            slot_empty[id]=true;in_use[id]=false;lockouts[id]=0;disabled[id]=0;
            if(score_words[function]>=0){
                const auto baseline=function==11?800:function==12?15:function==14?600:100;
                scores[{static_cast<std::int16_t>(id),
                    static_cast<std::uint8_t>(score_words[function])}]=static_cast<std::int16_t>(baseline);
            }
        }
        globals=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{key(4096),bhav({
            find(function),{99,254,255,{}},{99,254,255,{}}})}});
        if(!condition_routine_missing)
            condition_bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{key(4200),
                bhav(condition_routine_empty?std::vector<odh::Instruction>{}:
                    std::vector<odh::Instruction>{{98,254,255,{}}})}});
        else condition_bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
    }

    odh::VmPrimitive check_primitive(){
        return [this](const odh::VmFrame& frame,const odh::Instruction& i,
                const odh::Ts1Temps&,const odh::VmDispatch& dispatch){
            check_context=i.opcode==98&&dispatch.check_mode&&frame.context.caller==1&&
                (frame.context.callee==2||frame.context.callee==3)&&
                frame.context.stack_object==frame.context.callee&&frame.context.code_owner==condition_owner&&
                frame.arguments==std::vector<std::int16_t>(4,0);
            auto* check=const_cast<odh::Vm*>(dispatch.origin);
            check->temp_xl()[1]=77;
            if(move_during_check){
                const auto target=frame.context.callee;
                (void)world.set_position(target,{1600,0,1,0});
            }
            if(recycle_winner_during_check&&frame.context.callee==3){
                const bool removed=world.remove(2);
                const bool created=removed&&world.create(2,odh::EntityKind::object)&&world.define_type(2,2)&&
                    world.set_position(2,{0,0,1,0});
                std::vector<odh::EntryPoint> replacement_entries(33,{0,0});
                replacement_entries[entries[function]]={0,4101};
                recycled_winner=created&&world.define_entry_points(2,replacement_entries);
            }
            return odh::PrimitiveOutcome{condition_true?odh::PrimitiveDisposition::goto_true:
                odh::PrimitiveDisposition::goto_false,{}};
        };
    }

    odh::VmPrimitive primitive(){
        return [this](const odh::VmFrame& frame,const odh::Instruction& i,
                const odh::Ts1Temps&,const odh::VmDispatch& dispatch){
            if(i.opcode!=14)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            odh::FindBestObjectForFunctionRequest request;
            observed_source=dispatch.origin;
            request.world=&world;request.source_vm=const_cast<odh::Vm*>(dispatch.origin);request.source_frame=&frame;
            request.source_instruction=&i;request.source_dispatch=dispatch;
            request.ordered_live_entities=[this]()->std::optional<std::vector<odh::FindBestObjectIdentity>>{
                std::vector<odh::FindBestObjectIdentity> result;
                for(auto id:order){const auto* e=world.find(id);if(!e)return {};result.push_back({id,e->generation});}
                return result;
            };
            request.identities_still_match=[] {return true;};
            request.effective_lockout=[this](auto id,auto)->std::optional<std::int16_t>{
                const auto it=lockouts.find(id);return it==lockouts.end()?std::nullopt:std::optional<std::int16_t>{it->second};};
            request.disabled=[this](auto id,auto)->std::optional<std::uint32_t>{
                ++disabled_lookups;
                const auto it=disabled.find(id);return it==disabled.end()?std::nullopt:std::optional<std::uint32_t>{it->second};};
            request.score_value=[this](auto id,auto,auto word)->std::optional<std::int16_t>{
                const auto it=scores.find({id,word});return it==scores.end()?std::nullopt:std::optional<std::int16_t>{it->second};};
            request.slot_zero_empty=[this](auto id,auto)->std::optional<bool>{
                const auto it=slot_empty.find(id);return it==slot_empty.end()?std::nullopt:std::optional<bool>{it->second};};
            request.is_in_use=[this](auto id,auto)->std::optional<bool>{
                const auto it=in_use.find(id);return it==in_use.end()?std::nullopt:std::optional<bool>{it->second};};
            request.resolve_condition=[this](auto id,auto generation,auto routine){
                odh::FunctionalTreeResolvedCode code;code.owner=condition_owner;
                code.key=key(routine);code.global_program=globals;code.owner_program=condition_bank;
                code.memory=world.vm_memory();
                const auto* e=world.find(id);
                if(!e||e->generation!=generation)code.status=odh::FunctionalTreeCodeStatus::unsupported;
                else code.status=condition_status;
                return code;
            };
            request.caller_register_owner=vm.get();request.check_primitive=check_primitive();
            request.check_instruction_budget=check_budget;request.check_primitive_factory=condition_factory;request.scheduler_running_now=scheduler_running_now;
            return odh::find_best_object_for_function(request);
        };
    }

    void start(){
        if(condition_routine_missing)
            condition_bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
        else if(condition_routine_empty)
            condition_bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{key(4200),bhav({})}});
        if(has_condition){
            auto* target=const_cast<odh::Entity*>(world.find(2));
            target->entry_points->at(entries[function]).condition=4200;
        }
        vm=std::make_unique<odh::Vm>(globals,odh::ShareVmProgramTag{},world.vm_memory(),odh::VmLimits{},
            primitive(),odh::SharedRoutineOwners{{condition_owner,condition_bank}},odh::ShareVmOwnerProgramsTag{});
        require(vm->start(key(4096),{1,1,1,0},{0,0,0,0})==odh::VmStatus::running,"start source VM");
    }
    odh::VmStatus run(){return vm->run(1);}
};

void deep_condition_capacity_is_not_a_limit(){
    for(const bool running:{false,true})for(const std::uint64_t budget:{std::uint64_t{3},std::uint64_t{64}}){
        Harness h;h.has_condition=true;h.scheduler_running_now=running;h.check_budget=budget;
        odh::RoutineSet deep;
        for(std::uint16_t id=4200;id<4205;++id)deep.emplace(key(id),bhav({{static_cast<std::uint16_t>(id+1),254,255,{}}}));
        deep.emplace(key(4205),bhav({{98,254,255,{}}}));
        h.condition_bank=std::make_shared<const odh::VmProgram>(std::move(deep));
        h.start();h.vm->temps()[5]=11;h.vm->temp_xl()[1]=29;unsigned leaves=0;
        h.condition_factory=[&](const auto&){return odh::VmPrimitive{[&](const auto&,const auto&,const auto&,const auto& dispatch)->odh::PrimitiveOutcome{
            auto* check=const_cast<odh::Vm*>(dispatch.origin);++leaves;
            require(check->stack().size()==6&&(&check->temps()==&h.vm->temps())==running,
                "a legitimate six-frame condition grows beyond source List capacity5 with the requested register policy");
            check->temps()[5]=99;check->temp_xl()[1]=77;
            return {odh::PrimitiveDisposition::goto_true,{}};
        }};};
        const auto status=h.run();
        if(budget>5)require(status==odh::VmStatus::budget_exhausted&&leaves==1&&h.vm->stack().front().context.stack_object==2&&
            h.vm->temps()[5]==(running?99:11)&&h.vm->temp_xl()[1]==(running?77:29),
            "deep functional conditions succeed in both source alias and clone modes under the native64-frame budget");
        else require(status==odh::VmStatus::fault&&leaves==0&&h.vm->temps()[5]==11&&h.vm->temp_xl()[1]==29&&
            h.vm->diagnostic().find("status=budget_exhausted")!=std::string::npos,
            "lifting an allocation-capacity mistake retains the explicit instruction bound and rejects before deep leaf effects");
    }
}
void nested_check_registers_and_all_functions(){
    for(const bool running:{false,true})for(unsigned function=0;function<15;++function){
        Harness h(function);h.has_condition=true;h.scheduler_running_now=running;h.start();unsigned condition_calls=0;
        h.vm->temps()[0]=7;h.vm->temp_xl()[0]=8;
        odh::VmFrame address;require(h.vm->effective_memory().write(address,12,7,44),"real Caller.Thread has independent advertisement state");
        h.condition_factory=[&](const odh::VmMemory& memory){return odh::VmPrimitive{[&,memory](const odh::VmFrame& frame,const auto&,const auto&,const auto& dispatch)->odh::PrimitiveOutcome{
            ++condition_calls;auto* check=const_cast<odh::Vm*>(dispatch.origin);
            require(dispatch.check_mode&&h.observed_source!=h.vm.get()&&h.observed_source!=check&&
                (&check->temps()==&h.vm->temps())==running&&(&check->temp_xl()==&h.vm->temp_xl())==running&&
                check->temps()[0]==(running?111:7)&&check->temp_xl()[0]==(running?5432:8)&&
                frame.context.caller_generation==h.world.find(1)->generation&&frame.context.callee_generation==h.world.find(2)->generation,
                "primitive14 in a real Check borrows actual Caller.Thread registers and propagates exact candidate identities");
            require(memory.read(frame,12,7)==0&&memory.write(frame,12,9,66),"nested functional condition uses its own fresh advertisement dictionary and effective primitive memory");
            check->temps()[0]=222;check->temps()[5]=77;check->temp_xl()[0]=9999;
            require(h.observed_source->temps()[0]==(running?222:111)&&h.vm->temps()[5]==(running?77:0),
                "condition changes alias during scheduler ticks and clone the actual Caller.Thread outside them");
            return {odh::PrimitiveDisposition::goto_true,{}};
        }};};
        const auto root=key(4300);auto selection=find(function,254,255);
        odh::Vm outer({{root,bhav({{2,1,255,{0,0,111,0,0,5,8,7}},
            {2,2,255,{0,0,56,21,0,5,42,7}},{2,3,255,{7,0,55,0,0,5,12,7}},selection})}},h.world.vm_memory(),{},h.primitive());
        outer.start(root,{1,1,1});
        const auto result=outer.run_synchronous_check_with_registers(*h.vm,running,32,h.primitive());
        require(result.started&&result.status==odh::VmStatus::returned_true&&condition_calls==1&&h.vm->temps()[0]==(running?222:7)&&
            h.vm->temp_xl()[0]==(running?9999:8)&&outer.temps()[5]==(running?77:0)&&outer.advertisement_changes()->at((1U<<16)|7)==55&&
            !outer.advertisement_changes()->count((1U<<16)|9)&&h.vm->advertisement_changes()->at((1U<<16)|7)==44&&
            h.vm->stack().front().context.stack_object==1,
            "all fifteen function modes work synchronously without losing nested registers or corrupting parent execution/ads");
    }
}
void exact_caller_reference_and_diagnostics(){
    Harness h;h.has_condition=true;h.start();const auto old=h.world.find(1)->generation;
    require(h.world.remove_retaining_body({1,old})&&h.world.create(1,odh::EntityKind::avatar)&&
        h.world.define_type(1,101)&&h.world.define_entry_points(1,std::vector<odh::EntryPoint>(33,{0,0}))&&
        h.world.set_position(1,{1600,0,1,0})&&h.world.set_position(3,{1600,0,1,0}),"replace numeric Caller while its source frame retains the old exact reference");
    unsigned conditions=0;
    h.condition_factory=[&](const auto&){return odh::VmPrimitive{[&](const odh::VmFrame& frame,const auto&,const auto&,const auto&)->odh::PrimitiveOutcome{
        ++conditions;require(frame.context.caller_generation==old&&frame.context.callee_generation==h.world.find(2)->generation&&
            frame.context.stack_object_generation==h.world.find(2)->generation,
            "condition Caller copies its old source pointer while candidate roles capture the exact current generation");
        return {odh::PrimitiveDisposition::goto_true,{}};
    }};};
    require(h.run()==odh::VmStatus::budget_exhausted&&conditions==1&&h.vm->stack().front().context.stack_object==2,
        "distance scoring uses original Caller body rather than the new object with its reused numeric ID");
    Harness broken;broken.has_condition=true;
    broken.condition_factory=[](const auto&){return odh::VmPrimitive{[](const auto&,const auto&,const auto&,const auto&){
        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"nested service missing "+std::string(2000,'x')};
    }};};broken.start();
    require(broken.run()==odh::VmStatus::fault&&broken.vm->diagnostic().find("function=10 candidate=2 generation=")!=std::string::npos&&
        broken.vm->diagnostic().find("condition=4200 owner=901 status=fault detail=")!=std::string::npos&&
        broken.vm->diagnostic().find("nested service missing")!=std::string::npos&&broken.vm->diagnostic().size()<1024,
        "unsupported nested condition reports bounded source function/candidate/routine/owner and the real inner failure");
    for(const auto version:{std::uint16_t{0x8000},std::uint16_t{0x8001}}){
        Harness legacy;legacy.has_condition=true;auto body=bhav({{98,254,255,{}}});body.version=version;body.metadata.reset();
        legacy.condition_bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{key(4200),body}});legacy.start();
        require(legacy.run()==odh::VmStatus::budget_exhausted&&legacy.check_context,"functional checks admit supported legacy BHAV execution profiles");
    }
}
void mapping_and_surface_fallback(){
    for(unsigned function=0;function<15;++function){
        Harness h(function);h.order={1,2};h.start();
        require(h.run()==odh::VmStatus::budget_exhausted,"execute one source primitive");
        require(h.vm->stack().front().context.stack_object==2,
            "each function id uses its source entry and score mapping");
        if(score_words[function]>=0)
            require(h.scores.find({2,static_cast<std::uint8_t>(score_words[function])})!=h.scores.end(),
                "mapped scored function reads its corresponding ObjectData word");
    }
    Harness surface(10);surface.order={1,2};surface.slot_empty[2]=false;surface.start();
    require(surface.run()==odh::VmStatus::budget_exhausted,"run slot fallback case");
    require(surface.vm->stack().front().context.stack_object==1,
        "slot-zero fallback rejects occupied surface and preserves StackObject");
    Harness condition(10);condition.order={1,2};condition.has_condition=true;
    condition.slot_empty[2]=false;condition.start();
    require(condition.run()==odh::VmStatus::budget_exhausted,"run condition case");
    require(condition.vm->stack().front().context.stack_object==2&&condition.check_context&&
        condition.vm->temp_xl()[1]==77,
        "condition replaces slot fallback and shares Caller TempXL effects");
}

void score_filters_distance_and_ties(){
    Harness h(10);h.order={1,2,3};h.scores[{2,61}]=500;h.scores[{3,61}]=500;
    require(h.world.set_position(2,{48,0,1,0})&&h.world.set_position(3,{0,48,1,0}),"set equal-distance positions");
    h.start();require(h.run()==odh::VmStatus::budget_exhausted,"run tie case");
    require(h.vm->stack().front().context.stack_object==2,"strict score tie preserves first ascending ObjectID");
    Harness thresholds(11);thresholds.order={1,2,3};thresholds.scores[{2,39}]=799;thresholds.scores[{3,39}]=800;
    thresholds.start();require(thresholds.run()==odh::VmStatus::budget_exhausted,"run DirtyLevel case");
    require(thresholds.vm->stack().front().context.stack_object==3,
        "DirtyLevel threshold rejects 799 and admits 800");
    Harness gardening(12);gardening.order={1,2,3};gardening.scores[{2,64}]=14;gardening.scores[{3,64}]=15;
    gardening.start();require(gardening.run()==odh::VmStatus::budget_exhausted,"run GardeningValue case");
    require(gardening.vm->stack().front().context.stack_object==3,
        "GardeningValue threshold rejects 14 and admits 15");
    Harness repair(14);repair.order={1,2,3};repair.scores[{2,15}]=599;repair.scores[{3,15}]=600;
    repair.start();require(repair.run()==odh::VmStatus::budget_exhausted,"run RepairState case");
    require(repair.vm->stack().front().context.stack_object==3,
        "TS1 RepairState threshold rejects 599 and admits 600");
}

void filters_conditions_and_unknowns(){
    Harness h(10);h.order={1,2,3};h.lockouts[2]=1;h.disabled[3]=1;h.start();
    require(h.run()==odh::VmStatus::budget_exhausted,"run lockout/disabled case");
    require(h.vm->stack().front().context.stack_object==1,"lockout and disabled candidates are skipped");
    Harness short_circuit(10);short_circuit.order={1,2};short_circuit.lockouts[2]=1;
    short_circuit.disabled.erase(2);short_circuit.start();
    require(short_circuit.run()==odh::VmStatus::budget_exhausted&&short_circuit.disabled_lookups==0,
        "positive lockout skips before querying Disabled");
    Harness check(10);check.order={1,2};check.has_condition=true;check.condition_true=false;check.start();
    require(check.run()==odh::VmStatus::budget_exhausted,"run false-condition case");
    require(check.vm->stack().front().context.stack_object==1&&check.check_context,
        "false condition excludes candidate and preserves original StackObject");
    Harness in_use(10);in_use.order={1,2,3};in_use.in_use[2]=true;in_use.start();
    require(in_use.run()==odh::VmStatus::budget_exhausted,"run in-use case");
    require(in_use.vm->stack().front().context.stack_object==3,"IsInUse candidate skipped after scoring gate");

    Harness missing_routine(10);missing_routine.order={1,2};missing_routine.has_condition=true;
    missing_routine.condition_routine_missing=true;missing_routine.start();
    require(missing_routine.run()==odh::VmStatus::budget_exhausted&&
        missing_routine.vm->stack().front().context.stack_object==2&&!missing_routine.check_context,
        "known target bank missing condition BHAV skips condition and remains eligible");
    Harness empty_routine(10);empty_routine.order={1,2};empty_routine.has_condition=true;
    empty_routine.condition_routine_empty=true;empty_routine.start();
    require(empty_routine.run()==odh::VmStatus::budget_exhausted&&
        empty_routine.vm->stack().front().context.stack_object==1&&!empty_routine.check_context,
        "known empty condition BHAV evaluates false without starting a check VM");
    Harness absent_resource(10);absent_resource.order={1,2};absent_resource.has_condition=true;
    absent_resource.condition_status=odh::FunctionalTreeCodeStatus::target_resource_absent;
    absent_resource.start();
    require(absent_resource.run()==odh::VmStatus::budget_exhausted&&
        absent_resource.vm->stack().front().context.stack_object==2,
        "proven missing target resource follows source no-condition execution path");
    Harness unresolved(10);unresolved.order={1,2};unresolved.has_condition=true;
    unresolved.condition_status=odh::FunctionalTreeCodeStatus::unsupported;unresolved.start();
    require(unresolved.run()==odh::VmStatus::fault&&unresolved.vm->stack().front().context.stack_object==1,
        "unresolved condition provenance faults instead of selecting false");
    Harness malformed(10);malformed.order={1,3,2};malformed.start();
    require(malformed.run()==odh::VmStatus::fault&&malformed.vm->stack().front().context.stack_object==1,
        "incomplete source ordering evidence faults without changing StackObject");
}

void condition_rechecks_position_and_no_candidate_preserves_stack(){
    Harness moved(10);moved.order={1,2,3};moved.has_condition=true;moved.move_during_check=true;
    moved.scores[{2,61}]=300;moved.scores[{3,61}]=150;moved.start();
    require(moved.run()==odh::VmStatus::budget_exhausted,"run moved-position case");
    require(moved.vm->stack().front().context.stack_object==3,
        "condition side effects change candidate distance before selection");
    Harness recycled(10);recycled.order={1,2,3};recycled.has_condition=true;
    recycled.recycle_winner_during_check=true;recycled.scores[{2,61}]=300;
    recycled.scores[{3,61}]=200;
    auto* third=const_cast<odh::Entity*>(recycled.world.find(3));
    third->entry_points->at(entries[10]).condition=4200;
    recycled.start();
    require(recycled.run()==odh::VmStatus::fault&&recycled.recycled_winner&&
        recycled.vm->stack().front().context.stack_object==1,
        "recycled prior winner is rejected before its reused ID can be written as StackObject");
    Harness none(10);none.order={1,2};none.lockouts[2]=1;none.start();
    require(none.run()==odh::VmStatus::budget_exhausted,"run no-candidate case");
    require(none.vm->stack().front().context.stack_object==1,
        "no eligible candidate returns false without changing StackObject");
}
}

int main(){
    try {
        deep_condition_capacity_is_not_a_limit();
        nested_check_registers_and_all_functions();exact_caller_reference_and_diagnostics();
        mapping_and_surface_fallback();score_filters_distance_and_ties();
        filters_conditions_and_unknowns();condition_rechecks_position_and_no_candidate_preserves_stack();
        std::cout<<"FindBestObjectForFunction: "<<checks<<" checks passed\n";return 0;
    } catch(const std::exception& error){std::cerr<<"FindBestObjectForFunction: "<<error.what()<<'\n';return 1;}
}
