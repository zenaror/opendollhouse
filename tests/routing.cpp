#include "odh/routing.hpp"
#include "odh/route_frame.hpp"
#include "odh/slot_scoring.hpp"
#include "../tools/household_go_here_session.hpp"
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
            result.path.expanded==failed.expanded,"one ranked search falls through an unreachable first goal");
    result=odh::find_pre_ranked_slot_route(grid,{0,1},goals,{failed.expanded,100});
    require(result.path.status==odh::RouteStatus::found&&result.selected==1&&
            result.path.expanded==failed.expanded,"single traversal fits the full reachable component in budget");
    result=odh::find_pre_ranked_slot_route(grid,{0,1},goals,{1,100});
    require(result.path.status==odh::RouteStatus::budget_exhausted&&!result.selected,
            "unfinished first search never falls through to a reachable second goal");
    result=odh::find_pre_ranked_slot_route(grid,{0,1},goals,{100,0});
    require(result.path.status==odh::RouteStatus::budget_exhausted&&!result.selected,
            "frontier exhaustion never falls through");
    result=odh::find_pre_ranked_slot_route(grid,{0,1},goals,{100,100,1});
    require(result.path.status==odh::RouteStatus::budget_exhausted&&!result.selected,
            "candidate count is bounded before ordered searches");
    std::vector<odh::RoutePoint> rank_overflow(std::size_t(std::numeric_limits<std::uint16_t>::max())+1,{1,1});
    odh::RouteLimits rank_limits;rank_limits.candidates=rank_overflow.size();
    const auto rank_limited=odh::find_ranked_route(grid,{0,1},rank_overflow,rank_limits);
    require(rank_limited.path.status==odh::RouteStatus::budget_exhausted&&!rank_limited.selected,
            "rank storage bound rejects candidate lists that exceed uint16 indices");
    odh::RouteGrid many{128,64};
    for(std::int32_t y=0;y<64;++y)many.wall({63,y},{64,y});
    std::vector<odh::PreRankedSlotGoal> many_goals;
    many_goals.reserve(204);
    for(std::int32_t x=64;x<128&&many_goals.size()<203;++x)
        for(std::int32_t y=0;y<64&&many_goals.size()<203;++y)
            many_goals.push_back({{x,y},1,0});
    many_goals.push_back({{2,32},1,0});
    result=odh::find_pre_ranked_slot_route(many,{1,32},many_goals,{5000,8192,204});
    require(result.path.status==odh::RouteStatus::found&&result.selected==203&&
            result.path.points.back()==odh::RoutePoint{2,32}&&result.path.expanded==4096,
            "204 ranked goals search a disconnected preferred region only once before selecting a reachable goal");
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
        return odh::rank_reference_range_slot(table,index,actor,target,random);
    };
    odh::Routing routing{world,grid,globals};
    routing.set_ranged_slot_goal_provider(provider);
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

    std::vector<odh::SlotRecord> records(9);
    for(auto& record:records)record.type=3;
    auto& house07_slot=records[8];house07_slot.flags=0xA0FF;
    house07_slot.proximity=std::array<std::int32_t,3>{16,64,16};
    house07_slot.offset_bits={0,0,0};house07_slot.standing=1;house07_slot.sitting=0;
    house07_slot.ground=0;house07_slot.snap_target_slot=-1;house07_slot.max_size=0;
    house07_slot.unknown_i10=0;house07_slot.gradient_bits=0;house07_slot.height=0;
    house07_slot.facing=-3;house07_slot.resolution=8;
    odh::SlotTable house07_globals{0,10,records,0};odh::World main_world;odh::RouteGrid main_grid{160,160};
    odh::RandomState main_random{0};
    odh::PreRankedSlotGoalProvider main_provider=[&](const auto& table,std::size_t index,
        const auto& actor,const auto& target) {
        return odh::rank_reference_range_slot(table,index,actor,target,main_random);
    };
    odh::Routing main_routing{main_world,main_grid,house07_globals};
    main_routing.set_ranged_slot_goal_provider(main_provider);
    main_routing.set_route_goal_validator([&](const odh::VmFrame& frame,odh::RoutePoint point) {
        const auto* actor=main_world.find(frame.context.caller);
        const auto* target=main_world.find(frame.context.stack_object);
        return actor&&target&&actor->position&&target->position&&
            actor->position->level==target->position->level&&main_grid.contains(point)&&
            main_grid.walkable(point)&&point==odh::RoutePoint{96,80};
    });
    odh::RoutineSet global_route{{odh::routine_key(4096),odh::Bhav{0x8002,
        {{45,254,255,{8,0,2,0,0,0,0,0}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
    odh::Vm main_vm{std::move(global_route),main_world.vm_memory(),{},main_routing.primitive()};
    main_world.create(1,odh::EntityKind::avatar,{0,88,0});
    main_world.create(2,odh::EntityKind::object,{0,0,1});
    main_world.set_position(1,{96,64,1,3});main_world.set_position(2,{80,80,1,0});
    main_grid.block({80,80});main_routing.bind(main_vm);
    main_vm.start(odh::routine_key(4096),{1,2,2});
    const auto main_route_status=main_vm.run(10);
    require(main_route_status==odh::VmStatus::waiting&&main_routing.active()&&main_random.seed()==0,
        ("Global SLOT100 type-3 index8 Route45 accepts the exact randomized range profile: status="+
         std::to_string(static_cast<int>(main_vm.status()))+", active="+
         std::to_string(main_routing.active())+", rng="+std::to_string(main_random.seed())+
         ", vm="+main_vm.diagnostic()+", route="+main_routing.diagnostic()).c_str());
    for(std::uint64_t tick=1;tick<=64&&main_routing.active();++tick)main_routing.tick(tick);
    require(main_vm.status()==odh::VmStatus::returned_true&&!main_routing.active()&&
            main_world.find(1)->position->direction==4&&main_world.find(1)->person_data[37]==0x04,
        ("FaceAnywhere Global route does not force the candidate's neutral facing: status="+
         std::to_string(static_cast<int>(main_vm.status()))+", active="+
         std::to_string(main_routing.active())+", pos="+
         std::to_string(main_world.find(1)->position->x)+","+
         std::to_string(main_world.find(1)->position->y)+", direction="+
         std::to_string(main_world.find(1)->position->direction)+", entry="+
         std::to_string(main_world.find(1)->person_data[37])+", route="+
         main_routing.diagnostic()).c_str());
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
odh::SlotTable child_shy_global_slots() {
    std::vector<odh::SlotRecord> records(11);
    for(auto& record:records)record.type=3;
    auto& record=records[10];record.flags=0x8055;record.offset_bits={0,0,0};
    record.proximity=std::array<std::int32_t,3>{32,80,32};record.standing=1;record.sitting=10;
    record.ground=0;record.snap_target_slot=-1;record.max_size=0;record.unknown_i10=0;
    record.gradient_bits=0x3E400000;record.height=0;record.facing=-2;record.resolution=16;
    return {0,10,std::move(records),0};
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
void shared_world_dual_controller_tests() {
    odh::World world; odh::RouteGrid grid{64,32}; auto globals=slot();
    odh::Routing first{world,grid,globals}, second{world,grid,globals};
    odh::Vm vm1{goto_relative(),world.vm_memory(),{},first.primitive()};
    odh::Vm vm2{goto_relative(),world.vm_memory(),{},second.primitive()};
    world.create(1,odh::EntityKind::avatar,{0,88,0});
    world.create(2,odh::EntityKind::object,{0,0,1});
    world.create(3,odh::EntityKind::avatar,{0,88,0});
    world.create(4,odh::EntityKind::object,{0,0,1});
    world.set_position(1,{4,4,1,0}); world.set_position(2,{12,4,1,0});
    world.set_position(3,{4,20,1,0}); world.set_position(4,{28,20,1,0});
    auto validator=[&](const odh::VmFrame& f,odh::RoutePoint p) {
        const auto* target=world.find(f.context.stack_object);
        return target&&target->position&&p==odh::RoutePoint{target->position->x,target->position->y}&&grid.walkable(p);
    };
    first.set_route_goal_validator(validator); second.set_route_goal_validator(validator);
    require(first.bind(vm1)&&second.bind(vm2),"two routing controllers bind to separate VMs sharing world/grid/SLOT");
    require(!first.bind(vm2)&&!second.bind(vm1),"each routing controller rejects cross-binding to the other VM");
    vm1.start(odh::routine_key(4096),{1,2,2}); vm2.start(odh::routine_key(4096),{3,4,4});
    require(vm1.run(4)==odh::VmStatus::waiting&&vm2.run(4)==odh::VmStatus::waiting&&first.active()&&second.active(),
            "both VMs independently suspend on their own routes");
    for(std::uint64_t tick=1;tick<=64&&(first.active()||second.active());++tick) {
        first.tick(tick); second.tick(tick);
        require(vm1.status()==odh::VmStatus::waiting||vm1.status()==odh::VmStatus::returned_true,
                "first controller does not complete or fault the second VM");
        require(vm2.status()==odh::VmStatus::waiting||vm2.status()==odh::VmStatus::returned_true,
                "second controller does not complete or fault the first VM");
    }
    require(vm1.status()==odh::VmStatus::returned_true&&vm2.status()==odh::VmStatus::returned_true&&
            world.find(1)->position->x==12&&world.find(1)->position->y==4&&
            world.find(3)->position->x==28&&world.find(3)->position->y==20&&
            world.find(1)->position->x!=world.find(3)->position->x,
            "interleaved deterministic ticks reach distinct goals and return only to each owning VM");
}
void household_resident_route_owner_tests() {
    odh::World world;odh::RouteGrid first_grid{32,16},second_grid{48,32};auto globals=slot();
    odh::Routing first{world,first_grid,globals},second{world,second_grid,globals};
    odh::Vm first_vm{{},world.vm_memory(),{},first.primitive()};
    odh::Vm second_vm{{},world.vm_memory(),{},second.primitive()};
    constexpr odh::CodeOwner first_owner=501,second_owner=502;
    require(first_vm.install_owner(first_owner,goto_relative())&&
            second_vm.install_owner(second_owner,goto_relative()),
            "each household VM installs the same synthetic Go Here BHAV under its own code owner");
    world.create(11,odh::EntityKind::avatar,{0,88,0});world.create(12,odh::EntityKind::object,{0,0,1});
    world.create(21,odh::EntityKind::avatar,{0,88,0});world.create(22,odh::EntityKind::object,{0,0,1});
    world.set_position(11,{4,4,1,0});world.set_position(12,{12,4,1,0});
    world.set_position(21,{4,20,1,0});world.set_position(22,{28,20,1,0});
    bool first_context=false,second_context=false;
    first.set_route_goal_validator([&](const odh::VmFrame& frame,odh::RoutePoint point){
        first_context=frame.context.caller==11&&frame.context.stack_object==12&&
            frame.context.code_owner==first_owner;
        return first_context&&point==odh::RoutePoint{12,4}&&first_grid.walkable(point);
    });
    second.set_route_goal_validator([&](const odh::VmFrame& frame,odh::RoutePoint point){
        second_context=frame.context.caller==21&&frame.context.stack_object==22&&
            frame.context.code_owner==second_owner;
        return second_context&&point==odh::RoutePoint{28,20}&&second_grid.walkable(point);
    });
    require(first.bind(first_vm)&&second.bind(second_vm),"per-resident route controllers bind to their own VMs");
    require(!first.bind(second_vm)&&!second.bind(first_vm),"per-resident route controllers reject cross-binding");
    require(first_vm.start(odh::routine_key(4096),{11,12,12,first_owner})==odh::VmStatus::running&&
            second_vm.start(odh::routine_key(4096),{21,22,22,second_owner})==odh::VmStatus::running,
            "independent Go Here actions start with distinct resident and marker contexts");
    require(first_vm.run(4)==odh::VmStatus::waiting&&second_vm.run(4)==odh::VmStatus::waiting&&
            first_context&&second_context,"each controller validates its own resident, marker, owner, and grid");
    for(std::uint64_t tick=1;tick<=32&&(first.active()||second.active());++tick){
        first.tick(tick);second.tick(tick);
    }
    require(first_vm.status()==odh::VmStatus::returned_true&&second_vm.status()==odh::VmStatus::returned_true&&
            world.find(11)->position->x==12&&world.find(11)->position->y==4&&
            world.find(21)->position->x==28&&world.find(21)->position->y==20&&
            world.find(11)->position->x!=world.find(21)->position->x,
            "interleaved household Go Here routes complete independently at distinct destinations");
}
void household_go_here_session_tests() {
    odh::World world;odh::RouteGrid first_grid{32,16},second_grid{48,32};auto globals=slot();
    world.create(11,odh::EntityKind::avatar,{0,88,0});world.create(21,odh::EntityKind::avatar,{0,88,0});
    world.set_position(11,{4,4,1,0});world.set_position(21,{4,20,1,0});
    odh::EntityThreadControllers first_threads{world,11},second_threads{world,21};
    odh::Routing first_routing{world,first_grid,globals},second_routing{world,second_grid,globals};
    odh::VmPrimitive first_primitive=[&](const auto& f,const auto& i,const auto& t,const auto& d){
        if(i.opcode==17)return first_threads.interaction_primitive()(f,i,t,d);
        if(i.opcode==27)return first_routing.primitive()(f,i,t,d);
        return odh::PrimitiveOutcome{};
    };
    odh::VmPrimitive second_primitive=[&](const auto& f,const auto& i,const auto& t,const auto& d){
        if(i.opcode==17)return second_threads.interaction_primitive()(f,i,t,d);
        if(i.opcode==27)return second_routing.primitive()(f,i,t,d);
        return odh::PrimitiveOutcome{};
    };
    const odh::RoutineSet main{{odh::routine_key(4096),odh::Bhav{0x8002,
        {{17,254,255,{0,0,1,0}}},{},odh::Bhav::Metadata{0,1,0,0}}}};
    odh::Vm first_vm{main,world.vm_memory(),{},first_primitive};
    odh::Vm second_vm{main,world.vm_memory(),{},second_primitive};
    require(first_threads.bind(first_vm)&&second_threads.bind(second_vm)&&
            first_routing.bind(first_vm)&&second_routing.bind(second_vm),
            "synthetic household Go Here sessions bind queue and route controllers to each resident VM");
    first_routing.set_route_goal_validator([&](const odh::VmFrame& frame,odh::RoutePoint p){
        return frame.context.caller==11&&frame.context.stack_object!=0&&frame.context.code_owner==501&&
            p==odh::RoutePoint{12,4}&&first_grid.walkable(p);
    });
    second_routing.set_route_goal_validator([&](const odh::VmFrame& frame,odh::RoutePoint p){
        return frame.context.caller==21&&frame.context.stack_object!=0&&frame.context.code_owner==502&&
            p==odh::RoutePoint{28,20}&&second_grid.walkable(p);
    });
    first_vm.start(odh::routine_key(4096),{11,11,11,0},{120});
    second_vm.start(odh::routine_key(4096),{21,21,21,0},{120});
    require(first_vm.run(4)==odh::VmStatus::waiting&&second_vm.run(4)==odh::VmStatus::waiting,
            "each resident Main waits independently for its own Go Here action");
    odh_host::HouseholdGoHereSession first_session{world,11,first_vm,first_threads,first_routing};
    odh_host::HouseholdGoHereSession second_session{world,21,second_vm,second_threads,second_routing};
    require(first_session.request({12,4,1,0},501,goto_relative())&&
            second_session.request({28,20,1,0},502,goto_relative()),
            "each resident queues a Go Here marker and action under its own code owner");
    require(first_session.marker_id()&&second_session.marker_id()&&
            first_session.marker_id()!=second_session.marker_id()&&first_session.marker_alive()&&
            second_session.marker_alive(),"resident Go Here sessions own distinct live World markers");
    for(std::uint64_t tick=1;tick<=32&&(first_session.status()=="queued"||second_session.status()=="queued");++tick){
        if(first_routing.active())first_routing.tick(tick);
        if(second_routing.active())second_routing.tick(tick);
        first_threads.advance(tick);second_threads.advance(tick);
        first_vm.run(8);second_vm.run(8);
        first_threads.observe();second_threads.observe();
        first_session.observe();second_session.observe();
    }
    require(first_session.status()=="returned_true"&&second_session.status()=="returned_true"&&
            first_session.owner()==501&&second_session.owner()==502&&first_session.result()==true&&
            second_session.result()==true&&!first_session.marker_alive()&&!second_session.marker_alive()&&
            world.find(11)->position->x==12&&world.find(11)->position->y==4&&
            world.find(21)->position->x==28&&world.find(21)->position->y==20,
            "interleaved resident Go Here sessions complete only their own VM, owner, marker, and destination");
    world.create(31,odh::EntityKind::avatar,{0,88,0});world.set_position(31,{4,28,1,0});
    odh::EntityThreadControllers failed_threads{world,31};
    odh::Routing failed_routing{world,second_grid,globals};
    odh::Vm failed_vm{main,world.vm_memory(),{},[&](const auto& f,const auto& i,const auto& t,const auto& d){
        if(i.opcode==17)return failed_threads.interaction_primitive()(f,i,t,d);
        if(i.opcode==27)return failed_routing.primitive()(f,i,t,d);
        return odh::PrimitiveOutcome{};
    }};
    require(failed_threads.bind(failed_vm)&&failed_routing.bind(failed_vm),
            "host-failure fixture binds its resident interaction and route controllers");
    failed_routing.set_route_goal_validator([&](const odh::VmFrame& frame,odh::RoutePoint p){
        return frame.context.caller==31&&p==odh::RoutePoint{28,28}&&second_grid.walkable(p);
    });
    failed_vm.start(odh::routine_key(4096),{31,31,31,0},{120});failed_vm.run(4);
    odh_host::HouseholdGoHereSession failed_session{world,31,failed_vm,failed_threads,failed_routing};
    require(failed_session.request({28,28,1,0},503,goto_relative())&&failed_session.marker_alive(),
            "host-failure fixture creates a queued request and owned marker");
    failed_session.observe(true,"synthetic resident route tick failure");
    require(failed_session.status()=="host_failed"&&!failed_session.marker_alive(),
            "host route failure closes resident request and removes its marker before reporting");
    world.create(32,odh::EntityKind::avatar,{0,88,0});world.set_position(32,{4,29,1,0});
    odh::EntityThreadControllers incomplete_threads{world,32};
    odh::Routing incomplete_routing{world,second_grid,globals};
    odh::Vm incomplete_vm{main,world.vm_memory(),{},[&](const auto& f,const auto& i,const auto& t,const auto& d){
        if(i.opcode==17)return incomplete_threads.interaction_primitive()(f,i,t,d);
        if(i.opcode==27)return incomplete_routing.primitive()(f,i,t,d);
        return odh::PrimitiveOutcome{};
    }};
    require(incomplete_threads.bind(incomplete_vm)&&incomplete_routing.bind(incomplete_vm),
            "incomplete resident fixture binds its interaction and route controllers");
    incomplete_routing.set_route_goal_validator([&](const odh::VmFrame& frame,odh::RoutePoint p){
        return frame.context.caller==32&&p==odh::RoutePoint{28,29}&&second_grid.walkable(p);
    });
    incomplete_vm.start(odh::routine_key(4096),{32,32,32,0},{120});incomplete_vm.run(4);
    odh_host::HouseholdGoHereSession incomplete_session{world,32,incomplete_vm,incomplete_threads,incomplete_routing};
    require(incomplete_session.request({28,29,1,0},504,goto_relative())&&incomplete_session.marker_alive(),
            "incomplete resident fixture creates a queued request and owned marker");
    incomplete_session.finish_incomplete("tick_limit","synthetic route tick budget ended");
    require(incomplete_session.status()=="tick_limit"&&incomplete_session.route_status()=="incomplete"&&
            !incomplete_session.marker_alive(),
            "tick-limited resident request is finalized and its marker is removed before reporting");
}
void deferred_room_route_tests() {
    {
        odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();
        std::size_t local_goal_calls=0,step_calls=0,refreshes=0,context_ends=0;
        std::uint8_t local_goal_flags=0;
        odh::PreRankedSlotGoalProvider goals=[&](const auto&,std::size_t,const auto& actor,const auto& target) {
            ++local_goal_calls;
            require(actor.x==24&&target.x==28,"local SLOT planning starts only after the actor reaches the destination room");
            const odh::RoutePoint point{20,12};
            local_goal_flags=odh::reference_slot_entry_flags({target.x,target.y},target.direction,point,0x55);
            return std::vector<odh::PreRankedSlotGoal>{{point,local_goal_flags,6}};
        };
        odh::Routing routing{world,grid,slots,goals};
        routing.set_room_pair_provider([](const auto& actor,const auto& target)
            ->std::optional<std::pair<std::uint16_t,std::uint16_t>> {
            const auto room=static_cast<std::uint16_t>(actor.x<16?3:actor.x<24?2:1);
            return std::pair{room,std::uint16_t{1}};
        });
        routing.set_route_grid_refresh([&](const auto& actor) {++refreshes;return actor.x==4||actor.x==16||actor.x==24;});
        routing.set_room_portal_context_end_provider([&](auto){++context_ends;});
        routing.set_room_portal_step_provider([&](auto ticket,const auto&,const auto&,
            const std::optional<odh::VmCall>& previous,const std::optional<bool>& result,auto step) {
            ++step_calls;
            require(ticket!=0,"deferred planner receives the root VM ticket");
            if(!previous&&!result&&step==0)
                return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::call_ready,
                    odh::VmCall{odh::routine_key(4097),{1,2,2,9},{}},{}};
            require(previous.has_value()&&result.has_value(),"planner receives each completed Portal15 result");
            if(step==1&&!(*result))
                return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::call_ready,
                    odh::VmCall{odh::routine_key(4098),{1,2,2,10},{}},{}};
            require(*result,("a failed portal result is visible before the planner selects an alternate (step="+
                std::to_string(step)+", result="+(result?std::to_string(*result):"none")+")").c_str());
            if(step==2)
                return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::call_ready,
                    odh::VmCall{odh::routine_key(4098),{1,2,2,10},{}},{}};
            return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::destination_reached,{}, {}};
        });
        auto code=routines();
        odh::RoutineSet false_portal{{odh::routine_key(4097),odh::Bhav{0x8002,
            {{2,254,255,{1,0,0,0,0,2,7,7}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
        odh::RoutineSet true_portal{{odh::routine_key(4098),odh::Bhav{0x8002,
            {{2,254,255,{0,0,0,0,0,2,7,7}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
        odh::Vm vm{std::move(code),world.vm_memory(),{},routing.primitive(),
            {{9,std::move(false_portal)},{10,std::move(true_portal)}}};
        world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
        world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});routing.bind(vm);
        vm.start(odh::routine_key(4096),{1,2,2});
        require(vm.run(2)==odh::VmStatus::waiting&&routing.active()&&local_goal_calls==0,
                "cross-room Route45 waits on the root ticket before local SLOT selection");
        const auto root_ticket=vm.pending()->ticket;
        require(routing.tick(1)&&routing.external_frame_active()&&step_calls==1&&
                vm.pending()->ticket==root_ticket,"first Portal15 is scheduled at the original ticket safe point");
        require(vm.run(1)==odh::VmStatus::yielded&&routing.tick(2)&&routing.external_frame_active()&&
                step_calls==2&&refreshes==1&&vm.pending()->ticket==root_ticket,
                "false Portal15 refreshes the grid and schedules an alternate edge under the same ticket");
        world.set_position(1,{16,12,1,0});
        require(vm.run(1)==odh::VmStatus::yielded&&routing.tick(3)&&routing.external_frame_active()&&
                step_calls==3&&refreshes==2&&vm.pending()->ticket==root_ticket,
                "successful alternate opens the next room transition without replacing the root ticket");
        world.set_position(1,{24,12,1,0});
        require(vm.run(1)==odh::VmStatus::yielded&&routing.tick(4)&&!routing.external_frame_active()&&
                local_goal_calls==1&&context_ends==1&&vm.status()==odh::VmStatus::waiting,
                "verified destination room closes planner state and resumes local SLOT planning");
        for(std::uint64_t tick=5;tick<16&&routing.active();++tick)routing.tick(tick);
        const auto* arrived=world.find(1);
        require(!routing.active()&&vm.status()==odh::VmStatus::returned_true&&
                arrived->position->x==20&&arrived->person_data[37]==local_goal_flags&&vm.pending()==std::nullopt,
                ("local Route45 reaches its destination and completes the original ticket true (status="+
                 std::to_string(static_cast<int>(vm.status()))+", x="+std::to_string(arrived->position->x)+
                 ", flags="+std::to_string(arrived->person_data[37])+", active="+
                 std::to_string(routing.active())+", diagnostic="+routing.diagnostic()+")").c_str());
        require(refreshes==3&&step_calls==4&&context_ends==1,
                "failed edge plus two successful portal transitions refresh and clean ticket state exactly once");
    }
    {
        odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();odh::Routing routing{world,grid,slots};
        std::size_t ended=0;
        routing.set_room_pair_provider([](const auto&,const auto&)
            ->std::optional<std::pair<std::uint16_t,std::uint16_t>> {return std::pair{3,1};});
        routing.set_route_grid_refresh([](const auto&){return true;});
        routing.set_room_portal_context_end_provider([&](auto){++ended;});
        routing.set_room_portal_step_provider([](auto,const auto&,const auto&,const auto&,const auto&,auto) {
            return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::destination_reached,{}, {}};
        });
        odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive()};
        world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
        world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});routing.bind(vm);
        vm.start(odh::routine_key(4096),{1,2,2});
        require(vm.run(2)==odh::VmStatus::waiting&&routing.tick(1)&&!routing.active()&&
                vm.status()==odh::VmStatus::returned_false&&ended==1,
                "planner cannot claim destination while the live room resolver still disagrees");
    }
    {
        odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();odh::Routing routing{world,grid,slots};
        std::size_t ended=0;
        routing.set_room_pair_provider([](const auto&,const auto&)
            ->std::optional<std::pair<std::uint16_t,std::uint16_t>> {return std::pair{3,1};});
        routing.set_room_portal_context_end_provider([&](auto){++ended;});
        routing.set_room_portal_step_provider([](auto,const auto&,const auto&,const auto&,const auto&,auto) {
            return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::unavailable,{},"no alternate portal edge"};
        });
        odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive()};
        world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
        world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});routing.bind(vm);
        vm.start(odh::routine_key(4096),{1,2,2});
        require(vm.run(2)==odh::VmStatus::waiting&&routing.tick(1)&&!routing.active()&&
                vm.status()==odh::VmStatus::returned_false&&ended==1&&
                routing.diagnostic()=="no alternate portal edge",
                "missing alternate room route fails the original ticket and releases its planner state");
    }
    {
        odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();odh::Routing routing{world,grid,slots};
        std::vector<std::uint64_t> planned_tickets,ended_tickets;
        std::uint64_t root_ticket=0;
        routing.set_room_pair_provider([](const auto&,const auto&)
            ->std::optional<std::pair<std::uint16_t,std::uint16_t>> {return std::pair{3,1};});
        routing.set_route_grid_refresh([](const auto&){return true;});
        routing.set_room_portal_context_end_provider([&](auto ticket){ended_tickets.push_back(ticket);});
        routing.set_room_portal_step_provider([&](auto ticket,const auto&,const auto&,
            const std::optional<odh::VmCall>& previous,const std::optional<bool>&,auto) {
            planned_tickets.push_back(ticket);
            if(ticket==root_ticket&&!previous)
                return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::call_ready,
                    odh::VmCall{odh::routine_key(4097),{1,2,2,9},{}},{}};
            return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::unavailable,{},"nested route has no portal"};
        });
        odh::RoutineSet portal{{odh::routine_key(4097),odh::Bhav{0x8002,
            {{45,254,255,{1,0,2,0,0,0,0,0}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
        odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive(),{{9,std::move(portal)}}};
        world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
        world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});routing.bind(vm);
        vm.start(odh::routine_key(4096),{1,2,2});
        require(vm.run(2)==odh::VmStatus::waiting&&routing.active(),
                "nested deferred-route cleanup test establishes a parent room job");
        root_ticket=vm.pending()->ticket;
        require(routing.tick(1)&&routing.external_frame_active()&&vm.run(1)==odh::VmStatus::waiting&&
                vm.pending()->ticket!=root_ticket,
                "parent Portal15 can start an independent nested deferred Route45 ticket");
        const auto child_ticket=vm.pending()->ticket;
        require(routing.tick(2)&&vm.status()==odh::VmStatus::yielded&&vm.pending()->ticket==root_ticket&&
                ended_tickets==std::vector<std::uint64_t>{child_ticket},
                "failed nested room route cleans only its own root-ticket planner state");
        require(vm.run(1)==odh::VmStatus::yielded&&vm.external_frame_return()&&
                vm.external_frame_return()->ticket==root_ticket&&routing.tick(3)&&!routing.active()&&
                vm.status()==odh::VmStatus::returned_false&&
                ended_tickets==std::vector<std::uint64_t>{child_ticket,root_ticket},
                "parent route later cleans its distinct planner state after nested failure");
        require(planned_tickets.size()==3&&planned_tickets[0]==root_ticket&&
                planned_tickets[1]==child_ticket&&planned_tickets[2]==root_ticket,
                "nested and parent portal planners receive their own stable root-ticket identities");
    }
    {
        odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();odh::Routing routing{world,grid,slots};
        std::size_t ended=0;
        routing.set_room_pair_provider([](const auto&,const auto&)
            ->std::optional<std::pair<std::uint16_t,std::uint16_t>> {return std::pair{3,1};});
        routing.set_room_portal_context_end_provider([&](auto){++ended;});
        routing.set_room_portal_step_provider([](auto,const auto&,const auto&,const auto&,const auto&,auto) {
            return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::call_ready,
                odh::VmCall{odh::routine_key(4097),{1,2,2,9},{}},{}};
        });
        odh::RoutineSet portal{{odh::routine_key(4097),odh::Bhav{0x8002,
            {{2,254,255,{0,0,0,0,0,2,7,7}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
        odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive(),{{9,std::move(portal)}}};
        world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
        world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});routing.bind(vm);
        vm.start(odh::routine_key(4096),{1,2,2});
        require(vm.run(2)==odh::VmStatus::waiting&&routing.tick(1)&&routing.external_frame_active(),
                "restart cleanup test schedules a deferred Portal15 child");
        vm.start(odh::routine_key(4096),{1,2,2});
        require(!routing.tick(2)&&!routing.active()&&ended==1,
                "VM restart cancels deferred routing and releases per-ticket host state exactly once");
    }
}
void saved_go_here_cross_room_test() {
    odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();odh::Routing routing{world,grid,slots};
    std::size_t portal_plans=0,portal_returns=0,refreshes=0,context_ends=0;
    routing.set_room_pair_provider([](const auto& actor,const auto& target)
        ->std::optional<std::pair<std::uint16_t,std::uint16_t>> {
        const auto room=[](const auto& position){return static_cast<std::uint16_t>(position.x<16?3:1);};
        return std::pair{room(actor),room(target)};
    });
    routing.set_route_grid_refresh([&](const auto& actor){++refreshes;return actor.x==16;});
    routing.set_room_portal_context_end_provider([&](auto){++context_ends;});
    routing.set_room_portal_step_provider([&](auto,const auto& actor,const auto& target,
        const std::optional<odh::VmCall>& previous,const std::optional<bool>& result,auto step) {
        if(!previous&&!result&&step==0&&actor.x==4&&target.x==28){
            ++portal_plans;
            return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::call_ready,
                odh::VmCall{odh::routine_key(4097),{1,2,2,9},{}},{}};
        }
        require(previous.has_value()&&result.has_value()&&*result&&step==1&&actor.x==16&&target.x==28,
                "saved Go Here portal return confirms the actor reached the target room");
        ++portal_returns;
        return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::destination_reached,{}, {}};
    });
    const std::array<std::uint8_t,8> go_here{10,0,254,255,10,0,5,0};
    auto code=goto_relative(go_here);
    odh::RoutineSet portal{{odh::routine_key(4097),odh::Bhav{0x8002,
        {{2,254,255,{0,0,0,0,0,2,7,7}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
    odh::Vm vm{std::move(code),world.vm_memory(),{},routing.primitive(),{{9,std::move(portal)}}};
    world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
    world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});
    routing.set_route_goal_validator([&](const odh::VmFrame&,odh::RoutePoint point) {
        return point==odh::RoutePoint{28,12}&&grid.contains(point)&&grid.walkable(point);
    });
    routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
    require(vm.run(1)==odh::VmStatus::waiting&&routing.active()&&portal_plans==0,
            "saved Go Here starts as a deferred cross-room route without walking toward a remote grid point");
    require(routing.tick(1)&&routing.external_frame_active()&&portal_plans==1,
            "saved Go Here schedules Portal15 before its within-room route");
    world.set_position(1,{16,12,1,0});
    require(vm.run(1)==odh::VmStatus::yielded&&routing.tick(2)&&refreshes==1&&portal_returns==1&&
            routing.active()&&!routing.external_frame_active()&&vm.status()==odh::VmStatus::waiting,
            "saved Go Here refreshes the room grid and resumes after Portal15 returns true");
    for(std::uint64_t tick=3;tick<32&&routing.active();++tick)routing.tick(tick);
    require(!routing.active()&&vm.status()==odh::VmStatus::returned_true&&
            world.find(1)->position->x==28&&world.find(1)->position->y==12&&
            context_ends==1,
            "saved Go Here completes its exact destination after one bounded room transition");
}
void ignore_rooms_cross_room_test() {
    odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();
    slots.records[1].flags=0x0801;
    slots.records[1].facing=-3;
    std::size_t portal_plans=0,local_goal_plans=0,goal_checks=0;
    odh::PreRankedSlotGoalProvider goals=[&](const auto&,std::size_t,const auto&,const auto&) {
        ++local_goal_plans;
        return std::vector<odh::PreRankedSlotGoal>{{{20,12},0x01,2}};
    };
    odh::Routing routing{world,grid,slots,goals};
    routing.set_room_pair_provider([](const auto&,const auto&)
        ->std::optional<std::pair<std::uint16_t,std::uint16_t>> {return std::pair{3,1};});
    routing.set_route_goal_validator([&](const auto&,odh::RoutePoint point) {
        ++goal_checks;return point==odh::RoutePoint{20,12};
    });
    routing.set_room_portal_step_provider([&](auto,const auto&,const auto&,const auto&,const auto&,auto) {
        ++portal_plans;
        return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::call_ready,
            odh::VmCall{odh::routine_key(4097),{1,2,2,9},{}},{}};
    });
    odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive()};
    world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
    world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});routing.bind(vm);
    vm.start(odh::routine_key(4096),{1,2,2});
    require(vm.run(2)==odh::VmStatus::waiting&&routing.active()&&local_goal_plans==1&&
            goal_checks==1&&portal_plans==0,
            "cross-room IgnoreRooms SLOT uses local goal validation without scheduling portals");
    for(std::uint64_t tick=1;tick<=32&&routing.active();++tick)routing.tick(tick);
    require(!routing.active()&&vm.status()==odh::VmStatus::returned_true&&
            world.find(1)->position->x==20&&portal_plans==0,
            "IgnoreRooms completes through the local active-grid route while portal planning stays unused");
}
void child_shy_sitting_preflight_tests() {
    {
        odh::World world;odh::RouteGrid grid{256,256};auto globals=child_shy_global_slots();
        bool provider_called=false;odh::RandomState random(0);
        odh::Routing routing{world,grid,globals};
        routing.set_ranged_slot_goal_provider([&](const auto& table,std::size_t index,
            const auto& caller,const auto& target) {
            provider_called=true;
            return odh::rank_reference_range_slot(table,index,caller,target,random,
                odh::is_child_shy_sitting_fallback_slot(table,index));
        });
        odh::Vm vm{routines(2,0,10),world.vm_memory(),{},routing.primitive()};
        world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::avatar,{0,88,0});
        world.set_position(1,{4,12,1,0});world.set_position(2,{100,100,1,0});
        routing.set_route_goal_validator([&](const odh::VmFrame&,odh::RoutePoint point) {
            return grid.contains(point)&&grid.walkable(point);
        });
        routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
        require(vm.run(8)==odh::VmStatus::fault&&!provider_called&&!routing.active()&&
                vm.diagnostic().find("nearby-chair preflight")!=std::string::npos,
            "exact Global SLOT100 type3 index10 remains unsupported without chair preflight");
    }
    {
        odh::World world;odh::RouteGrid grid{256,256};auto globals=child_shy_global_slots();
        bool provider_called=false;odh::RandomState random(0);
        odh::Routing routing{world,grid,globals};
        routing.set_ranged_slot_goal_provider([&](const auto& table,std::size_t index,
            const auto& caller,const auto& target) {
            provider_called=true;
            return odh::rank_reference_range_slot(table,index,caller,target,random,true);
        });
        routing.set_sitting_slot_fallback_preflight([](const auto&,std::size_t,const auto&,const auto&) {
            return std::optional<std::string>{"nearby object exposes EntryPoint26"};
        });
        odh::Vm vm{routines(2,0,10),world.vm_memory(),{},routing.primitive()};
        world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::avatar,{0,88,0});
        world.set_position(1,{4,12,1,0});world.set_position(2,{100,100,1,0});
        routing.set_route_goal_validator([&](const odh::VmFrame&,odh::RoutePoint point) {
            return grid.contains(point)&&grid.walkable(point);
        });
        routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
        require(vm.run(8)==odh::VmStatus::fault&&!provider_called&&!routing.active()&&
                vm.diagnostic().find("nearby object exposes EntryPoint26")!=std::string::npos,
            "a nearby chair keeps the sitting preference and never falls through to standing goals");
    }
    {
        odh::World world;odh::RouteGrid grid{256,256};auto globals=child_shy_global_slots();
        std::size_t movement_callbacks=0;bool provider_called=false;odh::RandomState random(0);
        odh::Routing routing{world,grid,globals};
        routing.set_ranged_slot_goal_provider([&](const auto& table,std::size_t index,
            const auto& caller,const auto& target) {
            provider_called=true;
            return odh::rank_reference_range_slot(table,index,caller,target,random,true);
        });
        routing.set_sitting_slot_fallback_preflight([](const auto& table,std::size_t index,
            const auto&,const auto&) -> std::optional<std::string> {
            if(!odh::is_child_shy_sitting_fallback_slot(table,index))
                return "unverified child-shy sitting profile";
            return {};
        });
        odh::Vm vm{routines(2,0,10),world.vm_memory(),{},routing.primitive()};
        world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::avatar,{0,88,0});
        world.set_position(1,{4,12,1,0});world.set_position(2,{100,100,1,0});
        routing.set_route_goal_validator([&](const odh::VmFrame&,odh::RoutePoint point) {
            return grid.contains(point)&&grid.walkable(point);
        });
        routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
        require(vm.run(8)==odh::VmStatus::waiting&&provider_called&&routing.active(),
            "standing goals are generated only after the host confirms no nearby sitting target");
        for(std::uint64_t tick=1;tick<=512&&routing.active();++tick)
            routing.tick(tick,[&](const auto&){++movement_callbacks;});
        require(vm.status()==odh::VmStatus::returned_true&&!routing.active()&&movement_callbacks>0,
            "verified standing fallback performs real route movement in a constructed World");
    }
}
}
int main() {
    try {
        reference_slot_lattice_tests();
        reference_slot_direction_tests();
        reference_slot_circle_tests();
        pre_ranked_goal_tests();
        door_range_route_tests();
        route_frame_context_tests();
        shared_world_dual_controller_tests();
        household_resident_route_owner_tests();
        household_go_here_session_tests();
        deferred_room_route_tests();
        saved_go_here_cross_room_test();
        ignore_rooms_cross_room_test();
        child_shy_sitting_preflight_tests();
        {odh::World world;odh::RouteGrid grid{64,32};auto globals=slot();
         odh::PreRankedSlotGoalProvider goals=[](const auto&,std::size_t,const auto&,const auto&) {
             return std::vector<odh::PreRankedSlotGoal>{{{44,12},4,7,2}};
         };
         odh::Routing routing{world,grid,globals,goals};
         odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});grid.block({28,12});routing.bind(vm);
         vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(10)==odh::VmStatus::waiting&&routing.active_sitting_object()==2,
                 "Route45 keeps the selected sitting object attached while the avatar walks");
         for(std::uint64_t tick=1;tick<=64&&routing.active();++tick)routing.tick(tick);
         require(vm.status()==odh::VmStatus::returned_false&&!routing.active()&&!routing.active_sitting_object(),
                 "Route45 fails closed and clears a sitting goal when no EntryPoint26 provider exists");}
        {odh::World world;odh::RouteGrid grid{64,32};auto globals=slot();
         odh::PreRankedSlotGoalProvider goals=[](const auto&,std::size_t,const auto&,const auto&) {
             return std::vector<odh::PreRankedSlotGoal>{{{12,12},
                 odh::reference_slot_entry_flags({28,12},0,{12,12},0x55),7,2}};
         };
         odh::Routing routing{world,grid,globals,goals};
         odh::RoutineSet owner{{odh::routine_key(4097),odh::Bhav{0x8002,
             {{2,254,255,{0,0,0,0,0,2,7,7}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
         odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive(),{{9,owner}}};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});grid.block({28,12});
         routing.set_sitting_action_provider([](std::int16_t actor,std::int16_t chair)->std::optional<odh::VmCall> {
             return odh::VmCall{odh::routine_key(4097),{actor,chair,chair,9},{}};
         });
         routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(10)==odh::VmStatus::waiting&&routing.active(),"Route45 starts a route to a sitting goal");
         std::uint64_t tick=1;
         for(;tick<=32&&routing.active()&&!routing.external_frame_active();++tick)routing.tick(tick);
         const auto action_result=vm.run(1);
         require(routing.external_frame_active()&&action_result==odh::VmStatus::yielded,
                 "a chair EntryPoint26 success is awaited as a child VM action");
         require(routing.tick(tick)&&!routing.active()&&vm.status()==odh::VmStatus::returned_true,
                 "Route45 completes only after EntryPoint26 returns true");}
        {odh::World world;odh::RouteGrid grid{64,32};auto globals=slot();
         const auto goal=[&](odh::RoutePoint point,std::optional<std::int16_t> chair) {
             const auto flags=odh::reference_slot_entry_flags({28,12},0,point,0x55);
             return odh::PreRankedSlotGoal{point,flags,7,chair};
         };
         odh::PreRankedSlotGoalProvider goals=[&](const auto&,std::size_t,const auto&,const auto&) {
             return std::vector<odh::PreRankedSlotGoal>{goal({12,12},2),goal({20,12},{})};
         };
         odh::Routing routing{world,grid,globals,goals};
         const auto action=odh::routine_key(4097);
         odh::RoutineSet owner{{action,odh::Bhav{0x8002,
             {{46,1,255,{}},{2,254,255,{0,0,0,0,0,2,7,7}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
         odh::Vm vm{routines(),world.vm_memory(),{},
             [primitive=routing.primitive()](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch) {
                 if(instruction.opcode==46)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
                 return primitive(frame,instruction,temps,dispatch);
             },{{9,owner}}};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});grid.block({28,12});
         routing.set_sitting_action_provider([&](std::int16_t actor,std::int16_t chair)->std::optional<odh::VmCall> {
             if(actor!=1||chair!=2)return {};
             return odh::VmCall{action,{actor,chair,chair,9},{}};
         });
         routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(10)==odh::VmStatus::waiting&&routing.active()&&routing.active_sitting_object()==2,
                 "Route45 starts at the ranked chair destination");
         std::uint64_t tick=1;
         for(;tick<=32&&routing.active()&&!routing.external_frame_active();++tick)routing.tick(tick);
         require(routing.external_frame_active()&&routing.active_sitting_object()==2&&vm.run(1)==odh::VmStatus::waiting,
                 "Route45 calls the selected chair action only after reaching its destination");
         const auto child_ticket=vm.pending()->ticket;
         const auto child_completed=vm.complete(child_ticket,false);
         const auto child_status=vm.run(1);
         const auto route_tick=routing.tick(tick++);
         require(child_completed&&child_status==odh::VmStatus::yielded&&route_tick&&
                 routing.active()&&!routing.active_sitting_object(),
                 "a failed chair action selects the next ranked Route45 destination");
         for(;tick<=64&&routing.active();++tick)routing.tick(tick);
         require(!routing.active()&&vm.status()==odh::VmStatus::returned_true&&world.find(1)->position->x==20,
                 "Route45 succeeds at the alternate standing destination after EntryPoint26 returns false");}
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
         const std::array<std::uint8_t,8> go_here{10,0,254,255,10,0,5,0};
         auto code=goto_relative(go_here);
         code.emplace(odh::routine_key(398),odh::Bhav{0x8002,
             {{2,254,255,{0,0,0,0,0,2,7,7}}},{},odh::Bhav::Metadata{0,4,0,0}});
         odh::Vm vm{std::move(code),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,2});world.set_position(2,{12,12,1,5});
         routing.set_route_goal_validator([&](const odh::VmFrame&,odh::RoutePoint p) {
             return grid.contains(p)&&grid.walkable(p);
         });
         routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(1)==odh::VmStatus::waiting&&routing.tick(1)&&
                 world.find(1)->position->x==5&&routing.interrupt()&&routing.active()&&
                 routing.external_frame_active()&&vm.stack().back().key.id==398&&
                 vm.stack().back().arguments==std::vector<std::int16_t>{4,0,0,0},
                 "player interrupt stops an active Go Here after its last step and enters Global 398 as Interrupted");
         require(vm.run(1)==odh::VmStatus::yielded&&routing.tick(2)&&!routing.active()&&
                 vm.status()==odh::VmStatus::returned_false&&world.find(1)->position->x==5&&
                 routing.diagnostic()=="route interrupted by player"&&vm.run(1)==odh::VmStatus::returned_false,
                 "interrupted Go Here completes false without teleporting after the game's failure tree");}
        {odh::World world;odh::RouteGrid grid{32,32};auto globals=slot();odh::Routing routing{world,grid,globals};
         const std::array<std::uint8_t,8> go_here{10,0,254,255,10,0,5,0};
         auto code=goto_relative(go_here);
         code.emplace(odh::routine_key(398),odh::Bhav{0x8002,
             {{2,254,255,{0,0,0,0,0,2,7,7}}},{},odh::Bhav::Metadata{0,4,0,0}});
         odh::RoutineSet child_code{{odh::routine_key(4097),odh::Bhav{0x8002,
             {{46,1,1,{0,0,0,0,0,0,0,0}},
              {2,254,255,{0,0,0,0,0,2,7,7}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
         const auto route_primitive=routing.primitive();
         odh::Vm vm{std::move(code),world.vm_memory(),{},
             [route_primitive](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch) {
                 if(instruction.opcode==46)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
                 return route_primitive(frame,instruction,temps,dispatch);
             },{{9,std::move(child_code)}}};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,2});world.set_position(2,{12,12,1,5});
         routing.set_route_goal_validator([&](const odh::VmFrame&,odh::RoutePoint p) {
             return grid.contains(p)&&grid.walkable(p);
         });
         routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(1)==odh::VmStatus::waiting&&routing.active(),
                 "Go Here route is waiting before its child action starts");
         require(routing.call_external_frame({odh::routine_key(4097),{1,2,2,9},{}}),
                 "Go Here accepts a route-owned child action");
         require(vm.run(1)==odh::VmStatus::waiting,
                 "Go Here can be suspended while one of its route-owned child actions is active");
         const auto child_ticket=vm.pending()->ticket;
         require(routing.interrupt()&&routing.active()&&routing.external_frame_active()&&
                 vm.complete(child_ticket,true)&&vm.run(1)==odh::VmStatus::yielded&&
                 vm.external_frame_return()&&routing.tick(1)&&routing.active()&&
                 routing.external_frame_active()&&vm.stack().back().key.id==398&&
                 vm.stack().back().arguments==std::vector<std::int16_t>{4,0,0,0},
                 "Backspace waits for the child action to return, then starts Go Here's failure action");
         require(vm.run(1)==odh::VmStatus::yielded&&routing.tick(2)&&!routing.active()&&
                 vm.status()==odh::VmStatus::returned_false&&
                 routing.diagnostic()=="route interrupted by player",
                 "deferred Go Here cancellation finishes after Global 398 returns");}
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
         const std::array<std::uint8_t,8> in_front_facing{0,0,0,254,0,0,0,0};
         odh::Vm vm{goto_relative(in_front_facing),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,24,1,0});world.set_position(2,{20,24,1,0});
         odh::RandomState random(7);
         routing.set_ranged_slot_goal_provider([&](const auto& table,std::size_t index,
             const auto& caller,const auto& target) {
             return odh::rank_reference_range_slot(table,index,caller,target,random);
         });
         routing.set_route_goal_validator([&](const auto&,odh::RoutePoint p) {
             return grid.contains(p)&&grid.walkable(p);
         });
         routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         auto initial_status=vm.run(8);
         for(std::size_t run=0;run<4&&initial_status==odh::VmStatus::yielded;++run)
             initial_status=vm.run(8);
         require(initial_status==odh::VmStatus::waiting&&routing.active(),
                 "BHAV8215 InFrontOf/Facing builds ranked relative approach goals");
         for(std::uint64_t tick=1;tick<=32&&routing.active();++tick)routing.tick(tick);
         const auto* avatar=world.find(1);
         const auto dx=avatar->position->x-20,dy=avatar->position->y-24;
         require(vm.status()==odh::VmStatus::returned_true&&!routing.active()&&
                 dx==0&&dy==-16&&avatar->position->direction==4,
                 "InFrontOf/Facing walks to a north-front lattice goal and turns toward the object");}
        {odh::World world;odh::RouteGrid grid{32,32};auto globals=slot();odh::Routing routing{world,grid,globals};
         const std::array<std::uint8_t,8> go_here{10,0,254,255,10,0,5,0};
         odh::Vm vm{goto_relative(go_here),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,2});world.set_position(2,{12,12,1,5});
         routing.set_route_goal_validator([&](const odh::VmFrame& frame,odh::RoutePoint p) {
             const auto* actor=world.find(frame.context.caller);const auto* target=world.find(frame.context.stack_object);
             return actor&&target&&actor->position&&target->position&&
                 actor->position->level==target->position->level&&
                 p==odh::RoutePoint{target->position->x,target->position->y}&&grid.contains(p)&&grid.walkable(p);
         });routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(4)==odh::VmStatus::waiting&&routing.active(),
                 "saved Destination Go Here GotoRelativePosition schedules the exact selected point");
         for(std::uint64_t tick=1;tick<=8&&routing.active();++tick)routing.tick(tick);
         require(vm.status()==odh::VmStatus::returned_true&&!routing.active()&&
                 world.find(1)->position->x==12&&world.find(1)->position->y==12&&
                 world.find(1)->position->direction!=world.find(2)->position->direction,
                 "Go Here walks to target in steps and leaves arrival facing unrestricted");}
        for(const bool destination_walkable:{false,true})
        for(const bool failure_tree_returns_true:{false,true}) {
         odh::World world;odh::RouteGrid grid{32,32};auto globals=slot();odh::Routing routing{world,grid,globals};
         const std::array<std::uint8_t,8> go_here{10,0,254,255,10,0,5,0};
         auto code=goto_relative(go_here);
         code.emplace(odh::routine_key(398),odh::Bhav{0x8002,
             {{2,254,255,{0,0,static_cast<std::uint8_t>(failure_tree_returns_true?0:1),0,0,2,7,7}}},
             {},odh::Bhav::Metadata{0,4,0,0}});
         odh::Vm vm{std::move(code),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,2});world.set_position(2,{12,12,1,5});
         if(destination_walkable) {
             for(std::int32_t y=0;y<32;++y)grid.block({8,y});
         } else grid.block({12,12});
         routing.set_route_goal_validator([&](const odh::VmFrame&,odh::RoutePoint p) {
             return grid.contains(p)&&grid.walkable(p);
         });
         routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         vm.temps()[1]=failure_tree_returns_true?0:1;
         require(vm.run(1)==odh::VmStatus::waiting&&routing.active(),
                 "blocked saved Go Here waits for its failure routine instead of skipping it");
         require(routing.tick(1)&&routing.external_frame_active()&&vm.stack().back().key.id==398&&
                 vm.stack().back().arguments==std::vector<std::int16_t>{
                     static_cast<std::int16_t>(destination_walkable?3:7),0,0,0},
                 "Go Here calls global 398 with NoPath or NoValidGoals and no identified blocker");
         const auto failure_return=vm.run(1);
         require(failure_return==odh::VmStatus::yielded&&vm.external_frame_return(),
                 "global 398 returns to the route service at a scheduler boundary");
         const auto failure_resumed=routing.tick(2);
         require(failure_resumed&&!routing.active()&&vm.status()==odh::VmStatus::returned_false,
                 ("route service resumes after global 398 and completes the original wait (tick="+
                  std::to_string(failure_resumed)+", active="+std::to_string(routing.active())+
                  ", status="+std::to_string(static_cast<int>(vm.status()))+", diag="+
                  vm.diagnostic()+", route="+routing.diagnostic()+")").c_str());
         require(vm.run(1)==odh::VmStatus::returned_false,
                 "Go Here reports a failed walk whether its failure routine returns true or false");
         require(routing.diagnostic()==(destination_walkable?
                     "no path in supplied clearance grid":
                     "GotoRelativePosition target rejected by host room/clearance validator"),
                 "Go Here retains the route failure message after its callback frame is removed");
        }
        {odh::World world;odh::RouteGrid grid{32,32};auto globals=slot();odh::Routing routing{world,grid,globals};
         const std::array<std::uint8_t,8> go_here{10,0,254,255,10,0,5,0};
         auto code=goto_relative(go_here);
         code.emplace(odh::routine_key(398),odh::Bhav{0x8002,
             {{2,254,255,{0,0,0,0,0,2,7,7}}},{},odh::Bhav::Metadata{0,4,0,0}});
         odh::Vm vm{std::move(code),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,2});world.set_position(2,{12,12,1,5});
         routing.set_route_goal_validator([&](const odh::VmFrame&,odh::RoutePoint p) {
             return grid.contains(p)&&grid.walkable(p);
         });
         routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(1)==odh::VmStatus::waiting&&routing.active(),
                 "walkable Go Here first schedules a route without moving the avatar");
         grid.block({5,12});
         require(routing.tick(1)&&routing.external_frame_active()&&
                 vm.stack().back().arguments==std::vector<std::int16_t>{3,0,0,0}&&
                 world.find(1)->position->x==4,
                 "a newly blocked first step calls global 398 with NoPath and does not teleport");
         require(vm.run(1)==odh::VmStatus::yielded&&routing.tick(2)&&!routing.active()&&
                 vm.status()==odh::VmStatus::returned_false&&world.find(1)->position->x==4,
                 "route failure returns false after global 398 while preserving the avatar position");}
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
        {odh::World world;odh::RouteGrid grid{32,32};auto globals=slot();odh::Routing routing{world,grid,globals};
         const auto operands=std::array<std::uint8_t,8>{0,0,254,4,0,0,0,0};
         odh::RoutineSet code{{odh::routine_key(4112),odh::Bhav{0x8002,{{27,254,255,operands}},{},odh::Bhav::Metadata{0,4,0,0}}}};
         odh::Vm vm{std::move(code),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{12,12,1,0});
         routing.set_stair_goto_relative_validator([](const odh::VmFrame& frame,const odh::Instruction&) {
             return frame.key.id==4112&&frame.context.stack_object==2;
         });
         routing.set_route_goal_validator([&](const odh::VmFrame&,odh::RoutePoint p) {
             return grid.contains(p)&&grid.walkable(p);
         });
         routing.bind(vm);vm.start(odh::routine_key(4112),{1,2,2});
         require(vm.run(4)==odh::VmStatus::waiting&&routing.active(),
                 "the exact stair BHAV profile is accepted only after host identity validation");
         for(std::uint64_t tick=1;tick<=16&&routing.active();++tick)routing.tick(tick);
         require(vm.status()==odh::VmStatus::returned_true&&!routing.active()&&
                 world.find(1)->position->x==12&&world.find(1)->position->y==12,
                 "stair exact-point profile reaches the endpoint without changing floors");}
        {odh::World world;odh::RouteGrid grid{32,32};auto globals=slot();odh::Routing routing{world,grid,globals};
         const auto operands=std::array<std::uint8_t,8>{0,0,254,0,0,0,0,0};
         odh::RoutineSet code{{odh::routine_key(4112),odh::Bhav{0x8002,{{27,254,255,operands}},{},odh::Bhav::Metadata{0,4,0,0}}}};
         odh::Vm vm{std::move(code),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{12,12,1,0});
         routing.set_stair_goto_relative_validator([](const odh::VmFrame& frame,const odh::Instruction&) {
             return frame.key.id==4112&&frame.context.code_owner==99&&frame.context.stack_object==99;
         });
         routing.bind(vm);vm.start(odh::routine_key(4112),{1,2,2});
         require(vm.run(2)==odh::VmStatus::fault&&
                 vm.diagnostic().find("outside the synthetic route profiles")!=std::string::npos,
                 "stair profile rejects a different code owner or endpoint");}
        {odh::World world;odh::RouteGrid grid{32,32};auto globals=slot();odh::Routing routing{world,grid,globals};
         const auto operands=std::array<std::uint8_t,8>{0,0,254,0,0,0,0,0};
         odh::RoutineSet code{{odh::routine_key(4112),odh::Bhav{0x8002,{{27,254,255,operands}},{},odh::Bhav::Metadata{0,4,0,0}}}};
         odh::Vm vm{std::move(code),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{12,12,1,0});grid.block({12,12});
         routing.set_stair_goto_relative_validator([](const odh::VmFrame&,const odh::Instruction&){return true;});
         routing.set_route_goal_validator([&](const odh::VmFrame&,odh::RoutePoint p) {
             return grid.contains(p)&&grid.walkable(p);
         });
         routing.bind(vm);vm.start(odh::routine_key(4112),{1,2,2});
         require(vm.run(2)==odh::VmStatus::fault&&
                 vm.diagnostic().find("stair GotoRelativePosition failure tree is not implemented")!=std::string::npos,
                 "stair no-path remains explicitly unsupported instead of pretending failure-tree behavior");}
        {odh::World world;odh::RouteGrid grid{32,32};auto globals=slot();odh::Routing routing{world,grid,globals};
         const auto operands=std::array<std::uint8_t,8>{0,0,254,0,0,0,0,0};
         odh::RoutineSet code{{odh::routine_key(4112),odh::Bhav{0x8002,{{27,254,255,operands}},{},odh::Bhav::Metadata{0,4,0,0}}}};
         odh::Vm vm{std::move(code),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{12,12,2,0});
         routing.set_stair_goto_relative_validator([](const odh::VmFrame&,const odh::Instruction&){return true;});
         routing.set_route_goal_validator([](const odh::VmFrame&,odh::RoutePoint){return true;});
         routing.bind(vm);vm.start(odh::routine_key(4112),{1,2,2});
         require(vm.run(2)==odh::VmStatus::fault&&
                 vm.diagnostic().find("target on one level")!=std::string::npos,
                 "stair route profile preserves the general same-floor guard");}
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
         vm.start(odh::routine_key(4096),{1,1,1});
         require(vm.run(1)==odh::VmStatus::waiting&&routing.active(),
                 "self-target Route45 service waits before scheduling its host-resolved portal BHAV");
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
         odh::Routing routing{world,grid,slots};
         const auto return_bhav=[](bool result) {
             const auto lhs=static_cast<std::uint8_t>(result?0:1);
             const auto rhs=static_cast<std::uint8_t>(0);
             return odh::Bhav{0x8002,{{2,254,255,{lhs,0,rhs,0,0,2,7,7}}},
                 {},odh::Bhav::Metadata{0,4,0,0}};
         };
         odh::RoutineSet owner_failed{{odh::routine_key(4097),return_bhav(false)}};
         odh::RoutineSet owner_alternate{{odh::routine_key(4097),return_bhav(true)}};
         odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive(),
             {{9,std::move(owner_failed)},{10,std::move(owner_alternate)}}};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});grid.block({28,12});
         std::size_t retry_calls=0;
         routing.set_portal_retry_provider([&](const odh::VmCall& failed,const odh::EntityPosition& actor,
             const odh::EntityPosition& target,std::size_t retry_index)->std::optional<odh::VmCall> {
             ++retry_calls;
             require(failed.context.code_owner==9&&failed.context.callee==2&&retry_index==1,
                     "portal retry receives the exact failed edge identity and bounded attempt number");
             require(actor==odh::EntityPosition{4,12,1,0}&&target==odh::EntityPosition{28,12,1,0},
                     "portal retry receives current participant positions for route replanning");
             return odh::VmCall{odh::routine_key(4097),{1,2,2,10},{}};
         });
         routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(1)==odh::VmStatus::waiting&&routing.active(),
                 "Route45 parent waits before its first Portal15 attempt");
         const auto route_ticket=vm.pending()->ticket;
         require(routing.call_external_frame({odh::routine_key(4097),{1,2,2,9},{}})&&
                 vm.run(1)==odh::VmStatus::yielded&&vm.external_frame_return()&&
                 !vm.external_frame_return()->result,
                 "first Portal15 attempt returns false to the suspended route");
         require(routing.tick(1)&&retry_calls==1&&routing.external_frame_active()&&
                 vm.status()==odh::VmStatus::running,
                 "false portal return replans and schedules one alternate EntryPoint frame");
         require(vm.run(1)==odh::VmStatus::yielded&&vm.external_frame_return()&&
                 vm.external_frame_return()->ticket==route_ticket&&vm.external_frame_return()->code_owner==10&&
                 vm.external_frame_return()->result,
                 "alternate Portal15 frame returns under its independently checked code owner");
         require(routing.tick(2)&&vm.status()==odh::VmStatus::waiting&&vm.pending()->ticket==route_ticket&&
                 routing.active()&&!routing.external_frame_active(),
                 "successful alternate portal resumes the original Route45 wait");
         for(std::uint64_t tick=3;tick<=40&&routing.active();++tick)routing.tick(tick);
         require(vm.status()==odh::VmStatus::returned_true&&!routing.active(),
                 "Route45 continues to its local destination after alternate portal success");}
        {odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();
         odh::PreRankedSlotGoalProvider goals=[](const auto&,std::size_t,const auto&,const auto& target) {
             const odh::RoutePoint point{20,12};
             return std::vector<odh::PreRankedSlotGoal>{{point,
                 odh::reference_slot_entry_flags({target.x,target.y},target.direction,point,0x55),2}};
         };
         odh::Routing routing{world,grid,slots,goals};std::size_t refreshes=0;
         routing.set_route_grid_refresh([&](const odh::EntityPosition& position) {
             ++refreshes;return position==odh::EntityPosition{8,12,1,0};
         });
         const auto return_bhav=[](bool result) {
             const auto lhs=static_cast<std::uint8_t>(result?0:1);
             return odh::Bhav{0x8002,{{2,254,255,{lhs,0,0,0,0,2,7,7}}},
                 {},odh::Bhav::Metadata{0,4,0,0}};
         };
         odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive(),
             {{9,{{odh::routine_key(4097),return_bhav(false)}}},
              {10,{{odh::routine_key(4097),return_bhav(true)}}}}};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});routing.bind(vm);
         routing.set_portal_retry_provider([](const odh::VmCall&,const auto&,const auto&,std::size_t)
             ->std::optional<odh::VmCall> {return odh::VmCall{odh::routine_key(4097),{1,2,2,10},{}};});
         vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(1)==odh::VmStatus::waiting&&routing.active(),
                 "moved failed portal test establishes a pending parent route");
         require(routing.call_external_frame({odh::routine_key(4097),{1,2,2,9},{}}),
                 "moved failed portal test schedules its first external frame");
         world.set_position(1,{8,12,1,0});
         require(vm.run(1)==odh::VmStatus::yielded&&vm.external_frame_return()&&
                 !vm.external_frame_return()->result&&routing.tick(1)&&refreshes==1,
                 "failed portal movement refreshes the active room grid before retry");
         require(vm.run(1)==odh::VmStatus::yielded&&vm.external_frame_return()&&
                 vm.external_frame_return()->result&&routing.tick(2)&&routing.active(),
                 "stationary successful alternative returns to the original parent route");
         require(routing.tick(3)&&world.find(1)->position->x==9&&refreshes==1,
                 "parent replans from refreshed post-failure position instead of using its stale path");
         for(std::uint64_t tick=4;tick<=32&&routing.active();++tick)routing.tick(tick);
         require(!routing.active()&&vm.status()==odh::VmStatus::returned_true&&
                 world.find(1)->position->x==20,
                 "replanned parent reaches its original ranked SLOT goal and completes true");}
        {odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();odh::Routing routing{world,grid,slots};
         auto owner=odh::RoutineSet{{odh::routine_key(4097),odh::Bhav{0x8002,
             {{2,254,255,{1,0,0,0,0,2,7,7}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
         odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive(),{{9,std::move(owner)}}};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});grid.block({28,12});
         routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(1)==odh::VmStatus::waiting&&
                 routing.call_external_frame({odh::routine_key(4097),{1,2,2,9},{}})&&
                 vm.run(1)==odh::VmStatus::yielded&&vm.external_frame_return()&&
                 !vm.external_frame_return()->result,
                 "unavailable alternate-path case begins with a clean false portal return");
         require(routing.tick(1)&&!routing.active()&&vm.run(1)==odh::VmStatus::returned_false,
                 "false Portal15 with no retry provider fails the waiting Route45 cleanly");}
        {odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();odh::Routing routing{world,grid,slots};
         auto owner=odh::RoutineSet{{odh::routine_key(4097),odh::Bhav{0x8002,
             {{2,254,255,{1,0,0,0,0,2,7,7}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
         odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive(),{{9,std::move(owner)}}};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});grid.block({28,12});
         std::size_t retry_calls=0;
         routing.set_portal_retry_provider([&](const odh::VmCall&,const odh::EntityPosition&,
             const odh::EntityPosition&,std::size_t)->std::optional<odh::VmCall> {
             ++retry_calls;return std::nullopt;
         });
         routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(1)==odh::VmStatus::waiting&&
                 routing.call_external_frame({odh::routine_key(4097),{1,2,2,9},{}}),
                 "moved-avatar retry test starts with a suspended Route45 parent");
         world.set_position(1,{5,12,1,0});
         require(vm.run(1)==odh::VmStatus::yielded&&vm.external_frame_return()&&
                 !vm.external_frame_return()->result,
                 "failed Portal15 returns false after an external avatar move");
         require(routing.tick(1)&&!routing.active()&&retry_calls==0&&
                 vm.run(1)==odh::VmStatus::returned_false,
                 "moved avatar requires a successful route-grid refresh before retry planning");}
        {odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();odh::Routing routing{world,grid,slots};
         auto owner=odh::RoutineSet{{odh::routine_key(4097),odh::Bhav{0x8002,
             {{2,254,255,{1,0,0,0,0,2,7,7}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
         odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive(),{{9,std::move(owner)}}};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});
         std::size_t retry_calls=0;
         routing.set_route_grid_refresh([&](const odh::EntityPosition&) {return world.remove(1);});
         routing.set_portal_retry_provider([&](const auto&,const auto&,const auto&,auto)
             ->std::optional<odh::VmCall> {++retry_calls;return std::nullopt;});
         routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(1)==odh::VmStatus::waiting&&
                 routing.call_external_frame({odh::routine_key(4097),{1,2,2,9},{}}),
                 "refresh invalidation test starts with an active external portal");
         world.set_position(1,{5,12,1,0});
         require(vm.run(1)==odh::VmStatus::yielded&&routing.tick(1)&&!routing.active()&&
                 !world.find(1)&&retry_calls==0&&vm.run(1)==odh::VmStatus::returned_false,
                 "route-grid refresh that removes its actor is revalidated before retry planning");}
        {odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();odh::Routing routing{world,grid,slots};
         auto owner=odh::RoutineSet{{odh::routine_key(4097),odh::Bhav{0x8002,
             {{2,254,255,{1,0,0,0,0,2,7,7}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
         odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive(),{{9,std::move(owner)}}};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});grid.block({28,12});
         std::size_t retry_calls=0;
         routing.set_portal_retry_provider([&](const odh::VmCall& failed,const odh::EntityPosition&,
             const odh::EntityPosition&,std::size_t retry_index)->std::optional<odh::VmCall> {
             ++retry_calls;
             require(retry_index==retry_calls&&failed.context.code_owner==9,
                     "portal retries advance monotonically and preserve failed CodeOwner context");
             return failed;
         });
         routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(1)==odh::VmStatus::waiting&&
                 routing.call_external_frame({odh::routine_key(4097),{1,2,2,9},{}}),
                 "bounded retry test starts with a suspended Route45 parent");
         for(std::uint64_t tick=1;tick<=33;++tick) {
             require(vm.run(1)==odh::VmStatus::yielded&&vm.external_frame_return()&&
                     !vm.external_frame_return()->result,
                     "each bounded portal attempt returns false under the same parent ticket");
             require(routing.tick(tick),"portal retry scheduler accepts the next consecutive tick");
         }
         require(retry_calls==32&&!routing.active()&&vm.run(1)==odh::VmStatus::returned_false,
                 "portal retry limit schedules at most 32 alternatives and then fails closed");}
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
             return std::vector<odh::PreRankedSlotGoal>{};
         });
         const auto actor_before=*f.world.find(1);
         f.start();const auto* actor=f.world.find(1);
         require(f.vm.status()==odh::VmStatus::fault&&!f.routing.active()&&
                 actor->generation==actor_before.generation&&actor->position==actor_before.position&&
                 actor->radian_direction==actor_before.radian_direction&&
                 actor->person_data[37]==actor_before.person_data[37]&&
                 actor->static_footprint==actor_before.static_footprint&&
                 f.vm.diagnostic()=="pre-ranked SLOT goal provider returned no candidates",
                 "empty ranked provider fails clearly without changing the actor state");}
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
        {Fixture f;f.start();
         for(std::uint64_t tick=1;tick<=16&&f.routing.active();++tick)f.routing.tick(tick);
         require(f.vm.status()==odh::VmStatus::returned_true&&!f.routing.active(),
                 "first independent Route45 job completes");
         require(f.world.set_position(2,{36,12,1,0}),"second Route45 destination can move after idle period");
         f.vm.start(odh::routine_key(4096),{1,2,2});
         require(f.vm.run(10)==odh::VmStatus::waiting&&f.routing.active(),
                 "a second independent Route45 job can start after the first completes");
         require(f.routing.tick(100)&&f.routing.active()&&f.world.find(1)->position->x==13,
                 "new root route accepts a later host tick after an idle gap");
         require(f.routing.tick(101)&&f.routing.active(),
                 "consecutive host ticks remain required while the new route is active");}
        {Fixture f;f.vm.start(odh::routine_key(4096),{1,1,1});
         require(f.vm.run(10)==odh::VmStatus::waiting&&f.routing.active(),
                 "Route45 supports a caller routing relative to its own SLOT");
         for(std::uint64_t tick=1;tick<=64&&f.routing.active();++tick)f.routing.tick(tick);
         require(f.vm.status()==odh::VmStatus::returned_true&&!f.routing.active()&&
                 f.world.find(1)->position->x!=4&&f.world.find(1)->position->y!=12,
                 "self-target route keeps its initial SLOT center while the avatar moves");}
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
