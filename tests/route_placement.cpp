#include "odh/avatar_position_valid.hpp"
#include "odh/live_entry_point5.hpp"
#include "odh/routing.hpp"
#include "odh/slot_scoring.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}

constexpr odh::CodeOwner actor_owner=1;
constexpr odh::CodeOwner blocker_owner=3;
constexpr std::int16_t actor_id=1,target_id=2,blocker_id=3;
odh::Bhav bhav(std::initializer_list<odh::Instruction> instructions){
    return {0x8002,instructions,{},odh::Bhav::Metadata{0,4,4,0}};
}
odh::SlotTable range_table(){
    odh::SlotRecord slot{};
    slot.type=3;slot.offset_bits={0,0,0};slot.standing=1;slot.sitting=0;slot.ground=0;
    slot.flags=0x8055;slot.snap_target_slot=-1;
    slot.proximity=std::array<std::int32_t,3>{16,32,16};slot.max_size=0;slot.unknown_i10=0;
    slot.gradient_bits=0x3E400000;slot.height=0;slot.facing=-2;slot.resolution=16;
    return {0,10,{slot},0};
}

struct Fixture {
    odh::World world;
    odh::RouteGrid grid{128,128};
    odh::SlotTable slots=range_table();
    odh::Routing routing{world,grid,slots};
    std::shared_ptr<const odh::VmProgram> globals=
        std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
    std::shared_ptr<const odh::VmProgram> actor_bank;
    std::shared_ptr<const odh::VmProgram> blocker_bank;
    odh::Vm actor_vm;
    odh::Vm blocker_vm;
    odh::AvatarPositionValidServices position_services;
    std::unique_ptr<odh::AvatarPositionValid> position_valid;
    odh::RoutePlacementContext captured_context{};
    bool captured=false;
    bool throw_from_provider=false;
    bool reentry_checked=false;
    odh::AvatarPositionValidStatus reentry_status=odh::AvatarPositionValidStatus::success;
    std::size_t candidate_checks=0,intersection_checks=0,ep5_checks=0;
    std::vector<std::pair<std::int16_t,std::int16_t>> ep5_order;
    std::size_t attempt_walk_callbacks=0,set_position_callbacks=0;
    std::optional<odh::RouteExecutionProof> expired_attempt_proof,expired_position_proof;
    bool source_vm_parked=true;
    std::uint64_t source_ticket_expected=0;

    static std::shared_ptr<const odh::VmProgram> make_actor_bank(){
        return std::make_shared<const odh::VmProgram>(odh::RoutineSet{
            {odh::routine_key(4116),bhav({
                {45,1,255,{0,0,2,0,1,0,0,0}},
                {2,254,255,{0,0,91,0,0,5,8,7}}})},
            {odh::routine_key(4117),bhav({
                {27,1,255,{0,0,254,255,0,0,2,0}},
                {2,254,255,{0,0,91,0,0,5,8,7}}})},
            {odh::routine_key(4100),bhav({{99,254,255,{}}})}});
    }
    static std::shared_ptr<const odh::VmProgram> make_blocker_bank(){
        return std::make_shared<const odh::VmProgram>(odh::RoutineSet{
            {odh::routine_key(4300),bhav({{2,254,255,{0,0,1,0,0,0,0,0}}})},
            {odh::routine_key(4101),bhav({{99,254,255,{}}})}});
    }

