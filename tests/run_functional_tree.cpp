#include "odh/run_functional_tree.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
int checks=0;
void require(bool ok,const char* why) {++checks;if(!ok)throw std::runtime_error(why);}

odh::Instruction tree(unsigned function,unsigned flags=0,unsigned yes=1,unsigned no=2) {
    odh::Instruction result{20,static_cast<std::uint8_t>(yes),static_cast<std::uint8_t>(no),{}};
    result.operands[0]=static_cast<std::uint8_t>(function&255);
    result.operands[1]=static_cast<std::uint8_t>((function>>8)&255);
    result.operands[2]=static_cast<std::uint8_t>(flags);
    return result;
}
constexpr unsigned function_entries[]{18,19,20,21,22,23,24,25,26,27,14,28,16,17,29};
odh::Bhav bhav(std::vector<odh::Instruction> ins,unsigned args=4) {
    return {0x8002,std::move(ins),{},odh::Bhav::Metadata{0,static_cast<std::uint8_t>(args),
        static_cast<std::uint16_t>(args),0}};
}
odh::RoutineKey key(unsigned id) {return odh::routine_key(static_cast<std::uint16_t>(id));}

struct Harness {
    static constexpr odh::CodeOwner owner=701;
    odh::World world;
    std::shared_ptr<const odh::VmProgram> globals;
    std::shared_ptr<const odh::VmProgram> target_bank;
    odh::RoutineSet target_routines;
    std::unique_ptr<odh::Vm> source;
    odh::VmPrimitive check_primitive;
    odh::Vm::OwnerResolver owner_resolver;
    bool target_live=true,identity=true,action_tree=true,forge_dispatch=false;
    bool check_context=false,icon_committed=false,change_action_on_check=false;
    std::uint16_t wrong_resolved_key_id=65535,changed_action=4103;
    std::vector<std::uint16_t> resolved_ids;
    std::uint64_t check_budget=64;
    odh::FunctionalTreeCodeStatus condition_status=odh::FunctionalTreeCodeStatus::resolved;
    odh::FunctionalTreeCodeStatus action_status=odh::FunctionalTreeCodeStatus::resolved;

