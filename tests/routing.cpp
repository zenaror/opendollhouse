#include "odh/routing.hpp"
#include "odh/route_frame.hpp"
#include "odh/slot_scoring.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks=0;
void require(bool v,const char* message) {++checks;if(!v)throw std::runtime_error(message);}
odh::SlotTable slot();
void reference_slot_lattice_tests() {
    odh::SlotRecord ranged{};ranged.type=3;ranged.flags=0xA0FF;
    ranged.proximity=std::array<std::int32_t,3>{16,64,16};ranged.resolution=8;
    std::vector<odh::SlotRecord> records(10);
    records[0].type=0;
    for(std::size_t i=1;i<9;++i) records[i].type=3;
    records[9]=ranged;
    const odh::SlotTable table{0,10,records,0};
    const auto offsets=odh::enumerate_reference_slot_range_lattice(table,8);
    require(offsets.size()==289,"Global type-3 index 8 range profile has 17x17 reference offsets");
    require(offsets.front()==odh::RoutePoint{-64,-64} && offsets[16]==odh::RoutePoint{-64,64} &&
            offsets[17]==odh::RoutePoint{-56,-64} && offsets.back()==odh::RoutePoint{64,64},
            "reference lattice preserves x-outer/y-inner inclusive order");
    const auto proximity=odh::filter_reference_slot_range_proximity(table,8,offsets);
    require(proximity.size()==204,"index 8 proximity band filters 289 offsets to 204 Euclidean candidates");
    require(proximity.front()==odh::RoutePoint{-64,-8} && proximity.back()==odh::RoutePoint{64,8},
            "proximity filtering preserves the source enumeration order");
    auto square=table;square.records[9].flags|=(1U<<30);
    const auto square_candidates=odh::filter_reference_slot_range_proximity(square,8,offsets);
    require(square_candidates.size()==280,"FSOSquare uses the max-axis distance band");
    bool limited=false;
    try { (void)odh::enumerate_reference_slot_range_lattice(table,8,288); }
    catch(const std::length_error&) { limited=true; }
    require(limited,"reference lattice enforces candidate allocation budget");
    bool invalid=false;
    auto malformed=table;malformed.records[9].resolution=0;
    try { (void)odh::enumerate_reference_slot_range_lattice(malformed,8); }
    catch(const std::invalid_argument&) { invalid=true; }
    require(invalid,"reference lattice rejects zero resolution");
    invalid=false;malformed=table;malformed.records[9].proximity=std::array<std::int32_t,3>{64,16,16};
    try { (void)odh::enumerate_reference_slot_range_lattice(malformed,8); }
    catch(const std::invalid_argument&) { invalid=true; }
    require(invalid,"reference lattice rejects reversed proximity range");
    invalid=false;malformed=table;malformed.version=9;
    try { (void)odh::enumerate_reference_slot_range_lattice(malformed,8); }
    catch(const std::invalid_argument&) { invalid=true; }
    require(invalid,"reference lattice rejects incomplete version profile");
    invalid=false;malformed=table;malformed.records[9].proximity=std::array<std::int32_t,3>{16,0,16};
    try { (void)odh::enumerate_reference_slot_range_lattice(malformed,8); }
    catch(const std::invalid_argument&) { invalid=true; }
    require(invalid,"max-zero fallback to min rejects a non-ranged profile");
}
void reference_slot_direction_tests() {
    constexpr std::uint8_t all=0xFF;
    require(odh::reference_slot_entry_flags({160,160},0,{176,160},all)==0x04,
            "east goal maps to the reference EAST direction bit");
    require(odh::reference_slot_entry_flags({160,160},0,{176,144},all)==0x02,
            "diagonal goal uses the reference angular sectors");
    require(odh::reference_slot_entry_flags({160,160},1,{176,144},all)==0x01,
            "candidate sectors rotate relative to the target orientation");
    require(odh::reference_slot_entry_flags({392,456},0,{403,445},all)==0x02,
            "candidate classification uses floored tile coordinates from 1/16 positions");
    require(odh::reference_slot_entry_flags({-1,-1},0,{0,-1},all)==0x04,
            "negative lattice coordinates use mathematical floor rather than truncation");
    require(odh::reference_slot_entry_flags({160,160},0,{176,160},0)==0,
            "candidate rejected when the SLOT direction mask allows no matched sector");
}
void reference_slot_circle_tests() {
    const auto candidates=odh::enumerate_reference_slot_circle_candidates(slot(),1,{392,456},0);
    require(candidates.size()==8,"exact-radius SLOT profile enumerates a bounded eight-position ring");
    require(candidates.front().position==odh::RoutePoint{392,440}&&
            candidates[2].position==odh::RoutePoint{408,456},
            "circle candidates preserve N/NE/E ordering around target");
    require(candidates[0].entry_flags==0x01&&candidates[2].entry_flags==0x04,
            "circle candidates report reference mask bits for each position");
    auto malformed=slot();malformed.records[1].proximity=std::array<std::int32_t,3>{16,16,32};
    bool rejected=false;
    try {(void)odh::enumerate_reference_slot_circle_candidates(malformed,1,{0,0},0);}
    catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"circle model fails closed for anisotropic proximity fields");
    malformed=slot();malformed.records[1].offset_bits[0]=1;rejected=false;
    try {(void)odh::enumerate_reference_slot_circle_candidates(malformed,1,{0,0},0);}
    catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"circle model fails closed for an unsupported SLOT offset");
}
void pre_ranked_goal_tests() {
    odh::RouteGrid grid(9,3);
    const std::vector<odh::PreRankedSlotGoal> goals={{{8,1},1,5},{{1,1},4,2}};
    auto result=odh::find_pre_ranked_slot_route(grid,{0,1},goals);
    require(result.path.status==odh::RouteStatus::found&&result.selected==0&&
            result.path.points.back()==odh::RoutePoint{8,1},"ranked farther preference wins over nearest destination");
    const std::vector<odh::PreRankedSlotGoal> ties={{{1,2},4,2},{{1,0},1,5}};
    result=odh::find_pre_ranked_slot_route(grid,{0,1},ties);
    require(result.selected==0&&result.path.points.back()==ties[0].position,
            "equal distance goals retain provider tie order rather than grid index order");
    grid.block({8,1});
    result=odh::find_pre_ranked_slot_route(grid,{0,1},goals);
    require(result.path.status==odh::RouteStatus::found&&result.selected==1,
            "definitively blocked goal falls through to next preference");
    grid.block({8,1},false);
    for(int y=0;y<3;++y) grid.wall({4,y},{5,y});
    const auto failed=odh::find_route(grid,{0,1},{{8,1}});
    result=odh::find_pre_ranked_slot_route(grid,{0,1},goals);
    require(failed.status==odh::RouteStatus::no_path&&result.selected==1&&
            result.path.expanded>failed.expanded,"unreachable first goal retries and accumulates expanded work");
    result=odh::find_pre_ranked_slot_route(grid,{0,1},goals,{failed.expanded,100});
    require(result.path.status==odh::RouteStatus::budget_exhausted&&!result.selected&&
            result.path.expanded==failed.expanded,"next attempt cannot reset cumulative expansion budget");
    result=odh::find_pre_ranked_slot_route(grid,{0,1},goals,{1,100});
    require(result.path.status==odh::RouteStatus::budget_exhausted&&!result.selected,
            "unfinished first search never falls through to a reachable second goal");
    result=odh::find_pre_ranked_slot_route(grid,{0,1},goals,{100,0});
    require(result.path.status==odh::RouteStatus::budget_exhausted&&!result.selected,
            "frontier exhaustion never falls through");
    result=odh::find_pre_ranked_slot_route(grid,{0,1},goals,{100,100,1});
    require(result.path.status==odh::RouteStatus::budget_exhausted&&!result.selected,
            "candidate count is bounded before ordered searches");
    result=odh::find_pre_ranked_slot_route(grid,{0,1},{{{9,1},1,0},goals[1]});
    require(result.path.status==odh::RouteStatus::invalid_input&&!result.selected,
            "out of grid preferred candidate stops rather than masquerading as no path");
    result=odh::find_pre_ranked_slot_route(grid,{0,1},{{{8,1},1,8},goals[1]});
    require(result.path.status==odh::RouteStatus::invalid_input&&!result.selected,
            "unsupported facing stops before fallback");
    result=odh::find_pre_ranked_slot_route(grid,{0,1},{{{8,1},0,0},goals[1]});
    require(result.path.status==odh::RouteStatus::invalid_input&&!result.selected,
            "empty candidate entry flags stop before fallback");
}
void door_range_route_tests() {
    auto doors=slot();
    doors.records[0].flags=0x54;doors.records[0].facing=-2;
    doors.records[0].proximity=std::array<std::int32_t,3>{16,32,16};
    odh::World world;odh::RouteGrid grid{64,64};auto globals=slot();globals.version=9;
    odh::RandomState random{0};
    odh::PreRankedSlotGoalProvider provider=[&](const auto& table,std::size_t index,
        const auto& actor,const auto& target) {
        if(index!=0)return std::vector<odh::PreRankedSlotGoal>{};
        return odh::rank_reference_door_range_slot(table,index,actor,target,random);
    };
    odh::Routing routing{world,grid,globals};
    routing.set_door_range_goal_provider(provider);
    routing.set_stack_object_slot_table_provider([&](const odh::VmFrame& frame)->const odh::SlotTable* {
        return frame.context.stack_object==2?&doors:nullptr;
    });
    routing.set_route_goal_validator([&](const odh::VmFrame& frame,odh::RoutePoint point) {
        const auto* actor=world.find(frame.context.caller);const auto* target=world.find(frame.context.stack_object);
        return actor&&target&&actor->position&&target->position&&
            actor->position->level==target->position->level&&grid.contains(point)&&grid.walkable(point);
    });
    odh::RoutineSet route_code{{odh::routine_key(4096),odh::Bhav{0x8002,
        {{45,254,255,{0,0,0,0,1,0,0,0}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
    odh::Vm vm{std::move(route_code),world.vm_memory(),{},routing.primitive()};
    world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
    world.set_position(1,{4,20,1,0});world.set_position(2,{28,20,1,0});grid.block({28,20});
    routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2},{0});
    require(vm.run(10)==odh::VmStatus::waiting&&routing.active()&&random.seed()==0,
        "Door range Route45 consumes explicit RNG and schedules its highest-ranked clear goal");
    for(std::uint64_t tick=1;tick<=32&&routing.active();++tick)routing.tick(tick);
    require(vm.status()==odh::VmStatus::returned_true&&!routing.active()&&
        world.find(1)->position->x==12&&world.find(1)->position->y==20&&
        world.find(1)->person_data[37]==0x40,
        "Door range Route45 reaches the ranked west approach while preserving entry flags");
}
void route_frame_context_tests() {
    odh::RouteFrameContext root;
    require(!odh::route_incoming_direction(root,nullptr,0.25f),
            "root route without portal state has no incoming direction");
    root.portal_turns=1;
    require(odh::route_incoming_direction(root,nullptr,0.25f)==0.25f,
            "root PortalTurns enables continuous incoming direction");
    root.portal_turns=0;root.state=odh::RouteFrameState::room_portal;
    require(odh::route_incoming_direction(root,nullptr,-0.5f)==-0.5f,
            "root room portal state enables signed continuous direction");
    require(odh::consume_route_portal_turn(root,nullptr)&&root.portal_turns==1,
            "first portal turn is allowed and increments the route counter");
    require(!odh::consume_route_portal_turn(root,nullptr)&&root.portal_turns==2,
            "later portal turns are rejected but remain counted");

    odh::RouteFrameContext child;
    odh::RouteFrameContext parent;parent.state=odh::RouteFrameState::room_portal;
    require(odh::route_incoming_direction(child,&parent,1.25f)==1.25f,
            "nested route inherits its parent's room portal state");
    require(odh::consume_route_portal_turn(child,&parent)&&parent.portal_turns==1,
            "nested route consumes the parent's one portal-turn allowance");
    require(!odh::consume_route_portal_turn(child,&parent)&&parent.portal_turns==2,
            "nested route increments the effective parent counter on repeated checks");

    parent.state=odh::RouteFrameState::initial;parent.portal_turns=0;
    child.state=odh::RouteFrameState::room_portal;
    require(!odh::route_incoming_direction(child,&parent,1.25f),
            "nested route consults parent context instead of its own portal state");
    require(!odh::route_incoming_direction(root,nullptr,std::numeric_limits<float>::quiet_NaN()),
            "non-finite avatar angle is never published as route context");
}
odh::SlotTable slot() {
    odh::SlotRecord r{};r.type=3;r.standing=1;r.flags=0x55;r.snap_target_slot=-1;
    r.proximity=std::array<std::int32_t,3>{16,16,16};r.max_size=0;r.unknown_i10=0;
    r.gradient_bits=0x3E400000;r.height=0;r.facing=-2;r.resolution=16;
    // Route45 data=1 selects the second type-3 record. Record zero is a
    // different profile, matching the order in the live Global SLOT100.
    auto other=r;other.flags=0x8055;other.proximity=std::array<std::int32_t,3>{16,48,16};
    return {0,10,{other,r},0};
}
odh::RoutineSet routines(std::uint8_t scope=2,std::uint8_t flags=0,std::uint8_t data=1) {
    return {{odh::routine_key(4096),odh::Bhav{0x8002,{{45,254,255,{data,0,scope,0,flags,0,0,0}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
}
odh::RoutineSet goto_relative(std::array<std::uint8_t,8> operands={0,0,254,0,0,0,2,0}) {
    return {{odh::routine_key(4096),odh::Bhav{0x8002,
        {{27,254,255,operands}},{},odh::Bhav::Metadata{0,4,0,0}}}};
}
struct Fixture {
    odh::World world;odh::RouteGrid grid{64,32};odh::SlotTable slots=slot();
    odh::Routing routing{world,grid,slots};
    odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive()};
    Fixture(odh::PreRankedSlotGoalProvider provider={},odh::RouteLimits limits={})
        :routing(world,grid,slots,std::move(provider),limits) {
        world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
        world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});grid.block({28,12});routing.bind(vm);
    }
    void start() {vm.start(odh::routine_key(4096),{1,2,2});vm.run(10);}
};
}
int main() {
    try {
        reference_slot_lattice_tests();
        reference_slot_direction_tests();
        reference_slot_circle_tests();
        pre_ranked_goal_tests();
        door_range_route_tests();
        route_frame_context_tests();
        {odh::World world;odh::RouteGrid grid{64,32};auto globals=slot();globals.version=9;
         auto object_slots=slot();odh::Routing routing{world,grid,globals};
         routing.set_stack_object_slot_table_provider([&](const odh::VmFrame&) -> const odh::SlotTable* {
             return &object_slots;
         });
         routing.set_route_goal_validator([](const odh::VmFrame&,odh::RoutePoint){return false;});
         odh::Vm vm{routines(1,1),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});routing.bind(vm);
         vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(10)==odh::VmStatus::returned_false&&
                 routing.diagnostic().find("host goal validator rejected every direction-compatible SLOT candidate")==0,
                 "Route45 distinguishes host-rejected SLOT goals from direction-mask rejection");}
        {odh::World world;odh::RouteGrid grid{32,32};auto globals=slot();odh::Routing routing{world,grid,globals};
         odh::Vm vm{goto_relative(),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{12,12,1,3});
         routing.set_route_goal_validator([&](const odh::VmFrame& frame,odh::RoutePoint p) {
             const auto* actor=world.find(frame.context.caller);const auto* target=world.find(frame.context.stack_object);
             return actor&&target&&actor->position&&target->position&&actor->position->level==target->position->level&&
                 p==odh::RoutePoint{target->position->x,target->position->y}&&grid.contains(p)&&grid.walkable(p);
         });routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(4)==odh::VmStatus::waiting&&routing.active()&&world.find(1)->position->x==4,
                 "bounded GotoRelativePosition creates an asynchronous exact StackObject route");
         for(std::uint64_t tick=1;tick<=8&&routing.active();++tick)routing.tick(tick);
         require(vm.status()==odh::VmStatus::returned_true&&!routing.active()&&
                 world.find(1)->position->x==12&&world.find(1)->position->y==12&&
                 world.find(1)->position->direction==3&&world.find(1)->person_data[37]==0,
                 "OnTopOf reaches the validated exact point with SameDirection and zero entry flags");}
        {odh::World world;odh::RouteGrid grid{32,32};auto globals=slot();odh::Routing routing{world,grid,globals};
         auto operands=std::array<std::uint8_t,8>{0,0,254,4,0,0,2,0};
         odh::Vm vm{goto_relative(operands),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{12,12,1,3});
         routing.set_route_goal_validator([&](const odh::VmFrame& frame,odh::RoutePoint p) {
             const auto* actor=world.find(frame.context.caller);const auto* target=world.find(frame.context.stack_object);
             return actor&&target&&actor->position&&target->position&&actor->position->level==target->position->level&&
                 p==odh::RoutePoint{target->position->x,target->position->y}&&grid.contains(p)&&grid.walkable(p);
         });routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(4)==odh::VmStatus::waiting&&routing.active(),
                 "bounded GotoRelativePosition accepts the commercial OpposingDirection profile");
         for(std::uint64_t tick=1;tick<=8&&routing.active();++tick)routing.tick(tick);
         require(vm.status()==odh::VmStatus::returned_true&&world.find(1)->position->direction==7,
                 "OpposingDirection turns the avatar four eighth-turns from the target facing");}
        {odh::World world;odh::RouteGrid grid{32,32};auto globals=slot();odh::Routing routing{world,grid,globals};
         odh::Vm vm{goto_relative(),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{12,12,1,0});grid.block({12,12});
         routing.set_route_goal_validator([&](const odh::VmFrame&,odh::RoutePoint p){return grid.contains(p)&&grid.walkable(p);});
         routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(4)==odh::VmStatus::returned_false&&!routing.active()&&world.find(1)->position->x==4,
                 "blocked exact StackObject point fails without starting or bypassing the route");
         require(routing.diagnostic()=="GotoRelativePosition target rejected by host room/clearance validator",
                 "blocked GotoRelativePosition reports the host room/clearance rejection distinctly");}
        {odh::World world;odh::RouteGrid grid{32,32};auto globals=slot();odh::Routing routing{world,grid,globals};
         odh::Vm vm{goto_relative(),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{40,12,1,0});
         routing.set_route_goal_validator([](const odh::VmFrame&,odh::RoutePoint){return true;});
         routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(4)==odh::VmStatus::returned_false&&
                 routing.diagnostic()=="GotoRelativePosition target lies outside the active route grid",
                 "GotoRelativePosition reports a target outside the active route grid");}
        {odh::World world;odh::RouteGrid grid{32,32};auto globals=slot();odh::Routing routing{world,grid,globals};
         auto operands=std::array<std::uint8_t,8>{0,0,254,0,0,0,0,0};
         odh::Vm vm{goto_relative(operands),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{12,12,1,0});routing.bind(vm);
         vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(4)==odh::VmStatus::fault&&!routing.active()&&world.find(1)->position->x==4,
                 "GotoRelativePosition with failure-tree policy outside the supported profile is rejected");}
        {odh::World world;odh::RouteGrid grid{64,32};auto globals=slot();globals.version=9;
         auto object_slots=slot();bool provider_seen=false;
         odh::PreRankedSlotGoalProvider goals=[&](const auto& table,std::size_t index,const auto&,const auto&) {
             provider_seen=&table==&object_slots&&index==1;
             return std::vector<odh::PreRankedSlotGoal>{{{44,12},4,7}};
         };
         odh::Routing routing{world,grid,globals,goals};
         routing.set_stack_object_slot_table_provider([&](const odh::VmFrame& frame) -> const odh::SlotTable* {
             return frame.context.stack_object==2?&object_slots:nullptr;
         });
         odh::Vm vm{routines(1,1),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});routing.bind(vm);
         vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(10)==odh::VmStatus::waiting&&provider_seen&&routing.active(),
                 "Route45 Literal scope resolves and ranks the StackObject's local SLOT despite invalid globals");
         for(std::uint64_t tick=1;tick<=64&&routing.active();++tick) routing.tick(tick);
         require(vm.status()==odh::VmStatus::returned_true&&world.find(1)->position->x==44,
                 "StackObject Literal Route45 completes using its selected local SLOT");}
        {odh::World world;odh::RouteGrid grid{64,32};auto globals=slot();globals.version=9;
         auto object_slots=slot();bool provider_seen=false;
         odh::PreRankedSlotGoalProvider goals=[&](const auto& table,std::size_t index,const auto&,const auto& target) {
             provider_seen=&table==&object_slots&&index==1;
             const odh::RoutePoint point{44,12};
             return std::vector<odh::PreRankedSlotGoal>{{point,
                 odh::reference_slot_entry_flags({target.x,target.y},target.direction,point,0x55),2}};
         };
         odh::Routing routing{world,grid,globals,goals};
         routing.set_stack_object_slot_table_provider([&](const odh::VmFrame& frame) -> const odh::SlotTable* {
             return frame.context.stack_object==2?&object_slots:nullptr;
         });
         odh::Vm vm{routines(0,1,0),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});routing.bind(vm);
         vm.start(odh::routine_key(4096),{1,2,2},{1,0,0,0});
         const auto status=vm.run(10);
         if(status!=odh::VmStatus::waiting||!provider_seen||!routing.active())
             throw std::runtime_error("StackVariable dispatch status="+std::to_string(static_cast<int>(status))+ ", provider="+
                 std::to_string(provider_seen)+", diagnostic="+vm.diagnostic());
         require(true,"Route45 StackVariable scope resolves the StackObject SLOT index through BHAV argument 0");
         for(std::uint64_t tick=1;tick<=64&&routing.active();++tick)routing.tick(tick);
         require(vm.status()==odh::VmStatus::returned_true&&world.find(1)->position->x==44,
                 "StackVariable Route45 follows the exact ranked goal from its argument-selected SLOT");}
        {odh::World world;odh::RouteGrid grid{64,32};auto globals=slot();odh::Routing routing{world,grid,globals};
         odh::Vm vm{routines(1,1),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});routing.bind(vm);
         vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(10)==odh::VmStatus::fault&&!routing.active()&&world.find(1)->position->x==4,
                 "StackObject Literal Route45 fails closed without an exact SLOT provider");}
        {auto doors=slot();
         doors.records[0].flags=0x0801;doors.records[0].facing=-3;
         doors.records[0].proximity=std::array<std::int32_t,3>{16,16,16};
         doors.records[1].flags=0x0810;doors.records[1].facing=-3;
         const auto north=odh::enumerate_reference_slot_circle_candidates(doors,0,{28,20},0);
         const auto south=odh::enumerate_reference_slot_circle_candidates(doors,1,{28,20},0);
         require(std::any_of(north.begin(),north.end(),[](const auto& c){return c.position==odh::RoutePoint{28,4}&&c.entry_flags==1;})&&
                 std::any_of(south.begin(),south.end(),[](const auto& c){return c.position==odh::RoutePoint{28,36}&&c.entry_flags==16;}),
                 "Door SLOT128 N/S profiles enumerate their exact directional candidates");
         odh::World world;odh::RouteGrid grid{64,64};auto globals=slot();globals.version=9;
         odh::Routing routing{world,grid,globals};
         routing.set_stack_object_slot_table_provider([&](const odh::VmFrame&) -> const odh::SlotTable* {return &doors;});
         routing.set_route_goal_validator([](const odh::VmFrame&,odh::RoutePoint p){return p==odh::RoutePoint{28,4};});
         odh::Vm vm{routines(1,1,0),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,20,1,3});world.set_position(2,{28,20,1,0});grid.block({28,20});routing.bind(vm);
         vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(10)==odh::VmStatus::waiting&&routing.active(),
                 "Door IgnoreRooms route requires and uses the active-room goal validator");
         std::uint8_t last_walk_direction=255;
         for(std::uint64_t tick=1;tick<=64&&routing.active();++tick)
             routing.tick(tick,[&](const auto& movement){last_walk_direction=movement.position.direction;});
         const auto* actor=world.find(1);
         require(vm.status()==odh::VmStatus::returned_true&&actor->position->x==28&&actor->position->y==4&&
                 actor->person_data[37]==1, ("Door SLOT128 Route45 reaches the validated north candidate: status="+
                 std::to_string(static_cast<int>(vm.status()))+", position="+
                 std::to_string(actor->position->x)+","+std::to_string(actor->position->y)+
                 ", flags="+std::to_string(actor->person_data[37]) +", diagnostic="+routing.diagnostic()).c_str());
         require(actor->position->direction==last_walk_direction&&actor->position->direction!=4&&
                 std::abs(actor->radian_direction-3.14159265358979323846f)>0.01f,
                 "FaceAnywhere preserves the walk arrival orientation instead of forcing toward the Door");}
        {auto doors=slot();doors.records[0].flags=0x0801;doors.records[0].facing=-3;
         doors.records[0].proximity=std::array<std::int32_t,3>{16,16,16};
         odh::World world;odh::RouteGrid grid{64,64};auto globals=slot();odh::Routing routing{world,grid,globals};
         routing.set_stack_object_slot_table_provider([&](const odh::VmFrame&) -> const odh::SlotTable* {return &doors;});
         odh::Vm vm{routines(1,1,0),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,20,1,0});world.set_position(2,{28,20,1,0});routing.bind(vm);
         vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(10)==odh::VmStatus::fault&&!routing.active()&&world.find(1)->position->x==4,
                 "Door IgnoreRooms refuses a single-room route without a host goal validator");}
        {odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();
         odh::Routing routing{world,grid,slots};auto code=routines();
         odh::RoutineSet portal_code{{odh::routine_key(4097),odh::Bhav{0x8002,
             {{46,1,1,{0,0,0,0,0,0,0,0}},
              {2,254,255,{0,0,0,0,0,2,7,7}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
         auto route_primitive=routing.primitive();
         odh::Vm vm{std::move(code),world.vm_memory(),{},
             [route_primitive](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch) {
                 if(instruction.opcode==46) return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
                 return route_primitive(frame,instruction,temps,dispatch);
             },{{9,std::move(portal_code)}}};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});grid.block({28,12});
         std::size_t grid_refreshes=0;
         routing.set_route_grid_refresh([&](const odh::EntityPosition& position) {
             ++grid_refreshes;
             grid=odh::RouteGrid{128,32};grid.block({28,12});
             return position==odh::EntityPosition{70,12,1,0};
         });
         routing.bind(vm);
         vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(1)==odh::VmStatus::waiting&&routing.active(),
                 "Route45 service waits before scheduling its host-resolved portal BHAV");
         const auto route_ticket=vm.pending()->ticket;
         const bool portal_scheduled=routing.call_external_frame({odh::routine_key(4097),{1,2,2,9},{}});
         require(portal_scheduled&&vm.status()==odh::VmStatus::running&&
                 routing.external_frame_active()&&
                 routing.active_frame_context()->state==odh::RouteFrameState::room_portal,
                 "Routing suspends Route45 and marks the active frame as room portal");
         require(vm.run(1)==odh::VmStatus::waiting&&vm.pending()->ticket!=route_ticket,
                 "portal BHAV can suspend on its own service ticket");
         const auto portal_ticket=vm.pending()->ticket;
         require(routing.tick(1)&&world.find(1)->position->x==4&&routing.active(),
                 "route movement stays paused while portal BHAV waits");
         require(world.set_position(1,{70,12,1,0}),
                 "portal BHAV may update the avatar position before returning");
         require(vm.complete(portal_ticket,true)&&vm.run(1)==odh::VmStatus::yielded&&
                 vm.external_frame_return()&&vm.external_frame_return()->ticket==route_ticket,
                 "portal BHAV return publishes the suspended Route45 continuation");
         require(routing.tick(2)&&vm.status()==odh::VmStatus::waiting&&vm.pending()->ticket==route_ticket&&
                 world.find(1)->position->x==70&&grid_refreshes==1&&routing.active(),
                 "Routing rebuilds its room grid and resumes the original wait without same-tick movement");
         require(routing.tick(3)&&world.find(1)->position->x==69&&
                 routing.active_frame_context()->state==odh::RouteFrameState::walking,
                 "Route45 movement resumes on the rebuilt grid after the portal continuation boundary");}
        {odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();
         std::size_t provider_calls=0;
         odh::PreRankedSlotGoalProvider provider=[&](const auto&,std::size_t,const auto&,const auto& target) {
             const auto position=provider_calls++==0?odh::RoutePoint{12,12}:odh::RoutePoint{20,12};
             return std::vector<odh::PreRankedSlotGoal>{{position,
                 odh::reference_slot_entry_flags({target.x,target.y},target.direction,position,0x55),2}};
         };
         odh::Routing routing{world,grid,slots,provider};auto code=routines();
         odh::RoutineSet portal_code{{odh::routine_key(4097),odh::Bhav{0x8002,
             {{45,254,255,{1,0,2,0,0,0,0,0}},
              {2,254,255,{0,0,2,7,7,0,0,0}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
         odh::Vm vm{std::move(code),world.vm_memory(),{},routing.primitive(),{{9,std::move(portal_code)}}};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});grid.block({28,12});routing.bind(vm);
         vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(1)==odh::VmStatus::waiting&&routing.active(),"parent Route45 waits before nested portal routing");
         const auto parent_ticket=vm.pending()->ticket;
         require(routing.call_external_frame({odh::routine_key(4097),{1,2,2,9},{}})&&vm.run(1)==odh::VmStatus::waiting&&
                 vm.pending()->ticket!=parent_ticket&&provider_calls==2,
                 "portal BHAV can issue a nested Route45 with its own wait ticket");
         require(routing.active_frame_context()->state==odh::RouteFrameState::initial&&
                 routing.incoming_direction(0.25f)==0.25f&&routing.consume_portal_turn()&&
                 !routing.consume_portal_turn(),
                 "nested route reads and consumes PortalTurns on its suspended parent frame");
         for(std::uint64_t tick=1;tick<=16&&world.find(1)->position->x<20;++tick) routing.tick(tick);
         require(world.find(1)->position->x==20&&world.find(1)->static_footprint&&routing.active()&&
                 vm.status()==odh::VmStatus::yielded&&vm.external_frame_return()&&
                 vm.external_frame_return()->ticket==parent_ticket,
                 "nested Route45 returns control to the portal BHAV without dropping its parent job");
         require(routing.tick(17)&&vm.status()==odh::VmStatus::waiting&&vm.pending()->ticket==parent_ticket&&
                 world.find(1)->position->x==20,
                 "parent Route45 revalidates and replans from the nested route destination");
         require(routing.tick(18)&&world.find(1)->position->x==19&&
                 !world.find(1)->static_footprint&&routing.active(),
                 "parent route movement resumes on a later tick with its dynamic footprint restored");}
        {odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();std::size_t provider_calls=0;
         odh::PreRankedSlotGoalProvider provider=[&](const auto&,std::size_t,const auto& actor,const auto& target) {
             ++provider_calls;const odh::RoutePoint point{actor.x,actor.y};
             return std::vector<odh::PreRankedSlotGoal>{{point,
                 odh::reference_slot_entry_flags({target.x,target.y},target.direction,point,0x55),2}};
         };
         odh::Routing routing{world,grid,slots,provider};auto code=routines();
         odh::RoutineSet portal_code{{odh::routine_key(4097),odh::Bhav{0x8002,
             {{45,254,255,{1,0,2,0,0,0,0,0}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
         odh::Vm vm{std::move(code),world.vm_memory(),{},routing.primitive(),{{9,std::move(portal_code)}}};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});routing.bind(vm);
         vm.start(odh::routine_key(4096),{1,2,2});require(vm.run(1)==odh::VmStatus::waiting,"root route waits before depth test");
         for(std::size_t depth=1;depth<16;++depth)
             require(routing.call_external_frame({odh::routine_key(4097),{1,2,2,9},{}})&&
                     vm.run(1)==odh::VmStatus::waiting,
                     "bounded nested frame accepts a ticket-linked portal Route45");
         require(provider_calls==16&&routing.call_external_frame({odh::routine_key(4097),{1,2,2,9},{}})&&
                 vm.run(1)==odh::VmStatus::fault&&provider_calls==16,
                 "nested route depth limit fails closed before planning an over-budget child");}
        {bool invoked=false;
         Fixture f([&](const auto& table,std::size_t index,const auto& actor,const auto& target) {
             invoked=table.version==10&&index==1&&actor.x==4&&target.x==28;
             return std::vector<odh::PreRankedSlotGoal>{{{44,12},4,7},{{12,12},64,2}};
         });
         f.start();require(invoked&&f.vm.status()==odh::VmStatus::waiting,
                 "explicit ranked provider receives selected SLOT and actor/target positions");
         for(std::uint64_t tick=1;tick<=64&&f.routing.active();++tick) f.routing.tick(tick);
         const auto* actor=f.world.find(1);
         require(f.vm.status()==odh::VmStatus::returned_true&&actor->position->x==44&&actor->position->y==12,
                 "controller follows provider preference to farther SLOT goal");
         require(actor->person_data[37]==4&&actor->position->direction==7,
                 "arrival preserves provider entry flags and final facing rather than recomputing them");
         require(std::abs(actor->radian_direction-7.0f*0.7853981633974483f)<0.00001f,
                 "route arrival publishes the canonical continuous angle for its selected facing");}
        {Fixture f([](const auto&,auto,const auto&,const auto&) {
             return std::vector<odh::PreRankedSlotGoal>{{{44,12},4,7},{{12,12},64,2}};
         },{1,262144});f.start();
         require(f.vm.status()==odh::VmStatus::fault&&!f.routing.active()&&f.world.find(1)->position->x==4,
                 "controller fails unsupported on ranked budget exhaustion without movement");}
        {Fixture f([](const auto&,auto,const auto&,const auto&) {
             return std::vector<odh::PreRankedSlotGoal>{{{44,12},2,7},{{12,12},64,2}};
         });f.start();require(f.vm.status()==odh::VmStatus::fault&&!f.routing.active(),
                 "provider cannot bypass supported SLOT direction mask");}
        {Fixture f([](const auto&,auto,const auto&,const auto&) {
             return std::vector<odh::PreRankedSlotGoal>{{{44,12},1,7},{{12,12},64,2}};
         });f.start();require(f.vm.status()==odh::VmStatus::fault&&!f.routing.active(),
                 "provider flags must match the SLOT direction for each candidate");}
        {odh::World world;odh::RouteGrid grid{32,33,262144,{376,424}};auto slots=slot();
         odh::Routing routing{world,grid,slots};odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive()};
         world.create(105,odh::EntityKind::avatar,{0,88,0});world.create(128,odh::EntityKind::object,{0,0,1});
         world.set_position(105,{392,440,1,3});world.set_position(128,{392,456,1,4});routing.bind(vm);
         vm.start(odh::routine_key(4096),{105,128,128});
         const auto route_status=vm.run(10);
         require(route_status==odh::VmStatus::waiting&&routing.active(),
                 ("Route45 data=1 selects the exact-radius profile for the House07 actor-target coordinates; status="+
                  std::to_string(static_cast<int>(route_status))+", diagnostic="+routing.diagnostic()).c_str());
         require(routing.tick(1)&&vm.status()==odh::VmStatus::returned_true&&
                 world.find(105)->position->x==392&&world.find(105)->position->y==440,
                 "constructed clear grid exercises the already-at-goal Route45 path without movement");}
        {Fixture f;f.start();
         require(f.vm.status()==odh::VmStatus::waiting&&f.routing.active(),
                 "mutable-grid fixture plans a live Route45 path");
         require(f.grid.block({5,12}),"host can publish a newly blocked next step into the live grid");
         require(f.routing.tick(1),"host obstacle refresh is accepted by the route scheduler");
         require(!f.routing.active()&&f.vm.status()==odh::VmStatus::returned_false,
                 "blocked live route returns false and clears its active job");
         require(f.world.find(1)->position->x==4&&f.world.find(1)->static_footprint,
                 "Route45 revalidates the next step after a host obstacle refresh and stops before collision");}
        {Fixture f;f.start();require(f.vm.status()==odh::VmStatus::waiting,"route dispatch waits");
         require(f.routing.active_frame_context()&&
                 f.routing.active_frame_context()->state==odh::RouteFrameState::initial,
                 "active Route45 job exposes its live root route-frame state");
         require(f.world.find(1)->position->x==4&&f.world.find(1)->static_footprint,"planning does not move actor or change RoomInfo class");
         require(f.routing.tick(1) && f.world.find(1)->position->x==5&&!f.world.find(1)->static_footprint,
                 "first walking step moves one unit and registers a dynamic footprint");
         require(f.routing.active_frame_context()&&
                 f.routing.active_frame_context()->state==odh::RouteFrameState::walking,
                 "route-frame state advances from initial to walking with the first published step");
         require(!f.routing.tick(1) && !f.routing.tick(0) && !f.routing.tick(3) &&
                 f.world.find(1)->position->x==5,"duplicates/backwards/gaps do not consume movement");
         std::size_t movements=1;
         for(std::uint64_t tick=2;tick<=8;++tick) require(f.routing.tick(tick,[&](const auto&){++movements;}),"consecutive tick accepted");
         const auto& p=*f.world.find(1)->position;
         require(p.x==12 && p.y==12 && p.direction==2 && movements==8,"arrives and faces object");
         require(f.world.find(1)->person_data[37]==64 && f.vm.status()==odh::VmStatus::returned_true &&
                 !f.routing.active()&&f.world.find(1)->static_footprint,
                 "arrival updates route flags, restores a static footprint and completes exact wait");
         require(std::abs(f.world.find(1)->radian_direction-2.0f*0.7853981633974483f)<0.00001f,
                 "Route45 arrival keeps continuous and eight-notch facing state synchronized");}
        {Fixture* fixture=nullptr;
         Fixture f([&](const auto&,auto,const auto&,const auto&) {
             fixture->world.remove(2);
             return std::vector<odh::PreRankedSlotGoal>{{{44,12},4,7}};
         });fixture=&f;f.start();
         require(f.vm.status()==odh::VmStatus::fault&&!f.routing.active(),
                 "provider entity removal is rejected without stale world pointer access");}
        {Fixture f;f.world.set_position(1,{12,12,1,0});f.start();
         require(f.vm.status()==odh::VmStatus::waiting&&f.routing.active(),"validated start at SLOT goal creates turn-only route");
         require(f.routing.active_frame_context()&&
                 f.routing.active_frame_context()->state==odh::RouteFrameState::turn_only,
                 "coincident validated goal uses an explicit turn-only route-frame state");
         require(f.routing.tick(1)&&f.vm.status()==odh::VmStatus::returned_true&&!f.routing.active(),
                 "validated turn-only route completes without movement");
         const auto* actor=f.world.find(1);
         require(actor->position->x==12&&actor->position->y==12&&actor->position->direction==2&&actor->person_data[37]==64,
                 "turn-only route preserves position and applies facing/entry flags");}
        {Fixture f;f.world.set_position(1,{12,12,1,0});f.grid.block({12,12});f.start();
         require(f.vm.status()==odh::VmStatus::returned_false&&!f.routing.active(),
                 "coincident but blocked SLOT goal is rejected before turn-only shortcut");}
        {Fixture f;f.start();f.vm.start(odh::routine_key(4096),{1,2,2});
         require(!f.routing.tick(1) && f.world.find(1)->position->x==4 && !f.routing.active(),"restart cancels before movement");
         require(f.vm.status()==odh::VmStatus::running && f.vm.steps()==0,"old job cannot complete new invocation");}
        {Fixture f;f.start();f.grid.block({5,12});
         require(f.routing.tick(1) && f.vm.status()==odh::VmStatus::returned_false &&
                 f.world.find(1)->position->x==4,"new obstacle invalidates next segment");}
        {Fixture f;f.start();f.world.set_position(2,{29,12,1,0});
         require(f.routing.tick(1) && f.vm.status()==odh::VmStatus::returned_false,"target movement cancels route");}
        {Fixture f;f.start();
         require(f.world.set_radian_direction(1,0.1f)&&f.world.find(1)->position->direction==0,
                 "test changes the continuous angle without changing its direction notch");
         require(f.routing.tick(1)&&f.vm.status()==odh::VmStatus::returned_false&&
                 f.world.find(1)->position->x==4&&!f.routing.active(),
                 "active Route45 job cancels when its captured continuous angle changes within one notch");}
        {Fixture f;f.start();const auto generation=f.world.find(1)->generation;
         f.world.remove(1);f.world.create(1,odh::EntityKind::avatar,{0,88,0});f.world.set_position(1,{4,12,1,0});
         require(f.world.find(1)->generation!=generation && f.routing.tick(1) &&
                 f.world.find(1)->position->x==4 && f.vm.status()==odh::VmStatus::returned_false,"reused ID cannot receive old movement");}
        {Fixture f;f.start();f.world.remove(2);
         require(f.routing.tick(1) && f.vm.status()==odh::VmStatus::returned_false,"target removal cancels");}
        {Fixture f;f.slots.records[1].flags=0xFF;f.start();
         require(f.vm.status()==odh::VmStatus::fault && !f.routing.active(),"unsupported SLOT profile never becomes success");}
        {Fixture f;auto& slot=f.slots.records[1];slot.flags=0xA0FF;
         slot.proximity=std::array<std::int32_t,3>{16,64,16};slot.gradient_bits=0;
         slot.facing=-3;slot.resolution=8;f.start();
         require(f.vm.status()==odh::VmStatus::fault && !f.routing.active() &&
                 f.world.find(1)->position->x==4 && f.world.find(1)->position->y==12,
                 "unsupported ranged profile selected by Route45 data=1 remains fail-closed");}
        {Fixture f;f.world.attach(2,0,1);f.start();
         require(f.vm.status()==odh::VmStatus::fault,"attached avatar cannot walk as a free actor");}
        {Fixture f;f.world.set_position(1,{4,12,2,0});f.start();
         require(f.vm.status()==odh::VmStatus::fault,"cross-floor route unsupported explicitly");}
        {Fixture f;f.grid.block({4,11});f.grid.block({4,13});f.grid.block({3,12});f.grid.block({5,12});f.start();
         require(f.vm.status()==odh::VmStatus::returned_false && !f.routing.active(),"no path is false without movement");}
        {Fixture f;odh::Vm other(routines(),f.world.vm_memory(),{},f.routing.primitive());other.start(odh::routine_key(4096),{1,2,2});
         require(other.run(1)==odh::VmStatus::fault && !f.routing.active(),"controller refuses foreign VM identity");}
        {odh::World world;odh::RouteGrid grid{32,32,1024,{380,440}};auto slots=slot();
         const odh::RoutePoint start{403,445};
         for(std::size_t n=0;n<grid.cells();++n)grid.block(grid.point(n));
         grid.block(start,false);
         odh::Routing routing{world,grid,slots};odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{start.x,start.y,1,0});world.set_position(2,{392,456,1,0});routing.bind(vm);
         vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(10)==odh::VmStatus::returned_false&&!routing.active(),
                 "a walkable exact-radius goal outside the SLOT direction mask is not accepted");}
        {Fixture f;f.start();bool guarded=false;
         f.routing.tick(1,[&](const auto&){try{f.routing.tick(2);}catch(const std::logic_error&){guarded=true;}});
         require(guarded && f.world.find(1)->position->x==5 && f.routing.tick(2),"movement notification cannot reenter tick");}
        {Fixture f;f.start();bool was_dynamic=false;
         f.routing.tick(1,[&](const auto&){was_dynamic=!f.world.find(1)->static_footprint;f.vm.start(odh::routine_key(4096),{1,2,2});});
         require(was_dynamic&&!f.routing.tick(2) && f.world.find(1)->position->x==5&&
                 f.world.find(1)->static_footprint,"restart cancellation stops movement and restores static membership");}
        {Fixture f;require(!f.world.set_position(1,{-1,0,1,0}) && !f.world.set_position(1,{0,0,0,0}) &&
                            !f.world.set_position(1,{0,0,1,8}),"position profile bounds");}
        std::cout<<checks<<" routing checks passed (constructed world only)\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Routing test failed: "<<e.what()<<'\n';return 1;}
}
