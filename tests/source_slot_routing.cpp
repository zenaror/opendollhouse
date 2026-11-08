#include "odh/routing.hpp"
#include "odh/source_walk_plan.hpp"
#include "odh/slot_scoring.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
unsigned checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
std::uint32_t bits(float value){std::uint32_t result=0;std::memcpy(&result,&value,sizeof(value));return result;}
constexpr odh::CodeOwner owner=0x80000071U;
odh::SlotTable source_table(float x=0,float y=5.7f,std::int32_t face=-2){
    odh::SlotRecord slot{};slot.type=3;slot.offset_bits={bits(x),bits(y),bits(0)};
    slot.standing=0;slot.sitting=0;slot.ground=0;slot.flags=0;slot.snap_target_slot=-1;
    slot.proximity=std::array<std::int32_t,3>{16,16,16};slot.max_size=0;slot.unknown_i10=0;
    slot.gradient_bits=0x3e400000;slot.height=0;slot.facing=face;slot.resolution=16;
    return {0,10,std::vector<odh::SlotRecord>(9,slot),0};
}
odh::RoutineSet program(std::uint8_t scope=1,std::uint8_t flags=1,std::optional<odh::Instruction> operation={}){
    const auto index=static_cast<std::uint8_t>(scope==0?0:8);
    odh::Bhav main{0x8002,{{45,1,255,{index,0,scope,0,flags,0,0,0}},
        {2,254,255,{0,0,91,0,0,5,8,7}}},{},odh::Bhav::Metadata{0,4,4,0}};
    if(operation)main.instructions.front()=*operation;
    return {{odh::routine_key(4116),main}};
}
struct Fixture {
    odh::World world;odh::RouteGrid grid{64,64};odh::SlotTable table=source_table();
    odh::Routing route{world,grid,table};odh::VmPrimitive extra_primitive;odh::Vm vm;
    bool accept=true;unsigned validations=0,resources=0;
    unsigned turn_starts=0,motion_publishes=0,source_ticks=0,velocity_publications=0,placement_calls=0;
    odh::SourceSparsePlacementStatus initial_block=odh::SourceSparsePlacementStatus::success;
    std::int16_t collision_blocker=9;
    unsigned blocked_attempts=1,cleanup_calls=0,plan_calls=0,room_plans=0;
    bool obstruct_plans=false,turns_ended=true;
    std::function<odh::LiveAttemptWalkFootprint(odh::LiveAttemptWalkIdentity)> live_footprint;
    std::function<odh::LiveAttemptWalkEntryPoint5Result(const odh::LiveAttemptWalkEntryPoint5Request&)> live_ep5;
    unsigned live_ep5_calls=0,locomotion_profile_reads=0;
    odh::WalkMotionProfile current_locomotion_profile{false,88,false,0};
    std::function<std::optional<odh::SourceSparsePlacementOutcome>(const odh::RouteExecutionContext&,
        const odh::EntityPosition&)> live_placement;
    std::vector<odh::RoutePoint> planned_goals;
    std::vector<unsigned> plan_prefixes;
    std::function<std::optional<odh::SourceRoomRoutePlan>(const odh::SourceSparseMotionContext&,
        const odh::EntityPosition&)> room_plan;
    std::int16_t target_id=9;
    Fixture(bool populate=true,std::uint8_t scope=1,std::uint8_t flags=1,
        odh::PreRankedSlotGoalProvider ranked={},std::optional<odh::Instruction> operation={}):
        route(world,grid,table,std::move(ranked)),
        vm({{odh::routine_key(398),{0x8002,{{2,254,255,{1,0,73,0,0,5,8,7}}},{},
            odh::Bhav::Metadata{0,4,4,0}}}},world.vm_memory(),{},[this](const auto& f,const auto& i,const auto& t,const auto& d){
            return extra_primitive&&i.opcode==99?extra_primitive(f,i,t,d):route.primitive()(f,i,t,d);},{{owner,program(scope,flags,operation)}}){
        if(populate)require(world.create(7,odh::EntityKind::avatar,{0,88,3,16})&&
            world.create(9,odh::EntityKind::object)&&world.set_position(7,{4,12,1,0})&&
            world.set_position(9,{20,20,1,0}),"create independent actor and arbitrary target resource");
        route.set_stack_object_slot_table_provider([this](const auto& frame){++resources;
            require(frame.context.stack_object==target_id,"SLOT resolves through exact StackObject context");return &table;});
        route.set_route_goal_validator([this](const auto&,auto goal){++validations;
            return accept&&grid.contains(goal)&&grid.walkable(goal);});
        route.set_room_pair_provider([](const auto&,const auto&){
            return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{2,2}};});
        require(route.bind(vm),"bind source SLOT route graph");
    }
    void start(){const auto started=vm.start(odh::routine_key(4116),{7,7,target_id,owner},{8});
        const auto status=started==odh::VmStatus::running?vm.run(8):started;
        if(status!=odh::VmStatus::waiting)throw std::runtime_error(
            "source route did not wait: "+vm.diagnostic()+"; "+route.diagnostic());
        require(true,"source exact-point route waits");}
    void finish(std::uint64_t first=1,std::uint64_t limit=100){
        for(auto tick=first;route.active()&&tick<limit;++tick)require(route.tick(tick),"advance exact SLOT route");
        const auto status=route.active()?vm.status():vm.run(8);
        if(route.active()||status!=odh::VmStatus::returned_true||vm.temps()[0]!=91){
            const auto* actor=world.find(7);
            throw std::runtime_error("authored true branch resumes only after exact SLOT arrival (active="+
                std::to_string(route.active())+", status="+odh::vm_status_name(status)+", temp0="+std::to_string(vm.temps()[0])+
                ", actor="+(actor&&actor->position?std::to_string(actor->position->x)+","+std::to_string(actor->position->y):"missing")+
                ", plans="+std::to_string(plan_calls)+", prefix="+std::to_string(source_ticks)+", diagnostic="+route.diagnostic()+")");
        }
        require(true,"authored true branch resumes only after exact SLOT arrival");
    }
};
odh::SlotTable circle_table(){
    auto table=source_table();auto& slot=table.records[8];
    slot.offset_bits={0,0,0};slot.standing=1;slot.flags=0x55;
    return table;
}
void install_sparse_services(Fixture& fixture,float& begin_heading){
    require(fixture.world.declare_object_word(7,8,0),"declare source actor collision flags");
    require(fixture.world.declare_object_word(9,8,0),"declare source target collision flags");
    const auto actor_generation=fixture.world.find(7)->generation;
    const auto target_generation=fixture.world.find(9)->generation;
    odh::SourceWalkDomain domain;domain.state=odh::LiveAttemptWalkState::ready;
    domain.request.actor={7,actor_generation};domain.request.room=2;domain.request.level=1;
    domain.request.room_bounds={0,0,160,160};domain.request.architecture_complete=true;
    domain.request.entities_complete=true;domain.request.avatars_to_consider_complete=true;
    domain.request.entities={{7,actor_generation},{9,target_generation}};
    for(const auto& extra:fixture.world.snapshot().entities)if(extra.id!=7&&extra.id!=9)
        domain.request.entities.push_back({extra.id,extra.generation});
    domain.request.architectural_and_bounds={{-32,-32,2592,-16},{-32,2576,2592,2592},
        {-32,-32,-16,2592},{2576,-32,2592,2592}};
    domain.services.inventory_still_matches=[](const auto&){return true;};
    domain.services.footprint=[&fixture](const auto& identity){
        return fixture.live_footprint?fixture.live_footprint(identity):
            odh::LiveAttemptWalkFootprint{odh::LiveAttemptWalkFootprintState::absent,{}};};
    domain.services.entry_point5=[&fixture](const auto& request){
        return fixture.live_ep5?fixture.live_ep5(request):odh::LiveAttemptWalkEntryPoint5Result{};};
    odh::SourceSparseServices services;
    services.capture_avatars_to_consider=[](const auto& context,const auto& inherited)
        ->std::optional<std::vector<odh::LiveAttemptWalkIdentity>>{
        if(!context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch,context.phase))return {};
        return inherited.value_or(std::vector<odh::LiveAttemptWalkIdentity>{});
    };
    services.plan_input=[actor_generation,domain,&fixture](const auto& context,odh::RoutePoint goal,std::optional<float> outgoing,auto inherited)
        ->std::optional<odh::SourceSparsePlanInput>{
        if(!context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch,context.phase))return {};
        ++fixture.plan_calls;fixture.planned_goals.push_back(goal);fixture.plan_prefixes.push_back(fixture.source_ticks);
        odh::SourceSparsePlanInput input;input.profile={false,88,false,0};
        input.avatars_to_consider=inherited.value_or(std::vector<odh::LiveAttemptWalkIdentity>{});
        input.request.actor={7,actor_generation};input.request.destination={goal.x,goal.y};
        input.request.bezier=false;input.request.outgoing_direction=outgoing;
        input.services.context_still_matches=[context]{return context.proof.matches_after_restore(
            context.frame,*context.vm,context.instruction,context.dispatch,context.phase);};
        input.services.room_at=[](const auto&){return std::optional<std::uint16_t>{2};};
        input.services.domain=[domain,&fixture](auto){auto result=domain;
            result.request.entities.clear();
            for(const auto& entity:fixture.world.snapshot().entities)
                result.request.entities.push_back({entity.id,entity.generation});
            if(fixture.obstruct_plans){
                result.request.architectural_and_bounds.insert(result.request.architectural_and_bounds.begin(),{8,-32,10,2592});
                ++result.request.architectural_count;
            }
            return result;};
        return input;
    };
    services.place=[&fixture](const auto& context,const auto& position)->std::optional<odh::SourceSparsePlacementOutcome>{
        ++fixture.placement_calls;
        if(fixture.live_placement)return fixture.live_placement(context,position);
        if(fixture.placement_calls<=fixture.blocked_attempts&&fixture.initial_block!=odh::SourceSparsePlacementStatus::success){
            const auto* blocker=fixture.world.find(fixture.collision_blocker);
            return odh::SourceSparsePlacementOutcome{fixture.initial_block,fixture.collision_blocker,blocker->generation,{}};
        }
        return odh::SourceSparsePlacementOutcome{odh::SourceSparsePlacementStatus::success,{},{},{}};
    };
    services.source_tick=[&fixture](const auto&){++fixture.source_ticks;return true;};
    services.room_route=[&fixture](const auto& context,const auto& target)->std::optional<odh::SourceRoomRoutePlan>{
        ++fixture.room_plans;return fixture.room_plan?fixture.room_plan(context,target):std::nullopt;};
    services.publish_velocity=[&fixture](const auto&,const auto&,auto*){++fixture.velocity_publications;return true;};
    services.avatar_collision_state=[](const auto&,auto)->std::optional<odh::SourceSparseCollisionAvatarState>{
        return odh::SourceSparseCollisionAvatarState{true,0,false};
    };
    services.locomotion_profile=[&fixture](const auto&)->std::optional<odh::WalkMotionProfile>{
        ++fixture.locomotion_profile_reads;return fixture.current_locomotion_profile;};
    services.begin_locomotion=[&](const auto& context,const auto&,auto*){
        begin_heading=fixture.world.find(context.actor)->radian_direction;return true;};
    services.end_locomotion=[&fixture](const auto&,const auto&,auto*){++fixture.cleanup_calls;return true;};
    services.publish_motion=[&fixture](const auto&,const auto&,const auto&,auto*){++fixture.motion_publishes;return true;};
    fixture.route.set_source_sparse_services(std::move(services));
    odh::SourceLookTowardsServices turns;
    turns.profile=[](const auto&){return std::optional<odh::SourceLookTowardsProfile>{{0,false}};};
    turns.start=[&fixture](const auto&,auto,std::uint8_t,bool,float){++fixture.turn_starts;return std::optional<std::uint32_t>{1};};
    turns.ended=[&fixture](const auto&,auto){return std::optional<bool>{fixture.turns_ended};};
    turns.reset=[](const auto&,auto,bool){return true;};
    fixture.route.set_look_towards_services(std::move(turns));
}
void geometry_tests(){
    const auto table=source_table();
    const auto actual=odh::enumerate_source_slot_point_candidate(table,8,{20,20,1,0},0);
    require(actual.position==odh::EntityPosition{20,26,1,0}&&actual.radian_direction==0&&!actual.face_anywhere,
        "Doors-style fractional5.7 offset rounds to6; proximity16 does not become route radius");
    for(const auto value:{0.5f,1.5f,-0.5f,-1.5f}){
        const auto candidate=odh::enumerate_source_slot_point_candidate(source_table(value,value),8,{20,20,1,0},0);
        const auto expected=value==0.5f?20:value==1.5f?22:value==-0.5f?20:18;
        require(candidate.position.x==expected&&candidate.position.y==expected,"source midpoint coordinate rounding is even on both signs");
    }
    constexpr float pi=3.14159265358979323846f;
    const auto rotated=odh::enumerate_source_slot_point_candidate(table,8,{20,20,1,2},pi/2);
    require(rotated.position.x==14&&rotated.position.y==20,"float offset rotates with continuous target direction before rounding");
    auto weights=table;weights.records[8].standing=999;weights.records[8].ground=-17;
    weights.records[8].proximity=std::array<std::int32_t,3>{100,200,150};
    require(odh::enumerate_source_slot_point_candidate(weights,8,{20,20,1,0},0).position==actual.position,
        "standing weight, ground weight and proximity do not reject or displace a single exact-point candidate");
    for(const auto face:{-3,-2,-1,0,1,2,3,4,5,6,7}){
        const auto value=odh::enumerate_source_slot_point_candidate(source_table(3,5.7f,face),8,{20,20,1,3},0);
        require(value.face_anywhere==(face==-3)&&std::isfinite(value.radian_direction),"source exact SLOT facing variants remain explicit");
        if(face==-2)require(std::abs(value.radian_direction-std::atan2(-3.0f,5.7f))<0.00001f,
            "Facing uses unrounded point geometry, preserving a continuous non-notch angle");
        if(face>=0)require(value.facing==((3+face)&7),"source relative facing follows target direction");
    }
    for(unsigned mode=0;mode<4;++mode){
        auto invalid=table;
        if(mode==0)invalid.records[8].offset_bits[0]=bits(std::numeric_limits<float>::quiet_NaN());
        if(mode==1)invalid.records[8].offset_bits[2]=bits(15);
        if(mode==2)invalid.records[8].snap_target_slot=0;
        if(mode==3)invalid.records[8].sitting=1;
        bool rejected=false;try{(void)odh::enumerate_source_slot_point_candidate(invalid,8,{20,20,1,0},0);}
        catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"unsupported nonfinite, cross-floor, snapping or sitting policy remains explicit");
    }
}
void sparse_point_uses_complete_room_plan(){
    Fixture fixture;fixture.table=source_table(3,5.7f,0);
    require(fixture.world.set_position(9,{100,20,1,0})&&fixture.world.set_radian_direction(7,0.0f),
        "place an eastward destination beyond the synthetic grid with a north-facing actor requiring BeginTurn");
    float begin_heading=0;install_sparse_services(fixture,begin_heading);
    fixture.start();
    const auto saved=fixture.route.capture_local_session_route(owner);
    require(saved&&saved->source_sparse&&saved->arrival_radian_direction&&saved->path.back()==odh::RoutePoint{103,26},
        "exact-point Route45 publishes the sparse planner continuation without consulting the old grid");
    require(std::abs(saved->source_sparse->motion.target_direction-1.57079632679)<0.5,
        "sparse plan retains its initial eastward walk heading independently of final SLOT facing");
    require(fixture.route.tick(1)&&std::abs(begin_heading-saved->source_sparse->motion.target_direction)<0.0001&&
        std::abs(begin_heading-*saved->arrival_radian_direction)>1.0&&fixture.turn_starts==1&&fixture.motion_publishes==1,
        "completed BeginTurn starts locomotion and advances the first walking sample in that same tick");
    bool saw_finished_end_turn=false;
    for(std::uint64_t tick=2;fixture.route.active()&&tick<300;++tick){
        const auto turn_starts_before=fixture.turn_starts;
        require(fixture.route.tick(tick),"advance generic sparse exact-point movement");
        const auto checkpoint=fixture.route.capture_local_session_route(owner);
        if(checkpoint&&checkpoint->source_sparse&&checkpoint->source_sparse->lifecycle==2){
            if(!saw_finished_end_turn)require(fixture.turn_starts==turn_starts_before+1,
                "EndTurn clip starts on the arrival tick rather than one tick later");
            saw_finished_end_turn=true;
            require(checkpoint->source_sparse->motion.phase==odh::WalkMotionPhase::finished&&
                checkpoint->source_sparse->motion.move_total_frames==-1&&checkpoint->source_sparse->motion.remaining.empty(),
                "EndTurn checkpoint captures terminal AdvanceWaypoint state");
        }
    }
    require(!fixture.route.active()&&fixture.vm.run(8)==odh::VmStatus::returned_true&&
        fixture.world.find(7)->position->x==103&&fixture.world.find(7)->position->y==26&&saw_finished_end_turn&&
        std::abs(fixture.world.find(7)->radian_direction-*saved->arrival_radian_direction)<0.0001f,
        "sparse exact-point Route45 completes at an authored point outside the synthetic grid");

    Fixture anywhere;anywhere.table=source_table(3,5.7f,-3);
    require(anywhere.world.set_position(9,{100,20,1,0})&&anywhere.world.set_radian_direction(7,1.57079632679f),
        "prepare FaceAnywhere sparse point with a non-north starting direction");
    float anywhere_begin=0;install_sparse_services(anywhere,anywhere_begin);anywhere.start();
    const auto anywhere_saved=anywhere.route.capture_local_session_route(owner);
    require(anywhere_saved&&anywhere_saved->source_sparse&&anywhere_saved->source_sparse->face_anywhere&&
        anywhere.route.tick(1)&&std::abs(anywhere_begin-anywhere_saved->source_sparse->motion.target_direction)<0.0001&&
        std::abs(anywhere_begin-1.57079632679f)>0.01f,
        "FaceAnywhere still applies BeginWalk's initial path heading");
}
void collision_routes(){
    Fixture object;object.initial_block=odh::SourceSparsePlacementStatus::blocked_object;
    float heading=0;install_sparse_services(object,heading);object.start();
    if(!object.route.tick(1))throw std::runtime_error("source object collision: "+object.route.diagnostic());
    const auto replanned=object.route.capture_local_session_route(owner);
    require(replanned&&replanned->source_sparse&&replanned->source_sparse->collision&&
        replanned->source_sparse->collision->retries==4&&
        replanned->source_sparse->collision->phase==odh::SourceRouteCollisionPhase::ready&&object.placement_calls==2&&
        object.source_ticks>=2,"object reroute keeps retry budget and source continuation prefix instead of fake failure");
    Fixture exhausted;exhausted.initial_block=odh::SourceSparsePlacementStatus::blocked_object;exhausted.blocked_attempts=5;
    install_sparse_services(exhausted,heading);exhausted.start();
    require(exhausted.route.tick(1)&&!exhausted.route.active()&&exhausted.vm.run(8)==odh::VmStatus::returned_false&&
        exhausted.world.find(7)->person_data[62]==3&&exhausted.world.find(7)->static_footprint&&exhausted.cleanup_calls==1,
        "retry exhaustion publishes source NoPath, restores root standing state and resumes authored false");
    Fixture moving;moving.initial_block=odh::SourceSparsePlacementStatus::blocked_avatar;moving.collision_blocker=10;
    require(moving.world.create(10,odh::EntityKind::avatar,{0,88,3,16})&&moving.world.set_position(10,{10,11,1,0})&&
        moving.world.declare_object_word(10,8,0),"create a separate moving avatar blocker");
    install_sparse_services(moving,heading);moving.start();
    require(moving.route.tick(1),"moving avatar collision enters source waiting without rerouting immediately");
    const auto waiting=moving.route.capture_local_session_route(owner);
    require(waiting&&waiting->source_sparse&&waiting->source_sparse->collision&&
        waiting->source_sparse->collision->wait_time==29&&waiting->frame.wait_time==29&&
        waiting->source_sparse->motion.phase==odh::WalkMotionPhase::collision_required&&moving.velocity_publications==1,
        "source CONTINUE consumes the first wait tick immediately and preserves the rejected motion");
    Fixture resumed(false);resumed.table=moving.table;
    require(resumed.world.restore_snapshot(moving.world.snapshot())&&resumed.vm.restore_snapshot(moving.vm.snapshot()),
        "restore the waiting avatar World and VM into a fresh graph");
    float resumed_heading=0;install_sparse_services(resumed,resumed_heading);
    require(resumed.route.restore_local_session_route(*waiting,owner),"restore source waiting policy and rejected sample without callbacks");
    for(std::uint64_t tick=2;tick<=32;++tick){
        require(moving.route.tick(tick)&&resumed.route.tick(tick),"advance continuous and restored source collision waits");
        require(moving.world.find(7)->position==resumed.world.find(7)->position&&
            moving.world.find(7)->radian_direction==resumed.world.find(7)->radian_direction,
            "restored waiting resumes the same movement sample and heading");
    }
    require(moving.placement_calls>1&&resumed.placement_calls>0,
        "both waiting executions resume placement after their final wait tick");
}
void execution_tests(){
    for(const auto scope:{std::uint8_t{0},std::uint8_t{1},std::uint8_t{2}}){
        Fixture live(true,scope);live.table=source_table(3,5.7f);live.start();
        require(live.validations==1&&live.route.tick(1),"source candidate passes host validation before real movement");
        const auto saved=live.route.capture_local_session_route(owner);
        require(saved&&saved->entry_flags==0&&saved->arrival_radian_direction&&!saved->call_failure_trees&&
            std::abs(*saved->arrival_radian_direction-std::atan2(-3.0f,5.7f))<0.00001f,
            "snapshot preserves exact continuous arrival and source NoFailureTrees policy");
        Fixture resumed(false,scope);resumed.table=live.table;
        require(resumed.world.restore_snapshot(live.world.snapshot())&&resumed.vm.restore_snapshot(live.vm.snapshot())&&
            resumed.route.restore_local_session_route(*saved,owner),"fresh graph re-evaluates selected SLOT and restores the point route");
        Fixture invalid(false,scope);invalid.table=live.table;
        require(invalid.world.restore_snapshot(live.world.snapshot())&&invalid.vm.restore_snapshot(live.vm.snapshot()),"prepare failed-restore graph");
        auto wrong=*saved;*wrong.arrival_radian_direction+=0.25f;
        require(!invalid.route.restore_local_session_route(wrong,owner),"tampered continuous arrival cannot bypass source SLOT reconstruction");
        invalid.table.records[8].offset_bits[1]=bits(6.7f);
        require(!invalid.route.restore_local_session_route(*saved,owner),"changed exact SLOT resource cannot restore a stale goal");
        live.finish(2);resumed.finish(2);
        const auto* actor=live.world.find(7);const auto* copy=resumed.world.find(7);
        require(actor->position==copy->position&&actor->radian_direction==copy->radian_direction&&
            actor->position->x==23&&actor->position->y==26&&actor->person_data[37]==0&&
            actor->radian_direction==*saved->arrival_radian_direction&&live.vm.steps()==resumed.vm.steps(),
            "continuous and restored routes preserve fractional goal, exact final radians, entryflags0 and VM work");
    }
    for(const auto flags:{std::uint8_t{0},std::uint8_t{1}}){
        Fixture blocked(true,1,flags);blocked.accept=false;
        require(blocked.vm.start(odh::routine_key(4116),{7,7,9,owner},{8})==odh::VmStatus::running,"start blocked source SLOT request");
        const auto status=blocked.vm.run(8);
        if(flags==1)require(status==odh::VmStatus::returned_false&&!blocked.route.active(),
            "genuine host-rejected exact goal follows authored false when NoFailureTrees is set");
        else require(status==odh::VmStatus::waiting&&blocked.route.tick(1)&&
            blocked.vm.stack().back().key.id==398,"source point SLOT failure invokes global398 when enabled");
        Fixture portal(true,1,flags);
        portal.route.set_room_pair_provider([](const auto&,const auto&){
            return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,2}};});
        portal.route.set_room_portal_step_provider([](auto,const auto&,const auto&,const auto&,const auto&,auto){
            return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::unavailable,{},"no source room path"};});
        portal.start();require(portal.route.waiting_for_room_portal_step()&&portal.route.tick(1),
            "point SLOT room traversal is independent of failure-tree reporting policy");
        if(flags==0)require(portal.vm.stack().back().key.id==398,"deferred point SLOT failure retains authored global398 policy");
        else require(!portal.route.active()&&portal.vm.status()==odh::VmStatus::returned_false,
            "deferred NoFailureTrees point SLOT failure returns authored false");
    }
    Fixture zero;zero.start();require(zero.route.tick(1),"advance zero-angle source point route");
    const auto zero_saved=zero.route.capture_local_session_route(owner);
    require(zero_saved&&zero_saved->arrival_radian_direction&&*zero_saved->arrival_radian_direction==0,
        "source axis offset carries its exact zero-angle value");
    Fixture sign(false);
    require(sign.world.restore_snapshot(zero.world.snapshot())&&sign.vm.restore_snapshot(zero.vm.snapshot()),
        "prepare source signed-zero verification graph");
    auto negative_zero=*zero_saved;negative_zero.arrival_radian_direction=-0.0f;
    require(!sign.route.restore_local_session_route(negative_zero,owner),"source arrival verification compares exact IEEE bits, including signed zero");
    Fixture no_validator;
    no_validator.route.set_route_goal_validator({});
    require(no_validator.vm.start(odh::routine_key(4116),{7,7,9,owner},{8})==odh::VmStatus::running&&
        no_validator.vm.run(8)==odh::VmStatus::fault&&!no_validator.route.active(),
        "missing placement/wall host policy stays unsupported rather than faking a game false or success");
}
void self_anchor_tests(){
    for(const bool point:{false,true}){
        Fixture live(true,2);live.target_id=7;
        live.table=point?source_table(3,5.7f):circle_table();
        require(live.world.set_position(7,{20,20,1,0})&&live.world.set_radian_direction(7,0.31f),
            "prepare continuous self-target request transform");
        const auto anchor=*live.world.find(7)->position;
        const auto angle=live.world.find(7)->radian_direction;
        live.start();require(live.route.tick(1),"self-target route makes real progress before capture");
        std::string why;const auto saved=live.route.capture_local_session_route(owner,&why);
        require(saved&&saved->actor==saved->target&&saved->target_anchor_radian_direction&&
            bits(*saved->target_anchor_radian_direction)==bits(angle)&&saved->target_position==anchor&&
            !(saved->expected==anchor)&&saved->arrival_radian_direction.has_value()==point&&
            saved->path.front()==odh::RoutePoint{anchor.x,anchor.y},
            "self-target continuation freezes original transform separately from actor progress");
        Fixture resumed(false,2);resumed.target_id=7;resumed.table=live.table;
        require(resumed.world.restore_snapshot(live.world.snapshot())&&resumed.vm.restore_snapshot(live.vm.snapshot()),
            "restore independent self-target World and VM");
        resumed.route.set_room_pair_provider([&](const auto& actor,const auto& target){
            require(actor==saved->expected&&target==anchor,"room proof compares live actor with frozen self anchor");
            return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{2,2}};});
        require(resumed.route.restore_local_session_route(*saved,owner),
            "fresh self-target route restores after target actor has moved away from anchor");
        Fixture invalid(false,2);invalid.target_id=7;invalid.table=live.table;
        require(invalid.world.restore_snapshot(live.world.snapshot())&&invalid.vm.restore_snapshot(live.vm.snapshot()),
            "prepare self-anchor tamper graph");
        auto wrong=*saved;wrong.target_anchor_radian_direction.reset();
        require(!invalid.route.restore_local_session_route(wrong,owner),"self route requires its frozen angle marker");
        wrong=*saved;wrong.target_anchor_radian_direction=std::numeric_limits<float>::infinity();
        require(!invalid.route.restore_local_session_route(wrong,owner),"self anchor cannot contain a nonfinite angle");
        wrong=*saved;++wrong.target_position.x;
        require(!invalid.route.restore_local_session_route(wrong,owner),"self anchor must equal the original path start");
        wrong=*saved;wrong.target_position.direction=3;
        require(!invalid.route.restore_local_session_route(wrong,owner),"self anchor direction agrees with frozen angle");
        wrong=*saved;++wrong.target_generation;
        require(!invalid.route.restore_local_session_route(wrong,owner),"self target shares the exact actor generation");
        if(point){
            wrong=*saved;*wrong.target_anchor_radian_direction+=0.1f;
            require(!invalid.route.restore_local_session_route(wrong,owner),
                "point SLOT re-evaluates geometry and facing using the frozen angle");
        }
        live.finish(2);resumed.finish(2);
        const auto* actor=live.world.find(7);const auto* copy=resumed.world.find(7);
        require(actor->position==copy->position&&bits(actor->radian_direction)==bits(copy->radian_direction)&&
            actor->person_data[37]==saved->entry_flags&&copy->person_data[37]==saved->entry_flags&&
            live.vm.steps()==resumed.vm.steps(),"self circle and point restore preserve final position, angle, entry flags and VM work");
        if(point)require(bits(actor->radian_direction)==bits(*saved->arrival_radian_direction),
            "self exact point arrives at its original continuous facing");
        require(!live.route.capture_local_session_route(owner),"completed self walk retains no frozen anchor continuation");
    }
    Fixture other;other.start();require(other.route.tick(1),"prepare nonself anchor rejection");
    const auto saved=other.route.capture_local_session_route(owner);require(saved.has_value(),"capture ordinary target route");
    Fixture copy(false);require(copy.world.restore_snapshot(other.world.snapshot())&&copy.vm.restore_snapshot(other.vm.snapshot()),
        "prepare nonself tamper graph");
    auto wrong=*saved;wrong.target_anchor_radian_direction=3.14159265358979323846f;
    require(!copy.route.restore_local_session_route(wrong,owner),
        "nonself source-point anchor cannot contradict the frozen target direction");
    require(saved->target_anchor_radian_direction==0.0f&&copy.route.restore_local_session_route(*saved,owner),
        "ordinary source-point route restores its actual captured target angle after rejecting tampering");
}
odh::SlotTable full_circle_table(){
    auto result=source_table(0,0);auto& slot=result.records[8];
    slot.standing=1;slot.flags=255;slot.max_size=100;slot.gradient_bits=0;
    return result;
}
void source_circle_routes(){
    for(const std::uint32_t flags:{255U,0x8055U}){
    Fixture live(true,2);live.table=full_circle_table();live.table.records[8].flags=flags;odh::RandomState random(17);
    unsigned providers=0;
    const auto bind_provider=[&](Fixture& f,odh::RandomState& rng){
        f.route.set_continuous_ranged_slot_goal_provider([&f,&rng,&providers](const auto& table,auto index,
            const auto& actor,const auto& target,auto radians){
            ++providers;return odh::rank_reference_range_slot(table,index,actor,target,rng,false,
                [](auto){return true;},{},{},{},radians,[](auto){return odh::ReferenceSlotVerification::eligible;});
        });
    };
    bind_provider(live,random);live.start();
    require(live.route.tick(1),"source circle begins a genuine ranked walk");
    const auto saved=live.route.capture_local_session_route(owner);
    require(saved&&saved->source_circle&&saved->arrival_radian_direction&&saved->source_circle->slot_index==8&&
        saved->source_circle->slot.flags==flags&&saved->source_circle->fallback_goals.size()==(flags==255?7U:6U)&&
        bits(saved->source_circle->selected.radian_direction)==bits(*saved->arrival_radian_direction),
        "capture full source SLOT row, continuous arrival, frozen transform and remaining ranked options");
    const auto goal=saved->source_circle->selected.position;
    require(live.route.intersects_destination({goal.x+6,goal.y-6})==true&&
        live.route.intersects_destination({goal.x+7,goal.y})==false,
        "destination penalty uses source inclusive six-unit square without floor filtering");
    require(live.world.set_position(9,{45,45,1,2}),"target moves after choices are frozen");
    Fixture resumed(false,2);resumed.table=live.table;odh::RandomState resumed_random(random.seed());
    bind_provider(resumed,resumed_random);
    require(resumed.world.restore_snapshot(live.world.snapshot())&&resumed.vm.restore_snapshot(live.vm.snapshot())&&
        resumed.route.restore_local_session_route(*saved,owner),
        "fresh route restores frozen circle while nonself target has moved");
    Fixture invalid(false,2);invalid.table=live.table;
    require(invalid.world.restore_snapshot(live.world.snapshot())&&invalid.vm.restore_snapshot(live.vm.snapshot()),
        "prepare circle tamper graph");
    auto wrong=*saved;++*wrong.source_circle->slot.max_size;
    require(!invalid.route.restore_local_session_route(wrong,owner),"even source-ignored SLOT row metadata must match exact resource");
    wrong=*saved;wrong.source_circle->selected.radian_direction+=0.01f;
    require(!invalid.route.restore_local_session_route(wrong,owner),"circle selected continuous facing cannot be forged");
    wrong=*saved;wrong.source_circle->fallback_goals[0]=wrong.source_circle->selected;
    require(!invalid.route.restore_local_session_route(wrong,owner),"fallback cannot duplicate selected goal");
    const auto blocked=saved->path[saved->next];
    live.grid.block(blocked);resumed.grid.block(blocked);
    const auto seed=random.seed(),copy_seed=resumed_random.seed();
    require(live.route.tick(2)&&resumed.route.tick(2),"dynamic blockage tries retained source options");
    const auto retry=live.route.capture_local_session_route(owner);
    require(retry&&retry->source_circle&&!(retry->path.back()==saved->path.back())&&
        retry->source_circle->fallback_goals.size()<saved->source_circle->fallback_goals.size()&&
        providers==1&&random.seed()==seed&&resumed_random.seed()==copy_seed,
        "soft failure consumes only remaining ranked choices without provider rerun or RNG");
    live.finish(3);resumed.finish(3);
    require(live.world.find(7)->position==resumed.world.find(7)->position&&
        bits(live.world.find(7)->radian_direction)==bits(resumed.world.find(7)->radian_direction)&&
        bits(live.world.find(7)->radian_direction)==bits(*retry->arrival_radian_direction)&&
        !live.route.intersects_destination(goal).value(),
        "restored circle fallback reaches same frozen destination and exact source arrival radians");
    Fixture missing(true,2);missing.table=full_circle_table();const auto seed_before=random.seed();
    missing.route.set_ranged_slot_goal_provider([&](const auto&,auto,const auto&,const auto&){
        random.next(1);return std::vector<odh::PreRankedSlotGoal>{};});
    require(missing.vm.start(odh::routine_key(4116),{7,7,9,owner},{8})==odh::VmStatus::running&&
        missing.vm.run(8)==odh::VmStatus::fault&&random.seed()==seed_before,
        "full circle rejects missing continuous geometry capability before provider RNG");
    }
    Fixture legacy(true,2);legacy.table=circle_table();legacy.start();legacy.route.tick(1);
    const auto legacy_saved=legacy.route.capture_local_session_route(owner);
    require(legacy_saved&&!legacy_saved->source_circle,
        "old exact0x55 consumer without source providers retains its explicit legacy circle continuation");
    Fixture unsupported(true,2);unsupported.table=full_circle_table();unsupported.table.records[8].flags=0x8055;
    require(unsupported.vm.start(odh::routine_key(4116),{7,7,9,owner},{8})==odh::VmStatus::running&&
        unsupported.vm.run(8)==odh::VmStatus::fault&&!unsupported.route.active(),
        "new masked circles never fall back to unscored legacy geometry when source policy is absent");
}



