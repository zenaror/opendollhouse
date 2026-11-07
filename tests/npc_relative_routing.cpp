#include "odh/routing.hpp"
#include "odh/source_walk_plan.hpp"
#include "odh/slot_scoring.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
constexpr odh::CodeOwner owner=0x8000004fU;
odh::RoutineSet program(std::uint8_t direction=255,std::uint8_t flags=0,std::uint8_t location=254) {
    // Exact PedPortal4100pc1 operand shape, independent of NPC/resource IDs.
    odh::Instruction route{27,1,255,{0,0,254,direction,0,0,flags,0}};
    route.operands[2]=location;
    if(location!=254){route.operands[0]=123;route.operands[1]=45;route.operands[4]=67;route.operands[5]=89;}
    odh::Instruction marker{2,254,255,{0,0,91,0,0,5,8,7}};
    return {{odh::routine_key(4100),{0x8002,{route,marker},{},odh::Bhav::Metadata{0,4,4,0}}}};
}
struct Fixture {
    odh::World world;odh::RouteGrid grid{96,96};odh::SlotTable slots{};
    odh::Routing routing{world,grid,slots};
    odh::RandomState random{7};
    odh::Vm vm;
    Fixture(std::uint8_t direction=255,std::uint8_t flags=0,bool populate=true,std::uint8_t location=254,odh::EntityKind target_kind=odh::EntityKind::object)
        :vm({{odh::routine_key(398),{0x8002,{{2,254,255,{1,0,73,0,0,5,8,7}}},{},
            odh::Bhav::Metadata{0,4,4,0}}}},world.vm_memory(),{},routing.primitive(),{{owner,program(direction,flags,location)}}) {
        if(populate)require(world.create(7,odh::EntityKind::avatar,{0,88,3,16})&&
            world.create(9,target_kind,target_kind==odh::EntityKind::avatar?odh::EntityLayout{0,88,3,16}:odh::EntityLayout{})&&world.set_position(7,{4,12,1,1})&&
            world.set_position(9,{12,12,1,3}),"positioned actual avatar and portal target");
        routing.set_route_goal_validator([this](const auto&,auto point){return grid.contains(point)&&grid.walkable(point);});
        routing.set_room_pair_provider([](const auto&,const auto&){
            return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{2,2}};});
        routing.set_continuous_ranged_slot_goal_provider([this](const auto& table,auto index,const auto& caller,const auto& target,std::optional<float> angle){
            return odh::rank_reference_range_slot(table,index,caller,target,random,false,{},{},{},{},angle);
        });
        require(routing.bind(vm),"bind relative route controller");
    }
    void start(){require(vm.start(odh::routine_key(4100),{7,7,9,owner})==odh::VmStatus::running,
        "start relative route");const auto status=vm.run(8);
        if(status!=odh::VmStatus::waiting)throw std::runtime_error("relative route did not wait: "+vm.diagnostic());
        require(vm.temps()[0]==0,"relative route suspends before the following instruction");}
    void finish(std::uint64_t first=1){for(auto tick=first;tick<140&&routing.active();++tick)
        require(routing.tick(tick),"advance pending local relative movement");
        require(!routing.active()&&vm.run(8)==odh::VmStatus::returned_true&&vm.temps()[0]==91,
            "relative route completes authored true branch only after arrival");}
};
void install_sparse_services(Fixture& fixture,float& begin_heading){
    require(fixture.world.declare_object_word(7,8,0),"declare source actor collision flags");
    require(fixture.world.declare_object_word(9,8,0),"declare source target collision flags");
    const auto actor_generation=fixture.world.find(7)->generation;
    const auto target_generation=fixture.world.find(9)->generation;
    odh::SourceWalkDomain domain;domain.state=odh::LiveAttemptWalkState::ready;
    domain.request.actor={7,actor_generation};domain.request.room=2;domain.request.level=1;
    domain.request.room_bounds={0,0,128,128};domain.request.architecture_complete=true;
    domain.request.entities_complete=true;domain.request.avatars_to_consider_complete=true;
    domain.request.entities={{7,actor_generation},{9,target_generation}};
    domain.request.architectural_and_bounds={{-32,-32,2080,-16},{-32,2064,2080,2080},
        {-32,-32,-16,2080},{2064,-32,2080,2080}};
    domain.services.inventory_still_matches=[](const auto&){return true;};
    domain.services.footprint=[](const auto&){return odh::LiveAttemptWalkFootprint{odh::LiveAttemptWalkFootprintState::absent,{}};};
    odh::SourceSparseServices services;
    services.capture_avatars_to_consider=[](const auto& context,const auto& inherited)
        ->std::optional<std::vector<odh::LiveAttemptWalkIdentity>>{
        if(!context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch,context.phase))return {};
        return inherited.value_or(std::vector<odh::LiveAttemptWalkIdentity>{});
    };
    services.plan_input=[actor_generation,domain](const auto& context,odh::RoutePoint goal,std::optional<float> outgoing,auto inherited)
        ->std::optional<odh::SourceSparsePlanInput>{
        if(!context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch,context.phase))return {};
        odh::SourceSparsePlanInput input;input.profile={false,88,false,0};
        input.avatars_to_consider=inherited.value_or(std::vector<odh::LiveAttemptWalkIdentity>{});
        input.request.actor={7,actor_generation};input.request.destination={goal.x,goal.y};
        input.request.bezier=false;input.request.outgoing_direction=outgoing;
        input.services.context_still_matches=[context]{return context.proof.matches_after_restore(
            context.frame,*context.vm,context.instruction,context.dispatch,context.phase);};
        input.services.room_at=[](const auto&){return std::optional<std::uint16_t>{2};};
        input.services.domain=[domain](auto){return domain;};
        return input;
    };
    services.place=[](const auto&,const auto&)->std::optional<odh::SourceSparsePlacementOutcome>{
        return odh::SourceSparsePlacementOutcome{odh::SourceSparsePlacementStatus::success,{},{},{}};
    };
    services.begin_locomotion=[&](const auto& context,const auto&,auto*){
        begin_heading=fixture.world.find(context.actor)->radian_direction;return true;};
    services.end_locomotion=[](const auto&,const auto&,auto*){return true;};
    services.publish_motion=[](const auto&,const auto&,const auto&,auto*){return true;};
    fixture.routing.set_source_sparse_services(std::move(services));
}
}
int main(){try {
    Fixture continuous;continuous.start();
    require(continuous.routing.tick(1)&&continuous.world.find(7)->position->x==5,
        "NPC route moves toward actual portal instead of treating primitive as no-op");
    const auto route=continuous.routing.capture_local_session_route(owner);
    require(route&&route->entry_flags==0&&route->face_anywhere&&route->call_failure_trees,
        "source OnTopOf AnyDirection retains failure-tree policy in continuation");
    const auto world=continuous.world.snapshot();const auto vm=continuous.vm.snapshot();
    Fixture resumed(255,0,false);
    require(resumed.world.restore_snapshot(world)&&resumed.vm.restore_snapshot(vm)&&
        resumed.routing.restore_local_session_route(*route,owner),
        "fresh graph restores source relative route without rerunning primitive");
    auto malformed=*route;malformed.call_failure_trees=false;
    Fixture invalid(255,0,false);
    require(invalid.world.restore_snapshot(world)&&invalid.vm.restore_snapshot(vm)&&
        !invalid.routing.restore_local_session_route(malformed,owner),
        "saved relative flags cannot silently disable authored failure trees");
    continuous.finish(2);resumed.finish(2);
    require(continuous.world.find(7)->position==resumed.world.find(7)->position&&
        continuous.vm.steps()==resumed.vm.steps()&&continuous.vm.snapshot().ticket_sequence==resumed.vm.snapshot().ticket_sequence&&
        continuous.world.find(7)->person_data[37]==0&&continuous.world.find(7)->position->x==12,
        "split continuation preserves final position, route flags, steps and tickets");
    for(const auto direction:{std::uint8_t{0},std::uint8_t{4},std::uint8_t{7}}) {
        Fixture relative(direction,2);relative.start();relative.finish();
        require(relative.world.find(7)->position->direction==((3+direction)&7),
            "NoFailureTrees exact route applies relative target-facing offset");
    }
    {
        Fixture no_failure(255,2);no_failure.start();require(no_failure.routing.tick(1),"advance NoFailureTrees route");
        const auto checkpoint=no_failure.routing.capture_local_session_route(owner);
        require(checkpoint&&checkpoint->same_room_verified&&!checkpoint->call_failure_trees,
            "NoFailureTrees retains same-room proof and snapshot availability");
        Fixture copy(255,2,false);
        require(copy.world.restore_snapshot(no_failure.world.snapshot())&&copy.vm.restore_snapshot(no_failure.vm.snapshot())&&
            copy.routing.restore_local_session_route(*checkpoint,owner),
            "NoFailureTrees local route restores through its independent policy");
        no_failure.finish(2);copy.finish(2);
    }
    for(const auto flags:{std::uint8_t{0},std::uint8_t{2}}) {
        Fixture portal(255,flags);
        portal.routing.set_room_pair_provider([](const auto&,const auto&){
            return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,2}};});
        unsigned plans=0;
        portal.routing.set_room_portal_step_provider([&](auto,const auto&,const auto&,const auto&,const auto&,auto){
            ++plans;return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::unavailable,{},"no portal path"};});
        portal.start();require(portal.routing.waiting_for_room_portal_step()&&portal.routing.tick(1)&&plans==1,
            "cross-room geometry requests portal plan independently of failure-tree flag");
        if(flags==0)require(portal.vm.stack().size()==2&&portal.vm.stack().back().key.id==398,
            "failed deferred portal invokes authored global398 when failure trees are enabled");
        else require(!portal.routing.active()&&portal.vm.status()==odh::VmStatus::returned_false&&portal.vm.temps()[1]==0,
            "NoFailureTrees portal failure returns false without running global398");
    }
    Fixture facing(254,3);facing.start();facing.finish();
    require(facing.world.find(7)->position->direction==4,
        "OnTopOf Facing follows source atan2 zero-vector facing and ignored AllowDiffAlt");
    for(const auto location:{std::uint8_t{0},std::uint8_t{1},std::uint8_t{2},std::uint8_t{3},
            std::uint8_t{4},std::uint8_t{5},std::uint8_t{6},std::uint8_t{7},std::uint8_t{255}}) {
        for(const auto direction:{std::uint8_t{254},std::uint8_t{255},std::uint8_t{0},std::uint8_t{4}}) {
            Fixture range(direction,2,true,location);
            require(range.world.set_position(7,{42,48,1,0})&&range.world.set_position(9,{48,48,1,3}),
                "position relative range participants");
            range.start();range.finish();
            const auto arrived=*range.world.find(7)->position;
            const auto mask=static_cast<std::uint8_t>(location==255?255:1U<<location);
            require(odh::reference_slot_entry_flags({48,48},3,{arrived.x,arrived.y},mask)!=0,
                "asynchronous relative walk reaches authored sector with ignored legacy counters");
            if(direction<=7)require(arrived.direction==((3+direction)&7),
                "arrival facing follows independent direction operand");
        }
    }
    for(const auto flags:{std::uint8_t{0},std::uint8_t{2}}) {
        Fixture rejected_range(255,flags,true,4);
        rejected_range.routing.set_ranged_slot_goal_provider([](const auto&,auto,const auto&,const auto&){
            return std::vector<odh::PreRankedSlotGoal>{};
        });
        require(rejected_range.vm.start(odh::routine_key(4100),{7,7,9,owner})==odh::VmStatus::running,
            "start relative search with no eligible destinations");
        const auto status=rejected_range.vm.run(8);
        if(flags==2)require(status==odh::VmStatus::returned_false&&!rejected_range.routing.active(),
            "empty relative search follows source false branch with NoFailureTrees");
        else require(status==odh::VmStatus::waiting&&rejected_range.routing.tick(1)&&
                rejected_range.vm.stack().back().key.id==398,
            "empty relative search invokes the authored failure tree when enabled");
    }
    {
        Fixture angled(255,2,true,0,odh::EntityKind::avatar);
        require(angled.world.set_position(7,{64,64,1,0})&&angled.world.set_position(9,{48,80,1,0})&&
            angled.world.set_radian_direction(9,0.31f),"position target between discrete facing notches");
        angled.grid.block({48,64}); // source scores this north candidate before rejecting its obstacle
        angled.start();angled.finish();
        require(angled.world.find(7)->position->x==64&&angled.world.find(7)->position->y==64,
            "NPC uses eligible northeast candidate admitted by target continuous angle");
        odh::RandomState expected(7);expected.next(1024);expected.next(1024);
        require(angled.random.seed()==expected.seed(),"VM route preserves continuous-sector scoring draw count");
    }
    {
        Fixture legacy(255,2,true,0,odh::EntityKind::avatar);
        require(legacy.world.set_position(7,{64,64,1,0})&&legacy.world.set_position(9,{48,80,1,0})&&
            legacy.world.set_radian_direction(9,0.31f),"position non-notch target with legacy provider");
        unsigned calls=0;const auto seed=legacy.random.seed();
        legacy.routing.set_ranged_slot_goal_provider([&](const auto&,auto,const auto&,const auto&){
            ++calls;return std::vector<odh::PreRankedSlotGoal>{};
        });
        require(legacy.vm.start(odh::routine_key(4100),{7,7,9,owner})==odh::VmStatus::running&&
            legacy.vm.run(8)==odh::VmStatus::fault&&calls==0&&legacy.random.seed()==seed&&
            !legacy.routing.active(),"legacy provider cannot silently lose continuous sectors or consume RNG");
    }
    Fixture absent;
    require(absent.vm.start(odh::routine_key(4100),{7,7,30,owner})==odh::VmStatus::running&&
        absent.vm.run(8)==odh::VmStatus::returned_false&&!absent.routing.active(),
        "null StackObject follows source false branch before requesting movement");
    {
        Fixture sparse(255,2);
        require(sparse.world.set_position(7,{4,12,1,2})&&sparse.world.set_position(9,{100,12,1,0})&&
            sparse.world.set_radian_direction(7,1.57079632679f),
            "place exact GotoRelative destination beyond the synthetic route grid");
        float begin_heading=0;install_sparse_services(sparse,begin_heading);
        sparse.start();
        const auto saved=sparse.routing.capture_local_session_route(owner);
        require(saved&&saved->source_sparse&&saved->path.back()==odh::RoutePoint{100,12},
            "exact opcode27 uses source sparse planning outside the old grid");
        require(sparse.routing.tick(1)&&std::abs(begin_heading-saved->source_sparse->motion.target_direction)<0.0001,
            "opcode27 BeginWalk starts with the initial route heading");
        for(std::uint64_t tick=2;sparse.routing.active()&&tick<300;++tick)
            require(sparse.routing.tick(tick),"advance exact opcode27 sparse movement");
        require(!sparse.routing.active()&&sparse.vm.run(8)==odh::VmStatus::returned_true&&
            sparse.world.find(7)->position->x==100&&sparse.world.find(7)->position->y==12,
            "opcode27 sparse route completes without grid clipping");
    }
    Fixture blocked(255,2);blocked.grid.block({12,12});
    require(blocked.vm.start(odh::routine_key(4100),{7,7,9,owner})==odh::VmStatus::running&&
        blocked.vm.run(8)==odh::VmStatus::returned_false&&!blocked.routing.active(),
        "rejected exact goal returns authored false when failure trees are disabled");
    std::cout<<checks<<" NPC relative routing checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
