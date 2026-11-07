#include "odh/live_entry_point5.hpp"
#include "odh/routing.hpp"

#include <iostream>
#include <sstream>

namespace {
int checks=0;
void require(bool ok,const char* message) {++checks;if(!ok)throw std::runtime_error(message);}
odh::RoutineKey key(unsigned id) {return odh::routine_key(static_cast<std::uint16_t>(id));}
odh::Bhav routine(std::initializer_list<odh::Instruction> instructions) {
    return {0x8002,instructions,{},odh::Bhav::Metadata{0,4,4,0}};
}
odh::Instruction snap(std::uint8_t opcode=46) {return {opcode,254,255,{0,0,0,0,0,0,0,0}};}

struct Harness {
    odh::World world;
    odh::CodeOwner source_owner=700;
    odh::CodeOwner tested_owner=701;
    std::shared_ptr<const odh::VmProgram> globals=
        std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
    std::shared_ptr<const odh::VmProgram> source_bank;
    std::shared_ptr<const odh::VmProgram> tested_bank=
        std::make_shared<const odh::VmProgram>(odh::RoutineSet{
            {key(4100),routine({{99,254,255,{}}})},
            {key(4101),routine({{99,255,254,{}}})}});
    std::unique_ptr<odh::Vm> source;
    odh::Vm* source_vm_ptr=nullptr;
    std::optional<odh::LiveEntryPoint5Result> result;
    bool source_state_unchanged_inside=false;
    bool context_correct=false;
    bool identities_valid=true;
    unsigned random_calls=0;
    std::uint64_t check_budget=4096;