    Harness(unsigned function=0,unsigned flags=0,unsigned condition=0,
            unsigned action=4101,unsigned action_args=3) {
        require(world.create(1,odh::EntityKind::avatar)&&world.create(2,odh::EntityKind::object),
            "create functional tree caller and target");
        require(world.define_type(1,1)&&world.define_type(2,2),"type functional tree entities");
        std::vector<odh::EntryPoint> entries(33,{0,0});
        if(function<15)entries[function_entries[function]]={
            static_cast<std::uint16_t>(condition),static_cast<std::uint16_t>(action)};
        require(world.define_entry_points(2,entries),"bind target entry point table");
        target_routines.emplace(key(4101),bhav({{99,254,255,{}}},action_args));
        globals=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{key(4096),bhav({
            tree(function,flags),{99,254,255,{}},{99,254,255,{}}},4)}});
        target_bank=std::make_shared<const odh::VmProgram>(target_routines);
        check_primitive=[this](const odh::VmFrame& frame,const odh::Instruction& instruction,
                const odh::Ts1Temps&,const odh::VmDispatch& dispatch){
            check_context=instruction.opcode==98&&dispatch.check_mode&&
                frame.context.caller==1&&frame.context.callee==2&&frame.context.stack_object==2&&
                frame.context.code_owner==owner&&frame.arguments==std::vector<std::int16_t>(4,0);
            auto* vm=const_cast<odh::Vm*>(dispatch.origin);
            vm->temp_xl()[1]=909;
            // Simulate the source's live EntryPoints object changing during a
            // check; World intentionally exposes its runtime bank read-only.
            if(change_action_on_check)
                const_cast<odh::Entity*>(world.find(2))->entry_points->at(18).action=changed_action;
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        };
    }

    odh::VmPrimitive primitive() {
        return [this](const odh::VmFrame& frame,const odh::Instruction& instruction,
                const odh::Ts1Temps&,const odh::VmDispatch& dispatch){
            if(instruction.opcode!=20)
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            odh::RunFunctionalTreeRequest request;
            request.world=&world;request.source_vm=source.get();request.source_frame=&frame;
            request.source_instruction=&instruction;request.source_dispatch=dispatch;
            if(forge_dispatch)++request.source_dispatch.ticket;
            request.target_is_live=[this](auto id,auto gen){const auto* e=world.find(id);return
                target_live&&e&&e->generation==gen;};
            request.identities_still_match=[this]{return identity;};
            request.resolve_routine=[this](std::int16_t target,std::uint64_t generation,std::uint16_t id){
                odh::FunctionalTreeResolvedCode result;
                result.status=id==4102?condition_status:action_status;result.owner=owner;result.key=key(id);
                resolved_ids.push_back(id);
                if(wrong_resolved_key_id==id)result.key=key(4103);
                result.global_program=globals;result.owner_program=target_bank;
                result.memory=world.vm_memory();result.owner_resolver=owner_resolver;
                if(target!=2||!world.find(target)||world.find(target)->generation!=generation)
                    result.status=odh::FunctionalTreeCodeStatus::unsupported;
                return result;
            };
            request.caller_register_owner=source.get();request.check_primitive=check_primitive;
            request.check_instruction_budget=check_budget;
            request.action_tree_for=[this](const auto&,const auto&,const auto&)->std::optional<bool>{return action_tree;};
            request.prepare_icon_update=[this](const auto&,const auto&,const auto&,
                    std::int16_t icon,std::uint64_t generation,const std::optional<odh::VmCall>& child)
                    ->std::optional<odh::FunctionalTreeIconUpdate>{
                if(icon!=2||!world.find(icon)||world.find(icon)->generation!=generation)return std::nullopt;
                if(child&&child->key.id!=4101)return std::nullopt;
                return odh::FunctionalTreeIconUpdate{
                    []{return true;},[this]{icon_committed=true;return true;}};
            };
            return odh::run_functional_tree(request);
        };
    }

    void start(unsigned function=0,unsigned flags=0,unsigned main_pc=0) {
        (void)function;(void)flags;(void)main_pc;
        source=std::make_unique<odh::Vm>(globals,odh::ShareVmProgramTag{},world.vm_memory(),
            odh::VmLimits{},primitive(),odh::SharedRoutineOwners{{owner,target_bank}},odh::ShareVmOwnerProgramsTag{});
        require(source->start(key(4096),{1,1,2,0},{0,0,0,0})==odh::VmStatus::running,
            "start RunFunctionalTree Main");
    }
};

void mapping_and_operand_cases() {
    for(unsigned function=0;function<15;++function) {
        Harness h(function);h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.source->stack().size()==2&&
            h.source->stack()[1].key.id==4101&&h.source->stack()[1].context.callee==2&&
            h.source->stack()[1].context.stack_object==2&&
            h.source->stack()[0].pc==0&&h.source->stack()[1].exact_arguments&&
            h.source->stack()[1].arguments.size()==3,
            "mapped RunFunctionalTree pushes exact target action frame");
        require(function_entries[function]<33&&
            h.world.find(2)->entry_points->at(function_entries[function]).action==4101,
            "pinned function map selects its corresponding target entry point");
    }
    {
        Harness h(65535);h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.source->stack().size()==1&&
            h.source->stack()[0].pc==2,"function 65535 selects source false branch");
    }
    {
        Harness h(15);h.start();
        require(h.source->run(1)==odh::VmStatus::fault,"invalid function mapping faults instead of false");
    }
    {
        Harness h(0);h.forge_dispatch=true;h.start();
        require(h.source->run(1)==odh::VmStatus::fault&&h.source->stack().size()==1,
            "forged opcode-20 dispatch is rejected before child publication");
    }
    {
        Harness h(0);h.target_live=false;h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.source->stack().size()==1&&
            h.source->stack()[0].pc==2,"dead or unavailable target selects source false");
    }
    {
        Harness h(0);h.identity=false;h.start();
        require(h.source->run(1)==odh::VmStatus::fault&&h.source->stack().size()==1,
            "stale caller/resource authority faults instead of selecting ordinary false");
    }
    {
        Harness h(0,0,0,0);h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.source->stack().size()==1&&
            h.source->stack()[0].pc==2,"target entry with action zero selects false");
    }
}

