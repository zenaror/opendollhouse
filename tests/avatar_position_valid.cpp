#include "odh/avatar_position_valid.hpp"
#include "odh/live_entry_point5.hpp"
#include "odh/random.hpp"
#include "odh/snap.hpp"
#include "odh/routing.hpp"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
struct Fixture {
    odh::World world;
    odh::AvatarPositionArchitecture architecture{false,false,1,std::int16_t{0}};
    odh::RoomInfoObstacleQuery query{odh::RoomInfoObstacleIndexStatus::complete,odh::RoomInfoObstacleIndexReason::none,{}};
    odh::AvatarPositionValidServices services;
    odh::Vm vm;
    odh::AvatarPositionValidResult result;
    odh::EntityPosition candidate{32,32,1,0};std::uint8_t direction=0,flags=9;
    std::vector<std::pair<std::int16_t,std::int16_t>> calls;
    std::function<std::optional<bool>(const odh::AvatarPositionEntryPoint5Request&)> answer;
    std::function<std::optional<bool>(std::int16_t,std::int16_t)> group;
    unsigned geometry_calls=0;
    float observed_direction=-1;
    bool probe_route_api=false;
    odh::AvatarPositionValidStatus route_api_status=odh::AvatarPositionValidStatus::success;
    explicit Fixture(std::uint16_t chair2=0,std::uint16_t chair3=0,bool original_saved=false,std::uint16_t main_action=0):
        vm({{odh::routine_key(4096),{0x8002,{{46,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}}}},world.vm_memory(),{},
            [this](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
                odh::AvatarPositionValid validator(world,services);
                if(probe_route_api){
                    const odh::RoutePlacementContext route{&vm,frame,instruction,dispatch,{}};
                    route_api_status=validator.evaluate_route(route,1,world.find(1)->generation,
                        candidate,direction,flags).status;
                }
                result=validator.evaluate(frame,vm,instruction,dispatch,1,world.find(1)->generation,candidate,direction,flags);
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            }){
        for(std::int16_t id=1;id<=4;++id){
            const bool person=id==1||id==4;std::array<std::int16_t,68> words{};words[3]=-1;
            const bool created=original_saved?world.create_saved_entity(id,person?odh::EntityKind::avatar:odh::EntityKind::object,100+id,
                    {0},person?std::vector<std::int16_t>(88):std::vector<std::int16_t>{},{},words,{},1):
                world.create(id,person?odh::EntityKind::avatar:odh::EntityKind::object,person?odh::EntityLayout{1,88,1}:odh::EntityLayout{1,0,1});
            require(created&&
                world.define_type(id,100+id)&&world.set_position(id,{16*id,16,1,0})&&
                world.declare_object_word(id,8,0)&&world.declare_object_word(id,42,0),"create known live placement identities and flags");
            std::vector<odh::EntryPoint> entries(27);entries[5].action=static_cast<std::uint16_t>(4096+id);
            if(id==1)entries[1].action=main_action;
            if(id==2)entries[26].action=chair2;if(id==3)entries[26].action=chair3;
            require(world.define_entry_points(id,entries),"bind known EP5/EP26 metadata");
        }
        require(world.set_radian_direction(1,0.37f),"capture continuous actor direction");
        services.architecture=[this](auto,const auto&)->std::optional<odh::AvatarPositionArchitecture>{return architecture;};
        services.intersections=[this](auto,const auto&,auto){++geometry_calls;observed_direction=world.find(1)->radian_direction;return query;};
        services.same_group=[this](auto actor,auto other)->std::optional<bool>{return group?group(actor,other):std::optional<bool>{false};};
        services.entry_point5=[this](const auto& request)->std::optional<bool>{
            require(request.source_vm==&vm&&request.source_frame&&request.source_instruction&&
                vm.matches_active_primitive(*request.source_frame,*request.source_instruction,request.source_dispatch),
                "typed EP5 retains exact active source46 proof");
            require(!request.route_placement_proof,
                "legacy active source46 placement does not invent a route-placement lease");
            require(request.tested_generation==world.find(request.tested_entity)->generation&&
                request.other_generation==world.find(request.other)->generation&&
                request.arguments==std::array<std::int16_t,4>{request.other,request.ghost,0,0},
                "typed bilateral EP5 request retains generations and source exact arguments");
            calls.emplace_back(request.tested_entity,request.other);
            vm.temps()[6]=static_cast<std::int16_t>(calls.size());vm.temp_xl()[1]=static_cast<std::int32_t>(100+calls.size());
            return answer?answer(request):std::optional<bool>{false};
        };
    }
    void obstacles(std::initializer_list<std::int16_t> ids){
        query.intersections.clear();for(auto id:ids)query.intersections.push_back({id,0,{0,0,10,10},odh::RoomInfoObstacleCollection::static_set});
    }
    void set_word(std::int16_t id,std::int16_t index,std::int16_t value){
        odh::VmFrame frame;frame.context.caller=id;require(world.write(frame,3,index,value),"set live known placement word");
    }
    void run(){require(vm.start(odh::routine_key(4096),{1,1,2})==odh::VmStatus::running&&
        vm.run(1)==odh::VmStatus::returned_true,"evaluate placement in actual source46 dispatch");}
};
void creation_avatar_authority(){
    Fixture f;f.obstacles({2});
    const auto key=odh::routine_key(4700);
    odh::Vm source({{key,{0x8002,{{42,254,255,{}}},{},odh::Bhav::Metadata{0,4,0,0}}}},f.world.vm_memory(),{},
        [](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};});
    require(source.start(key,{4,4,0})==odh::VmStatus::running&&source.run(1)==odh::VmStatus::waiting,
        "avatar creation placement owns a real suspended opcode42 caller");
    bool live=true;unsigned ep5=0;
    odh::LiveObjectCreationPlacementSource proof{&source,source.run_generation(),1,f.world.find(1)->generation,[&]{return live;}};
    auto services=f.services;
    services.entry_point5=[&](const odh::AvatarPositionEntryPoint5Request& request)->std::optional<bool>{
        ++ep5;require(request.object_creation_source&&!request.source_vm&&!request.source_frame&&
            !request.source_instruction&&!request.source_dispatch.origin&&
            request.object_creation_source->source_vm==&source&&request.arguments==std::array<std::int16_t,4>{request.other,0,0,0},
            "creation EP5 forwards only real construction authority rather than a fabricated Snap dispatch");
        return false;
    };
    services.same_group=[](auto left,auto right)->std::optional<bool>{return left==4&&right==2;};
    odh::AvatarPositionValid validator(f.world,services);
    const auto generation=f.world.find(1)->generation;const auto before=source.snapshot();
    require(validator.evaluate_creation(proof,1,generation,f.candidate,0,0).status==odh::AvatarPositionValidStatus::blocked&&ep5==2,
        "ordinary NPC placement runs real bilateral EP5 and respects an actual obstacle");
    proof.ignored_intersection_group=odh::WorldEntityReference{4,f.world.find(4)->generation};ep5=0;
    require(validator.evaluate_creation(proof,1,generation,f.candidate,0,0).status==odh::AvatarPositionValidStatus::success&&ep5==0,
        "UnderneathMe retry ignores only the exact creating Caller multitile group");
    f.obstacles({2,3});
    const auto other=validator.evaluate_creation(proof,1,generation,f.candidate,0,0);
    require(other.status==odh::AvatarPositionValidStatus::blocked&&other.blocker==3&&ep5==2,
        "Caller group retry preserves every unrelated obstacle and its EP5 checks");
    const auto geometry=f.geometry_calls;auto invalid=proof;invalid.ignored_intersection_group->generation++;
    require(validator.evaluate_creation(invalid,1,generation,f.candidate,0,0).status==odh::AvatarPositionValidStatus::unsupported&&f.geometry_calls==geometry,
        "stale retry Caller generation cannot authorize ignored intersections");
    invalid=proof;invalid.source_run_generation++;
    require(validator.evaluate_creation(invalid,1,generation,f.candidate,0,0).status==odh::AvatarPositionValidStatus::unsupported,
        "creation placement refuses a different real VM run");
    live=false;
    require(validator.evaluate_creation(proof,1,generation,f.candidate,0,0).status==odh::AvatarPositionValidStatus::unsupported&&
        source.pending()->ticket==before.pending->ticket&&source.steps()==before.steps,
        "expired creation authority cannot consume or execute its suspended factory");
}
void host_constructor_avatar_authority(){
    Fixture f(0,0,false,4096);f.obstacles({2});
    bool live=true;unsigned calls=0;auto services=f.services;
    services.entry_point5=[&](const auto& request)->std::optional<bool>{
        ++calls;require(request.avatar_constructor_source&&!request.object_creation_source&&
            !request.original_recovery_source&&!request.source_vm&&!request.source_dispatch.origin,
            "constructor geometry forwards its own real source, without Create42 or original recovery");
        return false;
    };
    // CodeOwner0 is not a proven immutable private resource. Bind a real owner.
    const odh::RoutineSet main{{odh::routine_key(4096),
        {0x8002,{{0,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}}}};
    odh::Vm thread({},f.world.vm_memory(),{},odh::VmPrimitive{},odh::RoutineOwners{{44,main}});
    require(thread.start(odh::routine_key(4096),{1,1,0,44})==odh::VmStatus::running,"prepared exact private constructor Thread");
    odh::LiveAvatarConstructorPlacementSource source{&f.world,{1,f.world.find(1)->generation},&thread,thread.run_generation(),44,[&]{return live;}};
    odh::AvatarPositionValid validator(f.world,services);
    const auto before=thread.snapshot();
    require(validator.evaluate_constructor(source,f.candidate,0,0).status==odh::AvatarPositionValidStatus::blocked&&calls==2,
        "VerifyFamily constructor geometry respects bilateral obstacle checks");
    require(thread.steps()==before.steps&&thread.stack().front().pc==0&&!thread.pending(),
        "constructor geometry does not dispatch Main or fabricate a pending opcode");
    const auto geometry=f.geometry_calls;
    auto invalid=source;++invalid.avatar.generation;
    require(validator.evaluate_constructor(invalid,f.candidate,0,0).status==odh::AvatarPositionValidStatus::unsupported&&f.geometry_calls==geometry,
        "constructor stale identity rejects before geometry");
    live=false;
    require(validator.evaluate_constructor(source,f.candidate,0,0).status==odh::AvatarPositionValidStatus::unsupported&&f.geometry_calls==geometry,
        "expired private activation cannot authorize geometry");
}
void prepared_direct_authorization(){
    using Status=odh::AvatarPositionValidStatus;
    for(const auto opcode:{std::uint16_t{5},std::uint16_t{16},std::uint16_t{46}}){
        odh::World world;
        require(world.create(1,odh::EntityKind::avatar,{0,88,1})&&world.create(2,odh::EntityKind::object,{0,0,1})&&
            world.create(3,odh::EntityKind::object,{0,0,1})&&world.set_position(1,{16,16,1,0})&&
            world.set_position(2,{48,16,1,0})&&world.set_position(3,{32,16,1,0})&&
            world.place_in_slot(3,0,1)&&world.declare_object_word(1,8,0)&&world.declare_object_word(1,42,0)&&
            world.set_avatar_kill_timeout(1,-1),"create real held avatar and source placement metadata");
        std::vector<odh::EntryPoint> entries(27);entries[5].action=4100;
        require(world.define_type(1,101)&&world.define_type(2,102)&&
            world.define_entry_points(1,entries)&&world.define_entry_points(2,entries),
            "define prepared placement resource identities before their EP5 metadata");
        unsigned architecture_calls=0,ep5_calls=0;odh::AvatarPositionValidServices services;
        services.architecture=[&](auto,const auto&)->std::optional<odh::AvatarPositionArchitecture>{
            ++architecture_calls;return odh::AvatarPositionArchitecture{false,false,1,std::int16_t{0}};
        };
        services.intersections=[](auto,const auto&,auto){return odh::RoomInfoObstacleQuery{
            odh::RoomInfoObstacleIndexStatus::complete,odh::RoomInfoObstacleIndexReason::none,
            {{2,1,{30,30,34,34},odh::RoomInfoObstacleCollection::static_set}}};};
        services.same_group=[](auto,auto)->std::optional<bool>{return false;};
        services.entry_point5=[&](const odh::AvatarPositionEntryPoint5Request& request)->std::optional<bool>{
            ++ep5_calls;require(request.source_vm&&request.source_frame&&request.source_instruction&&
                request.source_instruction->opcode==opcode&&request.source_vm->matches_active_primitive(
                    *request.source_frame,*request.source_instruction,request.source_dispatch),
                "prepared placement retains the real original direct dispatch for the EP5 consumer");
            return true;
        };
        odh::AvatarPositionValid validator(world,services),other_validator(world,services);
        std::optional<odh::AvatarPositionValid::PreparedAuthorization> retained;
        odh::VmFrame saved_frame;odh::Instruction saved_instruction;odh::VmDispatch saved_dispatch{};
        const auto original_generation=world.find(1)->generation;bool replay=false;
        const odh::VmContext context=opcode==46?odh::VmContext{1,1,2,0}:
            opcode==16?odh::VmContext{3,3,1,0}:odh::VmContext{3,3,2,0};
        odh::Vm vm({{odh::routine_key(4096),{0x8002,{{opcode,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}}}},world.vm_memory(),{},
            [&](const auto& frame,const auto& instruction,const auto&,const odh::VmDispatch& dispatch){
                const auto& source=*dispatch.origin;
                if(replay){require(validator.evaluate_prepared(*retained,frame,source,instruction,dispatch,1,
                    original_generation,{32,32,1,0},0,0).status==Status::unsupported,
                    "prepared authority cannot cross a new VM run with the same authored instruction");
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};}
                retained=validator.prepare(frame,source,instruction,dispatch,1,original_generation);
                require(retained.has_value(),"mint authorization from the actual source5/16/46 before Pre");
                saved_frame=frame;saved_instruction=instruction;saved_dispatch=dispatch;
                require(validator.evaluate_prepared(*retained,frame,source,instruction,dispatch,1,original_generation,
                    {32,32,1,0},0,0).status==Status::success,"prepared evaluation accepts its original reciprocal attachment");
                require(world.detach(3,0),"source Pre empties the original holder slot");
                if(opcode==5){require(!validator.prepare(frame,source,instruction,dispatch,1,original_generation)&&
                    validator.evaluate(frame,source,instruction,dispatch,1,original_generation,{32,32,1,0},0,0).status==Status::unsupported,
                    "Drop cannot mint fresh authority after Pre emptied Caller slot0");}
                require(validator.evaluate_prepared(*retained,frame,source,instruction,dispatch,1,original_generation,
                    {32,32,1,0},0,0).status==Status::success&&ep5_calls==2,
                    "prepared direct placement evaluates after Pre while preserving original EP5 proof");
                const auto before=architecture_calls;
                odh::AvatarPositionValid::PreparedAuthorization absent;
                require(validator.evaluate_prepared(absent,frame,source,instruction,dispatch,1,original_generation,
                    {32,32,1,0},0,0).status==Status::unsupported&&
                    other_validator.evaluate_prepared(*retained,frame,source,instruction,dispatch,1,original_generation,
                    {32,32,1,0},0,0).status==Status::unsupported,"default and foreign-validator authorizations are rejected");
                auto wrong_dispatch=dispatch;++wrong_dispatch.ticket;
                auto wrong_instruction=instruction;wrong_instruction.operands[0]^=1;
                auto wrong_frame=frame;wrong_frame.locals.push_back(99);
                require(validator.evaluate_prepared(*retained,frame,source,instruction,wrong_dispatch,1,original_generation,
                    {32,32,1,0},0,0).status==Status::unsupported&&
                    validator.evaluate_prepared(*retained,frame,source,wrong_instruction,dispatch,1,original_generation,
                    {32,32,1,0},0,0).status==Status::unsupported&&
                    validator.evaluate_prepared(*retained,wrong_frame,source,instruction,dispatch,1,original_generation,
                    {32,32,1,0},0,0).status==Status::unsupported&&
                    validator.evaluate_prepared(*retained,frame,source,instruction,dispatch,1,original_generation+1,
                    {32,32,1,0},0,0).status==Status::unsupported&&architecture_calls==before,
                    "prepared authorization rejects changed ticket, instruction, frame and generation before callbacks");
                require(world.place_in_slot(2,0,1),"reparent the tested identity outside the authorized source relation");
                require(validator.evaluate_prepared(*retained,frame,source,instruction,dispatch,1,original_generation,
                    {32,32,1,0},0,0).status==Status::unsupported,"a different live parent is not the source Pre detach");
                require(world.detach(2,0)&&world.remove(1)&&world.create(1,odh::EntityKind::avatar,{0,88,1})&&
                    world.set_position(1,{32,32,1,0}),"replace the same numeric target with a fresh generation");
                require(validator.evaluate_prepared(*retained,frame,source,instruction,dispatch,1,world.find(1)->generation,
                    {32,32,1,0},0,0).status==Status::unsupported,"replacement generations cannot reuse prepared placement authority");
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            });
        require(vm.start(odh::routine_key(4096),context)==odh::VmStatus::running&&vm.run(1)==odh::VmStatus::returned_true,
            "run prepared placement checks inside the authentic synchronous primitive");
        require(validator.evaluate_prepared(*retained,saved_frame,vm,saved_instruction,saved_dispatch,1,original_generation,
            {32,32,1,0},0,0).status==Status::unsupported,"prepared placement expires immediately after the dispatch returns");
        replay=true;require(vm.start(odh::routine_key(4096),context)==odh::VmStatus::running&&vm.run(1)==odh::VmStatus::returned_true,
            "new VM run cannot reuse the previous prepared proof");
    }
}
void route_execution_scope(){
    using Phase=odh::RouteExecutionPhase;
    using Status=odh::AvatarPositionValidStatus;
    for(const auto opcode:{std::uint16_t{45},std::uint16_t{27}}){
        odh::World world;odh::RouteGrid grid(16,16);
        for(std::int16_t id=1;id<=3;++id){
            require(world.create(id,id==1?odh::EntityKind::avatar:odh::EntityKind::object,
                {1,id==1?88U:0U,0})&&world.define_type(id,static_cast<std::uint32_t>(100+id))&&
                world.set_position(id,{id==1?4:8,4,1,0})&&world.declare_object_word(id,8,0)&&
                world.declare_object_word(id,42,0),"create typed route placement identities before metadata");
            std::vector<odh::EntryPoint> entries(27);entries[5].action=4100;
            require(world.define_entry_points(id,entries),"define route placement live entry point metadata");
        }
        require(world.set_radian_direction(1,0.37f)&&world.set_avatar_kill_timeout(1,-1),
            "bind non-NORTH routed avatar lifecycle");
        odh::SlotRecord slot{};slot.type=3;slot.snap_target_slot=-1;slot.facing=-2;
        const odh::SlotTable table{0,10,{slot},0};odh::Routing route(world,grid,table);
        const odh::Instruction instruction=opcode==45?odh::Instruction{45,254,255,{0,0,2,0,1,0,0,0}}:
            odh::Instruction{27,254,255,{0,0,254,255,0,0,2,0}};
        odh::Vm vm({{odh::routine_key(4096),{0x8002,{instruction},{},odh::Bhav::Metadata{0,4,4,0}}}},
            world.vm_memory(),{},route.primitive());
        require(route.bind(vm),"bind typed route placement to actual VM");
        route.set_route_goal_validator([](const auto&,auto){return true;});
        route.set_room_pair_provider([](const auto&,const auto&){return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,1}};});
        unsigned geometry=0,ep5=0,attempts=0,moves=0;std::optional<odh::RouteExecutionContext> retained;
        odh::AvatarPositionValidServices services;
        services.architecture=[&](auto,const auto&)->std::optional<odh::AvatarPositionArchitecture>{
            ++geometry;return odh::AvatarPositionArchitecture{false,false,1,std::int16_t{0}};
        };
        services.intersections=[&](auto,const auto&,auto){
            require(world.find(1)->radian_direction==0,"source movement PositionValid temporarily exposes NORTH");
            return odh::RoomInfoObstacleQuery{odh::RoomInfoObstacleIndexStatus::complete,
                odh::RoomInfoObstacleIndexReason::none,{{3,1,{6,2,10,6},odh::RoomInfoObstacleCollection::static_set}}};
        };
        services.same_group=[](auto,auto)->std::optional<bool>{return false;};
        services.entry_point5=[&](const odh::AvatarPositionEntryPoint5Request& call)->std::optional<bool>{
            ++ep5;
            require(call.route_execution_proof&&!call.route_placement_proof&&call.route_execution_phase==Phase::set_position&&
                call.route_execution_proof->matches(*call.source_frame,*call.source_vm,*call.source_instruction,
                    call.source_dispatch,call.route_execution_phase)&&
                !call.route_execution_proof->matches_after_restore(*call.source_frame,*call.source_vm,*call.source_instruction,
                    call.source_dispatch,call.route_execution_phase)&&call.ghost==0&&
                call.arguments==std::array<std::int16_t,4>{call.other,0,0,0},
                "movement placement forwards typed authority and exact bilateral arguments during temporary NORTH");
            return false;
        };
        odh::AvatarPositionValid validator(world,std::move(services));
        route.set_route_execution_callback(Phase::attempt_walk,[&](const odh::RouteExecutionContext& context){
            ++attempts;
            require(validator.evaluate_execution(context,1,world.find(1)->generation,{5,4,1,0},0,0).status==Status::unsupported&&
                geometry==0&&world.find(1)->radian_direction==0.37f,
                "AttemptWalk authority cannot trigger NORTH PositionValid or movement placement callbacks");
        });
        route.set_route_execution_callback(Phase::set_position,[&](const odh::RouteExecutionContext& context){
            ++moves;retained=context;const auto generation=world.find(1)->generation;
            auto wrong=context;++wrong.dispatch.ticket;
            const auto before=geometry;
            require(validator.evaluate_execution(wrong,1,generation,{5,4,1,0},0,0).status==Status::unsupported&&
                validator.evaluate_execution(context,1,generation+1,{5,4,1,0},0,0).status==Status::unsupported&&
                validator.evaluate_execution(context,1,generation,{5,4,1,0},1,0).status==Status::unsupported&&
                validator.evaluate_execution(context,1,generation,{5,4,1,0},0,9).status==Status::unsupported&&geometry==before,
                "stale ticket/generation and non-source heading/flags reject before geometry or EP5 effects");
            const auto result=validator.evaluate_execution(context,1,generation,{5,4,1,0},0,0);
            require(result.status==Status::blocked&&result.blocker==3&&ep5==2&&
                world.find(1)->radian_direction==0.37f&&context.proof.matches_after_restore(context.frame,
                    vm,context.instruction,context.dispatch,context.phase),
                "real route SetPosition lease validates bilateral EP5 and restores the original continuous heading");
        });
        require(vm.start(odh::routine_key(4096),{1,1,2})==odh::VmStatus::running&&vm.run(1)==odh::VmStatus::waiting,
            "actual Route45/GotoRelative27 enters a source route wait");
        require(attempts==1&&moves==0&&route.tick(1)&&moves==1&&retained,
            "Routing alone issues AttemptWalk and pending-job movement scopes at their real boundaries");
        const auto before=geometry;
        require(validator.evaluate_execution(*retained,1,world.find(1)->generation,{5,4,1,0},0,0).status==Status::unsupported&&
            geometry==before,"retained movement context expires after the actual route callback returns");
    }
}
void overlap_order_and_effects(){
    Fixture f;f.direction=7;f.obstacles({3,2});f.run();
    constexpr double pi=3.141592653589793238462643383279502884;
    require(f.result.status==odh::AvatarPositionValidStatus::blocked&&f.result.placement_error==10&&f.result.blocker==3&&
        f.result.blocker_generation==f.world.find(3)->generation&&!f.result.blocker_is_avatar,
        "source sorted overlap order selects last known blocker without an EP26 stop");
    require(f.calls==std::vector<std::pair<std::int16_t,std::int16_t>>{{1,2},{2,1},{1,3},{3,1}}&&
        f.observed_direction==7*static_cast<float>(pi/4.0)&&f.world.find(1)->radian_direction==0.37f&&
        f.vm.temps()[6]==4&&f.vm.temp_xl()[1]==104,"live bilateral EP5 observes quantized direction and retains Temp/XL effects after failure");
    Fixture chair(4000);chair.obstacles({3,2});chair.run();
    require(chair.result.blocker==2&&chair.calls.size()==2,"first source chair blocker stops later live callbacks");
    Fixture exempt;exempt.obstacles({2});exempt.answer=[](const auto& request){return std::optional<bool>{request.tested_entity==1};};exempt.run();
    require(exempt.result.status==odh::AvatarPositionValidStatus::success&&exempt.calls.size()==1,
        "actor EP5 true short circuits the blocker entry point");
    Fixture grouped;grouped.obstacles({2,3});grouped.group=[](auto,auto other){return std::optional<bool>{other==2};};grouped.run();
    require(grouped.result.blocker==3&&grouped.calls.size()==2,"same authoritative group bypasses both EP5 calls");
    Fixture unknown;unknown.obstacles({2});unknown.answer=[](const auto&)->std::optional<bool>{return {};};unknown.run();
    require(unknown.result.status==odh::AvatarPositionValidStatus::unsupported&&unknown.calls.size()==1&&
        unknown.world.find(1)->radian_direction==0.37f,"unknown EP5 is explicit unsupported and restores temporary heading");
}
void ranged_relative_placement_scope(){
    using Status=odh::AvatarPositionValidStatus;
    for(const auto facing:{std::uint8_t{254},std::uint8_t{255},std::uint8_t{3}}){
        odh::World world;odh::RouteGrid grid(128,128);
        for(std::int16_t id=1;id<=2;++id){
            require(world.create(id,id==1?odh::EntityKind::avatar:odh::EntityKind::object,{1,id==1?88U:0U,0})&&
                world.define_type(id,100+id)&&world.set_position(id,{32*id,32,1,0})&&
                world.declare_object_word(id,8,0)&&world.declare_object_word(id,42,0),"create ranged placement participants");
            std::vector<odh::EntryPoint> entries(27);entries[5].action=4100;
            require(world.define_entry_points(id,entries),"bind bilateral ranged placement EP5 metadata");
        }
        require(world.set_avatar_kill_timeout(1,-1)&&world.set_radian_direction(1,0.37f),"set live ranged avatar lifecycle");
        const odh::Instruction instruction{27,254,255,{5,0,0,facing,0,0,2,0}};
        odh::Routing route(world,grid,odh::SlotTable{});
        odh::Vm vm({{odh::routine_key(4096),{0x8002,{instruction},{},odh::Bhav::Metadata{0,4,4,0}}}},
            world.vm_memory(),{},route.primitive());
        require(route.bind(vm),"bind raw ranged relative instruction to actual VM");
        route.set_route_goal_validator([&](const auto&,odh::RoutePoint point){return grid.contains(point)&&grid.walkable(point);});
        route.set_room_pair_provider([](const auto&,const auto&){return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,1}};});
        unsigned geometry=0,ep5=0,providers=0;std::optional<odh::RoutePlacementContext> retained;
        odh::AvatarPositionValidServices services;
        services.architecture=[&](auto,const auto&)->std::optional<odh::AvatarPositionArchitecture>{
            ++geometry;return odh::AvatarPositionArchitecture{false,false,1,std::int16_t{0}};};
        services.intersections=[](auto,const auto&,auto){return odh::RoomInfoObstacleQuery{
            odh::RoomInfoObstacleIndexStatus::complete,odh::RoomInfoObstacleIndexReason::none,
            {{2,1,{30,30,36,36},odh::RoomInfoObstacleCollection::static_set}}};};
        services.same_group=[](auto,auto)->std::optional<bool>{return false;};
        services.entry_point5=[&](const odh::AvatarPositionEntryPoint5Request& request)->std::optional<bool>{
            ++ep5;
            require(request.route_placement_proof&&!request.route_execution_proof&&
                request.source_instruction->opcode==27&&request.source_instruction->operands==instruction.operands&&
                request.route_placement_proof->matches(*request.source_frame,vm,*request.source_instruction,request.source_dispatch),
                "bilateral EP5 receives the real raw27 callback proof without a fabricated Route45");
            vm.temps()[6]=static_cast<std::int16_t>(ep5);
            require(world.set_person_data(1,10,70+ep5),"ranged live EP5 publishes source World effect");
            return false;
        };
        odh::AvatarPositionValid validator(world,std::move(services));
        route.set_authenticated_range_slot_goal_provider([&](const auto&,std::size_t index,const auto&,const auto&,float,
            const odh::RoutePlacementContext& context){
            ++providers;retained=context;const auto generation=world.find(1)->generation;
            require(index==0&&context.instruction.opcode==27,"transient range uses entry zero and original opcode");
            auto forged=context;forged.proof={};auto wrong_ticket=context;++wrong_ticket.dispatch.ticket;
            auto fake45=context;fake45.instruction.opcode=45;
            require(validator.evaluate_route(forged,1,generation,{36,32,1,0},0,9).status==Status::unsupported&&
                validator.evaluate_route(wrong_ticket,1,generation,{36,32,1,0},0,9).status==Status::unsupported&&
                validator.evaluate_route(fake45,1,generation,{36,32,1,0},0,9).status==Status::unsupported&&geometry==0,
                "unminted, altered-ticket and forged-opcode contexts reject before geometry");
            require(validator.evaluate_route(context,1,generation,{36,32,1,0},0,9).status==Status::blocked&&
                ep5==2&&vm.temps()[6]==2&&world.find(1)->person_data[10]==72&&world.find(1)->radian_direction==0.37f,
                "raw27 placement executes both live EP5 sides, preserves effects and restores only temporary heading");
            return odh::AuthenticatedRangeSlotGoals{};
        });
        require(vm.start(odh::routine_key(4096),{1,1,2})==odh::VmStatus::running,
            "start actual transient range source");
        (void)vm.run(1);
        if(providers!=1||!retained)throw std::runtime_error("raw27 invokes authenticated range provider: facing="+
            std::to_string(facing)+" vm="+std::string(odh::vm_status_name(vm.status()))+" diagnostic="+vm.diagnostic()+
            " route="+route.diagnostic());
        const auto before=geometry;
        require(validator.evaluate_route(*retained,1,world.find(1)->generation,{36,32,1,0},0,9).status==Status::unsupported&&geometry==before,
            "retained raw27 placement proof expires when its callback ends");
    }
}
void route_api_requires_opcode45(){
    Fixture f;f.probe_route_api=true;f.run();
    require(f.route_api_status==odh::AvatarPositionValidStatus::unsupported&&
        f.result.status==odh::AvatarPositionValidStatus::success,
        "route-only API rejects active opcode46 while legacy active46 validation still succeeds");
}
void floor_and_flags(){
    struct Case{std::uint16_t floor;std::int16_t flags,level,offset,error;};
    for(const auto& item:std::vector<Case>{{65535,0,1,0,28},{65535,2048,1,0,-1},{1,4096,1,0,30},
        {65534,0,1,0,28},{65534,4,1,0,-1},{1,8192,1,0,29},{0,0,1,0,19},{0,2,1,0,-1},
        {0,0,2,0,17},{0,512,2,0,-1},{0,0,2,1,-1},{1,0,2,0,-1}}){
        Fixture f;f.architecture.precise_floor=item.floor;f.architecture.level_offset=item.offset;f.candidate.level=item.level;
        f.set_word(1,42,item.flags);f.run();
        require(f.result.placement_error==item.error&&f.result.status==(item.error==-1?odh::AvatarPositionValidStatus::success:
            odh::AvatarPositionValidStatus::blocked),"avatar floor rules preserve source pool/water/terrain/air precedence");
    }
    Fixture bounds;bounds.architecture.out_of_bounds=true;bounds.flags=4;bounds.run();
    require(bounds.result.placement_error==0&&bounds.geometry_calls==0,"bounds precedes AllowIntersection bypass");
    Fixture bypass;bypass.flags=4;bypass.obstacles({2});bypass.run();
    require(bypass.result.status==odh::AvatarPositionValidStatus::success&&bypass.geometry_calls==0&&bypass.calls.empty()&&
        bypass.world.find(1)->radian_direction==0.37f,"AllowIntersection bypasses only overlap and temporary heading");
    Fixture user;user.flags=2;user.architecture.user_out_of_bounds=true;user.run();
    require(user.result.placement_error==0,"UserBuildableLimit enforces explicit user bounds");
    Fixture oow;oow.candidate={-32768,-32768,1,0};oow.services.architecture={};oow.run();
    require(oow.result.status==odh::AvatarPositionValidStatus::success&&oow.geometry_calls==0,"exact OUT_OF_WORLD sentinel succeeds before architecture");
    Fixture retiring;retiring.obstacles({2});require(retiring.world.set_avatar_kill_timeout(1,0),"set source avatar lifecycle");retiring.run();
    require(retiring.result.status==odh::AvatarPositionValidStatus::success&&retiring.geometry_calls==0&&
        retiring.world.find(1)->radian_direction==0.37f,"retiring avatar with null temporary footprint bypasses RoomInfo");
    Fixture zero_extent;zero_extent.obstacles({2});require(zero_extent.world.set_avatar_kill_timeout(1,0),"set zero extent lifecycle");
    zero_extent.set_word(1,8,4);zero_extent.run();
    require(zero_extent.result.status==odh::AvatarPositionValidStatus::blocked,"pinned avatar zero-extent exception still builds temporary footprint");
    Fixture solid;solid.obstacles({4});solid.set_word(1,8,16);solid.set_word(4,8,16);solid.run();
    require(solid.result.status==odh::AvatarPositionValidStatus::blocked&&solid.result.blocker_is_avatar,
        "first phase AllAvatarsSolid overrides both avatar intersection permissions");
    Fixture allowed;allowed.flags=0;allowed.obstacles({4});allowed.set_word(1,8,16);allowed.run();
    require(allowed.result.status==odh::AvatarPositionValidStatus::success,"final default phase honors actor AllowPersonIntersection");
    Fixture disallow;disallow.flags=0;disallow.obstacles({2});disallow.set_word(2,8,18);disallow.run();
    require(disallow.result.status==odh::AvatarPositionValidStatus::blocked,"object DisallowPersonIntersection overrides object allow flag");
}
void callbacks_and_generations(){
    Fixture throwing;throwing.obstacles({2});throwing.answer=[&](const auto&)->std::optional<bool>{
        require(throwing.world.set_person_data(1,10,81),"EP5 mutates live World");throw std::runtime_error("unsupported BHAV service");};throwing.run();
    require(throwing.result.status==odh::AvatarPositionValidStatus::unsupported&&throwing.world.find(1)->person_data[10]==81&&
        throwing.vm.temps()[6]==1&&throwing.world.find(1)->radian_direction==0.37f,
        "throwing live callback retains source effects while restoring only temporary direction");
    Fixture replacement;replacement.obstacles({2});const auto old=replacement.world.find(1)->generation;
    replacement.answer=[&](const auto&)->std::optional<bool>{
        require(replacement.world.remove(1)&&replacement.world.create(1,odh::EntityKind::avatar,{0,88,0})&&
            replacement.world.set_radian_direction(1,2.25f),"callback recycles actor identity");return false;};replacement.run();
    require(replacement.result.status==odh::AvatarPositionValidStatus::unsupported&&replacement.world.find(1)->generation!=old&&
        replacement.world.find(1)->radian_direction==2.25f,"temporary direction restoration never clobbers a recycled actor");
    Fixture replaced_obstacle;replaced_obstacle.obstacles({2});
    const auto old_obstacle=replaced_obstacle.world.find(2)->generation;
    replaced_obstacle.group=[&](auto,auto)->std::optional<bool>{
        require(replaced_obstacle.world.remove(2)&&
            replaced_obstacle.world.create(2,odh::EntityKind::object),
            "group callback replaces obstacle identity");return true;};
    replaced_obstacle.run();
    require(replaced_obstacle.result.status==odh::AvatarPositionValidStatus::unsupported&&
        replaced_obstacle.world.find(2)->generation!=old_obstacle&&replaced_obstacle.calls.empty(),
        "same-group clearance revalidates obstacle generation before skipping it");
    Fixture missing;missing.query.status=odh::RoomInfoObstacleIndexStatus::unsupported;missing.run();
    require(missing.result.status==odh::AvatarPositionValidStatus::unsupported&&missing.world.find(1)->radian_direction==0.37f,
        "incomplete RoomInfo cannot become empty successful clearance");
    Fixture inactive;odh::AvatarPositionValid validator(inactive.world,inactive.services);odh::VmFrame frame;
    frame.context={1,1,2};
    require(validator.evaluate(frame,inactive.vm,{46,254,255,{}},{&inactive.vm,1},1,inactive.world.find(1)->generation,
        {32,32,1,0},0,9).status==odh::AvatarPositionValidStatus::unsupported&&inactive.geometry_calls==0,
        "inactive fabricated source46 proof refuses before callbacks");
    odh::RoutePlacementContext unproved_route;
    unproved_route.vm=&inactive.vm;unproved_route.frame=frame;
    unproved_route.instruction={45,254,255,{}};unproved_route.dispatch={&inactive.vm,1,false};
    require(validator.evaluate_route(unproved_route,1,inactive.world.find(1)->generation,
        {32,32,1,0},0,9).status==odh::AvatarPositionValidStatus::unsupported&&inactive.geometry_calls==0,
        "default-invalid route placement proof cannot authorize opcode45 validation");
}