void deferred_relative_facing(){
    for(const auto flags:{0U,2U,5U}){
        const odh::Instruction op{27,1,255,{23,1,254,255,45,2,static_cast<std::uint8_t>(flags),0}};
        Fixture live(true,1,1,{},op);float heading=0;install_sparse_services(live,heading);bool same_room=false;
        live.route.set_room_pair_provider([&](const auto&,const auto&){return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,static_cast<std::uint16_t>(same_room?1:2)}};});
        live.route.set_room_portal_step_provider([&](auto,const auto&,const auto&,const auto&,const auto&,auto){same_room=true;return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::destination_reached,{},{}};});
        live.route.set_route_grid_refresh([](const auto&){return true;});
        live.start();std::string why;const auto saved=live.route.capture_session_route_stack(owner,&why);
        if(!saved)throw std::runtime_error("capture deferred relative facing: "+why);
        require(saved->jobs.front().deferred_room_route&&saved->jobs.front().face_anywhere&&
            saved->jobs.front().call_failure_trees==((flags&2U)==0),"deferred exact relative retains source direction and failure-tree operands before local plan exists");
        require(live.route.tick(1),"private exact-relative replay enters selected room");
        const auto local=live.route.capture_session_route_stack(owner,&why);
        require(local&&local->jobs.front().source_sparse&&local->jobs.front().face_anywhere,"relative replay preserves FaceAnywhere when replacing deferred frame with source motion");
    }
}

void parked_collision_failure_tree(){
    Fixture live(true,1,0);live.initial_block=odh::SourceSparsePlacementStatus::blocked_object;live.blocked_attempts=5;
    float heading=0;install_sparse_services(live,heading);live.start();require(live.route.tick(1)&&live.route.external_frame_active(),"source retry exhaustion parks exact failure tree398");
    std::string why;const auto stack=live.route.capture_session_route_stack(owner,&why);
    if(!stack)throw std::runtime_error("capture rejected CurrentPath failure child: "+why);
    require(stack->jobs.size()==1&&odh::is_source_failure_tree_continuation(stack->jobs.front())&&
        stack->jobs.front().source_sparse->motion.phase==odh::WalkMotionPhase::collision_required&&
        stack->jobs.front().source_sparse->collision->phase==odh::SourceRouteCollisionPhase::soft_failure,"failed route retains rejected CurrentPath only behind authenticated source failure call");
    Fixture copy(false,1,0);require(copy.world.restore_snapshot(live.world.snapshot())&&copy.vm.restore_snapshot(live.vm.snapshot()),"rebuild failure-child World and VM");
    float copied_heading=0;install_sparse_services(copy,copied_heading);
    auto wrong=*stack;wrong.jobs.front().failure_tree_invoked=false;
    require(!copy.route.restore_session_route_stack(wrong,owner),"generic parked child cannot borrow failure-tree rejected-motion continuation");
    wrong=*stack;wrong.jobs.front().external_frame_call->arguments[0]=8;
    require(!copy.route.restore_session_route_stack(wrong,owner),"failure-tree continuation requires exact source code/blocker args");
    require(copy.route.restore_session_route_stack(*stack,owner),"valid rejected-motion failure child restores without converting policy to waiting");
    require(live.vm.run(8)==odh::VmStatus::yielded&&copy.vm.run(8)==odh::VmStatus::yielded&&live.route.tick(2)&&copy.route.tick(2)&&
        !live.route.active()&&!copy.route.active()&&live.vm.run(8)==odh::VmStatus::returned_false&&copy.vm.run(8)==odh::VmStatus::returned_false,
        "completed failure tree resumes authored false and discards retained path in both graphs");
}