    Fixture():actor_bank(make_actor_bank()),blocker_bank(make_blocker_bank()),
        actor_vm(globals,odh::ShareVmProgramTag{},world.vm_memory(),odh::VmLimits{},routing.primitive(),
            odh::SharedRoutineOwners{{actor_owner,actor_bank}},odh::ShareVmOwnerProgramsTag{}),
        blocker_vm(globals,odh::ShareVmProgramTag{},world.vm_memory(),odh::VmLimits{},odh::VmPrimitive{},
            odh::SharedRoutineOwners{{blocker_owner,blocker_bank}},odh::ShareVmOwnerProgramsTag{}) {
        require(world.create(actor_id,odh::EntityKind::avatar,{1,88,1})&&
            world.create(target_id,odh::EntityKind::object,{1,0,1})&&
            world.create(blocker_id,odh::EntityKind::object,{1,0,1})&&
            world.set_position(actor_id,{32,32,1,0})&&world.set_position(target_id,{64,64,1,0})&&
            world.set_position(blocker_id,{40,40,1,0})&&world.set_radian_direction(actor_id,0.37f)&&
            world.set_avatar_kill_timeout(actor_id,-1),
            "create positioned route actor, target and obstacle identities");
        require(world.define_type(actor_id,0x10000001)&&world.define_type(target_id,0x10000002)&&
            world.define_type(blocker_id,0x10000003),"bind stable type identities before entry-point metadata");
        for(const auto id:{actor_id,target_id,blocker_id})
            require(world.declare_object_word(id,8,0)&&world.declare_object_word(id,20,0)&&
                world.declare_object_word(id,42,2),"initialize live placement fields");
        std::vector<odh::EntryPoint> actor_entries(27),blocker_entries(27);
        actor_entries[5].action=4100;blocker_entries[5].action=4101;
        require(world.define_entry_points(actor_id,actor_entries)&&world.define_entry_points(blocker_id,blocker_entries),
            "bind exact actor and obstacle EP5 action IDs");

        position_services.architecture=[](auto,const auto&)->std::optional<odh::AvatarPositionArchitecture>{
            return odh::AvatarPositionArchitecture{false,false,1,std::int16_t{0}};
        };
        position_services.intersections=[this](auto,const auto&,auto){
            ++intersection_checks;
            return odh::RoomInfoObstacleQuery{odh::RoomInfoObstacleIndexStatus::complete,
                odh::RoomInfoObstacleIndexReason::none,
                {{blocker_id,0,{0,0,10,10},odh::RoomInfoObstacleCollection::static_set}}};
        };
        position_services.same_group=[](auto,auto)->std::optional<bool>{return false;};
        position_services.entry_point5=[this](const odh::AvatarPositionEntryPoint5Request& source)->std::optional<bool>{
            ++ep5_checks;
            require(source.source_instruction&&source.source_instruction->opcode==45&&
                source.source_vm==&actor_vm&&source.route_placement_proof&&source.source_frame&&
                source.route_placement_proof->matches(*source.source_frame,*source.source_vm,
                    *source.source_instruction,source.source_dispatch),
                "AvatarPositionValid forwards a live route-placement proof to EP5");
            require(source.arguments==std::array<std::int16_t,4>{source.other,source.ghost,0,0}&&source.ghost==0,
                "bilateral EP5 retains exact source arguments under route authority");
            ep5_order.emplace_back(source.tested_entity,source.other);
            if(!reentry_checked){
                reentry_checked=true;
                const odh::RoutePlacementContext nested{source.source_vm,*source.source_frame,
                    *source.source_instruction,source.source_dispatch,*source.route_placement_proof};
                const auto nested_result=position_valid->evaluate_route(nested,actor_id,
                    source.tested_entity==actor_id?source.tested_generation:world.find(actor_id)->generation,
                    {48,48,1,0},0,9);
                reentry_status=nested_result.status;
            }

            const bool tested_actor=source.tested_entity==actor_id;
            const auto code_owner=tested_actor?actor_owner:blocker_owner;
            const auto owner_program=tested_actor?actor_bank:blocker_bank;
            auto* register_owner=tested_actor?&actor_vm:&blocker_vm;
            odh::LiveEntryPoint5Request request;
            request.world=&world;request.tested_entity=source.tested_entity;request.other_entity=source.other;
            request.tested_generation=source.tested_generation;request.other_generation=source.other_generation;
            request.action=source.action;request.ghost=source.ghost;
            request.source_vm=source.source_vm;request.source_frame=source.source_frame;
            request.source_instruction=source.source_instruction;request.source_dispatch=source.source_dispatch;
            request.route_placement_proof=source.route_placement_proof;
            request.code_owner=code_owner;request.global_program=globals;request.owner_program=owner_program;
            request.live_memory=world.vm_memory();request.register_owner=register_owner;
            request.identities_still_match=[this,tested=source.tested_entity,other=source.other,
                tested_generation=source.tested_generation,other_generation=source.other_generation,
                code_owner,owner_program]{
                const auto* a=world.find(tested);const auto* b=world.find(other);
                return a&&b&&a->generation==tested_generation&&b->generation==other_generation&&
                    (tested==actor_id?(code_owner==actor_owner&&owner_program==actor_bank):
                        (code_owner==blocker_owner&&owner_program==blocker_bank));
            };
            request.check_primitive=[this,tested=source.tested_entity,other=source.other,
                expected_owner=code_owner](const odh::VmFrame& frame,const odh::Instruction& instruction,
                    const odh::Ts1Temps&,const odh::VmDispatch& dispatch){
                require(instruction.opcode==99&&frame.context.caller==tested&&frame.context.callee==tested&&
                    frame.context.stack_object==other&&frame.context.code_owner==expected_owner&&
                    dispatch.check_mode,"disposable EP5 uses the tested entity's exact source context");
                require(frame.arguments==std::vector<std::int16_t>{other,0,0,0},
                    "disposable EP5 uses four exact source arguments");
                require(world.write(frame,3,20,static_cast<std::int16_t>(tested*100+other)),
                    "EP5 effect writes live World-backed data");
                auto* check_vm=const_cast<odh::Vm*>(dispatch.origin);
                check_vm->temps()[7]=static_cast<std::int16_t>(tested*10+other);
                check_vm->temp_xl()[1]=static_cast<std::int32_t>(tested*1000+other);
                return odh::PrimitiveOutcome{tested==actor_id?odh::PrimitiveDisposition::goto_false:
                    odh::PrimitiveDisposition::goto_true,{}};
            };
            const auto result=odh::evaluate_live_entry_point5(request);
            require(source.route_placement_proof->matches(*source.source_frame,*source.source_vm,
                *source.source_instruction,source.source_dispatch),
                "route proof remains live after each disposable EP5 callback");
            if(result.state==odh::LiveEntryPoint5State::unsupported)return {};
            return result.state==odh::LiveEntryPoint5State::evaluated_true;
        };
        position_valid=std::make_unique<odh::AvatarPositionValid>(world,std::move(position_services));
        require(routing.bind(actor_vm)&&blocker_vm.start(odh::routine_key(4300),
            {blocker_id,blocker_id,actor_id,blocker_owner})==odh::VmStatus::running,
            "bind Router45 and start the obstacle's independent register owner");
        actor_vm.temps()[4]=17;actor_vm.temp_xl()[0]=29;actor_vm.temp_xl()[1]=29;
        blocker_vm.temps()[4]=31;blocker_vm.temp_xl()[0]=45;blocker_vm.temp_xl()[1]=45;
        routing.set_room_pair_provider([](const auto&,const auto&){
            return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{2,2}};
        });
        routing.set_route_goal_validator([this](const auto&,auto point){
            return grid.contains(point)&&grid.walkable(point);
        });
    }

    odh::Instruction request()const{return {45,1,255,{0,0,2,0,1,0,0,0}};}
    void set_provider(){
        routing.set_authenticated_range_slot_goal_provider([this](const auto& table,std::size_t index,
            const auto& caller,const auto& target,float angle,const odh::RoutePlacementContext& context){
            ++candidate_checks;
            require(context.vm==&actor_vm&&context.instruction.opcode==45&&context.dispatch.origin==&actor_vm&&
                context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch),
                "authenticated range provider receives Routing-minted opcode45 proof");
            captured_context=context;captured=true;

            source_ticket_expected=context.dispatch.ticket;
            const auto before_steps=actor_vm.steps(),before_tickets=context.dispatch.ticket;
            const auto before_pc=actor_vm.stack().back().pc;
            const auto bad_ticket=[&]{
                auto bad=context;++bad.dispatch.ticket;
                return position_valid->evaluate_route(bad,actor_id,world.find(actor_id)->generation,
                    {48,48,1,0},0,9).status;
            }();
            auto bad_frame=context;++bad_frame.frame.pc;
            const auto wrong_frame=position_valid->evaluate_route(bad_frame,actor_id,
                world.find(actor_id)->generation,{48,48,1,0},0,9).status;
            odh::Vm foreign_vm(globals,odh::ShareVmProgramTag{},world.vm_memory());
            auto wrong_vm=context;wrong_vm.vm=&foreign_vm;
            const bool foreign_vm_matches=wrong_vm.proof.matches(wrong_vm.frame,*wrong_vm.vm,
                wrong_vm.instruction,wrong_vm.dispatch);
            require(bad_ticket==odh::AvatarPositionValidStatus::unsupported&&
                wrong_frame==odh::AvatarPositionValidStatus::unsupported&&!foreign_vm_matches,
                "wrong ticket, altered frame and foreign VM cannot reuse the active route proof");

            odh::RandomState random(0x12345678);
            const auto goals=odh::rank_source_standing_range_slot(table,index,caller,target,angle,random,
                [](auto){return odh::SourceSlotRoomResult::same_room;},
                [this,&context,&before_steps,before_tickets,before_pc](odh::RoutePoint point){
                    const auto result=position_valid->evaluate_route(context,actor_id,
                        world.find(actor_id)->generation,{point.x,point.y,world.find(target_id)->position->level,0},0,9);
                    require(context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch)&&
                        actor_vm.status()==odh::VmStatus::running&&actor_vm.steps()==before_steps&&
                        context.dispatch.ticket==before_tickets&&actor_vm.stack().back().pc==before_pc&&
                        world.find(actor_id)->radian_direction==0.37f&&world.find(actor_id)->position->direction==0,
                        "temporary NORTH validation restores actor orientation while outer Main remains parked");
                    if(result.status==odh::AvatarPositionValidStatus::unsupported)
                        return odh::ReferenceSlotVerification::unknown;
                    return result.status==odh::AvatarPositionValidStatus::success?
                        odh::ReferenceSlotVerification::eligible:odh::ReferenceSlotVerification::rejected_after_score;
                });
            require(!goals.empty(),"generic source standing range finds verified goals");
            source_vm_parked=actor_vm.steps()==before_steps&&source_ticket_expected==before_tickets&&
                actor_vm.stack().back().pc==before_pc;
            if(throw_from_provider)throw std::runtime_error("intentional provider exception after proof capture");
            return odh::AuthenticatedRangeSlotGoals{goals,0,0,0};
        });
    }

    void set_execution_callbacks(){
        routing.set_route_execution_callback(odh::RouteExecutionPhase::attempt_walk,[this](const auto& context){
            ++attempt_walk_callbacks;
            auto wrong_opcode=context.instruction;wrong_opcode.opcode=0;
            require(context.vm==&actor_vm&&(context.instruction.opcode==45||context.instruction.opcode==27)&&
                context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch,
                    odh::RouteExecutionPhase::attempt_walk)&&
                !context.proof.matches(context.frame,*context.vm,wrong_opcode,context.dispatch,
                    odh::RouteExecutionPhase::attempt_walk)&&
                !context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch,
                    odh::RouteExecutionPhase::set_position)&&
                !context.proof.matches(context.frame,*context.vm,context.instruction,
                    odh::VmDispatch{context.vm,context.dispatch.ticket+1,false},
                    odh::RouteExecutionPhase::attempt_walk)&&
                !odh::RouteExecutionProof{}.matches(context.frame,*context.vm,context.instruction,
                    context.dispatch,odh::RouteExecutionPhase::attempt_walk),
                "attempt-walk lease authenticates exact active source opcode and phase");
            expired_attempt_proof=context.proof;
        });
        routing.set_route_execution_callback(odh::RouteExecutionPhase::set_position,[this](const auto& context){
            ++set_position_callbacks;
            auto wrong_frame=context.frame;++wrong_frame.pc;
            require(context.instruction.opcode==45&&context.phase==odh::RouteExecutionPhase::set_position&&
                context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch,
                    odh::RouteExecutionPhase::set_position)&&
                !context.proof.matches(wrong_frame,*context.vm,context.instruction,context.dispatch,
                    odh::RouteExecutionPhase::set_position)&&
                !context.proof.matches(context.frame,*context.vm,context.instruction,
                    odh::VmDispatch{context.vm,context.dispatch.ticket+1,false},
                    odh::RouteExecutionPhase::set_position),
                "scheduler placement lease authenticates the waiting route instruction");
            expired_position_proof=context.proof;
            const auto saved_radians=world.find(actor_id)->radian_direction;
            require(world.set_radian_direction(actor_id,0.0f)&&
                context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch,
                    odh::RouteExecutionPhase::set_position),
                "set-position lease allows source's temporary NORTH orientation during EP5");
            require(world.set_radian_direction(actor_id,saved_radians)&&
                context.proof.matches_after_restore(context.frame,*context.vm,context.instruction,
                    context.dispatch,odh::RouteExecutionPhase::set_position),
                "set-position lease requires exact actor orientation restoration before world mutation");
        });
    }

    void start_waiting(){
        require(actor_vm.start(odh::routine_key(4116),{actor_id,actor_id,target_id,actor_owner})==odh::VmStatus::running,
            "start Route45 Main before its authenticated dispatch");
        require(actor_vm.run(1)==odh::VmStatus::waiting&&routing.active(),
            "real Route45 range request enters its pending placement callback");
    }
};