odh::Bhav cross_module_bhav(std::initializer_list<odh::Instruction> instructions,unsigned args=4){
    return {0x8002,instructions,{},odh::Bhav::Metadata{0,static_cast<std::uint8_t>(args),4,0}};
}
odh::SlotTable cross_module_snap_table(){
    std::vector<odh::SlotRecord> records(6);
    for(auto& record:records)record.type=3;
    auto& route=records[5];route.snap_target_slot=-1;route.flags=4112;
    route.offset_bits={0,0,0};route.facing=-2;route.sitting=0;
    route.proximity=std::array<std::int32_t,3>{16,16,16};route.max_size=0;
    route.unknown_i10=0;route.gradient_bits=0x3E400000;route.height=0;route.resolution=16;
    return {0,10,std::move(records),0};
}
void live_snap_avatar_position_valid_ep5(){
    odh::World world;
    constexpr odh::CodeOwner actor_owner=100,object_owner=200;
    require(world.create(1,odh::EntityKind::avatar,{1,88,1})&&
        world.create(2,odh::EntityKind::object,{1,0,0})&&
        world.define_type(1,0x10000001)&&world.define_type(2,0x10000002)&&
        world.set_position(1,{10,20,1,0})&&world.set_position(2,{48,64,1,0})&&
        world.set_radian_direction(1,0.37f)&&world.set_avatar_kill_timeout(1,-1),
        "create real Snap/placement actor and object entities");
    for(const auto id:{std::int16_t{1},std::int16_t{2}})
        require(world.declare_object_word(id,8,0)&&world.declare_object_word(id,42,2),
            "initialize live intersection and placement fields");
    require(world.declare_object_word(1,52,17)&&world.declare_object_word(1,54,23),
        "initialize Snap failure-result registers");
    std::vector<odh::EntryPoint> actor_entries(27),object_entries(27);
    actor_entries[5].action=4100;object_entries[5].action=4101;
    require(world.define_entry_points(1,actor_entries)&&world.define_entry_points(2,object_entries),
        "bind real actor and blocker EP5 resource identities");

    const auto global_program=std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
    auto snap_instruction=odh::Instruction{46,254,255,{5,0,4,0,0,0,0,0}};
    auto random_false=odh::Instruction{8,255,254,{0,0,8,0,255,255,7,0}};
    auto random_true=odh::Instruction{8,254,255,{0,0,8,0,255,255,7,0}};
    const auto actor_program=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
        {odh::routine_key(4096),cross_module_bhav({snap_instruction})},
        {odh::routine_key(4100),cross_module_bhav({{99,1,1,{}},random_false},9)}});
    const auto object_program=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
        {odh::routine_key(4101),cross_module_bhav({{99,1,1,{}},random_true},11)},
        {odh::routine_key(4200),cross_module_bhav({{99,254,255,{}}})}});
    odh::RandomState random{7};std::vector<odh::RandomRecord> random_records;
    const auto random_handler=odh::random_primitive(random,world.vm_memory(),
        [&](const auto& record){random_records.push_back(record);});
    odh::Vm* actor_vm=nullptr;odh::Vm* object_vm=nullptr;
    std::vector<std::pair<std::int16_t,std::int16_t>> check_order;
    std::vector<std::uint8_t> placement_directions;
    std::vector<float> temporary_directions;
    std::vector<std::array<std::int16_t,4>> check_arguments;
    bool caller_checks_false=true,blocker_checks_true=true;
    const odh::VmPrimitive check_primitive=[&](const odh::VmFrame& frame,
            const odh::Instruction& instruction,const odh::Ts1Temps&,const odh::VmDispatch& dispatch){
        if(instruction.opcode==99){
            const auto tested=frame.context.caller;
            const auto expected_owner=tested==1?actor_owner:object_owner;
            const auto expected_action=tested==1?4100:4101;
            const std::int16_t expected_other=tested==1?2:1;
            require(frame.key.id==expected_action&&frame.context.callee==tested&&
                frame.context.stack_object==expected_other&&frame.context.code_owner==expected_owner&&
                frame.arguments==std::vector<std::int16_t>{expected_other,0,0,0}&&frame.exact_arguments&&dispatch.check_mode,
                "live EP5 frame has tested Caller/Callee, other StackObject and exact source arguments");
            check_order.emplace_back(tested,expected_other);
            check_arguments.push_back({frame.arguments[0],frame.arguments[1],frame.arguments[2],frame.arguments[3]});
            require(world.write(frame,0,0,tested==1?111:222),"EP5 mutates the live tested entity attribute");
            auto* check_vm=const_cast<odh::Vm*>(dispatch.origin);
            check_vm->temps()[6]=tested==1?111:222;
            check_vm->temp_xl()[1]=tested==1?1111:2222;
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        }
        if(instruction.opcode==8)return random_handler(frame,instruction,{},dispatch);
        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"unmigrated EP5 primitive"};
    };

    odh::AvatarPositionValidServices position_services;
    position_services.architecture=[](auto,const auto&)->std::optional<odh::AvatarPositionArchitecture>{
        return odh::AvatarPositionArchitecture{false,false,0,std::int16_t{0}};
    };
    position_services.intersections=[&](auto,const auto&,auto)->odh::RoomInfoObstacleQuery{
        temporary_directions.push_back(world.find(1)->radian_direction);
        return {odh::RoomInfoObstacleIndexStatus::complete,odh::RoomInfoObstacleIndexReason::none,
            {{2,0,{40,56,56,72},odh::RoomInfoObstacleCollection::static_set}}};
    };
    position_services.same_group=[](auto,auto)->std::optional<bool>{return false;};
    position_services.entry_point5=[&](const odh::AvatarPositionEntryPoint5Request& source){
        require(source.source_vm==actor_vm&&source.source_frame&&source.source_instruction&&
            source.source_instruction->opcode==46&&actor_vm->matches_active_primitive(*source.source_frame,
                *source.source_instruction,source.source_dispatch),
            "EP5 adapter receives the active outer Snap source-46 proof");
        require(source.arguments==std::array<std::int16_t,4>{source.other,source.ghost,0,0}&&
            source.ghost==0,"position adapter preserves source bilateral arguments and ghost flag");
        const auto owner=source.tested_entity==1?actor_owner:object_owner;
        const auto bank=source.tested_entity==1?actor_program:object_program;
        auto* registers=source.tested_entity==1?actor_vm:object_vm;
        odh::LiveEntryPoint5Request request;
        request.world=&world;request.tested_entity=source.tested_entity;request.other_entity=source.other;
        request.tested_generation=source.tested_generation;request.other_generation=source.other_generation;
        request.action=source.action;request.ghost=source.ghost;request.source_vm=source.source_vm;
        request.source_frame=source.source_frame;request.source_instruction=source.source_instruction;
        request.source_dispatch=source.source_dispatch;request.route_placement_proof=source.route_placement_proof;
        request.code_owner=owner;
        request.global_program=global_program;request.owner_program=bank;request.live_memory=world.vm_memory();
        request.register_owner=registers;request.no_thread_proven=false;
        request.identities_still_match=[&,tested=source.tested_entity,other=source.other,
                tested_generation=source.tested_generation,other_generation=source.other_generation,
                owner,bank]{
            const auto* tested_entity=world.find(tested);const auto* other_entity=world.find(other);
            return tested_entity&&other_entity&&tested_entity->generation==tested_generation&&
                other_entity->generation==other_generation&&
                (tested==1?(owner==actor_owner&&bank==actor_program):(owner==object_owner&&bank==object_program));
        };
        request.check_primitive=check_primitive;request.instruction_budget=4096;
        const auto result=odh::evaluate_live_entry_point5(request);
        if(result.state==odh::LiveEntryPoint5State::unsupported)return std::optional<bool>{};
        if(source.tested_entity==1)caller_checks_false=caller_checks_false&&
            result.state==odh::LiveEntryPoint5State::evaluated_false;
        else blocker_checks_true=blocker_checks_true&&
            result.state==odh::LiveEntryPoint5State::evaluated_true;
        return std::optional<bool>{result.state==odh::LiveEntryPoint5State::evaluated_true};
    };
    odh::AvatarPositionValid position_valid(world,std::move(position_services));
    odh::Snap::DirectionalServices snap_services;
    odh::AvatarPositionValidResult candidate_result,final_result;
    snap_services.candidate=[&](const auto& frame,const auto& instruction,const auto& dispatch,
            std::int16_t actor,std::int16_t,const odh::EntityPosition& point){
        placement_directions.push_back(point.direction);
        candidate_result=position_valid.evaluate(frame,*actor_vm,instruction,dispatch,actor,
            world.find(actor)->generation,point,0,9);
        if(candidate_result.status==odh::AvatarPositionValidStatus::unsupported)
            return odh::Snap::SourcePlacementResult{odh::Snap::SourcePlacementStatus::unsupported};
        if(candidate_result.status==odh::AvatarPositionValidStatus::success)
            return odh::Snap::SourcePlacementResult{odh::Snap::SourcePlacementStatus::accepted};
        const auto* blocker=world.find(candidate_result.blocker);
        if(!candidate_result.blocker||!blocker)
            return odh::Snap::SourcePlacementResult{odh::Snap::SourcePlacementStatus::blocked_object};
        return odh::Snap::SourcePlacementResult{
            blocker->kind==odh::EntityKind::avatar?odh::Snap::SourcePlacementStatus::blocked_avatar:
                odh::Snap::SourcePlacementStatus::blocked_object,
            blocker->id,blocker->generation};
    };
    snap_services.place=[&](const auto& frame,const auto& instruction,const auto& dispatch,
            std::int16_t actor,std::int16_t,const odh::EntityPosition& point){
        placement_directions.push_back(point.direction);
        const auto generation=world.find(actor)->generation;
        const auto prepared=position_valid.prepare(frame,*actor_vm,instruction,dispatch,actor,generation);
        require(prepared.has_value(),"real Snap final placement captures its actual prepared source authorization");
        final_result=position_valid.evaluate_prepared(*prepared,frame,*actor_vm,instruction,dispatch,actor,
            generation,point,point.direction,0);
        if(final_result.status==odh::AvatarPositionValidStatus::unsupported)
            return odh::Snap::SourcePlacementResult{odh::Snap::SourcePlacementStatus::unsupported};
        if(final_result.status==odh::AvatarPositionValidStatus::blocked){
            const auto* blocker=world.find(final_result.blocker);
            if(!final_result.blocker||!blocker)
                return odh::Snap::SourcePlacementResult{odh::Snap::SourcePlacementStatus::blocked_object};
            return odh::Snap::SourcePlacementResult{
                blocker->kind==odh::EntityKind::avatar?odh::Snap::SourcePlacementStatus::blocked_avatar:
                    odh::Snap::SourcePlacementStatus::blocked_object,
                blocker->id,blocker->generation};
        }
        if(!world.place_on_floor(actor,point))
            return odh::Snap::SourcePlacementResult{odh::Snap::SourcePlacementStatus::unsupported};
        return odh::Snap::SourcePlacementResult{odh::Snap::SourcePlacementStatus::accepted};
    };
    auto slots=cross_module_snap_table();
    odh::Snap snap{world,2,odh::ObjdBinding{138,0x12345678,129,128},slots,100,{}, {},true,
        std::move(snap_services)};
    odh::VmLimits limits;limits.stack_depth=5;
    auto actor_storage=std::make_unique<odh::Vm>(global_program,odh::ShareVmProgramTag{},world.vm_memory(),limits,
        snap.primitive(),odh::SharedRoutineOwners{{actor_owner,actor_program}},odh::ShareVmOwnerProgramsTag{});
    auto object_storage=std::make_unique<odh::Vm>(global_program,odh::ShareVmProgramTag{},world.vm_memory(),limits,
        odh::VmPrimitive{},odh::SharedRoutineOwners{{object_owner,object_program}},odh::ShareVmOwnerProgramsTag{});
    actor_vm=actor_storage.get();object_vm=object_storage.get();
    require(snap.bind(*actor_vm)&&object_vm->start(odh::routine_key(4200),{2,2,1,object_owner})==odh::VmStatus::running&&
        actor_vm->start(odh::routine_key(4096),{1,2,2,actor_owner})==odh::VmStatus::running,
        "bind live Snap and start separate Main threads for both EP5 owners");
    actor_vm->temps()[6]=17;actor_vm->temp_xl()[1]=29;
    object_vm->temps()[6]=31;object_vm->temp_xl()[1]=45;
    const auto object_before=object_vm->snapshot();
    const auto status=actor_vm->run(1);
    const auto actor_after=actor_vm->snapshot(),object_after=object_vm->snapshot();
    const auto expected_rng_calls=std::size_t{4};
    odh::RandomState expected_random{7};for(std::size_t n=0;n<expected_rng_calls;++n)(void)expected_random.next(65535);
    require(status==odh::VmStatus::returned_true&&candidate_result.status==odh::AvatarPositionValidStatus::success&&
        final_result.status==odh::AvatarPositionValidStatus::success&&placement_directions==std::vector<std::uint8_t>{0,4}&&
        temporary_directions==std::vector<float>{0.0f,static_cast<float>(3.14159265358979323846)},
        "real zero-offset directional Snap runs candidate NORTH and final quantized placement through AvatarPositionValid");
    require(check_order==std::vector<std::pair<std::int16_t,std::int16_t>>{{1,2},{2,1},{1,2},{2,1}}&&
        check_arguments==std::vector<std::array<std::int16_t,4>>{{2,0,0,0},{1,0,0,0},{2,0,0,0},{1,0,0,0}}&&
        caller_checks_false&&blocker_checks_true,
        "Caller EP5 returns false then blocker EP5 true for both placement phases with exact source arguments");
    require(actor_after.status==odh::VmStatus::returned_true&&actor_after.steps==1&&
        actor_after.ticket_sequence==1&&actor_after.run_generation==1&&actor_after.stack.empty()&&
        actor_after.temps[6]==111&&actor_after.temp_xl[1]==1111&&
        object_after.status==object_before.status&&object_after.steps==object_before.steps&&
        object_after.ticket_sequence==object_before.ticket_sequence&&object_after.run_generation==object_before.run_generation&&
        object_after.stack.size()==1&&object_after.stack[0].pc==object_before.stack[0].pc&&
        object_after.temps[6]==222&&object_after.temp_xl[1]==2222,
        "each tested entity publishes its own Temp/TempXL writes without advancing the other Main or spending its ticket");
    require(world.find(1)->attributes[0]==111&&world.find(2)->attributes[0]==222&&
        random_records.size()==expected_rng_calls&&random.seed()==expected_random.seed()&&
        random_records[0].context.caller==1&&random_records[0].context.code_owner==actor_owner&&
        random_records[1].context.caller==2&&random_records[1].context.code_owner==object_owner&&
        random_records[2].context.caller==1&&random_records[3].context.caller==2&&
        world.find(1)->position&&world.find(1)->position->x==48&&world.find(1)->position->y==64&&
        world.find(1)->position->direction==4&&std::abs(world.find(1)->radian_direction-static_cast<float>(3.14159265358979323846))<1e-6f,
        "both live EP5 World/RNG effects persist and successful placement uses target center plus continuous arrival angle");
}
void retired_container_avatar_authority(){
    Fixture f;f.obstacles({2});
    require(f.world.create(5,odh::EntityKind::object,{0,0,1})&&f.world.set_position(5,{32,32,1,6})&&
        f.world.place_in_slot(5,0,1),"retired avatar container fixture");
    const odh::WorldEntityReference parent{5,f.world.find(5)->generation};
    const odh::WorldEntityReference child{1,f.world.find(1)->generation};
    auto program=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
        {odh::routine_key(4097),{0x8002,{{99,254,255,{}}},{},odh::Bhav::Metadata{0,4,0,0}}},
        {odh::routine_key(4098),{0x8002,{{99,254,255,{}}},{},odh::Bhav::Metadata{0,4,0,0}}}});
    auto globals=std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
    odh::Vm actor_thread{odh::RoutineSet{},f.world.vm_memory()},other_thread{odh::RoutineSet{},f.world.vm_memory()};
    unsigned executed=0;odh::LiveEntryPoint5Request saved;
    f.services.entry_point5=[&](const odh::AvatarPositionEntryPoint5Request& source)->std::optional<bool>{
        require(source.retired_container_source&&!source.source_vm&&!source.source_frame&&!source.source_instruction,
            "retired child EP5 has typed lifecycle authority without a fake VM frame");
        odh::LiveEntryPoint5Request request;request.world=&f.world;
        request.tested_entity=source.tested_entity;request.tested_generation=source.tested_generation;
        request.other_entity=source.other;request.other_generation=source.other_generation;
        request.action=source.action;request.ghost=source.ghost;
        request.retired_container_source=source.retired_container_source;
        request.code_owner=900;request.owner_program=program;request.global_program=globals;
        request.live_memory=f.world.vm_memory();request.register_owner=source.tested_entity==1?&actor_thread:&other_thread;
        request.identities_still_match=[&,identity=*source.retired_container_source]{
            return f.world.retired_child_authorized(identity.container,identity.child)&&identity.source_still_matches();};
        request.check_primitive=[&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
            require(instruction.opcode==99&&dispatch.check_mode&&frame.context.caller==frame.context.callee&&
                frame.context.entity_references_bound&&frame.arguments.size()==4,"actual bilateral EP5 executes exact tested context and arguments");
            ++executed;const_cast<odh::Vm*>(dispatch.origin)->temps()[0]=static_cast<std::int16_t>(executed);
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_false,{}};
        };
        saved=request;
        auto wrong=request;wrong.ghost=1;
        require(odh::evaluate_live_entry_point5(wrong).state==odh::LiveEntryPoint5State::unsupported,"lifecycle does not authorize ghostwalk EP5");
        wrong=request;wrong.source_dispatch.check_mode=true;
        require(odh::evaluate_live_entry_point5(wrong).state==odh::LiveEntryPoint5State::unsupported,"lifecycle cannot mix fabricated check dispatch proof");
        const auto result=odh::evaluate_live_entry_point5(request);
        if(result.state==odh::LiveEntryPoint5State::unsupported)return {};
        return result.state==odh::LiveEntryPoint5State::evaluated_true;
    };
    odh::AvatarPositionValid validator(f.world,f.services);
    odh::LiveRetiredContainerPlacementSource expired;
    require(f.world.remove_retaining_contents(parent,[&](const auto& token){
        bool active=true;expired={&f.world,token,child,[&active]{return active;}};
        // Retained token, not this callback alone, must prove this exact child.
        require(f.world.detach_retained_slot(token,0,child),"source Pre detaches exact avatar child");
        auto forged=expired;forged.child={4,f.world.find(4)->generation};
        require(validator.evaluate_lifecycle(forged,{32,32,1,0},0,0).status==odh::AvatarPositionValidStatus::unsupported,
            "parent lease cannot authorize a different live avatar");
        odh::World foreign;forged=expired;forged.world=&foreign;
        require(validator.evaluate_lifecycle(forged,{32,32,1,0},0,0).status==odh::AvatarPositionValidStatus::unsupported,
            "retired placement cannot cross World identity");
        const auto result=validator.evaluate_lifecycle(expired,{32,32,1,0},0,0);
        require(result.status==odh::AvatarPositionValidStatus::blocked&&executed==2&&actor_thread.temps()[0]==1&&other_thread.temps()[0]==2,
            "shared geometry invokes both actual EP5s using their own real Thread register banks");
        active=false;
        return true;
    }),"retired avatar lifecycle scope completes");
    // Replace dangling callback before testing the independently expired lease.
    expired.source_still_matches=[]{return true;};saved.retired_container_source=expired;
    require(validator.evaluate_lifecycle(expired,{32,32,1,0},0,0).status==odh::AvatarPositionValidStatus::unsupported&&
        odh::evaluate_live_entry_point5(saved).state==odh::LiveEntryPoint5State::unsupported&&executed==2,
        "expired opaque lease rejects replay even with an always-true external callback");
}
void original_recovery_avatar_authority(){
    using namespace odh;
    Fixture f(0,0,true);f.obstacles({2});
    std::vector<WorldEntityReference> order;
    for(const auto id:{4,2,1,3})order.push_back({static_cast<std::int16_t>(id),f.world.find(id)->generation});
    auto traversal=f.world.bind_original_load_order(order,8);require(traversal.has_value(),"actual original recovery geometry traversal");
    auto bank=std::make_shared<const VmProgram>(RoutineSet{
        {routine_key(4097),{0x8002,{{99,255,254,{}}},{},Bhav::Metadata{0,4,0,0}}},
        {routine_key(4098),{0x8002,{{99,255,254,{}}},{},Bhav::Metadata{0,4,0,0}}}});
    require(f.vm.install_shared_owner(701,bank)&&f.vm.start(routine_key(4096),{1,1,0})==VmStatus::running,
        "actual undispatched recovery person Thread with installed EP5 bank");
    Vm object_thread(f.vm.shared_program_for_owner(0),ShareVmProgramTag{},f.world.vm_memory(),VmLimits{},VmPrimitive{},
        SharedRoutineOwners{{702,bank}},ShareVmOwnerProgramsTag{});
    const WorldEntityReference person{1,f.world.find(1)->generation};bool scope=true;unsigned effects=0;
    LiveOriginalRecoveryPlacementSource source{&f.world,&*traversal,person,&f.vm,f.vm.run_generation(),[&]{return scope;}};
    source.registration=f.world.begin_original_person_notifications(*traversal);
    auto services=f.services;
    services.entry_point5=[&](const AvatarPositionEntryPoint5Request& request)->std::optional<bool>{
        require(request.original_recovery_source&&request.original_recovery_source->thread==&f.vm&&
            !request.source_vm&&!request.source_frame&&!request.source_instruction&&!request.source_dispatch.origin&&
            !request.route_placement_proof&&!request.route_execution_proof&&!request.object_creation_source&&!request.retired_container_source&&
            request.ghost==0,"geometry forwards only actual original recovery authority, without synthetic opcode or frame");
        LiveEntryPoint5Request live;live.world=&f.world;live.tested_entity=request.tested_entity;live.tested_generation=request.tested_generation;
        live.other_entity=request.other;live.other_generation=request.other_generation;live.action=request.action;live.ghost=request.ghost;
        live.original_recovery_source=request.original_recovery_source;live.global_program=f.vm.shared_program_for_owner(0);
        live.owner_program=bank;live.code_owner=request.tested_entity==1?701:702;
        live.register_owner=request.tested_entity==1?&f.vm:&object_thread;live.live_memory=f.world.vm_memory();
        live.identities_still_match=[&]{return scope&&f.world.is_live_reference(person);};
        live.check_primitive=[&](const auto& frame,const auto& instruction,const auto&,const auto&){
            require(instruction.opcode==99&&f.world.write(frame,3,25,73),"actual bilateral recovery EP5 publishes World effects");
            ++effects;PrimitiveOutcome outcome{PrimitiveDisposition::goto_true,{}};
            outcome.variable_write=VmVariableWrite{8,6,static_cast<std::int16_t>(frame.context.caller*10)};return outcome;
        };
        f.calls.emplace_back(request.tested_entity,request.other);
        const auto result=evaluate_live_entry_point5(live);
        if(result.state==LiveEntryPoint5State::unsupported)return {};
        return result.state==LiveEntryPoint5State::evaluated_true;
    };
    AvatarPositionValid validator(f.world,services);const auto position=f.world.find(1)->position;
    const auto radians=f.world.find(1)->radian_direction;const auto before=f.vm.snapshot();
    const auto result=validator.evaluate_recovery(source,f.candidate,3,0);
    require(result.status==AvatarPositionValidStatus::blocked&&effects==2&&
        f.calls==std::vector<std::pair<std::int16_t,std::int16_t>>{{1,2},{2,1}}&&
        f.vm.temps()[6]==10&&object_thread.temps()[6]==20&&
        f.world.find(1)->object_words.at(25)==73&&f.world.find(2)->object_words.at(25)==73&&
        f.world.find(1)->position==position&&f.world.find(1)->radian_direction==radians&&
        f.vm.run_generation()==before.run_generation&&f.vm.steps()==before.steps&&f.vm.stack().front().pc==0,
        "recovery uses shared geometry/bilateral live EP5, retains each owner's effects and restores temporary direction without moving or dispatching Main");
    auto invalid=source;++invalid.run_generation;
    require(validator.evaluate_recovery(invalid,f.candidate,0,0).status==AvatarPositionValidStatus::unsupported&&effects==2,
        "wrong recovery run rejects before geometry/EP5 effects");
    World foreign;invalid=source;invalid.world=&foreign;
    require(validator.evaluate_recovery(invalid,f.candidate,0,0).status==AvatarPositionValidStatus::unsupported,
        "wrong original recovery World rejects geometry");
    invalid=source;++invalid.person.generation;
    require(validator.evaluate_recovery(invalid,f.candidate,0,0).status==AvatarPositionValidStatus::unsupported,
        "stale original recovery person rejects geometry");
    require(f.world.set_ui_ghost(1,true)&&validator.evaluate_recovery(source,f.candidate,0,0).status==AvatarPositionValidStatus::unsupported&&
        f.world.set_ui_ghost(1,false),"ghost original recovery person cannot borrow geometry authority");
    scope=false;require(validator.evaluate_recovery(source,f.candidate,0,0).status==AvatarPositionValidStatus::unsupported&&effects==2,
        "expired original recovery scope rejects geometry before borrowed owners");
    scope=true;traversal->close();
    require(validator.evaluate_recovery(source,f.candidate,0,0).status==AvatarPositionValidStatus::unsupported,
        "closed original import token cannot authorize recovery geometry");
}
void shared_placement_direct_targets(){
    for(const auto opcode:{std::uint16_t{5},std::uint16_t{16}}){
        Fixture fixture;
        if(opcode==5)require(fixture.world.place_in_slot(1,0,4),"source Drop holds its actual avatar target");
        odh::AvatarPositionValid validator(fixture.world,fixture.services);odh::Vm* active=nullptr;
        odh::AvatarPositionValidStatus accepted=odh::AvatarPositionValidStatus::unsupported,wrong=odh::AvatarPositionValidStatus::success;
        odh::Vm vm({{odh::routine_key(4096),{0x8002,{{opcode,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}}}},fixture.world.vm_memory(),{},
            [&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
                accepted=validator.evaluate(frame,*active,instruction,dispatch,4,fixture.world.find(4)->generation,{64,64,1,0},0,0).status;
                wrong=validator.evaluate(frame,*active,instruction,dispatch,1,fixture.world.find(1)->generation,{64,64,1,0},0,0).status;
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            });active=&vm;vm.start(odh::routine_key(4096),{1,1,4,0});
        require(vm.run(1)==odh::VmStatus::returned_true&&accepted==odh::AvatarPositionValidStatus::success&&
            wrong==odh::AvatarPositionValidStatus::unsupported,"actual placement5/16 authenticates its held/StackObject target without borrowing Caller identity");
    }
}
}
int main(){try{host_constructor_avatar_authority();original_recovery_avatar_authority();retired_container_avatar_authority();creation_avatar_authority();shared_placement_direct_targets();prepared_direct_authorization();route_execution_scope();ranged_relative_placement_scope();route_api_requires_opcode45();overlap_order_and_effects();floor_and_flags();callbacks_and_generations();live_snap_avatar_position_valid_ep5();
    std::cout<<checks<<" avatar PositionValid checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"avatar PositionValid: "<<error.what()<<'\n';return 1;}}