void ordinary_go_here_sparse(){
    const odh::Instruction operation{27,1,255,{23,1,254,255,45,2,5,0}};
    require(odh::is_source_ordinary_go_here_instruction(operation),"ordinary source27 ignores original two counter values structurally");
    Fixture live(true,1,1,{},operation);float heading=0;install_sparse_services(live,heading);
    require(live.world.create(8,odh::EntityKind::avatar,{0,88,0,16})&&live.world.set_position(8,{35,35,1,0})&&live.world.declare_object_word(8,8,0),"create independent moving avatar for CanShoo consideration");
    live.start();require(live.route.tick(1),"ordinary GoHere begins real source movement");
    const auto saved=live.route.capture_local_session_route(owner);
    require(saved&&saved->source_sparse&&saved->face_anywhere&&saved->call_failure_trees&&!saved->source_circle&&!saved->source_range&&
        saved->path.back()==odh::RoutePoint{20,20}&&saved->source_sparse->motion.current&&live.motion_publishes>0,"ordinary GoHere uses smooth Motion and exact source goal rather than grid walk");
    require(live.route.prepare_source_shoo({8,live.world.find(8)->generation}).has_value(),"ordinary GoHere source route is eligible for prepared direct shoo");
    Fixture resumed(false,1,1,{},operation);require(resumed.world.restore_snapshot(live.world.snapshot())&&resumed.vm.restore_snapshot(live.vm.snapshot()),"rebuild independent ordinary GoHere graph");
    float resumed_heading=0;install_sparse_services(resumed,resumed_heading);
    require(resumed.route.restore_local_session_route(*saved,owner),"ordinary counter-bearing source GoHere restores its real motion");
    live.finish(2);resumed.finish(2);require(live.world.find(7)->position==resumed.world.find(7)->position&&live.vm.steps()==resumed.vm.steps(),"ordinary GoHere save continuation reaches identical position and VM work");
    const odh::Instruction historical{27,1,255,{10,0,254,255,10,0,5,0}};
    Fixture ghost(true,1,1,{},historical);require(ghost.world.set_ui_ghost(9,true),"historical marker remains explicitly UI-only");float ghost_heading=0;install_sparse_services(ghost,ghost_heading);ghost.start();
    const auto legacy=ghost.route.capture_local_session_route(owner);require(legacy&&!legacy->source_sparse&&ghost.plan_calls==0,"historical UI ghost stays on strict legacy path even with source services");
    // Use the original waiting VM, not the completed live continuation.
    Fixture waiting(true,1,1,{},operation);float waiting_heading=0;install_sparse_services(waiting,waiting_heading);waiting.start();auto waiting_saved=waiting.route.capture_local_session_route(owner);
    Fixture ghost_restore(false,1,1,{},operation);auto changed=waiting.world.snapshot();for(auto& e:changed.entities)if(e.id==9)e.ui_ghost=true;
    require(ghost_restore.world.restore_snapshot(changed)&&ghost_restore.vm.restore_snapshot(waiting.vm.snapshot()),"restore pending source27 with ghost-tampered target");
    float rejected_heading=0;install_sparse_services(ghost_restore,rejected_heading);
    require(waiting_saved&&!ghost_restore.route.restore_local_session_route(*waiting_saved,owner),"sparse flags5 cannot be restored against a historical UI ghost");
    auto malformed=operation;malformed.operands[6]=7;
    const auto ignored_flags=odh::decode_source_goto_relative_position(malformed);
    require(odh::is_source_ordinary_go_here_instruction(malformed)&&ignored_flags&&ignored_flags->no_failure_trees,
        "ignored flags retain ordinary geometry while the source NoFailureTrees bit remains effective");
    malformed=operation;malformed.operands[3]=0;require(!odh::is_source_ordinary_go_here_instruction(malformed),"changed facing is not the ordinary GoHere command");
}

void authenticated_circle_routes(){
    for(const auto facing:{-2,-3}){
        Fixture live;live.table=full_circle_table();live.table.records[8].facing=facing;
        float heading=0;install_sparse_services(live,heading);odh::RandomState random(71);unsigned providers=0,verified=0,legacy=0;
        require(live.world.declare_object_word(9,0,0),"circle verifier has live mutable source object data");
        std::optional<odh::RoutePlacementContext> captured;
        live.route.set_continuous_ranged_slot_goal_provider([&](const auto&,auto,const auto&,const auto&,auto)->std::vector<odh::PreRankedSlotGoal>{
            ++legacy;throw std::runtime_error("authenticated circle must not use read-only legacy verification");});
        live.route.set_authenticated_range_slot_goal_provider([&](const auto& table,auto index,const auto& actor,const auto& target,float radians,const auto& context){
            ++providers;captured=context;
            require(context.proof.matches(context.frame,live.vm,context.instruction,context.dispatch),"source circle receives actual active45 placement capability");
            auto wrong=context.dispatch;++wrong.ticket;
            require(!context.proof.matches(context.frame,live.vm,context.instruction,wrong),"circle capability rejects a forged dispatch ticket");
            auto goals=odh::rank_reference_range_slot(table,index,actor,target,random,false,[](auto){return true;},{},{},{},radians,[&](auto){
                ++verified;const auto old=live.world.find(7)->radian_direction;
                require(live.world.set_radian_direction(7,0)&&context.proof.matches(context.frame,live.vm,context.instruction,context.dispatch),"circle live placement permits its temporary NORTH heading");
                odh::VmFrame frame;frame.context.caller=9;
                require(live.world.write(frame,3,0,static_cast<std::int16_t>(verified))&&live.world.set_radian_direction(7,old),"effectful circle verifier preserves data effects and restores heading");
                return odh::ReferenceSlotVerification::eligible;});
            return odh::AuthenticatedRangeSlotGoals{std::move(goals),0,0,0};
        });
        live.start();require(providers==1&&verified==8&&legacy==0&&live.world.find(9)->object_words.at(0)==8&&captured&&
            !captured->proof.matches(captured->frame,live.vm,captured->instruction,captured->dispatch),"circle ranks once through live capability and expires proof after callback");
        const auto saved=live.route.capture_local_session_route(owner);require(saved&&saved->source_circle&&!saved->source_range&&saved->source_sparse&&
            saved->source_circle->fallback_goals.size()==7&&saved->face_anywhere==(facing==-3),"authenticated circle preserves existing circle manifest and complete ordered tail");
        Fixture copy(false);copy.table=live.table;require(copy.world.restore_snapshot(live.world.snapshot())&&copy.vm.restore_snapshot(live.vm.snapshot()),"restore independent circle participant and VM graph");
        float copy_heading=0;install_sparse_services(copy,copy_heading);
        require(copy.route.restore_local_session_route(*saved,owner)&&providers==1&&verified==8,"fresh graph restores circle without replaying verification effects or ranking RNG");
        const auto seed=random.seed();live.finish();copy.finish();
        require(live.world.find(7)->position==copy.world.find(7)->position&&bits(live.world.find(7)->radian_direction)==bits(copy.world.find(7)->radian_direction)&&
            providers==1&&verified==8&&random.seed()==seed,"source circle movement uses retained selection and does not rerank on restore");
    }
    for(bool unknown:{false,true}){
        Fixture replay;replay.table=circle_table();bool same_room=false;unsigned providers=0;std::optional<odh::RoutePlacementContext> captured;
        require(replay.world.declare_object_word(9,0,0),"replay verifier effects have known live object word");
        replay.route.set_room_pair_provider([&](const auto&,const auto&){return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,static_cast<std::uint16_t>(same_room?1:2)}};});
        replay.route.set_room_portal_step_provider([&](auto,const auto&,const auto&,const auto&,const auto&,auto){same_room=true;return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::destination_reached,{},{}};});
        replay.route.set_authenticated_range_slot_goal_provider([&](const auto& table,auto index,const auto&,const auto& target,float radians,const auto& context){
            ++providers;captured=context;require(!replay.vm.dispatch_active()&&context.proof.matches(context.frame,replay.vm,context.instruction,context.dispatch),"private circle replay authorizes exact waiting45 only during provider");
            odh::VmFrame frame;frame.context.caller=9;require(replay.world.write(frame,3,0,17),"circle replay retains effect before unknown capability");
            if(unknown)throw std::runtime_error("unknown live placement");
            const auto geometry=odh::enumerate_source_slot_circle_candidates(table,index,target,radians);const auto& first=geometry.front();
            odh::PreRankedSlotGoal goal{first.position,first.entry_flags,first.facing};goal.radian_direction=first.radian_direction;
            return odh::AuthenticatedRangeSlotGoals{{goal},0,0,0};});
        replay.start();const auto ticket=replay.vm.pending()->ticket;require(providers==0&&replay.route.waiting_for_room_portal_step(),"circle local verification waits for actual room transition");
        const bool advanced=replay.route.tick(1);
        require(providers==1&&captured&&!captured->proof.matches(captured->frame,replay.vm,captured->instruction,captured->dispatch)&&replay.world.find(9)->object_words.at(0)==17,
            "circle replay proof expires on successful or throwing return while source effects persist");
        if(unknown)require(!advanced&&replay.route.active()&&replay.vm.status()==odh::VmStatus::waiting&&replay.vm.pending()->ticket==ticket,"unknown circle replay retains original authored wait instead of dispatching false failure");
        else {require(advanced&&replay.route.capture_local_session_route(owner)->source_circle.has_value(),"private replay publishes source circle continuation");replay.finish(2);}
    }
}