void check_and_missing_code_cases() {
    {
        Harness h(0,0,4102);h.target_routines.emplace(key(4102),bhav({{98,254,255,{}}},7));
        h.target_bank=std::make_shared<const odh::VmProgram>(h.target_routines);h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.check_context&&
            h.source->temp_xl()[1]==909&&h.source->stack().size()==2&&
            h.source->steps()==1&&h.source->snapshot().ticket_sequence==1,
            "condition uses caller registers, exact four zero Args, target context and disposable stack depth");
    }
    {
        Harness h(0,0,4102);h.target_routines.emplace(key(4102),bhav({{98,254,255,{}}}));
        h.target_bank=std::make_shared<const odh::VmProgram>(h.target_routines);
        h.check_primitive=[](const auto&,const auto&,const auto&,const auto&){
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_false,{}};};
        h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.source->stack().size()==1&&
            h.source->stack()[0].pc==2,"condition false branches without action push");
    }
    {
        Harness h(0,0,4102);h.condition_status=odh::FunctionalTreeCodeStatus::target_resource_absent;h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.source->stack().size()==2,
            "proven missing condition resource follows source execute path");
    }
    {
        Harness h(0,0,4102);h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.source->stack().size()==2,
            "known bank with absent condition key follows source execute path");
    }
    {
        Harness h(0,0,4102);h.change_action_on_check=true;
        h.target_routines.emplace(key(4102),bhav({{98,254,255,{}}}));
        h.target_bank=std::make_shared<const odh::VmProgram>(h.target_routines);h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.source->stack().size()==1&&
            h.source->stack()[0].pc==2&&h.resolved_ids==std::vector<std::uint16_t>{4102,4103},
            "action function is reread after condition effects instead of using the stale row value");
    }
    {
        Harness h(0,0,4102);h.target_routines.emplace(key(4102),bhav({{98,254,255,{}}}));
        h.target_bank=std::make_shared<const odh::VmProgram>(h.target_routines);
        h.wrong_resolved_key_id=4102;h.start();
        require(h.source->run(1)==odh::VmStatus::fault&&h.source->stack().size()==1,
            "condition resource resolver cannot substitute a different BHAV key");
    }
    {
        Harness h(0,0,4102);h.target_routines.emplace(key(4102),bhav({{98,254,255,{}}}));
        h.target_bank=std::make_shared<const odh::VmProgram>(h.target_routines);
        auto globals=h.globals->routines();globals.emplace(key(0),bhav({{99,254,255,{}}},0));
        h.globals=std::make_shared<const odh::VmProgram>(std::move(globals));
        h.change_action_on_check=true;h.changed_action=0;h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.source->stack().size()==2&&
            h.source->stack().back().key.id==0&&h.source->stack().back().arguments.empty(),
            "condition-mutated zero action still resolves the exact global BHAV as source does");
    }
    {
        Harness h(0,0,4102);h.target_routines.emplace(key(4102),bhav({}));
        h.target_bank=std::make_shared<const odh::VmProgram>(h.target_routines);h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.source->stack().size()==1&&
            h.source->stack()[0].pc==2,"known empty condition BHAV evaluates false");
    }
    {
        Harness h(0,0,0,4103);h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.source->stack().size()==1&&
            h.source->stack()[0].pc==2,"missing action key in a known exact bank selects false");
    }
    {
        Harness h;h.wrong_resolved_key_id=4101;h.start();
        require(h.source->run(1)==odh::VmStatus::fault&&h.source->stack().size()==1,
            "action resource resolver cannot substitute a different BHAV key");
    }
    {
        Harness h;h.action_status=odh::FunctionalTreeCodeStatus::target_resource_absent;h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.source->stack().size()==1&&
            h.source->stack()[0].pc==2,"proven missing action resource selects source false");
    }
    {
        Harness h;h.action_status=odh::FunctionalTreeCodeStatus::unsupported;h.start();
        require(h.source->run(1)==odh::VmStatus::fault&&h.source->stack().size()==1,
            "unresolved action resource is unsupported rather than source false");
    }
    {
        Harness h(0,0,4102);h.target_routines.emplace(key(4102),bhav({{98,254,255,{}}}));
        h.target_bank=std::make_shared<const odh::VmProgram>(h.target_routines);
        h.check_primitive=[](const auto&,const auto&,const auto&,const auto&){
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};};
        h.start();
        require(h.source->run(1)==odh::VmStatus::fault&&h.source->stack().size()==1,
            "nonterminal condition check does not get misreported as ordinary false");
    }
    {
        Harness h;h.check_budget=0;h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.source->stack().size()==2,
            "no condition does not require a check budget");
    }
}