void authenticated_route_placement(){
    Fixture f;f.set_execution_callbacks();f.set_provider();
    const auto original=*f.world.find(actor_id)->position;
    f.start_waiting();
    require(f.captured&&f.candidate_checks==1&&f.source_vm_parked&&
        !f.captured_context.proof.matches(f.captured_context.frame,*f.captured_context.vm,
            f.captured_context.instruction,f.captured_context.dispatch),
        "copied route proof expires immediately when authenticated provider returns");
    require(f.attempt_walk_callbacks>0&&f.expired_attempt_proof&&
        !f.expired_attempt_proof->matches(f.captured_context.frame,*f.captured_context.vm,
            f.captured_context.instruction,f.captured_context.dispatch,odh::RouteExecutionPhase::attempt_walk),
        "attempt-walk lease expires after its synchronous route-planning callback");
    require(f.intersection_checks>0&&f.ep5_checks==2*f.intersection_checks&&
        f.ep5_order.size()==f.ep5_checks&&f.reentry_checked&&
        f.reentry_status==odh::AvatarPositionValidStatus::unsupported,
        "authenticated candidate placement runs bilateral live EP5 and rejects validator reentry");
    for(std::size_t n=0;n<f.ep5_order.size();n+=2)
        require(f.ep5_order[n]==std::pair<std::int16_t,std::int16_t>{actor_id,blocker_id}&&
            f.ep5_order[n+1]==std::pair<std::int16_t,std::int16_t>{blocker_id,actor_id},
            "source placement checks caller then intersecting object in stable bilateral order");
    const auto* actor=f.world.find(actor_id);
    require(actor&&actor->position&&actor->position->x==original.x&&actor->position->y==original.y&&
        actor->position->direction==original.direction&&actor->radian_direction==0.37f&&
        f.actor_vm.status()==odh::VmStatus::waiting&&f.actor_vm.pending()&&f.actor_vm.stack().size()==1&&
        f.actor_vm.stack().back().pc==0&&f.actor_vm.steps()==1&&
        f.actor_vm.snapshot().ticket_sequence==f.source_ticket_expected,
        "route candidate checks leave caller transform and the waiting Main frame intact");
    require(f.actor_vm.temps()[7]==static_cast<std::int16_t>(actor_id*10+blocker_id)&&
        f.actor_vm.temp_xl()[1]==static_cast<std::int32_t>(actor_id*1000+blocker_id)&&
        f.blocker_vm.temps()[7]==static_cast<std::int16_t>(blocker_id*10+actor_id)&&
        f.blocker_vm.temp_xl()[1]==static_cast<std::int32_t>(blocker_id*1000+actor_id),
        "EP5 World/TEMP/TempXL writes publish to each tested entity's own live register bank");
    odh::VmFrame actor_read{odh::routine_key(4116),0,{actor_id,actor_id,target_id,actor_owner},{},{}};
    odh::VmFrame blocker_read{odh::routine_key(4300),0,{blocker_id,blocker_id,actor_id,blocker_owner},{},{}};
    require(f.world.read(actor_read,3,20)==actor_id*100+blocker_id&&
        f.world.read(blocker_read,3,20)==blocker_id*100+actor_id,
        "real EP5 writes persist in the live World data banks");

    for(std::uint64_t tick=1;f.routing.active()&&tick<300;++tick)
        require(f.routing.tick(tick),"advance authenticated route after proof expires");
    require(f.set_position_callbacks>0&&f.expired_position_proof&&
        !f.expired_position_proof->matches(f.captured_context.frame,*f.captured_context.vm,
            f.captured_context.instruction,f.captured_context.dispatch,odh::RouteExecutionPhase::set_position),
        "set-position lease expires after synchronous scheduler placement callbacks");
    require(!f.routing.active()&&f.actor_vm.run(8)==odh::VmStatus::returned_true&&
        f.actor_vm.stack().empty(),"Main resumes after the route reaches a verified source goal");
}