odh::SlotTable range_table(){
    auto table=source_table();auto& slot=table.records[8];
    slot.offset_bits={0,0,0};slot.standing=1;slot.sitting=0;slot.ground=0;slot.flags=0xff;
    slot.proximity=std::array<std::int32_t,3>{16,32,16};return table;
}
odh::PreRankedSlotGoal range_goal(const odh::SlotTable& table,const odh::EntityPosition& target,float angle,odh::RoutePoint point){
    const auto geometry=odh::enumerate_source_slot_range_candidates(table,8,target,angle);
    for(const auto& goal:geometry)if(goal.position==point){
        odh::PreRankedSlotGoal result{goal.position,goal.entry_flags,goal.facing};result.radian_direction=goal.radian_direction;return result;
    }
    throw std::runtime_error("test range goal is outside source geometry");
}
// A source CurRoute is selected before AttemptWalk. NoPath retains the
// last attempted destination while failure398 runs; it is not a motion path.
void failed_attempt_retains_curroute(){
    for(unsigned mode=0;mode<5;++mode){
        const auto operation=mode==1?std::optional<odh::Instruction>{{27,1,255,{5,0,254,254,0,0,0,0}}}:std::nullopt;
        Fixture live(true,1,0,{},operation);live.grid=odh::RouteGrid{160,160};
        require(live.world.set_position(9,{64,64,1,0}),"failed route target across complete source barrier");
        if(mode==2)live.table=full_circle_table();
        if(mode==3)live.table=range_table();
        if(mode==4){live.table=range_table();live.table.records[8].proximity=std::array<std::int32_t,3>{16,64,32};}
        live.obstruct_plans=true;float heading=0;install_sparse_services(live,heading);
        odh::RandomState random(81);
        if(mode==2||mode==3)live.route.set_authenticated_range_slot_goal_provider(
            [&](const auto& table,auto index,const auto& actor,const auto& target,float radians,const auto&){
                // Circle and standing-range have separate source-authenticated
                // scorers. The circle helper deliberately rejects general range.
                if(mode==3)return odh::AuthenticatedRangeSlotGoals{odh::rank_source_standing_range_slot(
                    table,index,actor,target,radians,random,[](auto){return odh::SourceSlotRoomResult::same_room;},
                    [](auto){return odh::ReferenceSlotVerification::eligible;}),0,0,0};
                return odh::AuthenticatedRangeSlotGoals{odh::rank_reference_range_slot(table,index,actor,target,random,
                    false,[](auto){return true;},{},{},{},radians,[](auto){return odh::ReferenceSlotVerification::eligible;}),0,0,0};});
        if(mode==4)live.route.set_authenticated_slot_goal_provider([](const auto&,auto,const auto&,const auto&,float,const auto&){
            odh::AuthenticatedRangeSlotGoals result;result.failure_code=0;
            result.semantic_goals={{{64,80},1,4,3.14159274f,false,{},1},{{80,80},1,4,3.14159274f,false,{},1}};
            result.goals={{{64,80},1,4,{},{},3.14159274f},{{80,80},1,4,{},{},3.14159274f}};return result;});
        live.start();std::string why;
        require(!live.planned_goals.empty(),"NoPath comes from actual source planner attempts");
        const auto last=live.planned_goals.back();
        const auto saved=live.route.capture_session_route_stack(owner,&why);
        if(!saved)throw std::runtime_error("capture failed selected source destination mode="+std::to_string(mode)+": "+why);
        const auto& job=saved->jobs.back();
        require(job.failure_pending&&job.source_choices&&job.source_choices->selected.position==last&&
            job.source_choices->remaining.empty()&&!job.source_sparse&&job.path==std::vector<odh::RoutePoint>{{4,12}}&&
            live.motion_publishes==0&&live.placement_calls==0,"failed attempt keeps exact last CurRoute without fabricated motion");
        if(mode==4)require(live.planned_goals.size()==2&&last==odh::RoutePoint{80,80}&&
            live.route.intersects_destination({64,80})==std::optional<bool>{false},"exhausted choices retain last attempt, not first candidate");
        for(const auto dx:{-7,-6,-5,0,5,6,7})for(const auto dy:{-7,-6,-5,0,5,6,7})
            require(live.route.intersects_destination({last.x+dx,last.y+dy})==std::optional<bool>{std::abs(dx)<=6&&std::abs(dy)<=6},
                "failed CurRoute uses exact inclusive source rectangle, including diagonals");
        require(!live.route.capture_local_session_route(owner,&why),"failure continuation cannot flatten into local-only snapshot");
        Fixture restored(false,1,0,{},operation);restored.table=live.table;restored.grid=odh::RouteGrid{160,160};
        require(restored.world.restore_snapshot(live.world.snapshot())&&restored.vm.restore_snapshot(live.vm.snapshot()),"failed selection restores exact World and VM");
        float restored_heading=0;install_sparse_services(restored,restored_heading);
        auto malformed=*saved;malformed.jobs.back().source_choices->remaining.push_back(job.source_choices->selected);
        require(!restored.route.restore_session_route_stack(malformed,owner,&why),"failed choice cannot retain unattempted candidate roster");
        malformed=*saved;malformed.jobs.back().source_choices.reset();malformed.jobs.back().path.clear();
        require(!restored.route.restore_session_route_stack(malformed,owner,&why),"old empty failed route without selected manifest remains inadmissible, never assumed free");
        require(restored.route.restore_session_route_stack(*saved,owner,&why),why.c_str());
        require(restored.route.intersects_destination(last)==std::optional<bool>{true}&&restored.plan_calls==0&&restored.motion_publishes==0,
            "restored failure consults retained CurRoute without reranking or walking");
        require(live.route.tick(1)&&live.route.external_frame_active(),"failed attempt invokes actual source failure398");
        const auto child=live.route.capture_session_route_stack(owner,&why);
        if(!child)throw std::runtime_error("capture failed selected destination during398: "+why);
        require(child->jobs.back().failure_tree_invoked&&live.route.intersects_destination(last)==std::optional<bool>{true},
            "CurRoute remains consultable inside genuine failure child");
        Fixture childcopy(false,1,0,{},operation);childcopy.table=live.table;childcopy.grid=odh::RouteGrid{160,160};
        require(childcopy.world.restore_snapshot(live.world.snapshot())&&childcopy.vm.restore_snapshot(live.vm.snapshot()),"restore exact failure398 child ancestry");
        float child_heading=0;install_sparse_services(childcopy,child_heading);
        require(childcopy.route.restore_session_route_stack(*child,owner,&why)&&
            childcopy.route.intersects_destination(last)==std::optional<bool>{true}&&childcopy.plan_calls==0,
            "genuine failure398 child keeps last CurRoute across full-stack restore");
    }
}
void immediate_routing_service(){
    const auto child_owner=owner+1;
    for(const auto opcode:{22,27,45})for(const bool child_route:{false,true}){
        odh::Instruction operation{45,1,255,{0,0,1,0,1,0,0,0}};
        if(opcode==27)operation={27,1,255,{5,0,254,254,0,0,2,0}};
        if(opcode==22)operation={22,1,255,{2,0,0,0,0,0,0,0}};
        Fixture live(true,1,1,{},operation);live.table=source_table(0,0);
        float heading=0;install_sparse_services(live,heading);
        odh::RoutineSet child{{odh::routine_key(4100),{0x8002,
            {{2,254,255,{1,0,77,0,0,5,8,7}}},{},odh::Bhav::Metadata{0,4,4,0}}}};
        if(child_route)child.at(odh::routine_key(4100)).instructions={
            {27,1,255,{0,0,254,255,0,0,2,0}},
            {2,254,255,{1,0,77,0,0,5,8,7}}};
        require(live.vm.install_owner(child_owner,child),"install immediate child's exact private bank");
        live.start();const auto parent=*live.vm.pending();const auto frame=live.vm.stack().back();
        require(live.route.can_park_for_immediate(parent,frame),"source22/27/45 can park its exact active routing frame");
        auto forged=parent;++forged.ticket;
        require(!live.route.can_park_for_immediate(forged,frame)&&!live.route.park_for_immediate(parent.ticket),
            "forged pending and park before kernel injection are rejected without state changes");
        const odh::VmCall call{odh::routine_key(4100),{7,9,9,child_owner},{}};
        const auto immediate=live.vm.inject_immediate(call,[&](const auto& p,const auto& f){return live.route.can_park_for_immediate(p,f);});
        require(immediate&&live.route.park_for_immediate(parent.ticket)&&live.route.parked_for_immediate(),
            "kernel immediate continuation authenticates routing suspension");
        const auto source_ticks=live.source_ticks,moves=live.motion_publishes;
        require(live.route.tick(1)&&live.source_ticks==source_ticks&&live.motion_publishes==moves&&
            live.vm.temps()[1]==0,"covered route consumes no source RNG prefix movement or child execution");
        std::string reason;const auto saved=live.route.capture_session_route_stack(owner,&reason);
        if(!saved)throw std::runtime_error("parked route capture: "+reason);
        Fixture resumed(false,1,1,{},operation);resumed.table=live.table;
        require(resumed.vm.install_owner(child_owner,child)&&resumed.world.restore_snapshot(live.world.snapshot())&&
            resumed.vm.restore_snapshot(live.vm.snapshot()),"fresh immediate VM restores original raw pending and installed banks");
        float resumed_heading=0;install_sparse_services(resumed,resumed_heading);
        require(resumed.route.restore_session_route_stack(*saved,owner,&reason)&&resumed.route.parked_for_immediate(),
            "fresh route restore derives parking from exact saved VM continuation without new wire fields");
        auto invalid=*saved;++invalid.jobs.front().pending.ticket;
        Fixture rejected(false,1,1,{},operation);rejected.table=live.table;
        require(rejected.vm.install_owner(child_owner,child)&&rejected.world.restore_snapshot(live.world.snapshot())&&
            rejected.vm.restore_snapshot(live.vm.snapshot()),"prepare malformed parked-route restore receiver");
        float rejected_heading=0;install_sparse_services(rejected,rejected_heading);
        require(!rejected.route.restore_session_route_stack(invalid,owner),
            "changed parked pending identity is rejected before route publication");
        const auto drain=[&](Fixture& fixture,std::uint64_t tick,bool nested){
            if(nested){
                for(;fixture.route.active_route_depth()>1&&tick<150;++tick)
                    require(fixture.route.tick(tick),"advance immediate child route only");
                require(fixture.route.active_route_depth()==1&&fixture.route.parked_for_immediate()&&
                    fixture.vm.run(20)==odh::VmStatus::yielded,
                    "child route completes only its pending ticket and immediate action returns over parked parent");
            }
            require(fixture.vm.temps()[1]==77&&fixture.vm.resume_immediate(*immediate)&&
                fixture.route.retry_immediate_parent(parent.ticket)&&!fixture.route.parked_for_immediate()&&
                !fixture.route.retry_immediate_parent(parent.ticket),
                "exact immediate return resumes shared parent state once without rerunning source request");
            fixture.finish(tick,250);
        };
        const auto finish=[&](Fixture& fixture){
            const auto status=fixture.vm.run(20);
            if(child_route){
                require(status==odh::VmStatus::waiting&&fixture.route.active_route_depth()==2,
                    "immediate action may create a distinct child routing job over the frozen parent");
                // Advance actual child movement before saving its two-job stack.
                require(fixture.route.tick(2),"advance child once before fresh mid-child checkpoint");
                const auto nested=fixture.route.capture_session_route_stack(owner,&reason);
                if(!nested)throw std::runtime_error("immediate child route capture: "+reason);
                Fixture mid(false,1,1,{},operation);mid.table=fixture.table;
                require(mid.vm.install_owner(child_owner,child)&&mid.world.restore_snapshot(fixture.world.snapshot())&&
                    mid.vm.restore_snapshot(fixture.vm.snapshot()),"fresh mid-child VM and World retain original immediate and child pending");
                float mid_heading=0;install_sparse_services(mid,mid_heading);
                mid.source_ticks=fixture.source_ticks;
                require(mid.route.restore_session_route_stack(*nested,owner,&reason)&&mid.route.active_route_depth()==2&&
                    !mid.route.parked_for_immediate(),"fresh mid-child Routing restores active child plus covered parent without selecting goals");
                drain(fixture,3,true);drain(mid,3,true);
                require(mid.world.find(7)->position==fixture.world.find(7)->position&&
                    bits(mid.world.find(7)->radian_direction)==bits(fixture.world.find(7)->radian_direction)&&
                    mid.vm.temps()==fixture.vm.temps()&&mid.source_ticks==fixture.source_ticks,
                    "mid-child fresh restore preserves final pose registers and source RNG-prefix execution count");
            }else{
                require(status==odh::VmStatus::yielded,"non-routing immediate action returns over parked route");
                drain(fixture,2,false);
            }
        };
        finish(live);finish(resumed);
        require(live.world.find(7)->position==resumed.world.find(7)->position&&
            bits(live.world.find(7)->radian_direction)==bits(resumed.world.find(7)->radian_direction)&&
            live.vm.temps()==resumed.vm.temps(),"fresh parked-route continuation equals uninterrupted shared-service execution");
    }
}
void immediate_routing_interruption(){
    const odh::Instruction operation{45,1,255,{0,0,1,0,0,0,0,0}};
    Fixture fixture(true,1,0,{},operation);fixture.table=source_table(0,0);
    float heading=0;install_sparse_services(fixture,heading);
    const odh::CodeOwner child_owner=owner+1;
    require(fixture.vm.install_owner(child_owner,{{odh::routine_key(4100),{0x8002,
        {{2,254,255,{1,0,77,0,0,5,8,7}}},{},odh::Bhav::Metadata{0,4,4,0}}}}),
        "install interruption fixture immediate child bank");
    fixture.start();const auto parent=fixture.vm.pending()->ticket;
    const auto child=fixture.vm.inject_immediate({odh::routine_key(4100),{7,9,9,child_owner},{}},
        [&](const auto& p,const auto& f){return fixture.route.can_park_for_immediate(p,f);});
    require(child&&fixture.route.park_for_immediate(parent)&&fixture.route.interrupt()&&
        fixture.route.tick(1)&&fixture.vm.status()==odh::VmStatus::running&&!fixture.vm.pending()&&
        fixture.source_ticks==0,"canceling a covered route latches its interruption without completing or executing the immediate child");
    std::string reason;require(fixture.route.capture_session_route_stack(owner,&reason).has_value(),
        "latched route cancellation retains a persistable immediate parent");
    require(fixture.vm.run(8)==odh::VmStatus::yielded&&fixture.vm.temps()[1]==77&&
        fixture.vm.resume_immediate(*child)&&fixture.route.retry_immediate_parent(parent)&&fixture.route.tick(2)&&
        fixture.vm.run(8)==odh::VmStatus::yielded&&fixture.vm.external_frame_return()&&fixture.route.tick(3)&&
        fixture.vm.run(8)==odh::VmStatus::returned_false&&!fixture.route.active(),
        "covered route handles authored failure only after immediate returns, preserving its real child and root tickets");
}
void authenticated_transient_relative_routes(){
    const auto bind_provider=[](Fixture& fixture,odh::RandomState& random,unsigned& calls,
            std::optional<odh::RoutePlacementContext>& retained){
        fixture.route.set_authenticated_range_slot_goal_provider([&fixture,&random,&calls,&retained](const auto& table,
            auto index,const auto& caller,const auto& target,float radians,const auto& context){
            ++calls;retained=context;
            require(context.instruction.opcode==27&&index==0&&
                context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch),
                "transient range receives an authenticated REAL opcode27 placement lease");
            auto fake=context.instruction;fake.opcode=45;
            require(!context.proof.matches(context.frame,*context.vm,fake,context.dispatch),
                "raw27 lease cannot authorize a fabricated opcode45 instruction");
            const auto canonical=odh::make_source_goto_relative_slot(context.instruction);
            require(canonical&&table.records.size()==1&&table.records[0].proximity==canonical->records[0].proximity&&
                table.records[0].facing==canonical->records[0].facing,
                "actual provider sees the canonical transient row, not a file SLOT or counter-based index");
            return odh::AuthenticatedRangeSlotGoals{odh::rank_source_standing_range_slot(table,index,caller,target,
                radians,random,[](auto){return odh::SourceSlotRoomResult::same_room;},
                [](auto){return odh::ReferenceSlotVerification::eligible;}),0,0,0};
        });
    };
    for(const auto direction:{0,3,7,254,255}){
        odh::Instruction operation{27,1,255,{5,0,0,static_cast<std::uint8_t>(direction),0,0,2,0}};
        Fixture live(true,1,1,{},operation);float heading=0;install_sparse_services(live,heading);
        // A deliberately stale blocked bitmap must not decide source goals.
        for(std::int32_t y=0;y<64;++y)for(std::int32_t x=0;x<64;++x)live.grid.block({x,y});
        unsigned calls=0;odh::RandomState random{91};std::optional<odh::RoutePlacementContext> retained;
        bind_provider(live,random,calls,retained);live.start();
        require(calls==1&&live.validations==0&&live.resources==0&&retained&&
            !retained->proof.matches(retained->frame,live.vm,retained->instruction,retained->dispatch),
            "source27 uses live placement instead of cached-grid validation and expires its callback lease");
        std::string reason;const auto saved=live.route.capture_local_session_route(owner,&reason);
        if(!saved)throw std::runtime_error("transient relative capture: "+reason);
        require(saved->source_range&&saved->source_sparse&&!saved->source_circle&&
            saved->source_range->slot_index==0&&saved->source_range->slot.proximity==
                std::optional<std::array<std::int32_t,3>>{{16,24,0}}&&
            saved->face_anywhere==(direction==255)&&!saved->call_failure_trees,
            "selected transient range saves its complete canonical row and source facing policy");
        const auto goal=saved->source_range->selected;
        Fixture resumed(false,1,1,{},operation);
        require(resumed.world.restore_snapshot(live.world.snapshot())&&resumed.vm.restore_snapshot(live.vm.snapshot()),
            "transient relative fixture restores the same World and real raw27 pending frame");
        float restored_heading=0;install_sparse_services(resumed,restored_heading);
        unsigned replay_calls=0;odh::RandomState restored_random{random.seed()};std::optional<odh::RoutePlacementContext> replay_lease;
        bind_provider(resumed,restored_random,replay_calls,replay_lease);
        require(resumed.route.restore_local_session_route(*saved,owner)&&replay_calls==0&&resumed.resources==0,
            "raw27 selected range restores without scoring, resource substitution or new placement checks");
        live.finish();resumed.finish();
        require(live.world.find(7)->position==resumed.world.find(7)->position&&
            live.world.find(7)->position->x==goal.position.x&&live.world.find(7)->position->y==goal.position.y&&
            random.seed()==restored_random.seed(),
            "continuous and reopened transient ranges arrive at the same selected source goal without reranking");
        if(direction!=255)require(bits(live.world.find(7)->radian_direction)==bits(goal.radian_direction)&&
            bits(resumed.world.find(7)->radian_direction)==bits(goal.radian_direction),
            "authored and target-facing transient directions retain exact final radians across restore");
    }
}
void authenticated_range_routes(){
    Fixture live;live.table=range_table();
    require(live.world.set_radian_direction(7,1.0f),"range actor starts between direction notches");
    std::optional<odh::RoutePlacementContext> captured;unsigned providers=0;
    live.route.set_authenticated_range_slot_goal_provider([&](const auto& table,auto,const auto&,const auto& target,float angle,const auto& context){
        ++providers;captured=context;
        require(context.vm==&live.vm&&context.proof.matches(context.frame,live.vm,context.instruction,context.dispatch),
            "actual source45 callback receives its own opaque placement authority");
        auto wrong=context.dispatch;++wrong.ticket;
        auto frame=context.frame;++frame.arguments[0];
        auto exact=context.frame;exact.exact_arguments=!exact.exact_arguments;
        auto operation=context.instruction;++operation.operands[0];
        require(!context.proof.matches(context.frame,live.vm,context.instruction,wrong)&&
            !context.proof.matches(frame,live.vm,context.instruction,context.dispatch)&&
            !context.proof.matches(exact,live.vm,context.instruction,context.dispatch)&&
            !context.proof.matches(context.frame,live.vm,operation,context.dispatch),
            "placement authority rejects changed ticket, frame arguments, extent profile and instruction");
        const auto old=live.world.find(7)->radian_direction;
        require(live.world.set_radian_direction(7,0)&&context.proof.matches(context.frame,live.vm,context.instruction,context.dispatch),
            "legitimate temporary NORTH direction and direction notch remain inside the placement lease");
        const auto nested=live.route.primitive()(context.frame,context.instruction,live.vm.temps(),context.dispatch);
        require(nested.disposition==odh::PrimitiveDisposition::unsupported&&providers==1,
            "range provider cannot recursively mint a nested planning lease");
        require(live.world.set_radian_direction(7,old),"restore source temporary heading before ranked goals return");
        return odh::AuthenticatedRangeSlotGoals{{range_goal(table,target,angle,{20,4}),range_goal(table,target,angle,{4,20})},0,0,0};
    });
    bool trace_refused=false;
    require(live.vm.start(odh::routine_key(4116),{7,7,9,owner},{8})==odh::VmStatus::running,"start real generic range source frame");
    require(live.vm.run(8,[&](const auto& trace){if(trace.instruction.opcode!=45)return;
        try {(void)live.route.primitive()(live.vm.stack().back(),trace.instruction,live.vm.temps(),{&live.vm,1});}
        catch(const std::logic_error&){trace_refused=true;}
    })==odh::VmStatus::waiting&&trace_refused&&providers==1,"a guessed next ticket in trace cannot grant source45 authority");
    require(captured&&!captured->proof.matches(captured->frame,live.vm,captured->instruction,captured->dispatch),
        "captured range proof expires immediately after its provider callback");
    const auto snapshot=live.route.capture_local_session_route(owner);
    require(snapshot&&snapshot->source_range&&!snapshot->source_circle&&snapshot->arrival_radian_direction&&
        snapshot->path.back()==odh::RoutePoint{20,4}&&snapshot->source_range->fallback_goals.size()==1&&
        bits(*snapshot->arrival_radian_direction)==bits(snapshot->source_range->selected.radian_direction),
        "generic range keeps its own manifest and first reachable rank even when a later goal is cheaper");
    Fixture resumed(false);resumed.table=range_table();
    require(resumed.world.restore_snapshot(live.world.snapshot())&&resumed.vm.restore_snapshot(live.vm.snapshot())&&
        resumed.route.restore_local_session_route(*snapshot,owner),"generic range restores into a fresh graph without ranking or live placement callbacks");
    Fixture invalid(false);invalid.table=range_table();
    require(invalid.world.restore_snapshot(live.world.snapshot())&&invalid.vm.restore_snapshot(live.vm.snapshot()),"prepare isolated generic range rejection graph");
    auto tampered=*snapshot;++tampered.source_range->slot.standing;
    require(!invalid.route.restore_local_session_route(tampered,owner)&&!invalid.route.active(),"changed full SLOT row cannot borrow saved range authority");
    tampered=*snapshot;tampered.source_range->fallback_goals[0]=tampered.source_range->selected;
    require(!invalid.route.restore_local_session_route(tampered,owner),"duplicate range fallback cannot alter the frozen ranking manifest");
    tampered=*snapshot;tampered.source_circle.emplace();
    require(!invalid.route.restore_local_session_route(tampered,owner),"range cannot masquerade as a circle continuation");
    const auto forged=live.route.primitive()(live.vm.stack().back(),live.vm.pending()->instruction.instruction,
        live.vm.temps(),{&live.vm,live.vm.pending()->ticket});
    require(forged.disposition==odh::PrimitiveDisposition::unsupported&&providers==1,
        "public invocation while pending45 cannot impersonate the private replay phase");
    live.finish();resumed.finish();
    require(bits(live.world.find(7)->radian_direction)==bits(snapshot->source_range->selected.radian_direction)&&
        bits(live.world.find(7)->radian_direction)==bits(resumed.world.find(7)->radian_direction),
        "continuous source FaceTowards arrival survives fresh-graph continuation");
    const auto old_run=live.vm.run_generation();live.vm.start(odh::routine_key(4116),{7,7,9,owner},{8});
    require(live.vm.run_generation()!=old_run&&!captured->proof.matches(captured->frame,live.vm,captured->instruction,captured->dispatch),
        "retained source token cannot cross a restart of its own VM");
}
void range_retry_and_replay(){
    Fixture retry;retry.table=range_table();
    retry.route.set_authenticated_range_slot_goal_provider([](const auto& table,auto,const auto&,const auto& target,float angle,const auto&){
        return odh::AuthenticatedRangeSlotGoals{{range_goal(table,target,angle,{20,4}),range_goal(table,target,angle,{4,20})},0,0,0};});
    retry.start();const auto original=retry.route.capture_local_session_route(owner);
    require(original&&original->source_range&&original->path.size()>1&&retry.grid.block(original->path[1]),"block the next step of a frozen generic range route");
    require(retry.route.tick(1),"range retries its next saved ranked option after a movement obstruction");
    const auto fallback=retry.route.capture_local_session_route(owner);
    require(fallback&&fallback->source_range&&fallback->path.back()==odh::RoutePoint{4,20}&&fallback->source_range->fallback_goals.empty(),
        "range fallback preserves order without rerunning RNG or live placement");
    retry.finish(2);
    for(bool unknown:{false,true}){
        Fixture replay;replay.table=range_table();bool same_room=false;unsigned providers=0;std::optional<odh::RoutePlacementContext> captured;
        replay.route.set_room_pair_provider([&](const auto&,const auto&){return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,static_cast<std::uint16_t>(same_room?1:2)}};});
        replay.route.set_room_portal_step_provider([&](auto,const auto&,const auto&,const auto&,const auto&,auto){
            same_room=true;return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::destination_reached,{},{}};});
        replay.route.set_authenticated_range_slot_goal_provider([&](const auto& table,auto,const auto&,const auto& target,float angle,const auto& context){
            ++providers;captured=context;
            require(!replay.vm.dispatch_active()&&replay.vm.status()==odh::VmStatus::waiting&&
                context.proof.matches(context.frame,replay.vm,context.instruction,context.dispatch),
                "only the private room-route replay mints authority for the actual waiting source45");
            if(unknown)throw std::runtime_error("EP5 capability unknown");
            return odh::AuthenticatedRangeSlotGoals{{range_goal(table,target,angle,{20,4})},0,0,0};
        });
        replay.start();require(providers==0&&replay.route.waiting_for_room_portal_step(),"cross-room request parks before local generic goal generation");
        const auto ticket=replay.vm.pending()->ticket;
        const bool advanced=replay.route.tick(1);
        require(providers==1&&captured&&!captured->proof.matches(captured->frame,replay.vm,captured->instruction,captured->dispatch),
            "private replay lease also expires on both successful and throwing callback return");
        if(unknown){require(!advanced&&replay.route.active()&&replay.vm.pending()&&replay.vm.pending()->ticket==ticket&&
            replay.vm.status()==odh::VmStatus::waiting,"unknown replay placement keeps authored branch pending rather than forging false");}
        else {require(advanced&&replay.route.capture_local_session_route(owner)->source_range.has_value(),"private replay publishes a typed local range continuation");replay.finish(2);}
    }
}
void generic_ranked_sparse_choices(){
    unsigned providers=0;
    odh::PreRankedSlotGoalProvider ranked=[&](const auto&,auto,const auto&,const auto& target){
        ++providers;std::vector<odh::PreRankedSlotGoal> goals;
        for(const auto point:{odh::RoutePoint{100,20},odh::RoutePoint{120,20}}){
            const auto flags=odh::reference_slot_entry_flags({target.x,target.y},target.direction,point,255);
            odh::PreRankedSlotGoal goal{point,flags,static_cast<std::uint8_t>(goals.size()+2)};
            goals.push_back(goal);
        }
        return goals;
    };
    Fixture live(true,1,1,std::move(ranked));auto& slot=live.table.records[8];slot.offset_bits={bits(0.25f),bits(1.25f),0};
    slot.standing=1;slot.sitting=0;slot.ground=0;slot.flags=255;slot.proximity=std::array<std::int32_t,3>{16,80,16};
    float begin_heading=0;install_sparse_services(live,begin_heading);
    live.start();
    const auto saved=live.route.capture_local_session_route(owner);
    require(saved&&saved->source_sparse&&saved->source_sparse->choices&&!saved->source_range&&!saved->source_circle&&
        saved->source_sparse->choices->slot.flags==slot.flags&&
        saved->source_sparse->choices->slot.offset_bits==slot.offset_bits&&
        saved->source_sparse->choices->slot.proximity==slot.proximity&&saved->source_sparse->choices->slot_index==8&&
        saved->source_sparse->choices->fallback_goals.size()==1&&saved->path.back()==odh::RoutePoint{100,20}&&providers==1,
        "generic standing ranked SLOT freezes its full row and selected/fallback order in the sparse snapshot");
    Fixture resumed(false);resumed.table=live.table;
    require(resumed.world.restore_snapshot(live.world.snapshot())&&resumed.vm.restore_snapshot(live.vm.snapshot())&&
        resumed.route.restore_local_session_route(*saved,owner)&&providers==1,
        "generic choices restore against the exact resource row without reranking");
    Fixture invalid(false);invalid.table=live.table;
    require(invalid.world.restore_snapshot(live.world.snapshot())&&invalid.vm.restore_snapshot(live.vm.snapshot()),
        "prepare a generic choice tamper graph");
    auto tampered=*saved;++(*tampered.source_sparse->choices->slot.max_size);
    require(!invalid.route.restore_local_session_route(tampered,owner),
        "generic choices reject an exact-row mutation even when the changed field is unused by ranking");
    tampered=*saved;tampered.source_sparse->choices->fallback_goals.front()=tampered.source_sparse->choices->selected;
    require(!invalid.route.restore_local_session_route(tampered,owner),
        "generic choices reject duplicate selected/fallback identities");
}
void range_callback_boundaries(){
    for(bool at_goal:{false,true}){
        Fixture attached;attached.table=range_table();
        require(attached.world.create(10,odh::EntityKind::object,{0,0,1,0}),"create separate target attachment parent");
        attached.route.set_authenticated_range_slot_goal_provider([&](const auto& table,auto,const auto&,const auto& target,float angle,const auto& context){
            require(context.proof.matches(context.frame,attached.vm,context.instruction,context.dispatch),"target starts with valid unattached placement authority");
            if(!at_goal){
                require(attached.world.attach(10,0,9)&&*attached.world.find(9)->position==target,
                    "provider changes only target attachment and preserves frozen position");
                require(!context.proof.matches(context.frame,attached.vm,context.instruction,context.dispatch),
                    "target attachment mutation immediately revokes live placement authority");
            }
            return odh::AuthenticatedRangeSlotGoals{{range_goal(table,target,angle,{20,4})},0,0,0};
        });
        if(at_goal)attached.route.set_route_goal_validator([&](const auto&,auto){
            require(attached.world.attach(10,0,9),"goal callback changes only target attachment");return true;
        });
        require(attached.vm.start(odh::routine_key(4116),{7,7,9,owner},{8})==odh::VmStatus::running&&
            attached.vm.run(8)==odh::VmStatus::fault&&!attached.route.active()&&attached.world.find(9)->attachment.has_value(),
            "provider and goal callback attachment effects persist but cannot publish a stale source range job");
    }
    Fixture changed;changed.table=range_table();
    changed.route.set_authenticated_range_slot_goal_provider([](const auto& table,auto,const auto&,const auto& target,float angle,const auto&){
        return odh::AuthenticatedRangeSlotGoals{{range_goal(table,target,angle,{20,4}),range_goal(table,target,angle,{4,20})},0,0,0};});
    unsigned validators=0;
    changed.route.set_route_goal_validator([&](const auto&,auto){
        ++validators;require(changed.world.remove(9)&&changed.world.create(9,odh::EntityKind::object)&&
            changed.world.set_position(9,{20,20,1,0}),"goal validator recycles target identity");return true;
    });
    require(changed.vm.start(odh::routine_key(4116),{7,7,9,owner},{8})==odh::VmStatus::running&&
        changed.vm.run(8)==odh::VmStatus::fault&&!changed.route.active()&&validators==1,
        "range stops immediately after a goal validator changes participants and publishes no stale job");
    Fixture pending(true,0);pending.table=range_table();
    pending.route.set_authenticated_range_slot_goal_provider([](const auto& table,auto,const auto&,const auto& target,float angle,const auto&){
        return odh::AuthenticatedRangeSlotGoals{{range_goal(table,target,angle,{20,4})},0,0,0};});
    bool mutate_pending=false;
    pending.route.set_stack_object_slot_table_provider([&](const auto&){
        if(mutate_pending)require(pending.vm.update_pending(pending.vm.pending()->ticket,{odh::VmFrameBank::arguments,0,7}),
            "resource callback mutates pending SLOT argument without changing ticket or run");return &pending.table;
    });
    pending.start();const auto snapshot=pending.route.capture_local_session_route(owner);
    require(snapshot&&snapshot->source_range,"prepare argument-selected frozen range manifest");
    mutate_pending=true;
    require(!pending.route.capture_local_session_route(owner),
        "range manifest validation rejects exact pending-frame changes made by its resource callback");
    Fixture attached_actor;attached_actor.table=range_table();
    require(attached_actor.world.create(10,odh::EntityKind::object,{0,0,1,0})&&
        attached_actor.world.set_position(10,*attached_actor.world.find(7)->position),
        "create same-position container for range resource-callback actor attachment");
    attached_actor.route.set_authenticated_range_slot_goal_provider([](const auto& table,auto,const auto&,const auto& target,float angle,const auto&){
        return odh::AuthenticatedRangeSlotGoals{{range_goal(table,target,angle,{20,4})},0,0,0};});
    bool attach_actor=false;
    attached_actor.route.set_stack_object_slot_table_provider([&](const auto&){
        if(attach_actor)require(attached_actor.world.attach(10,0,7),
            "resource callback attaches the range actor without changing its position");
        return &attached_actor.table;
    });
    attached_actor.start();
    const auto attached_snapshot=attached_actor.route.capture_local_session_route(owner);
    require(attached_snapshot&&attached_snapshot->source_range,
        "capture valid unattached range actor before enabling resource-callback side effect");
    const auto actor_position=*attached_actor.world.find(7)->position;
    const auto actor_generation=attached_actor.world.find(7)->generation;
    const auto ticket=attached_actor.vm.pending()->ticket;
    attach_actor=true;
    require(!attached_actor.route.capture_local_session_route(owner)&&
        attached_actor.world.find(7)->attachment&&attached_actor.world.find(7)->attachment->container==10&&
        *attached_actor.world.find(7)->position==actor_position&&
        attached_actor.world.find(7)->generation==actor_generation&&
        attached_actor.vm.pending()->ticket==ticket&&attached_actor.route.active(),
        "range capture rejects callback-created actor attachment while preserving its live effect and pending route");
}
void range_failure_metadata(){
    Fixture failed(true,1,0);failed.table=range_table();
    failed.route.set_authenticated_range_slot_goal_provider([&](const auto&,auto,const auto&,const auto&,float,const auto&){
        return odh::AuthenticatedRangeSlotGoals{{},8,9,failed.world.find(9)->generation};});
    failed.start();require(failed.route.tick(1)&&failed.vm.stack().size()==2&&failed.vm.stack().back().arguments[0]==8&&
        failed.vm.stack().back().arguments[1]==9,"source range parser failure sends the known first object blocker to failure tree398");
    Fixture malformed;malformed.table=range_table();
    malformed.route.set_authenticated_range_slot_goal_provider([](const auto&,auto,const auto&,const auto&,float,const auto&){
        return odh::AuthenticatedRangeSlotGoals{{},13,0,0};});
    require(malformed.vm.start(odh::routine_key(4116),{7,7,9,owner},{8})==odh::VmStatus::running&&
        malformed.vm.run(8)==odh::VmStatus::fault&&!malformed.route.active(),"standing-only range cannot invent an avatar failure or silently clear unknown metadata");
}