void condition_resource_context_cases(){
    const auto named=odh::named_routine_key(42,false);
    const auto named_bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{named,
        bhav({{2,254,255,{0,0,17,0,0,2,8,7}}},0)}});
    for(const bool supply:{false,true})for(const std::int16_t value:{std::int16_t{17},std::int16_t{18}}){
        Harness h(0,0,4102);unsigned lookups=0,implicit=0;
        h.target_routines.emplace(key(4102),bhav({{28,254,255,{}}},0));
        h.target_bank=std::make_shared<const odh::VmProgram>(h.target_routines);
        if(supply)h.owner_resolver=[&](odh::CodeOwner owner)->std::shared_ptr<const odh::VmProgram>{
            ++lookups;require(owner==702,"functional condition requests exact supplied named-resource namespace");return named_bank;};
        h.check_primitive=[&](const auto& frame,const auto& instruction,const auto&,const odh::VmDispatch& dispatch)->odh::PrimitiveOutcome{
            require(instruction.opcode==28&&dispatch.origin&&dispatch.origin->matches_active_query(frame,instruction,dispatch),
                "functional named lookup uses the actual disposable condition frame");
            auto context=frame.context;context.code_owner=702;
            const auto result=const_cast<odh::Vm*>(dispatch.origin)->run_named_in_stack(named,context,{},16);
            if(!result.started||(result.status!=odh::VmStatus::returned_true&&result.status!=odh::VmStatus::returned_false))
                return {odh::PrimitiveDisposition::unsupported,result.diagnostic};
            return {result.status==odh::VmStatus::returned_true?odh::PrimitiveDisposition::goto_true:odh::PrimitiveDisposition::goto_false,{}};
        };
        h.start();require(h.source->set_owner_resolver([&](auto)->std::shared_ptr<const odh::VmProgram>{++implicit;return named_bank;}),
            "real functional Caller retains its independent resource provider");
        h.source->temps()[0]=value;const auto status=h.source->run(1);
        require(implicit==0&&!h.source->shared_program_for_owner(702),"condition clone never borrows owner policy or installs named banks on its register donor");
        if(!supply)require(status==odh::VmStatus::fault&&lookups==0&&h.source->stack().size()==1&&h.source->stack()[0].pc==0,
            "closed functional condition resources fail before parent continuation is advanced");
        else require(status==odh::VmStatus::budget_exhausted&&lookups==1&&
            (value==17?(h.source->stack().size()==2&&h.source->stack().back().context.code_owner==Harness::owner):
                       (h.source->stack().size()==1&&h.source->stack()[0].pc==2)),
            "explicit condition resource context executes authored true/false and preserves the real parent/action owners");
    }
}
void icon_and_empty_cases() {
    {
        Harness h(0,1);h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.source->stack().size()==2&&
            h.icon_committed,"icon owner commits only after child frame acceptance");
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.icon_committed&&
            h.source->stack()[0].pc==1,"icon owner commits after child push and ordinary return resumes parent");
    }
    {
        Harness h(0,0);h.action_tree=false;h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.source->stack().size()==2&&
            !h.icon_committed,"non-ActionTree calls ignore icon flag");
    }
    {
        Harness h(0,2);h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.icon_committed,
            "any positive source Flags value changes the active ActionTree icon");
    }
    {
        Harness h(0,0,0,4101,0);h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.source->stack().size()==2&&
            h.source->stack()[1].exact_arguments&&h.source->stack()[1].arguments.empty(),
            "nonempty action receives its exact zero-length source argument array");
    }
    {
        Harness h(0,1);h.target_routines[key(4101)]=bhav({},0);
        h.target_bank=std::make_shared<const odh::VmProgram>(h.target_routines);h.start();
        require(h.source->run(1)==odh::VmStatus::budget_exhausted&&h.source->stack().size()==1&&
            h.source->stack()[0].pc==0&&h.icon_committed,
            "empty action repeats RunFunctionalTree and commits icon without selecting false");
    }
}
}

int main() {
    try {
        condition_resource_context_cases();
        mapping_and_operand_cases();check_and_missing_code_cases();icon_and_empty_cases();
        std::cout<<"run_functional_tree: "<<checks<<" checks passed\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<"run_functional_tree: "<<error.what()<<'\n';return 1;
    }
}