    Harness() {
        require(world.create(1,odh::EntityKind::avatar)&&
            world.create(2,odh::EntityKind::object)&&world.declare_object_word(1,25,0)&&
            world.declare_object_word(2,25,0),
            "create live World entities for EP5");
        source_bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
            {key(4096),routine({snap()})}});
    }

    void start(odh::Vm* tested_thread=nullptr,bool no_thread=true,
               bool ghost_sandbox=false,std::uint16_t action=4100,
               bool invert_source_dispatch=false,
               odh::VmPrimitive check_override={},std::int16_t tested_id=2,
               std::int16_t other_id=1,bool route_placement_source=false) {
        source_state_unchanged_inside=false;context_correct=false;result.reset();
        auto check=check_override?std::move(check_override):odh::VmPrimitive{
            [&,tested_id,other_id](const odh::VmFrame& frame,const odh::Instruction& instruction,
                const odh::Ts1Temps&,const odh::VmDispatch& dispatch){
                context_correct=instruction.opcode==99&&frame.context.caller==tested_id&&
                    frame.context.callee==tested_id&&frame.context.stack_object==other_id&&
                    frame.context.code_owner==tested_owner&&frame.exact_arguments&&frame.arguments.size()==4&&
                    frame.arguments[0]==other_id&&frame.arguments[1]==0&&
                    frame.arguments[2]==0&&frame.arguments[3]==0&&dispatch.check_mode;
                (void)world.write(frame,3,25,73);
                auto* check_vm=const_cast<odh::Vm*>(dispatch.origin);
                check_vm->temps()[4]=88;
                check_vm->temp_xl()[1]=1234;
                ++random_calls;
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            }};
        const odh::VmPrimitive source_primitive=[&,tested_id,other_id,no_thread,
                ghost_sandbox,action,invert_source_dispatch,check,tested_thread](const odh::VmFrame& frame,
                const odh::Instruction& instruction,const odh::Ts1Temps&,
                const odh::VmDispatch& dispatch){
            auto altered=dispatch;
            if(invert_source_dispatch)++altered.ticket;
            odh::LiveEntryPoint5Request request;
            request.world=&world;request.tested_entity=tested_id;request.other_entity=other_id;
            request.tested_generation=world.find(tested_id)->generation;
            request.other_generation=world.find(other_id)->generation;
            request.action=action;request.ghost=0;
            request.source_vm=source_vm_ptr;request.source_frame=&frame;
            request.source_instruction=&instruction;request.source_dispatch=altered;
            if(route_placement_source)request.route_placement_proof=odh::RoutePlacementProof{};
            request.code_owner=tested_owner;request.global_program=globals;
            request.owner_program=tested_bank;request.live_memory=world.vm_memory();
            request.register_owner=tested_thread?tested_thread:(tested_id==1?source_vm_ptr:nullptr);
            request.no_thread_proven=no_thread&&request.register_owner==nullptr;
            request.ghost_sandbox=ghost_sandbox;
            request.identities_still_match=[&]{return identities_valid&&world.find(tested_id)&&world.find(other_id);};
            request.check_primitive=check;request.instruction_budget=check_budget;
            result=odh::evaluate_live_entry_point5(request);
            source_state_unchanged_inside=source_vm_ptr&&source_vm_ptr->status()==odh::VmStatus::running&&
                source_vm_ptr->steps()==dispatch.ticket&&source_vm_ptr->stack().size()==1&&
                source_vm_ptr->stack().back().pc==0&&dispatch.ticket!=0;
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        };
        if(route_placement_source)
            source_bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                {key(4096),routine({snap(45)})}});
        source=std::make_unique<odh::Vm>(globals,odh::ShareVmProgramTag{},world.vm_memory(),
            odh::VmLimits{},source_primitive,odh::SharedRoutineOwners{{source_owner,source_bank}},
            odh::ShareVmOwnerProgramsTag{});
        source_vm_ptr=source.get();
        require(source->start(key(4096),{1,1,1,source_owner})==odh::VmStatus::running,
            "start live Snap caller Main");
        source->temps()[4]=17;source->temp_xl()[1]=29;
    }
};
void deep_entry_checks(){
    odh::RoutineSet deep;
    for(unsigned i=0;i<8;++i)deep.emplace(key(4100+i),routine({{
        static_cast<std::uint16_t>(i==7?99:4101+i),254,255,{}}}));
    const auto bank=std::make_shared<const odh::VmProgram>(std::move(deep));
    for(const auto budget:{std::uint64_t{64},std::uint64_t{4}}){
        Harness h;h.tested_bank=bank;h.check_budget=budget;
        std::string leaf_diagnostic="not reached";
        // Numeric Gosub inherits entity references but constructs its own literal
        // argument array. The root-only exact EP5 argument contract is tested by
        // the unchanged default Harness handler in the other cases below.
        const odh::VmPrimitive leaf=[&](const odh::VmFrame& frame,const odh::Instruction& instruction,
                const odh::Ts1Temps&,const odh::VmDispatch& dispatch){
            std::ostringstream detail;
            detail<<"key="<<frame.key.id<<" exact="<<frame.exact_arguments
                <<" args="<<frame.arguments.size()<<" locals="<<frame.locals.size()
                <<" check="<<dispatch.check_mode<<" roles="<<frame.context.caller<<','
                <<frame.context.callee<<','<<frame.context.stack_object;
            for(const auto arg:frame.arguments)detail<<" arg="<<arg;
            leaf_diagnostic=detail.str();
            h.context_correct=instruction.opcode==99&&frame.key.scope==odh::RoutineScope::private_object&&frame.key.id==4107&&
                frame.context.caller==2&&frame.context.callee==2&&frame.context.stack_object==1&&
                frame.context.code_owner==h.tested_owner&&frame.context.entity_references_bound&&
                frame.context.caller_generation==h.world.find(2)->generation&&
                frame.context.callee_generation==h.world.find(2)->generation&&
                frame.context.stack_object_generation==h.world.find(1)->generation&&
                !frame.exact_arguments&&frame.arguments==std::vector<std::int16_t>(4,0)&&
                frame.locals==std::vector<std::int16_t>(4,0)&&dispatch.check_mode;
            (void)h.world.write(frame,3,25,73);
            auto* check_vm=const_cast<odh::Vm*>(dispatch.origin);
            check_vm->temps()[4]=88;check_vm->temp_xl()[1]=1234;
            ++h.random_calls;
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        };
        h.start(nullptr,true,false,4100,false,leaf);
        const auto source_status=h.source->run(64);
        const auto diagnostic=[&](const char* gate){
            std::ostringstream detail;
            detail<<gate<<" budget="<<budget<<" parent_status="<<static_cast<int>(source_status)
                <<" parent_steps="<<h.source->steps()<<" parent_depth="<<h.source->stack().size()
                <<" parent_preserved="<<h.source_state_unchanged_inside<<" primitive_calls="<<h.random_calls;
            if(h.result)detail<<" check_state="<<static_cast<int>(h.result->state)
                <<" check_steps="<<h.result->steps<<" reason="<<h.result->diagnostic.substr(0,512);
            detail<<" leaf="<<leaf_diagnostic;
            return detail.str();
        };
        require(source_status==odh::VmStatus::returned_true&&h.result.has_value(),
            diagnostic("deep EP5 uses real source placement dispatch").c_str());
        require(h.source_state_unchanged_inside,
            diagnostic("deep EP5 preserves the parked source Main").c_str());
        if(budget==64){
            require(h.result->state==odh::LiveEntryPoint5State::evaluated_true&&h.result->steps==8,
                diagnostic("eight-level EP5 returns true after eight instructions").c_str());
            require(h.context_correct&&h.random_calls==1,
                diagnostic("deep ordinary Gosub leaf has exact inherited references and literal child arguments").c_str());
        }else require(h.result->state==odh::LiveEntryPoint5State::unsupported&&h.result->steps==4&&h.random_calls==0,
            diagnostic("explicit low instruction budget stops the deep check before leaf effects").c_str());
    }
}
void typed_route_execution_ep5(){
    using Phase=odh::RouteExecutionPhase;
    using State=odh::LiveEntryPoint5State;
    for(const auto opcode:{std::uint16_t{45},std::uint16_t{27}}){
        odh::World world;odh::RouteGrid grid(16,16);
        require(world.create(1,odh::EntityKind::avatar,{1,88,0})&&world.create(2,odh::EntityKind::object,{1,0,0})&&
            world.set_position(1,{4,4,1,0})&&world.set_position(2,{8,4,1,0})&&world.set_radian_direction(1,0.37f),
            "create live own-thread route EP5 actor and counterpart");
        odh::SlotRecord slot{};slot.type=3;slot.snap_target_slot=-1;slot.facing=-2;
        const odh::SlotTable table{0,10,{slot},0};odh::Routing route(world,grid,table);
        const odh::Instruction instruction=opcode==45?odh::Instruction{45,254,255,{0,0,2,0,1,0,0,0}}:
            odh::Instruction{27,254,255,{0,0,254,255,0,0,2,0}};
        const auto globals=std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
        const auto bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
            {key(4096),routine({instruction})},{key(4100),routine({{99,254,255,{}}})}});
        constexpr odh::CodeOwner owner=700;
        odh::Vm vm(globals,odh::ShareVmProgramTag{},world.vm_memory(),{},route.primitive(),
            odh::SharedRoutineOwners{{owner,bank}},odh::ShareVmOwnerProgramsTag{});
        require(route.bind(vm),"bind actual route VM before typed EP5 scopes");
        route.set_route_goal_validator([](const auto&,auto){return true;});
        route.set_room_pair_provider([](const auto&,const auto&){return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,1}};});
        const auto actor_generation=world.find(1)->generation,other_generation=world.find(2)->generation;
        unsigned checks_run=0,attempts=0,moves=0;
        std::optional<odh::RouteExecutionContext> retained;
        const auto make_request=[&](const odh::RouteExecutionContext& context){
            odh::LiveEntryPoint5Request request;request.world=&world;request.tested_entity=1;request.other_entity=2;
            request.tested_generation=actor_generation;request.other_generation=other_generation;
            request.action=4100;request.ghost=context.phase==Phase::attempt_walk?1:0;
            request.source_vm=&vm;request.source_frame=&context.frame;request.source_instruction=&context.instruction;
            request.source_dispatch=context.dispatch;request.route_execution_proof=context.proof;
            request.route_execution_phase=context.phase;request.code_owner=owner;
            request.global_program=globals;request.owner_program=bank;request.live_memory=world.vm_memory();request.register_owner=&vm;
            request.identities_still_match=[&]{return world.find(1)&&world.find(2)&&
                world.find(1)->generation==actor_generation&&world.find(2)->generation==other_generation;};
            request.check_primitive=[&,phase=context.phase](const odh::VmFrame& frame,const auto&,const auto&,const odh::VmDispatch& dispatch){
                ++checks_run;
                require(dispatch.check_mode&&frame.context.caller==1&&frame.context.callee==1&&frame.context.stack_object==2&&
                    frame.context.code_owner==owner&&frame.arguments==std::vector<std::int16_t>{2,
                        static_cast<std::int16_t>(phase==Phase::attempt_walk?1:0),0,0}&&
                    world.find(1)->radian_direction==(phase==Phase::attempt_walk?0.37f:0.0f),
                    "typed route EP5 keeps own tested context, exact ghost args and phase-specific live heading");
                auto* check=const_cast<odh::Vm*>(dispatch.origin);check->temps()[4]=88;check->temp_xl()[1]=1234;
                require(world.write(frame,0,0,73),"publish live EP5 World effect");
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            };
            return request;
        };
        const auto exercise=[&](const odh::RouteExecutionContext& context){
            retained=context;
            const auto before_steps=vm.steps(),before_ticket=context.dispatch.ticket,before_run=vm.run_generation();
            const auto before_status=vm.status();const auto before_pc=vm.stack().back().pc;
            if(context.phase==Phase::set_position)require(world.set_radian_direction(1,0),"expose source movement temporary NORTH");
            auto request=make_request(context);
            auto wrong_phase=request;wrong_phase.route_execution_phase=context.phase==Phase::attempt_walk?Phase::set_position:Phase::attempt_walk;
            auto wrong_ticket=request;++wrong_ticket.source_dispatch.ticket;
            auto absent=request;absent.route_execution_proof=odh::RouteExecutionProof{};
            auto conflicting=request;conflicting.route_placement_proof=odh::RoutePlacementProof{};
            const auto before_checks=checks_run;
            require(odh::evaluate_live_entry_point5(wrong_phase).state==State::unsupported&&
                odh::evaluate_live_entry_point5(wrong_ticket).state==State::unsupported&&
                odh::evaluate_live_entry_point5(absent).state==State::unsupported&&
                odh::evaluate_live_entry_point5(conflicting).state==State::unsupported&&checks_run==before_checks,
                "wrong phase/ticket, unminted execution proof and mixed lease types cannot run live EP5");
            if(context.phase==Phase::attempt_walk){auto wrong_ghost=request;wrong_ghost.ghost=0;
                require(odh::evaluate_live_entry_point5(wrong_ghost).state==State::unsupported&&checks_run==before_checks,
                    "AttemptWalk lease cannot substitute ghost0 for source ghost1");}
            require(odh::evaluate_live_entry_point5(request).state==State::evaluated_true&&checks_run==before_checks+1&&
                vm.temps()[4]==88&&vm.temp_xl()[1]==1234&&world.find(1)->attributes[0]==73&&
                vm.steps()==before_steps&&vm.run_generation()==before_run&&vm.status()==before_status&&
                vm.stack().back().pc==before_pc&&context.dispatch.ticket==before_ticket,
                "actual active/pending route EP5 shares own Thread registers and World without advancing Main or spending its ticket");
            if(context.phase==Phase::set_position){
                require(!context.proof.matches_after_restore(context.frame,vm,context.instruction,context.dispatch,context.phase)&&
                    world.set_radian_direction(1,0.37f)&&
                    context.proof.matches_after_restore(context.frame,vm,context.instruction,context.dispatch,context.phase),
                    "movement proof requires exact original heading after the NORTH phase");
            }
        };
        route.set_route_execution_callback(Phase::attempt_walk,[&](const auto& context){++attempts;exercise(context);});
        route.set_route_execution_callback(Phase::set_position,[&](const auto& context){++moves;exercise(context);});
        require(vm.start(key(4096),{1,1,2,owner})==odh::VmStatus::running&&vm.run(1)==odh::VmStatus::waiting&&
            attempts==1&&moves==0,"actual source45/27 mints ghost1 AttemptWalk authority during its instruction");
        require(route.tick(1)&&moves==1&&retained,"actual pending route job mints movement authority without a fake primitive");
        const auto before=checks_run;
        require(odh::evaluate_live_entry_point5(make_request(*retained)).state==State::unsupported&&checks_run==before,
            "expired movement lease cannot execute EP5 after its Routing callback returns");
    }
}
void ranged_relative_placement_ep5(){
    using State=odh::LiveEntryPoint5State;
    odh::World world;odh::RouteGrid grid(128,128);
    require(world.create(1,odh::EntityKind::avatar,{1,88,0})&&world.create(2,odh::EntityKind::object,{1,0,0})&&
        world.set_position(1,{32,32,1,0})&&world.set_position(2,{64,32,1,0})&&
        world.declare_object_word(1,25,0)&&world.declare_object_word(2,25,0),"create real raw27 EP5 participants");
    const odh::Instruction raw{27,254,255,{5,0,0,254,0,0,2,0}};
    auto globals=std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
    auto bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
        {key(4096),routine({raw})},{key(4100),routine({{99,254,255,{}}})}});
    constexpr odh::CodeOwner owner=700;
    odh::Routing route(world,grid,odh::SlotTable{});
    odh::Vm source(globals,odh::ShareVmProgramTag{},world.vm_memory(),{},route.primitive(),
        odh::SharedRoutineOwners{{owner,bank}},odh::ShareVmOwnerProgramsTag{});
    odh::Vm other_thread(globals,odh::ShareVmProgramTag{},world.vm_memory(),{}, {},
        odh::SharedRoutineOwners{{owner,bank}},odh::ShareVmOwnerProgramsTag{});
    require(other_thread.start(key(4096),{2,2,1,owner})==odh::VmStatus::running&&route.bind(source),
        "retain each tested entity's own Thread registers");
    route.set_route_goal_validator([&](const auto&,odh::RoutePoint point){return grid.contains(point)&&grid.walkable(point);});
    route.set_room_pair_provider([](const auto&,const auto&){return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,1}};});
    const auto generation1=world.find(1)->generation,generation2=world.find(2)->generation;
    unsigned checks_run=0,providers=0;std::optional<odh::RoutePlacementContext> retained;
    const auto request_for=[&](const odh::RoutePlacementContext& context,std::int16_t tested){
        const auto other=static_cast<std::int16_t>(tested==1?2:1);
        odh::LiveEntryPoint5Request request;request.world=&world;request.tested_entity=tested;request.other_entity=other;
        request.tested_generation=tested==1?generation1:generation2;request.other_generation=tested==1?generation2:generation1;
        request.action=4100;request.source_vm=&source;request.source_frame=&context.frame;
        request.source_instruction=&context.instruction;request.source_dispatch=context.dispatch;
        request.route_placement_proof=context.proof;request.code_owner=owner;
        request.global_program=globals;request.owner_program=bank;request.live_memory=world.vm_memory();
        request.register_owner=tested==1?&source:&other_thread;
        request.identities_still_match=[&]{return world.find(1)&&world.find(2)&&
            world.find(1)->generation==generation1&&world.find(2)->generation==generation2;};
        request.check_primitive=[&,tested,other](const odh::VmFrame& frame,const auto&,const auto&,const odh::VmDispatch& dispatch){
            ++checks_run;
            require(dispatch.check_mode&&frame.context.caller==tested&&frame.context.callee==tested&&
                frame.context.stack_object==other&&frame.exact_arguments&&frame.arguments==std::vector<std::int16_t>{other,0,0,0},
                "raw27 placement keeps exact bilateral EP5 caller/arguments");
            auto* check=const_cast<odh::Vm*>(dispatch.origin);check->temps()[4]=70+tested;check->temp_xl()[1]=1200+tested;
            require(world.write(frame,3,25,80+tested),"raw27 EP5 publishes tested entity's live World effect");
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_false,{}};
        };
        return request;
    };
    route.set_authenticated_range_slot_goal_provider([&](const auto&,std::size_t index,const auto&,const auto&,float,
        const odh::RoutePlacementContext& context){
        ++providers;retained=context;
        require(index==0&&context.instruction.opcode==27&&context.instruction.operands==raw.operands,
            "EP5 placement source is actual raw27, not a fabricated45");
        const auto before_steps=source.steps(),before_run=source.run_generation();
        for(const auto tested:{std::int16_t{1},std::int16_t{2}}){
            auto request=request_for(context,tested);auto absent=request;absent.route_placement_proof.emplace();
            auto wrong=request;++wrong.source_dispatch.ticket;
            auto fake45=context;fake45.instruction.opcode=45;
            require(odh::evaluate_live_entry_point5(absent).state==State::unsupported&&
                odh::evaluate_live_entry_point5(wrong).state==State::unsupported&&
                odh::evaluate_live_entry_point5(request_for(fake45,tested)).state==State::unsupported,
                "unminted, altered-ticket and forged-opcode placement requests cannot execute EP5");
            require(odh::evaluate_live_entry_point5(request).state==State::evaluated_false,
                "raw27 typed placement executes actual EP5 rather than treating unknown as clearance");
        }
        require(checks_run==2&&source.temps()[4]==71&&other_thread.temps()[4]==72&&
            source.temp_xl()[1]==1201&&other_thread.temp_xl()[1]==1202&&
            world.find(1)->object_words.at(25)==81&&world.find(2)->object_words.at(25)==82&&
            source.steps()==before_steps&&source.run_generation()==before_run,
            "bilateral raw27 EP5 keeps each owner's registers and live effects without advancing Main");
        require(world.set_position(2,{65,32,1,0}),"change target during authenticated placement");
        require(odh::evaluate_live_entry_point5(request_for(context,1)).state==State::unsupported&&checks_run==2,
            "changed participant invalidates the original lease before more EP5 effects");
        return odh::AuthenticatedRangeSlotGoals{};
    });
    require(source.start(key(4096),{1,1,2,owner})==odh::VmStatus::running,"start raw27 placement source");
    (void)source.run(1);
    if(providers!=1||!retained)throw std::runtime_error("real raw27 route invokes typed placement: vm="+
        std::string(odh::vm_status_name(source.status()))+" diagnostic="+source.diagnostic()+" route="+route.diagnostic());
    require(odh::evaluate_live_entry_point5(request_for(*retained,1)).state==State::unsupported&&checks_run==2,
        "retained raw27 lease cannot execute EP5 after its callback returns");
}
void creation_placement_ep5(odh::EntityKind created_kind) {
    using State=odh::LiveEntryPoint5State;odh::World world;
    require(world.create(1,odh::EntityKind::avatar)&&world.create(2,created_kind)&&
        world.create(3,odh::EntityKind::object)&&world.create(4,odh::EntityKind::object)&&
        world.declare_object_word(3,25,0),"create ordinary construction placement participants");
    const auto waiting=[](const auto&,const auto&,const auto&,const auto&){
        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};};
    odh::Vm source({{key(4096),routine({{0,254,255,{}}})}},world.vm_memory(),{},waiting);
    odh::Vm registers({{key(4096),routine({{0,254,255,{}}})}},world.vm_memory(),{},waiting);
    source.start(key(4096),{1,1,1});registers.start(key(4096),{2,2,2});
    require(source.run(1)==odh::VmStatus::waiting&&registers.run(1)==odh::VmStatus::waiting,
        "construction capability retains real suspended source and tested Thread");
    auto wide=routine({{99,254,255,{}}});wide.metadata->arguments=12;
    auto globals=std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
    auto bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{key(4100),wide}});
    bool live_scope=true,expire_during_check=false;unsigned effects=0;
    odh::LiveEntryPoint5Request request;request.world=&world;request.tested_entity=2;request.other_entity=3;
    request.tested_generation=world.find(2)->generation;request.other_generation=world.find(3)->generation;
    request.action=4100;request.code_owner=701;request.global_program=globals;request.owner_program=bank;
    request.live_memory=world.vm_memory();request.register_owner=&registers;request.identities_still_match=[] {return true;};
    request.object_creation_source=odh::LiveObjectCreationPlacementSource{&source,source.run_generation(),2,
        world.find(2)->generation,[&]{return live_scope;}};
    request.check_primitive=[&](const auto& frame,const auto&,const auto&,const auto& dispatch){
        require(frame.exact_arguments&&frame.arguments==std::vector<std::int16_t>({3,0,0,0})&&frame.locals.size()==12&&
            frame.context.caller==2&&frame.context.callee==2&&frame.context.stack_object==3&&dispatch.check_mode,
            "ordinary creation EP5 supplies exact four source args despite twelve declared parameters");
        ++effects;require(world.write(frame,4,25,static_cast<std::int16_t>(70+effects)),"creation EP5 retains real StackObject World effects");
        const_cast<odh::Vm*>(dispatch.origin)->temps()[4]=static_cast<std::int16_t>(effects);
        if(expire_during_check)live_scope=false;
        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
    };
    const auto before=source.snapshot();
    require(odh::evaluate_live_entry_point5(request).state==State::evaluated_true&&effects==1&&registers.temps()[4]==1&&
        world.find(3)->object_words.at(25)==71&&source.pending()->ticket==before.pending->ticket&&source.steps()==before.steps,
        "real construction capability executes EP5 without an invented primitive dispatch and preserves source wait");
    const auto rejects_capability=[&](const auto& mutation,const char* message){auto invalid=request;mutation(invalid);
        const auto prior=effects;require(odh::evaluate_live_entry_point5(invalid).state==State::unsupported&&effects==prior,message);};
    rejects_capability([](auto& r){r.object_creation_source->source_still_matches={};},"creation EP5 requires an explicit live capability callback");
    rejects_capability([](auto& r){r.object_creation_source->source_run_generation++;},"creation EP5 rejects a stale source VM run");
    rejects_capability([](auto& r){r.object_creation_source->created_generation++;},"creation EP5 rejects stale newly-created generation");
    rejects_capability([&](auto& r){r.object_creation_source->created_entity=4;r.object_creation_source->created_generation=world.find(4)->generation;},
        "creation capability must identify an actual participant of this bilateral EP5");
    rejects_capability([&](auto& r){r.source_vm=&source;},"creation authority cannot be mixed with legacy dispatch VM evidence");
    rejects_capability([](auto& r){r.route_execution_proof.emplace();},"creation authority cannot be mixed with a route execution lease");
    rejects_capability([](auto& r){r.route_placement_proof.emplace();},"creation authority cannot be mixed with a route placement lease");
    rejects_capability([](auto& r){r.ghost=1;},"ordinary creation placement EP5 requires real ghost0 source semantics");
    live_scope=false;const auto prior=effects;
    require(odh::evaluate_live_entry_point5(request).state==State::unsupported&&effects==prior,
        "expired construction scope cannot execute EP5 after the factory returns");
    live_scope=true;expire_during_check=true;
    require(odh::evaluate_live_entry_point5(request).state==State::unsupported&&effects==prior+1&&registers.temps()[4]==effects&&
        world.find(3)->object_words.at(25)==70+effects,"expired-during-check construction retains already-published source World/register effects");
}

}