void face_anywhere_live_range_continuation(){
    const auto configure_shape=[](Fixture& fixture){
        fixture.table=range_table();auto& row=fixture.table.records[8];row.facing=-3;
        fixture.target_id=7;
        fixture.live_ep5=[&fixture](const odh::LiveAttemptWalkEntryPoint5Request& request){
            ++fixture.live_ep5_calls;
            require((request.tested.id==7||request.tested.id==11)&&request.action==4105&&request.ghost==1&&
                request.arguments==std::array<std::int16_t,4>{request.other.id,1,0,0},
                "synthetic solid footprint uses its declared synchronous false EP5 with exact source arguments");
            return odh::LiveAttemptWalkEntryPoint5Result{odh::LiveAttemptWalkEntryPoint5State::returned_false,{}};
        };
        fixture.live_footprint=[&fixture](const odh::LiveAttemptWalkIdentity identity){
            const auto* live=fixture.world.find(identity.id);
            if(!live||live->generation!=identity.generation)
                return odh::LiveAttemptWalkFootprint{odh::LiveAttemptWalkFootprintState::unsupported,{}};
            if(identity.id!=11)return odh::LiveAttemptWalkFootprint{odh::LiveAttemptWalkFootprintState::absent,{}};
            const auto p=*live->position;
            return odh::LiveAttemptWalkFootprint{odh::LiveAttemptWalkFootprintState::present,
                odh::RectRouteObstacle{p.x-4,p.y-4,p.x+4,p.y+4}};
        };
        fixture.live_placement=[&fixture](const auto& context,const auto& candidate)
                ->std::optional<odh::SourceSparsePlacementOutcome>{
            if(!context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch,context.phase))return {};
            const auto* blocker=fixture.world.find(11);if(!blocker||!blocker->position)return {};
            const auto p=*blocker->position;
            if(candidate.level==p.level&&std::abs(candidate.x-p.x)<=7&&std::abs(candidate.y-p.y)<=7)
                return odh::SourceSparsePlacementOutcome{odh::SourceSparsePlacementStatus::blocked_object,11,blocker->generation,{}};
            return odh::SourceSparsePlacementOutcome{odh::SourceSparsePlacementStatus::success,{},{},{}};
        };
    };
    unsigned warm_calls=0,cold_calls=0;
    odh::RandomState warm_random{0x7261},cold_random{0x7261};
    const auto bind_provider=[](Fixture& fixture,odh::RandomState& random,unsigned& calls){
        fixture.route.set_authenticated_range_slot_goal_provider([&fixture,&random,&calls](const auto& table,auto index,
                const auto& caller,const auto& target,float radians,const auto& context){
            ++calls;
            require(context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch),
                "FaceAnywhere ranking uses the exact authenticated source45 lease");
            const auto verify=[&](const odh::RoutePoint point){
                const auto* blocker=fixture.world.find(11);require(blocker&&blocker->position,"live range blocker still exists");
                const auto p=*blocker->position;
                return std::abs(point.x-p.x)<=7&&std::abs(point.y-p.y)<=7?
                    odh::ReferenceSlotVerification::rejected_after_score:odh::ReferenceSlotVerification::eligible;
            };
            return odh::AuthenticatedRangeSlotGoals{odh::rank_source_standing_range_slot(table,index,caller,target,
                radians,random,[](auto){return odh::SourceSlotRoomResult::same_room;},verify),0,0,0};
        });
    };
    Fixture warm;configure_shape(warm);
    require(warm.world.set_position(7,{40,40,1,0})&&warm.world.create(11,odh::EntityKind::object)&&
        warm.world.declare_object_word(11,8,0)&&warm.world.set_position(11,{80,80,1,0}),
        "construct a live solid object outside the initial range");
    std::vector<odh::EntryPoint> entries(6);entries[5].action=4105;
    require(warm.world.define_type(7,0x90110007U)&&warm.world.define_type(11,0x9011000bU)&&
        warm.world.define_entry_points(7,entries)&&warm.world.define_entry_points(11,entries),
        "declare exact synthetic resource identities and synchronous false EP5 entries before obstacle planning");
    // The warm grid predates this actual World mutation. A fresh graph has an
    // intentionally different cached bitmap; neither bitmap is source policy.
    require(warm.world.set_position(11,{56,40,1,0}),"move the live obstacle after warm grid preparation");
    Fixture cold(false);configure_shape(cold);
    require(cold.world.restore_snapshot(warm.world.snapshot())&&cold.vm.restore_snapshot(warm.vm.snapshot()),
        "fresh graph restores the same moved-obstacle World and idle VM");
    for(std::int32_t y=0;y<64;++y)for(std::int32_t x=0;x<64;++x)cold.grid.block({x,y});
    float warm_heading=0,cold_heading=0;install_sparse_services(warm,warm_heading);install_sparse_services(cold,cold_heading);
    bind_provider(warm,warm_random,warm_calls);bind_provider(cold,cold_random,cold_calls);
    warm.start();cold.start();
    const auto a=warm.route.capture_local_session_route(owner),b=cold.route.capture_local_session_route(owner);
    require(a&&b&&a->source_range&&b->source_range&&!a->source_sparse->choices&&!b->source_sparse->choices&&
        a->face_anywhere&&b->face_anywhere&&a->source_sparse->face_anywhere&&b->source_sparse->face_anywhere&&
        a->facing==0&&b->facing==0&&bits(*a->arrival_radian_direction)==0&&bits(*b->arrival_radian_direction)==0,
        "compatible FaceAnywhere ranges enter the shared source manifest with neutral facing and radians");
    const auto equal_goal=[](const auto& first,const auto& second){return first.position==second.position&&
        first.entry_flags==second.entry_flags&&first.facing==second.facing&&bits(first.radian_direction)==bits(second.radian_direction);};
    const auto& ar=*a->source_range;const auto& br=*b->source_range;
    require(equal_goal(ar.selected,br.selected)&&ar.fallback_goals.size()==br.fallback_goals.size()&&
        std::equal(ar.fallback_goals.begin(),ar.fallback_goals.end(),br.fallback_goals.begin(),equal_goal)&&
        std::none_of(ar.fallback_goals.begin(),ar.fallback_goals.end(),[](const auto& goal){return goal.position==odh::RoutePoint{56,40};})&&
        warm_calls==1&&cold_calls==1&&warm_random.seed()==cold_random.seed()&&warm.validations==0&&cold.validations==0&&
        warm.live_ep5_calls>0&&cold.live_ep5_calls==warm.live_ep5_calls,
        "warm and fresh grids produce identical full frozen tails and RNG after live obstacle verification");
    Fixture resumed(false);configure_shape(resumed);
    require(resumed.world.restore_snapshot(warm.world.snapshot())&&resumed.vm.restore_snapshot(warm.vm.snapshot()),
        "restore an admitted FaceAnywhere source route into fresh World and VM");
    float resumed_heading=0;install_sparse_services(resumed,resumed_heading);
    require(resumed.route.restore_local_session_route(*a,owner)&&resumed.plan_calls==0,
        "FaceAnywhere source continuation restores without ranking or planning");
    auto forged=*a;forged.face_anywhere=false;
    Fixture wrong(false);configure_shape(wrong);
    require(wrong.world.restore_snapshot(warm.world.snapshot())&&wrong.vm.restore_snapshot(warm.vm.snapshot())&&
        !wrong.route.restore_local_session_route(forged,owner),"FaceAnywhere range rejects a frozen facing-policy mutation");
    // A second live mutation is a real solid obstruction of the next source
    // placement and complete-room reroute. Both executions consume the same
    // alternatives and retain the authored failure, without cache substitution.
    const auto position=*warm.world.find(7)->position;
    require(warm.world.set_position(11,position)&&resumed.world.set_position(11,position),
        "move the same solid World object onto both active route paths");
    if(!warm.route.tick(1))throw std::runtime_error("warm FaceAnywhere collision: "+warm.route.diagnostic());
    if(!resumed.route.tick(1))throw std::runtime_error("restored FaceAnywhere collision: "+resumed.route.diagnostic());
    require(!warm.route.active()&&!resumed.route.active()&&warm.vm.run(8)==odh::VmStatus::returned_false&&
        resumed.vm.run(8)==odh::VmStatus::returned_false&&warm.world.find(7)->person_data[62]==3&&
        resumed.world.find(7)->person_data[62]==3&&warm.world.find(7)->position==resumed.world.find(7)->position&&
        warm.world.find(7)->radian_direction==resumed.world.find(7)->radian_direction&&
        warm.plan_calls==resumed.plan_calls+1&&warm.source_ticks==resumed.source_ticks&&warm_calls==1&&cold_calls==1,
        "live and restored FaceAnywhere collisions exhaust the same frozen alternatives without reranking or stale-grid permission");
}
void source_soft_failure_room_graph(){
    const auto ranked=[](const auto&,auto,const auto&,const auto& target){
        return std::vector<odh::PreRankedSlotGoal>{
            {{100,20},odh::reference_slot_entry_flags({target.x,target.y},target.direction,{100,20},255),6},
            {{4,20},odh::reference_slot_entry_flags({target.x,target.y},target.direction,{4,20},255),2},
            {{4,36},odh::reference_slot_entry_flags({target.x,target.y},target.direction,{4,36},255),2}};};
    const auto configure=[](Fixture& fixture){auto& row=fixture.table.records[8];
        row.offset_bits={bits(0.25f),bits(1.25f),0};row.standing=1;row.flags=255;
        row.proximity=std::array<std::int32_t,3>{16,80,16};};
    for(bool found:{false,true}){
        Fixture graph(true,1,static_cast<std::uint8_t>(found?1:0),ranked);configure(graph);bool crossed=false;
        require(graph.world.create(11,odh::EntityKind::object)&&graph.world.set_position(11,{12,12,1,0})&&
            graph.world.declare_object_word(11,8,0),
            "construct the independently identified pure room-graph edge with explicit collision flags");
        graph.route.set_room_pair_provider([&](const auto&,const auto&){
            return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,static_cast<std::uint16_t>(crossed?2:1)}};});
        graph.room_plan=[&](const auto& context,const auto& target)->std::optional<odh::SourceRoomRoutePlan>{
            require(!context.room_context_open&&target==*graph.world.find(9)->position&&
                context.dispatch.ticket==graph.vm.pending()->ticket,
                "never-portal fallback uses Init-empty history and the live original Slot target");
            if(!found)return odh::SourceRoomRoutePlan{};
            return odh::SourceRoomRoutePlan{true,{{1,2,11,*graph.world.find(11)->position}}};};
        float heading=0;install_sparse_services(graph,heading);graph.start();crossed=true;
        graph.initial_block=odh::SourceSparsePlacementStatus::blocked_object;graph.obstruct_plans=true;
        if(!graph.route.tick(1))throw std::runtime_error("pure fallback room graph: "+graph.route.diagnostic());
        if(found){const auto saved=graph.route.capture_local_session_route(owner);
            require(saved&&saved->source_sparse->fallback_room_route.size()==1&&!saved->source_sparse->walk_to_pending&&
                saved->path.back()==odh::RoutePoint{4,20}&&graph.room_plans==1&&!graph.route.external_frame_active(),
                "successful pure DoRoomRoute retains ordered Rooms while WALKING continues without executing a portal");
        }else{
            const auto failed=graph.route.capture_session_route_stack(owner);
            require(graph.room_plans==2&&graph.route.external_frame_active()&&graph.vm.stack().back().key.id==398&&
                graph.vm.stack().back().arguments==std::vector<std::int16_t>{8,9,0,0}&&
                graph.world.find(7)->person_data[62]==8&&failed&&failed->jobs.back().fallback_goals.empty(),
                "failed room alternatives are all removed while HardFail keeps original DestTileOccupied and blocker metadata");
        }
    }
    for(const bool recycle_actor:{false,true}){
        Fixture changed(true,1,1,ranked);configure(changed);bool crossed=false;
        changed.route.set_room_pair_provider([&](const auto&,const auto&){
            return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,static_cast<std::uint16_t>(crossed?2:1)}};});
        changed.room_plan=[&](const auto&,const auto&)->std::optional<odh::SourceRoomRoutePlan>{
            const std::int16_t id=recycle_actor?7:9;const auto position=*changed.world.find(id)->position;
            const auto kind=recycle_actor?odh::EntityKind::avatar:odh::EntityKind::object;
            require(changed.world.remove(id)&&(recycle_actor?changed.world.create(id,kind,{0,88,3,16}):changed.world.create(id,kind))&&
                changed.world.set_position(id,position),"recycle a participant at the identical position during graph callback");
            return odh::SourceRoomRoutePlan{};};
        float heading=0;install_sparse_services(changed,heading);changed.start();crossed=true;
        changed.initial_block=odh::SourceSparsePlacementStatus::blocked_object;changed.obstruct_plans=true;
        require(!changed.route.tick(1)&&changed.room_plans==1&&
            changed.route.diagnostic().find("participants or VM frame")!=std::string::npos&&
            !changed.route.capture_session_route_stack(owner),
            "same-position recycled actor or target invalidates the pure graph decision and cannot serialize stale route state");
    }
    Fixture arrived(true,1,1,ranked);configure(arrived);bool same_room=false;unsigned contexts_closed=0;
    arrived.route.set_room_pair_provider([&](const auto&,const auto&){
        return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,static_cast<std::uint16_t>(same_room?1:2)}};});
    arrived.route.set_room_portal_step_provider([&](auto,const auto&,const auto&,const auto&,const auto&,auto){
        same_room=true;return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::destination_reached,{},{}};});
    arrived.route.set_route_grid_refresh([](const auto&){return true;});
    arrived.route.set_room_portal_context_end_provider([&](auto){++contexts_closed;});
    float heading=0;install_sparse_services(arrived,heading);arrived.start();
    require(arrived.route.tick(1)&&contexts_closed==0,"source local replay retains the original room context until actual PreExit");
    const auto retained=arrived.route.capture_session_route_stack(owner);
    require(retained&&retained->jobs.size()==1&&retained->jobs.front().room_context_open&&
        retained->jobs.front().source_sparse&&!arrived.route.capture_local_session_route(owner),
        "postportal source walking uses stack persistence so ignored-room history is not silently discarded");
    // This retained-context scenario traverses 96 lattice units, exceeding
    // the helper's 100-tick allowance for its older short SLOT routes.
    arrived.finish(2,256);
    require(contexts_closed==1,"actual route exit releases the retained room context exactly once");
}
void source_soft_failure_alternatives(){
    // Circle, authenticated range and generic ranked rows use one shared
    // AttemptRoute consumer. The generic provider intentionally omits radians.
    for(unsigned profile=0;profile<4;++profile){
        unsigned providers=0;
        odh::PreRankedSlotGoalProvider generic=[&](const auto&,auto,const auto&,const auto& target){
            ++providers;return std::vector<odh::PreRankedSlotGoal>{
                {{100,20},odh::reference_slot_entry_flags({target.x,target.y},target.direction,{100,20},255),6},
                {{4,20},odh::reference_slot_entry_flags({target.x,target.y},target.direction,{4,20},255),2}};};
        Fixture live(true,1,1,generic);
        if(profile==0){live.table=circle_table();live.table.records[8].flags=255;
            live.route.set_continuous_ranged_slot_goal_provider([&](const auto& table,auto index,const auto&,const auto& target,auto angle){
                ++providers;std::vector<odh::PreRankedSlotGoal> goals;
                const auto all=odh::enumerate_source_slot_circle_candidates(table,index,target,*angle);
                for(const auto point:{odh::RoutePoint{20,4},odh::RoutePoint{4,20}}){
                    const auto found=std::find_if(all.begin(),all.end(),[&](const auto& value){return value.position==point;});
                    require(found!=all.end(),"shared circle fixture has the authored goal");
                    odh::PreRankedSlotGoal goal{found->position,found->entry_flags,found->facing};goal.radian_direction=found->radian_direction;
                    goals.push_back(goal);
                }return goals;});
        }else if(profile==1){live.table=range_table();
            live.route.set_authenticated_range_slot_goal_provider([&](const auto& table,auto,const auto&,const auto& target,float angle,const auto&){
                ++providers;return odh::AuthenticatedRangeSlotGoals{{range_goal(table,target,angle,{20,4}),
                    range_goal(table,target,angle,{4,20})},0,0,0};});
        }else{auto& slot=live.table.records[8];slot.offset_bits={bits(0.25f),bits(1.25f),0};
            slot.standing=1;slot.flags=255;slot.proximity=std::array<std::int32_t,3>{16,80,16};}
        const auto general=[&](const auto&,auto,const auto&,const auto& target,float,const auto&){
            ++providers;odh::AuthenticatedRangeSlotGoals result;result.failure_code=0;
            for(const auto point:{odh::RoutePoint{100,20},odh::RoutePoint{4,20}}){
                const auto entry=odh::reference_slot_entry_flags({target.x,target.y},target.direction,point,255);
                const auto facing=static_cast<std::uint8_t>(point.x==100?6:2);
                const auto angle=odh::source_game_object_radians(facing);
                result.goals.push_back({point,entry,facing,{},{},angle});
                result.semantic_goals.push_back({point,entry,facing,angle,false,{},1});
            }return result;};
        if(profile==3)live.route.set_authenticated_slot_goal_provider(general);
        float heading=0;install_sparse_services(live,heading);live.start();
        const auto before=live.route.capture_local_session_route(owner);
        require(before&&before->source_sparse&&providers==1,"ranked fallback freezes one source provider result");
        Fixture resumed(false);resumed.table=live.table;
        require(resumed.world.restore_snapshot(live.world.snapshot())&&resumed.vm.restore_snapshot(live.vm.snapshot()),
            "reopen the same frozen source fallback graph");
        float copied_heading=0;install_sparse_services(resumed,copied_heading);
        if(profile==3)resumed.route.set_authenticated_slot_goal_provider([](const auto&,auto,const auto&,const auto&,float,const auto&)->odh::AuthenticatedRangeSlotGoals{
            throw std::runtime_error("restored generic SoftFail cannot rerank");});
        require(resumed.route.restore_local_session_route(*before,owner)&&resumed.plan_calls==0,
            "restored source fallback does not plan or rank during load");
        live.initial_block=resumed.initial_block=odh::SourceSparsePlacementStatus::blocked_object;
        live.obstruct_plans=resumed.obstruct_plans=true;
        if(!live.route.tick(1))throw std::runtime_error("live shared SoftFail: "+live.route.diagnostic());
        if(!resumed.route.tick(1))throw std::runtime_error("resumed shared SoftFail: "+resumed.route.diagnostic());
        const auto after=live.route.capture_local_session_route(owner);
        const auto copied=resumed.route.capture_local_session_route(owner);
        require(after&&copied&&after->path.back()==odh::RoutePoint{4,20}&&copied->path.back()==after->path.back()&&
            after->source_sparse->collision&&after->source_sparse->collision->retries==4&&
            copied->source_sparse->collision->retries==4&&providers==1&&live.plan_calls==3&&resumed.plan_calls==2&&
            live.world.find(7)->position==resumed.world.find(7)->position&&
            !(odh::RoutePoint{live.world.find(7)->position->x,live.world.find(7)->position->y}==after->path.back()),
            "all ranked profiles consume frozen alternatives, preserve retries, replan on CONTINUE and never teleport or rerank");
        require(live.source_ticks==resumed.source_ticks&&live.plan_prefixes[2]>live.plan_prefixes[1],
            "collision SoftFail requests its next source Tick before the replacement AttemptWalk");
        live.finish(2);resumed.finish(2);
        require(live.world.find(7)->position==resumed.world.find(7)->position&&
            live.world.find(7)->radian_direction==resumed.world.find(7)->radian_direction&&providers==1,
            "live and reopened ranked fallback chains finish identically");
    }
    // The timeout prefix falls through in the SAME source entry. A selected
    // goal already at the actor leaves WalkTo null and the old CurrentPath.
    unsigned providers=0;
    odh::PreRankedSlotGoalProvider ranked=[&](const auto&,auto,const auto&,const auto& target){++providers;
        return std::vector<odh::PreRankedSlotGoal>{
            {{100,20},odh::reference_slot_entry_flags({target.x,target.y},target.direction,{100,20},255),6},
            {{4,12},odh::reference_slot_entry_flags({target.x,target.y},target.direction,{4,12},255),4}};};
    Fixture moving(true,1,1,ranked);auto& row=moving.table.records[8];row.offset_bits={bits(0.25f),bits(1.25f),0};
    row.standing=1;row.flags=255;row.proximity=std::array<std::int32_t,3>{16,80,16};
    require(moving.world.create(10,odh::EntityKind::avatar,{0,88,3,16})&&moving.world.set_position(10,{10,11,1,0})&&
        moving.world.declare_object_word(10,8,0),"create timeout's independently moving blocker");
    moving.initial_block=odh::SourceSparsePlacementStatus::blocked_avatar;moving.collision_blocker=10;
    float heading=0;install_sparse_services(moving,heading);moving.start();require(moving.route.tick(1),"enter a real source wait");
    const auto waiting=moving.route.capture_local_session_route(owner);
    require(waiting&&waiting->source_sparse&&waiting->source_sparse->collision,"real wait produces a stable policy snapshot");
    auto timeout=*waiting;timeout.source_sparse->collision->wait_time=1;timeout.source_sparse->collision->timeout=1;
    timeout.frame.wait_time=1;
    Fixture expired(false);expired.table=moving.table;
    require(expired.world.restore_snapshot(moving.world.snapshot())&&expired.vm.restore_snapshot(moving.vm.snapshot()),
        "restore the source wait just before timeout");
    float timeout_heading=0;install_sparse_services(expired,timeout_heading);expired.turns_ended=false;
    require(expired.route.restore_local_session_route(timeout,owner),"restore exact exhausted-timeout counters");
    const auto position=expired.world.find(7)->position;const auto old_motion=timeout.source_sparse->motion;
    if(!expired.route.tick(2))throw std::runtime_error("source timeout fallback: "+expired.route.diagnostic());
    std::string shortcut_rejection;
    const auto shortcut=expired.route.capture_local_session_route(owner,&shortcut_rejection);
    if(!shortcut)throw std::runtime_error("timeout at-position continuation capture: "+shortcut_rejection+
        "; active="+std::to_string(expired.route.active())+"; diagnostic="+expired.route.diagnostic());
    require(shortcut->source_sparse->walk_to_pending&&shortcut->source_sparse->lifecycle==2&&
        shortcut->frame.state==odh::RouteFrameState::end_turn,
        "timeout selects the at-position goal and retains WalkTo-null EndTurn");
    require(shortcut->source_sparse->motion.current&&
        shortcut->source_sparse->motion.move_frames==old_motion.move_frames&&
        shortcut->source_sparse->motion.move_total_frames==old_motion.move_total_frames&&shortcut->source_sparse->motion.velocity==0,
        "at-position AttemptWalk retains old CurrentPath counters and timeout velocity zero");
    const auto turning=expired.world.find(7)->position;
    require(turning&&position&&turning->x==position->x&&turning->y==position->y&&turning->level==position->level,
        "at-position timeout fallback keeps logical coordinates while the source EndTurn changes facing");
    require(expired.plan_calls==1&&expired.plan_prefixes.front()==1&&providers==1,
        "timeout replacement AttemptWalk falls through before another source prefix without reranking");
    Fixture copy(false);copy.table=expired.table;
    require(copy.world.restore_snapshot(expired.world.snapshot())&&copy.vm.restore_snapshot(expired.vm.snapshot()),
        "restore pending WalkTo-null turn World and VM");
    float copy_heading=0;install_sparse_services(copy,copy_heading);copy.turns_ended=false;
    require(copy.route.restore_local_session_route(*shortcut,owner)&&copy.plan_calls==0,
        "pending turn continuation restores old integrator and new goal without planning");
    expired.turns_ended=copy.turns_ended=true;
    auto arrived_position=*position;arrived_position.direction=shortcut->facing;
    require(expired.route.tick(3)&&copy.route.tick(3)&&!expired.route.active()&&!copy.route.active()&&
        expired.world.find(7)->position==arrived_position&&copy.world.find(7)->position==arrived_position&&
        expired.world.find(7)->radian_direction==*shortcut->arrival_radian_direction&&
        copy.world.find(7)->radian_direction==*shortcut->arrival_radian_direction&&
        expired.vm.run(8)==odh::VmStatus::returned_true&&copy.vm.run(8)==odh::VmStatus::returned_true&&
        expired.vm.temps()[0]==91&&copy.vm.temps()[0]==91,
        "TURN_ONLY with retained CurrentPath reaches exact new facing and authored true without advancing old motion");
}
void source_standing_before_walking(){
    for(const unsigned mode:{0U,1U,2U,3U,4U}){ // success, false, missing EP27, unbound sitting, architecture after EP27
        Fixture live;auto& row=live.table.records[8];row.standing=1;row.flags=255;
        require(live.world.create(11,odh::EntityKind::object,{0,0,1})&&live.world.define_type(11,111)&&
            live.world.set_position(11,{4,12,1,0})&&live.world.declare_object_word(11,8,0)&&
            live.world.declare_object_word(7,54,9),"source stand container and PrimitiveResultID metadata");
        std::vector<odh::EntryPoint> entries(28);entries[27].action=4101;
        require(live.world.define_entry_points(11,entries)&&live.world.set_person_data(7,0,1),"source sitting posture");
        if(mode!=3)require(live.world.move_to_slot(11,0,7),"source reciprocal seated avatar");
        const odh::Bhav stand{0x8002,{{99,1,255,{}},{2,static_cast<std::uint8_t>(mode==1?255:254),255,{2,0,55,0,0,5,8,7}}},{},odh::Bhav::Metadata{0,2,0,0}};
        require(live.vm.install_owner(11,{{odh::routine_key(4101),stand}}),"source exact stand resource bank");
        bool crossed_architecture_portal=false,architecture_changed=false;
        live.extra_primitive=[&](const auto&frame,const auto&,const auto&,const auto&){
            if(mode==4&&frame.context.callee==12){
                require(live.world.place_individual_on_floor(7,odh::EntityPosition{12,12,1,2},2),"new architecture portal has an actual placement effect");
                crossed_architecture_portal=true;return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            }
            if(mode==0||mode==4)require(live.world.place_individual_on_floor(7,odh::EntityPosition{8,12,1,2},2),"authored stand effect changes real actor pose");
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};};
        if(mode==4){
            require(live.world.create(12,odh::EntityKind::object)&&live.world.set_position(12,{12,12,1,0})&&live.world.declare_object_word(12,8,0)&&
                live.vm.install_owner(12,{{odh::routine_key(4100),{0x8002,{{99,254,255,{}}},{},odh::Bhav::Metadata{0,0,0,0}}}}),"independent architecture portal child");
            live.route.set_room_pair_provider([&](const auto&,const auto&){return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{std::uint16_t(architecture_changed?(crossed_architecture_portal?3:2):2),std::uint16_t(architecture_changed?3:2)}};});
            live.route.set_room_route_invalidation_provider([](auto){return true;});
            live.room_plan=[&](const auto&,const auto&)->std::optional<odh::SourceRoomRoutePlan>{return odh::SourceRoomRoutePlan{true,{{2,3,12,*live.world.find(12)->position}}};};
            live.route.set_room_portal_step_provider([&](auto,const auto&,const auto&,const auto&previous,const auto&result,auto){
                if(!previous)return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::call_ready,odh::VmCall{odh::routine_key(4100),{7,12,12,12},{},odh::VmCallReturnMode::ordinary,true},{}};
                require(result==true&&crossed_architecture_portal,"architecture room traversal uses its own portal result");
                return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::destination_reached,{},{}};});
        }
        float heading=0;install_sparse_services(live,heading);unsigned providers=0,stands=0;
        const auto provider=[&](const auto&,auto,const auto&,const auto&,float,const auto& context){
            require(context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch),"standing candidate selection uses genuine source lease");
            ++providers;odh::AuthenticatedRangeSlotGoals result;result.failure_code=0;
            result.goals={{{20,12},1,2,{},{},1.57079637f},{{20,20},1,2,{},{},1.57079637f}};
            result.semantic_goals={{{20,12},1,2,1.57079637f,false,{},1},{{20,20},1,2,1.57079637f,false,{},1}};return result;};
        live.route.set_authenticated_slot_goal_provider(provider);
        live.route.set_standing_action_provider([&](auto actor,auto chair)->std::optional<odh::VmCall>{
            ++stands;require(actor==7&&chair==11&&live.plan_calls==0,"actual container EP27 runs before any AttemptWalk");
            if(mode==2)return {};
            odh::VmCall call{odh::routine_key(4101),{actor,chair,chair,11},{0,0}};call.exact_arguments=true;return call;});
        live.start();std::string why;const auto before=live.route.capture_local_session_route(owner,&why);
        if(!before)throw std::runtime_error("pre-stand snapshot: "+why);
        require(odh::is_source_standing_before_walk(*before)&&!before->source_sparse&&live.plan_calls==0,
            "source choices are frozen while seated but no walking motion is fabricated");
        require(live.route.tick(1),"source standing phase first tick");
        if(mode==2){require(!live.route.active()&&live.plan_calls==0&&live.vm.run(8)==odh::VmStatus::returned_false,
            "unavailable EP27 follows source CantStand without moving or clearing containment");continue;}
        if(mode==3){require(stands==0&&live.world.find(7)->person_data[0]==0&&live.plan_calls==1,
            "unbound source sitting resets posture before ordinary AttemptWalk without invented chair");live.finish(2,160);continue;}
        require(live.vm.run(8)==odh::VmStatus::waiting&&live.vm.pending()->instruction.instruction.opcode==99,
            "real EP27 child reaches its own pending service");
        const auto middle=live.route.capture_session_route_stack(owner,&why);
        if(!middle)throw std::runtime_error("mid-stand capture: "+why);
        require(middle->jobs.back().external_frame_purpose==odh::RouteExternalFramePurpose::standing&&
            middle->jobs.back().external_frame_callee_generation==live.world.find(11)->generation&&
            middle->jobs.back().frame.state==odh::RouteFrameState::stand_func,"EP27 persists exact chair generation and stand phase");
        if(mode==4){
            architecture_changed=true;require(live.route.invalidate_room_routes(),"architecture changes while actual EP27 child is covered");
            const auto pending=live.vm.pending();require(pending&&live.vm.complete(pending->ticket,true)&&live.vm.run(8)==odh::VmStatus::yielded&&live.route.tick(2),"ACK old stand child before scheduling new architecture portal");
            const auto portal_state=live.route.capture_session_route_stack(owner,&why);
            if(!portal_state)throw std::runtime_error("post-EP27 architecture portal capture: "+why);
            require(portal_state->jobs.back().room_route_replan_active&&portal_state->jobs.back().external_frame_active&&
                !portal_state->jobs.back().external_frame_purpose&&portal_state->jobs.back().external_frame_callee_generation==0&&
                portal_state->jobs.back().external_frame_call->context.callee==12,
                "new portal discards ONLY the completed stand/shoo child classification while preserving real source goal");
            require(live.vm.run(8)==odh::VmStatus::yielded&&live.route.tick(3),"new portal returns through its own classification");
            live.finish(4,180);continue;
        }
        Fixture resumed(false);resumed.table=live.table;
        require(resumed.world.restore_snapshot(live.world.snapshot())&&resumed.vm.install_owner(11,{{odh::routine_key(4101),stand}})&&
            resumed.vm.restore_snapshot(live.vm.snapshot()),"fresh mid-stand World and VM");
        float copied_heading=0;install_sparse_services(resumed,copied_heading);
        resumed.route.set_authenticated_slot_goal_provider([](const auto&,auto,const auto&,const auto&,float,const auto&)->odh::AuthenticatedRangeSlotGoals{
            throw std::runtime_error("fresh mid-stand cannot rerank or consume RNG");});
        resumed.route.set_standing_action_provider([](auto,auto)->std::optional<odh::VmCall>{throw std::runtime_error("fresh mid-stand cannot repeat source condition");});
        require(resumed.route.restore_session_route_stack(*middle,owner,&why),why.c_str());
        auto stale=*middle;++stale.jobs.back().external_frame_callee_generation;
        Fixture rejected(false);rejected.table=live.table;
        require(rejected.world.restore_snapshot(live.world.snapshot())&&rejected.vm.install_owner(11,{{odh::routine_key(4101),stand}})&&
            rejected.vm.restore_snapshot(live.vm.snapshot()),"prepare malformed stand fresh execution");
        float ignored=0;install_sparse_services(rejected,ignored);rejected.route.set_authenticated_slot_goal_provider(provider);
        require(!rejected.route.restore_session_route_stack(stale,owner,&why),"stale chair generation cannot restore source EP27");
        const auto finish_child=[](Fixture& f){const auto pending=f.vm.pending();return pending&&f.vm.complete(pending->ticket,true)&&f.vm.run(8)==odh::VmStatus::yielded;};
        require(finish_child(live)&&finish_child(resumed),"stand child returns through genuine suspended route channel");
        if(mode==0){
            require(live.route.tick(2)&&resumed.route.tick(2)&&live.plan_calls==1&&resumed.plan_calls==1&&
                live.world.find(7)->person_data[0]==0&&!live.world.find(7)->attachment&&providers==1&&stands==1,
                "successful original/restored stand plans once from authored real pose and resets lingering Posture1");
            live.finish(3,160);resumed.finish(3,160);
            require(live.world.find(7)->position==resumed.world.find(7)->position&&live.vm.temps()==resumed.vm.temps(),
                "fresh stand resume matches uninterrupted route pose and VM outputs");
        }else{
            require(live.route.tick(2)&&live.route.external_frame_active()&&stands==2&&live.plan_calls==0&&providers==1,
                "source false stand retries next frozen goal without reranking or walking");
            require(live.vm.run(8)==odh::VmStatus::waiting&&finish_child(live)&&live.route.tick(3)&&!live.route.active()&&
                live.vm.run(8)==odh::VmStatus::returned_false&&live.world.find(7)->attachment,
                "exhausted CantStand preserves actual attachment and authored false branch");
        }
    }
}
void general_slot_source_choices(){
    for(const bool chair_returns_true:{true,false}){
        Fixture live;auto& row=live.table.records[8];row.offset_bits={0,0,0};row.flags=1;row.standing=1;row.sitting=1000;
        row.proximity=std::array<std::int32_t,3>{16,128,48};
        require(live.world.set_position(9,{20,48,1,0})&&live.world.create(11,odh::EntityKind::object)&&
            live.world.define_type(11,111)&&live.world.set_position(11,{20,32,1,4})&&live.world.declare_object_word(11,8,0),"general source chair resource and explicit collision flags");
        std::vector<odh::EntryPoint> entries(27);entries[26].action=4100;
        require(live.world.define_entry_points(11,entries),"source chair EP26");
        const odh::Bhav chair{0x8002,{{2,static_cast<std::uint8_t>(chair_returns_true?254:255),255,{2,0,55,0,0,5,8,7}}},{},odh::Bhav::Metadata{0,2,0,0}};
        require(live.vm.install_owner(11,{{odh::routine_key(4100),chair}}),"install actual immutable EP26 bank");
        float heading=0;install_sparse_services(live,heading);unsigned calls=0;
        const auto provider=[&](const auto&,auto,const auto&,const auto&,float,const odh::RoutePlacementContext& context){
            require(context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch),"full SLOT parser gets exact placement lease");++calls;
            odh::AuthenticatedRangeSlotGoals result;result.failure_code=0;
            const odh::SourceSlotChoiceGoal seated{{20,32},1,4,3.14159274f,false,odh::LiveAttemptWalkIdentity{11,live.world.find(11)->generation},1};
            const odh::SourceSlotChoiceGoal standing{{20,16},1,4,3.14159274f,false,{},1};
            result.semantic_goals={seated,standing};
            result.goals={{{20,32},1,4,11,{},3.14159274f},{{20,16},1,4,{},{},3.14159274f}};return result;};
        live.route.set_authenticated_slot_goal_provider(provider);
        live.route.set_sitting_action_provider([](std::int16_t actor,std::int16_t target)->std::optional<odh::VmCall>{
            odh::VmCall call{odh::routine_key(4100),{actor,target,target,11},{0,0}};call.exact_arguments=true;return call;});
        live.start();std::string why;const auto saved=live.route.capture_session_route_stack(owner,&why);
        require(saved&&saved->jobs.back().source_choices&&!saved->jobs.back().source_sparse&&live.plan_calls==0&&
            saved->jobs.back().path==std::vector<odh::RoutePoint>{{4,12}}&&saved->jobs.back().source_choices->selected.chair->id==11,
            "source chooses chair and preserves CurRoute before any AttemptWalk or movement");
        Fixture resumed(false);resumed.table=live.table;
        require(resumed.world.restore_snapshot(live.world.snapshot())&&resumed.vm.install_owner(11,{{odh::routine_key(4100),chair}})&&
            resumed.vm.restore_snapshot(live.vm.snapshot()),"fresh general chair World/VM restore");
        float resumed_heading=0;install_sparse_services(resumed,resumed_heading);
        resumed.route.set_authenticated_slot_goal_provider([](const auto&,auto,const auto&,const auto&,float,const auto&)->odh::AuthenticatedRangeSlotGoals{
            throw std::runtime_error("fresh restore must not rerun parser/RNG");});
        resumed.route.set_sitting_action_provider([](std::int16_t actor,std::int16_t target)->std::optional<odh::VmCall>{
            odh::VmCall call{odh::routine_key(4100),{actor,target,target,11},{0,0}};call.exact_arguments=true;return call;});
        require(resumed.route.restore_session_route_stack(*saved,owner,&why),why.c_str());
        auto malformed=*saved;++malformed.jobs.back().source_choices->selected.chair->generation;
        Fixture rejected(false);rejected.table=live.table;
        require(rejected.world.restore_snapshot(live.world.snapshot())&&rejected.vm.install_owner(11,{{odh::routine_key(4100),chair}})&&
            rejected.vm.restore_snapshot(live.vm.snapshot()),"prepare stale chair restore");
        float ignored=0;install_sparse_services(rejected,ignored);rejected.route.set_authenticated_slot_goal_provider(provider);
        require(!rejected.route.restore_session_route_stack(malformed,owner,&why),"changed chair generation cannot borrow a valid selected ID");
        if(!resumed.route.tick(1))throw std::runtime_error("fresh chair first tick: "+resumed.route.diagnostic()+"; VM "+resumed.vm.diagnostic());
        const auto resumed_status=resumed.vm.run(8);
        if(resumed_status!=odh::VmStatus::yielded)throw std::runtime_error("fresh chair child status "+std::string(odh::vm_status_name(resumed_status))+": "+resumed.vm.diagnostic());
        if(!resumed.route.tick(2))throw std::runtime_error("fresh chair return tick: "+resumed.route.diagnostic()+"; VM "+resumed.vm.diagnostic());
        require(live.route.intersects_destination({20,32},11)==std::optional<bool>{true},"generic chair destination consultation uses exact chair and semantic position");
        require(live.route.intersects_destination({1000,1000},11)==std::optional<bool>{true}&&
            live.route.intersects_destination({26,38})==std::optional<bool>{true}&&
            live.route.intersects_destination({27,38})==std::optional<bool>{false},
            "matching source chair wins independently of position; other candidates use inclusive rectangle");
        require(live.route.tick(1)&&live.vm.stack().back().key.id==4100&&live.plan_calls==0&&live.vm.stack().back().arguments==std::vector<std::int16_t>{0,0},
            "EP26 owns the actor before walking, using exact source argument extent");
        require(!live.route.capture_local_session_route(owner,&why)&&why.find("stack-aware")!=std::string::npos,
            "active EP26 cannot be flattened into a local-only route snapshot");
        const auto mid=live.route.capture_session_route_stack(owner,&why);
        require(mid&&mid->jobs.back().source_choices->chair_attempted&&mid->jobs.back().external_frame_sitting,
            "active EP26 saves its selected chair and genuine child continuation");
        Fixture midcopy(false);midcopy.table=live.table;
        require(midcopy.world.restore_snapshot(live.world.snapshot())&&midcopy.vm.install_owner(11,{{odh::routine_key(4100),chair}})&&
            midcopy.vm.restore_snapshot(live.vm.snapshot()),"fresh mid-EP26 World and VM restore");
        float midheading=0;install_sparse_services(midcopy,midheading);
        midcopy.route.set_authenticated_slot_goal_provider([](const auto&,auto,const auto&,const auto&,float,const auto&)->odh::AuthenticatedRangeSlotGoals{
            throw std::runtime_error("mid-EP26 restore must not rerun parser");});
        midcopy.route.set_sitting_action_provider([](auto,auto)->std::optional<odh::VmCall>{
            throw std::runtime_error("mid-EP26 restore must not repeat condition or schedule child");});
        auto parent_turn=*mid;odh::RouteFrameContext child_turn;
        require(odh::consume_route_portal_turn(child_turn,&parent_turn.jobs.back().frame)&&
            parent_turn.jobs.back().frame.portal_turns==1,"nested route consumes genuine EP26 parent turn allowance");
        require(midcopy.route.restore_session_route_stack(parent_turn,owner,&why),why.c_str());
        const auto restored_turn=midcopy.route.capture_session_route_stack(owner,&why);
        require(restored_turn&&restored_turn->jobs.back().frame.portal_turns==1,
            "native mid-EP26 restore retains parent turn state without walking motion");
        require(midcopy.vm.run(8)==odh::VmStatus::yielded&&midcopy.route.tick(2),
            "fresh mid-EP26 resumes existing child and exact return channel without checks or RNG");
        require(live.vm.run(8)==odh::VmStatus::yielded,"source EP26 returns through its external channel");
        require(live.route.tick(2),"source chair return resumes parent");
        if(chair_returns_true)require(!live.route.active()&&live.vm.run(8)==odh::VmStatus::returned_true&&live.plan_calls==0,
            "successful chair completes outer route without fabricated movement");
        else{
            require(live.route.active()&&live.plan_calls==1&&calls==1,"failed chair takes next frozen standing goal without rerunning provider/RNG");
            const auto fallback=live.route.capture_session_route_stack(owner,&why);
            require(fallback&&fallback->jobs.back().source_choices&&!fallback->jobs.back().source_choices->selected.chair&&
                !fallback->jobs.back().source_choices->chair_attempted&&fallback->jobs.back().source_sparse,
                "standing fallback keeps the same exact source manifest and real motion");
            live.finish(3,150);
        }
    }
}