void source27_attempt_walk_lease(){
    Fixture f;std::size_t calls=0;std::optional<odh::RouteExecutionProof> copied;
    f.routing.set_route_execution_callback(odh::RouteExecutionPhase::attempt_walk,[&](const auto& context){
        ++calls;copied=context.proof;
        require(context.instruction.opcode==27&&context.goal==odh::RoutePoint{64,64}&&
            context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch,
                odh::RouteExecutionPhase::attempt_walk),
            "opcode27 receives a scoped proof at its exact OnTopOf destination before grid acceptance");
    });
    require(f.actor_vm.start(odh::routine_key(4117),
        {actor_id,actor_id,target_id,actor_owner})==odh::VmStatus::running&&
        f.actor_vm.run(1)==odh::VmStatus::waiting&&f.routing.active()&&calls==1,
        "source opcode27 uses the authenticated attempt-walk callback seam");
    const auto frame=f.actor_vm.stack().back();
    const auto instruction=f.actor_vm.pending()->instruction.instruction;
    const odh::VmDispatch dispatch{&f.actor_vm,f.actor_vm.pending()->ticket,false};
    require(copied&&!copied->matches(frame,f.actor_vm,instruction,dispatch,
        odh::RouteExecutionPhase::attempt_walk),
        "opcode27 attempt-walk proof cannot be reused after callback return");
}