void original_recovery_placement_ep5(){
    using namespace odh;
    World world;std::array<std::int16_t,68> words{};words[3]=-1;
    for(const auto id:{1,2})require(world.create_saved_entity(id,id==1?EntityKind::avatar:EntityKind::object,1000+id,
        {},id==1?std::vector<std::int16_t>(88):std::vector<std::int16_t>{},{},words,{},0),"original recovery EP5 bodies");
    const WorldEntityReference person{1,world.find(1)->generation},object{2,world.find(2)->generation};
    auto traversal=world.bind_original_load_order({object,person},4);require(traversal.has_value(),"genuine original World traversal");
    auto globals=std::make_shared<const VmProgram>(RoutineSet{});
    auto program=std::make_shared<const VmProgram>(RoutineSet{{key(4096),routine({{98,254,255,{}}})},{key(4100),routine({{99,254,255,{}}})}});
    std::function<void()> during_dispatch;
    auto thread=std::make_unique<Vm>(globals,ShareVmProgramTag{},world.vm_memory(),VmLimits{},
        [&](const auto&,const auto&,const auto&,const auto&){if(during_dispatch)during_dispatch();return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};},
        SharedRoutineOwners{{700,program}},ShareVmOwnerProgramsTag{});
    Vm object_thread(globals,ShareVmProgramTag{},world.vm_memory(),VmLimits{},VmPrimitive{},SharedRoutineOwners{{701,program}},ShareVmOwnerProgramsTag{});
    require(thread->start(key(4096),{1,1,0,700})==VmStatus::running,"park actual person Main before dispatch");
    thread->temps()[0]=11;object_thread.temps()[0]=17;bool scope=true;unsigned effects=0;
    LiveOriginalRecoveryPlacementSource source{&world,&*traversal,person,thread.get(),thread->run_generation(),[&]{return scope;}};
    source.registration=world.begin_original_person_notifications(*traversal);
    LiveEntryPoint5Request request;request.world=&world;request.tested_entity=2;request.tested_generation=object.generation;
    request.other_entity=1;request.other_generation=person.generation;request.action=4100;request.code_owner=701;
    request.global_program=globals;request.owner_program=program;request.live_memory=world.vm_memory();request.register_owner=&object_thread;
    request.identities_still_match=[&]{return world.is_live_reference(person)&&world.is_live_reference(object);};
    request.original_recovery_source=source;
    request.check_primitive=[&](const auto& frame,const auto& instruction,const auto&,const auto&){
        require(instruction.opcode==99&&frame.context.caller==request.tested_entity&&frame.context.callee==request.tested_entity&&
            frame.context.stack_object==request.other_entity&&frame.arguments==std::vector<std::int16_t>({request.other_entity,0,0,0}),
            "recovery EP5 uses tested entity and ordinary four explicit placement arguments");
        require(world.write(frame,3,25,73),"live recovery EP5 World effect");++effects;
        PrimitiveOutcome outcome{PrimitiveDisposition::goto_true,{}};outcome.variable_write=VmVariableWrite{8,0,91};return outcome;
    };
    require(evaluate_live_entry_point5(request).state==LiveEntryPoint5State::evaluated_true&&effects==1&&
        object_thread.temps()[0]==91&&thread->temps()[0]==11&&world.find(2)->object_words.at(25)==73,
        "recovery uses real World and tested object TEMP without borrowing person's bank");
    auto invalid=request;invalid.ghost=1;
    require(evaluate_live_entry_point5(invalid).state==LiveEntryPoint5State::unsupported&&effects==1,"recovery requires ghost0");
    invalid=request;invalid.register_owner=thread.get();
    require(evaluate_live_entry_point5(invalid).state==LiveEntryPoint5State::unsupported&&thread->temps()[0]==11&&effects==1,
        "recovery cannot use person's register bank for the other tested object");
    for(unsigned kind=0;kind<6;++kind){
        invalid=request;
        if(kind==0)invalid.object_creation_source=LiveObjectCreationPlacementSource{};
        if(kind==1)invalid.retired_container_source=LiveRetiredContainerPlacementSource{};
        if(kind==2)invalid.route_placement_proof=RoutePlacementProof{};
        if(kind==3)invalid.route_execution_proof=RouteExecutionProof{};
        if(kind==4)invalid.source_vm=thread.get();
        if(kind==5)invalid.source_dispatch.origin=thread.get();
        require(evaluate_live_entry_point5(invalid).state==LiveEntryPoint5State::unsupported&&effects==1,
            "original recovery authority never mixes with another placement source");
    }
    World foreign;invalid=request;invalid.original_recovery_source->world=&foreign;
    require(evaluate_live_entry_point5(invalid).state==LiveEntryPoint5State::unsupported,"wrong recovery World rejects");
    invalid=request;++invalid.original_recovery_source->run_generation;
    require(evaluate_live_entry_point5(invalid).state==LiveEntryPoint5State::unsupported,"wrong recovery run rejects");
    invalid=request;invalid.original_recovery_source->person=object;
    require(evaluate_live_entry_point5(invalid).state==LiveEntryPoint5State::unsupported,"recovery requires actual person class");
    invalid=request;++invalid.original_recovery_source->person.generation;
    require(evaluate_live_entry_point5(invalid).state==LiveEntryPoint5State::unsupported,"stale recovery person rejects");
    invalid=request;invalid.other_entity=3;invalid.other_generation=1;
    require(evaluate_live_entry_point5(invalid).state==LiveEntryPoint5State::unsupported,"recovery person must participate in tested pair");
    invalid=request;invalid.original_recovery_source->registration.reset();
    require(evaluate_live_entry_point5(invalid).state==LiveEntryPoint5State::unsupported,"recovery requires captured original PERSON storage authority");
    during_dispatch=[&]{require(evaluate_live_entry_point5(request).state==LiveEntryPoint5State::unsupported,
        "recovery rejects its person Thread during actual dispatch");};
    require(thread->run(1)==VmStatus::returned_true&&effects==1,"active source Thread cannot fabricate idle recovery placement");
    request.tested_entity=1;request.tested_generation=person.generation;request.other_entity=2;request.other_generation=object.generation;
    request.code_owner=700;request.register_owner=thread.get();
    invalid=request;invalid.register_owner=nullptr;invalid.no_thread_proven=true;
    require(evaluate_live_entry_point5(invalid).state==LiveEntryPoint5State::unsupported,
        "recovery's demonstrated person Thread cannot be contradicted by no-thread fallback");
    require(evaluate_live_entry_point5(request).state==LiveEntryPoint5State::evaluated_true&&thread->temps()[0]==91&&effects==2,
        "recovery also admits the person as tested entity with its own TEMP owner");
    request.tested_entity=2;request.tested_generation=object.generation;request.other_entity=1;request.other_generation=person.generation;
    request.code_owner=701;request.register_owner=&object_thread;
    const auto primitive=request.check_primitive;
    request.check_primitive=[&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
        auto outcome=primitive(frame,instruction,temps,dispatch);scope=false;return outcome;
    };
    require(evaluate_live_entry_point5(request).state==LiveEntryPoint5State::unsupported&&effects==3&&object_thread.temps()[0]==91,
        "expired-during-check recovery preserves already completed World and TEMP effects");
    scope=true;const auto* old_address=&*traversal;
    traversal.reset();traversal=world.bind_original_load_order({person,object},4);
    require(traversal&&old_address==&*traversal&&evaluate_live_entry_point5(request).state==LiveEntryPoint5State::unsupported&&effects==3,
        "old copied registration rejects replacement token at identical address with same person/run and true scope callback");
    scope=false;
    traversal.reset();thread.reset();request.action=0;
    require(evaluate_live_entry_point5(request).state==LiveEntryPoint5State::unsupported&&effects==3,
        "expired scope is checked before touching destroyed traversal/Thread even for no-action EP5");
}