void authenticated_slot_between_floors(){
    // The ordinary SLOT primitive that starts a chair interaction targets
    // the object before choosing any local standing or sitting position.
    enum class Case{crossed,no_connection,unknown_graph,private_changed,false_arrival,attached};
    for(auto scenario:{Case::crossed,Case::no_connection,Case::unknown_graph,Case::private_changed,Case::false_arrival,Case::attached}){
        odh::Instruction go{45,1,255,{1,0,2,0,0,0,0,0}};
        if(scenario==Case::private_changed)go.operands[2]=1;
        Fixture f(true,2,0,{},go);float heading=0;install_sparse_services(f,heading);
        require(f.world.set_position(7,{4,12,2,0})&&f.world.create(11,odh::EntityKind::object,{0,0,1})&&
            f.world.define_type(11,111)&&f.world.declare_object_word(11,8,0)&&f.world.set_position(11,{4,12,2,0}),"ordinary SLOT actor starts upstairs with a real portal object");
        std::vector<odh::EntryPoint> entries(16);entries[15].action=4100;
        require(f.world.define_entry_points(11,entries)&&f.vm.install_owner(11,{{odh::routine_key(4100),
            {0x8002,{{99,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}}}}),"install actual portal EP15 child bank");
        unsigned selections=0,portal_calls=0,refreshes=0;std::uint64_t parent_ticket=0;
        f.route.set_room_pair_provider([scenario](const auto& actor,const auto& target){
            const auto actor_room=scenario==Case::false_arrival?target.level+1:actor.level+1;
            return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{
                static_cast<std::uint16_t>(actor_room),static_cast<std::uint16_t>(target.level+1)}};
        });
        f.route.set_route_grid_refresh([&](const auto& actor){++refreshes;return actor.level==1;});
        f.route.set_room_portal_step_provider([&](auto ticket,const auto& actor,const auto& target,
                const auto& previous,const auto& result,auto step){
            ++portal_calls;
            require(ticket==parent_ticket&&target.level==1,"portal planning uses the original SLOT ticket and target");
            if(scenario==Case::unknown_graph)
                return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::unsupported,{},"incomplete portal inventory"};
            if(scenario==Case::no_connection)
                return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::unavailable,{},"no connection between floors"};
            if(scenario==Case::false_arrival||previous){
                if(previous)require(result==true&&step==1&&actor.level==1,"actual portal child returned on the target floor");
                return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::destination_reached,{},{}};
            }
            require(actor.level==2&&step==0&&!result,"cross-floor SLOT starts with a portal before goal selection");
            odh::VmCall call{odh::routine_key(f.world.find(11)->entry_points->at(15).action),{7,11,11,11},
                std::vector<std::int16_t>(4),odh::VmCallReturnMode::ordinary,true};
            require(f.world.bind_context_live(call.context),"portal call bound to the exact participant generations");
            return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::call_ready,call,{}};
        });
        f.route.set_authenticated_slot_goal_provider([&](const auto&,auto index,const auto& actor,const auto& target,
                float,const odh::RoutePlacementContext& context){
            ++selections;
            require(index==1&&actor.level==1&&target.level==1&&context.dispatch.ticket==parent_ticket&&
                context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch),
                "placement uses the original SLOT and ticket only after crossing floors");
            odh::AuthenticatedRangeSlotGoals goals;
            goals.failure_code=0;
            goals.goals={{{20,16},1,4,{},{},3.14159274f}};
            goals.semantic_goals={{{20,16},1,4,3.14159274f,false,{},1}};
            return goals;
        });
        f.extra_primitive=[&](const auto& frame,const auto&,const auto&,const auto&){
            require(frame.context.caller==7&&frame.context.callee==11&&frame.context.code_owner==11&&
                f.world.set_position(7,{12,12,1,0}),"only the executing stair child publishes the downstairs position");
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        };
        if(scenario==Case::attached){
            require(f.world.attach(11,0,7)&&f.vm.start(odh::routine_key(4116),{7,7,9,owner},{8})==odh::VmStatus::running&&
                f.vm.run(8)==odh::VmStatus::fault&&!f.route.active()&&!portal_calls&&!selections,
                "attachment without source sitting posture does not authorize a cross-floor route");
            continue;
        }
        f.start();parent_ticket=f.vm.pending()->ticket;
        require(!selections&&f.route.waiting_for_room_portal_step(),"destination is not scored on the actor's previous floor");
        if(scenario==Case::private_changed){
            f.table.records[1].flags|=0x0800U;
            require(!f.route.tick(1)&&f.route.active()&&f.vm.pending()->ticket==parent_ticket&&
                !portal_calls&&!selections,"changed private IgnoreRooms resource cannot dispatch a stair child");
            continue;
        }
        if(scenario==Case::unknown_graph){
            require(!f.route.tick(1)&&f.route.active()&&f.vm.pending()->ticket==parent_ticket&&
                !f.route.external_frame_active()&&f.world.find(7)->person_data[62]==0&&!selections,
                "incomplete portal inventory does not fabricate a route failure or consume the action ticket");
            continue;
        }
        if(scenario!=Case::crossed){
            require(f.route.tick(1)&&f.route.external_frame_active()&&f.vm.stack().back().key.id==398,
                "unreachable or unconfirmed floor takes the authored failure tree");
            if(scenario==Case::no_connection)require(f.vm.stack().back().arguments[0]==2&&f.world.find(7)->person_data[62]==2,"missing stairs reports NoRoomRoute");
            require(f.vm.run(8)==odh::VmStatus::yielded&&f.route.tick(2)&&f.vm.run(8)==odh::VmStatus::returned_false&&
                !f.route.active()&&!selections&&f.world.find(7)->position->level==2,"failure leaves actor upstairs and returns false");
            continue;
        }
        const auto saved=f.route.capture_session_route_stack(owner);
        require(saved&&saved->jobs.size()==1&&saved->jobs.front().ticket==parent_ticket&&saved->jobs.front().deferred_room_route,
            "deferred cross-floor request preserves the original parent ticket on save");
        auto parked=*saved;
        odh::RoutePortalContextSnapshot context;context.ticket=parent_ticket;parked.portal_contexts.push_back(context);
        Fixture fresh(false,2,0,{},go);
        require(fresh.world.restore_snapshot(f.world.snapshot())&&fresh.vm.restore_snapshot(f.vm.snapshot()),
            "fresh load retains the actual pending upstairs SLOT frame");
        float fresh_heading=0;install_sparse_services(fresh,fresh_heading);
        fresh.route.set_route_grid_refresh([](const auto&){return true;});
        fresh.route.set_authenticated_slot_goal_provider([](const auto&,auto,const auto&,const auto&,float,const auto&)
            ->odh::AuthenticatedRangeSlotGoals{throw std::runtime_error("restore cannot score a goal");});
        fresh.route.set_room_portal_step_provider([](auto,const auto&,const auto&,const auto&,const auto&,auto)
            ->odh::RoutePortalStepPlan{throw std::runtime_error("restore cannot run a portal");});
        auto mismatched=parked;++mismatched.jobs.front().ticket;
        std::string why;
        require(!fresh.route.restore_session_route_stack(mismatched,owner,&why)&&!fresh.route.active(),
            "mismatched saved frame ticket cannot authorize the upstairs transition");
        fresh.table.records[1].flags|=0x0800U;
        require(!fresh.route.restore_session_route_stack(parked,owner,&why)&&!fresh.route.active(),
            "real pending ticket cannot restore stairs for an IgnoreRooms global SLOT");
        fresh.table.records[1].flags&=~0x0800U;
        require(fresh.route.restore_session_route_stack(parked,owner,&why)&&fresh.vm.pending()->ticket==parent_ticket,
            "valid pending cross-floor SLOT reopens without running a portal or scoring a goal");
        require(f.route.tick(1)&&f.route.external_frame_active(),"first stair schedules the EP15 child");
        require(f.vm.run(8)==odh::VmStatus::yielded,"stair child returns to its waiting SLOT parent");
        const auto resumed=f.route.tick(2);
        require(resumed&&selections==1&&portal_calls==2&&refreshes==1,("stair arrival replays local SLOT once: resumed="+
            std::to_string(resumed)+" selections="+std::to_string(selections)+" plans="+std::to_string(portal_calls)+
            " refreshes="+std::to_string(refreshes)+" diagnostic="+f.route.diagnostic()+" vm="+f.vm.diagnostic()).c_str());
        f.finish(3);
        require(f.world.find(7)->position->level==1&&f.world.find(7)->position->x==20&&f.world.find(7)->position->y==16,
            "ordinary SLOT reaches its actual downstairs destination");
    }
}