void route_execution_proof_expires_on_throw(){
    Fixture f;std::optional<odh::RouteExecutionContext> captured;
    f.routing.set_route_execution_callback(odh::RouteExecutionPhase::attempt_walk,[&](const auto& context){
        captured=context;
        throw std::runtime_error("intentional execution callback failure");
    });
    require(f.actor_vm.start(odh::routine_key(4117),
        {actor_id,actor_id,target_id,actor_owner})==odh::VmStatus::running,
        "start callback-exception fixture");
    require(f.actor_vm.run(1)==odh::VmStatus::fault&&captured.has_value()&&
        !captured->proof.matches(captured->frame,*captured->vm,captured->instruction,captured->dispatch,
            odh::RouteExecutionPhase::attempt_walk),
        "throwing source route callback faults explicitly and expires its lease");
}

void attempt_callback_removal_is_sticky(){
    Fixture f;unsigned provider_calls=0,execution_calls=0;
    odh::SlotRecord door_range{};door_range.type=3;door_range.flags=0x54;
    door_range.offset_bits={0,0,0};door_range.standing=1;door_range.snap_target_slot=-1;
    door_range.proximity=std::array<std::int32_t,3>{16,32,16};door_range.max_size=0;
    door_range.unknown_i10=0;door_range.gradient_bits=0x3E400000;door_range.height=0;door_range.facing=-2;
    door_range.resolution=16;f.slots={0,10,{door_range},0};
    f.routing.set_ranged_slot_goal_provider([&](const auto&,std::size_t,const auto&,const auto& target){
        ++provider_calls;std::vector<odh::PreRankedSlotGoal> result;
        const auto geometry=odh::enumerate_source_slot_range_candidates(f.slots,0,target,0.0f);
        require(geometry.size()>=2,"build multiple pre-ranked goals for pointer lifetime regression");
        for(std::size_t n=0;n<2;++n)
            result.push_back({geometry[n].position,geometry[n].entry_flags,geometry[n].facing,{},{},geometry[n].radian_direction});
        return result;
    });
    f.routing.set_route_execution_callback(odh::RouteExecutionPhase::attempt_walk,[&](const auto& context){
        ++execution_calls;
        require(context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch,
            odh::RouteExecutionPhase::attempt_walk),"first ranked goal has a live attempt lease");
        require(f.world.remove(target_id),"remove first callback target to invalidate participant proof");
    });
    require(f.actor_vm.start(odh::routine_key(4116),
        {actor_id,actor_id,target_id,actor_owner})==odh::VmStatus::running,"start ranked callback removal fixture");
    const auto status=f.actor_vm.run(1);
    if(status!=odh::VmStatus::fault||provider_calls!=1||execution_calls!=1||f.world.find(target_id))
        throw std::runtime_error("ranked callback removal: status="+std::to_string(static_cast<int>(status))+
            ", providers="+std::to_string(provider_calls)+", callbacks="+std::to_string(execution_calls)+
            ", target_present="+std::to_string(f.world.find(target_id)!=nullptr)+", diagnostic="+f.actor_vm.diagnostic());
    require(true,"callback participant removal remains unsupported and sticky across later ranked candidates");
}

void route_proof_expires_on_throw(){
    Fixture f;f.throw_from_provider=true;f.set_provider();
    require(f.actor_vm.start(odh::routine_key(4116),{actor_id,actor_id,target_id,actor_owner})==odh::VmStatus::running,
        "start exception-expiry fixture before authenticated dispatch");
    bool raised=false;
    try{(void)f.actor_vm.run(1);}
    catch(const std::runtime_error& error){raised=std::string(error.what())=="intentional provider exception after proof capture";}
    require(raised&&f.captured,
        "throwing authenticated provider unwinds without preserving its callback lease");
    require(!f.captured_context.proof.matches(f.captured_context.frame,*f.captured_context.vm,
        f.captured_context.instruction,f.captured_context.dispatch),
        "copied placement proof is invalid after the provider unwinds exceptionally");
}
}

int main(){try{authenticated_route_placement();source27_attempt_walk_lease();
    route_execution_proof_expires_on_throw();attempt_callback_removal_is_sticky();route_proof_expires_on_throw();
    std::cout<<checks<<" route placement checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"route placement: "<<error.what()<<'\n';return 1;}}