void host_avatar_constructor_ep5(){
    using namespace odh;
    for(const std::int16_t age:{std::int16_t{9},std::int16_t{27}}){
        World world;require(world.create(1,EntityKind::avatar,{0,101,3,16})&&world.define_type(1,1001)&&
            world.create(2,EntityKind::object,{1,0,1})&&world.define_type(2,1002)&&world.set_person_data(1,58,age),
            "fresh actual constructor identities");
        require(world.declare_object_word(2,25,0),"actual obstacle effect scalar backing");
        std::vector<EntryPoint> entries(6);entries[1].action=4096;entries[5].action=4100;
        require(world.define_entry_points(1,entries)&&world.define_entry_points(2,entries),"actual constructor/Main and obstacle entry tables");
        unsigned main_runs=0,effects=0;
        const RoutineSet main{{routine_key(4096),{0x8002,{{201,254,255,{}}},{},Bhav::Metadata{0,4,4,0}}}};
        Vm thread({},world.vm_memory(),{},[&](const auto&,const auto&,const auto&,const auto&){
            ++main_runs;return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};},{{44,main}});
        require(thread.start(routine_key(4096),{1,1,0,44})==VmStatus::running,"source Main prepared without dispatch");
        Vm registers({},world.vm_memory());bool live=true,expire_during_check=false;
        LiveEntryPoint5Request request;request.world=&world;request.tested_entity=2;request.other_entity=1;
        request.tested_generation=world.find(2)->generation;request.other_generation=world.find(1)->generation;
        request.action=4100;request.code_owner=55;request.register_owner=&registers;
        request.global_program=std::make_shared<const VmProgram>(RoutineSet{});
        request.owner_program=std::make_shared<const VmProgram>(RoutineSet{{routine_key(4100),
            {0x8002,{{202,254,255,{}}},{},Bhav::Metadata{0,4,4,0}}}});
        request.avatar_constructor_source=LiveAvatarConstructorPlacementSource{&world,{1,world.find(1)->generation},
            &thread,thread.run_generation(),44,[&]{return live;}};
        request.identities_still_match=[&]{return live;};
        request.check_primitive=[&](const auto& frame,const auto&,const auto&,const auto& dispatch){
            require(dispatch.check_mode&&frame.context.caller==2&&frame.context.callee==2&&frame.context.stack_object==1,
                "real obstacle EP5 context uses tested object's owner");
            ++effects;require(world.write(frame,3,25,71),"actual EP5 World effect");
            if(expire_during_check)live=false;
            return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
        };
        require(evaluate_live_entry_point5(request).state==LiveEntryPoint5State::evaluated_true&&effects==1&&main_runs==0&&thread.stack().front().pc==0,
            "host constructor runs actual EP5 without running Main");
        for(unsigned invalid=0;invalid<9;++invalid){
            auto bad=request;
            switch(invalid){
            case 0:bad.avatar_constructor_source->source_still_matches={};break;
            case 1:++bad.avatar_constructor_source->avatar.generation;break;
            case 2:++bad.avatar_constructor_source->run_generation;break;
            case 3:++bad.avatar_constructor_source->code_owner;break;
            case 4:bad.object_creation_source=LiveObjectCreationPlacementSource{};break;
            case 5:bad.original_recovery_source=LiveOriginalRecoveryPlacementSource{};break;
            case 6:bad.source_vm=&thread;break;
            case 7:bad.tested_entity=1;bad.tested_generation=world.find(1)->generation;
                bad.other_entity=2;bad.other_generation=world.find(2)->generation;break;
            case 8:bad.avatar_constructor_source->world=nullptr;break;
            }
            require(evaluate_live_entry_point5(bad).state==LiveEntryPoint5State::unsupported&&effects==1&&main_runs==0,
                "constructor matrix rejects missing/stale/mixed ownership before effects");
        }
        expire_during_check=true;
        require(evaluate_live_entry_point5(request).state==LiveEntryPoint5State::unsupported&&effects==2&&main_runs==0,
            "constructor expiry during EP5 retains actual effects and refuses completion");
        live=false;auto expired=request;expired.action=0;
        require(evaluate_live_entry_point5(expired).state==LiveEntryPoint5State::unsupported&&effects==2,
            "expired constructor is not accepted through absent native EP5");
    }
}