void architecture_room_portal_restore(){
    const odh::Bhav portal{0x8002,{{99,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}};
    Fixture live;bool edited=false,crossed=false;float heading=0;unsigned resets=0;
    require(live.world.create(11,odh::EntityKind::object)&&live.world.set_position(11,{12,12,1,0})&&live.world.declare_object_word(11,8,0)&&
        live.vm.install_owner(11,{{odh::routine_key(4100),portal}}),"architecture replan has an actual independent portal bank");
    live.route.set_room_pair_provider([&](const auto&,const auto&){return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{std::uint16_t(edited?(crossed?3:2):2),std::uint16_t(edited?3:2)}};});
    live.route.set_room_route_invalidation_provider([&](auto){++resets;return true;});
    live.room_plan=[&](const auto&,const auto&)->std::optional<odh::SourceRoomRoutePlan>{return odh::SourceRoomRoutePlan{true,{{2,3,11,*live.world.find(11)->position}}};};
    auto call=odh::VmCall{odh::routine_key(4100),{7,11,11,11},std::vector<std::int16_t>(4),odh::VmCallReturnMode::ordinary,true};
    require(live.world.bind_context_live(call.context),"portal fixture captures the actual generation-bound call");
    live.route.set_room_portal_step_provider([&](auto,const auto&,const auto&,const auto&previous,const auto&result,auto){
        if(!previous)return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::call_ready,call,{}};
        require(result==true&&crossed,"architecture portal arrival follows the genuine child return");return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::destination_reached,{},{}};});
    live.extra_primitive=[&](const auto&,const auto&,const auto&,const auto&){require(live.world.set_position(7,{12,12,1,0}),"actual portal child moves actor");crossed=true;return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};};
    install_sparse_services(live,heading);live.start();std::uint64_t tick=1;
    for(;tick<20;++tick){auto state=live.route.capture_local_session_route(owner);if(state&&state->frame.state==odh::RouteFrameState::walking)break;require(live.route.tick(tick),"portal fixture finishes begin turn");}
    edited=true;require(live.route.invalidate_room_routes()&&live.route.tick(tick),"source invalidation schedules new room traversal");
    std::string why;auto saved=live.route.capture_session_route_stack(owner,&why);
    if(!saved)throw std::runtime_error("architecture midportal capture: "+why);
    require(saved->jobs.back().room_route_replan_active&&saved->jobs.back().external_frame_active&&resets==1,
        "new room traversal suspends the original CurRoute under the real portal child");
    saved->portal_contexts.push_back({saved->jobs.back().ticket,{},odh::RoomPortalEdge{2,3,11,*live.world.find(11)->position},call,live.world.find(11)->generation});
    Fixture fresh(false);fresh.table=live.table;bool cold_crossed=false;float cold_heading=0;
    require(fresh.world.restore_snapshot(live.world.snapshot())&&fresh.vm.install_owner(11,{{odh::routine_key(4100),portal}})&&fresh.vm.restore_snapshot(live.vm.snapshot()),"fresh World/VM preserves exact active portal child");
    fresh.route.set_room_pair_provider([&](const auto&,const auto&){return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{std::uint16_t(cold_crossed?3:2),3}};});
    fresh.route.set_room_route_invalidation_provider([](auto){throw std::runtime_error("load or post-ACK must not repeat consumed room invalidation");return false;});
    fresh.route.set_room_portal_step_provider([&](auto,const auto&,const auto&,const auto&previous,const auto&result,auto){
        require(previous.has_value()&&result==true&&cold_crossed,"restored room traversal uses exact prior child receipt");return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::destination_reached,{},{}};});
    fresh.extra_primitive=[&](const auto&,const auto&,const auto&,const auto&){require(fresh.world.set_position(7,{12,12,1,0}),"restored actual portal child moves actor");cold_crossed=true;return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};};
    install_sparse_services(fresh,cold_heading);
    const auto architecture_restored=fresh.route.restore_session_route_stack(*saved,owner,&why);
    if(!architecture_restored)throw std::runtime_error("architecture portal restore: "+why);
    auto forged=*saved;forged.jobs.back().pending.ticket++;
    Fixture invalid(false);invalid.table=live.table;require(invalid.world.restore_snapshot(live.world.snapshot())&&invalid.vm.install_owner(11,{{odh::routine_key(4100),portal}})&&invalid.vm.restore_snapshot(live.vm.snapshot()),"malformed event fixture original VM");
    float invalid_heading=0;install_sparse_services(invalid,invalid_heading);invalid.route.set_room_route_invalidation_provider([](auto){return true;});invalid.route.set_room_portal_step_provider([](auto,const auto&,const auto&,const auto&,const auto&,auto){return odh::RoutePortalStepPlan{};});
    require(!invalid.route.restore_session_route_stack(forged,owner),"architecture traversal cannot forge original route ticket");
    const auto warm_return=live.vm.run(8),cold_return=fresh.vm.run(8);
    if(warm_return!=odh::VmStatus::yielded||cold_return!=odh::VmStatus::yielded)
        throw std::runtime_error("architecture portal child return warm="+std::string(odh::vm_status_name(warm_return))+" cold="+odh::vm_status_name(cold_return)+"; warm VM "+live.vm.diagnostic()+"; cold VM "+fresh.vm.diagnostic());
    if(!live.route.tick(tick+1))throw std::runtime_error("warm architecture portal ACK: "+live.route.diagnostic()+"; VM "+live.vm.diagnostic());
    if(!fresh.route.tick(tick+1))throw std::runtime_error("fresh architecture portal ACK: "+fresh.route.diagnostic()+"; VM "+fresh.vm.diagnostic());
    for(auto now=tick+2;now<180&&(live.route.active()||fresh.route.active());++now){if(live.route.active())require(live.route.tick(now),"warm architecture portal continuation");if(fresh.route.active())require(fresh.route.tick(now),"fresh architecture portal continuation");}
    require(!live.route.active()&&!fresh.route.active()&&live.vm.run(8)==odh::VmStatus::returned_true&&fresh.vm.run(8)==odh::VmStatus::returned_true&&live.world.find(7)->position==fresh.world.find(7)->position,
        "new architecture room traversal and fresh midchild restore reach the same original goal");
}
void architecture_room_invalidation(){
    Fixture live;float heading=0;install_sparse_services(live,heading);
    unsigned resets=0;live.route.set_room_route_invalidation_provider([&](auto ticket){++resets;return live.vm.pending()&&ticket==live.vm.pending()->ticket;});
    live.room_plan=[](const auto&context,const auto&)->std::optional<odh::SourceRoomRoutePlan>{
        require(!context.room_context_open,"source architecture reset precedes pure room graph");return odh::SourceRoomRoutePlan{true,{}};};
    live.start();std::uint64_t next=1;
    for(;next<20;++next){const auto state=live.route.capture_local_session_route(owner);
        if(state&&state->frame.state==odh::RouteFrameState::walking)break;
        require(live.route.tick(next),"finish source begin turn before architecture edit");
    }
    require(next<20,"source route reaches actual walking state");
    const auto before=live.route.capture_local_session_route(owner);const auto ticks=live.source_ticks,plans=live.plan_calls;
    require(before&&before->source_sparse&&live.route.invalidate_room_routes(),"mark current source frame invalid at an external event boundary");
    const auto marked=live.route.capture_local_session_route(owner);
    require(marked&&marked->room_route_invalid&&marked->path==before->path&&marked->next==before->next&&
        live.source_ticks==ticks&&live.plan_calls==plans,"invalidation changes only source flag, not RNG, CurrentPath or execution");
    Fixture fresh(false);fresh.table=live.table;
    require(fresh.world.restore_snapshot(live.world.snapshot())&&fresh.vm.restore_snapshot(live.vm.snapshot()),"fresh invalidated route World/VM");
    float new_heading=0;install_sparse_services(fresh,new_heading);unsigned cold_resets=0;
    fresh.route.set_room_route_invalidation_provider([&](auto){++cold_resets;return true;});fresh.room_plan=live.room_plan;
    require(fresh.route.restore_local_session_route(*marked,owner),"fresh native restore retains unprocessed architecture flag");
    const auto restored=fresh.route.capture_local_session_route(owner);
    require(restored&&restored->room_route_invalid&&cold_resets==0&&fresh.plan_calls==0,"load does not process event or replan eagerly");
    require(live.route.tick(next)&&fresh.route.tick(next),"both actual routing ticks process the same architecture event");
    require(resets==1&&cold_resets==1&&live.room_plans==1&&fresh.room_plans==1&&live.plan_calls==plans+1&&fresh.plan_calls==1,
        "architecture event clears ignored rooms and replans same selected goal exactly once");
    const auto warm=live.route.capture_local_session_route(owner),cold=fresh.route.capture_local_session_route(owner);
    require(warm&&cold&&!warm->room_route_invalid&&!cold->room_route_invalid&&warm->path.back()==before->path.back()&&
        cold->path.back()==before->path.back()&&live.world.find(7)->position==fresh.world.find(7)->position,
        "source event keeps CurRoute and both continuations agree");
    for(auto tick=next+1;tick<150&&(live.route.active()||fresh.route.active());++tick){
        if(live.route.active())require(live.route.tick(tick),"live invalidation continuation");
        if(fresh.route.active())require(fresh.route.tick(tick),"fresh invalidation continuation");
    }
    require(!live.route.active()&&!fresh.route.active()&&live.vm.run(8)==odh::VmStatus::returned_true&&fresh.vm.run(8)==odh::VmStatus::returned_true&&
        live.world.find(7)->position==fresh.world.find(7)->position,"warm/fresh invalidated route reaches original authored endpoint");
    odh::RouteStackSnapshot nested;nested.jobs.resize(3);for(std::size_t i=0;i<3;++i){nested.jobs[i].ticket=i+1;nested.jobs[i].path={{int(i),1}};}
    odh::invalidate_session_room_routes(nested);
    require(std::all_of(nested.jobs.begin(),nested.jobs.end(),[](const auto&job){return job.room_route_invalid;})&&nested.jobs[2].path.front()==odh::RoutePoint{2,1},
        "architecture snapshot event covers all suspended frames without inventing paths");
    Fixture unknown;float unknown_heading=0;install_sparse_services(unknown,unknown_heading);unknown.start();
    require(unknown.route.invalidate_room_routes(),"missing authority case marks without effects");
    // Deferred turns are processed first, as source excludes BEGIN/END_TURN.
    for(unsigned tick=1;tick<20&&unknown.route.active();++tick){
        if(!unknown.route.tick(tick))break;
    }
    require(unknown.route.active()&&unknown.route.diagnostic().find("providers are unavailable")!=std::string::npos,
        "missing event provider stays explicit unsupported, never a free or failed authored route");
}