int main() {
    try {
        host_avatar_constructor_ep5();
        original_recovery_placement_ep5();
    deep_entry_checks();
        typed_route_execution_ep5();
        ranged_relative_placement_ep5();
        creation_placement_ep5(odh::EntityKind::object);creation_placement_ep5(odh::EntityKind::avatar);
        {
            Harness h;h.start(nullptr,false,false,4100,false,{},1,2);
            const auto status=h.source->run(1);
            const odh::VmFrame read{key(4096),0,{1,1,2,h.source_owner},{},{}};
            require(status==odh::VmStatus::returned_true&&h.result&&
                h.result->state==odh::LiveEntryPoint5State::evaluated_true&&h.result->steps==1,
                "EP5 exact resource-backed check returns true");
            require(h.context_correct,"EP5 disposable frame has exact tested/counterpart context and args");
            require(h.source_state_unchanged_inside&&h.source->steps()==1&&
                h.source->snapshot().ticket_sequence==1,
                "EP5 does not advance caller Main work or spend its dispatch ticket");
            require(h.source->temps()[4]==88&&h.source->temp_xl()[1]==1234&&h.random_calls==1&&
                h.world.read(read,3,25)==73,
                "live World, RNG callback, TEMP and TempXL effects survive");
        }
        {
            Harness h;
            auto tested=std::make_unique<odh::Vm>(h.globals,odh::ShareVmProgramTag{},h.world.vm_memory(),
                odh::VmLimits{},odh::VmPrimitive{},odh::SharedRoutineOwners{{h.tested_owner,h.tested_bank}},
                odh::ShareVmOwnerProgramsTag{});
            require(tested->start(key(4101),{2,2,1,h.tested_owner})==odh::VmStatus::running,
                "start separate tested-entity Main thread");
            tested->temps()[4]=31;tested->temp_xl()[1]=45;
            const auto ticket=tested->snapshot().ticket_sequence,steps=tested->steps();
            h.start(tested.get(),false);
            const auto status=h.source->run(1);
            require(status==odh::VmStatus::returned_true&&h.result&&
                h.result->state==odh::LiveEntryPoint5State::evaluated_true&&
                tested->status()==odh::VmStatus::running&&tested->stack().size()==1&&
                tested->stack().back().pc==0&&tested->snapshot().ticket_sequence==ticket&&
                tested->steps()==steps&&tested->temps()[4]==88&&tested->temp_xl()[1]==1234&&
                h.context_correct,
                "bilateral side uses the tested entity's own register owner without advancing its Main thread");
        }
        {
            Harness h;
            auto zero_seed=odh::VmPrimitive{[&](const odh::VmFrame&,const odh::Instruction&,
                    const odh::Ts1Temps& temps,const odh::VmDispatch& dispatch){
                const bool zero=temps[4]==0&&dispatch.origin->temp_xl()[1]==0;
                if(!zero)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"nonzero initial registers"};
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_false,{}};
            }};
            // Reinstall with a fresh source dispatch so the no-thread check
            // proves source's zero-initialized NativeRegisterState behavior.
            h.start(nullptr,true,false,4100,false,std::move(zero_seed));
            const auto status=h.source->run(1);
            require(status==odh::VmStatus::returned_true&&h.result&&
                h.result->state==odh::LiveEntryPoint5State::evaluated_false&&
                h.result->steps==1,
                "proven absent entity Thread starts EP5 with zero registers and returns source false");
        }
        {
            Harness h;h.start(nullptr,true,false,4100);
            h.tested_bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                {key(4101),routine({{99,254,255,{}}})}});
            const auto status=h.source->run(1);
            require(status==odh::VmStatus::returned_true&&h.result&&
                h.result->state==odh::LiveEntryPoint5State::evaluated_false&&h.random_calls==0,
                "known exact owner bank with an absent EP5 BHAV is source false");
        }
        {
            Harness h;h.start();
            h.tested_bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                {key(4100),routine({})}});
            require(h.source->run(1)==odh::VmStatus::returned_true&&h.result&&
                h.result->state==odh::LiveEntryPoint5State::evaluated_false&&
                h.result->steps==0&&h.random_calls==0,
                "known empty EP5 returns false without effects or interpreter work");
        }
        {
            Harness h;h.start();
            auto wide=routine({{99,254,255,{}}});wide.metadata->arguments=5;
            h.tested_bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                {key(4100),std::move(wide)}});
            require(h.source->run(1)==odh::VmStatus::returned_true&&h.result&&
                h.result->state==odh::LiveEntryPoint5State::evaluated_true&&
                h.result->steps==1&&h.random_calls==1&&h.context_correct,
                "EP5 metadata above four retains source exact-four arguments and real effects");
        }
        {
            Harness h;h.start(nullptr,true,false,4100,true);
            const auto status=h.source->run(1);
            require(status==odh::VmStatus::returned_true&&h.result&&
                h.result->state==odh::LiveEntryPoint5State::unsupported&&h.random_calls==0,
                "forged source dispatch is rejected before any EP5 side effect");
        }
        {
            Harness h;h.start(nullptr,true,false,4100,false,{},2,1,true);
            const auto status=h.source->run(1);
            require(status==odh::VmStatus::returned_true&&h.result&&
                h.result->state==odh::LiveEntryPoint5State::unsupported&&h.random_calls==0,
                "default-invalid route placement proof cannot authorize EP5 during opcode45");
        }
        {
            Harness h;h.start(nullptr,true,false,4100);
            const auto status=h.source->run(1);
            require(status==odh::VmStatus::returned_true&&h.result&&
                h.result->state==odh::LiveEntryPoint5State::evaluated_true,
                "baseline exact resource-backed EP5 evaluates before missing-bank checks");
        }
        {
            Harness h;h.start(nullptr,true,false,4100);
            // Remove the resolved owner bank after source setup. Missing exact
            // resource evidence must not be mistaken for an absent BHAV.
            h.tested_bank.reset();
            const auto status=h.source->run(1);
            require(status==odh::VmStatus::returned_true&&h.result&&
                h.result->state==odh::LiveEntryPoint5State::unsupported&&h.random_calls==0,
                "unresolved exact owner bank remains unsupported rather than known false");
        }
        {
            Harness h;h.start(nullptr,true,false,255);
            const auto status=h.source->run(1);
            require(status==odh::VmStatus::returned_true&&h.result&&
                h.result->state==odh::LiveEntryPoint5State::evaluated_false&&h.random_calls==0,
                "native action function IDs at or below 255 return source false");
        }
        {
            Harness h;h.start();h.identities_valid=false;
            const auto status=h.source->run(1);
            require(status==odh::VmStatus::returned_true&&h.result&&
                h.result->state==odh::LiveEntryPoint5State::unsupported&&h.random_calls==0,
                "stale tested/other identity evidence is rejected before executing EP5");
        }
        {
            Harness h;h.start(nullptr,true,true);
            const auto status=h.source->run(1);
            require(status==odh::VmStatus::returned_true&&h.result&&
                h.result->state==odh::LiveEntryPoint5State::unsupported&&h.random_calls==0,
                "ghost-image sandbox is an explicit unsupported boundary");
        }
        {
            Harness h;h.start(nullptr,false,false,4100,false,
                [&](const odh::VmFrame& frame,const odh::Instruction&,
                    const odh::Ts1Temps&,const odh::VmDispatch& dispatch){
                    (void)h.world.write(frame,3,25,91);
                    const_cast<odh::Vm*>(dispatch.origin)->temp_xl()[1]=777;
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                        "unmigrated asynchronous primitive"};
                },1,2);
            const auto status=h.source->run(1);
            const odh::VmFrame read{key(4096),0,{1,1,2,h.tested_owner},{},{}};
            require(status==odh::VmStatus::returned_true&&h.result&&
                h.result->state==odh::LiveEntryPoint5State::unsupported&&
                h.world.read(read,3,25)==91&&h.source->temp_xl()[1]==777,
                "failed or unsupported EP5 retains World and shared TempXL effects already made");
        }
        std::cout<<"live_entry_point5: "<<checks<<" checks passed\n";return 0;
    } catch(const std::exception& e) {
        std::cerr<<"live_entry_point5: "<<e.what()<<"\n";return 1;
    }
}