struct UnplacedRouteFixture {
    odh::World world;odh::SlotTable table=source_table();odh::Routing route{world,table};odh::Vm vm;
    unsigned source_ticks=0,room_reads=0,root_exits=0,nested_exits=0;bool room_proved=true,exit_supported=true;
    std::vector<std::int16_t> failure_arguments;
    static odh::RoutineSet failure_program(bool available,bool nested){
        if(!available)return {};
        std::vector<odh::Instruction> instructions;
        if(nested)instructions.push_back({45,255,1,{8,0,1,0,0,0,0,0}});
        instructions.push_back({2,static_cast<std::uint8_t>(nested?2:1),255,{1,0,73,0,0,5,8,7}});
        instructions.push_back({99,254,255,{}});
        return {{odh::routine_key(398),{0x8002,std::move(instructions),{},odh::Bhav::Metadata{0,4,4,0}}}};
    }
    explicit UnplacedRouteFixture(std::uint8_t flags=0,bool populate=true,bool failure_available=true,bool nested=false):
        vm(failure_program(failure_available,nested),world.vm_memory(),{},
            [this](const auto& f,const auto& i,const auto& t,const auto& d){
                if(i.opcode==99){failure_arguments=f.arguments;return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};}
                return route.primitive()(f,i,t,d);},{{owner,program(1,flags)}}){
        if(populate)require(world.create(7,odh::EntityKind::avatar,{0,88,3,16})&&world.create(9,odh::EntityKind::object,{0,0,1})&&
            world.set_position(9,{20,20,1,0})&&world.declare_object_word(7,8,0),"unplaced avatar has actual identity, flags and a positioned target");
        route.set_stack_object_slot_table_provider([this](const auto&){return &table;});
        route.set_unplaced_room_failure_provider([this](const auto& target)->std::optional<std::uint16_t>{
            ++room_reads;require(target==*world.find(9)->position,"room proof uses real target pose");
            return room_proved?std::optional<std::uint16_t>{2}:std::nullopt;
        });
        const auto valid=[this](const odh::SourceSparseMotionContext& context){
            const auto* actor=world.find(7);
            if(!actor||context.actor_generation!=actor->generation||context.actor_unplaced!=!actor->position)return false;
            if(context.lifecycle_instruction)return vm.matches_active_primitive(context.frame,*context.lifecycle_instruction,context.dispatch);
            return !vm.dispatch_active()&&vm.pending()&&context.dispatch.ticket==vm.pending()->ticket;
        };
        odh::SourceSparseServices services;
        services.source_tick=[this,valid](const auto& context){require(valid(context),"FAILED tick has real pending or active primitive authority");++source_ticks;return true;};
        services.end_locomotion=[this,valid](const auto& context,const auto&,std::string* error){
            require(valid(context),"PreExit is lifecycle authority, not a positioned-motion callback");
            if(!exit_supported){if(error)*error="fixture source PreExit dependency unknown";return false;}
            if(context.root_route){++root_exits;return world.set_static_footprint(7,true);}++nested_exits;return true;
        };
        route.set_source_sparse_services(std::move(services));require(route.bind(vm),"bind the same native Routing without a geometry allocation");
    }
    odh::VmStatus start(){require(vm.start(odh::routine_key(4116),{7,7,9,owner},{8})==odh::VmStatus::running,"start exact original route owner");return vm.run(12);}
};
void unplaced_same_run_failure_timing(){
    for(const bool nested:{false,true}){
        UnplacedRouteFixture live(0,true,true,nested);const auto spatial=live.world.spatial_revision();
        const auto status=live.start();
        const bool admitted=status==odh::VmStatus::waiting&&live.route.active()&&live.route.external_frame_active()&&
            live.vm.stack().back().key.id==398&&live.route.active_frame_context()->state==odh::RouteFrameState::failed&&
            live.vm.pending()->instruction.instruction.opcode==99&&live.vm.temps()[1]==73&&
            live.failure_arguments==std::vector<std::int16_t>{2,0,0,0}&&live.world.find(7)->person_data[62]==2&&
            live.source_ticks==(nested?1U:0U)&&live.nested_exits==(nested?1U:0U)&&live.root_exits==0&&
            !live.route.has_grid()&&!live.world.find(7)->position&&live.world.spatial_revision()==spatial+1;
        // World deliberately invalidates derived spatial data for person-bank
        // writes too: mandatory RouteResult0->2 is exactly one such change.
        if(!admitted){
            std::string args;for(const auto arg:live.failure_arguments)args+=std::to_string(arg)+",";
            throw std::runtime_error("source398 first-yield timing mismatch nested="+std::to_string(nested)+
                " status="+odh::vm_status_name(status)+" vm="+live.vm.diagnostic()+" routing="+live.route.diagnostic()+
                " active="+std::to_string(live.route.active())+" external="+std::to_string(live.route.external_frame_active())+
                " top="+std::to_string(live.vm.stack().empty()?0:live.vm.stack().back().key.id)+
                " pending_opcode="+std::to_string(live.vm.pending()?live.vm.pending()->instruction.instruction.opcode:0)+
                " temp1="+std::to_string(live.vm.temps()[1])+" args="+args+
                " route_result="+std::to_string(live.world.find(7)->person_data[62])+
                " rng="+std::to_string(live.source_ticks)+" root_exits="+std::to_string(live.root_exits)+
                " nested_exits="+std::to_string(live.nested_exits)+" has_grid="+std::to_string(live.route.has_grid())+
                " positioned="+std::to_string(live.world.find(7)->position.has_value())+
                " revision="+std::to_string(live.world.spatial_revision())+" before="+std::to_string(spatial));
        }
        require(admitted,"398 effects and any nested no-child FAILED tick execute before the first genuine child yield, without a host route tick");
        std::string why;const auto saved=live.route.capture_session_route_stack(owner,&why);
        if(!saved)throw std::runtime_error("same-run failure398 capture: "+why);
        require(saved->jobs.size()==1&&odh::is_source_unplaced_room_failure(saved->jobs.front())&&
            !live.route.intersects_destination({20,20}).value_or(true),"yielded source failure has no CurRoute or invented destination reservation");
        // Static admission proof also serves staged older pending snapshots;
        // exercise its own identity guards without borrowing an active owner.
        if(!nested){
            auto state=live.vm.snapshot();state.stack.resize(1);state.pending=saved->jobs.front().pending;
            state.suspended_pending.clear();state.external_frame_return.reset();state.status=odh::VmStatus::waiting;
            UnplacedRouteFixture staged(0,false);
            require(staged.world.restore_snapshot(live.world.snapshot())&&staged.vm.restore_snapshot(state),"prepare exact staged source parent VM for static proof validation");
            auto proof=saved->jobs.front();proof.failure_pending=true;proof.failure_tree_invoked=false;
            proof.external_frame_active=false;proof.external_frame_owner=0;proof.external_frame_call.reset();
            require(odh::Routing::validates_immediate_parent(staged.world,staged.vm,*state.pending,state.stack.front(),&proof),"exact staged source witness admits its own unplaced pending frame");
            for(unsigned field=0;field<3;++field){auto wrong=proof;
                if(field==0)++wrong.ticket;if(field==1)++wrong.run_generation;if(field==2)++wrong.target_generation;
                require(!odh::Routing::validates_immediate_parent(staged.world,staged.vm,*state.pending,state.stack.front(),&wrong),
                    "static API independently rejects wrong witness ticket, run and live target generation");
            }
        }
        UnplacedRouteFixture cold(0,false,true,nested);
        require(cold.world.restore_snapshot(live.world.snapshot())&&cold.vm.restore_snapshot(live.vm.snapshot()),"rebuild actual unplaced World and yielded398 VM");
        auto bad=*saved;bad.jobs.front().unplaced_room_failure->slot.flags|=0x0800;
        require(!cold.route.restore_session_route_stack(bad,owner,&why),"IgnoreRooms cannot borrow a NoRoomRoute witness");
        bad=*saved;bad.jobs.front().unplaced_room_failure->slot_index++;
        require(!cold.route.restore_session_route_stack(bad,owner,&why),"changed exact SLOT index cannot restore");
        const auto previous=cold.table.records[8].standing;cold.table.records[8].standing++;
        require(!cold.route.restore_session_route_stack(*saved,owner,&why),"changed installed SLOT bytes cannot restore a valid numeric owner");
        cold.table.records[8].standing=previous;
        require(cold.route.restore_session_route_stack(*saved,owner,&why)&&cold.vm.temps()[1]==73&&!cold.route.has_grid(),"cold restore resumes actual yielded child without readmission or artificial geometry");
        for(auto* fixture:{&live,&cold}){
            require(fixture->world.set_person_data(7,62,9)&&fixture->vm.complete(fixture->vm.pending()->ticket,true)&&
                fixture->vm.run(8)==odh::VmStatus::yielded&&fixture->route.active()&&fixture->root_exits==0,
                "source398 return stops at the source-proven external boundary before FAILED RNG and PreExit");
            require(fixture->route.tick(1)&&fixture->vm.run(8)==odh::VmStatus::returned_false&&!fixture->route.active()&&
                fixture->root_exits==1&&fixture->world.find(7)->person_data[62]==9&&!fixture->world.find(7)->position,
                "next owning routing tick runs root PreExit even unplaced and preserves authored child effects");
        }
        require(live.source_ticks==(nested?2U:1U)&&cold.source_ticks==1,"cold restore consumes only remaining FAILED tick RNG, never replays nested history");
    }
    UnplacedRouteFixture disabled(1,true,false);
    require(disabled.start()==odh::VmStatus::returned_false&&!disabled.route.active()&&!disabled.vm.pending()&&
        disabled.source_ticks==1&&disabled.root_exits==1&&disabled.failure_arguments.empty()&&
        disabled.world.find(7)->person_data[62]==2&&!disabled.world.find(7)->position,
        "NoFailureTrees performs legitimate transient FAILED RNG/PreExit/false in the initial run with no wait or398 dependency");
    UnplacedRouteFixture missing(0,true,false);
    require(missing.start()==odh::VmStatus::fault&&missing.route.active()&&!missing.route.external_frame_active()&&
        !missing.vm.pending()&&missing.source_ticks==0&&missing.root_exits==0&&
        missing.vm.diagnostic().find("failure tree398")!=std::string::npos,
        "missing required398 fails atomic admission explicitly, never yields an artificial route wait or authored false");
    UnplacedRouteFixture unsupported_exit(1);unsupported_exit.exit_supported=false;
    require(unsupported_exit.start()==odh::VmStatus::fault&&unsupported_exit.route.active()&&
        unsupported_exit.vm.diagnostic().find("PreExit dependency unknown")!=std::string::npos,
        "unadmitted PreExit capability stays an explicit source service failure rather than skipped cleanup");
    for(unsigned unsupported=0;unsupported<3;++unsupported){
        UnplacedRouteFixture unknown;
        if(unsupported==0)unknown.room_proved=false;
        if(unsupported==1)unknown.table.records[8].flags|=0x0800;
        if(unsupported==2)require(unknown.world.attach(9,0,7),"use an actual contained unplaced avatar");
        require(unknown.start()==odh::VmStatus::fault&&!unknown.route.active()&&unknown.world.find(7)->person_data[62]!=2,
            "unknown room, IgnoreRooms and contained absence remain explicit unsupported rather than invented room failure");
    }
}
void unplaced_child_interruption_and_placement(){
    UnplacedRouteFixture live;const auto child_owner=owner+1;
    const odh::RoutineSet child{{odh::routine_key(4100),{0x8002,
        {{2,254,255,{2,0,77,0,0,5,8,7}}},{},odh::Bhav::Metadata{0,4,4,0}}}};
    require(live.vm.install_owner(child_owner,child)&&live.start()==odh::VmStatus::waiting,"source398 reaches its genuine first child wait");
    const auto pending=*live.vm.pending();
    const odh::VmCall call{odh::routine_key(4100),{7,9,9,child_owner},{}};
    const auto immediate=live.vm.inject_immediate(call,[&](const auto& p,const auto& frame){
        return p.ticket==pending.ticket&&p.instruction.instruction.opcode==99&&frame.context.caller==7;
    });
    require(immediate&&live.route.parked_for_immediate()&&live.route.tick(1)&&live.source_ticks==0,"real immediate child covers398 without ticking its failed routing parent");
    std::string why;const auto saved=live.route.capture_session_route_stack(owner,&why);
    if(!saved)throw std::runtime_error("source398 immediate capture: "+why);
    UnplacedRouteFixture cold(0,false);
    require(cold.vm.install_owner(child_owner,child)&&cold.world.restore_snapshot(live.world.snapshot())&&
        cold.vm.restore_snapshot(live.vm.snapshot())&&cold.route.restore_session_route_stack(*saved,owner,&why),"cold restore retains failed parent, real398 and genuine immediate child together");
    odh::RouteGrid warm_grid{64,64},cold_grid{64,64};
    for(auto* fixture:{&live,&cold}){
        require(fixture->vm.run(8)==odh::VmStatus::yielded&&fixture->vm.temps()[2]==77&&fixture->vm.resume_immediate(*immediate)&&
            fixture->vm.complete(fixture->vm.pending()->ticket,true)&&fixture->vm.run(8)==odh::VmStatus::yielded,
            "shared immediate registers and398 return resume exactly once through existing VM ownership");
        require(fixture->world.set_position(7,{4,12,1,0})&&fixture->world.set_person_data(7,62,9),"source placement may happen while failure child is retained");
    }
    require(live.route.attach_grid(warm_grid)&&cold.route.attach_grid(cold_grid),"same failed-route owner accepts actual later geometry, without replacing the controller");
    for(auto* fixture:{&live,&cold})require(fixture->route.tick(2)&&fixture->vm.run(8)==odh::VmStatus::returned_false&&
        fixture->source_ticks==1&&fixture->root_exits==1&&fixture->world.find(7)->person_data[62]==9&&
        fixture->world.find(7)->position==std::optional<odh::EntityPosition>{{4,12,1,0}},
        "warm and cold FAILED continuation preserves actual later placement and child effects");
}

}
int main(){try{unplaced_same_run_failure_timing();unplaced_child_interruption_and_placement();failed_attempt_retains_curroute();authenticated_slot_between_floors();architecture_room_portal_restore();architecture_room_invalidation();source_standing_before_walking();general_slot_source_choices();immediate_routing_interruption();immediate_routing_service();authenticated_transient_relative_routes();deferred_relative_facing();parked_collision_failure_tree();ordinary_go_here_sparse();authenticated_circle_routes();face_anywhere_live_range_continuation();source_soft_failure_room_graph();source_soft_failure_alternatives();authenticated_range_routes();range_retry_and_replay();range_callback_boundaries();range_failure_metadata();source_circle_routes();generic_ranked_sparse_choices();geometry_tests();collision_routes();execution_tests();self_anchor_tests();sparse_point_uses_complete_room_plan();std::cout<<checks<<" source exact SLOT routing checks passed\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
